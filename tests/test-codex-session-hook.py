#!/usr/bin/env python3
"""Exercise Codex hook detection, repair, and startup session capture."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def run(helper: Path, home: Path, argument: str, event=None) -> subprocess.CompletedProcess:
    environment = os.environ.copy()
    environment["CODEX_HOME"] = str(home)
    environment["SAKURA_CODEX_TRACKING_DIR"] = str(home / "tracking")
    environment["SAKURA_CODEX_TAB_TOKEN"] = "fixture-tab"
    return subprocess.run(
        [str(helper), argument] if argument else [str(helper)],
        input=json.dumps(event) if event is not None else None,
        capture_output=True,
        text=True,
        env=environment,
        check=False,
    )


def expect_status(helper: Path, home: Path, expected: str) -> None:
    result = run(helper, home, "--status")
    if result.returncode != 0 or result.stdout.strip() != expected:
        raise AssertionError(
            f"expected hook status {expected!r}, got "
            f"{result.returncode}: {result.stdout!r} {result.stderr!r}"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--helper", required=True, type=Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="sakura-codex-hook-") as directory:
        home = Path(directory)
        expect_status(args.helper, home, "missing")

        installed = run(args.helper, home, "--install")
        if installed.returncode != 0:
            raise AssertionError(f"hook install failed: {installed.stderr}")
        expect_status(args.helper, home, "enabled")

        hooks_path = home / "hooks.json"
        config = json.loads(hooks_path.read_text(encoding="utf-8"))
        del config["hooks"]["SessionStart"]
        hooks_path.write_text(json.dumps(config), encoding="utf-8")
        expect_status(args.helper, home, "partial")
        if run(args.helper, home, "--install").returncode != 0:
            raise AssertionError("partial hook repair failed")
        expect_status(args.helper, home, "enabled")

        event = {
            "session_id": "fixture-session",
            "hook_event_name": "SessionStart",
        }
        recorded = run(args.helper, home, "", event)
        if recorded.returncode != 0:
            raise AssertionError(f"SessionStart recording failed: {recorded.stderr}")
        tracking = (home / "tracking" / "fixture-tab").read_text(encoding="utf-8")
        if "session_id=fixture-session" not in tracking or "event=SessionStart" not in tracking:
            raise AssertionError(f"unexpected tracking record: {tracking!r}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
