# Feature Queue (Polonio v0.1)

Rule: Work top-to-bottom. Pick the first TODO item only.

Legend: TODO | DOING | DONE | BLOCKED

## Milestone 0 — Build + CLI skeleton
- [DONE] (M0-1) Add doctest test runner and `make test`
- [DONE] (M0-2) Implement CLI: `help`, `version`
- [DONE] (M0-3) Parse `run <file.pol>` and shorthand `<file.pol>` (stub execution)

## Milestone 1 — Source model + errors
- [DONE] (M1-1) Source loader with path + in-memory buffer
- [DONE] (M1-2) Location/span tracking utilities
- [DONE] (M1-3) Unified error formatting: `file:line:col: message`

## Milestone 2 — Lexer
- [DONE] (M2-1) Lexer: identifiers + keywords + numbers + strings
- [DONE] (M2-2) Lexer: operators + punctuation + compound ops
- [DONE] (M2-3) Lexer: block comments `/* ... */` + unterminated errors

## Milestone 3 — Expression parser
- [DONE] (M3-1) AST nodes + expression parser (Pratt) for precedence
- [DONE] (M3-2) Parse array/object literals
- [DONE] (M3-3) Parse calls + indexing + assignment expressions
- [TODO] (M3-4) Add `--dump-ast` for expressions (dev flag)

## Milestone 4 — Statement parser
- [DONE] (M4-1) Parse: `var`, `echo`, expression statements
- [DONE] (M4-2) Parse: `if/elseif/else/end`
- [DONE] (M4-3) Parse: `for/in/end` and `while/end`
- [DONE] (M4-4) Parse: `function/end` and `return`

## Milestone 5 — Runtime core
- [DONE] (M5-1) Value type + env scopes + truthiness + equality
- [DONE] (M5-2) Execute statements + expressions
- [DONE] (M5-3) Functions + closures + recursion
- [DONE] (M5-4) Loops + break/continue

## Milestone 6 — Builtins core
- [DONE] (M6-1) Builtins core + type/tostring/nl2br

## Milestone 6A — Builtins minimal
- [TODO] (M6A-1) Output: echo/print/println + output buffer
- [TODO] (M6A-2) String: len + htmlspecialchars + nl2br
- [TODO] (M6A-3) Count/type conversions: count/type/to_string/to_number

## Milestone 7 — Template engine
- [TODO] (M7-1) Template scanner: TEXT vs CODE segments
- [TODO] (M7-2) `$var` interpolation in TEXT + HTML comment stripping rule
- [TODO] (M7-3) Spanning blocks: if/for across HTML

## Milestone 8 — Superglobals
- [TODO] (M8-1) `_SERVER` minimal population
- [TODO] (M8-2) `_GET` query parsing
- [TODO] (M8-3) `_COOKIE` parsing
- [TODO] (M8-4) `_POST` x-www-form-urlencoded parsing

## Milestone 9 — CGI mode
- [TODO] (M9-1) CGI auto-detect + read env/stdin + headers + render
- [TODO] (M9-2) Integration tests for CGI mode

## Milestone 10 — Dev server
- [TODO] (M10-1) `polonio serve` basic server + routing + static files
- [TODO] (M10-2) Populate superglobals in server mode
- [TODO] (M10-3) Smoke test doc + minimal automated test if feasible
