# RFC 0005 — Builtin Error Consistency

## Status

Accepted.

## Context

Polonio registers 99 builtin names. RFC 0004 is authoritative for structural
error categories, locations, adapter responsibilities, message compatibility,
and fatal-per-execution behavior. It deliberately keeps builtin argument
failures in `RuntimeError` rather than creating a public `ArgumentError`.

The complete current `builtins.cpp` audit shows equivalent failures constructed
in several ways: `ensure_arg`, local exact-arity checks, local type checks,
`coerce_int`, storage-path helpers, object extraction, HTTP/session checks,
and SQLite helpers. Most errors have the call site, but many lack structured
facts; their prose varies, and some capability/resource failures are still
`RuntimeError`. v1 needs consistent facts without freezing every English
message. Compatibility aliases must remain behaviorally identical.

## Goals

- Define consistent reasons and metadata for invalid builtin calls.
- Define arity, type, value, shape, context, configuration, and resource rules.
- Make builtin failures testable without full-message equality.
- Preserve canonical/alias equivalence and call-site locations.
- Establish reusable validation and error-construction helpers.

## Non-goals

- A public `ArgumentError` category, stable symbolic error codes, or recovery.
- Changing accepted inputs, redesigning signatures, adding namespaces, or
  removing aliases.
- Rewriting messages merely for style.

## Terminology

An **arity failure** has an invalid argument count. A **type failure** has an
argument of the wrong Polonio value type. A **value failure** has the right
type but an invalid range, format, enum-like value, or state. A **shape
failure** concerns an object, options object, or container structure. An
**unsupported-value failure** rejects a nested value that cannot be represented
or bound. A **context failure** lacks a runtime context. A **configuration
failure** has a present capability lacking required configuration. A
**capability failure** is either context or configuration under RFC 0004. A
**resource failure** is an external resource access failure; an **operation
failure** is an attempted resource/host operation after validation. An
**argument index** is user-facing and one-based. A **canonical builtin** is the
operation named by the manifest; a **compatibility alias** is an additional
registered name for it.

## Current-behavior audit

All registered names were audited against `install_builtins`; aliases share a
callback today but the callback frequently hard-codes the legacy name. The
following is a family summary, not a new behavior specification.

| Family | Current failures and notable inconsistencies |
| --- | --- |
| Conversion/type | `ensure_arg` supplies only a zero-based detail index; `to_number` distinguishes invalid numeric text from an unsupported type, while conversion/string helpers accept broad implicit conversions. `tostring` and `to_string` use separate hard-coded names. |
| Output and escaping | `print`/`println` are variadic; `debug` and `html_escape` use exact local checks while `htmlspecialchars` uses minimum arity, so aliases accept different extra arguments. |
| Strings | Most use `ensure_arg` and stringify values; `substr` uses a bounded local check and `coerce_int`, which truncates non-integral numbers and reports parameter labels rather than indexes. |
| Arrays and objects | Collection/object functions commonly require only missing arguments, allowing surplus arguments. Type failures usually omit an argument number; `get` permits an optional default. |
| Math and dates | Local number/type checks and range checks vary (`sqrt`, `randint`, date parsing); numeric integrality is not uniformly validated. |
| Request and response | Response calls use `require_cgi_context` before or after arity depending on builtin. `status`/`header` callbacks hard-code alias names, and request helpers silently return empty values without context. |
| Sessions and security | Session context is `CapabilityError`, but a missing secret is reported as capability `session secret` rather than a configuration fact. JSON-serialization and secure-RNG failures are generic runtime failures; malformed password hashes return `false`. |
| Storage | Exact arity is local; path typing uses a helper without details. Storage root, path validation, and filesystem outcomes are delegated to storage helpers and are not yet uniformly classified. |
| SQLite | Connection/state failures and prepare/step host messages are currently `RuntimeError`; parameter count/type and unsupported bindings have no uniform facts. |
| Uploads/file responses | Upload object validation is ad hoc; `send_file` and `send_mail` inspect only known options and currently ignore unknown keys. Missing directories/temp files and write failures are runtime failures. |
| Mail | Options and nested header validation are handwritten; recipient/headers/body data can be sensitive, and current messages do not carry a redaction model. |

Existing helpers are `ensure_arg` (minimum arity), `require_string_value`,
`require_array_value`, `coerce_int`, `require_storage_path_arg`,
`require_upload_tmp_path`, `require_cgi_context`, `require_session_context`,
`require_session_key`, `storage_root`/path resolution and storage operations,
`require_db_handle`, and SQLite bind/parameter helpers. They use inconsistent
phrasing, omit metadata, expose internal zero-based indices where present, and
do not consistently distinguish type/value/shape/context/resource causes.

## Structured builtin failure reasons

The conceptual `reason` field is a closed v1 vocabulary: `Arity`, `Type`,
`Value`, `Shape`, `UnsupportedValue`, `Context`, `Configuration`, `Resource`,
and `Operation`. It is a stable structured fact, not a public symbolic error
code. `Internal` is not a builtin-call reason: an unexpected host invariant is
`InternalError` with a safe operation fact.

