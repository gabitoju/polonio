# RFC 0003 — Core Language vs Runtime Boundary

## Status

Implemented.

## Context

The reference executable currently combines a parser, interpreter, template
renderer, CGI adapter, local development server, HTTP APIs, sessions,
storage, SQLite, uploads, file responses, and file-mode mail. That breadth is
useful for the reference distribution, but it must not make HTTP or SQLite an
implicit requirement for every implementation of the Polonio language.

RFC 0002 classifies the registered builtin surface into five numbered layers.
This RFC defines the boundary those layers imply: what a conforming
implementation must provide, which additional profiles it may provide, and
how a future runtime extends rather than redefines the language. It records
the current implementation's composition; it does not change it.

## Goals

- Define “Polonio language”, “standard library”, and each runtime layer.
- Define conformance claims that are meaningful without HTTP, SQLite, or a
  particular executable.
- Define the official reference distribution without treating it as the only
  conforming implementation.
- Preserve the layered/global-name decisions of RFC 0002 and the alias policy
  of RFC 0001.
- Leave room for additional runtimes without changing language semantics.

## Non-goals

- Changing parser, lexer, AST, interpreter, template, or builtin behavior.
- Adding, renaming, or removing APIs.
- Defining a module system, namespace syntax, capability-discovery API, or
  plugin ABI.
- Requiring a production HTTP server or defining deployment policy.

## Definitions

### The Polonio language

The Polonio language is the syntax and execution semantics of Polonio source:
its lexical rules, parser grammar, seven value kinds, expressions, operators,
variables, functions and closures, statements and control flow, indexing,
assignment, errors at the language boundary, and the semantics of literal
template/code segmentation where applicable. `echo` is language syntax, not a
registered builtin. The language also includes Layer 1 Core Language Support
operations defined below.

The language is not synonymous with the current executable. In particular,
HTTP, CGI, the local development server, storage, SQLite, sessions, uploads,
and mail are not language syntax or language-semantic requirements.

### The Polonio standard library

The standard library is Layer 2: the general-purpose builtin API for strings,
collections/objects, math, predicates, dates, escaping, URL conversion, and
the development-oriented `debug` surface. It is a separately claimable
library profile, not a requirement of the minimal language profile.

### Runtime layers

The Template, Web, and Data runtimes are capabilities layered on the language
and standard-library profiles. They define integration contexts and APIs, not
additional language syntax. Their current builtin classification is the
authoritative reviewed manifest in `docs/builtins_v1_manifest.tsv`.

### Reference distribution

The reference distribution is the official Polonio executable and its
published accompanying runtime. It ships all currently implemented numbered
layers: Language Core, Standard Library, Template Runtime, Web Runtime, and
Data Runtime. It is one complete distribution profile, not the definition of
the language itself.

## Layer Responsibilities and Conformance

### Layer 1 — Core Language Support

| Aspect | Decision |
|---|---|
| Purpose | Make basic execution and value handling available to every conforming implementation. |
| Responsibilities | Implement the language grammar/semantics described above and the canonical operations `type`, `to_string`, `to_number`, and `count`. |
| Dependencies | None beyond the implementation of the language value model and output-value conversion required by `to_string`. |
| Required APIs | `type`, `to_string`, `to_number`, `count`. RFC 0001's `tostring` spelling remains a compatibility alias for `to_string`, not another operation. |
| Optional APIs | All Layers 2–5; they are not implied by Layer 1. |
| Conformance | Required for every implementation claiming **Language Core** conformance. |
| May another implementation omit it? | No. An implementation lacking this layer cannot claim Polonio language conformance. |

### Layer 2 — Standard Library

| Aspect | Decision |
|---|---|
| Purpose | Supply generally useful computation independent of a request, response, storage root, or database connection. |
| Responsibilities | Provide the registered string, collection/object, math, predicate, date, escaping, URL, and development utility families classified as Layer 2 by RFC 0002. |
| Dependencies | Layer 1 and the language value model; `now` depends on a clock. No HTTP, CGI, storage, or SQLite dependency. |
| Required APIs | All Layer 2 canonical operations for an implementation claiming **Reference Standard Library** conformance. `htmlspecialchars` remains RFC 0001's compatibility alias for `html_escape`. |
| Optional APIs | Layers 3–5. The development designation on `debug` is cross-cutting and does not make it a separate layer. |
| Conformance | Optional for Language Core; required for a Reference Standard Library claim and for the official reference distribution. |
| May another implementation omit it? | Yes, if it claims only Language Core and documents that the standard-library profile is absent. |

