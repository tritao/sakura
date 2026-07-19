#!/usr/bin/env python3
"""Exercise the versioned Sakura-to-Codex session-helper protocol."""

import argparse
import base64
import os
from pathlib import Path
import selectors
import stat
import subprocess
import sys
import tempfile


SESSION_ID = "fixture-session"


def make_fake_codex(directory: Path) -> None:
    fake_codex = directory / "codex"
    fake_codex.write_text(
        "#!/usr/bin/env python3\n"
        "import json\n"
        "import sys\n"
        "for line in sys.stdin:\n"
        "    request = json.loads(line)\n"
        "    if 'id' not in request:\n"
        "        continue\n"
        "    if request.get('id') == 1:\n"
        "        response = {'id': 1, 'result': {}}\n"
        "    else:\n"
        "        response = {'id': request.get('id'), 'result': {'thread': {\n"
        "            'id': 'fixture-thread', 'name': 'Fixture', 'cwd': '/tmp'}}}\n"
        "    print(json.dumps(response), flush=True)\n",
        encoding="utf-8",
    )
    fake_codex.chmod(fake_codex.stat().st_mode | stat.S_IXUSR)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--helper", required=True, type=Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="sakura-codex-helper-") as directory:
        fake_bin = Path(directory)
        make_fake_codex(fake_bin)
        environment = os.environ.copy()
        environment["PATH"] = os.pathsep.join((str(fake_bin), environment.get("PATH", "")))
        request = "\t".join(
            (
                "1",
                "v1",
                "info",
                base64.b64encode(SESSION_ID.encode()).decode(),
            )
        )
        process = subprocess.Popen(
            [str(args.helper), "--server"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=environment,
        )
        stdout = ""
        stderr = ""
        selector = None
        try:
            process.stdin.write(request + "\n")
            process.stdin.flush()
            process.stdin.close()
            selector = selectors.DefaultSelector()
            selector.register(process.stdout, selectors.EVENT_READ)
            if not selector.select(10):
                raise AssertionError("Codex session helper did not respond")
            stdout = process.stdout.readline()
        except (OSError, subprocess.TimeoutExpired) as error:
            raise AssertionError(f"Codex session helper failed: {error}")
        finally:
            if selector is not None:
                selector.close()
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            stderr = process.stderr.read()

    if process.returncode is not None and process.returncode > 0:
        raise AssertionError(f"helper failed: {stderr.strip()}")
    fields = stdout.strip().split("\t")
    if len(fields) != 5 or fields[:3] != ["1", "v1", "ok"]:
        raise AssertionError(f"unexpected helper response: {stdout!r}")
    name = base64.b64decode(fields[3], validate=True).decode()
    cwd = base64.b64decode(fields[4], validate=True).decode()
    if (name, cwd) != ("Fixture", "/tmp"):
        raise AssertionError(f"unexpected session info: {(name, cwd)!r}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
