# Contributor testing guide

This guide collects the day-to-day commands contributors use to build the server, exercise the
integration suite, and debug failures without guessing which preset or helper target to rely on.

## Build and run the server with presets

1. Configure a Debug tree (includes test targets and compile commands):

   ```sh
   cmake --preset dev-debug
   ```

2. Build the server binary, CLI, and stage the Python clients used by integration tests:

   ```sh
   cmake --build --preset dev-debug
   ```

   For a lighter rebuild that skips the test binary, use `cmake --build --preset dev-debug-server`.

3. Create a minimal TOML config that points at a writable data directory (adjust the port if
   `6767` is in use):

   ```toml
   db_path = "./.local/jubildb-data"
   listen_address = "127.0.0.1"
   listen_port = 6767
   ```

   The server refuses `listen_port = 0`; pick an unused port instead.

4. Initialize the database path once with the CLI to avoid permission surprises:

   ```sh
   ./build/dev-debug/jubectl init ./.local/jubildb-data
   ```

5. Run the server:

   ```sh
   ./build/dev-debug/jubildb_server --config ./server.toml --workers 4 --backlog 64
   ```

   Logs are emitted to stdout/stderr. Stop with `Ctrl+C`.

## Integration tests

### CTest (C++ and end-to-end)

1. Build the tests (includes the integration suite and the staged Python clients):

   ```sh
   cmake --build --preset dev-debug
   ```

2. Run the full suite:

   ```sh
   ctest --preset dev-debug
   ```

3. To focus on the dual-client integration cases only:

   ```sh
   ctest --preset dev-debug -R DualClient
   ```

   The preset already enables `--output-on-failure`; additional logs live under
   `build/dev-debug/Testing/Temporary/LastTest.log`.

### Python-oriented flows

The integration tests shell out to the staged Python clients, so `python3` must be available. No
extra pip packages are required. If you add pure-Python integration tests under `tests/python/`, use:

```sh
PYTHONPATH=build/dev-debug/python_clients pytest tests/python
```

## Dependencies and data preparation

- **Presets:** Always configure via `cmake --preset dev-debug` (or `dev-debug-tidy` for clang-tidy).
- **Binaries:** Build `jubildb_server` and `jubectl` via the `dev-debug-server` preset or the full
  `dev-debug` build. The Python clients are staged automatically into `build/<preset>/python_clients/`.
- **Python:** `python3` is required for the integration harness; the clients rely only on the
  standard library. Set `PYTHONPATH=build/<preset>/python_clients` when invoking the Python clients
  outside the build tree so imports resolve.
- **Sample data:** `jubectl init <path>` bootstraps a new data directory and is a safe way to create
  throwaway stores for local testing. The integration suite creates and deletes its own temporary
  directories, but crashed runs can leave `/tmp/jubildb-dual-client-*` paths behind.

## Troubleshooting

- **Port conflicts:** If `jubildb_server` fails to bind, pick a different `listen_port` in
  `server.toml` (for example, `7676`) and retry. Avoid `0`, which fails validation.
- **Missing artifacts:** Errors like “jubectl binary not found” or missing Python clients mean the
  `dev-debug` build was skipped. Re-run `cmake --build --preset dev-debug` to stage everything.
- **Cleanup:** Stop stray servers with `pkill -f jubildb_server` or by terminating the process ID,
  then remove leftover temp directories (`rm -rf /tmp/jubildb-dual-client-*`).
- **Logs:** Server output goes to stdout/stderr. CTest keeps transcripts in
  `build/<preset>/Testing/Temporary/LastTest.log`, and individual integration failures echo
  their command output in the test failure message.
