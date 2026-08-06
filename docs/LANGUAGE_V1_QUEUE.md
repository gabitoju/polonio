# Polonio Language v1 Queue

This queue turns the existing implementation into a specified, compatible
Polonio v1. Work from top to bottom: write and accept an RFC before its
implementation, and do not move far ahead of accepted work. Freezing happens
only in L4, when the v1 specifications and compatibility contract exist.

## Statuses

- `RFC` — no proposal yet.
- `DISCUSSING` — proposal under review.
- `ACCEPTED` — decision made; implementation may follow.
- `IMPLEMENTED` — code, tests, and documentation match the accepted decision.
- `FROZEN` — part of the L4 v1 compatibility contract.
- `BLOCKED` — waits on an earlier decision.

# L1 — Language Foundation

**Status: IMPLEMENTED.** L1 established the public surface and runtime
boundary. Its final `FROZEN` state remains blocked until L4 publishes the v1
specification and compatibility contract. The completed subitems below are
preserved as implementation history.

## [IMPLEMENTED] RFC 0001 — Canonical Names and Alias Policy

- [DONE] Apply canonical names to public documentation.
- [DONE] Add alias conformance tests.
- [BLOCKED] Freeze the alias policy in Language Specification v1.

See `docs/rfcs/0001-alias-policy.md`.

## [IMPLEMENTED] RFC 0002 — Builtin Categorization and Namespace Policy

- [DONE] Create the authoritative built-in inventory.
- [DONE] Apply layer classifications to public documentation.
- [DONE] Add built-in classification validation.
- [BLOCKED] Freeze the built-in and namespace policy in Language Specification v1.

See `docs/rfcs/0002-builtin-namespace-policy.md`.

## [IMPLEMENTED] RFC 0003 — Core Language vs Runtime Boundary

- [DONE] Publish conformance profiles and matrix.
- [DONE] Apply the runtime boundary to documentation.
- [DONE] Validate built-in profile assignments.
- [BLOCKED] Freeze the language/runtime boundary in Language Specification v1.

See `docs/rfcs/0003-language-runtime-boundary.md`.

## [IMPLEMENTED] Editorial and documentation synchronization

The public documentation has been reviewed and synchronized with the accepted
L1 decisions. Future edits must preserve the distinction between Language Core,
the Standard Library, and optional runtime profiles.

# L2 — Language Semantics

**Purpose:** define the observable behavior of Polonio programs. Each entry is
a planning title, not an accepted design, unless its RFC says otherwise.

1. [IMPLEMENTED] RFC 0004 — Error Model
   - [DONE] RFC 0004a — Structured error representation
   - [DONE] RFC 0004b — Error category mapping
   - [DONE] RFC 0004c — Adapter presentation consistency
   - [DONE] RFC 0004d — Error-model conformance tests
   - [BLOCKED] RFC 0004e — Freeze error model in Language Specification v1
2. [IMPLEMENTED] RFC 0005 — Builtin Error Consistency
   - [DONE] RFC 0005a — Structured builtin failure metadata
   - [DONE] RFC 0005b — Shared validation helpers
   - [DONE] RFC 0005c — Core and standard-library migration
   - [DONE] RFC 0005d — Web-runtime migration
   - [DONE] RFC 0005e — Data-runtime migration
   - [DONE] RFC 0005f — Alias and family conformance tests
   - [DONE] RFC 0005g — Final builtin error audit
   - [BLOCKED] RFC 0005h — Freeze builtin error contract in Language Specification v1
3. [ACCEPTED] RFC 0006 — Failure Philosophy
   - [BLOCKED] Define recovery block semantics and syntax after this RFC.
   - [BLOCKED] Define a read-only structured Error value after recovery semantics.
4. [RFC] RFC 0007 — Type and Conversion Semantics
5. [RFC] RFC 0008 — Truthiness
6. [RFC] RFC 0009 — Equality and Comparison
7. [RFC] RFC 0010 — Scope and Closures
8. [RFC] RFC 0011 — Collection Mutation and Aliasing
9. [RFC] RFC 0012 — Function Semantics
10. [RFC] RFC 0013 — Include Semantics
11. [RFC] RFC 0014 — Evaluation and Output Order
12. [RFC] RFC 0015 — Template Interpolation and Escaping

RFC 0004 is the current item. It must map present behavior before RFC 0005
normalizes built-in failures or RFC 0006 considers user-level recovery.

# L3 — Ecosystem

**Purpose:** define the official developer experience after L2 semantics
stabilize. Do not design or implement these topics yet.

Planning topics include project layout, a conformance runner, formatter,
linter, testing conventions, package or module tooling, documentation
generation, debugger or diagnostics, and distribution and embedding guidance.

# L4 — v1 Release

**Purpose:** consolidate and freeze Polonio v1.

Expected deliverables:

- Language Specification v1.0
- Standard Library v1.0
- Runtime Profile specifications
- compatibility policy
- conformance suite
- release checklist and release notes
- freeze of accepted and implemented RFCs

L4 is also the point at which the L1 items above can become `FROZEN`.
