# RFC 0011 — Scope and Closures

## Status

Implemented.

## Purpose

This RFC defines the small lexical-scope model used by ordinary Polonio
programs. It preserves the coherent reference behavior needed by functions,
closures, loops, `recover`, includes, and templates.

## Decision

Lookup is lexical: current environment, then lexical parents through the
top-level environment. A missing name raises the existing non-recoverable
`RuntimeError`; there are no implicit globals.

`var name = value` creates or replaces `name` in the current environment.
Redeclaration there is legal. An inner `var` may shadow an outer binding.
Plain and compound assignment update the nearest existing lexical binding.
When plain `name = value` finds no binding, it creates one in the current
environment; compound assignment requires an existing binding.

| Construct | New scope? | Notes |
| --- | --- | --- |
| top level | yes | Root environment for one execution; builtins and runtime globals are installed here. |
| function invocation | yes | Child of captured lexical environment; parameters and self-name are local. |
| `if` / `elseif` / `else` | no | Selected body uses its current environment. |
| `while` | no | Condition and body use the current environment. |
| `for` | yes, per iteration | Fresh child environment contains loop bindings. |
| `attempt` body | no | Ordinary declarations and assignments use the current environment. |
| `recover` body | yes | Fresh child environment; optional Error binding exists only here. |
| `include` | no | Included source uses the caller's current interpreter environment. |
| template code block | no | Blocks in one render share the interpreter environment. |

## Functions and closures

Parameters and locals belong to the invocation environment and can shadow
outer names. Lookup reaches captured lexical bindings, so top-level values are
visible to functions and nested functions can see outer parameters and locals.
The named function is also local to each invocation, supporting recursion.

Closures capture lexical bindings by reference, not by value snapshot. A later
outer assignment is visible to the closure. Assignment from an inner function
updates the nearest captured binding, so a `counter`/`next` closure can mutate
`n` without `nonlocal`. Parameters can be reassigned. Argument and collection
aliasing remain RFC 0012 work.

## Blocks and loops

`if`, `elseif`, `else`, and `while` are not block scopes: a `var` in an
executed body remains in the current environment afterward. Each `for`
iteration has a fresh environment for its value and optional index/key names;
they do not leak after the loop. Closures made in iterations capture that
iteration's binding. Outer assignments in a loop still update outer bindings.

## Recovery, includes, and templates

`attempt` does not create an ordinary scope. On an operational failure,
`recover error` runs in a child environment; `error` is an immutable Error view
only inside recover, not attempt, and does not leak after `end`. Other recover
declarations are also local to recover.

An include runs in the caller environment: it can read caller names, introduce
variables/functions visible after the include, and assign through the normal
rule. Nested includes preserve this environment. Template text and code blocks
for one render share it, so earlier declarations are visible later.

Builtins and `_GET`, `_POST`, `_COOKIE`, `_SERVER`, and `_FILES` are ordinary
root-environment bindings. Local declarations can shadow them; this RFC adds
no protected namespace or separate resolution.

## Application fit

Game of Life can use function-local counters and nested loops. Scraps can keep
top-level configuration/current-user bindings while `auth.pol` and `routes.pol`
run in caller scope and define helpers. No dynamic scope or module system is
needed.

## Implementation audit

| Accepted rule | Current behavior | Classification |
| --- | --- | --- |
| lexical lookup and missing-name RuntimeError | parent-chain lookup | matches; documentation/test work |
| local `var`, redeclaration, shadowing | `set_local` in current environment | matches; documentation/test work |
| nearest-binding assignment; missing plain assignment creates local | `Env::assign` | matches; documentation/test work |
| closure capture, mutation, and recursion | captured parent plus invocation-local self-name | matches; documentation/test work |
| no `if`/`while` scope | bodies do not replace the environment | matches; documentation-only |
| fresh `for` iteration scope | new child environment each iteration | matches; documentation/test work |
| attempt/recover scope | attempt is current; recover is a child | matches; documentation/test work |
| include/template shared environment | renderer reuses one interpreter | matches; documentation/test work |

No implementation correction or breaking change is proposed.

## Out of scope

Dynamic scope, `global`, `nonlocal`, `let`/`const`, modules/import namespaces,
private module scope, explicit/copy capture lists, declaration hoisting,
temporal dead zones, and universal block-level lexical environments are
rejected or deferred as unnecessary for current goals.

## Follow-up

Conformance coverage verifies the accepted behavior; no runtime semantic
correction was required. RFC 0012 — Collection Mutation and Aliasing remains
separate.
