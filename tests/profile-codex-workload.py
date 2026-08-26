#!/usr/bin/env python3
"""Profile Sakura with a deterministic, isolated Codex-like workload.

The profiler starts its own Xvfb server and uses a temporary Sakura config.
It never connects to, restarts, or writes the session of a running Sakura.
"""

import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time


PROFILES = ("idle", "active", "mixed")


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
    parser.add_argument("--json", metavar="PATH", help="also write metrics as JSON")
    args = parser.parse_args()
    if args.sessions < 1 or args.duration <= 0 or args.warmup < 0:
        parser.error("sessions and duration must be positive; warmup cannot be negative")
    binary = Path(args.binary).resolve()
    if not binary.is_file():
        raise SystemExit(f"binary not found: {binary}")
    require("Xvfb")

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
            "CODEX_HOME": str(root / "codex-home"),
        })
        xvfb, env["DISPLAY"] = start_xvfb()
        log = (root / "sakura.log").open("w", encoding="utf-8")
        app = subprocess.Popen([str(binary), "--config-file", config.name],
                               cwd=root, env=env, stdout=log,
                               stderr=subprocess.STDOUT)
        samples = []
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
            start = time.monotonic()
            deadline = start + args.duration
            while time.monotonic() < deadline and app.poll() is None:
                pids = process_tree(app.pid)
                metric = read_metrics(pids)
                samples.append(metric)
                time.sleep(args.sample_interval)
            elapsed = time.monotonic() - start
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
            for key in ("read_bytes", "write_bytes", "read_syscalls", "write_syscalls",
                        "voluntary_context_switches", "involuntary_context_switches"):
                report[key] = delta(after, before, key)
            if args.profile != "idle" and report["workload_output_bytes"] == 0:
                raise RuntimeError("active Codex workload produced no output")
            print(json.dumps(report, indent=2, sort_keys=True))
            if args.json:
                Path(args.json).write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                                           encoding="utf-8")
        finally:
            if app.poll() is None:
                app.send_signal(signal.SIGTERM)
                try:
                    app.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    app.kill()
                    app.wait(timeout=3)
            log.close()
            if xvfb.poll() is None:
                xvfb.terminate()
                try:
                    xvfb.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    xvfb.kill()


if __name__ == "__main__":
    main()
