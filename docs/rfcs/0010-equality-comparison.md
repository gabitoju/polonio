# RFC 0010 — Equality and Comparison

## Status

Implemented.

## Purpose

This RFC defines the observable meaning of the existing `==`, `!=`, `<`,
`>`, `<=`, and `>=` operators. It adopts one predictable, non-coercing model
for small application programs; it does not add operators or alter RFC 0008
conversion semantics or RFC 0009 truthiness.

## Decision

Equality compares values without conversion. Truthiness is irrelevant: `0 ==
false`, `"" == false`, and `[] == false` are all false. `!=` is exactly the
logical negation of `==`.

| Left / right | Equality |
| --- | --- |
| `null` / `null` | true |
| bool / bool | boolean value |
| number / number | numeric value; `0 == -0` is true |
| string / string | exact byte value |
| array / array | structural value equality |
| object / object | structural value equality |
| function / function | reference identity |
| Error view / Error view | reference identity |
| different public types, including Error view / object | false |

`null` is therefore unequal to every non-null value. Numbers are finite under
RFC 0008, so no NaN or infinity equality rule is required. Strings receive no
locale processing or Unicode normalization.

Arrays are equal when they have the same length and every corresponding
element is equal recursively in the same order. `[1, 2] == [1, 2]` is true;
`[1, 2] == [2, 1]` and `[1] == ["1"]` are false. Objects are equal when they
have the same set of string keys and equal values at every key; insertion and
iteration order do not matter. Thus `{"a": 1, "b": 2}` equals `{"b": 2,
"a": 1}`.

Functions compare only by reference identity. Assigning a function value to a
second variable preserves identity, but separately-created functions are
unequal even if their source, parameters, or captured values match. Builtin
functions follow the same rule. Error views also compare only by reference
identity. They are not ordinary objects for equality: this avoids making their
safe diagnostic projection an equality contract or exposing hidden error
details.

Structural comparison applies to acyclic arrays and ordinary objects. Existing
mutable collection operations can form cycles, so equality of a value whose
reachable array/object graph is cyclic raises a non-recoverable `RuntimeError`
rather than recursing indefinitely. The implementation must detect this case;
no coinductive or graph-isomorphism equality is part of v1.

## Ordering

Ordering is intentionally narrow and never coerces.

| Type pair | Ordering |
| --- | --- |
| number / number | normal finite numeric ordering |
| string / string | byte-wise lexicographic ordering |
| all other pairs, including mixed types | non-recoverable `RuntimeError` |

String ordering is byte ordering, not locale-aware or human-language
collation. `"a" < "b"` is true; `2 < "10"` is a `RuntimeError`, not false.
`null`, bool, array, object, function, and Error view have no ordering. An
invalid ordered comparison is a programming error under RFC 0006 and cannot
be recovered by `attempt`/`recover`.

## Chained comparisons

The current grammar parses repeated comparison operators as ordinary
left-associative binary expressions. It does not provide Python-style chained
comparison semantics. For example, `1 < x < 10` means `(1 < x) < 10`; its
first comparison produces a bool, so the next ordered comparison is invalid
and raises `RuntimeError`. This RFC does not change the grammar.

## Rationale and application fit

Structural collection equality makes ordinary data assertions useful without
coercion, while identity equality keeps callable and diagnostic runtime values
opaque. Numeric comparisons directly support Game of Life expressions such as
`neighbors == 2`, `neighbors == 3`, `row >= 0`, and `row < count(grid)`.
Exact string equality and numeric status comparisons support Scraps patterns
such as `method == "POST"`, `username == stored_username`, `user_id ==
session_get("user_id")`, `count(rows) == 0`, and `status >= 400`. No loose
equality is needed for these cases.

## Current implementation audit

| Area | Current behavior | Accepted behavior / classification |
| --- | --- | --- |
| `==` / `!=` for null, bool, number, string | same-storage comparison; `!=` negates `==` | matches; conformance tests required |
| Cross-type equality | false | matches; conformance tests required |
| Array equality | recursive, length- and order-sensitive structural equality | matches for acyclic values; cycle detection is an implementation correction |
| Object equality | recursive structural equality by key set and values | matches for acyclic ordinary objects; cycle detection is an implementation correction |
| Function equality | compares name, parameters, body representation, and closure representation | must become identity equality; potentially breaking implementation correction |
| Builtin function equality | compares registration name, callback, and canonical name | must become identity equality; potentially breaking implementation correction |
| Error-view equality | compares the exposed immutable object structurally | must become identity equality; potentially breaking implementation correction |
| Numeric ordering | numbers only, no coercion | matches; conformance tests required |
| String ordering | rejected because ordering currently requires numbers | add byte-wise ordering; implementation correction |
| Other/mixed ordering | rejected through numeric requirement | matches required `RuntimeError`; diagnostics need comparison/operator and type conformance coverage |
| Chained comparisons | left-associative ordinary binary expressions | documentation-only clarification |

The reference implementation uses active `(left, right)` container pairs for
structural equality, so repeated acyclic sharing is valid while a recursive
pair raises `RuntimeError`. Function and Error views carry opaque identities;
only copied references compare equal. String ordering compares raw bytes.

## Out of scope

This RFC rejects or defers `===`, `!==`, spaceship/comparator operators,
custom comparison hooks, operator overloading, implicit coercion,
locale-aware collation, user-defined equality, hashing contracts, and a total
ordering across all types. These are not required for the v1 application
goals.

## Follow-up

Implementation must preserve the accepted no-conversion rule, add conformance
coverage for every equality and ordering row, provide the required fatal
`RuntimeError` behavior, and align the language documentation. RFC 0011 —
Scope and Closures remains separate and is not decided here.
