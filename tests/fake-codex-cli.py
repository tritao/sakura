#!/usr/bin/env python3
"""Minimal long-lived Codex stand-in for sakura-ctl integration tests."""

import signal
import sys
import time


signal.signal(signal.SIGHUP, signal.SIG_IGN)
signal.signal(signal.SIGTERM, lambda *_args: sys.exit(0))
while True:
    time.sleep(1)
