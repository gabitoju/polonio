# Polonio Language Specification

## Version 0.1.0 (Frozen)

------------------------------------------------------------------------

# 1. Overview

Polonio is a server-side templating language for generating HTML. It began as
a C++ CGI experiment in 2005 and was fully reimplemented in 2026.

The official Reference Distribution includes:

-   A template renderer
-   Web Runtime adapters (CGI and a development server)
-   A single self-contained C++17 binary

Polonio templates mix HTML with embedded code blocks to create dynamic pages
while keeping the surrounding document visible.

------------------------------------------------------------------------

# 2. Core Characteristics

## Template Syntax

-   `<% ... %>` --- code block
-   `$var` --- interpolation in HTML
-   `<% echo expr %>` --- inline output

## Data Types (7)

-   `null`
-   `bool`
-   `number`
-   `string`
-   `array`
-   `object`
-   `function`

## Registered Builtins and Runtime Profiles

The Reference Distribution registers 99 built-in names across
Language Core, Standard Library, Template Runtime, Web Runtime, and Data
Runtime layers. The Standard Library is not the same thing as the complete
registered surface. The authoritative inventory and profile availability are
in `docs/STANDARD_LIBRARY_V1.md`.

## Storage Builtins

The Reference Distribution's Data Runtime provides sandboxed storage helpers.
Every storage operation resolves the requested relative
path against the `POLONIO_STORAGE_PATH` environment variable, rejects
absolute paths, rejects traversal (`..`) after normalization, and raises
a runtime error if the resolved target would escape the storage root or
if the storage root is not configured.

Available functions:

-   `file_read(path)` / `file_write(path, content)` / `file_append(path, content)`
-   `file_exists(path)` / `file_delete(path)`
-   `file_size(path)` / `file_modified(path)`
-   `dir_create(path)` / `dir_exists(path)` / `dir_list(path)`

## Database Builtins

The Reference Distribution's Data Runtime includes SQLite for lightweight
persistence. Database files are
resolved relative to `POLONIO_STORAGE_PATH` using the same sandbox rules
as storage: only relative paths are allowed, traversal is rejected, and
paths cannot leave the configured root. SQL parameters are bound
positionally (arrays) and query results return arrays of objects keyed
by column name.

Available functions:

-   `db_connect(path)` / `db_close()`
-   `db_query(sql[, params])` / `db_exec(sql[, params])`
-   `db_last_insert_id()`
-   `db_begin()` / `db_commit()` / `db_rollback()`

## Control Flow

-   `if / elseif / else / end`
-   `for / in / end`
-   `while / end`

## Functions

-   User-defined
-   Closures
-   Recursion
-   First-class values

## Web Runtime Extensions

-   `_GET`
-   `_POST`
-   `_FILES`
-   `_COOKIE`
-   `_SERVER`

`_FILES` is populated for multipart file parts. These Web Runtime APIs are
implementation extensions rather than additions to the template syntax.

## Reference Distribution Execution Modes

-   CLI template processor
-   Development server adapter
-   Automatic CGI adapter

## Runtime Extensions (Implementation Reference)

This specification defines language syntax. The following implemented runtime
facilities do not change that syntax: request helpers (`request_body`,
`request_header`, `request_headers`, `request_json`, and `cookies`), signed
sessions, CSRF helpers, password hashing, secure random tokens, sandboxed
storage, SQLite, multipart uploads, `send_file`, and file-mode `send_mail`.

Storage and SQLite resolve relative paths beneath `POLONIO_STORAGE_PATH`.
Sessions use a signed cookie and require `POLONIO_SESSION_SECRET` when they are
used. `send_mail` writes an `.eml` file to the storage outbox; it does not send
SMTP mail.

The development server is local tooling: it binds `127.0.0.1`, supports GET
and POST, renders `.pol` files, and serves static files. CGI and server
transport behavior, request limits, and routing conventions are documented in
`docs/site/runtime.html`. The detailed builtin reference is
`docs/site/builtins.html`.

