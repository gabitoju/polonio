# Scraps

Scraps is a deliberately small public plain-text publishing application. It validates Polonio's Web and Data Runtime through relative includes, SQLite, sessions, password hashing, CSRF checks, redirects, and escaped HTML output.

```sh
export POLONIO_STORAGE_PATH="$(mktemp -d /tmp/polonio-scraps.XXXXXX)"
export POLONIO_SESSION_SECRET="development-secret-change-me"
./build/polonio serve --root ./examples --port 8080
```

Open `http://127.0.0.1:8080/scraps/`. Register, log in, publish a named scrap, then publish the same name to overwrite it. Public query routes are `/scraps/?route=user&username=NAME` and `/scraps/?route=scrap&username=NAME&name=SCRAP`.

The database is `scraps.sqlite` below `POLONIO_STORAGE_PATH`; do not point it at the repository. Tables are created with `CREATE TABLE IF NOT EXISTS`. Validation is intentionally modest; password recovery, uploads, pagination, and production-server operation are out of scope.
