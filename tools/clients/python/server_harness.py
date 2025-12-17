from __future__ import annotations

import contextlib
import os
import pathlib
import re
import shutil
import subprocess
import tempfile
import time
from dataclasses import dataclass
from typing import Mapping, Optional

_START_LINE = re.compile(r"jubildb server started .*?:([0-9]+)\s*$")


def _repo_root() -> pathlib.Path:
    cursor = pathlib.Path(__file__).resolve()
    for ancestor in (cursor, *cursor.parents):
        if (ancestor / "CMakeLists.txt").exists():
            return ancestor
    raise FileNotFoundError("Unable to locate repository root from server_harness.py")


def find_server_binary() -> pathlib.Path:
    root = _repo_root()
    cwd = pathlib.Path.cwd()
    candidates = [
        cwd / "jubildb_server",
        root / "build" / "dev-debug" / "jubildb_server",
        root / "build" / "dev-debug-tidy" / "jubildb_server",
    ]

    for candidate in candidates:
        if candidate.is_file():
            return candidate

    raise FileNotFoundError("jubildb_server binary not found; build the server target first")


@dataclass
class RunningServer:
    process: subprocess.Popen[bytes]
    port: int
    log_path: pathlib.Path
    config_path: pathlib.Path
    db_path: pathlib.Path
    workspace: pathlib.Path

    def stop(self, timeout: float = 5.0) -> None:
        _terminate_process(self.process, timeout)


def _write_config(config_path: pathlib.Path, *, db_path: pathlib.Path, host: str,
                  port: int) -> None:
    contents = [
        f"db_path = \"{db_path}\"",
        f"listen_address = \"{host}\"",
        f"listen_port = {port}",
    ]
    config_path.write_text("\n".join(contents) + "\n", encoding="utf-8")


def _safe_read(path: pathlib.Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError:
        return ""


def _wait_for_startup(process: subprocess.Popen[bytes], log_path: pathlib.Path,
                      timeout: float) -> int:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        output = _safe_read(log_path)
        match = _START_LINE.search(output)
        if match:
            return int(match.group(1))

        if process.poll() is not None:
            raise RuntimeError(
                f"jubildb_server exited early with code {process.returncode}. See log at {log_path}\n{output}")

        time.sleep(0.05)

    raise TimeoutError(
        f"Timed out waiting for jubildb_server to start after {timeout} seconds. See log at {log_path}\n{_safe_read(log_path)}"
    )


def _terminate_process(process: subprocess.Popen[bytes], timeout: float) -> None:
    if process.poll() is not None:
        return

    process.terminate()
    try:
        process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=timeout)


@contextlib.contextmanager
def running_server(*,
                   binary_path: Optional[pathlib.Path] = None,
                   listen_host: str = "127.0.0.1",
                   listen_port: int = 0,
                   workers: Optional[int] = None,
                   backlog: Optional[int] = None,
                   db_path: Optional[pathlib.Path] = None,
                   startup_timeout: float = 10.0,
                   shutdown_timeout: float = 5.0,
                   cleanup: bool = False,
                   env: Optional[Mapping[str, str]] = None):
    workspace = pathlib.Path(tempfile.mkdtemp(prefix="jubildb-server-"))
    database_path = pathlib.Path(db_path) if db_path is not None else workspace / "db"
    database_path.mkdir(parents=True, exist_ok=True)

    config_path = workspace / "server.toml"
    _write_config(config_path, db_path=database_path, host=listen_host, port=listen_port)

    log_path = workspace / "jubildb_server.log"
    log_path.touch()

    binary = pathlib.Path(binary_path) if binary_path is not None else find_server_binary()
    if not binary.is_file():
        raise FileNotFoundError(f"jubildb_server binary missing at {binary}")

    command = [str(binary), "--config", str(config_path)]
    if workers is not None:
        command.extend(["--workers", str(workers)])
    if backlog is not None:
        command.extend(["--backlog", str(backlog)])

    env_vars = dict(os.environ)
    if env:
        env_vars.update(env)

    with log_path.open("ab", buffering=0) as log_stream:
        process = subprocess.Popen(command,
                                   stdout=log_stream,
                                   stderr=subprocess.STDOUT,
                                   cwd=binary.parent,
                                   env=env_vars)

        try:
            port = _wait_for_startup(process, log_path, startup_timeout)
            yield RunningServer(process=process,
                                port=port,
                                log_path=log_path,
                                config_path=config_path,
                                db_path=database_path,
                                workspace=workspace)
        finally:
            _terminate_process(process, shutdown_timeout)
            if cleanup:
                shutil.rmtree(workspace, ignore_errors=True)
