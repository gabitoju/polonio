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
- [DONE] (M3-4) Add `--dump-ast` for expressions (dev flag)

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
- [DONE] (M6-2) String builtins: len/lower/upper/trim/replace/split/contains
- [DONE] (M6-3) Array/Object builtins: count/push/pop/join/range/keys/has_key/get/set
- [DONE] (M6-4) Math + type predicates + now()
- [DONE] (M6-5) Date format helpers: date_format/date_parts
- [DONE] (M6-6) html_escape builtin
- [DONE] (M6-7) substr/slice/values builtins
- [DONE] (M6-8) pow/sqrt builtins
- [DONE] (M6-9) shift/unshift builtins
- [DONE] (M6-10) concat builtin
- [DONE] (M6-11) debug builtin
- [DONE] (M6-12) date_add_days builtin
- [DONE] (M6-13) date_parse builtin
- [DONE] (M6-14) rand/randint builtins

## Milestone 7 — Template engine
- [DONE] (M7-1) Template scanning and execution of `<% %>` blocks
- [DONE] (M7-2) HTML-mode `$var` interpolation
- [DONE] (M7-3) Inline `<% echo expr %>` evaluation
- [DONE] (M7-4) Include statement

## Milestone 6A — Builtins minimal
- [DONE] (M6A-1) Output: echo/print/println + output buffer
- [DONE] (M6A-2) String: len + htmlspecialchars + nl2br
- [DONE] (M6A-3) Count/type conversions: count/type/to_string/to_number

## Milestone 7 — Template engine
- [DONE] (M7-1) Template scanner: TEXT vs CODE segments
- [DONE] (M7-2) `$var` interpolation in TEXT + HTML comment stripping rule
- [DONE] (M7-3) Spanning blocks: if/for across HTML

## Milestone 8 — Superglobals
- [DONE] (M8-1) HTTP superglobals `_GET` `_POST` `_COOKIE` `_SERVER`
- [DONE] (M8-2) Response control + escaping

## Milestone 9 — CGI mode
- [DONE] (M9-1) CGI auto-detect + read env/stdin + headers + render
- [DONE] (M9-2) Integration tests for CGI mode

## Milestone 10 — Dev server
- [DONE] (M10-1) `polonio serve` CLI plumbing + stub listener
- [TODO] (M10-2) Static asset handling + routing
- [TODO] (M10-3) Template rendering for `.pol` files
- [TODO] (M10-4) POST handling + superglobals
- [TODO] (M10-5) Response helpers + redirects
- [TODO] (M10-6) Documentation + help updates
