#!/usr/bin/env python3
"""Stress Sakura's sidebar/session persistence through real GTK launches.

The test intentionally uses a private config directory.  It seeds a nested
workspace, exercises task actions, moves terminals between groups with xdotool,
closes Sakura through the window manager, and restores the result repeatedly.
"""

import argparse
import configparser
import os
from pathlib import Path
import random
import shlex
import signal
import subprocess
import sys
import tempfile
import time

try:
    from Xlib import X as X11
    from Xlib import display as x11_display
    from Xlib.protocol import event as x11_event
except ImportError:
    X11 = None
    x11_display = None
    x11_event = None


GROUPS = [
    ("group-a", "root", "Alpha"),
    ("group-b", "group-a", "Beta"),
    ("group-c", "root", "Gamma"),
    ("group-d", "group-c", "Delta"),
    ("group-e", "root", "Epsilon"),
]

TERMINAL_PARENTS = [
    "group-a", "group-b", "group-b", "group-c", "group-d", "group-d",
    "group-e", "group-e", "root", "root", "group-a", "group-c",
]
TERMINAL_CWDS = [
    "/tmp", "/var", "/usr", "/etc", "/usr/bin", "/home",
    os.path.expanduser("~"), "/", "/tmp", "/var", "/usr", "/etc",
]
TERMINAL_TITLES = [f"Stress terminal {index:02d}"
                   for index in range(len(TERMINAL_PARENTS))]
CODEX_TERMINAL_INDEX = 3
CODEX_SESSION_ID = "stress-codex-session"
SELECTED_TERMINAL_INDEX = 3
CURRENT_SESSION_VERSION = 11


def workspace_state_file(session_file):
    return Path(f"{session_file}.workspace")


def reset_workspace_state(session_file):
    workspace_file = workspace_state_file(session_file)
    workspace_file.unlink(missing_ok=True)
    Path(f"{workspace_file}.bak").unlink(missing_ok=True)


def read_authority(session_file):
    parser = configparser.ConfigParser(interpolation=None)
    workspace_file = workspace_state_file(session_file)
    parser.read(workspace_file if workspace_file.is_file() else session_file,
                encoding="utf-8")
    return parser


def run(command, env, timeout=5, check=True):
    return subprocess.run(command, env=env, text=True, capture_output=True,
                          timeout=timeout, check=check)


def write_fixture(config_file, session_file, active_group_id=None):
    reset_workspace_state(session_file)
    config_file.write_text(
        "[sakura]\n"
        "less_questions=true\n"
        "dont_save=false\n"
        "sidebar_visible=true\n"
        "sidebar_width=300\n"
        "window_columns=80\n"
        "window_rows=40\n",
        encoding="utf-8",
    )

    lines = [
        "[Session]",
        "version=3",
        f"group_count={len(GROUPS)}",
        f"terminal_count={len(TERMINAL_PARENTS)}",
        f"selected_terminal={SELECTED_TERMINAL_INDEX}",
        "sidebar_visible=true",
        "sidebar_width=300",
    ]
    if active_group_id is not None:
        lines.append(f"active_group_id={active_group_id}")
    lines.append("")
    for index, (group_id, parent, title) in enumerate(GROUPS):
        lines.extend([
            f"[Group{index}]",
            f"id={group_id}",
            f"parent={parent}",
            f"title={title}",
            "",
        ])
    for index, parent in enumerate(TERMINAL_PARENTS):
        lines.extend([
            f"[Terminal{index}]",
            f"parent={parent}",
            f"cwd={TERMINAL_CWDS[index]}",
            f"terminal_id=stress-terminal-{index:02d}",
            f"kind={'codex' if index == CODEX_TERMINAL_INDEX else 'shell'}",
            "title_set_by_user=true",
            f"title={TERMINAL_TITLES[index]}",
        ])
        if index == CODEX_TERMINAL_INDEX:
            lines.append(f"codex_session_id={CODEX_SESSION_ID}")
        lines.append("")
    session_file.write_text("\n".join(lines), encoding="utf-8")


def write_pane_switch_fixture(config_file, session_file):
    reset_workspace_state(session_file)
    config_file.write_text(
        "[sakura]\nless_questions=true\ndont_save=false\nsidebar_visible=true\n",
        encoding="utf-8",
    )
    session_file.write_text(
        """[Session]
version=4
group_count=0
terminal_count=3
page_count=2
layout_count=4
selected_terminal=2
selected_terminal_id=switch-terminal-c
selected_page_id=switch-page-2
active_group_id=root

[Page0]
id=switch-page-1
parent=root
title=Split page
title_set_by_user=true
root_layout=switch-root
active_terminal_id=switch-terminal-b

[Page1]
id=switch-page-2
parent=root
title=Other page
title_set_by_user=true
root_layout=switch-leaf-c
active_terminal_id=switch-terminal-c

[Layout0]
id=switch-root
page=switch-page-1
type=split
direction=right
ratio=0.5
first=switch-leaf-a
second=switch-leaf-b

[Layout1]
id=switch-leaf-a
page=switch-page-1
type=leaf
terminal_id=switch-terminal-a

[Layout2]
id=switch-leaf-b
page=switch-page-1
type=leaf
terminal_id=switch-terminal-b

[Layout3]
id=switch-leaf-c
page=switch-page-2
type=leaf
terminal_id=switch-terminal-c

[Terminal0]
parent=root
cwd=/tmp
terminal_id=switch-terminal-a
kind=shell
title_set_by_user=true
title=Pane A

[Terminal1]
parent=root
cwd=/tmp
terminal_id=switch-terminal-b
kind=shell
title_set_by_user=true
title=Pane B

[Terminal2]
parent=root
cwd=/tmp
terminal_id=switch-terminal-c
kind=shell
title_set_by_user=true
title=Page C
""",
        encoding="utf-8",
    )


def write_group_close_fixture(config_file, session_file):
    reset_workspace_state(session_file)
    config_file.write_text(
        "[sakura]\nless_questions=true\ndont_save=false\n"
        "sidebar_visible=true\nsidebar_width=300\n",
        encoding="utf-8",
    )
    session_file.write_text(
        "[Session]\n"
        "version=3\n"
        "group_count=2\n"
        "terminal_count=3\n"
        "selected_terminal=2\n"
        "sidebar_visible=true\n"
        "sidebar_width=300\n"
        "active_group_id=group-a\n\n"
        "[Group0]\n"
        "id=group-a\n"
        "parent=root\n"
        "title=Alpha\n\n"
        "[Group1]\n"
        "id=group-b\n"
        "parent=root\n"
        "title=Beta\n\n"
        "[Terminal0]\n"
        "parent=group-a\n"
        "cwd=/tmp\n"
        "terminal_id=close-terminal-a\n"
        "kind=shell\n"
        "title_set_by_user=true\n"
        "title=Close A\n\n"
        "[Terminal1]\n"
        "parent=group-b\n"
        "cwd=/tmp\n"
        "terminal_id=close-terminal-b\n"
        "kind=shell\n"
        "title_set_by_user=true\n"
        "title=Close B\n\n"
        "[Terminal2]\n"
        "parent=group-a\n"
        "cwd=/tmp\n"
        "terminal_id=close-terminal-c\n"
        "kind=shell\n"
        "title_set_by_user=true\n"
        "title=Close C\n",
        encoding="utf-8",
    )


def write_task_fixture(config_file, session_file):
    reset_workspace_state(session_file)
    config_file.write_text(
        "[sakura]\nless_questions=true\ndont_save=false\n"
        "sidebar_visible=true\nsidebar_width=300\n",
        encoding="utf-8",
    )
    session_file.write_text(
        "[Session]\n"
        "version=5\n"
        "group_count=2\n"
        "task_count=3\n"
        "terminal_count=2\n"
        "page_count=2\n"
        "layout_count=2\n"
        "selected_terminal=0\n"
        "selected_terminal_id=task-terminal-a\n"
        "selected_page_id=task-page-a\n"
        "selected_task_id=task-a\n"
        "active_group_id=group-a\n"
        "sidebar_visible=true\n"
        "sidebar_width=300\n\n"
        "[Group0]\n"
        "id=group-a\n"
        "parent=root\n"
        "title=Alpha\n\n"
        "[Group1]\n"
        "id=group-b\n"
        "parent=root\n"
        "title=Beta\n\n"
        "[Task0]\n"
        "id=task-a\n"
        "parent=group-a\n"
        "group=group-a\n"
        "title=Task Alpha\n"
        "provider=local\n"
        "status=0\n\n"
        "[Task1]\n"
        "id=task-empty\n"
        "parent=group-a\n"
        "group=group-a\n"
        "title=Empty Task\n"
        "provider=local\n"
        "status=0\n\n"
        "[Task2]\n"
        "id=task-b\n"
        "parent=group-b\n"
        "group=group-b\n"
        "title=Task Beta\n"
        "provider=local\n"
        "status=0\n\n"
        "[Page0]\n"
        "id=task-page-a\n"
        "parent=task-a\n"
        "title=Task Alpha Page\n"
        "title_set_by_user=true\n"
        "root_layout=task-layout-a\n"
        "active_terminal_id=task-terminal-a\n"
        "task_id=task-a\n\n"
        "[Page1]\n"
        "id=task-page-b\n"
        "parent=task-b\n"
        "title=Task Beta Page\n"
        "title_set_by_user=true\n"
        "root_layout=task-layout-b\n"
        "active_terminal_id=task-terminal-b\n"
        "task_id=task-b\n\n"
        "[Layout0]\n"
        "id=task-layout-a\n"
        "page=task-page-a\n"
        "type=leaf\n"
        "terminal_id=task-terminal-a\n\n"
        "[Layout1]\n"
        "id=task-layout-b\n"
        "page=task-page-b\n"
        "type=leaf\n"
        "terminal_id=task-terminal-b\n\n"
        "[Terminal0]\n"
        "parent=task-a\n"
        "cwd=/tmp\n"
        "terminal_id=task-terminal-a\n"
        "kind=shell\n"
        "title_set_by_user=true\n"
        "title=Task Alpha Terminal\n\n"
        "[Terminal1]\n"
        "parent=task-b\n"
        "cwd=/tmp\n"
        "terminal_id=task-terminal-b\n"
        "kind=shell\n"
        "title_set_by_user=true\n"
        "title=Task Beta Terminal\n",
        encoding="utf-8",
    )


