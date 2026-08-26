#!/usr/bin/env python3
"""Exercise sakura-ctl composition against an isolated real agent."""

import argparse
import configparser
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time


def run(*args):
    result = subprocess.run(args, text=True, capture_output=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(args)}\n"
            f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--agent", required=True)
    parser.add_argument("--ctl", required=True)
    parser.add_argument("--fake-codex", required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="sakura-ctl-test-") as temp:
        root = Path(temp)
        socket = root / "agent.sock"
        workspace = root / "workspace.ini"
        prompt = root / "prompt.md"
        manifest = root / "manifest.yml"
        prompt.write_text("Inspect the camera worktree.\n", encoding="utf-8")
        manifest.write_text(
            "sessions:\n"
            "  - title: \"Tony · Manifest\"\n"
            "    working_directory: /tmp\n"
            f"    prompt_file: {prompt}\n"
            "    reasoning: high\n",
            encoding="utf-8",
        )
        env = os.environ.copy()
        env["SAKURA_CODEX_BINARY"] = str(Path(args.fake_codex).resolve())
        agent = subprocess.Popen([
            args.agent, "--socket", str(socket), "--workspace-file",
            str(workspace), "--workspace-id", "ctl-integration",
        ], env=env)
        try:
            deadline = time.monotonic() + 5
            while not socket.exists() and time.monotonic() < deadline:
                time.sleep(0.02)
            assert socket.exists(), "agent socket did not appear"
            target = ("--socket", str(socket), "--workspace", "ctl-integration")
            created = run(
                args.ctl, "codex", *target, "--group-name", "Tony",
                "--create-group", "--title", "Tony · Camera",
                "--working-directory", "/tmp", "--prompt-file", str(prompt),
                "--reasoning", "high", "--print", "json",
            )
            result = json.loads(created.stdout)
            assert result["workspace_id"] == "ctl-integration"
            assert result["group_id"] and result["page_id"] and result["terminal_id"]
            groups = run(args.ctl, "groups", *target, "--print", "json")
            assert any(item["name"] == "Tony" for item in
                       map(json.loads, groups.stdout.splitlines()))

            first = run(
                args.ctl, "codex", "apply", *target, "--group-name", "Tony",
                "--manifest", str(manifest), "--print", "json",
            )
            second = run(
                args.ctl, "codex", "apply", *target, "--group-name", "Tony",
                "--manifest", str(manifest), "--print", "json",
            )
            assert json.loads(first.stdout)["page_id"]
            assert json.loads(second.stdout)["status"] == "reused"
            saved = configparser.ConfigParser()
            saved.read(workspace, encoding="utf-8")
            assert saved["Session"].getint("page_count") == 2
            page = saved["Page0"]
            assert page["title"] == "Tony · Camera"
            assert page.getboolean("title_set_by_user")
        finally:
            agent.terminate()
            try:
                agent.wait(timeout=3)
            except subprocess.TimeoutExpired:
                agent.kill()
                agent.wait()


if __name__ == "__main__":
    main()