### Layer 3 — Template Runtime

| Aspect | Decision |
|---|---|
| Purpose | Render Polonio templates as ordered text output. |
| Responsibilities | Provide template scanning/segmentation, interpolation, includes, the output model, and the Layer 3 `print` and `println` APIs. `echo` remains syntax. |
| Dependencies | Language Core and an output sink/buffer. It does not require HTTP, CGI, storage, or SQLite merely to render a template. |
| Required APIs | `print` and `println`, plus the template behavior this profile claims. |
| Optional APIs | Web and Data Runtime facilities; an implementation may choose its own output host. |
| Conformance | Optional for Language Core; required for an implementation claiming **Template Engine** conformance. |
| May another implementation omit it? | Yes. A CLI evaluator or embedded expression interpreter may be language-conforming without template rendering. |

### Layer 4 — Web Runtime

| Aspect | Decision |
|---|---|
| Purpose | Bind Polonio programs to HTTP request/response execution. |
| Responsibilities | Provide the Layer 4 request, response, cookie/session, CSRF/security, upload, file-response, and file-mode mail APIs listed in the reviewed manifest. In the current reference distribution this includes CGI and the development server adapters. |
| Dependencies | Language Core and the Reference Standard Library. Request-bound APIs require a request/response context; sessions and CSRF require their configured secret/context; uploads, `send_file`, and `send_mail` use the Data Runtime storage model in the current implementation. A template-capable web renderer additionally requires Layer 3. |
| Required APIs | All registered Layer 4 canonical operations for an implementation claiming **Web Runtime** conformance: request and response helpers, sessions, the classified security helpers, uploads, `send_file`, and `send_mail`. RFC 0001 aliases `status` and `header` remain accepted spellings of `http_status` and `http_header`. |
| Optional APIs | CGI transport and the local development-server transport are reference-distribution adapters, not requirements of the abstract Web Runtime profile. An alternate implementation may provide another HTTP host while preserving the defined API/context contract. |
| Conformance | Optional for Language Core and Reference Standard Library. Required only for a Web Runtime claim. |
| May another implementation omit it? | Yes. A conforming CLI-only interpreter, template-only renderer, or embedded library need not implement HTTP, CGI, sessions, CSRF, uploads, `send_file`, or `send_mail`. |

### Layer 5 — Data Runtime

| Aspect | Decision |
|---|---|
| Purpose | Provide persisted file/directory storage and SQLite-backed data access. |
| Responsibilities | Provide the registered sandboxed `file_`/`dir_` APIs and `db_` SQLite APIs classified as Layer 5 by RFC 0002. |
| Dependencies | Language Core and an implementation-defined storage root/backing filesystem. The current reference implementation uses `POLONIO_STORAGE_PATH`; its SQLite support also depends on SQLite. |
| Required APIs | All Layer 5 storage and SQLite operations for an implementation claiming **Data Runtime** conformance. |
| Optional APIs | Web Runtime and Template Runtime. A Data Runtime may be used by a CLI, embedded, or alternate web host. |
| Conformance | Optional for Language Core and Reference Standard Library. Required only for a Data Runtime claim. |
| May another implementation omit it? | Yes. A conforming implementation may omit storage and SQLite entirely and must not claim the Data Runtime profile. |

## Required Boundary Decisions

| Capability | Decision |
|---|---|
| SQLite | Layer 5 Data Runtime; not part of the language or Language Core conformance. |
| Sandboxed storage | Layer 5 Data Runtime; not part of the language or Language Core conformance. |
| HTTP | Layer 4 Web Runtime; not part of the language. |
| CGI | A current reference-distribution Web Runtime adapter; not part of the language or required by the abstract Web Runtime profile. |
| `polonio serve` development server | A current reference-distribution Web Runtime adapter/tool; not part of the language or a required Web Runtime transport. |
| `send_mail` | Layer 4 Web Runtime; the current implementation writes storage-backed `.eml` files and does not define SMTP as language behavior. |
| `send_file` | Layer 4 Web Runtime with a current Data Runtime dependency; not part of the language. |
| Sessions and CSRF | Layer 4 Web Runtime; not part of the language. |

