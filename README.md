# Polonio

Single-binary server-side templating language written in C++17, featuring a modern lexer/parser pipeline, interpreted runtime, and CGI-ready HTML templating.

## Features

- Custom lexer, Pratt expression parser, AST, and tree-walking interpreter
- Lexical scoping with closures and a rich standard library (string, array, object, math, type, date, output, HTTP)
- Template engine with `<% %>` code blocks, `$var` interpolation, `<% echo %>` inline output, and `include "file.pol"`
- CLI commands for running templates, showing help/version, and an in-progress development server stub
- CGI mode with automatic detection, HTTP superglobals, and response-control builtins
- Make-based build and doctest-powered test suite

## Quick Example

```pol
<% var name = "World" %>
<html>
  <body>
    <h1>Hello $name!</h1>
    <% if _SERVER["REQUEST_METHOD"] == "POST" %>
      <p>You posted: <% echo _POST["message"] %></p>
    <% else %>
      <form method="post">
        <input name="message">
        <button>Submit</button>
      </form>
    <% end %>
  </body>
</html>
```

Run it:

```bash
make
./build/polonio run hello.pol
```

## Build Instructions

```bash
git clone https://github.com/your-org/polonio.git
cd polonio
make          # builds build/polonio
make test     # builds and runs doctest suite
```

Requires a C++17-capable compiler and standard Unix build tools.

## CLI Usage

```
polonio help
polonio version
polonio run <file.pol>
polonio <file.pol>          # shorthand for run
polonio --dump-ast <expr>   # developer helper
polonio serve [--port N] [--root DIR]
                           # experimental dev server stub (responds "OK")
```

## Template Syntax Overview

- `<% ... %>`: execute Polonio code (control flow, variable declarations, etc.)
- `<% echo expr %>`: inline output inside HTML
- `$var` / `$identifier`: interpolate variables in text segments
- `<% include "partial.pol" %>`: render another template with shared interpreter state
- HTML comments `/* */` inside text segments are stripped

## Built-in Functions (Highlights)

- **String (11)**: `len`, `lower`, `upper`, `trim`, `replace`, `split`, `contains`, `htmlspecialchars`, etc.
- **Array/Object (14)**: `count`, `push`, `pop`, `join`, `keys`, `has_key`, `get`, `set`, and more.
- **Math/Type (21)**: `abs`, `floor`, `ceil`, `round`, `min`, `max`, `type`, `is_*`, `to_string`, `to_number`, etc.
- **Date (5)**: `now`, `date_format`, `date_parts`, `date_parse`, `date_add_days`
- **Output (4)**: `echo`, `print`, `println`, `nl2br`
- **HTTP/Response (8)**: `status`, `header`, `http_status`, `http_header`, `http_content_type`, `redirect`, `urlencode`, `urldecode`

## CGI Mode

When no CLI arguments are provided and `GATEWAY_INTERFACE` is set, `polonio` auto-detects CGI mode:

1. Reads environment variables and standard input to populate `_GET`, `_POST`, `_COOKIE`, `_SERVER`
2. Executes the target template via `SCRIPT_FILENAME`
3. Uses `http_status`, `http_header`, `http_content_type`, and `redirect` to manage headers
4. Emits headers followed by template output

Example snippet:

```pol
<% if not _GET["logged_in"] %>
  <% redirect("/login") %>
<% end %>
```

## Builtins Coverage

Polonio v0.1 still trails the full builtin list from the language spec. For the authoritative, currently-shipping set (including `http_status`, `http_header`, `http_content_type`, `redirect`, `urlencode`, and `urldecode`), check `src/polonio/runtime/builtins.cpp`.

## Development & Testing

- Follow milestone queue in `docs/FEATURE_QUEUE.md` (work top-to-bottom)
- Use TDD: update/create doctest cases in `tests/test_main.cpp` before implementation
- Preferred workflow: edit → `make test` → commit → push feature branch
- Coding guidelines, specs, and low-level docs live under `docs/`

## Project Structure

```
src/
  main.cpp                     # CLI entry point
  polonio/
    common/                    # Source loading, locations, errors
    lexer/                     # Tokenizer
    parser/                    # Pratt parser + AST
    runtime/                   # Interpreter, builtins, template renderer, CGI helpers
docs/                          # Specs, feature queue, agent guide
tests/test_main.cpp            # doctest suite
third_party/                   # vendored libraries (doctest, etc.)
Makefile                       # build/test targets
```

## Roadmap

- Milestone 10: `polonio serve` development server with routing/static assets (in progress)
- Complete remaining builtins from the language spec
- Additional runtime features and tooling as listed in `docs/FEATURE_QUEUE.md`

## License

MIT License. See `LICENSE` for details.
