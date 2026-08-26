[![Sponsor sakura!!](https://github.com/sponsors/dabisu/card)](https://github.com/sponsors/dabisu)

# sakura

**sakura** is a simple [gtk](http://www.gtk.org) and [vte](https://gitlab.gnome.org/GNOME/vte) based terminal emulator. It uses tabs to provide several terminals in one window and allows to change configuration options via a contextual menu. No more no less.

## Installation

The included installer builds Sakura and installs the executable, desktop
launcher, icon, documentation, and helper scripts:

```bash
./install.sh
```

The default prefix is `/usr/local`, so the installer may ask for your sudo
password. After installation, search for **Sakura** in the Linux Mint
application menu. The desktop database is refreshed automatically when the
system tool is available. The installer also installs the FiraCode Nerd Font
so gh-dash icons render correctly; select **FiraCode Nerd Font** in Sakura's
**Options → Select font...** menu. Set `SKIP_NERD_FONT=1` to skip that step.

If Sakura is launched while the saved workspace is already open, it asks
whether to exit or launch a separate persistent instance. The second instance
uses its own saved session file.

For a per-user install without sudo, use:

```bash
PREFIX="$HOME/.local" ./install.sh
```

Set `BUILD_TYPE=Debug` if you need debug symbols. The lower-level CMake
installation commands remain available for packaging and development.

Make sure your distribution sources the vte script for OSC 7 support in no login shells. If not, please add the following line to you .bashrc:

```bash
. /etc/profile.d/vte-2.91.sh
```

## Usage

**sakura** has several command line options. Run `sakura --help` for a full list.

### Codex sessions

Sakura can restore Codex conversations by their saved session ID. The installer
automatically enables the optional Codex hook when the `codex` command is
available. If Codex is installed later, enable it with:

```bash
scripts/sakura-codex-session-hook --install
```

If Codex asks you to review the new or changed hook, approve it with `/hooks`.

Start or resume Codex tabs from the terminal popup's **Codex** menu. Sakura
stores the Codex session ID with the workspace and reopens it with
`codex resume` after the next launch. New and resumed sessions launch Codex
with `--yolo` by default. Passing `--codex-unsafe-mode` additionally includes
Codex's explicit `--dangerously-bypass-approvals-and-sandbox` flag for
compatibility with existing launch scripts. Codex commands typed manually inside
ordinary Sakura shell tabs are tracked as well. The tracker records the ID at
session start and again when a prompt is submitted, covering interactive
session-picker flows. Existing shell tabs created before tracking was enabled
must be closed and reopened so they receive Sakura's tracking environment.

Sakura also reads the Codex session name through Codex's app-server interface.
Names set with Codex's `/rename` command are shown in the Sakura tree unless
the tree node has been manually renamed. If a session has no Codex name yet,
Sakura displays `Codex`.

### Terminal groups and scoped tabs

The sidebar is a workspace navigator. Select a group to scope the tab strip to
that group and its nested groups; select **All terminals** to return to the
whole workspace. Switching groups keeps each live terminal running and moves
the view to the last terminal available in that scope. New terminals follow the
current tab's group when **All terminals** is selected, or the active group when
a specific group is selected. An empty group offers a direct **New terminal**
action.

Groups can be nested by dragging a group onto another group. Dropping before or
after a group reorders it among that group's siblings.

Tasks and groups can be archived from their sidebar context menu instead of
being removed. Archiving preserves the hierarchy and any attached live
terminals; use **All terminals → Show archived** to reveal archived entries,
then choose **Restore** to bring them back. Archiving a task or group also
archives its nested children. Permanent deletion is available only for empty
archived entries and requires confirmation.

If a manually launched session is missed, use **Codex → Attach current tab...**
to associate its session ID with the current tab. **Codex → Check session
tracking** shows whether the current tab has the required environment. After
using `/rename` while staying in the same tab, use **Codex → Refresh session
name** to update the tree immediately. Use **Codex → Rename session...** to
rename the current Codex session from Sakura; the name is saved with the
workspace and remains the Codex session's persistent name.

### Tool tabs

Sakura gives interactive terminal tools their own reusable tabs. Open the
**Tools** button in the sidebar or the **Tools** submenu from a terminal or
sidebar context menu.

**GitUI** is opened at the current Git repository root. Sakura reuses the
existing GitUI tab for that repository, labels it with the repository name, and
restores it with the workspace. GitUI remains an optional dependency; Sakura
checks `PATH`, `$CARGO_HOME/bin`, and the default `~/.cargo/bin` install path.

**Git Cola** is opened at the current Git repository root and reused per
repository. It requires the system `git-cola` package.

**GitHub Dashboard** runs `gh dash` in the current working directory, so
gh-dash can use its normal `GH_DASH_CONFIG` or repository-local `.gh-dash.yml`
configuration. Sakura reuses one dashboard tab and restores it with the
workspace. Install the optional dependency with:

    gh extension install dlvhdr/gh-dash

**Open pull request...** accepts an HTTPS GitHub pull request URL and opens a
dedicated tab with `gh pr view <url>`. The URL is saved with the workspace and
the tab remains open after the details are displayed. When Sakura is built with
WebKitGTK 4.1 or 4.0 development files, the tab embeds the GitHub page with
back, forward, reload, and external-browser controls. Without WebKitGTK, Sakura
uses the terminal-based `gh pr view` fallback.
Install `libwebkit2gtk-4.1-dev` (or the 4.0 development package) before
building to enable the embedded view.

The **Open Here** button and menu open the current directory in the desktop
file manager or a detected graphical editor. Sakura checks `code`, `codium`,
`zed`, `subl`, `gnome-text-editor`, `gedit`, `xed`, `pluma`, `mousepad`, and
`kate` in that order. Set `SAKURA_EDITOR` or `editor_command` in
`~/.config/sakura/sakura.conf` to choose another command; `{directory}` can be
used as an explicit directory placeholder, otherwise Sakura appends the
directory to the command.

All integrated tool tabs are optional. If a tool is unavailable during session restoration,
Sakura restores that entry as a regular shell tab instead.

### Terminal history

Each Sakura tab has a stable ID in the workspace session file and a private
command-history file in the adjacent `sakura.conf.session.history` directory.
The file is reused when Sakura relaunches, so shell history stays associated
with the same terminal tab. Sakura exports its path as both `HISTFILE` and
`SAKURA_HISTORY_FILE`. Closing a tab explicitly removes its history file;
closing or restarting Sakura preserves it.

For Bash, Sakura automatically launches regular shell tabs through a private
startup layer. It sources the normal `~/.bashrc` first, then appends an
idempotent `history -a; history -n` prompt hook without modifying user shell
files. Other shells still receive `HISTFILE` and can use their native history
settings. Set `SAKURA_DISABLE_HISTORY_INTEGRATION=1` before launching Sakura
to disable the automatic Bash layer.

### Split panes

Use **Split Right** or **Split Down** from a terminal's context menu (or their
keybindings) to divide a notebook page into panes. The recursive pane tree,
split directions, divider positions, and active pane on each page are saved in
the session file and restored on the next launch. A regular split starts with
the existing pane at 60% and the new pane at 40%; the **Layout preset** menu
also provides balanced columns, rows, a 2 × 2 grid, and a main-plus-stack
layout.

### Session persistence stress test

With `Xvfb` and `xdotool` installed, the isolated session stress test can be
run from the build tree:

```bash
cmake --build build --target session-stress
```

It repeatedly drags terminals between nested groups, closes and restores the
workspace, checks CWD/title/selection/Codex metadata, validates session
backups, and verifies that a failed restore does not overwrite the original
session file.

For repeatable responsiveness measurements, build the optimized profiling
preset and run the isolated Codex-like workload:

```bash
cmake --preset opt
cmake --build --preset opt
python3 tests/profile-codex-workload.py --binary build-opt/src/sakura
```

The workload keeps 24 restored Codex sessions active while repeatedly
switching visible sidebar rows with real X11 pointer events. Its JSON report
includes click-to-active and selection-to-focus p50/p95/p99/max latency,
missed or incorrect switches, GTK main-loop stalls with recent-cause
attribution, timed UI activities, CPU, memory, I/O, and context-switch metrics.
The command fails if switch failures exceed 1%, focus-sample coverage falls
below 98%, active p95 exceeds 50 ms, focus p95 exceeds 25 ms, stall p95 exceeds
50 ms, or more than three stalls exceed 50 ms. Use `--interaction-interval 0`
for throughput-only profiling.

Tracked Codex tabs show their current state in the terminal sidebar: working,
needs approval, ready to review, interrupted, or error. Working tabs show an
animated spinner; completed, interrupted, and error states use stable status
symbols instead. When a background Codex turn finishes or needs approval,
Sakura marks that terminal as needing attention and updates the sidebar count.
Ordinary terminals use bell and process-exit signals as a fallback.

### Reproducible test presets

The repository includes CMake presets for the normal and sanitizer test
configurations. From a clean checkout:

```bash
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure

cmake --preset asan
cmake --build --preset asan --parallel
ctest --preset asan --output-on-failure
```

The sanitizer preset enables AddressSanitizer and UBSan for every build and
runs the core plus in-process GTK tests. It leaves out the process-level X11
smoke test because its external timing is inherently noisy under sanitizers;
the smoke test remains part of the normal debug preset. Leak detection is
disabled for sanitizer runs because GTK/VTE/font backends retain allocations
until process shutdown.

## Keybindings

**sakura** supports keyboard bindings in its config file (`~/.config/sakura/sakura.conf`), but there's no GUI to edit them, so please use your favourite editor to change the following values. Keybindings are a combination of an accelerator+key.

### Accelerators

Accelerators can be set to any _GdkModifierType_ mask value. The full list of _GdkModifierType_ values is available [here](http://gtk.php.net/manual/en/html/gdk/gdk.enum.modifiertype.html)

Mask values can be combined by ORing them. For example, to set the delete tab accelerator to Ctrl+Shift, change the option "del_tab_accelerator" value to "5". This number comes from ORing GDK_SHIFT_MASK and GDK_CONTROL_MASK.

I realise that this configuration is not user-friendly, but...  :-P

Quick reference: Shift(1), Cps-Lock(2), Ctrl(4), Alt(8), Ctrl-S(5), Ctrl-A(12), Ctrl-A-S(13)

### Keys

To change default keys, set the key value you want to modify to your desired key. For example, if you want to use the "D" key instead of the "W" key to delete a tab, set "del_tab_key" to "D" in the config file.

### Default keybindings

	Ctrl + Shift + T                 -> New tab
	Ctrl + Shift + O                 -> New window		
	Ctrl + Shift + W                 -> Close current tab
	Ctrl + Shift + C                 -> Copy selected text
	Ctrl + Shift + V                 -> Paste selected text
	Ctrl + Shift + N                 -> Set tab name

	Alt  + Left cursor               -> Previous tab
	Alt  + Right cursor              -> Next tab
	Alt  + Shift + Left cursor       -> Move tab to the left
	Alt  + Shift + Right cursor      -> Move tab to the right
	Alt  + [1-9]                     -> Switch to tab N (1-9)

	Ctrl + Shift + S                 -> Toggle/Untoggle scrollbar
	Ctrl + Mouse left button         -> Open link
	F11                              -> Fullscreen
	Shift + PageUp                   -> Move up through scrollback by page
	Shift + PageDown                 -> Move down through scrollback by page
	Ctrl + Shift + Up                -> Move up through scrollback by line
	Ctrl + Shift + Down              -> Move down through scrollback by line
	Ctrl + Shift + [F1-F6]           -> Select the colorset for the current tab

You can also increase and decrease the font size in the GTK standard way:

	Ctrl + '+'                                -> Increase font size
	Ctrl + '-'                                -> Decrease font size

By default, mouse buttons are bound to the following:

	Button1                          -> No action
	Button2                          -> Paste
	Button3                          -> Context menu

Behavior can be changed with the following config settings:

	copy_on_select                   -> set to true to automatically copy selected text
	paste_button                     -> set to desired mouse button (default: 2)
	menu_button                      -> set to desired mouse button (default: 3)

## Contributing
Pull requests are welcome. But please, create first a bug report in [Launchpad](https://bugs.launchpad.net/sakura), particularly if you plan to make major changes, to make sure your patch will be merged into **sakura**. If you'd like to contribute with translations, use the translations framework in [Launchpad](https://translations.launchpad.net/sakura) or send [me](mailto:dabisu@gmail.com) directly the translated po file.

## License
[GPL 2.0](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)

\
Enjoy **sakura**!
