# RFC 0004 — Polonio Error Model

## Status

Accepted.

## Context

Polonio errors currently originate in source loading, lexing, parsing, template
rendering, interpretation, built-ins, storage, SQLite, CGI processing, and the
development server. `PolonioError` currently distinguishes only `IO`, `Lex`,
`Parse`, and `Runtime`; several adapters also use unstructured C++ exceptions.
Source diagnostics and execution failures therefore have different location
quality and different presentations. CLI, CGI, and `serve` also expose them
differently.

v1 needs a stable observable model before builtin messages are made consistent
or user programs can recover. It must specify categories and facts without
freezing every English sentence.

## Goals

- Define public error categories and their required facts.
- Distinguish source processing, program execution, and host capability or
  resource failures.
- Define fatality, location, and adapter presentation responsibilities.
- Provide the foundation for RFC 0005 and RFC 0006.

## Non-goals

- Implementing `try/catch`, rewriting messages, stack traces, logging, or a
  production mode.
- Exposing C++ exception text or changing HTTP status semantics.
- Freezing exact message wording.

## Terminology

A **diagnostic** describes a failure for a human or host. An **error category**
is its stable public class. A **source location** is a path, line, and column;
a **source span** has start and end locations. An **execution error** arises
while a valid program runs. A **host capability error** means a required
optional runtime context is absent or unusable. A **resource error** is an
external file, database, transport, or operating-system failure. A **fatal
error** ends the current program or request execution. A **recoverable error**
is one a future language construct may handle. A **presentation adapter**
formats engine failures for CLI, CGI, server, or embedding use. An **internal
implementation failure** is an invariant or unexpected host failure.

## Current-system audit

All present failures terminate the current evaluation; none is exposed as a
Polonio value. `PolonioError::format()` produces `path:line:column: message`.
The lexer and parser preserve a token location. Most interpreter-wide failures
use the template path but location `1:1`; built-ins commonly receive an
invocation location. No stack or include trace exists.

| Current path | Origin and current kind | Location / presentation | Coverage and inconsistency |
|---|---|---|---|
| Source loading | `Source::from_file`; `IO` | Path and `1:1`; CLI stderr, CGI 500, server 500 | Direct tests cover missing file. |
| Lexing | lexer strings, comments, symbols; `Lex` | Path and token start; CLI stderr, CGI/server 500 | Direct location tests. |
| Parsing/template scanning | parser, unterminated template block/comment; `Parse` | Path and token/scanner location; adapters as above | Direct parser/template tests. |
| Language execution | undefined names, calls, indexing, arithmetic, loops, includes; `Runtime` | Often path plus `1:1`; no code-visible value | Runtime tests cover representative paths. |
| Built-in arity/type/value | `builtins.cpp`; `Runtime` | Invocation location when passed; adapters terminate execution | Many throw-only tests; wording varies by builtin. |
| Optional capability | missing HTTP context, storage root, DB connection, session secret; `Runtime` | Usually builtin location; adapters terminate execution | CGI/storage/session tests are selective. |
| Resources | storage/files, SQLite, upload/outbox; `Runtime` | Usually invocation location; SQLite text may be included | Storage/SQLite tests exist, but detail and coverage vary. |
| Request/multipart | CGI body processing; `Runtime` | Template path `1:1` or none; CGI/server 500 | Request tests exist; malformed server request differs. |
| CLI | argument and option validation; plain stderr strings | No source location; non-zero exit | CLI tests check fragments. |
| CGI | `PolonioError` catch in `main.cpp` | Plain-text `Status: 500`; other `std::exception`s may escape | CGI tests cover normal and selected errors. |
| Dev server | request parser throws `std::runtime_error`; routing and template execution | 400 malformed request, 404 missing route, 405 method, 500 template/host failure | Server tests cover 400/404/405/500 behavior. |

This audit intentionally records current inconsistency: only four categories
exist, host exceptions are not normalized, runtime locations are incomplete,
and adapter bodies can expose raw operational detail.

## Public error categories

v1 public categories are `SourceError`, `LexError`, `ParseError`,
`RuntimeError`, `CapabilityError`, `ResourceError`, and `InternalError`.
Conforming implementations must distinguish them structurally. `ArgumentError`
is not a separate public category: it is `RuntimeError` with optional
`operation`, `function_name`, `argument_index`, and `reason` facts. This keeps
the public hierarchy small while making RFC 0005 actionable.

`CapabilityError` is distinct because a valid program may require a profile or
context that the host did not provide. Missing HTTP context, storage root,
session secret, and database connection map here. `ResourceError` is distinct
because a configured capability can still fail externally: missing or unreadable
storage data, SQLite prepare/step/open errors, upload writes, and mail-outbox
writes map here. Source-file loading maps to `SourceError` rather than
`ResourceError` because it occurs before program source is available.

`InternalError` is not a normal user-program error and must not leak private
implementation detail. Current unstructured server/socket exceptions are the
main migration target.

## Required decisions

### Fatality and future recovery

