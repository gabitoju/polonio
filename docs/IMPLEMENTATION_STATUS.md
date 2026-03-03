# Implementation Status Report

Generated on `2026-03-02` for the Polonio repository. This report summarizes the current implementation against the v0.1 specifications, records validation commands, and recommends the next steps.

## A. Specs & Roadmap Summary

- **Language Spec (`docs/polonio_language_spec_v0_1.md`)** – Defines Polonio syntax, control flow, built-in library (48 functions across String/Array/Object/Math/Type/Date/Output/HTTP), CLI commands (`run`, `serve`, `help`, `version`, `--dump-ast`), template semantics, and CGI mode expectations.
- **Implementation Plan (`docs/polonio_implementation_spec_v0_1.md`)** – Describes incremental milestones M0–M10 covering build system, parser/interpreter, builtins, template engine, superglobals, CGI, and dev server. Emphasizes TDD and one-feature-per-branch workflow.
- **Feature Queue (`docs/FEATURE_QUEUE.md`)** – Tracks milestone tasks. All items through M9 are marked `DONE`; M10 (dev server) is in progress with the CLI plumbing/stub listener (M10-1) completed.
- **Codex Agent Manual (`docs/CODEX_AGENT.md`)** – Reinforces incremental workflow, branch naming, commit conventions, and use of the feature queue.

## B. Codebase Survey

- **Modules present** – `src/polonio/common` (source + errors), `lexer`, `parser` (Pratt AST), `runtime` (value/env/output/interpreter/builtins/cgi/template_scanner/template_renderer). [`Makefile`](Makefile) wires all modules into `build/polonio` and `build/polonio_tests`.
- **Builtins registry** – `install_builtins` in `src/polonio/runtime/builtins.cpp:786-835` registers 35 symbols (type/tostring/to_number, string helpers, array/object helpers, math comparators, type predicates, `now`, HTTP response helpers `status/header/http_*`, `redirect`, `urlencode`, `urldecode`, `date_parts`, `date_format`). Functions listed in the spec such as `substr`, `shift`, `unshift`, `slice`, `concat`, `values`, `pow`, `sqrt`, `rand`, `randint`, `date_parse`, `date_add_days`, and `debug` are absent.
- **CLI commands** – `src/main.cpp:84-220` implements `help`, `version`, `run`, shorthand `<file>`, developer `--dump-ast`, CGI auto-detect (lines 116-125), and a `serve` command that validates `--port/--root` and runs a stub HTTP server responding with a fixed `200 OK` body.
- **Templates** – `src/polonio/runtime/template_renderer.cpp` compiles mixed HTML/code segments with `$var` interpolation, HTML comment stripping, inline echo, includes, and text escaping helpers. Template scanning performed in `src/polonio/runtime/template_scanner.cpp`.
- **CGI runtime** – `src/polonio/runtime/cgi.cpp` parses env vars/stdin into `_GET`, `_POST`, `_COOKIE`, `_SERVER`; `ResponseContext` (`src/polonio/runtime/interpreter.h:27-55`) emits headers/status. `main.cpp:94-115` wires CGI mode when no CLI args and `GATEWAY_INTERFACE` is set.

## C. Validation Commands & Outputs

All commands executed from repo root (`/Users/gabitoju/projects/cpp/polonio`).

1. **Build & test**

```bash
make clean && make && make test
```

Output excerpt:

```
rm -rf build
...
[doctest] test cases: 136 | 136 passed | 0 failed
```

2. **Version**

```bash
./build/polonio version
```

Output:

```
0.1.0
```

3. **Help**

```bash
./build/polonio help
```

Output shows documented commands including placeholder `polonio serve ...`.

4. **Pure code execution**

```bash
code_file=$(mktemp /tmp/polonio_code_XXXXXX.pol); \
echo 'echo 21 + 21' > "$code_file"; \
./build/polonio run "$code_file"; \
rm "$code_file"
```

