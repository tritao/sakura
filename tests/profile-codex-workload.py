#!/usr/bin/env python3
"""Profile Sakura with a deterministic, isolated Codex-like workload.

The profiler starts its own Xvfb server and uses a temporary Sakura config.
It never connects to, restarts, or writes the session of a running Sakura.
"""

import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile
import threading
import time


PROFILES = ("idle", "active", "mixed")
LATENCY_PATTERN = re.compile(
    r"selection-focus-latency-us=(\d+) terminal=(profile-codex-\d+)")
STALL_PATTERN = re.compile(
    r"ui-main-loop-stall-us=(\d+) cause=([a-z-]+) cause-age-us=(-?\d+)")
ACTIVITY_PATTERN = re.compile(r"ui-activity-us=(\d+) cause=([a-z-]+)")
BACKGROUND_ACTIVITY_PATTERN = re.compile(
    r"ui-background-activity-us=(\d+) cause=([a-z-]+)")
PAINT_LATENCY_PATTERN = re.compile(
    r"selection-paint-latency-us=(\d+) terminal=(profile-codex-\d+)")
FRAME_INTERVAL_PATTERN = re.compile(r"ui-frame-interval-us=(\d+)")
STARTUP_MILESTONE_PATTERN = re.compile(
    r"startup-milestone-us=(\d+) name=([a-z-]+)")


def require(command):
    if subprocess.run(["which", command], stdout=subprocess.DEVNULL,
                      stderr=subprocess.DEVNULL).returncode:
        raise SystemExit(f"required command not found: {command}")


