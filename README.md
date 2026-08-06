# Polonio

Polonio is a C++17 templating language. The official reference distribution is a single binary that ships the language, template runtime, CGI adapter, and local development server.

## Features

- Templates with `<% %>` code, `$var` interpolation, inline `echo`, and relative includes
- Closures, arrays, objects, control flow, and 99 registered builtins
- The reference distribution's Web Runtime provides CGI, a filesystem-routed local server, GET/POST, URL-encoded forms, multipart uploads, cookies, JSON bodies through `request_json()`, and signed sessions/CSRF.
- The reference distribution's Data Runtime provides sandboxed storage and SQLite; its Web Runtime also provides password helpers, `send_file`, and file-mode `send_mail`.

## Build

```sh
make
make test
```

## CLI

```text
polonio help
polonio version
polonio run <file.pol>
polonio <file.pol>
polonio --dump-ast <expr>
polonio serve [--root DIR] [--port N]
```

Run the included examples:

```sh
export POLONIO_STORAGE_PATH="$PWD/.polonio-storage"
export POLONIO_SESSION_SECRET="development-secret-change-me"
mkdir -p "$POLONIO_STORAGE_PATH"
./build/polonio serve --root ./examples --port 8080
```

The server listens on `127.0.0.1`, defaults to port `8080` and the current directory, serves static assets and `.pol` templates, resolves extensionless paths to `.pol` files, uses `index.pol` then `index.html` for directories, and renders `404.pol` when available.

## Current Runtime Boundaries

- CGI and `polonio serve` are adapters shipped by the reference distribution's Web Runtime; neither is required for Language Core conformance.
- `polonio serve` is single-threaded, loopback-only, and intended for local development—not a hardened public Internet server.
- The server supports GET/POST, URL-encoded forms, multipart uploads, and JSON access through `request_json()`. It does not provide TLS, keep-alive, concurrent handling, or chunked request bodies.
- `send_mail` writes `.eml` files to the sandboxed storage outbox; SMTP delivery and background jobs are not implemented.
- The Data Runtime provides the reference distribution's SQLite backend and sandboxed storage through `POLONIO_STORAGE_PATH`; database and upload paths use that root.
- There is no framework router, middleware, ORM, or production-server feature set.

## Documentation

- [Language specification](docs/polonio_language_spec_v0_1.md) — syntax and language contract
- [HTTP/runtime guide](docs/site/runtime.html) — CGI, server, request, session, response, storage, and mail behavior
- [Builtin reference](docs/site/builtins.html) — registered runtime APIs
- [Examples](examples/README.md) — runnable filesystem routes
- [Architecture audit](ARCHITECTURE_AUDIT.md) — current implementation snapshot
- [Conformance profiles](docs/CONFORMANCE_PROFILES.md) — language, runtime, and reference-distribution guarantees
- [Conformance matrix](docs/CONFORMANCE_MATRIX.md) — definitive profile quick reference

## License

MIT. See [LICENSE](LICENSE).
