# Polonio Language v1 Queue

This document tracks the design, stabilization, and compatibility work required to define Polonio Language v1.

The historical implementation queue in `docs/FEATURE_QUEUE.md` records how the current interpreter and runtime were built. This queue governs the next phase: turning the existing implementation into a coherent, documented, and stable language.

## Working Rule

Work from top to bottom.

Pick the first item whose status is `RFC` or `ACCEPTED`.

Design work must be completed before implementation begins.

## Statuses

* `RFC` — A design question is open and requires investigation and a written proposal.
* `DISCUSSING` — A proposal exists and is being reviewed.
* `ACCEPTED` — The design decision has been accepted but is not fully implemented.
* `IMPLEMENTED` — Code and tests match the accepted design.
* `FROZEN` — The behavior is part of the Polonio v1 compatibility contract.
* `BLOCKED` — Work cannot continue until another decision is completed.

## Completion Requirements

An item may move to `ACCEPTED` only when its design decision is documented.

An item may move to `IMPLEMENTED` only when:

1. The implementation matches the accepted decision.
2. Conformance tests exist.
3. User-facing documentation is updated.
4. Existing tests pass.

An item may move to `FROZEN` only when its behavior is included in the Polonio v1 specification.

---

# L1 — Public Language Surface

## [IMPLEMENTED] L1-1 Canonical Names and Alias Policy

- [DONE] L1-1a Apply canonical names to public documentation
- [DONE] L1-1b Add alias conformance tests
- [BLOCKED] L1-1c Freeze alias policy in Language Specification v1

Define the canonical public name for every duplicated builtin or syntax form.

Known aliases to review:

* `tostring` / `to_string`
* `htmlspecialchars` / `html_escape`
* `status` / `http_status`
* `header` / `http_header`

Questions:

* Which name is canonical?
* Which names remain compatibility aliases?
* Are aliases documented in the main reference or only in a compatibility section?
* Can aliases be deprecated before v1, or must they remain indefinitely?
* Should aliases produce warnings?

Deliverable:

* `docs/rfcs/0001-alias-policy.md`
* Canonical-name table covering every current alias.
* No code changes during the RFC pass.

Acceptance criteria:

* Every registered builtin has one canonical documented name.
* Compatibility aliases have explicitly documented behavior.
* Documentation consistently uses canonical names.

---

## [IMPLEMENTED] L1-2 Builtin Categorization and Namespace Policy

- [DONE] L1-2a Create authoritative builtin inventory
- [DONE] L1-2b Apply layer classifications to public documentation
- [DONE] L1-2c Add builtin classification validation
- [BLOCKED] L1-2d Freeze builtin and namespace policy in Language Specification v1

Polonio currently exposes a large global builtin surface.

Define whether v1 keeps global functional names such as:

```pol
file_read("notes/a.txt")
db_query("select * from users")
session_get("user_id")
```

or introduces a future namespaced model such as:

```pol
file.read("notes/a.txt")
db.query("select * from users")
session.get("user_id")
```

Questions:

* Are global builtins part of the permanent v1 API?
* Would namespaces require object member access or a module system?
* Can namespaced APIs coexist with global compatibility aliases?
* Which functions belong to the language, standard library, or web runtime?

Deliverable:

* `docs/rfcs/0002-builtin-namespace-policy.md`
* Classification of all builtin families:

  * core language
  * standard library
  * web runtime
  * development tooling

No namespace implementation is required in this item.

---

## [RFC] L1-3 Core Language vs Runtime Boundary

Define the official boundary between:

* Polonio language syntax and semantics
* standard builtins
* template runtime
* HTTP runtime
* development server
* optional application/framework libraries

Questions:

* Is SQLite part of the language distribution or an optional runtime capability?
* Are sessions, CSRF, uploads, and mail language features or web-runtime features?
* Which capabilities must every conforming Polonio implementation provide?
* Can a non-web Polonio implementation still be conforming?

Deliverable:

* `docs/rfcs/0003-language-runtime-boundary.md`
* A capability matrix defining mandatory and optional implementation layers.

---

# L2 — Error Model

## [RFC] L2-1 Error Categories and Formatting

Review the current error system and define the v1 error model.

Cover:

* lexer errors
* parser errors
* runtime errors
* I/O errors
* HTTP errors
* database errors
* storage errors
* security errors

Questions:

* Which error categories are public?
* What source location must an error include?
* Should included templates show an include stack?
* Which parts of error messages are compatibility guarantees?
* Should user-visible errors and internal diagnostic details be separated?

Deliverable:

* `docs/rfcs/0004-error-model.md`
* Canonical error format.
* Error category table.
* Required source-location behavior.

---

## [RFC] L2-2 Builtin Argument Error Consistency

