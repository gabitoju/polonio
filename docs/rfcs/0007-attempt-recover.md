# RFC 0007 — `attempt` / `recover` Blocks

## Status

Implemented.

## Context

RFC 0004 defines Polonio's structured error categories. RFC 0005 makes
builtin failures structurally consistent. RFC 0006 is authoritative: expected
and domain outcomes are ordinary values; programming and internal failures are
fatal; only `CapabilityError` and `ResourceError` are operational failures
eligible for language-level recovery. This RFC supplies the first and only
recovery construct planned for Polonio v1.

## Goals

- Define a small, explicit recovery block for operational failures.
- Preserve automatic failure propagation through functions, includes, and
  template code.
- Define scope, nesting, control-flow, output, and response-finalization
  behavior precisely.

## Non-goals

This RFC does not define user-created errors, `raise`, `throw`, retry,
rollback, cleanup/finalization, an exception hierarchy, typed handlers,
multiple handlers, or an Error field-access API.

## Syntax

Polonio adopts exactly these forms:

```pol
attempt
    statements
recover
    statements
end
```

```pol
attempt
    statements
recover error
    statements
end
```

`recover` has zero or one identifier. It uses no parentheses, commas, `as`, or
braces. An `attempt` has exactly one `recover` clause; a second `recover` in
the same block is invalid syntax.

## Recoverable failures

Only an operational `CapabilityError` or `ResourceError` raised while an
`attempt` body executes may enter that block's `recover` body.

`SourceError`, `LexError`, and `ParseError` occur before or outside executable
recovery. `InternalError` remains fatal. `RuntimeError` is never intercepted:
invalid operations, invalid builtin calls, undefined names, and other
programming bugs ignore every `attempt` block and terminate normally through
the applicable adapter boundary. A recover body is not a catch-all mechanism.

## Execution and propagation

Execution follows this sequence:

1. Execute the attempt body.
2. If it completes without an operational failure, skip the recover body and
   continue with the statement following `end`.
3. If an eligible operational failure occurs, abandon the remaining attempt
   body, bind its Error value if requested, execute the recover body once, and
   then continue with the statement following `end`.

No retry occurs. Execution never jumps back into the attempt body. Recovery
does not undo prior side effects, including writes, database changes, session
changes, headers, or emitted output. An operational failure that is not handled
by an enclosing eligible `attempt` continues through user functions until an
adapter boundary, where RFC 0004 and RFC 0006 terminal behavior applies.

If the recover body itself produces an eligible operational failure, that
failure is not handled by the same block; it propagates to a lexically outer
eligible `attempt`, if any, otherwise to the adapter boundary. Non-operational
failures in a recover body remain fatal.

## Error binding and scope

In `recover error`, `error` is the future immutable, read-only `Error` object
described by RFC 0006, preserving the structured facts from RFC 0004 and RFC
0005. This RFC defines its binding, not its fields or construction API.

The recovery variable exists only for the recover body. It is not created in
the attempt body, before `recover`, or after `end`. `recover` without an
identifier creates no variable.

## Nesting and control flow

Nested `attempt` blocks are legal. Each `recover` handles only eligible
failures produced while its own attempt body is active. Thus an inner attempt
gets the first opportunity; a failure left unhandled reaches an outer attempt.
Failures produced by a recover body belong to any lexically outer attempt, not
to the attempt that selected that recover body.

`return` is legal in both attempt and recover bodies and has ordinary return
semantics. It exits the current function immediately; it does not run a
recover body or resume an attempt body. `break` and `continue` are legal in a
recover body when it is syntactically inside a loop, with their ordinary
nearest-enclosing-loop meaning. They remain subject to the normal syntactic
rules outside a loop.

## Functions, includes, and templates

Operational failures propagate through user-defined functions until a matching
recover block or an adapter boundary; no other language interception exists.
Failures raised while evaluating an `include` propagate normally, so an
attempt surrounding the include statement may recover them.

Attempt blocks are legal in template code sections. Their recover body affects
only the current template execution. It does not alter adapter-level error
presentation or recover a different request.