def write_named_page_move_fixture(config_file, session_file):
    """Minimal legacy fixture matching the reported app-qt-free move."""
    reset_workspace_state(session_file)
    config_file.write_text(
        "[sakura]\nless_questions=true\ndont_save=false\n"
        "sidebar_visible=true\nsidebar_width=300\n",
        encoding="utf-8",
    )
    session_file.write_text(
        "[Session]\nversion=3\ngroup_count=2\nterminal_count=1\n"
        "selected_terminal=0\nactive_group_id=group-source\n"
        "sidebar_visible=true\nsidebar_width=300\n\n"
        "[Group0]\nid=group-source\nparent=root\norder=0\n"
        "title=Source\n\n"
        "[Group1]\nid=group-qt-free\nparent=root\norder=1\n"
        "title=qt-free\n\n"
        "[Terminal0]\nparent=group-source\ncwd=/tmp\n"
        "terminal_id=app-qt-free\nkind=shell\n"
        "title_set_by_user=true\ntitle=app-qt-free\n",
        encoding="utf-8",
    )


def read_session(session_file):
    parser = configparser.ConfigParser(interpolation=None)
    parser.read(session_file, encoding="utf-8")
    if not parser.has_section("Session"):
        raise AssertionError("session has no [Session] section")
    if parser.getint("Session", "version") not in (3, 4, 5, 6, 7, 8,
                                                       CURRENT_SESSION_VERSION):
        raise AssertionError("unexpected session version")

    authority = parser
    workspace_file = Path(f"{session_file}.workspace")
    if workspace_file.is_file():
        authority = configparser.ConfigParser(interpolation=None)
        authority.read(workspace_file, encoding="utf-8")
        if not authority.has_section("Workspace"):
            raise AssertionError("agent snapshot has no [Workspace] revision")

    group_count = authority.getint("Session", "group_count")
    task_count = authority.getint("Session", "task_count", fallback=0)
    terminal_count = parser.getint("Session", "terminal_count")
    groups = {}
    for index in range(group_count):
        section = f"Group{index}"
        if not authority.has_section(section):
            raise AssertionError(f"missing {section}")
        group_id = authority.get(section, "id")
        if group_id in groups:
            raise AssertionError(f"duplicate group id {group_id}")
        groups[group_id] = authority.get(section, "parent")

    tasks = {}
    for index in range(task_count):
        section = f"Task{index}"
        if not authority.has_section(section):
            raise AssertionError(f"missing {section}")
        task_id = authority.get(section, "id")
        if task_id in groups or task_id in tasks:
            raise AssertionError(f"duplicate task id {task_id}")
        tasks[task_id] = authority.get(section, "parent")

    pages = {}
    page_count = authority.getint("Session", "page_count", fallback=0)
    for index in range(page_count):
        section = f"Page{index}"
        page_id = authority.get(section, "id")
        pages[page_id] = authority.get(section, "parent", fallback="root")

    terminals = []
    terminal_id_list = []
    terminal_ids = set()
    for index in range(terminal_count):
        section = f"Terminal{index}"
        if not parser.has_section(section):
            raise AssertionError(f"missing {section}")
        parent = parser.get(section, "parent")
        parent = pages.get(parent, parent)
        terminals.append(parent)
        if parser.has_option(section, "terminal_id"):
            terminal_id = parser.get(section, "terminal_id")
            if terminal_id in terminal_ids:
                raise AssertionError(f"duplicate terminal id {terminal_id}")
            terminal_ids.add(terminal_id)
            terminal_id_list.append(terminal_id)
        else:
            terminal_id_list.append(None)

    valid_parents = {"root", *groups, *tasks}
    for group_id, parent in groups.items():
        if parent not in valid_parents:
            raise AssertionError(f"group {group_id} points to missing {parent}")
    for task_id, parent in tasks.items():
        if parent not in valid_parents:
            raise AssertionError(f"task {task_id} points to missing {parent}")
    for index, parent in enumerate(terminals):
        if parent not in valid_parents:
            raise AssertionError(f"terminal {index} points to missing {parent}")

    # Verify that group nesting is acyclic.  A cycle would make the next
    # sidebar restore insert rows under groups that are not yet reachable.
    for group_id in groups:
        seen = set()
        current = group_id
        while current != "root":
            if current in seen:
                raise AssertionError(f"cycle in group parents at {group_id}")
            seen.add(current)
            current = groups[current]

    return groups, terminals, terminal_id_list


def read_expanded_sidebar(session_file):
    parser = configparser.ConfigParser(interpolation=None)
    parser.read(session_file, encoding="utf-8")
    count = parser.getint("Session", "expanded_sidebar_count", fallback=-1)
    if count < 0:
        return None
    expanded = set()
    for index in range(count):
        section = f"ExpandedSidebar{index}"
        if not parser.has_section(section):
            raise AssertionError(f"missing {section}")
        kind = parser.get(section, "kind")
        item_id = parser.get(section, "id")
        if kind not in ("group", "task", "session") or not item_id:
            raise AssertionError(f"invalid expanded sidebar record in {section}")
        expanded.add((kind, item_id))
    return expanded


def sidebar_expansion_is(session_file, expected):
    actual = read_expanded_sidebar(session_file)
    if (actual != expected and
            os.environ.get("SAKURA_STRESS_VERBOSE")):
        print(f"sidebar expansion is {actual}, expected {expected}",
              file=sys.stderr)
    return actual == expected


def read_task_state(session_file):
    parser = configparser.ConfigParser(interpolation=None)
    workspace_file = Path(f"{session_file}.workspace")
    parser.read(workspace_file if workspace_file.is_file() else session_file,
                encoding="utf-8")
    # Selection remains desktop presentation state even when task records come
    # from the agent snapshot.
    desktop = configparser.ConfigParser(interpolation=None)
    desktop.read(session_file, encoding="utf-8")
    task_count = parser.getint("Session", "task_count", fallback=0)
    tasks = {}
    for index in range(task_count):
        section = f"Task{index}"
        task_id = parser.get(section, "id")
        tasks[task_id] = {
            "parent": parser.get(section, "parent", fallback="root"),
            "group": parser.get(section, "group", fallback="root"),
            "title": parser.get(section, "title", fallback=""),
            "status": parser.getint(section, "status", fallback=0),
            "archived": parser.getboolean(section, "archived", fallback=False),
        }
    return (
        tasks,
        desktop.get("Desktop", "selected_task_id", fallback=
                    parser.get("Session", "selected_task_id", fallback="")),
        desktop.get("Desktop", "active_group_id", fallback=
                    parser.get("Session", "active_group_id", fallback="root")),
    )


def read_metadata(session_file):
    parser = configparser.ConfigParser(interpolation=None)
    parser.read(session_file, encoding="utf-8")
    count = parser.getint("Session", "terminal_count")
    records = {}
    ordered_ids = []
    for index in range(count):
        section = f"Terminal{index}"
        terminal_id = parser.get(section, "terminal_id")
        ordered_ids.append(terminal_id)
        records[terminal_id] = {
            "cwd": parser.get(section, "cwd", fallback=""),
            "title": parser.get(section, "title", fallback=""),
            "title_set_by_user": parser.getboolean(section, "title_set_by_user",
                                                     fallback=False),
            "kind": parser.get(section, "kind", fallback="shell"),
            "codex_session_id": parser.get(section, "codex_session_id", fallback=""),
        }
    selected_index = parser.getint("Session", "selected_terminal", fallback=-1)
    selected_id = (ordered_ids[selected_index]
                   if 0 <= selected_index < len(ordered_ids) else None)
    return records, selected_id


