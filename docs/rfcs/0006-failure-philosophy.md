# RFC 0006 — Failure Philosophy

## Status

Implemented.

## Purpose

This RFC answers what failures mean in Polonio and whether code should react
to them. It decides the model before any recovery syntax. Not every undesirable
outcome is an error: routine absence and application outcomes are values.

## Current baseline

RFC 0004 categories (`SourceError`, `LexError`, `ParseError`, `RuntimeError`,
`CapabilityError`, `ResourceError`, and `InternalError`) terminate the current
execution today. Adapters present safe structured diagnostics. There is no
user-level recovery construct, user-created Error value, explicit user raise,
or error-return convention. Builtin errors propagate through user functions.

Current APIs already represent expected outcomes using `null`, `false`, empty
arrays/objects, counts, and application-defined objects: for example absent
object keys, empty queries, CSRF mismatch, and password verification do not
need an error. This is current behavior, not alone the complete v1 philosophy.

## Goals

- Separate errors from expected conditions and business outcomes.
- Separate programming bugs from operational failures.
- Keep successful paths simple and avoid mandatory value-plus-error returns.
- Decide whether generated runtime failures may later be recoverable.
- Define user-function propagation and user-created-failure boundaries.
- Provide a foundation for a later recovery RFC without using exceptions for
  routine control flow.

## Non-goals

This RFC designs no recovery or exception syntax, error-object syntax, typed
catches, multiple returns, tuples, destructuring, Result/Option types, stack
unwinding, cleanup/finally, retries, user error classes, or rollback rules.

## Failure taxonomy

| Class | Meaning | RFC 0004 mapping | v1 representation |
| --- | --- | --- | --- |
| Invalid program | Source cannot form a program | SourceError, LexError, ParseError | fatal diagnostic |
| Programming error | Program violates language or API contract | RuntimeError | fatal diagnostic |
| Operational failure | Valid operation cannot run in its host environment | CapabilityError, ResourceError | fatal now; future recovery candidate |
| Expected condition | Ordinary negative/absent outcome | none by default | documented normal value |
| Domain failure | Application rule rejects a requested outcome | none by default | application-defined normal value/object |
| Internal implementation failure | Engine or host invariant fails | InternalError | fatal diagnostic |

### Invalid program

Source loading, lexing, and parsing fail before a valid executable program is
available. They are always fatal to that evaluation and never recoverable by
Polonio code.

### Programming error

Undefined names, calls of non-functions, invalid operators/indexing, and
invalid builtin arity/type/value/shape are programming errors. A program or
library must correct them; they abort the current execution. A future broad
handler must not hide `RuntimeError`, and user code cannot convert one into a
normal value by language mechanism. RFC 0005 reasons remain diagnostic facts,
not independently recoverable classes.

### Operational failure

A valid program can lack a declared runtime capability/configuration or lose a
configured external resource. These are `CapabilityError` and `ResourceError`.
They propagate automatically through user functions, includes, and template
code blocks. They are fatal when uncaught in v1, but are the only categories
eligible for future language-level recovery. Recovery is continuation only;
it never promises rollback.

### Expected condition

Expected absence, mismatch, negative lookup, empty result, and zero affected
rows are values, not errors. APIs must document whether they use `null`,
`false`, an empty collection, a count, or another normal value. The structured
error system is not used for routine control flow. A future absence-semantics
RFC may make these conventions more consistent.

### Domain or business failure

Validation failures, permission decisions, conflicts, quota rules, and invalid
application state transitions normally use values defined by the application
or framework. `null`/`false` suit simple predicates; validation or status
objects suit richer outcomes. v1 mandates no wrapper shape and supplies no
built-in domain-failure constructor. Domain failures neither use nor propagate
as `ResourceError` by default.

### Internal implementation failure

`InternalError` represents a violated engine invariant or unexpected host
failure that cannot meaningfully be attributed to the program. It is always
fatal and never recoverable by Polonio code. Diagnostics are safe and
adapter-redacted; private host details do not become a language value.

## Core philosophy

The following principles are adopted:

1. Expected conditions are values, not exceptions.
2. Programming errors abort current execution and are not future recovery
   targets.
3. Internal implementation failures are uncatchable.
4. Capability and resource failures may eventually be recoverable.
5. An uncaught operational failure aborts the current execution/request.
6. Recovery never implies transaction or side-effect rollback.
7. Domain validation is expressed by application/framework values.
8. v1 has no user-created structured failures.
9. Successful code has no mandatory error-return boilerplate.
10. Routine absence never requires an exception-like mechanism.

## User-defined functions

v1 adopts Option A: functions propagate runtime-generated failures
automatically but cannot create structured failures. Option B (re-propagating a
captured operational failure) is a future recovery-RFC candidate; it is not a
v1 feature. Options C and D—generic user failures and typed/domain failures—
are rejected for v1 because they blur operational errors with business values
and require new public origin/category design.

## Domain-result policy

Domain-result conventions belong to applications and frameworks, not Language
Core or the current Standard Library. A finder may return `null`; a predicate
may return `false`; richer validation may return `{ "ok": false, "errors":
... }`, and a status object may carry a successful value. These are ordinary
objects, not a language-level Result type. API authors should document their
normal negative result and avoid treating a routine condition as an operation
failure.

## Alternatives

### Go-style error values

