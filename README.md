# Polonio

Polonio is a small, HTML-first templating language for rendering dynamic pages. Write ordinary markup, add Polonio where a page needs data or control flow, and run the result with one C++17 binary.

It is a good fit for local template-driven applications, small CGI deployments, and people who want templates to remain easy to read. The official Reference Distribution includes the language, template renderer, CGI adapter, and local development server.

## What you can do

- Mix HTML with `<% %>` code blocks, `$var` interpolation, inline `echo`, and relative includes.
- Use arrays, objects, functions, loops, conditions, and a standard set of helpers.
- Build local web applications with forms, uploads, sessions, JSON requests, sandboxed storage, and SQLite when using the Reference Distribution.

## Start here

Build Polonio and run its tests:

```sh
make
make test
```

Create `hello.pol`:

```pol
<% var name = "World" %>
<h1>Hello $name!</h1>
```

Render it:

```sh
./build/polonio run hello.pol
```

Continue with the [Language guide](docs/site/language.html), then try the runnable [examples](examples/README.md).

## Commands

```text
polonio help
polonio version
polonio run <file.pol>
polonio <file.pol>
polonio --dump-ast <expr>
polonio serve [--root DIR] [--port N]
```

To run the included web examples:

```sh
export POLONIO_STORAGE_PATH="$PWD/.polonio-storage"
export POLONIO_SESSION_SECRET="development-secret-change-me"
mkdir -p "$POLONIO_STORAGE_PATH"
./build/polonio serve --root ./examples --port 8080
```

Open `http://127.0.0.1:8080/`. The server listens on `127.0.0.1`, serves static assets and `.pol` templates, and defaults to port `8080` and the current directory.

## Runtime notes

The bundled server is loopback-only and intended for local development; it is not a public production server. It does not provide TLS, concurrent handling, keep-alive, SMTP delivery, or framework features such as routing and an ORM. See the [Runtime guide](docs/site/runtime.html) for the complete capability and deployment notes.

## Documentation

- [Language guide](docs/site/language.html) — learn template syntax and core concepts
- [Examples](docs/site/examples.html) — run small, focused templates in a useful order
- [Built-in functions](docs/site/builtins.html) — look up helpers by task
- [Runtime guide](docs/site/runtime.html) — learn the optional web and data capabilities
- [Language specification](docs/polonio_language_spec_v0_1.md) — precise v0.1 language contract
- [Conformance profiles](docs/CONFORMANCE_PROFILES.md) and [matrix](docs/CONFORMANCE_MATRIX.md) — implementation capability guarantees

## License

MIT. See [LICENSE](LICENSE).