def read_active_group_id(session_file):
    parser = configparser.ConfigParser(interpolation=None)
    parser.read(session_file, encoding="utf-8")
    if parser.has_option("Desktop", "active_group_id"):
        return parser.get("Desktop", "active_group_id")
    return parser.get("Session", "active_group_id", fallback="root")


def read_group_orders(session_file):
    parser = read_authority(session_file)
    count = parser.getint("Session", "group_count", fallback=0)
    orders = {}
    for index in range(count):
        section = f"Group{index}"
        group_id = parser.get(section, "id")
        orders[group_id] = parser.getint(section, "order", fallback=index)
    return orders


def read_task_orders(session_file):
    parser = read_authority(session_file)
    count = parser.getint("Session", "task_count", fallback=0)
    orders = {}
    for index in range(count):
        section = f"Task{index}"
        task_id = parser.get(section, "id")
        orders[task_id] = parser.getint(section, "order", fallback=index)
    return orders


def assert_no_stale_gobject_pointer_criticals(log_file):
    """Catch known Sakura lifecycle warnings without flagging Xvfb teardown noise."""
    if not log_file.is_file():
        return
    text = log_file.read_text(encoding="utf-8", errors="replace")
    signatures = (
        "invalid unclassed pointer in cast to 'GObject'",
        "instance with invalid (NULL) class pointer",
        "g_signal_handler_is_connected: assertion",
        "Could not create terminal history file",
        "Workspace invariant failed after mutation",
    )
    critical_lines = [line for line in text.splitlines()
                      if any(signature in line for signature in signatures)]
    if critical_lines:
        raise AssertionError(
            "Sakura emitted GLib-GObject criticals:\n" +
            "\n".join(critical_lines[-8:]))


def expected_metadata():
    records = {}
    for index, terminal_id in enumerate(
            f"stress-terminal-{index:02d}" for index in range(len(TERMINAL_PARENTS))):
        records[terminal_id] = {
            "cwd": os.path.realpath(TERMINAL_CWDS[index]),
            "title": TERMINAL_TITLES[index],
            "title_set_by_user": True,
            "kind": "codex" if index == CODEX_TERMINAL_INDEX else "shell",
            "codex_session_id": CODEX_SESSION_ID if index == CODEX_TERMINAL_INDEX else "",
        }
    return records


def assert_metadata(session_file, expected, selected_id=None):
    actual, actual_selected_id = read_metadata(session_file)
    comparable_actual = {key: dict(value) for key, value in actual.items()}
    comparable_expected = {key: dict(value) for key, value in expected.items()}
    # A resumed Codex session may report its authoritative cwd asynchronously;
    # the persistence invariant here is its identity, kind, and user metadata.
    for terminal_id, record in comparable_actual.items():
        if record["kind"] == "codex" and terminal_id in comparable_expected:
            record.pop("cwd", None)
            comparable_expected[terminal_id].pop("cwd", None)
    if comparable_actual != comparable_expected:
        raise AssertionError(
            f"terminal metadata changed: expected {comparable_expected}, "
            f"got {comparable_actual}"
        )
    if selected_id is not None and actual_selected_id != selected_id:
        raise AssertionError(
            f"selected terminal changed: expected {selected_id}, got {actual_selected_id}"
        )


def visible_rows(groups, terminals):
    children = {"root": []}
    for group_id, parent in groups.items():
        children.setdefault(parent, []).append(group_id)
        children.setdefault(group_id, [])

    rows = [("group", "root")]

    def visit(group_id):
        for child in children.get(group_id, []):
            rows.append(("group", child))
            visit(child)
        for index, parent in enumerate(terminals):
            if parent == group_id:
                # A single-pane page is represented by its page row. The
                # underlying terminal remains the drag target for metadata
                # and session assertions.
                rows.append(("page", index))

    visit("root")
    return rows


def visible_rows_for_expansion(groups, terminals, expanded, terminal_ids=None):
    children = {"root": []}
    for group_id, parent in groups.items():
        children.setdefault(parent, []).append(group_id)
        children.setdefault(group_id, [])

    rows = [("group", "root")]

    def visit(group_id):
        if ("group", group_id) not in expanded:
            return
        for child in children.get(group_id, []):
            rows.append(("group", child))
            visit(child)
        for index, parent in enumerate(terminals):
            if parent == group_id:
                rows.append(("page", index))

    visit("root")
    return [
        (kind, (terminal_ids[item] if terminal_ids is not None else item)
         if kind == "page" else item)
        for kind, item in rows
    ]


def start_xvfb():
    read_fd, write_fd = os.pipe()
    process = subprocess.Popen(
        ["Xvfb", "-displayfd", str(write_fd), "-screen", "0", "1600x1000x24",
         "-nolisten", "tcp"],
        pass_fds=(write_fd,), stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
        text=True,
    )
    os.close(write_fd)
    try:
        display = os.read(read_fd, 32).decode("ascii").strip()
    finally:
        os.close(read_fd)
    if not display:
        error = process.stderr.read() if process.stderr else ""
        process.terminate()
        raise RuntimeError(f"Xvfb did not provide a display: {error}")
    return process, f":{display}"