## Output and finalized responses

Output already emitted before a failure remains emitted. Entering `recover`
does not erase, replace, or roll back it.

`send_file` finalizes the response. Finalization is an adapter commit boundary:
after it succeeds, a subsequent operational failure is terminal for that
response and is not delivered to an `attempt`/`recover` block. The adapter
must not append a recovery body, replace the finalized response, or claim a
different response outcome. This rule avoids presenting recovery as rollback;
code must complete all fallible preparation before `send_file` when it needs a
fallback response.

## Examples

### Recover with an Error binding

```pol
attempt
    var config = file_read("config.json")
recover error
    var config = "{}"
    var recovery_error = error
end
```

The bound Error value may be retained as an ordinary immutable value; its
concrete field API is deferred. Only a capability or resource failure from
`file_read` enters recovery.

### Recover without a variable

```pol
attempt
    file_write("audit.log", line)
recover
    echo "Audit logging is temporarily unavailable."
end
```

### SQLite transaction

```pol
attempt
    db_begin()
    db_exec("insert into orders (number) values (?)", [number])
    db_commit()
recover error
    db_rollback()
    echo "The order could not be saved."
end
```

Recovery does not roll back automatically. The explicit `db_rollback()` is
application code and may itself propagate an operational failure outward.

### Nested attempt

```pol
attempt
    attempt
        var avatar = file_read("avatars/current.png")
    recover
        var avatar = file_read("avatars/default.png")
    end
recover error
    echo "No avatar is available."
end
```

The inner recover handles a failure from the first read. If its fallback read
also fails, the outer recover receives that failure.

### Template code

```pol
<%
attempt
    include "partials/account.pol"
recover
%>
<p>Account details are unavailable.</p>
<%
end
%>
```

The enclosing attempt may recover an operational failure during the include;
output rendered before that failure remains in the response.

## Explicit v1 exclusions

The following do not exist in v1: `raise`, `throw`, `finally`, `ensure`, typed
recover clauses, multiple recover clauses, `recover if ResourceError`, catch-all
runtime-error handling, user-defined error classes, and `retry`.

## Alternatives considered

### `try` / `catch`

Rejected. `catch` conventionally suggests broad exception interception and
typed exception hierarchies. `attempt` / `recover` instead describes a bounded
operation and its operational fallback, matching RFC 0006's category limit.

### `begin` / `rescue`

Rejected. The Ruby terminology brings the semantics and expectations of broad
Ruby exceptions. Polonio deliberately rejects broad recovery and user-created
exceptions.

### Result objects

Rejected as a language-wide replacement. RFC 0006 keeps Result-like objects
available to applications for domain outcomes, but mandatory wrappers would
add propagation boilerplate and conflict with Polonio's single-value and
template-oriented flow.

### Go-style error returns

Rejected. Polonio has no multiple-return or destructuring design, and explicit
error returns would change every fallible API while duplicating automatic
operational propagation.

### Ruby exceptions

Rejected. Broad `rescue`, typed classes, and arbitrary raising can hide
programming defects and blur business outcomes with host failures. The adopted
construct preserves only the narrow operational-recovery benefit.

## Future extensions

Future RFCs may separately define re-propagation of a captured Error, the
immutable Error value's field API, typed recovery, cleanup/finalization, or
user-created failures. None is implied or committed by this RFC.

## Decision

Polonio v1 adopts `attempt` / `recover` as its first and only planned recovery
construct. It handles only `CapabilityError` and `ResourceError` from its own
attempt body, resumes only after `end`, and never retries or rolls back.
Recovery bindings are scoped solely to `recover` and refer to the future
immutable Error value. Nesting, ordinary return/loop control, functions,
includes, templates, output preservation, and finalized-response terminality
follow the rules above. All broad-exception features remain excluded.

## Implementation consequences

The reference implementation now provides parser, AST, interpreter, Error-value,
response-state, and conformance-test support. Error bindings are immutable
object-like values with safe RFC 0004 and RFC 0005 fields; no constructor or
re-propagation feature exists.