| Failure | Category | Reason |
| --- | --- | --- |
| Wrong count, type, value, or shape | RuntimeError | Arity, Type, Value, Shape, UnsupportedValue |
| Missing runtime context | CapabilityError | Context |
| Present capability, missing/misconfigured prerequisite | CapabilityError | Configuration |
| External resource unavailable | ResourceError | Resource |
| Resource action fails after validation | ResourceError | Operation |
| Unexpected host invariant | InternalError | no builtin reason |

## Required structured fields

Every builtin failure requires `reason`, `function_name` (the called registered
name), `canonical_function_name`, and its Polonio invocation location. The
existing `function_name` field may serve the first role; a migration adds the
canonical field and remaining facts.

| Reason | Required additional fields |
| --- | --- |
| Arity | `expected_arity_min`, `expected_arity_max`, `actual_arity` |
| Type | `argument_index`, `expected_type`, `actual_type` |
| Value | `argument_index` when attributable, `expected_value`; optional safe `actual_value_summary` |
| Shape | `argument_index`; `option_name` when applicable; `expected_type`/`actual_type` or `expected_value` |
| UnsupportedValue | `argument_index`, `actual_type`, and nested path/option name when known |
| Context | `capability` |
| Configuration | `capability`, `configuration_name` |
| Resource | `resource`, `operation` when known |
| Operation | `operation`, `resource` when applicable; safe causal summary may be retained |

`actual_value_summary` is optional, bounded, and safe only for small
non-sensitive scalar values. It must never contain passwords, password hashes,
secrets, tokens, cookies, request bodies, mail bodies, SQL parameter values,
or header values. Large strings/arrays/objects are summarized by type and size,
not serialized. Resource paths may be retained subject to the resource policy.

## Arity policy

Each callable validates its declared complete arity: exact uses equal min/max;
minimum has max absent; maximum has min zero; bounded ranges carry both;
variadic functions have a minimum and absent maximum. Optional arguments are
bounded ranges, not a sequence of missing-argument checks. Extra arguments
must be rejected unless a signature is explicitly variadic. Thus facts express
exactly 2, at least 1, at most 3, and between 1 and 2 unambiguously. The
formatter should conceptually say `<function>: expected <arity>, got <actual>`.

This is a normalization requirement for implementation, not a retroactive
change in this RFC. Alias pairs must declare and validate the same arity.

## Type policy

User-facing type names are exactly `null`, `bool`, `number`, `string`,
`array`, `object`, and `function`. A union is an ordered `expected_type` list
or display such as `array or object`; C++ names never appear. Implicit
conversion is accepted only where that builtin already documents/implements it;
it is not a type error. Callable requirements use `function`. Container element
requirements use the container argument index plus a nested element path;
options use the option name. `null` is accepted only where the signature says
so, including explicit union types; it is not silently synonymous with omitted.

## Value and range policy

Correct-type invalid inputs use `Value`: ranges (`random_token`, HTTP status),
malformed text (dates, URL escapes), forbidden path/header/recipient values,
invalid transaction state, and invalid enum-like options. `expected_value`
states the constraint (for example `integer between 100 and 599`), not raw
prose. Unsupported nested serialization/SQLite binding uses
`UnsupportedValue`. A safe small non-secret number, bool, or public enum-like
string may appear in `actual_value_summary`; all other values are summarized.
Existing false-return APIs, including password verification and CSRF mismatch,
retain their current accepted behavior and are not converted to errors here.

## Shape and options policy

Non-object options, missing required keys, wrong option types, invalid nested
headers, and missing upload `tmp_path` are `Shape`; a nested non-serializable
element is `UnsupportedValue`. `send_file` and `send_mail` currently ignore
unknown option keys, and no other current builtin has a conflicting options-key
contract. This RFC preserves that behavior: unknown keys are ignored unless a
specific builtin's standard-library contract explicitly rejects them. Future
rejections require signature/documentation and are `Shape` with `option_name`.

## Context and configuration policy

Missing web response, request, session, storage, or SQLite runtime context is
`CapabilityError`/`Context`; an implementation must not silently substitute a
value unless that builtin's existing behavior explicitly does so. Missing or
unusable prerequisite configuration is `CapabilityError`/`Configuration`.
Canonical stable structured identifiers are `web-response`, `web-request`,
`session`, `storage`, and `sqlite`; configuration identifiers are
`POLONIO_STORAGE_PATH`, `POLONIO_SESSION_SECRET`, and `database-connection`.
These identifiers are stable v1 structured values, not codes. `database-
connection` describes an unavailable required connection instance, while a
configured database that fails to open is resource/operation failure.

## Resource and operation policy