def find_window(env, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = run(["xdotool", "search", "--onlyvisible", "--class", "sakura"],
                     env, timeout=2, check=False)
        windows = [line for line in result.stdout.splitlines() if line.strip()]
        if windows:
            # Sakura also creates a small fade window with the same class.
            # Select the largest visible class window, which is the terminal.
            candidates = []
            for window in windows:
                geometry = run(["xdotool", "getwindowgeometry", "--shell", window],
                               env, timeout=2, check=False).stdout
                values = {}
                for line in geometry.splitlines():
                    if "=" in line:
                        key, value = line.split("=", 1)
                        try:
                            values[key] = int(value)
                        except ValueError:
                            pass
                candidates.append((values.get("WIDTH", 0) * values.get("HEIGHT", 0),
                                   window))
            return max(candidates)[1]
        time.sleep(0.1)
    window_ids = run(["xdotool", "search", "--onlyvisible", "--name", ".*"],
                     env, timeout=2, check=False).stdout.splitlines()
    names = []
    for window_id in window_ids:
        name = run(["xdotool", "getwindowname", window_id], env,
                   timeout=2, check=False).stdout.strip()
        names.append(f"{window_id}={name}")
    raise RuntimeError("Sakura window did not appear; visible windows: "
                       f"{', '.join(names) or '(none)'}")


def window_geometry(window, env):
    result = run(["xdotool", "getwindowgeometry", "--shell", window], env)
    values = {}
    for line in result.stdout.splitlines():
        key, value = line.split("=", 1)
        values[key] = int(value)
    return values["X"], values["Y"], values["WIDTH"], values["HEIGHT"]


def wait_for_session(session_file, predicate=None, timeout=10):
    deadline = time.monotonic() + timeout
    last_error = None
    last_value = None
    while time.monotonic() < deadline:
        try:
            value = read_session(session_file)
            last_value = value
            if predicate is None or predicate(value):
                return value
        except (AssertionError, configparser.Error, OSError) as error:
            last_error = error
        time.sleep(0.1)
    if last_error:
        raise AssertionError(f"session did not become valid: {last_error}")
    raise AssertionError(f"session did not reach the expected state; last value: {last_value}")


def wait_for_session_ready(session_file, predicate=None, timeout=10):
    def ready(value):
        return session_has_current_version(session_file) and \
            (predicate is None or predicate(value))

    return wait_for_session(session_file, ready, timeout)


def wait_for_task_state(session_file, predicate, timeout=10):
    deadline = time.monotonic() + timeout
    last_error = None
    last_value = None
    while time.monotonic() < deadline:
        try:
            value = read_task_state(session_file)
            last_value = value
            if predicate(value):
                return value
        except (AssertionError, configparser.Error, OSError) as error:
            last_error = error
        time.sleep(0.1)
    if last_error:
        raise AssertionError(f"task state did not become valid: {last_error}")
    raise AssertionError(
        f"task state did not reach the expected state; last value: {last_value}"
    )


def wait_for_task_state_ready(session_file, predicate, timeout=10):
    return wait_for_task_state(
        session_file,
        lambda value: session_has_current_version(session_file) and predicate(value),
        timeout,
    )


def sidebar_row_center(window, env, rows, row_index, row_top=73,
                       group_row_height=25, terminal_row_height=25,
                       task_row_height=41):
    window_x, window_y, _, _ = window_geometry(window, env)
    y = row_top
    for row_kind, _ in rows[:row_index]:
        if row_kind == "group":
            y += group_row_height
        elif row_kind == "task":
            y += task_row_height
        else:
            y += terminal_row_height
    row_kind = rows[row_index][0]
    if row_kind == "group":
        height = group_row_height
    elif row_kind == "task":
        height = task_row_height
    else:
        height = terminal_row_height
    return window_x + 100, window_y + y + height // 2


def sidebar_scroll_to_top(window, env, row_top=73):
    window_x, window_y, _, _ = window_geometry(window, env)
    run(["xdotool", "mousemove", "--sync", str(window_x + 80),
         str(window_y + row_top + 10)], env)
    run(["xdotool", "click", "--repeat", "30", "--delay", "10", "4"], env)


def sidebar_click_row(window, env, rows, row_index, button):
    sidebar_scroll_to_top(window, env)
    x, y = sidebar_row_center(window, env, rows, row_index)
    # A submenu activation can leave a transient GTK menu grab alive for one
    # event-loop turn. Escape makes the next pointer action deterministic.
    run(["xdotool", "key", "Escape"], env, check=False)
    run(["xdotool", "mousemove", str(x), str(y)], env)
    time.sleep(0.15)
    run(["xdotool", "click", str(button)], env)
    time.sleep(0.25)


def sidebar_toggle_group(window, env, rows, row_index):
    sidebar_scroll_to_top(window, env)
    x, y = sidebar_row_center(window, env, rows, row_index)
    run(["xdotool", "key", "Escape"], env, check=False)
    run(["xdotool", "mousemove", "--sync", str(x), str(y)], env)
    time.sleep(0.15)
    run(["xdotool", "click", "--repeat", "2", "--delay", "80", "1"], env)
    time.sleep(0.4)


def sidebar_click_expander(window, env, rows, row_index, depth=1):
    """Click the GTK tree expander, before the first cell renderer."""
    sidebar_scroll_to_top(window, env)
    window_x, window_y, _, _ = window_geometry(window, env)
    _, y = sidebar_row_center(window, env, rows, row_index)
    # GTK's first-column cell begins after the expander; each tree level adds
    # the theme's 18px indentation. Top-level groups are one level below the
    # synthetic root row.
    x = window_x + 28 + max(0, depth - 1) * 18
    run(["xdotool", "key", "Escape"], env, check=False)
    run(["xdotool", "mousemove", "--sync", str(x), str(y)], env)
    time.sleep(0.15)
    run(["xdotool", "click", "1"], env)
    time.sleep(0.4)


def open_task_context_menu(window, env, rows, row_index):
    sidebar_click_row(window, env, rows, row_index, 3)
    run(["xdotool", "key", "Home"], env)


def activate_task_context_item(window, env, rows, row_index, down_count):
    """Open a task row menu and activate an item by its stable menu order."""
    open_task_context_menu(window, env, rows, row_index)
    for _ in range(down_count):
        run(["xdotool", "key", "Down"], env)
    run(["xdotool", "key", "Return"], env)
    time.sleep(0.35)


def session_has_terminal_ids(session_file):
    parser = configparser.ConfigParser(interpolation=None)
    parser.read(session_file, encoding="utf-8")
    count = parser.getint("Session", "terminal_count", fallback=0)
    return all(parser.has_option(f"Terminal{index}", "terminal_id")
               and parser.get(f"Terminal{index}", "terminal_id")
               for index in range(count))


def session_has_current_version(session_file):
    parser = configparser.ConfigParser(interpolation=None)
    parser.read(session_file, encoding="utf-8")
    return parser.getint("Session", "version", fallback=0) == CURRENT_SESSION_VERSION


def drag_sidebar_row(window, rows, source_row, target_row, env, row_top,
                     group_row_height, terminal_row_height, sidebar_width,
                     target_edge=None):
    window_x, window_y, _, _ = window_geometry(window, env)
    # Selection changes can scroll the tree to the active terminal.  Return to
    # the top so our model-to-pixel mapping is deterministic.
    run(["xdotool", "mousemove", "--sync", str(window_x + 80),
         str(window_y + row_top + 10)], env)
    run(["xdotool", "click", "--repeat", "30", "--delay", "10", "4"], env)
    if os.environ.get("SAKURA_STRESS_SCREENSHOT"):
        run(["import", "-display", env["DISPLAY"], "-window", window,
             os.environ["SAKURA_STRESS_SCREENSHOT"]], env, timeout=5)
    source_x = window_x + min(sidebar_width - 30, 100)
    target_x = window_x + min(sidebar_width - 30, 120)

    def row_y(row):
        y = row_top
        for preceding in rows[:row[1]]:
            y += group_row_height if preceding[0] == "group" else terminal_row_height
        height = group_row_height if row[0] == "group" else terminal_row_height
        return window_y + y + height // 2

    source_y = row_y(source_row)
    target_y = row_y(target_row)
    if target_edge == "before":
        target_y -= (group_row_height if target_row[0] == "group"
                     else terminal_row_height) // 2 - 3
    elif target_edge == "after":
        target_y += (group_row_height if target_row[0] == "group"
                     else terminal_row_height) // 2 - 3
    if os.environ.get("SAKURA_STRESS_VERBOSE"):
        print(f"drag rows {source_row[1]} -> {target_row[1]} at "
              f"({source_x},{source_y}) -> ({target_x},{target_y})")

    run(["xdotool", "mousemove", "--sync", str(source_x), str(source_y)], env)
    run(["xdotool", "mousedown", "1"], env)
    time.sleep(0.25)
    for fraction in (0.2, 0.4, 0.6, 0.8, 1.0):
        x = round(source_x + (target_x - source_x) * fraction)
        y = round(source_y + (target_y - source_y) * fraction)
        run(["xdotool", "mousemove", "--sync", str(x), str(y)], env)
        time.sleep(0.08)
    time.sleep(0.25)
    run(["xdotool", "mouseup", "1"], env)


def drag_terminal_into_group(window, rows, source_row, target_row, env, row_top,
                             group_row_height, terminal_row_height, sidebar_width):
    drag_sidebar_row(window, rows, source_row, target_row, env, row_top,
                     group_row_height, terminal_row_height, sidebar_width)


def terminal_parent(value, terminal_id):
    for index, candidate in enumerate(value[2]):
        if candidate == terminal_id:
            return value[1][index]
    raise AssertionError(f"terminal {terminal_id} is missing from the session")


def visual_rows_for_session(value):
    return [
        (kind, value[2][item] if kind == "page" else item)
        for kind, item in visible_rows(value[0], value[1])
    ]


def run_sidebar_expansion_case(binary, config_file, session_file, env, log_file):
    """Exercise group toggles, selection, projection restore, and restart."""
    write_fixture(config_file, session_file, active_group_id="group-c")
    process, window = launch_sakura(binary, config_file, env, log_file)
    collapsed_group = ("group", "group-a")
    try:
        def initial_expansion_ready(value):
            expanded = read_expanded_sidebar(session_file)
            return expanded is not None and (
                ("group", "root") in expanded and
                collapsed_group in expanded
            )

        current = wait_for_session_ready(
            session_file,
            initial_expansion_ready,
        )
        all_expanded = read_expanded_sidebar(session_file)
        collapsed = all_expanded - {collapsed_group}
        groups, terminals, _ = current
        rows = visible_rows_for_expansion(groups, terminals, all_expanded)
        sidebar_toggle_group(window, env, rows, rows.index(collapsed_group))
        wait_for_session_ready(
            session_file,
            lambda value: sidebar_expansion_is(session_file, collapsed),
        )

        # Selection of another visible group must not rebuild the tree into an
        # all-expanded state as a side effect.
        current = read_session(session_file)
        rows = visible_rows_for_expansion(current[0], current[1], collapsed)
        sidebar_click_row(window, env, rows, rows.index(("group", "group-c")), 1)
        wait_for_session_ready(
            session_file,
            lambda value: sidebar_expansion_is(session_file, collapsed) and
            read_active_group_id(session_file) == "group-c",
        )

        # A clean restart must preserve the same stable-ID expansion state.
        close_window(process, window, env)
        process = None
        window = None
        process, window = launch_sakura(binary, config_file, env, log_file)
        wait_for_session_ready(
            session_file,
            lambda value: sidebar_expansion_is(session_file, collapsed),
        )

        current = read_session(session_file)
        rows = visible_rows_for_expansion(current[0], current[1], collapsed)
        sidebar_toggle_group(window, env, rows, rows.index(collapsed_group))
        wait_for_session_ready(
            session_file,
            lambda value: sidebar_expansion_is(session_file, all_expanded),
        )
    finally:
        if process is not None:
            close_window(process, window, env)


def run_named_page_move_restart_case(binary, config_file, session_file,
                                     env, log_file):
    """Move app-qt-free into qt-free and assert agent state after restart."""
    write_named_page_move_fixture(config_file, session_file)
    process, window = launch_sakura(binary, config_file, env, log_file)
    source_id = "app-qt-free"
    try:
        current = wait_for_session_ready(
            session_file,
            lambda value: terminal_parent(value, source_id) == "group-source",
        )
        rows = visual_rows_for_session(current)
        drag_terminal_into_group(
            window, rows,
            ("page", rows.index(("page", source_id))),
            ("group", rows.index(("group", "group-qt-free"))),
            env, 73, 25, 25, 300,
        )
        moved = wait_for_session(
            session_file,
            lambda value: terminal_parent(value, source_id) == "group-qt-free",
        )
        workspace_file = workspace_state_file(session_file)
        if not workspace_file.is_file():
            raise AssertionError("agent workspace snapshot was not created")
        authority = read_authority(session_file)
        revision = authority.getint("Workspace", "revision", fallback=0)
        if revision < 1:
            raise AssertionError("page move did not advance agent revision")

        close_window(process, window, env)
        process = None
        window = None
        process, window = launch_sakura(binary, config_file, env, log_file)
        restored = wait_for_session_ready(
            session_file,
            lambda value: terminal_parent(value, source_id) == "group-qt-free",
        )
        if restored[0] != moved[0]:
            raise AssertionError("agent group hierarchy changed after restart")
        if read_authority(session_file).getint(
                "Workspace", "revision", fallback=0) < revision:
            raise AssertionError("agent revision regressed after restart")
    finally:
        if process is not None:
            close_window(process, window, env)


def run_expander_click_case(binary, config_file, session_file, env, log_file):
    """Verify the actual GTK expander hit target toggles reliably."""
    write_fixture(config_file, session_file)
    process, window = launch_sakura(binary, config_file, env, log_file)
    try:
        current = wait_for_session_ready(
            session_file,
            lambda value: ("group", "group-a") in
            (read_expanded_sidebar(session_file) or set()),
        )
        rows = visual_rows_for_session(current)
        group_row = rows.index(("group", "group-a"))
        sidebar_click_expander(window, env, rows, group_row, depth=1)
        wait_for_session_ready(
            session_file,
            lambda value: ("group", "group-a") not in
            (read_expanded_sidebar(session_file) or set()),
        )
        collapsed = read_session(session_file)
        sidebar_click_expander(
            window, env, visual_rows_for_session(collapsed), group_row, depth=1)
        wait_for_session_ready(
            session_file,
            lambda value: ("group", "group-a") in
            (read_expanded_sidebar(session_file) or set()),
        )
    finally:
        close_window(process, window, env)


def run_drag_regression_case(binary, config_file, session_file, env, log_file):
    """Exercise nested-group page moves and verify scope after each drag."""
    write_fixture(config_file, session_file, active_group_id="group-c")
    process, window = launch_sakura(binary, config_file, env, log_file)
    # The fixture's restored terminal geometry is intentionally compact. Give
    # the drag regression enough vertical room that source and destination
    # rows can be exercised without relying on pointer auto-scroll.
    run(["xdotool", "windowsize", window, "1000", "800"], env)
    time.sleep(0.3)
    source_id = "stress-terminal-01"
    # One real pointer move is sufficient to exercise the mutation boundary.
    # Reusing session-file row order for subsequent drags is invalid because
    # GTK appends a moved row locally while persistence canonicalizes panes by
    # group. Group and task reorders are exercised separately below.
    steps = [("group-b", "group-a")]
    try:
        current = wait_for_session_ready(
            session_file,
            lambda value: terminal_parent(value, source_id) == "group-b",
        )
        expected_groups = dict(current[0])
        visual_rows = visual_rows_for_session(current)
        collapsed_groups = set()
        for expected_source, target_group in steps:
            if expected_source == "group-c":
                # Keep the source row on screen for the upward drag. The
                # nested Delta group and Beta subtree are unrelated to this
                # move, so collapsing them makes the deterministic pointer
                # path fit the small test viewport while retaining the real
                # nested-group hierarchy. Beta itself remains visible as the
                # destination row.
                if "group-d" not in collapsed_groups:
                    expanded_rows = visual_rows_for_session(current)
                    sidebar_toggle_group(
                        window, env, expanded_rows,
                        expanded_rows.index(("group", "group-d")),
                    )
                    wait_for_session(
                        session_file,
                        lambda value: ("group", "group-d") not in
                        (read_expanded_sidebar(session_file) or set()),
                    )
                    collapsed_groups.add("group-d")
                if "group-b" not in collapsed_groups:
                    current = read_session(session_file)
                    expanded_rows = visual_rows_for_session(current)
                    sidebar_toggle_group(
                        window, env, expanded_rows,
                        expanded_rows.index(("group", "group-b")),
                    )
                    wait_for_session(
                        session_file,
                        lambda value: ("group", "group-b") not in
                        (read_expanded_sidebar(session_file) or set()),
                    )
                    collapsed_groups.add("group-b")
                current = read_session(session_file)
                expanded = {("group", "root")}
                expanded.update(("group", group_id)
                                for group_id in current[0]
                                if group_id not in collapsed_groups)
                visual_rows = visible_rows_for_expansion(
                    current[0], current[1], expanded, current[2])
                run(["xdotool", "windowsize", window, "1000", "800"], env)
                time.sleep(0.3)
            actual_source = terminal_parent(current, source_id)
            if actual_source != expected_source:
                raise AssertionError(
                    f"drag precondition failed: {source_id} is in {actual_source}, "
                    f"expected {expected_source}"
                )

            source_row = ("page", visual_rows.index(("page", source_id)))
            target_row = ("group", visual_rows.index(("group", target_group)))
            drag_terminal_into_group(
                window, visual_rows, source_row, target_row, env, 73, 25, 25, 300,
            )

            current = wait_for_session(
                session_file,
                lambda value: terminal_parent(value, source_id) == target_group,
            )
            if current[0] != expected_groups:
                raise AssertionError("group model changed during page drag")
            if target_group == "group-b" and "group-b" in collapsed_groups:
                # The final move lands in Beta, which was collapsed only to
                # keep its destination row on screen. Reopen it before the
                # selection assertion so the moved page row is available.
                expanded_rows = visual_rows_for_session(current)
                sidebar_toggle_group(
                    window, env, expanded_rows,
                    expanded_rows.index(("group", "group-b")),
                )
                wait_for_session(
                    session_file,
                    lambda value: ("group", "group-b") in
                    (read_expanded_sidebar(session_file) or set()),
                )
                collapsed_groups.remove("group-b")
            # The agent snapshot is the canonical sibling order. Rebuild the
            # row map so coordinates follow the stable projection.
            if collapsed_groups:
                expanded = {("group", "root")}
                expanded.update(("group", group_id)
                                for group_id in current[0]
                                if group_id not in collapsed_groups)
                visual_rows = visible_rows_for_expansion(
                    current[0], current[1], expanded, current[2])
            else:
                visual_rows = visual_rows_for_session(current)

            # Selecting the moved page must make its owning group the active
            # scope; otherwise the tab bar can retain the previous group.
            sidebar_click_row(window, env, visual_rows,
                              visual_rows.index(("page", source_id)), 1)
            wait_for_session(
                session_file,
                lambda value: terminal_parent(value, source_id) == target_group and
                read_active_group_id(session_file) == target_group and
                read_metadata(session_file)[1] == source_id,
            )

        # Reorder two top-level groups through the sidebar's explicit reorder
        # handler, keeping both groups under root while exercising persisted
        # sibling order. Collapse unrelated subtrees first so both rows are
        # visible in the small process-test window; this keeps the pointer
        # geometry deterministic without changing the hierarchy under test.
        current = read_session(session_file)
        expanded = read_expanded_sidebar(session_file) or set()
        rows = visible_rows_for_expansion(
            current[0], current[1], expanded, current[2])
        sidebar_toggle_group(
            window, env, rows, rows.index(("group", "group-a")))
        wait_for_session(
            session_file,
            lambda value: ("group", "group-a") not in
            (read_expanded_sidebar(session_file) or set()),
        )
        current = read_session(session_file)
        expanded = read_expanded_sidebar(session_file) or set()
        rows = visible_rows_for_expansion(
            current[0], current[1], expanded, current[2])
        sidebar_toggle_group(
            window, env, rows, rows.index(("group", "group-c")))
        wait_for_session(
            session_file,
            lambda value: ("group", "group-c") not in
            (read_expanded_sidebar(session_file) or set()),
        )
        current = read_session(session_file)
        expanded = read_expanded_sidebar(session_file) or set()
        visual_rows = visible_rows_for_expansion(
            current[0], current[1], expanded, current[2])
        # Terminal attach/resize callbacks may restore Sakura's compact
        # geometry while the preceding moves settle. Reapply the test window
        # size immediately before the group drag so the destination row is
        # mapped when GTK receives the drop.
        run(["xdotool", "windowsize", window, "1000", "800"], env)
        time.sleep(0.3)
        source_row = ("group", visual_rows.index(("group", "group-c")))
        target_row = ("group", visual_rows.index(("group", "group-e")))
        initial_group_orders = read_group_orders(session_file)
        drag_sidebar_row(
            window, visual_rows, source_row, target_row, env, 73, 25, 25, 300,
            target_edge="after",
        )
        current = wait_for_session(
            session_file,
            lambda value: terminal_parent(value, source_id) == "group-a" and
            read_group_orders(session_file) != initial_group_orders,
        )
        final_group_orders = read_group_orders(session_file)

        close_window(process, window, env)
        process = None
        restored = read_session(session_file)
        if terminal_parent(restored, source_id) != "group-a":
            raise AssertionError("final dragged parent was not persisted")
        if read_group_orders(session_file) != final_group_orders:
            raise AssertionError("group reorder was not persisted")

        # A second launch proves the persisted parent survives restore, not just
        # the in-memory projection.
        process, window = launch_sakura(binary, config_file, env, log_file)
        restored = wait_for_session_ready(
            session_file,
            lambda value: terminal_parent(value, source_id) == "group-a",
        )
        if restored[0].get("group-b") != "group-a":
            raise AssertionError("nested group parent changed during drag restore")
        if read_group_orders(session_file) != final_group_orders:
            raise AssertionError("group reorder changed during restore")
        close_window(process, window, env)
        process = None
        assert_no_stale_gobject_pointer_criticals(log_file)
    except Exception:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait(timeout=3)
        raise


def run_task_drag_reorder_case(binary, config_file, session_file, env, log_file):
    """Verify dragging a task preserves its page subtree and sibling order."""
    write_task_fixture(config_file, session_file)
    process, window = launch_sakura(binary, config_file, env, log_file)
    rows = [
        ("group", "root"),
        ("group", "group-a"),
        ("task", "task-a"),
        ("page", "task-page-a"),
        ("task", "task-empty"),
        ("group", "group-b"),
        ("task", "task-b"),
        ("page", "task-page-b"),
    ]
    try:
        wait_for_task_state_ready(
            session_file,
            lambda value: value[0]["task-a"]["parent"] == "group-a",
        )
        initial_orders = read_task_orders(session_file)
        drag_sidebar_row(
            window, rows, ("task", 2), ("group", 1), env, 73, 25, 41, 300,
        )
        wait_for_session(
            session_file,
            lambda value: value[1][value[2].index("task-terminal-a")] == "task-a" and
            read_task_orders(session_file) != initial_orders,
        )
        task_state = read_task_state(session_file)
        if task_state[0]["task-a"]["parent"] != "group-a":
            raise AssertionError("task reorder changed task ownership")

        close_window(process, window, env)
        process = None
        final_orders = read_task_orders(session_file)
        process, window = launch_sakura(binary, config_file, env, log_file)
        wait_for_task_state_ready(
            session_file,
            lambda value: value[0]["task-a"]["parent"] == "group-a" and
            read_task_orders(session_file) == final_orders,
        )
        close_window(process, window, env)
        process = None
        assert_no_stale_gobject_pointer_criticals(log_file)
    except Exception:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait(timeout=3)
        raise


def request_window_close(window, env):
    if x11_display is None:
        raise RuntimeError("python-xlib is required for graceful X11 close tests")
    run(["xdotool", "windowactivate", "--sync", window], env, timeout=5,
        check=False)
    display = x11_display.Display(env["DISPLAY"])
    window_object = display.create_resource_object("window", int(window, 0))
    wm_protocols = display.intern_atom("WM_PROTOCOLS")
    wm_delete_window = display.intern_atom("WM_DELETE_WINDOW")
    close_event = x11_event.ClientMessage(
        window=window_object,
        client_type=wm_protocols,
        data=(32, [wm_delete_window, X11.CurrentTime, 0, 0, 0]),
    )
    # Match the protocol a window manager uses for a graceful close. In
    # particular, do not call xdotool windowclose, which destroys the X
    # window directly and bypasses GTK's delete-event lifecycle.
    window_object.send_event(close_event, propagate=False)
    display.flush()
    display.close()


def close_window(process, window, env):
    if process.poll() is None:
        # Allow queued GTK focus/selection idles to finish before asking X11
        # to close the window. Closing while one of those callbacks is still
        # dispatching can produce a nondeterministic BadWindow in Xvfb.
        # Allow restored VTE spawn completions and the initial GTK projection
        # to settle. Instrumented builds can still be dispatching those
        # callbacks when the window first becomes discoverable; callers can
        # raise this without changing the normal test cadence.
        default_settle = "10.0" if os.environ.get("ASAN_OPTIONS") else "3.0"
        settle_seconds = float(os.environ.get("SAKURA_STRESS_STARTUP_SETTLE",
                                               default_settle))
        time.sleep(max(0.0, settle_seconds))
        request_window_close(window, env)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            try:
                request_window_close(window, env)
            except Exception:
                pass
            try:
                # Sanitizer-instrumented GTK/VTE teardown can take longer than
                # a normal close while child processes and async I/O unwind.
                process.wait(timeout=25)
            except subprocess.TimeoutExpired:
                process.send_signal(signal.SIGTERM)
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=3)
    if process.returncode not in (0, None):
        raise RuntimeError(f"Sakura exited with status {process.returncode}")


