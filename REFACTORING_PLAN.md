# Sakura Refactoring Plan

## Goals

Refactor Sakura into a small number of cohesive modules while preserving its
current behavior, session format, GTK/VTE integration, and ability to absorb
upstream C changes.

The main architectural goals are:

- Make application and tab ownership explicit.
- Use stable tab identities instead of notebook page indexes throughout the
  application.
- Separate session data from GTK widgets.
- Keep the sidebar, tab strip, and session state synchronized through clear
  interfaces.
- Consolidate external tool and Codex integrations.
- Keep the source tree compact and avoid a large collection of tiny files.
- Maintain a working, testable application after every phase.

## Target Source Layout

The intended result is five implementation files and one private header:

| File | Responsibility |
| --- | --- |
| `src/sakura.c` | `main`, CLI, configuration, top-level window and menu lifecycle |
| `src/sakura-tab.c` | Terminal creation, spawning, tab metadata, title, CWD, and status |
| `src/sakura-workspace.c` | Sidebar, groups, scoped tab strip, selection, and ordering |
| `src/sakura-session.c` | Session snapshots, validation, locking, save, and restore |
| `src/sakura-integrations.c` | Codex, Git tools, PR browser, Open Here, and URI launching |
| `src/sakura-private.h` | Internal structures, enums, ownership rules, and cross-module interfaces |

No general-purpose `utils` module should be introduced. Helpers should remain
with the subsystem that owns the behavior.

## Phase 0: Establish the Safety Net

Create one cleanup commit with no intended behavioral changes:

- Remove tracked Python bytecode and ignore `__pycache__` and `*.pyc` files.
- Enable CTest.
- Keep the existing session stress test.
- Add one `tests/test-core.c` executable for pure logic.
- Verify Debug and Release builds.
- Enable `-Wall -Wextra -Wformat=2` in Debug builds and clear existing
  warnings.
- Record a manual smoke-test checklist covering:
  - Shell tab creation and closing
  - Codex detection, naming, and status transitions
  - Tool launching and tab reuse
  - Sidebar grouping, nesting, and reordering
  - Session save and restart
  - Ctrl-click URL opening
  - File Manager and Editor Open Here actions

## Phase 1: Make State and Ownership Explicit

Keep the implementation in `sakura.c` initially, but establish the structures
needed by the later file split:

- Replace the anonymous global structure with a named `SakuraApp` type.
- Rename `struct sakura_tab` to `SakuraTab`.
- Rename the sidebar node structure to `SakuraSidebarNode`.
- Introduce explicit lifecycle functions:
  - `sakura_app_init()`
  - `sakura_app_dispose()`
  - `sakura_tab_new()`
  - `sakura_tab_free()`
- Replace tab qdata macros with typed functions.
- Pass `SakuraApp *` or `SakuraTab *` through callback data where practical.
- Centralize timeout, subprocess, string, regex, and widget cleanup.
- Document ownership conventions:
  - `_new` and `_dup` return owned values.
  - `_get` returns borrowed values.
  - `_take` consumes ownership.

This phase should not change visible application behavior.

## Phase 2: Introduce Stable Tab Identity

Notebook page numbers are view positions and change whenever tabs move or are
removed. They should not be used as persistent identities.

- Store tabs in an application-owned `GPtrArray`.
- Use `SakuraTab *` or `terminal_id` as the normal tab identity.
- Restrict notebook page numbers to adapter functions:
  - `sakura_tab_from_page()`
  - `sakura_page_from_tab()`
  - `sakura_current_tab()`
- Make the notebook and sidebar views of the tab model rather than the
  authoritative tab storage.
- Centralize state changes through `sakura_tab_set_status()`.
- Audit callbacks for stale page indexes and replace them with stable tab
  pointers.

This phase should remove an important class of wrong-tab and sidebar
synchronization bugs.

## Phase 3: Extract Tab Lifecycle

Move terminal and tab behavior into `src/sakura-tab.c`:

- Tab widget creation and destruction
- VTE configuration
- Shell, Codex, and terminal-tool spawning hooks
- Child-exit, title, bell, and mouse callbacks
- CWD and OSC7 tracking
- Tab labels, attention, and status state
- Per-tab shell history setup

Keep its cross-module interface small. The intended shape is approximately:

```c
SakuraTab *sakura_tab_new(SakuraApp *app,
                          const SakuraTabOptions *options);
void sakura_tab_close(SakuraApp *app, SakuraTab *tab);
void sakura_tab_focus(SakuraApp *app, SakuraTab *tab);
void sakura_tab_set_status(SakuraApp *app,
                           SakuraTab *tab,
                           SakuraTabStatus status,
                           gboolean attention);
```

Widget internals should not be exposed unless another module genuinely needs
them.

## Phase 4: Extract and Test Session Persistence

Create plain session records containing no GTK widgets. For example:

```c
typedef struct {
    gchar *terminal_id;
    gchar *cwd;
    gchar *title;
    SakuraTabKind kind;
    SakuraToolKind tool;
    gchar *tool_target;
    gchar *codex_session_id;
    gchar *parent_group_id;
} SakuraSessionTab;
```

Then:

