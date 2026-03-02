#include "polonio/runtime/template_renderer.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "polonio/common/error.h"
#include "polonio/common/location.h"
#include "polonio/common/source.h"
#include "polonio/lexer/lexer.h"
#include "polonio/parser/parser.h"
#include "polonio/runtime/interpreter.h"
#include "polonio/runtime/env.h"
#include "polonio/runtime/output.h"
#include "polonio/runtime/template_scanner.h"

namespace polonio {

namespace {

bool is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool is_ident_part(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

struct RenderState {
    Interpreter& interpreter;
    std::vector<std::filesystem::path> path_stack;
    static constexpr std::size_t kMaxDepth = 50;
};

struct PathGuard {
    std::vector<std::filesystem::path>& stack;
    explicit PathGuard(std::vector<std::filesystem::path>& s, std::filesystem::path path)
        : stack(s) {
        stack.push_back(std::move(path));
    }
    ~PathGuard() { stack.pop_back(); }
};

std::filesystem::path canonicalize(const std::filesystem::path& path) {
    try {
        return std::filesystem::weakly_canonical(path);
    } catch (...) {
        return std::filesystem::absolute(path);
    }
}

} // namespace

std::string process_text_segment(RenderState& state, const TemplateSegment& segment, const Source& source) {
    std::string output;
    output.reserve(segment.content.size());
    Location loc = segment.location;
    const std::string& text = segment.content;
    std::size_t i = 0;
    while (i < text.size()) {
        char ch = text[i];
        if (ch == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            Location comment_loc = loc;
            loc = advance(loc, '/');
            loc = advance(loc, '*');
            i += 2;
            bool closed = false;
            while (i < text.size()) {
                if (text[i] == '*' && i + 1 < text.size() && text[i + 1] == '/') {
                    loc = advance(loc, '*');
                    loc = advance(loc, '/');
                    i += 2;
                    closed = true;
                    break;
                }
                loc = advance(loc, text[i]);
                ++i;
            }
            if (!closed) {
                throw PolonioError(ErrorKind::Parse, "unterminated HTML comment", source.path(), comment_loc);
            }
            continue;
        }
        if (ch == '$') {
            Location dollar_loc = loc;
            loc = advance(loc, ch);
            if (i + 1 < text.size() && text[i + 1] == '$') {
                output.push_back('$');
                loc = advance(loc, '$');
                i += 2;
                continue;
            }
            if (i + 1 < text.size() && is_ident_start(text[i + 1])) {
                std::size_t j = i + 1;
                std::string name;
                while (j < text.size() && is_ident_part(text[j])) {
                    name.push_back(text[j]);
                    loc = advance(loc, text[j]);
                    j++;
                }
                auto env = state.interpreter.env();
                auto* value = env->find(name);
                if (!value) {
                    throw PolonioError(ErrorKind::Runtime,
                                       "undefined variable: " + name,
                                       source.path(),
                                       dollar_loc);
                }
                output += OutputBuffer::value_to_string(*value);
                i = j;
                continue;
            }
            output.push_back('$');
            ++i;
            continue;
        }
        output.push_back(ch);
        loc = advance(loc, ch);
        ++i;
    }
    return output;
}

bool is_inline_echo(const Program& program, EchoStmt** echo_out) {
    const auto& statements = program.statements();
    if (statements.size() != 1) {
        return false;
    }
    if (auto echo_stmt = std::dynamic_pointer_cast<EchoStmt>(statements[0])) {
        if (echo_out) {
            *echo_out = echo_stmt.get();
        }
        return true;
    }
    return false;
}

void execute_code_segment(RenderState& state, const TemplateSegment& segment, const Source& source) {
    Lexer lexer(segment.content, source.path());
    auto tokens = lexer.scan_all();
    Parser parser(tokens, source.path());
    auto program = parser.parse_program();
    EchoStmt* echo_stmt = nullptr;
    if (is_inline_echo(program, &echo_stmt)) {
        Value value = state.interpreter.eval_expr(echo_stmt->expr());
        state.interpreter.write_text(OutputBuffer::value_to_string(value));
        return;
    }
    state.interpreter.exec_program(program);
}

void render_source(RenderState& state, const Source& source, const std::filesystem::path& canonical_path) {
    PathGuard guard(state.path_stack, canonical_path);
    auto segments = scan_template(source);
    for (const auto& segment : segments) {
        if (segment.kind == TemplateSegment::Kind::Text) {
            state.interpreter.write_text(process_text_segment(state, segment, source));
        } else {
            execute_code_segment(state, segment, source);
        }
    }
}

std::string render_template_with_interpreter(const Source& source, Interpreter& interpreter) {
    interpreter.clear_output();
    RenderState state{interpreter, {}};
    auto root_path = canonicalize(source.path());
    interpreter.set_include_callback([&state](const std::string& include_path, const Location& loc) {
        if (state.path_stack.empty()) {
            throw PolonioError(ErrorKind::Runtime, "include not allowed", "", loc);
        }
        if (state.path_stack.size() >= RenderState::kMaxDepth) {
            throw PolonioError(ErrorKind::Runtime, "include depth exceeded", state.path_stack.back().string(), loc);
        }
        auto base_dir = state.path_stack.back().parent_path();
        auto candidate = (base_dir / include_path).lexically_normal();
        auto canonical_child = canonicalize(candidate);
        for (const auto& existing : state.path_stack) {
            if (existing == canonical_child) {
                throw PolonioError(ErrorKind::Runtime, "include cycle detected", state.path_stack.back().string(), loc);
            }
        }
        auto child_source = Source::from_file(candidate.string());
        Source normalized(canonical_child.string(), child_source.content());
        render_source(state, normalized, canonical_child);
    });
    render_source(state, source, root_path);
    return interpreter.output();
}

std::string render_template(const Source& source) {
    Interpreter interpreter(std::make_shared<Env>(), source.path());
    return render_template_with_interpreter(source, interpreter);
}

} // namespace polonio
