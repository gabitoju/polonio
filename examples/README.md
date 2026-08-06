# Polonio examples

Build and run the examples from the repository root:

```sh
make
export POLONIO_STORAGE_PATH="$PWD/.polonio-storage"
export POLONIO_SESSION_SECRET="development-secret-change-me"
mkdir -p "$POLONIO_STORAGE_PATH"
./build/polonio serve --root ./examples --port 8080
```

Open `http://127.0.0.1:8080/`, `/forms`, `/json`, `/session`, `/upload`,
`/storage`, `/sqlite`, or `/mail`. `download.pol` needs a relative file already
present in the storage root (for example `downloads/example.txt`). Runtime data
belongs in `.polonio-storage/`, which is ignored by Git.

`send_mail` writes `.eml` files below `.polonio-storage/outbox`; it does not
deliver email. These examples use the official reference distribution's Web
and Data Runtimes. The included server is for local development only; none of
those runtime capabilities is required for Language Core conformance.