Output: `42`

5. **Template execution with HTML + interpolation + includes**

```bash
tmpdir=$(mktemp -d /tmp/polonio_tpl_XXXX); main="$tmpdir/main.pol"; partial="$tmpdir/partial.pol"; nested="$tmpdir/nested.pol"
# (files created with HTML, <% var %>, $name, <% echo %>, nested include)
./build/polonio run "$main"
rm -r "$tmpdir"
```

Output excerpt (HTML preserved, include/nested evaluation shown):

```
<p>Hello Ada!</p>
<section>

20
<footer>Ada rocks</footer>
```

6. **CGI smoke (GET)**

```bash
cgi_tpl=$(mktemp /tmp/polonio_cgi_XXXX.pol)
# template prints method, GET params, cookies, POST data
GATEWAY_INTERFACE=CGI/1.1 SCRIPT_FILENAME="$cgi_tpl" REQUEST_METHOD=GET \
QUERY_STRING='a=1&b=two&b=three' HTTP_COOKIE='y=two' ./build/polonio | head -n 30
```

Output excerpt:

```
Content-Type: text/html

Method:GET
A:1
B:two,three
Cookie:two
Post:
```

7. **CGI smoke (POST form)**

```bash
GATEWAY_INTERFACE=CGI/1.1 SCRIPT_FILENAME="$cgi_tpl" REQUEST_METHOD=POST \
CONTENT_TYPE='application/x-www-form-urlencoded' CONTENT_LENGTH=11 \
HTTP_COOKIE='y=two' ./build/polonio <<< 'm=hi&m=there' | head -n 30
```

Output excerpt shows `Method:POST` and `Post:hi,ther` (input truncated because CONTENT_LENGTH specified as 11; actual string `m=hi&m=there` length 12, so final “e” omitted—evidence of strict length handling).

8. **HTTP helper smoke**

```bash
helpers_tpl=$(mktemp /tmp/polonio_http_XXXX.pol)
GATEWAY_INTERFACE=CGI/1.1 SCRIPT_FILENAME="$helpers_tpl" ./build/polonio | head -n 20
```

Output:

```
Status: 302
X-Test: ok
Content-Type: text/plain
Location: /next

done
```

## D. Gap Analysis