------------------------------------------------------------------------

# 3. File Structure

Polonio files use the `.pol` extension.

A `.pol` file consists of:

-   Literal HTML/text (emitted directly)
-   Embedded Polonio code blocks (`<% ... %>`)

Example:

``` pol
<% var name = "World" %>
<h1>Hello $name!</h1>
```

------------------------------------------------------------------------

# 4. Template Semantics

## 4.1 Code Blocks

Code is embedded between `<%` and `%>`.

Multiline blocks are supported:

``` pol
<%
var a = 10
var b = 20
%>
```

------------------------------------------------------------------------

## 4.2 Interpolation

Outside code blocks, `$identifier` is replaced with its string value.

Rules:

-   Interpolation is valid only outside `<% %>`.
-   The name must follow identifier syntax.
-   `null` interpolates as an empty string.
-   Interpolation does **not** escape HTML automatically.

------------------------------------------------------------------------

## 4.3 Inline Output

Inside code blocks:

``` pol
<p>2 + 3 = <% echo 2 + 3 %></p>
```

-   `echo` writes to output.
-   `print(expr)` is a Template Runtime builtin with corresponding output
    behavior; `echo` itself is language syntax, not a builtin alias.
-   Output is raw unless escaped manually.

------------------------------------------------------------------------

# 5. Data Types

## null

Represents absence of value.

## bool

`true`, `false`

## number

Double-precision floating point.

## string

Supports: - `"double quoted"` - `'single quoted'` - Escapes: `\n`, `\t`,
`\\`, `\"`, `\'`

## array

Ordered list, zero-indexed.

``` pol
<% var items = ["apple", "banana"] %>
```

## object

String-keyed map.

``` pol
<% var user = {"name": "Juan"} %>
```

Access via:

``` pol
user["name"]
```

## function

First-class callable value with lexical scoping.

------------------------------------------------------------------------

# 6. Variables

Declaration:

``` pol
<% var name %>
<% var name = "Juan" %>
```

Uninitialized variables default to `null`.

Assignment:

``` pol
<% name = "Maria" %>
```

Compound operators: `+=`, `-=`, `*=`, `/=`, `%=`, and `..=` (string concatenation).

------------------------------------------------------------------------

# 7. Operators

Arithmetic: `+ - * / %`

String concatenation: `..`

Comparison: `< <= > >= == !=`

Logical: `and`, `or`, `not`

Assignment: Right-associative.

Operator precedence (high → low):

1.  Function call / indexing
2.  Unary `-`, `not`
3.  `* / %`
4.  `+ -`
5.  `..`
6.  Comparison
7.  Equality
8.  `and`
9.  `or`
10. Assignment

------------------------------------------------------------------------

# 8. Control Flow

## 8.1 if / elseif / else / end

``` pol
<% if condition %>
  ...
<% elseif other %>
  ...
<% else %>
  ...
<% end %>
```

Blocks may span HTML.

------------------------------------------------------------------------

## 8.2 for / in

Array:

``` pol
<% for fruit in items %>
  <li>$fruit</li>
<% end %>
```

Index + value:

``` pol
<% for i, entry in entries %>
  ...
<% end %>
```

Object iteration: - `for key, value in object`

------------------------------------------------------------------------

## 8.3 while

``` pol
<% while condition %>
  ...
<% end %>
```

------------------------------------------------------------------------

# 9. Functions

Definition:

``` pol
<%
function greet(name)
  return "Hello, " .. name
end
%>
```

Supports closures, recursion, and lexical scope.

------------------------------------------------------------------------

# 10. Built-in Library

## String

len, substr, split, join, replace, trim, lower, upper, contains,
starts_with, ends_with, html_escape

## Array

count, push, pop, shift, unshift, slice, concat, contains

## Object

keys, values, has_key, get, set, count

## Math

abs, floor, ceil, round, min, max, pow, sqrt, rand, randint

## Type

type, is_null, is_bool, is_number, is_string, is_array, is_object,
is_function, to_string, to_number

