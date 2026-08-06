# Polonio Conformance Profiles

This document applies RFC 0003. A profile is a capability claim, not a
namespace or a new runtime feature. The official reference distribution ships
all profiles; a conforming alternate implementation may ship a smaller set and
must state that set accurately.

## Language Core

**Purpose:** establish a portable Polonio language implementation.

**Required capabilities:** lexer/parser, language semantics, seven values,
variables, expressions/operators, control flow, functions/closures, execution,
and Layer 1 `type`, `to_string`, `to_number`, and `count`.

**Excluded capabilities:** Layer 2 helpers, template rendering, HTTP, CGI,
sessions, storage, SQLite, uploads, `send_file`, and `send_mail`.

**Typical implementations:** a CLI interpreter or an embedded evaluator.

## Reference Standard Library

**Purpose:** provide the general-purpose library above Language Core.

**Required capabilities:** Language Core plus every Layer 2 string,
collection/object, math, predicate, date, escaping, URL, and development
builtin.

**Excluded capabilities:** template output, HTTP/CGI, sessions, storage, and
SQLite.

**Typical implementations:** a CLI interpreter with utility builtins or an
embedded engine exposing the standard-library profile.

## Template Runtime

**Purpose:** render Polonio templates as ordered text.

**Required capabilities:** Language Core, template scanning/segmentation,
interpolation, includes, output behavior, and Layer 3 `print`/`println`.

**Excluded capabilities:** HTTP transport, CGI, sessions, uploads, storage,
and SQLite. Layer 2 is optional to this standalone profile; a
reference-compatible template renderer also provides the Reference Standard
Library.

**Typical implementations:** a static renderer or a template-only renderer.

## Web Runtime

**Purpose:** execute Polonio with an HTTP request/response context.

**Required capabilities:** Language Core, Reference Standard Library, and all
Layer 4 request/response, session, security, upload, file-response, and
file-mode-mail APIs. The current reference APIs retain their documented
context and environment requirements.

**Excluded capabilities:** a mandated HTTP transport. CGI and the development
server are reference-distribution adapters, not abstract Web Runtime
requirements. Template rendering is optional unless a host claims it.

**Typical implementations:** a CGI runtime or an alternate web host. The
official development server is a Web Runtime adapter, not a language feature.

## Data Runtime

**Purpose:** provide persistent sandboxed files/directories and SQLite data.

**Required capabilities:** Language Core, Reference Standard Library, and all
Layer 5 `file_`/`dir_` storage and `db_` SQLite APIs. The reference
distribution uses `POLONIO_STORAGE_PATH` and SQLite.

**Excluded capabilities:** HTTP, CGI, request/response state, sessions, and
template rendering.

**Typical implementations:** an embedded engine with storage/database support
or a CLI interpreter that needs persistent data.

## Reference Distribution

**Purpose:** identify the complete, currently shipped Polonio package.

**Required capabilities:** every numbered layer, the current CGI adapter, and
the local `polonio serve` development-server adapter.

**Excluded capabilities:** guarantees not implemented by the current server,
including TLS, concurrent handling, keep-alive, streaming, chunked request
bodies, and SMTP delivery.

**Typical implementation:** the official `polonio` executable.

## Inheritance and Compatibility

Language Core is required by every profile. Reference Standard Library is
required by Web Runtime and Data Runtime. Template Runtime is independently
claimable; it only requires Layer 2 when the implementation claims
reference-compatible template support. A profile claim guarantees all of its
required registered canonical APIs. RFC 0001 compatibility aliases remain
accepted spellings for applicable operations and do not add profiles or
operations.

`docs/CONFORMANCE_MATRIX.md` is the quick-reference matrix. The builtin
inventory records each registered name's layer and profile availability.