| Area | Spec expectation | Implemented? | Evidence | Next action |
| --- | --- | --- | --- | --- |
| CLI | Spec requires `polonio run`, shorthand, `help`, `version`, `--dump-ast`, and functional `polonio serve` dev server (`docs/polonio_language_spec_v0_1.md:359-366`). | **Partial** | `src/main.cpp:84-160` shows CLI commands and returns `serve: not implemented yet`; `./build/polonio help` output confirms placeholder. | Implement milestone M10 (`polonio serve`), or update docs to reflect current limitations. |
| Parser | Pratt parser with arrays/objects/calls/assignments (`docs/polonio_implementation_spec_v0_1.md:52-104`). | **Yes** | Modules `src/polonio/parser/*.cpp` and passing doctest suite (`make test`). | Keep maintained; add more parser tests for upcoming features. |
| Runtime | Interpreter with lexical scoping, closures, loops, includes (`docs/polonio_implementation_spec_v0_1.md:105-143`). | **Yes** | `src/polonio/runtime/interpreter.cpp` implements env stack, loops, includes; template runs and CGI smoke succeed. | Continue ensuring coverage when adding new statements. |
| Templates | HTML/text splitting, `$var`, inline `<% echo %>`, includes (`docs/polonio_language_spec_v0_1.md:102-153`). | **Yes** | `src/polonio/runtime/template_renderer.cpp` plus template smoke test (#5). | Add more integration tests for nested blocks and error cases. |
| CGI / HTTP | Spec expects CGI auto-detect + env parsing + response helpers; Feature Queue marks M9 `TODO`. | **Yes (implementation) / Docs lag** | `src/main.cpp:116-124` auto-detects `GATEWAY_INTERFACE`; CGI tests (#6-8) show headers and helpers functioning. Feature queue still lists M9 as TODO. | Update `docs/FEATURE_QUEUE.md` to reflect CGI completion and add regression tests for auto-detect behavior. |
| Builtins | Spec lists 48 functions (String `substr`, Array `shift`, etc.) (`docs/polonio_language_spec_v0_1.md:302-329`). | **Partial** | `install_builtins` (`src/polonio/runtime/builtins.cpp:786-835`) lacks `substr`, `shift`, `unshift`, `slice`, `concat`, `values`, `pow`, `sqrt`, `rand`, `randint`, `date_parse`, `date_add_days`, `debug`. | Implement missing builtins with tests; document interim deviations. |
| Docs / Roadmap | Feature queue should mirror implementation state. | **No** | `docs/FEATURE_QUEUE.md:68-74` still marks M9/M10 TODO, even though CGI runtime is present while dev server absent. README also recently added. | Align documentation: mark completed milestones, describe remaining work, ensure README references actual behavior. |

## E. Missing or Divergent Items

1. **Builtins coverage gap** – Several functions promised in the language spec are absent (e.g., `substr`, `shift/unshift`, `slice`, `concat`, `values`, `pow`, `sqrt`, `rand`, `randint`, `date_parse`, `date_add_days`, `debug`) – see `docs/polonio_language_spec_v0_1.md:302-328` vs `src/polonio/runtime/builtins.cpp:786-835`.
2. **Dev server unimplemented** – Spec `polonio serve` (docs lines 359-366) and CLI help advertise the command, but `src/main.cpp:148-150` returns “not implemented yet”.
3. **Feature queue mismatch** – `docs/FEATURE_QUEUE.md:68-74` lists CGI milestones as TODO despite functioning CGI mode (auto-detect, superglobals, headers). Integration tests exist in `tests/test_main.cpp` around CGI cases, so the status should be updated or scope clarified.
4. **CGI POST length handling** – Smoke test #7 shows user-provided `CONTENT_LENGTH` truncates request body; this matches CGI spec but should be documented to avoid confusion when lengths don’t match actual stdin.

## F. Recommended Next Steps

1. **Finalize Milestone M9 (CGI documentation & tests)**  
   - *Goal*: Align docs with implemented CGI behavior, add automated CLI-less tests that verify auto-detect when no arguments are provided, and ensure response helpers are fully covered.  
   - *Acceptance tests*: Add doctest cases that simulate empty argv with `GATEWAY_INTERFACE` set, verifying `_GET/_POST/_COOKIE/_SERVER`, `http_*` helpers, and error formatting. Update `docs/FEATURE_QUEUE.md` to mark M9 done once coverage is complete.  
   - *Branch name*: `feature/M9-cgi-validation`.

2. **Implement missing builtins from spec**  
   - *Goal*: Add remaining String/Array/Object/Math/Date/Output builtins (`substr`, `shift`, `unshift`, `slice`, `concat`, `values`, `pow`, `sqrt`, `rand`, `randint`, `date_parse`, `date_add_days`, `debug`) with doctest coverage.  
   - *Acceptance tests*: Extend `tests/test_main.cpp` with unit cases per function covering happy path and error handling. Update README/spec once parity reached.  
   - *Branch name*: `feature/M6-builtins-complete`.

3. **Milestone M10 – Development server**  
   - *Goal*: Implement `polonio serve` command that serves templates/static files, populates superglobals, and offers a basic routing layer per spec.  
   - *Acceptance tests*: Add integration tests (if feasible) or scripted smoke instructions verifying static file serving, template rendering, and `--port/--root` handling. Update CLI help once command works.  
   - *Branch name*: `feature/M10-dev-server`.

---

Reproduction: rerun the commands listed in Section C verbatim to validate build/tests and runtime behavior.
