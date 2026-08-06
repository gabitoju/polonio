# Polonio v1 Builtin Inventory

The reviewed machine-readable inventory is
[builtins_v1_manifest.tsv](builtins_v1_manifest.tsv). It lists every
registered name, canonical target, numbered layer, category, and
Compatibility/Development designation, and is validated against
`install_builtins` by `tools/validate_builtin_manifest.sh`.

The runtime has 99 registrations: Layer 1: 5; Layer 2: 51; Layer 3: 2;
Layer 4: 23; Layer 5: 18. Compatibility designations: `tostring`,
`htmlspecialchars`, `status`, `header`; Development: `debug`.

## Profile availability

Each builtin belongs to exactly one numbered layer. **Profiles** below means
the profiles that guarantee that registered name: it is not an additional
registration or namespace. Language Core is inherited by every profile.
Reference Standard Library is required by Web Runtime and Data Runtime;
Template Runtime is independently claimable and may provide Layer 2 as an
additional reference-compatible capability. The Reference Distribution ships
every listed builtin.

| Registered builtin(s) | Canonical operation | Layer / category | Profiles | Designation |
|---|---|---|---|---|
| `type`, `to_string`, `tostring`, `to_number`, `count` | `type`, `to_string`, `to_number`, `count` | Language Core / core | Language Core; Standard Library; Template Runtime; Web Runtime; Data Runtime; Reference Distribution | `tostring`: Compatibility |
| `print`, `println` | same name | Template Runtime / template output | Template Runtime; Reference Distribution | — |
| `debug` | `debug` | Standard Library / debug | Standard Library; Web Runtime; Data Runtime; Reference Distribution | Development |
| `nl2br` | `nl2br` | Standard Library / string | Standard Library; Web Runtime; Data Runtime; Reference Distribution | — |
| `html_escape`, `htmlspecialchars` | `html_escape` | Standard Library / escaping | Standard Library; Web Runtime; Data Runtime; Reference Distribution | `htmlspecialchars`: Compatibility |
| `len`, `substr`, `lower`, `upper`, `trim`, `replace`, `split`, `contains`, `starts_with`, `ends_with` | same name | Standard Library / string | Standard Library; Web Runtime; Data Runtime; Reference Distribution | — |
| `push`, `pop`, `shift`, `unshift`, `concat`, `join`, `slice`, `range` | same name | Standard Library / collection | Standard Library; Web Runtime; Data Runtime; Reference Distribution | — |
| `keys`, `has_key`, `get`, `set`, `values` | same name | Standard Library / object | Standard Library; Web Runtime; Data Runtime; Reference Distribution | — |
| `abs`, `floor`, `ceil`, `round`, `pow`, `sqrt`, `rand`, `randint`, `min`, `max` | same name | Standard Library / math | Standard Library; Web Runtime; Data Runtime; Reference Distribution | — |
| `is_null`, `is_bool`, `is_number`, `is_string`, `is_array`, `is_object`, `is_function` | same name | Standard Library / predicate | Standard Library; Web Runtime; Data Runtime; Reference Distribution | — |
| `now`, `date_parts`, `date_format`, `date_add_days`, `date_parse` | same name | Standard Library / date | Standard Library; Web Runtime; Data Runtime; Reference Distribution | — |
| `urlencode`, `urldecode` | same name | Standard Library / URL | Standard Library; Web Runtime; Data Runtime; Reference Distribution | — |
| `http_status`, `status`, `http_header`, `header`, `http_content_type`, `redirect` | `http_status`, `http_header`, `http_content_type`, `redirect` | Web Runtime / response | Web Runtime; Reference Distribution | `status`, `header`: Compatibility |
| `request_body`, `request_header`, `request_headers`, `cookies`, `request_json` | same name | Web Runtime / request | Web Runtime; Reference Distribution | — |
| `session_get`, `session_set`, `session_unset`, `session_clear` | same name | Web Runtime / session | Web Runtime; Reference Distribution | — |
| `random_token`, `csrf_token`, `csrf_verify`, `hash_password`, `verify_password` | same name | Web Runtime / security | Web Runtime; Reference Distribution | — |
| `upload_save` | `upload_save` | Web Runtime / upload | Web Runtime; Reference Distribution | — |
| `send_file` | `send_file` | Web Runtime / response file | Web Runtime; Reference Distribution | — |
| `send_mail` | `send_mail` | Web Runtime / mail | Web Runtime; Reference Distribution | — |
| `file_read`, `file_write`, `file_append`, `file_exists`, `file_delete`, `file_size`, `file_modified`, `dir_create`, `dir_list`, `dir_exists` | same name | Data Runtime / storage | Data Runtime; Reference Distribution | — |
| `db_connect`, `db_close`, `db_query`, `db_exec`, `db_last_insert_id`, `db_begin`, `db_commit`, `db_rollback` | same name | Data Runtime / SQLite | Data Runtime; Reference Distribution | — |

Layer 4 APIs require their documented Web Runtime context. Layer 5 APIs
require their documented Data Runtime storage/SQLite availability. In the
reference distribution, `upload_save`, `send_file`, and `send_mail` also use
the storage model. See [CONFORMANCE_PROFILES.md](CONFORMANCE_PROFILES.md) and
[CONFORMANCE_MATRIX.md](CONFORMANCE_MATRIX.md) for the profile contract.

## Global-name and namespace policy

All current global builtins remain stable through v1.x. Namespace syntax and
namespaced aliases are not part of v1. New aliases are forbidden by default;
new generic globals require an RFC. Runtime-specific names use established
`http_`, `request_`, `session_`, `file_`, `dir_`, and `db_` prefixes. Existing
`get`, `set`, `keys`, and `values` remain valid but do not justify new generic
globals.