def start_xvfb():
    read_fd, write_fd = os.pipe()
    process = subprocess.Popen(
        ["Xvfb", "-displayfd", str(write_fd), "-screen", "0",
         "1600x1000x24", "-nolisten", "tcp"],
        pass_fds=(write_fd,), stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    os.close(write_fd)
    try:
        display = os.read(read_fd, 32).decode("ascii").strip()
    finally:
        os.close(read_fd)
    if not display:
        process.terminate()
        raise RuntimeError("Xvfb did not provide a display")
    return process, f":{display}"


def run_xdotool(env, *args, timeout=5):
    return subprocess.run(["xdotool", *map(str, args)], env=env, check=True,
                          text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=timeout)


def wait_for_window(pid, env, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = subprocess.run(
            ["xdotool", "search", "--onlyvisible", "--pid", str(pid)],
            env=env, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        windows = result.stdout.split()
        if windows:
            return windows[-1]
        time.sleep(0.05)
    raise RuntimeError("Sakura window did not appear")


def window_origin(window, env):
    result = run_xdotool(env, "getwindowgeometry", "--shell", window)
    values = dict(line.split("=", 1) for line in result.stdout.splitlines()
                  if "=" in line)
    return int(values["X"]), int(values["Y"])


def startup_probe(binary, root, config, base_env, sessions, index):
    stats_dir = root / f"startup-stats-{index:02d}"
    log_path = root / f"startup-{index:02d}.log"
    env = base_env.copy()
    env["SAKURA_PROFILE_STATS_DIR"] = str(stats_dir)
    write_fixture(config, sessions)
    launched_us = time.monotonic_ns() // 1000
    with log_path.open("w", encoding="utf-8") as log:
        app = subprocess.Popen([str(binary), "--config-file", config.name],
                               cwd=root, env=env, stdout=log,
                               stderr=subprocess.STDOUT)
        try:
            wait_for_window(app.pid, env)
            window_us = time.monotonic_ns() // 1000
            deadline = time.monotonic() + 10
            milestones = {}
            converged_us = None
            while time.monotonic() < deadline and app.poll() is None:
                try:
                    contents = log_path.read_text(encoding="utf-8",
                                                  errors="replace")
                except OSError:
                    contents = ""
                milestones.update({name: int(stamp)
                                   for stamp, name in
                                   STARTUP_MILESTONE_PATTERN.findall(contents)})
                if converged_us is None and len(list(stats_dir.glob("*"))) >= sessions:
                    converged_us = time.monotonic_ns() // 1000
                if ("terminal-ready" in milestones and
                        "workspace-ready" in milestones and
                        converged_us is not None):
                    break
                time.sleep(0.01)
            if "terminal-ready" not in milestones:
                raise RuntimeError("startup probe did not reach terminal readiness")
            if converged_us is None:
                raise RuntimeError("startup probe did not converge Codex workers")
            restore_step_us = [
                int(value) for value, cause in ACTIVITY_PATTERN.findall(contents)
                if cause == "workspace-restore-step"
            ]
            return {
                "window_visible_us": window_us - launched_us,
                "agent_started_us": milestones["agent-started"] - launched_us,
                "workspace_restored_us": milestones["workspace-restored"] - launched_us,
                "workspace_ready_us": milestones["workspace-ready"] - launched_us,
                "terminal_ready_us": milestones["terminal-ready"] - launched_us,
                "codex_converged_us": converged_us - launched_us,
                "window_to_terminal_ready_us": (
                    milestones["terminal-ready"] - window_us),
                "agent_to_workspace_ready_us": (
                    milestones["workspace-ready"] - milestones["agent-started"]),
                "terminal_to_workspace_ready_us": (
                    milestones["workspace-ready"] - milestones["terminal-ready"]),
                "restore_step_total_us": sum(restore_step_us),
                "restore_step_max_us": max(restore_step_us, default=0),
            }
        finally:
            terminate_process_tree(app)


def startup_summary(probes):
    report = {"runs": len(probes), "cold": {}, "warm": {}}
    for key in probes[0] if probes else ():
        report["cold"][key.removesuffix("_us") + "_ms"] = round(
            probes[0][key] / 1000, 3)
        warm_values = [probe[key] for probe in probes[1:]]
        if warm_values:
            report["warm"][key.removesuffix("_us") + "_ms"] = duration_summary(
                warm_values)
    return report


def start_interaction_workload(app, env, sessions, interval):
    """Select the first six sessions back and forth using real X events."""
    if interval <= 0 or sessions < 2:
        return None, [], [], []
    window = wait_for_window(app.pid, env)
    x, y = window_origin(window, env)
    # Xvfb intentionally has no window manager, so focus the client directly.
    run_xdotool(env, "windowfocus", "--sync", window)
    # The sidebar begins at y=73. Its first group and session rows are 25px
    # high in the isolated GTK theme used by the profiling display.
    row_x = x + 100
    # The projection uses an extra heading in its singleton-workstream layout;
    # with multiple workstreams the first group's sessions begin one row earlier.
    singleton_group_offset = 25 if sessions <= 6 else 0
    first_row_y = y + 73 + 25 + singleton_group_offset + 12
    expected = []
    observed = []
    active_latency_us = []
    stop = False
    # Keep both targets away from the viewport edge so scrolling and theme
    # clipping cannot turn the benchmark into a geometry test.
    limit = min(5, sessions) - 1
    # Bare Xvfb can consume the first click while activating the client. Two
    # identical setup clicks establish a known selection before measurement.
    for _ in range(2):
        run_xdotool(env, "mousemove", row_x,
                    first_row_y + limit * 25)
        run_xdotool(env, "click", "--delay", 0, 1)
        time.sleep(0.1)

    def drive():
        nonlocal stop
        targets = (0, limit)
        attempt = 0
        time.sleep(interval)
        while not stop:
            next_index = targets[attempt % len(targets)]
            attempt += 1
            target = f"profile-codex-{next_index:03d}"
            target_title = f"Codex workload {next_index + 1:02d}"
            try:
                run_xdotool(env, "mousemove", row_x,
                            first_row_y + next_index * 25)
                expected.append(target)
                started = time.monotonic()
                run_xdotool(env, "click", "--delay", 0, 1)
            except (subprocess.SubprocessError, OSError):
                break
            deadline = started + 0.5
            actual = None
            title = ""
            while not stop and time.monotonic() < deadline:
                try:
                    title = run_xdotool(env, "getwindowname", window).stdout.strip()
                except (subprocess.SubprocessError, OSError):
                    break
                if title == target_title:
                    actual = target
                    active_latency_us.append((time.monotonic() - started) * 1_000_000)
                    break
                time.sleep(0.002)
            if actual is None:
                match = re.fullmatch(r"Codex workload (\d+)", title)
                if match:
                    actual = f"profile-codex-{int(match.group(1)) - 1:03d}"
            if stop and actual is None:
                expected.pop()
                break
            observed.append(actual)
            time.sleep(interval)

    worker = threading.Thread(target=drive, name="sidebar-latency-driver",
                              daemon=True)
    worker.start()

    def stop_driver():
        nonlocal stop
        stop = True
        worker.join(timeout=2)

    return stop_driver, expected, observed, active_latency_us


def percentile(values, percent):
    if not values:
        return None
    ordered = sorted(values)
    index = max(0, min(len(ordered) - 1,
                       int((percent / 100) * len(ordered) + 0.999999) - 1))
    return round(ordered[index] / 1000, 3)


def duration_summary(values):
    return {
        "count": len(values),
        "p50_ms": percentile(values, 50),
        "p95_ms": percentile(values, 95),
        "p99_ms": percentile(values, 99),
        "max_ms": round(max(values) / 1000, 3) if values else None,
    }


def write_fixture(config_file, sessions):
    config_file.write_text(
        "[sakura]\nless_questions=true\ndont_save=false\n"
        "sidebar_visible=true\nsidebar_width=300\n"
        "window_columns=120\nwindow_rows=42\n",
        encoding="utf-8",
    )
    session_file = Path(f"{config_file}.session")
    group_count = max(1, (sessions + 5) // 6)
    lines = [
        "[Session]", "version=3", f"group_count={group_count}",
        f"terminal_count={sessions}", "selected_terminal=0",
        "sidebar_visible=true", "sidebar_width=300", "active_group_id=group-00", "",
    ]
    for index in range(group_count):
        lines += [f"[Group{index}]", f"id=group-{index:02d}", "parent=root",
                  f"title=Workstream {index + 1}", ""]
    for index in range(sessions):
        workload_cwd = config_file.parent / "workloads" / f"session-{index:03d}"
        workload_cwd.mkdir(parents=True, exist_ok=True)
        lines += [
            f"[Terminal{index}]", f"parent=group-{index // 6:02d}",
            f"cwd={workload_cwd}", f"terminal_id=profile-codex-{index:03d}",
            "kind=codex", "title_set_by_user=true",
            f"codex_session_id=00000000-0000-0000-0000-{index:012d}",
            f"title=Codex workload {index + 1:02d}", "",
        ]
    session_file.write_text("\n".join(lines), encoding="utf-8")


def write_fake_codex(path):
    path.write_text(r'''#!/usr/bin/env python3
import configparser
import os
from pathlib import Path
import signal
import sys
import tempfile
import time

if len(sys.argv) > 1 and sys.argv[1] == "app-server":
    raise SystemExit(0)

running = True
def stop(signum, frame):
    global running
    running = False
signal.signal(signal.SIGTERM, stop)
signal.signal(signal.SIGHUP, stop)
signal.signal(signal.SIGINT, stop)

profile = os.environ.get("SAKURA_PROFILE_KIND", "mixed")
token = os.environ.get("SAKURA_CODEX_TAB_TOKEN") or f"worker-{os.getpid()}"
try:
    slot = int(Path.cwd().name.rsplit("-", 1)[-1])
except ValueError:
    slot = sum(token.encode())
active = profile == "active" or (profile == "mixed" and slot % 4 != 0)
tracking_dir = os.environ.get("SAKURA_CODEX_TRACKING_DIR")
stats_dir = os.environ.get("SAKURA_PROFILE_STATS_DIR")
session_id = "profile-" + token
bytes_emitted = 0

def publish_stats():
    if not stats_dir:
        return
    directory = Path(stats_dir)
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    target = directory / token
    temporary = target.with_suffix(".tmp")
    temporary.write_text(f"{int(active)} {bytes_emitted} {os.getpid()}\n")
    os.replace(temporary, target)

def track(event, state, turn=""):
    if not tracking_dir:
        return
    directory = Path(tracking_dir)
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    body = ("[tracking]\n" + f"session_id={session_id}\n" +
            f"event={event}\nstate={state}\n")
    if turn:
        body += f"turn_id={turn}\n"
    fd, temporary = tempfile.mkstemp(prefix=".profile.", dir=directory)
    with os.fdopen(fd, "w") as output:
        output.write(body)
    os.replace(temporary, directory / token)

track("SessionStart", "idle")
publish_stats()
sequence = 0
next_transition = time.monotonic() + 1.0 + (slot % 7) * 0.1
next_output = time.monotonic()
phase_running = active
if active:
    track("UserPromptSubmit", "running", "turn-0")
while running:
    now = time.monotonic()
    if profile == "mixed" and now >= next_transition:
        phase_running = not phase_running
        sequence += 1
        track("UserPromptSubmit" if phase_running else "Stop",
              "running" if phase_running else "idle", f"turn-{sequence}")
        next_transition = now + 1.2 + (slot % 9) * 0.13
    if phase_running and now >= next_output:
        # Cursor movement, color, erasure, Unicode, wrapped lines, and regular
        # text approximate the mixture handled by a Codex TUI and its logs.
        payload = (f"\x1b[38;5;{32 + slot % 180}m[{token}]\x1b[0m "
                   f"analysis chunk {sequence:06d} — inspecting src/sakura-workspace.c "
                   "and updating deterministic workload state\r\n"
                   "\x1b[2K  ✓ parsed 128 records; rendering sidebar and scrollback\r\n")
        try:
            os.write(sys.stdout.fileno(), payload.encode())
        except OSError:
            break
        bytes_emitted += len(payload.encode())
        if sequence % 20 == 0:
            publish_stats()
        sequence += 1
        next_output = now + 0.025 + (slot % 5) * 0.006
    time.sleep(0.005 if phase_running else 0.05)
''', encoding="utf-8")
    path.chmod(0o755)


def process_tree(root_pid):
    parents = {}
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            fields = (entry / "stat").read_text().split()
            parents[int(entry.name)] = int(fields[3])
        except (FileNotFoundError, PermissionError, ValueError, IndexError):
            pass
    result = {root_pid}
    changed = True
    while changed:
        changed = False
        for pid, parent in parents.items():
            if parent in result and pid not in result:
                result.add(pid)
                changed = True
    return result


def terminate_process_tree(process):
    if process is None:
        return
    pids = process_tree(process.pid) if process.poll() is None else {process.pid}
    if process.poll() is None:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)
    descendants = pids - {process.pid}
    for pid in descendants:
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
    deadline = time.monotonic() + 1
    while descendants and time.monotonic() < deadline:
        descendants = {pid for pid in descendants if Path(f"/proc/{pid}").exists()}
        if descendants:
            time.sleep(0.02)
    for pid in descendants:
        try:
            os.kill(pid, signal.SIGKILL)
        except ProcessLookupError:
            pass


def read_metrics(pids):
    totals = {"cpu_ticks": 0, "rss_bytes": 0, "read_bytes": 0,
              "write_bytes": 0, "read_syscalls": 0, "write_syscalls": 0,
              "voluntary_context_switches": 0,
              "involuntary_context_switches": 0}
    live = 0
    page_size = os.sysconf("SC_PAGE_SIZE")
    for pid in pids:
        root = Path("/proc") / str(pid)
        try:
            stat = (root / "stat").read_text().split()
            totals["cpu_ticks"] += int(stat[13]) + int(stat[14])
            totals["rss_bytes"] += int(stat[23]) * page_size
            io_values = {}
            for line in (root / "io").read_text().splitlines():
                key, value = line.split(":", 1)
                io_values[key] = int(value)
            totals["read_bytes"] += io_values.get("read_bytes", 0)
            totals["write_bytes"] += io_values.get("write_bytes", 0)
            totals["read_syscalls"] += io_values.get("syscr", 0)
            totals["write_syscalls"] += io_values.get("syscw", 0)
            for line in (root / "status").read_text().splitlines():
                if line.startswith("voluntary_ctxt_switches:"):
                    totals["voluntary_context_switches"] += int(line.split()[1])
                elif line.startswith("nonvoluntary_ctxt_switches:"):
                    totals["involuntary_context_switches"] += int(line.split()[1])
            live += 1
        except (FileNotFoundError, PermissionError, ValueError, IndexError):
            pass
    totals["processes"] = live
    return totals


def delta(after, before, key):
    return max(0, after.get(key, 0) - before.get(key, 0))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default="build-opt/src/sakura")
    parser.add_argument("--profile", choices=PROFILES, default="mixed")
    parser.add_argument("--sessions", type=int, default=24)
    parser.add_argument("--duration", type=float, default=20.0)
    parser.add_argument("--warmup", type=float, default=5.0)
    parser.add_argument("--sample-interval", type=float, default=0.25)
    parser.add_argument("--interaction-interval", type=float, default=0.075,
                        help="seconds between sidebar switches; 0 disables them")
    parser.add_argument("--max-active-p95-ms", type=float, default=50.0)
    parser.add_argument("--max-focus-p95-ms", type=float, default=25.0)
    parser.add_argument("--max-switch-failure-percent", type=float, default=1.0)
    parser.add_argument("--min-focus-sample-percent", type=float, default=98.0)
    parser.add_argument("--max-main-loop-stall-p95-ms", type=float, default=50.0)
    parser.add_argument("--max-main-loop-stalls-over-50", type=int, default=3)
    parser.add_argument("--max-paint-p95-ms", type=float, default=40.0)
    parser.add_argument("--max-frame-interval-p95-ms", type=float, default=30.0)
    parser.add_argument("--max-frame-intervals-over-50", type=int, default=3)
    parser.add_argument("--startup-runs", type=int, default=3,
                        help="isolated startup probes before steady-state profiling")
    parser.add_argument("--max-startup-window-ms", type=float, default=1000.0)
    parser.add_argument("--max-startup-terminal-ms", type=float, default=3000.0)
    parser.add_argument("--max-startup-convergence-ms", type=float, default=5000.0)
    parser.add_argument("--json", metavar="PATH", help="also write metrics as JSON")
    args = parser.parse_args()
    if (args.sessions < 1 or args.duration <= 0 or args.warmup < 0 or
            args.interaction_interval < 0 or args.startup_runs < 0):
        parser.error("sessions and duration must be positive; warmup cannot be negative")
    binary = Path(args.binary).resolve()
    if not binary.is_file():
        raise SystemExit(f"binary not found: {binary}")
    require("Xvfb")
    if args.interaction_interval > 0 or args.startup_runs > 0:
        require("xdotool")

    with tempfile.TemporaryDirectory(prefix="sakura-codex-profile-") as temporary:
        root = Path(temporary)
        config = root / "profile.conf"
        write_fixture(config, args.sessions)
        fake_bin = root / "bin"
        fake_bin.mkdir()
        write_fake_codex(fake_bin / "codex")
        env = os.environ.copy()
        env.update({
            "DISPLAY": "", "GDK_BACKEND": "x11", "GDK_CORE_DEVICE_EVENTS": "1",
            "PATH": f"{fake_bin}:{env.get('PATH', '')}",
            "SAKURA_PROFILE_KIND": args.profile,
            "SAKURA_PROFILE_STATS_DIR": str(root / "producer-stats"),
            "SAKURA_LATENCY_TRACE": "1",
            "CODEX_HOME": str(root / "codex-home"),
        })
        xvfb, env["DISPLAY"] = start_xvfb()
        startup_probes = [
            startup_probe(binary, root, config, env, args.sessions, index)
            for index in range(args.startup_runs)
        ]
        startup_report = startup_summary(startup_probes)
        # Each probe is allowed to persist its session. Restore the identical
        # fixture before the detailed steady-state run.
        write_fixture(config, args.sessions)
        log_path = root / "sakura.log"
        log = log_path.open("w", encoding="utf-8")
        app = subprocess.Popen([str(binary), "--config-file", config.name],
                               cwd=root, env=env, stdout=log,
                               stderr=subprocess.STDOUT)
        samples = []
        stop_interactions = None
        expected_interactions = []
        observed_interactions = []
        active_latency_us = []
        try:
            deadline = time.monotonic() + args.warmup
            while time.monotonic() < deadline and app.poll() is None:
                time.sleep(0.1)
            if app.poll() is not None:
                raise RuntimeError(f"Sakura exited during warmup with {app.returncode}")
            initial_pids = process_tree(app.pid)
            before = read_metrics(initial_pids)
            before_display = read_metrics({xvfb.pid})
            initial_worker_pids = set()
            for stats_file in (root / "producer-stats").glob("*"):
                try:
                    initial_worker_pids.add(int(stats_file.read_text().split()[2]))
                except (OSError, ValueError, IndexError):
                    pass
            before_workers = read_metrics(initial_worker_pids)
            (stop_interactions, expected_interactions, observed_interactions,
             active_latency_us) = start_interaction_workload(
                app, env, args.sessions, args.interaction_interval)
            log.flush()
            latency_log_offset = log.tell()
            start = time.monotonic()
            deadline = start + args.duration
            while time.monotonic() < deadline and app.poll() is None:
                pids = process_tree(app.pid)
                metric = read_metrics(pids)
                samples.append(metric)
                time.sleep(args.sample_interval)
            elapsed = time.monotonic() - start
            if stop_interactions is not None:
                stop_interactions()
                stop_interactions = None
            if app.poll() is not None:
                raise RuntimeError(f"Sakura exited during measurement with {app.returncode}")
            after = read_metrics(process_tree(app.pid))
            after_display = read_metrics({xvfb.pid})
            producer_stats = []
            for stats_file in (root / "producer-stats").glob("*"):
                try:
                    producer_stats.append(tuple(map(int, stats_file.read_text().split())))
                except (OSError, ValueError):
                    pass
            clock_ticks = os.sysconf("SC_CLK_TCK")
            cpu_seconds = delta(after, before, "cpu_ticks") / clock_ticks
            # Worker PIDs are part of the application process tree. Subtract
            # them to expose Sakura plus sakura-agent cost separately.
            worker_after = read_metrics(initial_worker_pids)
            worker_cpu_seconds = delta(worker_after, before_workers, "cpu_ticks") / clock_ticks
            pipeline_cpu_seconds = max(0.0, cpu_seconds - worker_cpu_seconds)
            display_cpu_seconds = delta(after_display, before_display, "cpu_ticks") / clock_ticks
            report = {
                "profile": args.profile, "sessions": args.sessions,
                "startup": startup_report,
                "duration_seconds": round(elapsed, 3),
                "cpu_seconds": round(cpu_seconds, 3),
                "average_cpu_percent_one_core": round(cpu_seconds / elapsed * 100, 2),
                "sakura_agent_cpu_percent_one_core": round(
                    pipeline_cpu_seconds / elapsed * 100, 2),
                "producer_cpu_percent_one_core": round(
                    worker_cpu_seconds / elapsed * 100, 2),
                "xvfb_cpu_percent_one_core": round(
                    display_cpu_seconds / elapsed * 100, 2),
                "average_rss_mib": round(sum(s["rss_bytes"] for s in samples) /
                                         max(1, len(samples)) / 1048576, 2),
                "peak_rss_mib": round(max((s["rss_bytes"] for s in samples), default=0) /
                                      1048576, 2),
                "peak_processes": max((s["processes"] for s in samples), default=0),
                "workload_workers": len(producer_stats),
                "active_workload_workers": sum(row[0] for row in producer_stats),
                "workload_output_bytes": sum(row[1] for row in producer_stats),
            }
            log.flush()
            with log_path.open(encoding="utf-8", errors="replace") as latency_log:
                latency_log.seek(latency_log_offset)
                trace_contents = latency_log.read()
            latency_events = LATENCY_PATTERN.findall(trace_contents)
            stall_events = STALL_PATTERN.findall(trace_contents)
            activity_events = ACTIVITY_PATTERN.findall(trace_contents)
            background_activity_events = BACKGROUND_ACTIVITY_PATTERN.findall(
                trace_contents)
            paint_latency_events = PAINT_LATENCY_PATTERN.findall(trace_contents)
            frame_interval_us = [
                int(value) for value in FRAME_INTERVAL_PATTERN.findall(trace_contents)
            ]
            if expected_interactions:
                latency_events = latency_events[-len(expected_interactions):]
            latency_us = [int(value) for value, _ in latency_events]
            if expected_interactions:
                paint_latency_events = paint_latency_events[-len(expected_interactions):]
            paint_latency_us = [int(value) for value, _ in paint_latency_events]
            stall_us = [int(value) for value, _, _ in stall_events]
            stall_causes = {}
            for value, cause, age in stall_events:
                cause_rows = stall_causes.setdefault(
                    cause, {"count": 0, "max_ms": 0.0,
                            "max_cause_age_ms": 0.0})
                cause_rows["count"] += 1
                cause_rows["max_ms"] = max(cause_rows["max_ms"],
                                             int(value) / 1000)
                if int(age) >= 0:
                    cause_rows["max_cause_age_ms"] = max(
                        cause_rows["max_cause_age_ms"], int(age) / 1000)
            activity_by_cause = {}
            for value, cause in activity_events:
                activity_by_cause.setdefault(cause, []).append(int(value))
            background_activity_by_cause = {}
            for value, cause in background_activity_events:
                background_activity_by_cause.setdefault(cause, []).append(int(value))
            report.update({
                "sidebar_switch_attempts": len(expected_interactions),
                "sidebar_switch_samples": len(latency_us),
                "sidebar_switch_missed": observed_interactions.count(None),
                "sidebar_switch_wrong_terminal": sum(
                    actual is not None and expected != actual
                    for expected, actual in zip(expected_interactions,
                                                observed_interactions)),
                "sidebar_active_latency_ms_p50": percentile(active_latency_us, 50),
                "sidebar_active_latency_ms_p95": percentile(active_latency_us, 95),
                "sidebar_active_latency_ms_p99": percentile(active_latency_us, 99),
                "sidebar_active_latency_ms_max": (
                    round(max(active_latency_us) / 1000, 3)
                    if active_latency_us else None),
                "sidebar_focus_latency_ms_p50": percentile(latency_us, 50),
                "sidebar_focus_latency_ms_p95": percentile(latency_us, 95),
                "sidebar_focus_latency_ms_p99": percentile(latency_us, 99),
                "sidebar_focus_latency_ms_max": (
                    round(max(latency_us) / 1000, 3) if latency_us else None),
                "sidebar_paint_samples": len(paint_latency_us),
                "sidebar_paint_latency_ms_p50": percentile(paint_latency_us, 50),
                "sidebar_paint_latency_ms_p95": percentile(paint_latency_us, 95),
                "sidebar_paint_latency_ms_p99": percentile(paint_latency_us, 99),
                "sidebar_paint_latency_ms_max": (
                    round(max(paint_latency_us) / 1000, 3)
                    if paint_latency_us else None),
                "frame_samples": len(frame_interval_us),
                "frame_interval_ms_p50": percentile(frame_interval_us, 50),
                "frame_interval_ms_p95": percentile(frame_interval_us, 95),
                "frame_interval_ms_p99": percentile(frame_interval_us, 99),
                "frame_interval_ms_max": (
                    round(max(frame_interval_us) / 1000, 3)
                    if frame_interval_us else None),
                "frame_intervals_over_16_7ms": sum(
                    value > 16700 for value in frame_interval_us),
                "frame_intervals_over_25ms": sum(
                    value > 25000 for value in frame_interval_us),
                "frame_intervals_over_50ms": sum(
                    value > 50000 for value in frame_interval_us),
                "main_loop_stalls_over_16ms": len(stall_us),
                "main_loop_stalls_over_25ms": sum(value >= 25000 for value in stall_us),
                "main_loop_stalls_over_50ms": sum(value >= 50000 for value in stall_us),
                "main_loop_stall_ms_p50": percentile(stall_us, 50),
                "main_loop_stall_ms_p95": percentile(stall_us, 95),
                "main_loop_stall_ms_p99": percentile(stall_us, 99),
                "main_loop_stall_ms_max": (
                    round(max(stall_us) / 1000, 3) if stall_us else None),
                "main_loop_stall_causes": stall_causes,
                "ui_activity_durations": {
                    cause: duration_summary(values)
                    for cause, values in sorted(activity_by_cause.items())
                },
                "background_activity_durations": {
                    cause: duration_summary(values)
                    for cause, values in sorted(
                        background_activity_by_cause.items())
                },
            })
            failures = [
                {"expected": expected, "actual": actual}
                for expected, actual in zip(expected_interactions,
                                            observed_interactions)
                if expected != actual
            ]
            if failures:
                report["sidebar_switch_failure_examples"] = failures[:5]
            if report["sidebar_switch_wrong_terminal"] or report["sidebar_switch_missed"]:
                report["sidebar_focus_terminal_sample"] = [
                    terminal for _, terminal in latency_events[:10]
                ]
            for key in ("read_bytes", "write_bytes", "read_syscalls", "write_syscalls",
                        "voluntary_context_switches", "involuntary_context_switches"):
                report[key] = delta(after, before, key)
            if args.profile != "idle" and report["workload_output_bytes"] == 0:
                raise RuntimeError("active Codex workload produced no output")
            print(json.dumps(report, indent=2, sort_keys=True))
            if args.json:
                Path(args.json).write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                                           encoding="utf-8")
            if args.interaction_interval > 0:
                errors = []
                attempts = report["sidebar_switch_attempts"]
                if attempts == 0:
                    errors.append("no sidebar switches were attempted")
                else:
                    failures_count = (report["sidebar_switch_missed"] +
                                      report["sidebar_switch_wrong_terminal"])
                    failure_percent = failures_count / attempts * 100
                    sample_percent = report["sidebar_switch_samples"] / attempts * 100
                    if failure_percent > args.max_switch_failure_percent:
                        errors.append(
                            f"switch failure rate {failure_percent:.2f}% exceeds "
                            f"{args.max_switch_failure_percent:.2f}%")
                    if sample_percent < args.min_focus_sample_percent:
                        errors.append(
                            f"focus sample coverage {sample_percent:.2f}% is below "
                            f"{args.min_focus_sample_percent:.2f}%")
                    paint_sample_percent = report["sidebar_paint_samples"] / attempts * 100
                    if paint_sample_percent < args.min_focus_sample_percent:
                        errors.append(
                            f"paint sample coverage {paint_sample_percent:.2f}% is below "
                            f"{args.min_focus_sample_percent:.2f}%")
                active_p95 = report["sidebar_active_latency_ms_p95"]
                focus_p95 = report["sidebar_focus_latency_ms_p95"]
                if active_p95 is None or active_p95 > args.max_active_p95_ms:
                    errors.append(
                        f"active p95 {active_p95}ms exceeds {args.max_active_p95_ms}ms")
                if focus_p95 is None or focus_p95 > args.max_focus_p95_ms:
                    errors.append(
                        f"focus p95 {focus_p95}ms exceeds {args.max_focus_p95_ms}ms")
                paint_p95 = report["sidebar_paint_latency_ms_p95"]
                if paint_p95 is None or paint_p95 > args.max_paint_p95_ms:
                    errors.append(
                        f"paint p95 {paint_p95}ms exceeds {args.max_paint_p95_ms}ms")
                frame_p95 = report["frame_interval_ms_p95"]
                if (frame_p95 is None or
                        frame_p95 > args.max_frame_interval_p95_ms):
                    errors.append(
                        f"frame interval p95 {frame_p95}ms exceeds "
                        f"{args.max_frame_interval_p95_ms}ms")
                frames_over_50 = report["frame_intervals_over_50ms"]
                if frames_over_50 > args.max_frame_intervals_over_50:
                    errors.append(
                        f"{frames_over_50} frame intervals over 50ms exceeds "
                        f"{args.max_frame_intervals_over_50}")
                stall_p95 = report["main_loop_stall_ms_p95"]
                if (stall_p95 is not None and
                        stall_p95 > args.max_main_loop_stall_p95_ms):
                    errors.append(
                        f"main-loop stall p95 {stall_p95}ms exceeds "
                        f"{args.max_main_loop_stall_p95_ms}ms")
                stalls_over_50 = report["main_loop_stalls_over_50ms"]
                if stalls_over_50 > args.max_main_loop_stalls_over_50:
                    errors.append(
                        f"{stalls_over_50} main-loop stalls over 50ms exceeds "
                        f"{args.max_main_loop_stalls_over_50}")
                if errors:
                    raise RuntimeError("interaction benchmark failed: " + "; ".join(errors))
            if startup_probes:
                startup_errors = []
                cold = startup_report["cold"]
                if cold["window_visible_ms"] > args.max_startup_window_ms:
                    startup_errors.append("cold window visibility exceeds threshold")
                if cold["terminal_ready_ms"] > args.max_startup_terminal_ms:
                    startup_errors.append("cold terminal readiness exceeds threshold")
                if cold["codex_converged_ms"] > args.max_startup_convergence_ms:
                    startup_errors.append("cold Codex convergence exceeds threshold")
                warm = startup_report["warm"]
                if warm:
                    if warm["window_visible_ms"]["p95_ms"] > args.max_startup_window_ms:
                        startup_errors.append("warm window visibility p95 exceeds threshold")
                    if warm["terminal_ready_ms"]["p95_ms"] > args.max_startup_terminal_ms:
                        startup_errors.append("warm terminal readiness p95 exceeds threshold")
                    if warm["codex_converged_ms"]["p95_ms"] > args.max_startup_convergence_ms:
                        startup_errors.append("warm Codex convergence p95 exceeds threshold")
                if startup_errors:
                    raise RuntimeError("startup benchmark failed: " + "; ".join(startup_errors))
        finally:
            if stop_interactions is not None:
                stop_interactions()
            terminate_process_tree(app)
            log.close()
            if xvfb.poll() is None:
                xvfb.terminate()
                try:
                    xvfb.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    xvfb.kill()


if __name__ == "__main__":
    main()
