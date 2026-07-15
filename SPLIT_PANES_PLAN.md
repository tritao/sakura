# Split Panes and Grid Layout Plan

This plan adds split terminal panes and grid-like layouts while preserving the
current notebook tabs, workspace groups, Codex tracking, tool integration, and
session restore behavior.

Implementation should begin after the current modular refactor and history
rewrite have been committed. The first split-pane commit should start from a
clean modular baseline.

## Design recommendation

Treat a notebook tab as a page containing a recursive binary layout tree:

```text
SakuraPage
└── SakuraLayoutNode
    ├── LEAF -> SakuraPane
    └── SPLIT
        ├── first child
        └── second child
```

Use nested `GtkPaned` widgets rather than a fixed `GtkGrid`. A binary split
tree supports arbitrary layouts, native draggable dividers, simple pane
removal/collapse, and grid presets built from ordinary splits. A true grid
container should only be considered later if exact alignment of every row and
column divider becomes a hard requirement.

The user-facing split actions should be named **Split Right** and **Split
Down**. This avoids the ambiguity of “horizontal” and “vertical” when
describing the direction in which the new pane appears.

## Ownership model

Introduce three distinct concepts in `src/sakura-private.h`:

```c
typedef struct sakura_page SakuraPage;
typedef struct sakura_pane SakuraPane;
typedef struct sakura_layout_node SakuraLayoutNode;

typedef enum {
    SAKURA_LAYOUT_LEAF,
    SAKURA_LAYOUT_SPLIT
} SakuraLayoutKind;

typedef enum {
    SAKURA_SPLIT_RIGHT,
    SAKURA_SPLIT_DOWN
} SakuraSplitDirection;
```

`SakuraPage` owns one notebook page and one root layout node. It stores the
page ID, optional user title, active pane, last active pane ID, and sidebar
association.

`SakuraPane` owns one terminal-like surface and its process state. It contains
the current `SakuraTab` fields: terminal ID, VTE/browser widget, process ID,
CWD, history, Codex metadata, tool metadata, status, attention state, and its
own layout leaf.

`SakuraLayoutNode` is either a leaf referring to exactly one pane or a split
with a direction, normalized ratio, two children, parent pointer, and the
associated `GtkPaned` widget.

Change `SakuraApp` ownership to:

```c
GPtrArray *pages;       /* Notebook order */
GHashTable *panes;      /* terminal_id -> SakuraPane */
SakuraPage *active_page;
SakuraPane *active_pane;
```

The notebook contains pages, never individual panes. Pane IDs remain globally
stable because Codex tracking, history files, and tool-tab reuse depend on
them.

## Module boundaries

Keep terminal/process behavior separate from layout behavior:

| File | Responsibility |
| --- | --- |
| `src/sakura-pane.c` | VTE creation, spawning, callbacks, pane metadata and lifecycle |
| `src/sakura-layout.c` | Pages, layout tree, `GtkPaned`, splitting, closing, focus and zoom |
| `src/sakura-workspace.c` | Sidebar hierarchy, groups, page selection and snapshot assembly |
| `src/sakura-session.c` | Versioned page/pane/layout records and validation |
| `src/sakura-integrations.c` | Codex/tool lookup and focusing containing pages |
| `src/sakura.c` | Menus, configuration, keybindings and application lifecycle |

The current `sakura-tab.c` can first be renamed to `sakura-pane.c`, with
`sakura-layout.c` added as a new focused module. Update `CMakeLists.txt` in the
same commit as each source move.

## Phase 1: centralize active surface access

Before enabling splits, remove assumptions that “current notebook page” means
“current terminal.” Add accessors such as:

```c
SakuraPage *sakura_current_page(void);
SakuraPane *sakura_active_pane(void);
SakuraPage *sakura_page_at_index(gint index);
gint sakura_page_index(SakuraPage *page);
SakuraPane *sakura_pane_for_vte(VteTerminal *vte);
SakuraPane *sakura_find_pane_by_id(const gchar *terminal_id);
```

Replace direct notebook-page lookups in copy, paste, search, CWD lookup,
rename, URL handling, Codex actions, tool reuse, font/color operations, and
process status handling.

Acceptance criterion: behavior is unchanged while every page still contains
exactly one pane.

## Phase 2: rename terminal tabs to panes

Perform a mechanical rename of the current terminal-oriented `SakuraTab`
type and functions to `SakuraPane` terminology. Examples:

```text
sakura_tab_spawn_shell() -> sakura_pane_spawn_shell()
sakura_tab_set_status()  -> sakura_pane_set_status()
sakura_tab_for_vte()     -> sakura_pane_for_vte()
sakura_tab_free()        -> sakura_pane_free()
```

Keep visual tab-strip functions named for pages or the tab bar. Do not mix
functional split behavior into this rename commit.

## Phase 3: introduce single-pane pages

Split the current tab creation flow into separate operations:

```c
SakuraPage *sakura_page_new(const gchar *restore_page_id);
SakuraPane *sakura_pane_new(const SakuraPaneOptions *options);
void sakura_page_set_root(SakuraPage *page, SakuraLayoutNode *root);
void sakura_page_add_to_notebook(SakuraPage *page, gint position);
gboolean sakura_pane_start(SakuraPane *pane,
                           const SakuraPaneLaunchOptions *options);
```

Every existing terminal should become:

```text
Page
└── Leaf
    └── Pane
```

Separate allocation, widget creation, layout insertion, and process spawning.
This allows session restoration to construct the complete layout before
starting child processes.

## Phase 4: implement the pure layout tree

Implement and test model operations before adding GTK mutation:

```c
SakuraLayoutNode *sakura_layout_leaf_new(SakuraPage *, SakuraPane *);
SakuraLayoutNode *sakura_layout_split_new(SakuraPage *,
                                          SakuraSplitDirection,
                                          gdouble ratio,
                                          SakuraLayoutNode *first,
                                          SakuraLayoutNode *second);
gboolean sakura_layout_split_leaf(SakuraLayoutNode *leaf,
                                  SakuraSplitDirection direction,
                                  SakuraPane *new_pane);
gboolean sakura_layout_remove_leaf(SakuraLayoutNode *leaf);
gboolean sakura_layout_contains_pane(SakuraLayoutNode *, SakuraPane *);
guint sakura_layout_pane_count(SakuraLayoutNode *);
void sakura_layout_foreach_pane(SakuraLayoutNode *, GFunc, gpointer);
gboolean sakura_layout_validate(SakuraPage *, GError **error);
```

Removing a leaf must close the page if it was the root. Otherwise replace its
parent split with the surviving sibling and repair all parent pointers.

The invariant checker must reject:

- A root with a parent
- Splits without exactly two children
- Leaves without exactly one pane
- Duplicate panes or nodes
- Broken page/leaf back-pointers
- An active pane not belonging to its page
- Cycles
- Excessive depth

Use defensive limits such as 64 panes per page and depth 32 during restoration.

## Phase 5: render recursive splits

Each leaf should have a focusable wrapper around the existing pane surface:

```text
GtkEventBox / GtkFrame
└── pane hbox
    ├── VTE or supported surface
    └── scrollbar
```

Each split should render as:

```c
GtkWidget *paned = gtk_paned_new(
    direction == SAKURA_SPLIT_RIGHT
        ? GTK_ORIENTATION_HORIZONTAL
        : GTK_ORIENTATION_VERTICAL);
```

### Split operation

1. Resolve the active page, pane and leaf.
2. Create a new pane inheriting CWD, colorset and environment policy.
3. Create its leaf node.
4. Create a split containing the existing and new leaves.
5. Replace the old leaf in its parent or page root.
6. Replace the corresponding GTK widget.
7. Attach both children to `GtkPaned`.
8. Set an initial ratio of `0.5`.
9. Start the new shell.
10. Focus the new pane.
11. Mark the session dirty.

Take temporary references while reparenting widgets so the old container cannot
destroy a surviving child.

Persist normalized ratios rather than pixel positions:

```c
ratio = position / available_size;
```

Ignore divider callbacks during initial allocation and restoration. Debounce
session writes using the existing save timer. Accept ratios from `0.05` to
`0.95`.

## Phase 6: active pane and directional focus

Add:

```c
void sakura_page_set_active_pane(SakuraPage *, SakuraPane *, gboolean focus);
```

The function should update active CSS state, `SakuraApp.active_page`,
`SakuraApp.active_pane`, `last_active_pane_id`, the page title, tab status and
sidebar selection.

A pane becomes active when its VTE receives focus, its wrapper is clicked, it
is selected from the sidebar, or it is chosen by a focus command.

Directional focus should use widget allocations, not only tree siblings:

1. Reject panes outside the requested direction.
2. Prefer candidates overlapping on the perpendicular axis.
3. Minimize distance in the requested direction.
4. Use perpendicular center distance as a tie-breaker.

This handles asymmetric nested layouts better than sibling-only traversal.

## Phase 7: pane lifecycle commands

Implement these operations:

- Split Right
- Split Down
- Close Pane
- Close Other Panes
- Focus Left, Right, Up and Down
- Equalize Current Split
- Zoom and Unzoom Pane

Closing a pane must disconnect signals, stop Codex tracking work, apply the
existing process-confirmation policy, remove its sidebar node, collapse the
layout, select a surviving pane, and mark the session dirty. Closing the final
pane closes the page.

Zoom should hide sibling subtrees along the active leaf's ancestor path while
leaving the layout tree unchanged. Do not reparent widgets or persist zoom in
the first implementation.

## Phase 8: session format version 4

Bump the session version from 3 to 4. Keep existing terminal records for pane
metadata and add page/layout records:

```ini
[Session]
version=4
page_count=2
layout_count=5
terminal_count=4
selected_page_id=page-1
selected_terminal_id=terminal-2

[Page0]
id=page-1
parent=group-1
title=Development
title_set_by_user=true
root_layout=layout-1
active_terminal_id=terminal-2

[Layout0]
id=layout-1
page=page-1
type=split
direction=right
ratio=0.5
first=layout-2
second=layout-3

[Layout1]
id=layout-2
page=page-1
type=leaf
terminal_id=terminal-1
```