Rejected for v1. Polonio has neither multiple-return nor destructuring syntax;
adopting `value + error` would redesign signatures, add boilerplate propagation,
and harm template ergonomics. An application may choose an object wrapper, but
that is not a language mandate.

### Ruby-style exceptions

Full Ruby-style exceptions are rejected: broad rescue can conceal programming
errors, make domain flow implicit, and cannot undo partial side effects. A
restricted Ruby-like direction is preferred for the future: automatic
propagation and handling only of operational failures, no inline rescue model
assumed, no user hierarchy, and syntax deferred.

### Result-style values

Rejected as a language mechanism and standard-library-wide convention. They
remain suitable application/framework territory for domain outcomes because
they are explicit, compatible with existing single values, and do not force
every fallible API to wrap success. Their boilerplate and nesting make them a
poor universal replacement for operational propagation.

### Other alternatives considered

1. Make every undesirable result an exception — rejected; absence is routine.
2. Never allow recovery — rejected; valid deployments can handle an unavailable
   optional capability or resource at an outer layer.
3. Result objects for all fallible operations — rejected for the reasons above.
4. User-defined exception classes — rejected for v1 scope and taxonomy drift.
5. Domain failures only as primitive values — rejected; applications may need
   validation/status objects.

## Recoverability direction

| Category | Future recoverable? | Rationale |
| --- | ---: | --- |
| SourceError | No | no executable program exists |
| LexError | No | no valid program exists |
| ParseError | No | no valid program exists |
| RuntimeError | No | programming/API contract violation |
| CapabilityError | Yes | a valid program may adapt to host availability |
| ResourceError | Yes | a valid operation may have a local fallback |
| InternalError | No | unsafe to continue after engine/host invariant failure |

RFC 0005 reasons do not change this table: even a structured `RuntimeError`
argument failure remains a programming error, not a future handler target.

## Propagation and fatality

Generated failures propagate through function calls, includes, and template
code blocks until execution ends or a future permitted operational handler is
defined. They cross user-function boundaries automatically; adapters may
intercept them for presentation without making them code-recoverable. An
uncaught failure ends the current CLI evaluation or web request, never normally
the development-server process. It does not turn a host adapter's safe 500 or
non-zero result into language-level recovery.

## Side effects and rollback

Future recovery does not undo file writes/deletes, uploads, mail writes,
session mutation, emitted output, HTTP headers, finalized responses, or any
external change. SQLite rollback occurs only through explicit current behavior
or a future separately specified transaction abstraction. Recovery means
control-flow continuation, never implicit compensation.

## Adapter behavior

Uncaught failures retain RFC 0004 behavior: CLI emits a structured diagnostic
to stderr and exits non-zero; CGI emits a valid 500 response; the development
server returns request-local 500 and remains alive where possible; embeddings
receive a structured host-visible error. This is adapter handling, not
user-level recovery.

## Error-value direction

A future permitted operational handler should receive a read-only structured
Error value derived from RFC 0004/0005 facts. It has no direct constructor in
v1, cannot mutate category or origin, and is not specified here as syntax or
field-access API. Message-only and opaque-only approaches are rejected because
they discard useful category/location/capability/resource facts.

## Required decisions

This RFC decides programming errors as contract/bug failures; operational
failures as capability/resource failures; expected and domain conditions as
values; only capability/resource future recoverability; automatic propagation;
no v1 user-created failures; rejection of Go-style multiple returns and full
Ruby exceptions; Result objects as application territory; no rollback; fatal
uncaught operational failures; and a future read-only structured Error value.

## Decision

Polonio distinguishes invalid programs, programming errors, operational
failures, expected conditions, domain failures, and internal failures. Expected
and domain outcomes use normal values. Invalid-program, runtime-programming,
and internal failures are fatal and never user-recoverable. Capability and
resource failures propagate automatically and may gain restricted future
recovery, while remaining fatal when uncaught. Functions cannot create errors
in v1. Go-style errors and language-level Result values are rejected; restricted
operational recovery is preferred over full Ruby-style exceptions. A future
handler receives a read-only structured Error value. No recovery provides
rollback, and adapters retain RFC 0004 terminal behavior.

## Consequences

Positive: applications keep common successful paths clear, absence stays
simple, adapter/runtime failures remain diagnosable, and future recovery has a
small safe target. Negative: applications must choose and document domain
result shapes; callers cannot recover operational failures yet; partial effects
require explicit design. API design distinguishes normal negative values from
host failures. User functions gain automatic propagation but no error creation.
Frameworks own validation conventions. A later syntax RFC must honor category
restrictions, output/finalization concerns, and immutable error facts. No
existing successful API changes or compatibility breaks result.

## Follow-up work

- Recovery block semantics and syntax for `CapabilityError`/`ResourceError`.
- Read-only structured Error value and permitted re-propagation semantics.
- Expected-absence conventions and validation/status-object guidance.
- Output/finalization interaction for a handled operational failure.
- Transaction/resource-cleanup semantics, if separately justified.

## Implementation status

The reference implementation exposes an immutable category-derived
`Recoverability` designation. `CapabilityError` and `ResourceError` are
`Operational`; all other RFC 0004 categories are `Never`. This is eligibility
metadata for a future restricted mechanism, not current catchability. Existing
function/template propagation and CLI/CGI/server terminal boundaries remain
unchanged and are covered by conformance tests. No Polonio Error value,
user-created error, recovery syntax, or rollback behavior exists.
