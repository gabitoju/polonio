# Polonio examples

Build and run the examples from the repository root:

```sh
make
export POLONIO_STORAGE_PATH="$PWD/.polonio-storage"
export POLONIO_SESSION_SECRET="development-secret-change-me"
mkdir -p "$POLONIO_STORAGE_PATH"
./build/polonio serve --root ./examples --port 8080
```

Open `http://127.0.0.1:8080/` and start with `/hello`. Then try `/forms`,
`/json`, `/session`, `/upload`,
`/storage`, `/sqlite`, or `/mail`. `download.pol` needs a relative file already
present in the storage root (for example `downloads/example.txt`). Runtime data
belongs in `.polonio-storage/`, which is ignored by Git.

## Conway's Game of Life

`game_of_life.pol` is a standalone CLI validation example. From the repository
root, run:

```sh
./build/polonio run examples/game_of_life.pol
```

It continuously computes and renders successive generations of a glider on a
finite board; stop it with Ctrl-C. CLI template output is buffered until the
program exits, so this is a loop-semantics demonstration rather than a live
terminal animation. The board is an array of independently constructed rows,
each containing boolean cell values; `#` is live and `.` is dead. The example
exercises nested arrays and indexing, loops, functions, arithmetic and
comparisons, truthiness, output, and reference-like collection mutation while
building a separate next grid.

`send_mail` writes `.eml` files below `.polonio-storage/outbox`; it does not
deliver email. These examples use the official Reference Distribution's Web
and Data Runtimes. The included server is for local development only; see the
Runtime guide for its limits.
