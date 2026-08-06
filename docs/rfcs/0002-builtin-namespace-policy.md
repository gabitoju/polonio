# RFC 0002 — Builtin Categorization and Namespace Policy

## Status

Implemented.

## Context

Polonio currently registers 99 global builtin names in
`src/polonio/runtime/builtins.cpp:2218-2317`. The surface spans conversions,
collections, templates, HTTP, storage, SQLite, sessions, uploads, mail, and
security. A v1 organization policy is needed before more APIs are added. This
RFC defines categories and compatibility policy; it introduces neither syntax
nor namespace objects.

## Goals

- Classify the existing builtin surface.
- Define core, standard-library, and optional runtime layers.
- State whether global names remain valid in v1.
- State whether namespaces are a v1 or future feature.
- Prevent uncontrolled growth of global names.

## Non-goals

- Implementing namespace syntax or modifying parser behavior.
- Renaming current functions or removing aliases.
- Designing a module system or separate binaries.
- Changing builtin semantics.

## Current Surface

| Family | Current names (canonical first where applicable) | Count | Runtime-dependent? | Proposed layer |
|---|---|---:|---|---|
| Type/conversion | `type`, `to_string`, `to_number`; compatibility `tostring` | 4 | no | Core + compatibility |
| Template output | `print`, `println` | 2 | output buffer | Template runtime |
| General string | `len`, `substr`, `lower`, `upper`, `trim`, `replace`, `split`, `contains`, `starts_with`, `ends_with`, `nl2br` | 11 | no | Standard library |
| Escaping | `html_escape`; compatibility `htmlspecialchars` | 2 | no | Standard library + compatibility |
| Collections/objects | `count`, `push`, `pop`, `shift`, `unshift`, `concat`, `join`, `slice`, `range`, `keys`, `has_key`, `get`, `set`, `values` | 14 | no | Core (`count`) and standard library (remainder) |
| Math/type predicates | `abs`, `floor`, `ceil`, `round`, `pow`, `sqrt`, `rand`, `randint`, `min`, `max`, `is_null`, `is_bool`, `is_number`, `is_string`, `is_array`, `is_object`, `is_function` | 17 | no | Standard library |
| Date | `now`, `date_parts`, `date_format`, `date_add_days`, `date_parse` | 5 | clock for `now` | Standard library |
| URL utility | `urlencode`, `urldecode` | 2 | no | Standard library |
| HTTP response | `http_status`, `http_header`, `http_content_type`, `redirect`; compatibility `status`, `header` | 6 | response/CGI context | Web runtime + compatibility |
| HTTP request | `request_body`, `request_header`, `request_headers`, `cookies`, `request_json` | 5 | request/CGI context | Web runtime |
| Sessions | `session_get`, `session_set`, `session_unset`, `session_clear` | 4 | request/session secret | Web runtime |
| Security | `random_token`, `csrf_token`, `csrf_verify`, `hash_password`, `verify_password` | 5 | CSRF requires session; others do not | Web runtime reference surface |
| Storage | `file_read`, `file_write`, `file_append`, `file_exists`, `file_delete`, `file_size`, `file_modified`, `dir_create`, `dir_list`, `dir_exists` | 10 | `POLONIO_STORAGE_PATH` | Data runtime |
| SQLite | `db_connect`, `db_close`, `db_query`, `db_exec`, `db_last_insert_id`, `db_begin`, `db_commit`, `db_rollback` | 8 | SQLite and storage root | Data runtime |
| Upload/file/mail | `upload_save`, `send_file`, `send_mail` | 3 | web response/request and storage root | Web runtime (with data dependency) |
| Development | `debug` | 1 | stderr | Development surface |

This inventory accounts for all 99 registered names. RFC 0001 defines the
canonical spelling of its four alias groups: `to_string`, `html_escape`,
`http_status`, and `http_header`.

## Layer Model

The layers are conformance profiles, not namespaces and not parser syntax.

### Layer 1 — Core Language Support

Every conforming implementation must provide language syntax/semantics plus
`type`, `to_string`, `to_number`, and `count`. Their compatibility alias
`tostring` remains accepted under RFC 0001 but is not a separate operation.

### Layer 2 — Standard Library

The reference distribution must provide general string, escaping, collection,
object, math, predicate, date, and URL utilities listed above. A minimal
language implementation may omit this layer and still be language-conforming,
provided it declares that profile. `html_escape` is the canonical escape name;
`htmlspecialchars` remains a compatibility alias.

### Layer 3 — Template Runtime

The template reference runtime provides `print` and `println` through its
output buffer. Template scanning, interpolation, includes, and the `echo`
statement are runtime/syntax facilities, not registered builtins. This layer
is mandatory for a conforming template-runtime implementation, not for a
non-template language implementation.

### Layer 4 — Web Runtime