## Date

now, date_parse, date_format, date_add_days

## Output

echo, print, println, debug, nl2br

## HTTP

urlencode, urldecode\
http_status\
http_header\
http_content_type\
redirect

## Reference Notes (v0.1)

The lists above summarize the v0.1 surface. For the authoritative current
registry, canonical aliases, and layer/profile classification, refer to
`docs/STANDARD_LIBRARY_V1.md`; runtime-specific details are in
`docs/site/runtime.html`.

------------------------------------------------------------------------

# 11. Superglobals

Available in global scope:

-   `_GET`
-   `_POST`
-   `_FILES`
-   `_COOKIE`
-   `_SERVER`

Example:

``` pol
<% if _SERVER["REQUEST_METHOD"] == "POST" %>
  ...
<% end %>
```

------------------------------------------------------------------------

# 12. CLI

Build:

    make
    make test

Binary:

    build/polonio

Run template:

    polonio run file.pol
    polonio file.pol

Dev server:

    polonio serve --port 3000 --root ./examples

Defaults: port `8080` and the current directory as the root.

------------------------------------------------------------------------

# 13. CGI Mode

If:

-   No CLI arguments are provided
-   `GATEWAY_INTERFACE` is set

Polonio automatically enters CGI mode.

It reads CGI environment variables, populates superglobals, emits headers,
and renders the template.

------------------------------------------------------------------------

# 14. Reference Distribution Constraints

-   C++17
-   Single binary
-   Reference Distribution storage and SQLite support

------------------------------------------------------------------------

# 15. Security Model (v0.1)

-   Templates are assumed trusted.
-   No auto-escaping.
-   Escaping must be explicit (`html_escape()`).

## Compatibility names

The v0.1 runtime retains the following compatibility aliases. New
documentation and examples use the canonical names: `to_string` (`tostring`),
`html_escape` (`htmlspecialchars`), `http_status` (`status`), and
`http_header` (`header`). Each alias remains functional with the same
behavior. `echo` is language syntax, not a built-in. Sandboxing is outside
Language Core; the Reference Distribution's Data Runtime provides sandboxed
storage.

## Error model

The reference implementation reports structured Source, Lex, Parse, Runtime,
Capability, Resource, and Internal error categories. Errors end the current
execution; user-level recovery is deferred. Source-associated diagnostics carry
their available location, and template include diagnostics retain include
context. Exact English message wording is not a compatibility guarantee; see
`docs/rfcs/0004-error-model.md` for the v1 design and adapter rules.

Builtin invocation failures additionally expose structured reason and safe
facts: arity, type, value, shape, unsupported value, context, configuration,
resource, or operation. Argument positions are one-based. Exact prose is not
stable, and diagnostics redact credentials, tokens, cookies, request bodies,
mail bodies, upload contents, and SQL parameter values.

Expected absence and ordinary negative outcomes are normal values, such as
`null`, `false`, empty collections, or counts. Programming errors terminate
the current execution. Capability and resource failures also currently
terminate execution, though they are eligible for a future restricted recovery
mechanism; Polonio v0.1 provides no recovery syntax or Error value. Domain and
validation outcomes are application-defined values, and recovery never implies
rollback of output, files, sessions, database changes, or external effects.

------------------------------------------------------------------------

## Language and runtime boundary

This frozen v0.1 document describes syntax and the currently shipped
reference implementation. RFC 0003 defines the v1 planning boundary: Language
Core consists of syntax, semantics, values, execution, and Layer 1 support.
The Standard Library, Template Runtime, Web Runtime, and Data Runtime are
separately claimable profiles. HTTP, CGI, the development server, sessions,
storage, SQLite, uploads, `send_file`, and `send_mail` are runtime
capabilities, not Language Core requirements. The official reference
distribution ships every currently implemented layer. See
`docs/CONFORMANCE_PROFILES.md` and `docs/CONFORMANCE_MATRIX.md` for the
profile contract; this note does not alter the v0.1 syntax specification.
