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


def stop_agent(process):
    process.terminate()
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


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
        session = root / "desktop.session"
        prompt = root / "prompt.md"
        manifest = root / "manifest.yml"
        arguments_log = root / "codex-arguments.jsonl"
        prompt.write_text("Inspect the camera worktree.\n", encoding="utf-8")
        manifest.write_text(
            "sessions:\n"
            "  - title: \"Tony · Manifest\"\n"
            "    working_directory: /tmp\n"
            f"    prompt_file: {prompt}\n"
            "    model: gpt-5.6-luna\n"
            "    reasoning: xhigh\n",
            encoding="utf-8",
        )
        env = os.environ.copy()
        env["SAKURA_CODEX_BINARY"] = str(Path(args.fake_codex).resolve())
        env["SAKURA_FAKE_CODEX_ARGUMENTS_LOG"] = str(arguments_log)
        os.environ["SAKURA_CODEX_BINARY"] = env["SAKURA_CODEX_BINARY"]
        os.environ["SAKURA_FAKE_CODEX_ARGUMENTS_LOG"] = env[
            "SAKURA_FAKE_CODEX_ARGUMENTS_LOG"
        ]
        agent = subprocess.Popen([
            args.agent, "--socket", str(socket), "--workspace-file",
            str(workspace), "--session", str(session),
            "--workspace-id", "ctl-integration",
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
                "--model", "gpt-5.6-luna", "--reasoning", "xhigh",
                "--print", "json",
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
            assert saved["Session"].getint("page_count") == 2, dict(saved["Session"])
            assert saved["Session"].getint("terminal_count") == 2, dict(saved["Session"])

            stop_agent(agent)
            socket.unlink(missing_ok=True)
            agent = subprocess.Popen([
                args.agent, "--socket", str(socket), "--workspace-file",
                str(workspace), "--session", str(session),
                "--workspace-id", "ctl-integration",
            ], env=env)
            deadline = time.monotonic() + 5
            while not socket.exists() and time.monotonic() < deadline:
                time.sleep(0.02)
            assert socket.exists(), "restarted agent socket did not appear"
            restored = configparser.ConfigParser()
            restored.read(workspace, encoding="utf-8")
            assert restored["Session"].getint("page_count") == 2, dict(restored["Session"])
            assert restored["Session"].getint("terminal_count") == 2, dict(restored["Session"])
            recovered = run(
                args.ctl, "codex", "apply", *target, "--group-name", "Tony",
                "--manifest", str(manifest), "--print", "json",
            )
            reused = run(
                args.ctl, "codex", "apply", *target, "--group-name", "Tony",
                "--manifest", str(manifest), "--print", "json",
            )
            assert json.loads(recovered.stdout)["status"] == "reused"
            assert json.loads(reused.stdout)["status"] == "reused"
            saved.read(workspace, encoding="utf-8")
            assert saved["Session"].getint("page_count") == 2, dict(saved["Session"])
            assert saved["Session"].getint("terminal_count") == 2, dict(saved["Session"])
            page = saved["Page0"]
            assert page["title"] == "Tony · Camera"
            assert page.getboolean("title_set_by_user")
            terminals = [saved[f"Terminal{index}"] for index in range(2)]
            assert all(item["kind"] == "codex" for item in terminals)
            assert all(item["codex_model"] == "gpt-5.6-luna"
                       for item in terminals)
            assert all(item["codex_reasoning_effort"] == "xhigh"
                       for item in terminals)
            assert all(item["codex_session_id"] for item in terminals)
            assert all(item.getboolean("resume_on_start") for item in terminals)

            run(
                args.ctl, "codex", *target, "--group-name", "Tony",
                "--title", "Exited normally", "--working-directory", "/tmp",
                "--resume", "74f4716f-68e5-4b88-90db-f91f0c451715",
                "--model", "exit-immediately", "--reasoning", "xhigh",
            )
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                saved.read(workspace, encoding="utf-8")
                exited = [saved[f"Terminal{index}"] for index in range(
                    saved["Session"].getint("terminal_count"))
                    if saved[f"Terminal{index}"]["codex_model"] ==
                    "exit-immediately"]
                if exited and not exited[0].getboolean("resume_on_start"):
                    break
                time.sleep(0.02)
            assert len(exited) == 1
            assert exited[0].getint("runtime_status") == 2
            assert not exited[0].getboolean("resume_on_start")

            failed_env = os.environ.copy()
            failed_env["SAKURA_CTL_READY_TIMEOUT_MS"] = "300"
            failed = subprocess.run([
                args.ctl, "codex", *target, "--group-name", "Tony",
                "--title", "Rolled back", "--working-directory", "/tmp",
                "--model", "no-session", "--reasoning", "xhigh",
            ], env=failed_env, text=True, capture_output=True)
            assert failed.returncode != 0
            assert "timed out waiting" in failed.stderr
            saved.read(workspace, encoding="utf-8")
            assert saved["Session"].getint("page_count") == 3
            assert saved["Session"].getint("terminal_count") == 3
            deadline = time.monotonic() + 2
            while (not arguments_log.exists() or
                   len(arguments_log.read_text(encoding="utf-8").splitlines()) < 2
                   ) and time.monotonic() < deadline:
                time.sleep(0.02)
            launches = [json.loads(line) for line in
                        arguments_log.read_text(encoding="utf-8").splitlines()]
            assert len(launches) == 7
            assert launches[1] == ["app-server", "--stdio"]
            tui_launches = [launch for launch in launches
                            if launch[:1] != ["app-server"]]
            for launch in tui_launches[:4]:
                assert launch[launch.index("--model") + 1] == "gpt-5.6-luna"
                assert "model_reasoning_effort=xhigh" in launch
            assert all("resume" in launch for launch in tui_launches[1:4])
            assert "9f328589-2569-5184-a037-0d4415dbb70d" in tui_launches[1]
            assert tui_launches[4][tui_launches[4].index("--model") + 1] == "exit-immediately"
            assert tui_launches[5][tui_launches[5].index("--model") + 1] == "no-session"
        finally:
            stop_agent(agent)


if __name__ == "__main__":
    main()