Configured files, directories, storage roots, SQLite databases/statements,
uploads, and mail outbox operations use `ResourceError`. Unavailable/missing/
unreadable targets use `Resource`; failed read, write, append, delete,
create-directory, open-database, prepare-statement, execute-statement,
save-upload, or write-mail uses `Operation`. `operation` is a stable structured
identifier. Relative storage paths may be shown locally when they do not expose
a secret; absolute host paths, temporary upload paths, mail bodies, SQL text,
and host error text must be redacted from portable diagnostics. SQLite error
text is retained only as a safe, adapter-redactable cause, never as the primary
message or stable fact. Source loading remains `SourceError` under RFC 0004.

## Human-readable message policy

Messages communicate the facts but are not exact-string API:

```text
<function>: expected <arity>, got <actual>
<function>: argument <n> must be <type>, got <actual-type>
<function>: argument <n> has an invalid value
<function>: option '<name>' must be <type>
<function>: requires <capability> context
<function>: missing required configuration <name>
<function>: failed to <operation> <resource>
```

They avoid C++ names, raw host prose, unexplained abbreviations, inconsistent
punctuation, and sensitive values.

## Alias consistency

Aliases must have identical category, reason, canonical metadata, argument
facts, source location, side effects, accepted inputs, and validation order.
Only `function_name` may differ to identify the invoked registration.
`to_string`/`tostring`, `html_escape`/`htmlspecialchars`, `http_status`/
`status`, and `http_header`/`header` are mandatory conformance pairs. The
current callback-name divergence is migration work.

## Validation order

For a builtin requiring a context, validate deterministically in this order:

1. Arity.
2. Top-level argument types.
3. Object/options shape and nested required types.
4. Values, ranges, and state constraints.
5. Required context/capability and configuration.
6. External resource access.
7. Operation execution.

This makes an invalid call report its local contract before host availability;
for example wrong arity outside HTTP reports `Arity`, and malformed options
while disconnected reports `Shape`. A builtin with no arguments can check
context at step 5. Existing deviations are migration inventory items.

## Testing policy

Conformance tests assert RFC 0004 category, reason, canonical and called name
when aliased, one-based index, types, arity bounds, capability/configuration,
resource/operation facts, and call-site location. They normally avoid complete
English messages; dedicated formatter tests may assert them. The suite needs a
representative case for every reason and every family, alias-pair equivalence,
and secret/large-value redaction. A migration inventory maps each registered
builtin to declared signature and normalized failure paths; an unlisted or
uncovered builtin is unnormalized.

## Implementation strategy

Implement shared arity, type, value/range, option extraction, capability, and
resource-error constructors, then migrate in reviewable batches:

1. Type/conversion/output.
2. Strings and escaping.
3. Arrays and objects.
4. Math and date.
5. Request and response.
6. Sessions and security.
7. Storage.
8. SQLite.
9. Uploads, `send_file`, and mail.
10. Alias conformance and final audit.

No single rewrite of `builtins.cpp` is required.

## Alternatives considered

1. Keep ad hoc messages — rejected: facts and classification remain unstable.
2. Public `ArgumentError` — rejected by RFC 0004's small category hierarchy.
3. Stable symbolic codes — rejected as premature before user-level recovery.
4. Freeze strings — rejected; structured facts are the compatibility contract.
5. Return null/false — rejected; it changes current failure semantics.
6. Per-builtin validation — rejected; it preserves drift.
7. Generate signatures — deferred: useful later, but insufficient for context,
   shape, redaction, and operation rules and too invasive for initial migration.

## Required decisions

All required decisions are resolved: the reason vocabulary and per-reason
fields above; one-based indexing; seven canonical types; min/max arity facts;
safe-value redaction; context/configuration and resource/operation mappings;
ignored-unknown-key default with documented exceptions; validation order; alias
equivalence; conformance testing; and stable capability/configuration
identifiers. Deliberately deferred: generated signatures, exact formatter text,
and user-level matching/recovery (RFC 0006).

## Decision

Builtin invocation failures use the RFC 0004 categories and the nine-reason
model above. Every failure has reason, called and canonical names, and call
site; reason-specific fields are mandatory. Validation is arity, type, shape,
value, context/configuration, resource, then execution. Messages are
meaning-compatible but not exact API. Diagnostics redact secrets and boundedly
summarize values. Aliases are equivalent except for the invoked registered name.
Migration follows the ten batches above.

## Consequences

Positive: portable, testable diagnostics; consistent profile failures; safer
redaction; and a direct foundation for recovery. Negative: migration must touch
many throw sites and preserve intentionally permissive inputs. Cost is
family-by-family helper adoption and conformance coverage. Program behavior is
unchanged by this design RFC; later normalization can improve categories and
facts without freezing prose. Tests shift to structural assertions, and
standard-library documentation gains signatures/options contracts. RFC 0006
can build recovery on reasons and facts without adopting codes now.

## Follow-up work

- Build shared builtin validation/error infrastructure.
- Create family-by-family migration tasks and a complete normalization ledger.
- Add error conformance and formatter/redaction tests.
- Update standard-library signature/options documentation.
- Draft RFC 0006 — User-Level Error Handling.
