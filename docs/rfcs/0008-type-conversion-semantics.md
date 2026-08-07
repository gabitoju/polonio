# RFC 0008 — Type and Conversion Semantics

## Status

Accepted.

## Decision

Polonio v1 has exactly seven primary public types: `null`, `bool`, `number`,
`string`, `array`, `object`, and `function`. `type` returns one of these names;
for ordinary values exactly one `is_*` predicate is true. User-defined and
builtin functions are first-class `function` values.

The RFC 0007 Error binding is not an eighth type. It is a privileged immutable
object-like runtime view: `type(error)` is `"object"`, `is_object(error)` is
true, normal bracket reads and object read helpers work, and `set(error, ...)`
raises non-recoverable `RuntimeError`. It may be assigned, passed, returned,
and stored, but cannot be constructed by code or serialized through sessions,
JSON, SQLite binding, or other transport APIs. Only RFC 0004/0005 safe facts
are exposed; private causes remain hidden.

`number` is one type: `1` and `1.5` have no observable integer distinction.
Source literals are finite decimal numbers. The v1 contract does not expose a
C++ representation; NaN and infinity have no portable literal spelling and
are not portable values. Arithmetic accepts numbers only; no bool, null, or
string coercion occurs. Division and remainder reject zero. Integer-only APIs
(array indexes, range/count/random/date parameters) require finite
integral-valued numbers, never truncation. Negative zero need not be distinct.
Finite integral display omits `.0`; display is locale-independent with `.` and
may use scientific notation. Precision/range beyond a stable finite display
policy is implementation-defined pending conformance work.

Strings are byte strings. Literal escapes are the current quoted escapes;
`len`, `substr`, `lower`, and `upper` operate on bytes, not Unicode characters.
v1 makes no UTF-8 validity or character-boundary guarantee. Runtime-provided
strings may contain arbitrary non-NUL bytes; no literal NUL escape is defined.
`send_file` is response transfer, not string conversion.

Arrays are ordered, zero-based heterogeneous collections. They may contain all
primary values and Error views. Array indices require non-negative integral
numbers; negative, fractional, numeric-string, and out-of-range indexes are
RuntimeError. Arrays display as `[array]` and cannot convert to number.
Objects map string keys to heterogeneous values; literal and bracket keys are
strings, no dot access exists, missing keys read as `null`, and duplicate
literal keys use the last value. Iteration order is not guaranteed. Objects
display as `[object]` and cannot convert to number. Mutation/aliasing is
deferred to RFC 0012.

`null` is the absence value, is permitted in arrays/objects, displays as `""`,
and converts to `0`; missing keys and explicit null both read as null, with
`has_key` distinguishing them. `bool` displays as `"true"`/`"false"` and
converts to `1`/`0`, but is not numeric.

## Explicit conversion matrix

`to_string` and compatibility alias `tostring` use standard display conversion,
not debug formatting. `to_number` is strict: a trimmed string must wholly match
a signed decimal with optional fraction/exponent. Empty trimmed strings convert
to `0` for compatibility. Partial parses, hex/binary/octal, NaN/Infinity text,
overflow, and non-finite results are errors.

| From | string/display | number |
| --- | --- | --- |
| null | `""` | `0` |
| bool | `"true"` / `"false"` | `1` / `0` |
| number | numeric display | identity |
| string | identity | strict whole decimal or RuntimeError |
| array | `[array]` | RuntimeError |
| object | `[object]` | RuntimeError |
| function | `[function]` | RuntimeError |
| Error view | `[object]` | RuntimeError |

## Implicit contexts

| Context | Policy |
| --- | --- |
| Arithmetic / unary minus | number only; no conversion |
| `..` / `..=` | display-convert both operands |
| `echo`, `print`, `println`, interpolation | display conversion |
| `debug` | diagnostic representation, not conversion |
| Boolean condition | deferred to RFC 0009 |
| Array index | finite non-negative integral number only |
| Object bracket key | string only |
| Builtin argument | signature-specific; no global coercion |
| Equality/comparison | deferred to RFC 0010 |

Thus `"count=" .. 5` is allowed display concatenation, while `"10" + 5`
is RuntimeError. Output formatting is not general coercion. Conversion failure
is RuntimeError with RFC 0005 type/value facts where applicable, has no
sentinel value, and cannot enter RFC 0007 recovery.

## Serialization boundary

JSON, sessions, SQLite mapping, mail formatting, HTTP parsing, file bytes, and
`send_file` are runtime protocols, not `to_string`/`to_number` conversion.

## Current implementation audit

| Difference | Classification |
| --- | --- |
| `std::stod` accepts implementation-specific NaN/Infinity and overflow behavior | implementation change required |
| `coerce_int`, `range`, `randint`, `substr`, and date helpers truncate fractions | implementation change required |
| default stream number formatting does not provide the required finite display contract | implementation change required |
| read-only Error view reports `object`, but `is_object`, `keys`, `values`, `has_key`, and `get` do not consistently accept it | implementation change required |
| byte strings, strict interpreter array indexing, display placeholders | documentation/test conformance work |
| truthiness, equality, aliasing, function comparison, template escaping | intentionally deferred |

## Alternatives and consequences

JavaScript/PHP-style coercion and permissive partial parsing are rejected;
strict arithmetic plus output-only formatting is adopted. Separate integers and
floats are rejected. Error as an eighth type is rejected in favor of an
immutable object view. Byte strings are adopted over unimplemented Unicode
character semantics. This improves predictability but requires numeric and
Error-introspection migration plus conversion conformance tests.

Follow-up implementation work is to normalize number parsing/formatting and
integral APIs, complete Error object introspection, and add tests. RFC 0009 —
Truthiness is next. Truthiness, equality/comparison, mutation/aliasing, and
serialization shapes are intentionally not decided here.