- Parse an entire session into a validated snapshot before changing the UI.
- Apply the snapshot only after validation succeeds.
- Serialize from tab and group records rather than directly from GTK models.
- Preserve the current session format and backup behavior.
- Keep atomic writes and instance locking.
- Unit-test:
  - Session round trips
  - Malformed and truncated sessions
  - Unsupported versions
  - Missing optional tools
  - Nested groups
  - Selected scope and tab restoration
  - Codex session metadata
  - Failed-restore protection
- Run the existing session stress test after every persistence change.

## Phase 5: Extract Workspace UI

Move sidebar, grouping, and scoped-tab-strip behavior to
`src/sakura-workspace.c`.

The workspace module owns:

- Adding, removing, and updating tabs in the notebook-related views
- Group creation, rename, deletion, and nesting
- Active scope and selected tab
- Sidebar and tab-strip ordering
- Attention counters and spinner rendering
- Producing a session snapshot of the current workspace
- Applying a validated session snapshot to the workspace

Session persistence remains separate. The workspace supplies and consumes
plain snapshots; the session module reads and writes them.

## Phase 6: Consolidate Integrations

Move external integration behavior into `src/sakura-integrations.c`.

Replace repeated tool switches with one descriptor table:

```c
typedef struct {
    SakuraToolKind kind;
    const gchar *id;
    const gchar *label;
    const gchar *icon;
    const gchar *executable;
    gboolean repository_scoped;
} SakuraToolSpec;
```

This module should handle:

- Tool metadata and availability
- Executable discovery
- Repository-root resolution
- Command construction
- Tool-tab reuse rules
- Codex tracking and session-name helper communication
- GitHub PR browser tabs and terminal fallback
- URI and mail launching
- File manager and editor launching

GTK callbacks should request an integration action instead of constructing
commands or resolving executables themselves.

## Phase 7: Reduce the Remaining Application File

After the subsystem extractions, `src/sakura.c` should primarily contain:

- Process startup and shutdown
- CLI parsing
- Configuration loading and saving
- Top-level GTK window construction
- Main menus and preferences
- Theme, font, palette, and color configuration
- Wiring the tab, workspace, session, and integration modules together

A target of roughly 2,000 to 2,500 lines is reasonable. The exact number is
less important than keeping ownership and dependencies clear.

## Phase 8: Modernize CMake

Modernize the build only after source boundaries have stabilized:

- List source files with `target_sources()`.
- Replace global include directories, definitions, and libraries with
  target-scoped commands.
- Use separate pkg-config targets for GLib, GIO, GTK, VTE, X11, and WebKit.
- Keep WebKitGTK optional.
- Add the core unit-test target to CTest.
- Provide an optional sanitizer build for AddressSanitizer and
  UndefinedBehaviorSanitizer.
- Apply a consistent warning policy to every Sakura source file.

## C++ Decision

The recommended path for this refactor is to remain in C.

Reasons:

- GTK3, VTE, and GLib expose native C APIs.
- Sakura still follows an upstream C repository.
- The current problems are primarily global state, ownership, and subsystem
  coupling rather than limitations of C.
- A language conversion would introduce widespread callback, casting, and
  build churn before improving those boundaries.

Orthodox C++20 becomes reasonable if Sakura is intentionally becoming a
permanent independent fork. If that decision is made, it should happen after
Phase 0 and before structural refactoring:

1. Convert the existing program to compile as C++ in a dedicated mechanical
   commit with no intentional behavior changes.
2. Keep the same five-module architecture using `.cc` files.
3. Continue using the GTK/VTE C APIs rather than introducing `gtkmm`.
4. Use RAII for owned GLib and GObject resources, `enum class` for states, and
   standard value containers where they improve ownership.
5. Use static callback trampolines carrying an application or tab pointer.
6. Never allow C++ exceptions to cross a GTK or GLib callback boundary.

A mixed or partial C/C++ migration should be avoided. The project should
either remain idiomatic GLib C or perform a deliberate whole-program compiler
conversion.

## Commit Strategy

Each phase should be divided into small, reviewable commits. Every commit must
build and leave Sakura usable.

Suggested sequence:

1. Test and repository hygiene
2. Named state structures and ownership rules
3. Explicit tab lifecycle and stable tab storage
4. Tab module extraction
5. Session snapshot model and tests
6. Session module extraction
7. Workspace module extraction
8. Integration descriptor table and module extraction
9. Remaining application cleanup
10. Target-based CMake modernization

Avoid combining refactoring with UI changes, session-format migrations,
renaming sweeps, or new integrations.

## Verification Gates

Every phase must pass:

- Debug build
- Release build
- `git diff --check`
- Core CTest suite
- Session stress test
- Manual smoke checklist
- Zero newly introduced compiler warnings

Session-format compatibility should be tested against a fixture produced by
the current committed version.

## Definition of Done

The refactor is complete when:

- The source tree contains no more than the five planned implementation files
  and one shared private header unless a strong new boundary is discovered.
- Application, tab, workspace, and session ownership are explicit.
- Notebook page indexes are confined to the notebook adapter layer.
- Session parsing and serialization operate on plain records and are covered
  by tests.
- Sidebar and tab-strip changes flow through the workspace API.
- Tool and Codex integrations use centralized descriptors and launch paths.
- `sakura.c` is primarily composition, configuration, and top-level UI.
- Debug and Release builds, unit tests, stress tests, and smoke tests all pass.
- Current user-visible behavior and the existing session format remain
  compatible.

The first implementation batch should cover Phase 0 and the named-state and
ownership portion of Phase 1.
