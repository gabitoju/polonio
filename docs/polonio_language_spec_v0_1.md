# Polonio Language Specification

## Version 0.1.0 (Frozen)

------------------------------------------------------------------------

# 1. Overview

Polonio is a server-side templating language designed for generating
HTML.

Originally created in 2005 as a C++ CGI experiment, Polonio was fully
reimplemented in 2026 with:

-   A modern lexer
-   A recursive-descent parser
-   A tree-walking interpreter
-   A built-in HTTP runtime
-   A development server
-   A single self-contained C++17 binary

Polonio templates mix HTML with embedded code blocks, enabling
expressive, readable dynamic web pages.

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

## Built-in Standard Library (48 functions)

Grouped by: - String - Array - Object - Math - Type - Date - Output -
HTTP

## Control Flow

-   `if / elseif / else / end`
-   `for / in / end`
-   `while / end`

## Functions

-   User-defined
-   Closures
-   Recursion
-   First-class values

## HTTP Runtime

-   `_GET`
-   `_POST`
-   `_COOKIE`
-   `_SERVER`

## Execution Modes

-   CLI template processor
-   Development server
-   Automatic CGI mode

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

Rules: - Only valid outside `<% %>`. - Must follow identifier syntax. -
`null` interpolates as an empty string. - Interpolation does **not**
auto-escape HTML.

------------------------------------------------------------------------

## 4.3 Inline Output

Inside code blocks:

``` pol
<p>2 + 3 = <% echo 2 + 3 %></p>
```

-   `echo` writes to output.
-   `print(expr)` is an alias.
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

Compound operators: - `+= -= *= /= %=` - `..=` (string concatenation)

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

Supports: - Closures - Recursion - Lexical scope

------------------------------------------------------------------------

# 10. Built-in Library

## String

len, substr, split, join, replace, trim, lower, upper, contains,
starts_with, ends_with, htmlspecialchars

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

## Implementation Notes (v0.1)

The lists above capture the full target surface for v0.1, but not every builtin is currently implemented. For the authoritative, shipping set of functions (including `http_status`, `http_header`, `http_content_type`, `redirect`, `urlencode`, and `urldecode`), refer to the registry in `src/polonio/runtime/builtins.cpp`.

------------------------------------------------------------------------

# 11. Superglobals

Available in global scope:

-   `_GET`
-   `_POST`
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

Defaults: - Port: 8080 - Root: current directory

------------------------------------------------------------------------

# 13. CGI Mode

If:

-   No CLI arguments are provided
-   `GATEWAY_INTERFACE` is set

Polonio automatically enters CGI mode.

It: 1. Reads CGI environment variables 2. Populates superglobals 3.
Emits headers 4. Renders template

------------------------------------------------------------------------

# 14. Implementation Constraints

-   C++17
-   Single binary
-   No runtime dependencies
-   Tree-walking interpreter (v0.1)

------------------------------------------------------------------------

# 15. Security Model (v0.1)

-   Templates are assumed trusted.
-   No auto-escaping.
-   Escaping must be explicit (`htmlspecialchars()`).
-   Sandboxing is out of scope for v0.1.