Therefore, an implementation can accurately call itself “a conforming Polonio
implementation” when it implements the Language Core profile without HTTP or
SQLite and explicitly states its supported profiles. It can be a CLI-only
interpreter, a template-only renderer, an embedded library, or an alternate
web runtime. Such an implementation must not claim profiles it omits.

## Conformance Profiles

| Profile | Guarantees | Does not guarantee |
|---|---|---|
| **Language Core** | Polonio syntax/semantics and Layer 1 canonical operations. | Standard-library helpers, template rendering, HTTP, CGI, sessions, storage, or SQLite. |
| **Reference Standard Library** | Language Core plus every Layer 2 canonical operation. | Template, Web, and Data runtime capability. |
| **Template Engine** | Language Core, template scanning/interpolation/includes/output model, and Layer 3 `print`/`println`. A reference-compatible template engine also provides Layer 2. | HTTP transport, CGI, sessions, storage, SQLite. |
| **Web Runtime** | Language Core and Reference Standard Library plus all Layer 4 APIs and their required execution contexts. | A particular transport such as CGI or `polonio serve`, unless separately claimed; Data Runtime only where an API's documented dependency requires it. |
| **Data Runtime** | Language Core and Reference Standard Library plus all Layer 5 storage and SQLite APIs. | HTTP, CGI, request/response contexts, or template rendering. |
| **Reference Distribution** | All five layers and the current CGI and local-development-server adapters. | Production-server qualities not implemented by the distribution, such as TLS, concurrency, keep-alive, streaming, or SMTP delivery. |

An implementation claiming source compatibility for a supplied profile must
also honor applicable RFC 0001 compatibility aliases. The aliases do not add
operations or alter the profile's numbered-layer count.

## Optional Capability Disclosure

Profiles are declared in implementation documentation and release metadata.
The current language has no builtin runtime capability-discovery API, and this
RFC does not add one. Documentation for a runtime-dependent API must identify
its required profile and environment/context requirements. A profile claim is
all-or-nothing for that profile's required registered operations; a partial
implementation must describe itself as partial rather than Web Runtime or Data
Runtime conforming.

## Compatibility and Future Runtimes

New facilities extend Polonio through additional runtime profiles; they do
not alter Layer 1 syntax or semantics merely by existing. A future Graph,
AI, Network, or Filesystem Runtime must:

1. define its APIs, contexts, dependencies, and conformance requirements in
   an RFC;
2. follow the global-name and alias rules of RFC 0002 and RFC 0001;
3. state that Language Core remains sufficient without that runtime; and
4. avoid redefining existing language values, operators, parser syntax, or
   required core behavior without a separate language-semantic RFC.

This permits alternate hosts to add capabilities while keeping programs and
conformance claims understandable. Namespace syntax remains deferred beyond
v1 under RFC 0002; these layers are profiles, not namespace objects.

## Alternatives Considered

1. **Everything belongs to the language.** Rejected: it would make HTTP,
   SQLite, storage, and one deployment model mandatory for every interpreter,
   contradicting the actual separability of the current layers.
2. **Everything belongs to the runtime.** Rejected: it leaves no stable
   baseline for source compatibility. Syntax, semantics, values, and Layer 1
   operations must define what it means to implement Polonio.
3. **The current layered model.** Accepted: it preserves the implemented
   reference distribution while providing exact optional profiles for template,
   web, and data capabilities.

## Decision

Polonio v1 is defined by Language Core: syntax, semantics, and Layer 1 Core
Language Support. Layer 2 is the separately claimable Reference Standard
Library. Layers 3, 4, and 5 are optional Template, Web, and Data Runtime
profiles. CGI and the local development server are reference-distribution
adapters, not language requirements. The official reference distribution ships
all five layers. Future runtimes extend this model through documented optional
profiles and must not redefine the language.

## Consequences

Positive consequences:

- A small CLI-only or embedded implementation has a concrete conformance path.
- Template, web, and data features can evolve without silently expanding the
  language definition.
- The reference executable remains a complete, useful distribution without
  making its transport choices universal requirements.

Negative consequences:

- Documentation and implementations must state profile support precisely.
- A program using optional APIs is portable only to implementations that claim
  the corresponding profiles.
- Future runtime additions require explicit design and conformance work.

## Follow-up Work

- Incorporate the accepted boundary into `docs/polonio_language_spec_v1_0.md`
  when that specification is created.
- Define capability-discovery only if a concrete interoperability need arises.
- Continue the queued semantic, error-model, template, and conformance RFCs.