Audit all builtins for:

* wrong argument count
* wrong argument type
* invalid argument value
* missing runtime context
* missing external resource

Define consistent message templates, for example:

```text
function_name: expected 2 arguments, got 1
function_name: argument 1 must be string
function_name: value must be greater than zero
```

Deliverable:

* `docs/rfcs/0005-builtin-errors.md`
* An inventory of current inconsistencies.
* Accepted message conventions.

Implementation should be split into small follow-up batches after the RFC is accepted.

---

## [RFC] L2-3 User-Level Error Handling

Determine whether Polonio v1 needs language-level error handling.

Candidate designs:

```pol
try
  risky_operation()
catch error
  echo error["message"]
end
```

or explicit result values:

```pol
var result = attempt(risky_operation)
if result["ok"]
  echo result["value"]
else
  echo result["error"]
end
```

Questions:

* Should runtime errors be catchable?
* Which errors must remain fatal?
* How do errors interact with finalized HTTP responses?
* Is a `finally` block required?
* Does error handling belong in v1 or a later release?

Deliverable:

* `docs/rfcs/0006-user-error-handling.md`
* Accepted design or an explicit decision to defer beyond v1.

Do not implement syntax before this RFC is accepted.

---

# L3 — Modules, Includes, and Reuse

## [RFC] L3-1 Include Semantics

Document and freeze the current behavior of:

```pol
include "file.pol"
```

Questions:

* Is the path resolved relative to the including file?
* Does the included file share the current lexical environment?
* Can included files define variables and functions visible to the caller?
* Is a file executed every time it is included?
* How are cycles and maximum depth handled?
* What happens when the same file is included multiple times?
* How are errors attributed to included files?

Deliverable:

* `docs/rfcs/0007-include-semantics.md`
* Conformance tests for includes, scope, repetition, cycles, and errors.

---

## [BLOCKED] L3-2 Module System

Blocked by L3-1.

Evaluate whether Polonio needs a module system beyond `include`.

Potential requirements:

* load-once behavior
* explicit exports
* isolated scope
* namespaces
* dependency resolution
* reusable libraries without template output

Candidate syntax must not be selected until requirements are documented.

Deliverable:

* `docs/rfcs/0008-module-system.md`
* Accepted module design or an explicit decision that `include` is sufficient for v1.

---

# L4 — Core Semantics

## [RFC] L4-1 Type and Conversion Semantics

Freeze the seven Polonio types:

* null
* bool
* number
* string
* array
* object
* function

Document:

* literal syntax
* runtime representation visible to users
* conversions performed by `to_string` and `to_number`
* invalid conversions
* number formatting
* integral versus fractional numbers
* behavior of mutable arrays and objects

Deliverable:

* `docs/rfcs/0009-type-conversion-semantics.md`
* Type conversion matrix.
* Conformance tests.

---

## [RFC] L4-2 Truthiness

Freeze truthiness behavior for every type.

Current behavior must be verified from code and tests rather than assumed.

At minimum, document:

* null
* false / true
* zero
* nonzero numbers
* empty and nonempty strings
* empty and nonempty arrays
* empty and nonempty objects
* functions

Deliverable:

* `docs/rfcs/0010-truthiness.md`
* Truthiness table.
* Conformance tests for every type.

---

## [RFC] L4-3 Equality and Comparison

Define:

* equality between values of the same type
* equality between different types
* deep array/object equality
* function equality
* numeric comparison
* string comparison
* unsupported comparisons
* whether identity exists separately from equality

Deliverable:

* `docs/rfcs/0011-equality-comparison.md`
* Comparison matrix.
* Conformance tests.

---

## [RFC] L4-4 Variable Scope and Closures

Freeze:

* global/top-level scope
* function-local scope
* block scope
* loop scope
* assignment to outer variables
* closure capture
* recursive functions
* variables created through assignment
* interaction between includes and scope

Deliverable:

* `docs/rfcs/0012-scope-closures.md`
* Scope rules.
* Conformance tests.

---

## [RFC] L4-5 Collection Mutation

Document which operations mutate their input.

Review at least:

* `push`
* `pop`
* `shift`
* `unshift`
* `set`
* `concat`
* `slice`
* assignment through indexing

Questions:

* Are arrays and objects reference values?
* Does assignment create aliases?
* Which functions return a new collection?
* How does deep equality interact with shared mutable values?

Deliverable:

* `docs/rfcs/0013-collection-mutation.md`
* Mutation table.
* Aliasing and copy-semantics examples.
* Conformance tests.

---

## [RFC] L4-6 Function Semantics

Freeze:

* declarations
* first-class function values
* closures
* recursion
* missing arguments
* extra arguments
* return without a value
* functions that emit template output
* anonymous functions, if currently supported
* calling non-function values

