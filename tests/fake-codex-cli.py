#!/usr/bin/env python3
"""Minimal long-lived Codex stand-in for sakura-ctl integration tests."""

import signal
import json
import os
from pathlib import Path
import sys
import time
import uuid


if sys.argv[1:] == ["features", "list"]:
    print("in_app_updates stable true")
    sys.exit(0)

arguments_log = os.environ.get("SAKURA_FAKE_CODEX_ARGUMENTS_LOG")
if arguments_log:
    with open(arguments_log, "a", encoding="utf-8") as output:
        output.write(json.dumps(sys.argv[1:]) + "\n")

if sys.argv[1:3] == ["app-server", "--stdio"]:
    thread_id = "9f328589-2569-5184-a037-0d4415dbb70d"
    goal_state_path = os.environ.get("SAKURA_FAKE_CODEX_GOAL_STATE")

    def load_goals():
        if goal_state_path and Path(goal_state_path).exists():
            return json.loads(Path(goal_state_path).read_text(encoding="utf-8"))
        return {}

    def save_goals(goals):
        if goal_state_path:
            Path(goal_state_path).write_text(json.dumps(goals), encoding="utf-8")

    for line in sys.stdin:
        message = json.loads(line)
        method = message.get("method")
        request_id = message.get("id")
        if request_id is None:
            continue
        if method == "initialize":
            result = {"userAgent": "fake-codex"}
        elif method == "thread/start":
            result = {"thread": {"id": thread_id}}
        elif method == "thread/resume":
            thread_id = message["params"]["threadId"]
            result = {"thread": {"id": thread_id}}
        elif method == "thread/name/set":
            result = {}
        elif method == "turn/start":
            turn_id = "fake-initial-turn"
            result = {"turn": {"id": turn_id, "status": "inProgress"}}
        elif method == "thread/goal/get":
            result = {"goal": load_goals().get(message["params"]["threadId"])}
        elif method == "thread/goal/set":
            params = message["params"]
            goals = load_goals()
            goal = goals.get(params["threadId"], {
                "threadId": params["threadId"], "objective": "",
                "createdAt": 1, "tokensUsed": 0, "timeUsedSeconds": 0,
            })
            if params.get("objective") is not None:
                goal["objective"] = params["objective"]
            if params.get("status") is not None:
                goal["status"] = params["status"]
            goal["updatedAt"] = 2
            goals[params["threadId"]] = goal
            save_goals(goals)
            result = {"goal": goal}
        elif method == "thread/goal/clear":
            goals = load_goals()
            goals.pop(message["params"]["threadId"], None)
            save_goals(goals)
            result = {}
        else:
            print(json.dumps({"id": request_id, "error": {
                "code": -32601, "message": f"unsupported method: {method}"
            }}), flush=True)
            continue
        print(json.dumps({"id": request_id, "result": result}), flush=True)
        if method == "turn/start":
            print(json.dumps({
                "method": "turn/completed",
                "params": {"threadId": thread_id, "turn": {
                    "id": turn_id, "status": "completed"
                }},
            }), flush=True)
    sys.exit(0)

tracking_dir = os.environ.get("SAKURA_CODEX_TRACKING_DIR")
tracking_token = os.environ.get("SAKURA_CODEX_TAB_TOKEN")
if "slow-session" in sys.argv:
    time.sleep(2)
if tracking_dir and tracking_token and "no-session" not in sys.argv:
    session_id = str(uuid.uuid5(uuid.NAMESPACE_URL, tracking_token))
    tracking_path = Path(tracking_dir)
    tracking_path.mkdir(parents=True, exist_ok=True)
    (tracking_path / tracking_token).write_text(
        "[tracking]\n"
        f"session_id={session_id}\n"
        "event=SessionStart\n"
        "state=idle\n",
        encoding="utf-8",
    )

if "exit-immediately" in sys.argv:
    sys.exit(0)


signal.signal(signal.SIGHUP, signal.SIG_IGN)
signal.signal(signal.SIGTERM, lambda *_args: sys.exit(0))
while True:
    time.sleep(1)
