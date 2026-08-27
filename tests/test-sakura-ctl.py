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
        goal_state = root / "codex-goals.json"
        goal = root / "collision-static.goal"
        prompt.write_text("Inspect the camera worktree.\n", encoding="utf-8")
        goal.write_text("Fix static collision and verify its tests.\n", encoding="utf-8")
        manifest.write_text(
            "sessions:\n"
            "  - title: \"Tony · Manifest\"\n"
            "    session_name: collision\n"
            "    working_directory: /tmp\n"
            f"    prompt_file: {prompt}\n"
            "    model: gpt-5.6-luna\n"
            "    reasoning: xhigh\n"
            f"    goal_file: {goal}\n"
            "    goal_policy: start-if-none\n",
            encoding="utf-8",
        )
        env = os.environ.copy()
        env["SAKURA_CODEX_BINARY"] = str(Path(args.fake_codex).resolve())
        env["SAKURA_FAKE_CODEX_ARGUMENTS_LOG"] = str(arguments_log)
        env["SAKURA_FAKE_CODEX_GOAL_STATE"] = str(goal_state)
        os.environ["SAKURA_CODEX_BINARY"] = env["SAKURA_CODEX_BINARY"]
        os.environ["SAKURA_FAKE_CODEX_ARGUMENTS_LOG"] = env[
            "SAKURA_FAKE_CODEX_ARGUMENTS_LOG"
        ]
        os.environ["SAKURA_FAKE_CODEX_GOAL_STATE"] = env[
            "SAKURA_FAKE_CODEX_GOAL_STATE"
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
                "--columns", "132", "--rows", "41",
                "--print", "json",
            )
            result = json.loads(created.stdout)
            assert result["workspace_id"] == "ctl-integration"
            assert result["group_id"] and result["page_id"] and result["terminal_id"]
            started_goal = run(
                args.ctl, "goal", "start", *target, "--group", "Tony",
                "--title", "Tony · Camera", "--file", str(goal),
            )
            assert json.loads(started_goal.stdout)["goal"]["status"] == "active"
            run(
                args.ctl, "goal", "clear", *target, "--group-name", "Tony",
                "--title", "Tony · Camera",
            )
            groups = run(args.ctl, "groups", *target, "--print", "json")
            assert any(item["name"] == "Tony" for item in
                       map(json.loads, groups.stdout.splitlines()))

            first = run(
                args.ctl, "codex", "apply", *target, "--group-name", "Tony",
                "--manifest", str(manifest), "--print", "json",
            )
            planned = run(
                args.ctl, "codex", "apply", *target, "--group-name", "Tony",
                "--manifest", str(manifest), "--dry-run", "--print", "json",
            )
            assert json.loads(planned.stdout)["status"] == "reuse"
            second = run(
                args.ctl, "codex", "apply", *target, "--group-name", "Tony",
                "--manifest", str(manifest), "--print", "json",
            )
            assert json.loads(first.stdout)["page_id"]
            assert json.loads(second.stdout)["status"] == "reused"
            goal_status = run(
                args.ctl, "goal", "status", *target, "--group-name", "Tony",
                "--title", "Tony · Manifest",
            )
            assert json.loads(goal_status.stdout)["goal"]["status"] == "active"
            assert "static collision" in json.loads(goal_status.stdout)["goal"][
                "objective"
            ]
            paused = run(
                args.ctl, "goal", "pause", *target, "--group-name", "Tony",
                "--session-name", "collision",
            )
            assert json.loads(paused.stdout)["goal"]["status"] == "paused"
            resumed = run(
                args.ctl, "goal", "resume", *target, "--group-name", "Tony",
                "--title", "Tony · Manifest",
            )
            assert json.loads(resumed.stdout)["goal"]["status"] == "active"
            cleared = run(
                args.ctl, "goal", "clear", *target, "--group-name", "Tony",
                "--title", "Tony · Manifest",
            )
            assert json.loads(cleared.stdout)["cleared"]
            restarted_goal = run(
                args.ctl, "codex", "apply", *target, "--group-name", "Tony",
                "--manifest", str(manifest), "--print", "json",
            )
            assert json.loads(restarted_goal.stdout)["status"] == "reused"
            renamed_manifest = root / "renamed-manifest.yml"
            renamed_manifest.write_text(
                manifest.read_text(encoding="utf-8").replace(
                    "Tony · Manifest", "Display title changed"
                ),
                encoding="utf-8",
            )
            renamed_reuse = run(
                args.ctl, "codex", "apply", *target, "--group-name", "Tony",
                "--manifest", str(renamed_manifest), "--print", "json",
            )
            assert json.loads(renamed_reuse.stdout)["status"] == "reused"
            duplicate_manifest = root / "duplicate-manifest.yml"
            duplicate_manifest.write_text(
                manifest.read_text(encoding="utf-8") +
                manifest.read_text(encoding="utf-8").split("sessions:\n", 1)[1],
                encoding="utf-8",
            )
            duplicate = subprocess.run([
                args.ctl, "codex", "apply", *target, "--group-name", "Tony",
                "--manifest", str(duplicate_manifest), "--dry-run",
            ], text=True, capture_output=True)
            assert duplicate.returncode != 0
            assert "duplicate session_name 'collision'" in duplicate.stderr
            conflict_manifest = root / "conflict-manifest.yml"
            conflict_manifest.write_text(
                manifest.read_text(encoding="utf-8").replace(
                    "gpt-5.6-luna", "gpt-5.6-terra"
                ),
                encoding="utf-8",
            )
            conflict = subprocess.run([
                args.ctl, "codex", "apply", *target, "--group-name", "Tony",
                "--manifest", str(conflict_manifest), "--dry-run", "--print", "json",
            ], text=True, capture_output=True)
            assert conflict.returncode != 0
            assert json.loads(conflict.stdout)["status"] == "conflict"
            preflight_manifest = root / "preflight-manifest.yml"
            preflight_manifest.write_text(
                "sessions:\n"
                "  - title: preflight-new\n"
                "    session_name: preflight-new\n"
                "    working_directory: /tmp\n"
                "    model: gpt-5.6-luna\n"
                "    reasoning: xhigh\n" +
                conflict_manifest.read_text(encoding="utf-8").split(
                    "sessions:\n", 1
                )[1],
                encoding="utf-8",
            )
            before_preflight = configparser.ConfigParser()
            before_preflight.read(workspace, encoding="utf-8")
            rejected = subprocess.run([
                args.ctl, "codex", "apply", *target, "--group-name", "Tony",
                "--manifest", str(preflight_manifest), "--print", "json",
            ], text=True, capture_output=True)
            assert rejected.returncode != 0
            after_preflight = configparser.ConfigParser()
            after_preflight.read(workspace, encoding="utf-8")
            assert after_preflight["Session"].getint("page_count") == \
                before_preflight["Session"].getint("page_count")
            missing_identity = root / "missing-identity.yml"
            missing_identity.write_text(
                "sessions:\n  - title: legacy\n"
                "    working_directory: /tmp\n",
                encoding="utf-8",
            )
            missing = subprocess.run([
                args.ctl, "codex", "apply", *target, "--group-name", "Tony",
                "--manifest", str(missing_identity), "--dry-run",
            ], text=True, capture_output=True)
            assert missing.returncode != 0
            assert "requires session_name" in missing.stderr
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
            manifest_pages = [saved[f"Page{index}"] for index in range(2)
                              if saved[f"Page{index}"]["title"] == "Tony · Manifest"]
            assert len(manifest_pages) == 1
            assert manifest_pages[0].getboolean("title_set_by_user")
            terminals = [saved[f"Terminal{index}"] for index in range(2)]
            assert all(item["kind"] == "codex" for item in terminals)
            assert all(item["codex_model"] == "gpt-5.6-luna"
                       for item in terminals)
            assert all(item["codex_reasoning_effort"] == "xhigh"
                       for item in terminals)
            assert all(item.getint("cols") == 132 for item in terminals)
            assert all(item.getint("rows") == 41 for item in terminals)
            assert all(item["codex_session_id"] for item in terminals)
            assert sum(item.get("codex_session_name") == "collision"
                       for item in terminals) == 1
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
            assert len(launches) == 19
            assert launches[1] == ["app-server", "--stdio", "--enable", "goals"]
            tui_launches = [launch for launch in launches
                            if launch[:1] != ["app-server"]]
            for launch in tui_launches[:4]:
                assert launch[launch.index("--model") + 1] == "gpt-5.6-luna"
                assert "model_reasoning_effort=xhigh" in launch
            assert all("resume" in launch for launch in tui_launches[1:4])
            assert "9f328589-2569-5184-a037-0d4415dbb70d" in tui_launches[1]
            assert tui_launches[4][tui_launches[4].index("--model") + 1] == "exit-immediately"
            assert tui_launches[5][tui_launches[5].index("--model") + 1] == "no-session"

            recovered_stale = run(
                args.ctl, "codex", "apply", *target, "--group-name", "Tony",
                "--manifest", str(conflict_manifest), "--recover-stale",
                "--print", "json",
            )
            assert json.loads(recovered_stale.stdout)["page_id"]
            original_still_reusable = run(
                args.ctl, "codex", "apply", *target, "--group-name", "Tony",
                "--manifest", str(manifest), "--dry-run", "--print", "json",
            )
            assert json.loads(original_still_reusable.stdout)["status"] == "reuse"

            interrupted_manifest = root / "interrupted-manifest.yml"
            interrupted_manifest.write_text(
                "sessions:\n"
                "  - title: interrupt-one\n"
                "    session_name: interrupt-one\n"
                "    working_directory: /tmp\n"
                "    model: gpt-5.6-luna\n"
                "    reasoning: xhigh\n"
                "  - title: interrupt-two\n"
                "    session_name: interrupt-two\n"
                "    working_directory: /tmp\n"
                "    model: slow-session\n"
                "    reasoning: xhigh\n",
                encoding="utf-8",
            )
            saved.read(workspace, encoding="utf-8")
            before_interrupt = saved["Session"].getint("page_count")
            applying = subprocess.Popen([
                args.ctl, "codex", "apply", *target, "--group-name", "Tony",
                "--manifest", str(interrupted_manifest),
            ], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                saved.read(workspace, encoding="utf-8")
                if saved["Session"].getint("page_count") >= before_interrupt + 2:
                    break
                time.sleep(0.02)
            assert saved["Session"].getint("page_count") >= before_interrupt + 2
            applying.terminate()
            applying.wait(timeout=3)
            time.sleep(2.2)
            repeated = run(
                args.ctl, "codex", "apply", *target, "--group-name", "Tony",
                "--manifest", str(interrupted_manifest), "--print", "json",
            )
            repeated_statuses = [json.loads(line)["status"] for line in
                                 repeated.stdout.splitlines()]
            assert repeated_statuses == ["reused", "reused"]

            stop_agent(agent)
            archived = configparser.ConfigParser()
            archived.read(workspace, encoding="utf-8")
            for index in range(archived["Session"].getint("page_count")):
                page_section = archived[f"Page{index}"]
                page_id = page_section["id"]
                matching = [archived[f"Terminal{terminal_index}"] for terminal_index
                            in range(archived["Session"].getint("terminal_count"))
                            if archived[f"Terminal{terminal_index}"]["page_id"] == page_id]
                if (matching and matching[0].get("codex_session_name") == "collision" and
                        matching[0].get("codex_model") == "gpt-5.6-luna"):
                    page_section["archived"] = "true"
            with workspace.open("w", encoding="utf-8") as output:
                archived.write(output)
            socket.unlink(missing_ok=True)
            agent = subprocess.Popen([
                args.agent, "--socket", str(socket), "--workspace-file",
                str(workspace), "--session", str(session),
                "--workspace-id", "ctl-integration",
            ], env=env)
            deadline = time.monotonic() + 5
            while not socket.exists() and time.monotonic() < deadline:
                time.sleep(0.02)
            archived_history = run(
                args.ctl, "codex", "apply", *target, "--group-name", "Tony",
                "--manifest", str(conflict_manifest), "--dry-run", "--print", "json",
            )
            assert json.loads(archived_history.stdout)["status"] == "reuse"
        finally:
            stop_agent(agent)


if __name__ == "__main__":
    main()