def launch_sakura(binary, config_file, env, log_file):
    log_handle = log_file.open("a", encoding="utf-8")
    # Keep this relative on purpose. Sakura resets its cwd to $HOME after
    # initialization, so this catches persistence paths that are accidentally
    # resolved again from the post-startup cwd.
    command = [str(binary), "--config-file", config_file.name]
    launcher = env.get("SAKURA_STRESS_LAUNCHER")
    if launcher:
        command = shlex.split(launcher) + command
        if env.get("SAKURA_STRESS_VERBOSE"):
            print("launch:", command, file=sys.stderr)
    process = subprocess.Popen(
        command, cwd=config_file.parent,
        env=env, stdout=log_handle, stderr=subprocess.STDOUT,
    )
    log_handle.close()
    return process, find_window(env)


def run_failed_restore_case(binary, config_file, session_file, env, log_file):
    invalid_session = (
        "[Session]\n"
        "version=3\n"
        "group_count=1\n"
        "terminal_count=0\n"
        "selected_terminal=0\n"
        "\n"
        "[Group0]\n"
        "id=group-preserved\n"
        "parent=root\n"
        "title=Preserved\n"
    )
    session_file.write_text(invalid_session, encoding="utf-8")
    process, window = launch_sakura(binary, config_file, env, log_file)
    time.sleep(0.5)
    close_window(process, window, env)
    if session_file.read_text(encoding="utf-8") != invalid_session:
        raise AssertionError("failed restore overwrote the original session file")