Parse and validate the entire snapshot before modifying the UI. Validate unique
IDs, one root per page, existing child references, exactly-once pane
references, valid page ownership, active-pane membership and finite ratios.

For session versions 1–3, create one page with one leaf for each terminal.
Preserve group membership, CWD, title, tool state, Codex state, attention and
selected terminal. Failed restores must continue to preserve the previous
valid snapshot.

## Phase 9: workspace and sidebar integration

Add sidebar node types for groups, pages and panes:

```text
Group
└── Page
    ├── Pane A
    └── Pane B
```

For a single-pane page, show only the page row to avoid redundant nesting. When
the second pane is created, expose the pane children and expand the page.

Groups own pages. Moving an individual pane between groups should be deferred;
it is ambiguous whether the pane should remain in the same page layout.

Page status aggregates its panes with this priority:

```text
needs approval/error/interrupted
ready or attention
working
idle
none
```

Page attention is true if any pane needs attention. Manual page titles always
win. Otherwise use the active pane title for one pane and an active-title-plus-
count form for multiple panes.

## Phase 10: Codex and tool integration

Update Codex and tool lookup to search global panes. Focusing a result must
select its containing page, activate the target pane and focus its widget.

For the first release:

- Shell and Codex terminal pages may split.
- New splits create shells.
- Embedded WebKit pull-request pages remain single-pane.
- External GUI tools remain single-pane.
- Terminal tools can be enabled later after reuse behavior is verified.

Disable split actions for surfaces that cannot safely host a terminal pane.

## Phase 11: menus, settings and shortcuts

Add a **Pane** submenu containing split, focus, close, equalize and zoom
actions. Add configurable settings for all pane actions:

```ini
split_right_accelerator=
split_down_accelerator=
focus_pane_left_accelerator=
focus_pane_right_accelerator=
focus_pane_up_accelerator=
focus_pane_down_accelerator=
```

Initial split defaults may use `Ctrl+Shift+E` and `Ctrl+Shift+O`, but directional
focus defaults should be selected only after auditing existing tab shortcuts
and terminal application conflicts.

## Phase 12: geometry and rendering changes

The current window resize increments are based on one VTE's character size.
They cannot represent multiple panes plus divider widths.

When a page contains multiple panes:

- Disable character-cell window resize increments.
- Keep minimum sizes on individual leaves.
- Re-enable single-VTE geometry hints when one pane remains.
- Apply font and color changes to every pane.
- Preserve divider ratios across font and window-size changes.

## Phase 13: grid presets

Do not add a second grid data structure. Build presets from the split tree:

- Two columns
- Two rows
- 2×2
- Three-pane main-and-stack layout

Nested `GtkPaned` cannot guarantee that all internal dividers remain perfectly
aligned on both axes. A custom grid container should only be designed if that
limitation is unacceptable after the tree implementation is in use.

## Testing plan

### Pure tests

- Split root and nested leaves in both directions.
- Remove every leaf and verify parent collapse.
- Verify page, node, leaf and pane back-pointers.
- Verify directional focus in regular and asymmetric layouts.
- Verify status aggregation.
- Round-trip session version 4.
- Migrate versions 1–3.
- Reject cycles, duplicate IDs, orphan nodes and invalid ratios.
- Reject excessive depth and pane count.

### GTK and integration tests

- Independent child processes in every pane.
- Correct CWD and Bash history inheritance.
- Copy, paste, search and URL handling target the active pane.
- Font, colors and scrollbar settings reach all panes.
- Closing one pane leaves siblings intact.
- Codex and tool lookup focuses the correct pane.
- Sidebar selection focuses the correct leaf.
- Session restore recreates layout ratios and active pane.

### Manual stress tests

- Repeatedly create and destroy 2×2 layouts.
- Drag every divider, restart and verify ratios.
- Close panes in every order.
- Switch pages while background panes need attention.
- Restore layouts containing Codex sessions.
- Resize from small to maximized windows.
- Run the session stress test and ASan/UBSan builds.

## Commit sequence

Keep each commit buildable and behaviorally reviewable:

1. `refactor: centralize active page and terminal access`
2. `refactor: rename terminal tabs to panes`
3. `refactor: wrap panes in notebook pages`
4. `test: add split layout model coverage`
5. `feat: add recursive pane layout trees`
6. `feat: render terminal splits with GtkPaned`
7. `feat: add pane focus and lifecycle actions`
8. `feat: persist pages and split layouts`
9. `feat: expose split panes in workspace navigation`
10. `feat: aggregate pane status and attention`
11. `feat: integrate Codex and tools with split panes`
12. `feat: add grid presets and pane controls`
13. `fix: adapt window geometry to split layouts`
14. `docs: document split-pane workflows`
15. `i18n: refresh translation catalogs`

The first milestone is commit 3: every existing terminal is a one-pane page
with unchanged behavior. After that point, split functionality is additive and
can be implemented incrementally.

