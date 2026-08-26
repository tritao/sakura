#!/usr/bin/env python3
"""Minimal long-lived Codex stand-in for sakura-ctl integration tests."""

import signal
import json
import os
from pathlib import Path
import sys
import time
import uuid


arguments_log = os.environ.get("SAKURA_FAKE_CODEX_ARGUMENTS_LOG")
if arguments_log:
    with open(arguments_log, "a", encoding="utf-8") as output:
        output.write(json.dumps(sys.argv[1:]) + "\n")

tracking_dir = os.environ.get("SAKURA_CODEX_TRACKING_DIR")
tracking_token = os.environ.get("SAKURA_CODEX_TAB_TOKEN")
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
