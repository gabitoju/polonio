# RFC 0009 — Truthiness

## Status

Accepted.

## Purpose

This RFC answers only which values are true or false in a boolean context.
It applies to `if`, `elseif`, `while`, and future logical/conditional forms.
It does not define equality, ordering, conversions, precedence, or logical
operator syntax.

## Decision

| Value | Truthiness |
| --- | --- |
| `null` | false |
| `false` / `true` | false / true |
| `0` (including negative zero) | false |
| any other finite number | true |
| `""` | false |
| every other string, including `"0"` | true |
| empty array `[]` | false |
| non-empty array | true |
| empty object `{}` | false |
| non-empty object | true |
| function | true |
| immutable Error view | true |

`if error` is legal and true. Error values are normally inspected for their
safe fields inside `recover`, not used as ordinary condition values, but making
them true preserves the principle that a present object-like value is true and
does not create a special failure control-flow path.

## Rationale

Empty collections are false because `if rows`, `if users`, and `if config`
read naturally as “has content” in web applications. This avoids mandatory
`count(...) > 0` without making ordinary strings surprising. `"0"` remains
true: its textual content is present and it is not PHP's numeric-string
special case. Numbers retain ordinary zero/nonzero behavior. Functions are
always true because they are present callable values, not collections or
results.

This is intentionally a small, predictable model: absence, false, numeric
zero, empty text, and empty collections are false; every other ordinary value
is true.

## Future logical operators

Future `and`, `or`, and `not` evaluate operands through this table and must
short-circuit where their eventual RFC specifies. This RFC neither adds syntax
nor decides their result-value policy.

## Alternatives

PHP is rejected because `"0"` being false is surprising and data-dependent.
JavaScript is rejected for broad coercion and its special values. Python is a
close match for empty collections, zero, and empty strings; Polonio adopts that
readability while retaining its own Error view. Ruby and Lua are rejected
because empty collections and zero remain true, making common web-result
conditions less direct.

## Implementation audit

The current implementation already makes null, false, zero, empty strings,
all non-empty strings, functions, and Error views behave as decided. It makes
all arrays and objects true, including empty ones. That is an implementation
change and a breaking semantic correction for programs relying on `if []` or
`if {}`. No documentation-only divergence is known.

## Follow-up

Implementation must update `Value::is_truthy`, add conformance coverage for
all primary values and Error views in `if`/`elseif`/`while`, and preserve
short-circuit behavior. RFC 0010 — Equality and Comparison is next; it is not
decided here.
