# RFC 0012 — Collection Mutation and Aliasing

## Status

Accepted.

## Decision

Arrays and mutable objects are reference-like values. Assignment, function
argument binding, return, nesting, closure capture, and include execution copy
the collection reference, never collection contents. There is no implicit deep
copy or copy-on-write.

Thus `var b = a; push(b, 3)` makes `count(a)` observe `3`, and `set(b,
"name", "Pedro")` changes `a["name"]`. A parameter can mutate a passed
collection visibly, but `items = []` or `user = {}` only rebinds that local
parameter under RFC 0011. Returned collections remain valid and aliases made
before return remain shared. Nested values retain their aliases: pushing an
`inner` array after storing it in `outer` changes `outer[0]`.

| Operation | Collection behavior |
| --- | --- |
| assignment, parameter, return, nesting | reference copy; no hidden copy |
| variable rebinding | changes only the lexical binding |
| `push`, `pop`, `shift`, `unshift` | mutate array in place |
| `set` | mutates mutable object in place |
| `concat`, `slice`, `keys`, `values`, `range` | return new collection; contained values retain normal aliases |
| `get`, `has_key`, `count`, `join` | read-only |
| structural equality | RFC 0010; independent of aliasing |
| cycles | legal consequence of mutation; cyclic equality raises RFC 0010 RuntimeError |

There is no supported indexed or object-key assignment syntax today; mutation
uses existing builtins. Self-rebinding is harmless. `push(a, a)` and storing a
collection inside itself can create cycles; they remain legal. Serialization
continues to follow its existing runtime boundary contracts.

Functions and Error views retain their RFC 0010 identity behavior; primitive
values are ordinary immutable values. No collection identity operator is added:
aliasing is observable only through mutation.

## Builtin audit

`push`/`unshift` return the new count; `pop`/`shift` return the removed value
or null. `set` returns the assigned value. `concat` and `slice` allocate a new
outer array; `keys` and `values` allocate new arrays. No remove/delete/sort or
reverse collection builtin is registered. Read-only Error views reject `set`.

## Rationale

Reference-like mutable collections give Game of Life natural row construction,
while also making repeated `push(grid, row)` visibly share `row`. Scraps can
pass query rows, users, config, and response objects to helpers without
defensive copying. Structural equality remains useful for value checks even
when two values are distinct collection objects.

Deep-copy assignment, copy-on-write, persistent collections, ownership,
borrowing, move semantics, clone protocols, weak references, collection
identity operators, garbage-collection specification, deterministic
destruction, and concurrency semantics are out of scope.

## Implementation audit

The runtime stores arrays and mutable objects behind shared pointers; existing
assignment, calls, returns, closures, and includes preserve those pointers.
The listed mutation/read-only/new-result builtin behavior already matches this
decision. Cycles are constructible through `push` and `set`, with equality
already governed by RFC 0010. No implementation correction or breaking change
is proposed; follow-up is conformance tests and concise documentation.