def run_pane_switch_case(binary, config_file, session_file, env, log_file):
    """Verify that real terminal key events change pages in both directions."""
    write_fixture(config_file, session_file)
    process, window = launch_sakura(binary, config_file, env, log_file)
    try:
        wait_for_session_ready(session_file,
                         lambda value: len(value[2]) == len(TERMINAL_PARENTS))

        run(["xdotool", "windowfocus", "--sync", window], env)
        window_x, window_y, _, height = window_geometry(window, env)
        run(["xdotool", "mousemove", "--sync", str(window_x + 500),
             str(window_y + height // 2), "click", "1"], env)

        original_title = TERMINAL_TITLES[SELECTED_TERMINAL_INDEX]
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            title = run(["xdotool", "getwindowname", window], env).stdout.strip()
            if title == original_title:
                break
            time.sleep(0.1)
        if title != original_title:
            raise AssertionError(f"restored terminal title is {title}, expected {original_title}")

        run(["xdotool", "key", "alt+Right"], env)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            title = run(["xdotool", "getwindowname", window], env).stdout.strip()
            if title != original_title:
                break
            time.sleep(0.1)
        if title == original_title:
            raise AssertionError("Alt+Right did not change the active terminal")

        process.terminate()
        process.wait(timeout=5)
    except Exception:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=3)
        raise


def run_create_delete_case(binary, config_file, session_file, env, log_file):
    """Exercise real new-page, split-pane, pane-close, and page-close mutations."""
    write_pane_switch_fixture(config_file, session_file)
    process, window = launch_sakura(binary, config_file, env, log_file)
    try:
        wait_for_session_ready(session_file, lambda value: len(value[2]) == 3)
        run(["xdotool", "windowfocus", "--sync", window], env)

        # Use a standalone page before the split/close sequence. This exercises
        # the mutation path where GTK changes notebook pages synchronously
        # while the switch-page callback is intentionally suppressed.
        run(["xdotool", "key", "ctrl+shift+t"], env)
        wait_for_session(session_file, lambda value: len(value[2]) == 4)
        run(["xdotool", "key", "ctrl+shift+w"], env)
        wait_for_session(session_file, lambda value: len(value[2]) == 3)

        run(["xdotool", "key", "ctrl+shift+e"], env)
        wait_for_session(session_file, lambda value: len(value[2]) == 4)

        # The split shortcut focuses the new pane. Pane close must remove only
        # that pane and preserve the containing notebook page.
        run(["xdotool", "key", "ctrl+shift+w"], env)
        wait_for_session(
            session_file,
            lambda value: len(value[2]) == 3 and
            set(value[2]) == {
                "switch-terminal-a", "switch-terminal-b", "switch-terminal-c"
            },
        )

        # Closing again removes the now single-pane page. This used to be a
        # common source of stale notebook/sidebar/session entries.
        run(["xdotool", "key", "ctrl+shift+w"], env)
        wait_for_session(
            session_file,
            lambda value: len(value[2]) == 2 and
            set(value[2]) == {"switch-terminal-a", "switch-terminal-b"},
        )
        close_window(process, window, env)
        process = None
        assert_no_stale_gobject_pointer_criticals(log_file)
    except Exception:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait(timeout=3)
        raise


def run_group_close_selection_case(binary, config_file, session_file, env, log_file):
    """Ensure closing an active page selects the next page in its group."""
    write_group_close_fixture(config_file, session_file)
    process, window = launch_sakura(binary, config_file, env, log_file)
    try:
        wait_for_session_ready(
            session_file,
            lambda value: value[1] == ["group-a", "group-a", "group-b"] and
            value[2] == ["close-terminal-a", "close-terminal-c", "close-terminal-b"],
        )
        run(["xdotool", "windowfocus", "--sync", window], env)

        # The physical next notebook page is group B. Group-aware fallback must
        # skip it and select the remaining page in group A instead.
        run(["xdotool", "key", "ctrl+shift+w"], env)
        wait_for_session(
            session_file,
            lambda value: value[1] == ["group-a", "group-b"] and
            value[2] == ["close-terminal-a", "close-terminal-b"],
        )
        _, selected_id = read_metadata(session_file)
        if selected_id != "close-terminal-a":
            raise AssertionError(
                f"closing group A selected {selected_id}, expected close-terminal-a"
            )

        close_window(process, window, env)
        process = None
    except Exception:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait(timeout=3)
        raise


def run_task_workflow_case(binary, config_file, session_file, env, log_file):
    """Exercise task context actions across groups and task archive fallback."""
    write_task_fixture(config_file, session_file)
    process, window = launch_sakura(binary, config_file, env, log_file)
    rows = [
        ("group", "root"),
        ("group", "group-a"),
        ("task", "task-a"),
        ("page", "task-page-a"),
        ("task", "task-empty"),
        ("group", "group-b"),
        ("task", "task-b"),
        ("page", "task-page-b"),
    ]
    try:
        wait_for_task_state_ready(
            session_file,
            lambda value: value[1] == "task-a" and value[2] == "group-a" and
            value[0]["task-b"]["status"] == 0,
        )
        run(["xdotool", "windowfocus", "--sync", window], env)

        # Set status on task B without first selecting its row. This exercises
        # the context action's cross-group context preparation.
        open_task_context_menu(window, env, rows, 6)
        for _ in range(5):
            run(["xdotool", "key", "Down"], env)
        run(["xdotool", "key", "Right"], env)
        run(["xdotool", "key", "End"], env)
        run(["xdotool", "key", "Return"], env)
        wait_for_task_state(
            session_file,
            lambda value: value[1] == "task-b" and value[2] == "group-b" and
            value[0]["task-b"]["status"] == 4,
        )
        _, selected_id = read_metadata(session_file)
        if selected_id != "task-terminal-b":
            raise AssertionError(
                f"status action selected {selected_id}, expected task-terminal-b"
            )

        # Start work on the same task through its direct context action.
        activate_task_context_item(window, env, rows, 6, 2)
        wait_for_task_state(
            session_file,
            lambda value: value[1] == "task-b" and value[2] == "group-b" and
            value[0]["task-b"]["status"] == 1,
        )

        # Rename the active task. The persisted title proves the dialog action
        # reached the task model; the metadata refresh keeps the scope label in
        # sync while this task remains active.
        activate_task_context_item(window, env, rows, 6, 6)
        run(["xdotool", "key", "ctrl+a"], env)
        run(["xdotool", "type", "--delay", "1", "Renamed Task Beta"], env)
        run(["xdotool", "key", "Return"], env)
        wait_for_task_state(
            session_file,
            lambda value: value[1] == "task-b" and value[2] == "group-b" and
            value[0]["task-b"]["title"] == "Renamed Task Beta",
        )

        # Select the empty task normally, then archive it from its context menu.
        # Archiving clears the task-filtered scope and must restore the first
        # terminal in group A rather than leaving a stale tab selected.
        sidebar_click_row(window, env, rows, 4, 1)
        wait_for_task_state(
            session_file,
            lambda value: value[1] == "task-empty" and value[2] == "group-a",
        )
        activate_task_context_item(window, env, rows, 4, 7)
        wait_for_task_state(
            session_file,
            lambda value: value[0].get("task-empty", {}).get("archived", False) and
            value[1] == "task-a" and value[2] == "group-a",
        )
        _, selected_id = read_metadata(session_file)
        if selected_id != "task-terminal-a":
            raise AssertionError(
                f"archiving empty task selected {selected_id}, expected task-terminal-a"
            )

        # Let the authoritative post-archive sidebar selection and tab refresh
        # finish before asking X11 to close the window.
        run(["xdotool", "key", "Escape"], env, check=False)
        time.sleep(1.0)
        close_window(process, window, env)
        process = None
    except Exception:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=3)
        raise


def run_visible_terminal_case(binary, config_file, session_file, env, log_file):
    """Verify sidebar selection focuses the VTE belonging to the selected page."""
    write_fixture(config_file, session_file)
    with config_file.open("a", encoding="utf-8") as handle:
        handle.write("new_tab_after_current=true\n")
    process, window = launch_sakura(binary, config_file, env, log_file)
    try:
        groups, terminals, _ = wait_for_session_ready(
            session_file,
            lambda value: len(value[2]) == len(TERMINAL_PARENTS),
        )
        rows = visible_rows(groups, terminals)
        window_x, window_y, _, _ = window_geometry(window, env)
        run(["xdotool", "windowfocus", "--sync", window], env)

        expected_restored_title = TERMINAL_TITLES[SELECTED_TERMINAL_INDEX]
        deadline = time.monotonic() + 5
        title = ""
        while time.monotonic() < deadline:
            title = run(["xdotool", "getwindowname", window], env).stdout.strip()
            if title == expected_restored_title:
                break
            time.sleep(0.1)
        if title != expected_restored_title:
            raise AssertionError(
                f"restored selection is {expected_restored_title}, but GTK displayed {title}"
            )

        # Select the ninth restored page, a shell whose saved cwd is /tmp,
        # then execute a command through the focused VTE. Logical selection
        # and window titles can change even when GtkNotebook retains an
        # invisible page; successful terminal input proves the selected page
        # is actually rendered and mapped.
        marker_file = config_file.parent / "rendered-terminal-cwd"
        marker_file.unlink(missing_ok=True)
        run(["xdotool", "mousemove", "--sync", str(window_x + 500),
             str(window_y + 400), "click", "1"], env)
        run(["xdotool", "key", "alt+9"], env)
        time.sleep(0.3)
        run(["xdotool", "type", "--delay", "1",
             f"pwd > {marker_file}"], env)
        run(["xdotool", "key", "Return"], env)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and not marker_file.exists():
            time.sleep(0.1)
        if not marker_file.exists():
            raise AssertionError("selected notebook page was not mapped to the input VTE")
        rendered_cwd = os.path.realpath(marker_file.read_text(encoding="utf-8").strip())
        expected_cwd = os.path.realpath(TERMINAL_CWDS[8])
        if rendered_cwd != expected_cwd:
            raise AssertionError(
                f"selected page rendered cwd {rendered_cwd}, expected {expected_cwd}"
            )

        # Exercise the actual split shortcut on the rendered shell. A missing
        # leaf-to-widget link used to make every first split fail with
        # "Cannot split the current terminal pane".
        before_count = len(read_session(session_file)[2])
        run(["xdotool", "key", "ctrl+shift+e"], env)
        deadline = time.monotonic() + 5
        after_count = before_count
        while time.monotonic() < deadline:
            after_count = len(read_session(session_file)[2])
            if after_count == before_count + 1:
                break
            time.sleep(0.1)
        if after_count != before_count + 1:
            raise AssertionError("splitting the rendered terminal did not create a pane")

        # Keep the targets near the top of the tree; these are the same rows
        # whose pixel geometry is exercised by the drag portion of this test.
        for terminal_index in (1,):
            row_index = rows.index(("page", terminal_index))
            row_y = 73
            for row in rows[:row_index]:
                row_y += 25 if row[0] == "group" else 25
            row_y -= 21

            run(["xdotool", "mousemove", "--sync", str(window_x + 80),
                 str(window_y + 83)], env)
            run(["xdotool", "click", "--repeat", "30", "--delay", "10", "4"], env)
            run(["xdotool", "mousemove", "--sync", str(window_x + 100),
                 str(window_y + row_y), "click", "1"], env)

            deadline = time.monotonic() + 5
            expected_title = TERMINAL_TITLES[terminal_index]
            title = ""
            while time.monotonic() < deadline:
                title = run(["xdotool", "getwindowname", window], env).stdout.strip()
                if title == expected_title:
                    break
                time.sleep(0.1)
            if title != expected_title:
                raise AssertionError(
                    f"sidebar selected {expected_title}, but GTK displayed {title}"
                )

        process.terminate()
        process.wait(timeout=5)
    except Exception:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=3)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default="build/src/sakura")
    parser.add_argument("--iterations", type=int, default=20)
    parser.add_argument("--seed", type=int, default=20260714)
    parser.add_argument("--row-top", type=int, default=73,
                        help="top of the first sidebar row in window coordinates")
    parser.add_argument("--group-row-height", type=int, default=25,
                        help="group row height used for xdotool drags")
    parser.add_argument("--terminal-row-height", type=int, default=25,
                        help="terminal row height used for xdotool drags")
    parser.add_argument("--max-drag-row", type=int, default=5,
                        help="largest visible row index used for automated drags")
    parser.add_argument("--no-drag", action="store_true",
                        help="only stress restore/save cycles")
    parser.add_argument("--drag-only", action="store_true",
                        help="run the deterministic nested-group drag regression")
    parser.add_argument("--expansion-only", action="store_true",
                        help="run the deterministic sidebar expansion regression")
    parser.add_argument("--page-move-restart-only", action="store_true",
                        help="run the app-qt-free to qt-free restart regression")
    parser.add_argument("--expander-only", action="store_true",
                        help="run the real GTK expander-arrow click regression")
    parser.add_argument("--screenshot", metavar="PATH",
                        help="capture the sidebar window before dragging")
    args = parser.parse_args()

    if args.iterations < 1:
        parser.error("--iterations must be positive")
    exclusive_modes = sum((args.drag_only, args.expansion_only,
                           args.page_move_restart_only, args.expander_only))
    if exclusive_modes > 1:
        parser.error("single-case regression modes are mutually exclusive")
    binary = Path(args.binary).resolve()
    if not binary.is_file():
        raise SystemExit(f"binary not found: {binary}")
    for command in ("Xvfb", "xdotool"):
        if subprocess.call(["which", command], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL) != 0:
            raise SystemExit(f"required command not found: {command}")

    randomizer = random.Random(args.seed)
    xvfb = None
    app = None
    window = None
    with tempfile.TemporaryDirectory(prefix="sakura-session-stress-") as directory:
        root = Path(directory)
        config_file = root / "sakura.conf"
        session_file = root / "sakura.conf.session"
        log_file = root / "sakura.log"
        write_fixture(config_file, session_file)

        xvfb, display = start_xvfb()
        env = os.environ.copy()
        env["DISPLAY"] = display
        env["GDK_BACKEND"] = "x11"
        # GTK's XI2 backend can query the pointer while xdotool is closing the
        # Xvfb window, producing a fatal BadWindow during test cleanup. Use
        # core device events for deterministic Xvfb shutdown; real desktop
        # launches retain the normal XI2 backend.
        env["GDK_CORE_DEVICE_EVENTS"] = "1"
        env["SHELL"] = "/bin/sh"
        fake_bin = root / "bin"
        fake_bin.mkdir()
        fake_codex = fake_bin / "codex"
        fake_codex.write_text(
            "#!/bin/sh\n"
            "if [ \"$1\" = app-server ]; then exit 0; fi\n"
            "trap 'exit 0' HUP INT TERM\n"
            "while :; do sleep 1; done\n",
            encoding="utf-8",
        )
        fake_codex.chmod(0o755)
        env["PATH"] = f"{fake_bin}:{env.get('PATH', '')}"

        fixture_groups, fixture_terminals, fixture_terminal_ids = read_session(session_file)
        metadata_expected = expected_metadata()
        expected_selected_id = f"stress-terminal-{SELECTED_TERMINAL_INDEX:02d}"
        expected_groups = fixture_groups
        expected_terminals = fixture_terminals
        expected_terminal_ids = fixture_terminal_ids
        try:
            if args.expansion_only:
                run_sidebar_expansion_case(binary, config_file, session_file,
                                           env, log_file)
                print("sidebar expansion regression: passed")
                return
            if args.page_move_restart_only:
                run_named_page_move_restart_case(
                    binary, config_file, session_file, env, log_file)
                print("app-qt-free page move/restart regression: passed")
                return
            if args.expander_only:
                run_expander_click_case(
                    binary, config_file, session_file, env, log_file)
                print("GTK expander-arrow regression: passed")
                return
            if args.drag_only:
                run_drag_regression_case(binary, config_file, session_file,
                                         env, log_file)
                run_task_drag_reorder_case(binary, config_file, session_file,
                                            env, log_file)
                print("deterministic drag regressions: passed")
                return
            for iteration in range(args.iterations):
                app, window = launch_sakura(binary, config_file, env, log_file)
                if args.screenshot:
                    run(["import", "-display", display, "-window", window,
                         args.screenshot], env, timeout=5)
                current = wait_for_session(
                    session_file,
                    lambda value: session_has_terminal_ids(session_file) and
                    session_has_current_version(session_file),
                )
                assert_metadata(session_file, metadata_expected, expected_selected_id)
                if (current[0] != fixture_groups or
                        len(current[1]) != len(fixture_terminals) or
                        set(current[2]) != set(fixture_terminal_ids)):
                    raise AssertionError(
                        "restore changed the workspace before drag at iteration "
                        f"{iteration}: expected group/terminal identities from fixture, got {current}"
                    )
                expected_groups, expected_terminals, expected_terminal_ids = current

                # Exercise one pointer mutation, then devote later iterations
                # to repeated close/restore of that exact authoritative state.
                # Re-dragging from the serialized terminal order is not a
                # stable GTK coordinate oracle after the first projection
                # rebuild; dedicated deterministic cases cover further drags.
                if not args.no_drag and iteration == 0:
                    rows = visible_rows(current[0], current[1])
                    terminal_choices = [(row, row_index)
                                        for row_index, row in enumerate(rows)
                                        if row[0] == "page" and
                                        row_index <= args.max_drag_row]
                    group_choices = [(row, row_index)
                                     for row_index, row in enumerate(rows)
                                     if row[0] == "group" and row[1] != "root" and
                                     row_index <= args.max_drag_row]
                    source, source_row_index = terminal_choices[
                        iteration % len(terminal_choices)]
                    source_index = source[1]
                    source_id = current[2][source_index]
                    targets = [(row, row_index) for row, row_index in group_choices
                               if current[1][source_index] != row[1]]
                    target, target_row_index = targets[randomizer.randrange(len(targets))]
                    target_index = target[1]

                    source_row = (source[0], source_row_index)
                    target_row = (target[0], target_row_index)
                    drag_terminal_into_group(
                        window, rows, source_row, target_row, env, args.row_top,
                        args.group_row_height, args.terminal_row_height, 300,
                    )

                    def moved(value):
                        for index, terminal_id in enumerate(value[2]):
                            if terminal_id == source_id:
                                return value[1][index] == target_index
                        return False

                    current = wait_for_session(session_file, moved)
                    assert_metadata(session_file, metadata_expected)
                    _, expected_selected_id = read_metadata(session_file)
                    expected_groups, expected_terminals, expected_terminal_ids = current

                close_window(app, window, env)
                app = None
                window = None
                assert_no_stale_gobject_pointer_criticals(log_file)
                restored = read_session(session_file)
                if restored != (expected_groups, expected_terminals, expected_terminal_ids):
                    raise AssertionError(
                        f"clean shutdown changed the workspace at iteration {iteration}"
                    )
                assert_metadata(session_file, metadata_expected, expected_selected_id)
                backup_file = Path(f"{session_file}.bak")
                if not backup_file.is_file():
                    raise AssertionError("session backup was not created")
                read_metadata(backup_file)
                print(f"iteration {iteration + 1}/{args.iterations}: "
                      f"{len(restored[0])} groups, {len(restored[1])} terminals")

            run_pane_switch_case(binary, config_file, session_file, env, log_file)
            print("terminal key switching: passed")
            run_create_delete_case(binary, config_file, session_file, env, log_file)
            print("create/delete lifecycle: passed")
            run_group_close_selection_case(binary, config_file, session_file, env, log_file)
            print("group-aware close selection: passed")
            run_sidebar_expansion_case(binary, config_file, session_file, env, log_file)
            print("sidebar expansion regression: passed")
            run_task_workflow_case(binary, config_file, session_file, env, log_file)
            print("task workflow actions: passed")
            run_visible_terminal_case(binary, config_file, session_file, env, log_file)
            print("restored sidebar-to-VTE mapping: passed")
            run_failed_restore_case(binary, config_file, session_file, env, log_file)
            print("failed-restore preservation: passed")
        except Exception:
            if app is not None:
                try:
                    close_window(app, window, env) if window else app.kill()
                except Exception as cleanup_error:
                    print(f"cleanup error: {cleanup_error}", file=sys.stderr)
            print(f"Sakura log: {log_file}", file=sys.stderr)
            if log_file.exists():
                print(log_file.read_text(encoding="utf-8", errors="replace")[-12000:],
                      file=sys.stderr)
            raise
        finally:
            if app is not None and app.poll() is None:
                app.kill()
            if xvfb is not None:
                xvfb.terminate()
                try:
                    xvfb.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    xvfb.kill()

    print("session stress test passed")


if __name__ == "__main__":
    main()