HTTP response/request, session, CSRF, password, upload, file-response, and
mail APIs form the reference web runtime. They require the appropriate
request, response, or session context where the implementation does so today.
`http_status` and `http_header` are canonical; `status` and `header` are
compatibility aliases. This layer is optional for language conformance and
mandatory only for a declared web-runtime profile.

### Layer 5 — Data Runtime

Sandboxed storage and SQLite are data-runtime capabilities. Both use the
storage-root model in the current reference implementation; SQLite also
requires the SQLite library. This layer is optional for language conformance
and mandatory only for a declared data-runtime profile.

`debug` is a development surface, outside required language and reference
runtime profiles. Compatibility aliases are a cross-cutting designation, not
an additional operational layer.

## Global Name Policy

**Accepted option: keep current globals in v1 and defer namespaces beyond
v1.** All current canonical names and compatibility aliases remain global and
functional throughout v1.x. No parallel `file.*`, `db.*`, `http.*`, or
`session.*` aliases will be added before a module/namespace RFC is accepted.

Option B is not viable in v1: the parser implements indexing (`value[index]`)
and `..` concatenation, but no member-access token or namespace-object model.
Adding namespace syntax would therefore require parser, runtime, and module
design work outside this RFC. Categories are documentation and conformance
profiles, not injected objects.

## New Builtin Naming Rules

1. New multiword global builtin names use `snake_case`.
2. Runtime-specific operations use established family prefixes where practical:
   `http_`, `request_`, `session_`, `file_`, `dir_`, and `db_`.
3. A short global is acceptable only for fundamental core/standard-library
   operations with a clear, non-runtime-specific meaning.
4. New generic globals that could collide with language, collection, or future
   module vocabulary require an RFC. Existing `get`, `set`, `keys`, and
   `values` remain stable v1 standard-library names, but are not precedent for
   adding more generic globals.
5. Functions are verb-first when they perform an action (`file_read`,
   `session_set`); predicates use `is_`; collection names use established
   singular/plural meanings (`key` argument, `keys` result).
6. New aliases are forbidden by default. They require an explicit compatibility
   rationale and an RFC decision; RFC 0001 governs the existing four groups.

## Conformance Policy

| Profile | Required capabilities |
|---|---|
| Language | Syntax/semantics plus Layer 1 canonical operations. Web and SQLite are not required. |
| Reference distribution | Language, Layer 2, Layer 3, and the current reference implementations of Layers 4 and 5. |
| Template runtime | Language plus Layer 3; Layer 2 is expected when claiming reference compatibility. |
| Web runtime | Language/reference library plus Layer 4, with documented request/response/session context requirements. |
| Data runtime | Language/reference library plus Layer 5, with documented storage/SQLite availability. |

An implementation identifies optional capabilities in its conformance claim and
documentation. The current implementation has no runtime capability-discovery
API; this RFC does not invent one.

## Documentation Policy

Builtin references group functions by the layers/families above. Runtime-
dependent entries state their required context and environment variables.
Optional layers are labelled in runtime documentation. Canonical names and
aliases follow RFC 0001. The language specification describes syntax and core
requirements, then references runtime manuals for web/data details rather than
duplicating every runtime API.

## Alternatives Considered

1. **Keep every builtin global without classification.** Rejected: it leaves
   the public surface ungoverned.
2. **Introduce namespaces immediately.** Rejected: no member access, namespace
   objects, or module system exists today.
3. **Remove web/data functions from the official distribution.** Rejected: they
   are implemented reference capabilities; profiles describe their optionality
   without removal.
4. **Expose capabilities only through injected objects.** Rejected: it requires
   the same unresolved syntax/object/module work as immediate namespaces.
5. **Delay all policy until a module system exists.** Rejected: a stable global
   policy is necessary before adding APIs, and module work is independent.

## Decision

- Existing global builtins are stable throughout v1.x.
- Namespace syntax is not part of v1 and is deferred until a module-system RFC
  defines required syntax, objects, loading, and compatibility migration.
- Layers 1–5 above are the accepted classification and conformance model.
- New globals require the naming rules above; a novel generic global or any
  new alias requires an RFC.
- Web and data/SQLite APIs are not language-conformance requirements.
- The reference distribution ships all currently implemented layers, while
  alternate implementations declare the profiles they provide.

## Consequences

Positive: existing code remains compatible, the global surface has explicit
growth rules, and non-web implementations have a clear conformance path.

Negative: v1 retains a large global namespace, documentation must explain
profiles, and namespace ergonomics are deferred until module-system design.

## Follow-up Work

- L1-3 language/runtime boundary refinement.
- Standard-library inventory and documentation regrouping.
- Module-system and namespace RFC.
- Conformance-profile and capability-discovery design.
- Review of existing generic globals such as `get` and `set` in a future major
  version.
