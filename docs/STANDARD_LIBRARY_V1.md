# Polonio v1 Builtin Inventory

The reviewed machine-readable inventory is [builtins_v1_manifest.tsv](builtins_v1_manifest.tsv). It lists every registered name, canonical target, numbered layer, category, and Compatibility/Development designation. It is validated against `install_builtins` by `tools/validate_builtin_manifest.sh`.

The runtime has 99 registrations: Layer 1: 5; Layer 2: 52; Layer 3: 2; Layer 4: 29; Layer 5: 18. Compatibility designations: `tostring`, `htmlspecialchars`, `status`, `header`; Development: `debug`.

## Profiles

Language Conformance requires syntax/core semantics and Layer 1, not Web or Data Runtime. Reference Standard Library requires Layers 1–3. Web Runtime Profile requires Layer 4 request/response/session/upload capabilities. Data Runtime Profile requires Layer 5 storage and SQLite. The official reference distribution ships all layers.

## Global-name and namespace policy

All current global builtins remain stable through v1.x. Namespace syntax and namespaced aliases are not part of v1. New aliases are forbidden by default; new generic globals require an RFC. Runtime-specific names use established `http_`, `request_`, `session_`, `file_`, `dir_`, and `db_` prefixes. Existing `get`, `set`, `keys`, and `values` remain valid but do not justify new generic globals.