Deliverable:

* `docs/rfcs/0014-function-semantics.md`
* Conformance tests for all accepted behavior.

---

# L5 — Template Semantics

## [RFC] L5-1 Template Output Model

Define how these output forms interact:

```pol
echo value
print(value)
println(value)
```

```html
Hello $name
```

```html
<% echo expression %>
```

Also define:

* output buffering
* text emitted before runtime errors
* output from included templates
* response finalization
* behavior after `send_file`
* binary versus text output

Deliverable:

* `docs/rfcs/0015-template-output-model.md`
* Output ordering rules.
* Conformance tests.

---

## [RFC] L5-2 Interpolation and Escaping

Freeze:

* valid `$variable` syntax
* `$$` escaping
* undefined-variable behavior
* interpolation inside HTML attributes
* whether interpolation performs escaping
* canonical HTML escaping function
* distinction between raw output and escaped output

Deliverable:

* `docs/rfcs/0016-interpolation-escaping.md`
* Security guidance.
* Conformance tests.

Do not introduce automatic escaping without a separate accepted RFC.

---

## [RFC] L5-3 Template Comments

Document:

* Polonio block comments
* HTML comment stripping behavior
* interactions with code blocks
* source-location tracking through removed comments

Deliverable:

* `docs/rfcs/0017-template-comments.md`
* Conformance tests.

---

# L6 — Standard Library Stabilization

## [RFC] L6-1 Standard Library Inventory

Create the authoritative inventory of all registered builtins.

For each builtin record:

* canonical name
* aliases
* category
* signature
* side effects
* runtime context requirements
* mutation behavior
* error behavior
* documentation location
* test coverage

Deliverable:

* `docs/STANDARD_LIBRARY_V1.md`

This document must be generated from an implementation audit and manually reviewed.

---

## [BLOCKED] L6-2 Standard Library Consistency Pass

Blocked by:

* L1-1
* L2-2
* L6-1

Apply accepted conventions incrementally across builtin families.

Suggested implementation batches:

1. Core/type/output
2. Strings
3. Arrays/objects
4. Math/date
5. HTTP/request/response
6. Sessions/security
7. Storage/SQLite
8. Uploads/mail

Each batch must preserve compatibility aliases unless an accepted RFC says otherwise.

---

# L7 — Conformance Suite

## [RFC] L7-1 Conformance Test Format

Define a language-level test format independent from internal C++ unit structure.

Suggested layout:

```text
tests/conformance/
  expressions/
  variables/
  control_flow/
  functions/
  collections/
  scope/
  templates/
  includes/
  errors/
  builtins/
```

Each case should contain:

* `.pol` input
* expected stdout
* expected stderr, when applicable
* expected exit status
* optional environment variables
* optional fixture files

Deliverable:

* `docs/rfcs/0018-conformance-suite.md`
* A runner design.
* Three representative test cases.

---

## [BLOCKED] L7-2 Core Language Conformance Cases

Blocked by L7-1 and the relevant semantic RFCs.

Implement conformance fixtures for:

* expressions and precedence
* variables and assignments
* control flow
* functions and closures
* types and conversions
* collections
* errors

---

## [BLOCKED] L7-3 Template Conformance Cases

Blocked by L7-1 and L5 RFCs.

Implement fixtures for:

* text/code segmentation
* interpolation
* inline echo
* includes
* comments
* output order
* error locations

---

## [BLOCKED] L7-4 Standard Library Conformance Cases

Blocked by L6-1 and L7-1.

Create fixtures for every canonical builtin and compatibility alias.

---

# L8 — Compatibility and Release

## [BLOCKED] L8-1 Compatibility Policy

Blocked by L1, L4, L5, and L6 decisions.

Define:

* semantic versioning policy
* source compatibility
* builtin compatibility
* deprecation process
* alias removal policy
* error-message compatibility
* platform compatibility

Deliverable:

* `docs/COMPATIBILITY.md`

---

## [BLOCKED] L8-2 Polonio Language Specification v1.0

Consolidate accepted RFCs into:

* `docs/polonio_language_spec_v1_0.md`

The specification must distinguish:

* required core language behavior
* required standard library
* optional web runtime
* implementation-defined behavior

---

## [BLOCKED] L8-3 v1 Release Candidate

Requirements:

* all required semantic RFCs accepted
* required implementations complete
* conformance suite passing
* standard library documented
* examples passing
* compatibility policy published
* no unresolved v1-blocking issues

---

# Current Next Item

The first item to work on is:

```text
L1-1 Canonical Names and Alias Policy
```

The first pass must only investigate and write:

```text
docs/rfcs/0001-alias-policy.md
```

It must not rename or remove builtins.