`SourceError`, `LexError`, and `ParseError` are fatal and never recoverable by
Polonio code. `InternalError` is fatal and never recoverable. `RuntimeError`,
`CapabilityError`, and `ResourceError` are fatal in v1 only because no
user-level recovery exists; RFC 0006 may make selected occurrences recoverable.
Fatal means the current program/request stops, not that the host process must
stop.

### Locations, spans, and includes

Every `LexError` and `ParseError` requires source path, line, column, and a
span when known. Every source-associated execution error requires its call or
operation location; a genuinely host-only error may omit location. Built-in
failures use the invocation site. The current `1:1` fallback is documented
existing behavior, not sufficient v1 implementation work.

An include diagnostic must name the included source location and carry an
ordered include chain, caller first and failing file last, when an include is
involved. The display depth is capped at 32 frames; engines may retain more.
Function call stacks are deferred beyond v1. RFC 0013 defines include details.

### Messages, fields, and codes

The stable contract is category plus structured facts and general message
meaning. Exact English wording, punctuation, and formatting are not compatible
API, except formatter-specific tests. Every error has `category` and
human-readable `message`. When available it also has `source_path`, location or
span, `operation`, `function_name`, `argument_index`, `capability`, `resource`,
and a safe causal summary. Private exception data, secrets, and raw host
objects are never public fields.

v1 uses categories only: it introduces no stable symbolic error-code set.
Per-builtin codes would prematurely duplicate RFC 0005 and user-level handling;
RFC 0006 may propose codes if programs need machine matching.

### Presentation and adapter behavior

The engine creates structured errors; adapters select a safe transport format.
The canonical human diagnostic is:

```text
path:line:column: Category: message
```

Without a location it is `Category: message`. Include chains follow the primary
diagnostic as separate `included from path:line:column` lines. Colors are not
required. The current formatter lacks category and traces; that is migration
work, not a present behavior change.

- CLI writes a diagnostic to stderr and exits non-zero; it must not report
  success.
- CGI emits a valid plain-text 500 response. It may show a safe development
  diagnostic but must not expose private host details.
- The development server retains 400 for malformed requests, 404 for unresolved
  routes, and 405 for unsupported methods. Engine and host execution failures
  are 500 with a development diagnostic that omits raw C++ details.
- An embedding host receives or throws the structured failure through its host
  API; it owns final presentation.

### Output and finalized responses

Current output can be accumulated before an error and adapter behavior is not
uniformly specified. `send_file()` finalizes a response and later output
operations fail; the replacement or preservation of a partially/finalized body
is not yet a stable language rule. Both partial output and errors after response
finalization are implementation-defined until RFC 0014. Adapters must still
avoid sending a false successful response where they can safely replace it.

## Security and information disclosure

Diagnostics must not expose session secrets, password material, request bodies,
or internal C++ exception text. Paths, SQL text/errors, mail addresses, and
resource names are potentially sensitive. The engine may retain safe detail;
CLI and local development presentations may show useful local paths, while CGI,
servers, and embedding hosts must redact according to their deployment policy.
This RFC does not create a production mode.

## Testing policy

Future conformance fixtures assert category, required location, stable facts,
and key message meaning. They avoid full-message equality except formatter
tests, test CLI exit/status separately from CGI/server behavior, and verify
secret redaction. Existing tests already overfit the formatter once
(`PolonioError format includes path and location`) and otherwise mostly assert
kind or message fragments; RFC 0005 must review builtin-specific fragments.

## Alternatives considered

1. One `PolonioError` category — rejected: it hides source, capability, and
   resource distinctions required by users and adapters.
2. Detailed class hierarchy — rejected: too much API surface before recovery.
3. Result objects instead of errors — rejected: changes language flow before
   RFC 0006.
4. Freeze exact messages — rejected: wording should improve without breaking
   programs.
5. Let adapters classify everything — rejected: portable conformance requires
   engine-level categories.
6. Add user exceptions now — rejected: parser/runtime design belongs to RFC 0006.

## Decision

Polonio v1 adopts the seven categories above. Argument failures are structured
`RuntimeError`s; unavailable profiles/context are `CapabilityError`s; external
operational failures are `ResourceError`s. All errors are fatal to current
evaluation in v1; only runtime, capability, and resource failures may later be
recoverable. Source-associated failures require locations, include chains are
required but call stacks are deferred, messages are not exact-string API, and
no stable error codes are introduced. The engine owns classification; adapters
own safe presentation. Partial/finalized output remains implementation-defined
pending RFC 0014.

## Consequences

Positive: portable diagnostics, clear runtime-profile failures, safer adapter
boundaries, and a base for recovery. Negative: every current throw site must be
audited and location propagation will require work. Migration changes categories
from current `IO/Lex/Parse/Runtime` and normalizes unstructured exceptions;
current program semantics do not change. Implementation work includes a
structured representation, formatter, source spans, include traces, mapping
all audit rows, and conformance/adapter fixtures.

## Follow-up work

- RFC 0005 — Builtin Error Consistency.
- RFC 0006 — User-Level Error Handling.
- Implement structured errors and consistent formatting.
- Add include tracing and source/call-site propagation.
- Add conformance fixtures and adapter-specific error tests.
