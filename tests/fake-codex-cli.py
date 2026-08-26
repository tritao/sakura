#!/usr/bin/env python3
"""Minimal long-lived Codex stand-in for sakura-ctl integration tests."""

import signal
import json
import os
import sys
import time


arguments_log = os.environ.get("SAKURA_FAKE_CODEX_ARGUMENTS_LOG")
if arguments_log:
    with open(arguments_log, "a", encoding="utf-8") as output:
        output.write(json.dumps(sys.argv[1:]) + "\n")


signal.signal(signal.SIGHUP, signal.SIG_IGN)
signal.signal(signal.SIGTERM, lambda *_args: sys.exit(0))
while True:
    time.sleep(1)
