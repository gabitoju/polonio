# RFC 0001 — Canonical Names and Alias Policy

## Status

Implemented.

## Context

The current runtime has 99 registered builtins. Four operations are exposed
under two names. Without a policy, reference material can choose different
names, new APIs can accumulate overlapping globals, and compatibility behavior
has no defined v1 boundary. This RFC records the present implementation and
selects one primary spelling per operation before v1 documentation is frozen.

## Goals

- One canonical documented name for each operation.
- Backward compatibility where reasonable.
- Predictable naming conventions.
- No accidental API duplication.

## Non-goals

- Implementing namespaces.
- Removing compatibility aliases in this RFC.
- Changing semantics.
- Introducing deprecation warnings immediately.

## Current Alias Inventory

The complete registry is `install_builtins` in
`src/polonio/runtime/builtins.cpp:2218-2317`. It contains only the four alias
groups below. `echo` is not in that registry: the lexer/parser recognize it as
the `EchoStmt` language statement (`src/polonio/lexer/lexer.cpp` and
`src/polonio/parser/parser.cpp`), so it is not a builtin alias.

| Operation | Names | Implementation target / identical behavior | Tested? | Current documentation |
|---|---|---|---|---|
| Rendered-value conversion | `tostring`, `to_string` | Separate functions, both call `OutputBuffer::value_to_string`; valid-call results are identical. Their missing-argument diagnostics name the invoked builtin, so errors are not byte-identical. | Both have direct successful tests (`tests/test_main.cpp:3506-3518`); no cross-alias conformance test. | Both listed in `docs/site/builtins.html`; it currently calls `tostring` primary. |
| HTML escaping | `htmlspecialchars`, `html_escape` | Separate functions with the same five-character escaping mapping and output conversion. Arity diagnostics use different names, so errors are not byte-identical. | Both have direct tests (`2532-2574`); no cross-alias conformance test. | Both listed as aliases in `docs/site/builtins.html`; language spec uses `htmlspecialchars`. |
| HTTP status | `status`, `http_status` | Both registrations target `builtin_status`; exactly identical at runtime, including context/effect/diagnostics. | Both are exercised in CGI tests (`844-893`); no equality-specific test. | Both listed; site examples lead with `http_status`. |
| HTTP header | `header`, `http_header` | Both registrations target `builtin_header`; exactly identical at runtime, including one- and two-argument forms, context/effect/diagnostics. | Both are exercised in CGI tests (`844-893`); no equality-specific test. | Both listed; site examples lead with `http_header`. |

No repository evidence establishes a historical origin for these names. The
`htmlspecialchars` and `tostring` spellings are plausibly compatibility-style
names, but this RFC does not claim a historical reason without evidence.

## Naming Principles

1. Use `snake_case` for newly documented multiword builtins.
2. Prefer descriptive web-runtime prefixes where a generic global word could
   be ambiguous; this applies to response status and headers.
3. Do not make PHP-specific spelling the primary v1 spelling when a clear
   snake_case equivalent exists; retain it only for compatibility.
4. Prefer a verb plus a precise noun for actions and conversions.
5. Minimize global-name ambiguity while preserving existing user code.
6. A compatibility alias is a supported spelling, not a second canonical API.

## Canonical Name Decisions

| Operation | Canonical name | Compatibility alias | Decision |
|---|---|---|---|
| Rendered-value conversion | `to_string` | `tostring` | `to_string` follows the project’s snake_case convention and pairs with `to_number`. Keep `tostring` for existing programs. Primary signatures/examples use `to_string`; the alias is shown in an alias note. It may only be removed in a future major version after an explicit compatibility decision. |
| HTML escaping | `html_escape` | `htmlspecialchars` | `html_escape` is concise, descriptive, and snake_case. Keep the older spelling for compatibility. Primary signatures/examples use `html_escape`; the alias is shown in an alias note. Removal requires a future major version. |
| HTTP status | `http_status` | `status` | Status is response/HTTP-specific in this runtime. The prefix avoids an ambiguous global and aligns with `http_header` and `http_content_type`. Keep `status` for compatibility. Primary signatures/examples use `http_status`; the alias is shown in an alias note. Removal requires a future major version. |
| HTTP header | `http_header` | `header` | Headers are HTTP response headers in the applicable runtime context. The prefix gives the same clarity and family consistency as `http_status`. Keep `header` for compatibility. Primary signatures/examples use `http_header`; the alias is shown in an alias note. Removal requires a future major version. |

## Compatibility Policy

The following policy is accepted for v1:

- Existing aliases remain functional throughout v1.x.
- New primary documentation and examples use canonical names.
- Compatibility aliases are listed in a dedicated alias note or compatibility
  table, rather than receiving equally primary signatures.
- v1.0 emits no runtime warning for an alias.
- Alias removal, if ever accepted, requires a future major version.
- Conformance tests must establish semantic equivalence of aliases.

This policy is adopted unchanged from the proposed baseline. It keeps current
programs working while giving authors one predictable spelling to teach.

## Documentation Policy

- Canonical names appear in section headings, signatures, navigation, and all
  new examples.
- The compatibility alias appears immediately below the canonical signature in
  an “Aliases” or compatibility table entry.
- Search/navigation should index both spellings so existing users can find the
  canonical entry.
- Aliases do not receive separate primary reference sections; they redirect to
  the canonical operation with a compatibility note.

## Test Policy

Future conformance tests must call every alias group with the same valid input
and verify the same return value, mutation behavior, and output behavior. They
must also compare failure category and observable error behavior for invalid
calls, and compare the same CGI/server response effects for response aliases.
Where current diagnostics include the called builtin name, follow-up work must
decide whether semantic equivalence permits that name difference or whether
diagnostics should be normalized before v1 is frozen. This RFC makes no code
change.

## Alternatives Considered

1. **Remove aliases before v1.** Rejected: it breaks existing programs without
   evidence that migration pressure is warranted.
2. **Keep every name equally canonical.** Rejected: it preserves documentation
   drift and does not resolve public API duplication.
3. **Add deprecation warnings immediately.** Rejected: warnings change runtime
   behavior and are outside this design-only RFC.
4. **Introduce namespaces before resolving aliases.** Rejected: namespace
   policy is separately queued as L1-2; alias policy is useful independently.

## Decision

Polonio v1 has the following canonical-name table:

| Canonical | Compatibility alias |
|---|---|
| `to_string` | `tostring` |
| `html_escape` | `htmlspecialchars` |
| `http_status` | `status` |
| `http_header` | `header` |

The compatibility and documentation policies above are accepted. This is a
design decision only; it neither changes the current registry nor freezes a
runtime implementation.

## Consequences

Positive consequences: one spelling is taught for every duplicated operation,
the web-response family is clearer, and existing templates continue to work.

Negative consequences: the global surface remains duplicated throughout v1,
documentation must carry alias notes, and conformance work is required before
the behavior can be frozen.

## Follow-up Work

- Documentation consistency pass using the canonical names.
- Alias conformance tests, including valid calls, failure behavior, mutation,
  output, and HTTP effects.
- Standard-library inventory update.
- A future-major-version deprecation/removal policy, if needed.
