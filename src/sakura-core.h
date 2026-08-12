#ifndef SAKURA_CORE_H
#define SAKURA_CORE_H

#include <glib.h>

#define SAKURA_SESSION_VERSION 10
#define NUM_COLORSETS 6

/* The domain layer owns these bounds. GTK rendering and persistence must
 * validate the same layout values before they reach different clients. */
#define SAKURA_LAYOUT_MIN_RATIO 0.05
#define SAKURA_LAYOUT_MAX_RATIO 0.95
#define SAKURA_LAYOUT_DEFAULT_RATIO 0.60
#define SAKURA_LAYOUT_MAX_DEPTH 32

typedef enum {
	SAKURA_TAB_SHELL,
	SAKURA_TAB_CODEX,
	SAKURA_TAB_TOOL
} SakuraTabKind;

typedef enum {
	SAKURA_TOOL_NONE,
	SAKURA_TOOL_GITUI,
	SAKURA_TOOL_GH_DASH,
	SAKURA_TOOL_GH_PR,
	SAKURA_TOOL_GIT_COLA
} SakuraToolKind;

typedef enum {
	SAKURA_TAB_STATUS_NONE,
	SAKURA_TAB_STATUS_IDLE,
	SAKURA_TAB_STATUS_RUNNING,
	SAKURA_TAB_STATUS_NEEDS_APPROVAL,
	SAKURA_TAB_STATUS_READY,
	SAKURA_TAB_STATUS_INTERRUPTED,
	SAKURA_TAB_STATUS_ERROR
} SakuraTabStatus;

typedef enum {
	SAKURA_LAYOUT_LEAF,
	SAKURA_LAYOUT_SPLIT
} SakuraLayoutKind;

typedef enum {
	SAKURA_SPLIT_RIGHT,
	SAKURA_SPLIT_DOWN
} SakuraSplitDirection;

typedef enum {
	SAKURA_TASK_READY,
	SAKURA_TASK_WORKING,
	SAKURA_TASK_BLOCKED,
	SAKURA_TASK_REVIEW,
	SAKURA_TASK_DONE
} SakuraTaskStatus;

typedef enum {
	SAKURA_TERMINAL_STARTING,
	SAKURA_TERMINAL_RUNNING,
	SAKURA_TERMINAL_EXITED,
	SAKURA_TERMINAL_CLOSED,
	SAKURA_TERMINAL_ERROR
} SakuraCoreTerminalStatus;

typedef struct sakura_session_snapshot SakuraSessionSnapshot;
typedef struct sakura_core_workspace SakuraCoreWorkspace;
typedef struct sakura_core_group SakuraCoreGroup;
typedef struct sakura_core_task SakuraCoreTask;
typedef struct sakura_core_page SakuraCorePage;
typedef struct sakura_core_terminal SakuraCoreTerminal;

typedef struct {
	gchar *id;
	gchar *parent_id;
	gchar *title;
	gchar *directory;
	guint order;
	gboolean archived;
} SakuraSessionGroupRecord;

typedef struct {
	gchar *id;
	gchar *parent_id;
	gchar *group_id;
	gchar *title;
	gchar *provider;
	gchar *external_id;
	gchar *url;
	SakuraTaskStatus status;
	guint order;
	gboolean archived;
} SakuraSessionTaskRecord;

typedef struct {
	gchar *parent_id;
	gchar *cwd;
	gchar *title;
	gchar *terminal_id;
	gchar *tool_id;
	gchar *tool_target;
	gchar *codex_session_id;
	gchar *codex_session_name;
	gchar *codex_reasoning_effort;
	gint colorset;
	SakuraTabKind kind;
	gboolean title_set_by_user;
	SakuraTabStatus status;
	gboolean attention;
	gint64 attention_timestamp;
} SakuraSessionTabRecord;

typedef struct {
	gchar *id;
	gchar *parent_id;
	gchar *group_id;
	gchar *title;
	gboolean title_set_by_user;
	gboolean archived;
	gchar *root_layout_id;
	gchar *active_terminal_id;
	gchar *task_id;
} SakuraSessionPageRecord;

typedef struct {
	gchar *id;
	gchar *page_id;
	gchar *type;
	SakuraSplitDirection direction;
	gdouble ratio;
	gchar *first_id;
	gchar *second_id;
	gchar *terminal_id;
} SakuraSessionLayoutRecord;

typedef enum {
	SAKURA_SIDEBAR_EXPANSION_GROUP,
	SAKURA_SIDEBAR_EXPANSION_TASK,
	SAKURA_SIDEBAR_EXPANSION_SESSION
} SakuraSidebarExpansionKind;

typedef struct {
	gchar *id;
	SakuraSidebarExpansionKind kind;
} SakuraSessionSidebarExpansionRecord;

struct sakura_session_snapshot {
	GPtrArray *groups;
	GPtrArray *tasks;
	GPtrArray *tabs;
	GPtrArray *pages;
	GPtrArray *layouts;
	GPtrArray *expanded_sidebar_nodes;
	gchar *workspace_id;
	gint selected_terminal;
	gchar *selected_terminal_id;
	gchar *selected_page_id;
	gchar *selected_task_id;
	gchar *active_group_id;
	gchar *root_directory;
	guint64 workspace_revision;
	gboolean sidebar_visible;
	gint sidebar_width;
	gboolean show_archived;
	gboolean sidebar_expansion_saved;
};

/* GTK-free workspace state. These objects deliberately contain no widgets,
 * pages, PTY handles, or sidebar nodes. A desktop or web adapter can project
 * this model into its own view without becoming the owner of the domain. */
struct sakura_core_group {
	gchar *id;
	gchar *title;
	gchar *directory;
	SakuraCoreGroup *parent;
	guint order;
	gboolean archived;
};

struct sakura_core_task {
	gchar *id;
	gchar *title;
	gchar *provider;
	gchar *external_id;
	gchar *url;
	SakuraTaskStatus status;
	SakuraCoreTask *parent;
	SakuraCoreGroup *group;
	guint order;
	gboolean archived;
};

struct sakura_core_page {
	gchar *id;
	gchar *title;
	gboolean title_set_by_user;
	gboolean archived;
	gchar *root_layout_id;
	gchar *active_terminal_id;
	SakuraCoreGroup *group;
	SakuraCoreTask *task;
};

struct sakura_core_terminal {
	gchar *id;
	gchar *cwd;
	gchar *title;
	SakuraCoreGroup *group;
	SakuraCoreTask *task;
	guint cols;
	guint rows;
	SakuraCoreTerminalStatus status;
};

struct sakura_core_workspace {
	SakuraCoreGroup *root_group;
	GPtrArray *groups; /* SakuraCoreGroup *, owned. */
	GPtrArray *tasks;  /* SakuraCoreTask *, owned. */
	GPtrArray *pages;  /* SakuraCorePage *, owned. */
	GPtrArray *terminals; /* SakuraCoreTerminal *, owned. */
	SakuraCoreGroup *active_group; /* Borrowed. */
	SakuraCoreTask *active_task; /* Borrowed. */
};

SakuraSessionSnapshot *sakura_session_snapshot_new(void);
void sakura_session_snapshot_free(SakuraSessionSnapshot *snapshot);
gboolean sakura_session_snapshot_load(GKeyFile *key_file,
                                      SakuraSessionSnapshot *snapshot,
                                      GError **error);
void sakura_session_snapshot_save(const SakuraSessionSnapshot *snapshot,
                                  GKeyFile *key_file);

SakuraCoreWorkspace *sakura_core_workspace_new(void);
void sakura_core_workspace_free(SakuraCoreWorkspace *workspace);
SakuraCoreGroup *sakura_core_group_new(const gchar *id,
                                       const gchar *title,
                                       SakuraCoreGroup *parent);
void sakura_core_group_free(SakuraCoreGroup *group);
SakuraCoreTask *sakura_core_task_new(const gchar *id,
                                     const gchar *title,
                                     SakuraCoreGroup *group,
                                     SakuraCoreTask *parent);
void sakura_core_task_free(SakuraCoreTask *task);
SakuraCorePage *sakura_core_page_new(const gchar *id,
                                     SakuraCoreGroup *group,
                                     SakuraCoreTask *task);
void sakura_core_page_free(SakuraCorePage *page);
SakuraCoreTerminal *sakura_core_terminal_new(const gchar *id,
                                             const gchar *cwd,
                                             SakuraCoreGroup *group,
                                             SakuraCoreTask *task,
                                             guint cols, guint rows);
void sakura_core_terminal_free(SakuraCoreTerminal *terminal);

gboolean sakura_core_workspace_set_root(SakuraCoreWorkspace *workspace,
                                         SakuraCoreGroup *root_group);
gboolean sakura_core_workspace_add_group(SakuraCoreWorkspace *workspace,
                                         SakuraCoreGroup *group);
gboolean sakura_core_workspace_can_move_group(
	SakuraCoreWorkspace *workspace, SakuraCoreGroup *source,
	SakuraCoreGroup *parent, SakuraCoreGroup *target);
gboolean sakura_core_workspace_move_group(
	SakuraCoreWorkspace *workspace, SakuraCoreGroup *source,
	SakuraCoreGroup *parent, SakuraCoreGroup *target, gboolean after);
gboolean sakura_core_workspace_can_remove_group(
	SakuraCoreWorkspace *workspace, SakuraCoreGroup *group);
gboolean sakura_core_workspace_remove_group(SakuraCoreWorkspace *workspace,
                                            SakuraCoreGroup *group);
gboolean sakura_core_workspace_add_task(SakuraCoreWorkspace *workspace,
                                        SakuraCoreTask *task);
gboolean sakura_core_workspace_move_task(
	SakuraCoreWorkspace *workspace, SakuraCoreTask *source,
	SakuraCoreGroup *group, SakuraCoreTask *parent,
	SakuraCoreTask *target, gboolean after);
gboolean sakura_core_workspace_can_remove_task(
	SakuraCoreWorkspace *workspace, SakuraCoreTask *task);
gboolean sakura_core_workspace_remove_task(SakuraCoreWorkspace *workspace,
                                           SakuraCoreTask *task);
gboolean sakura_core_workspace_add_terminal(SakuraCoreWorkspace *workspace,
                                            SakuraCoreTerminal *terminal);
gboolean sakura_core_workspace_remove_terminal(
	SakuraCoreWorkspace *workspace, SakuraCoreTerminal *terminal);
SakuraCoreTerminal *sakura_core_workspace_find_terminal(
	SakuraCoreWorkspace *workspace, const gchar *id);
SakuraCoreGroup *sakura_core_workspace_find_group(
	SakuraCoreWorkspace *workspace, const gchar *id);
SakuraCoreTask *sakura_core_workspace_find_task(
	SakuraCoreWorkspace *workspace, const gchar *id);
SakuraCorePage *sakura_core_workspace_find_page(
	SakuraCoreWorkspace *workspace, const gchar *id);
gboolean sakura_core_workspace_add_page(SakuraCoreWorkspace *workspace,
                                         SakuraCorePage *page);
gboolean sakura_core_workspace_remove_page(SakuraCoreWorkspace *workspace,
                                            SakuraCorePage *page);
GPtrArray *sakura_core_workspace_ordered_groups(
	const SakuraCoreWorkspace *workspace);
GPtrArray *sakura_core_workspace_ordered_tasks(
	const SakuraCoreWorkspace *workspace);
gboolean sakura_core_workspace_group_is_archived(
	const SakuraCoreWorkspace *workspace, const SakuraCoreGroup *group);
gboolean sakura_core_workspace_task_is_archived(
	const SakuraCoreWorkspace *workspace, const SakuraCoreTask *task);
void sakura_core_workspace_set_group_archived(SakuraCoreWorkspace *workspace,
                                              SakuraCoreGroup *group,
                                              gboolean archived);
void sakura_core_workspace_set_task_archived(SakuraCoreWorkspace *workspace,
                                             SakuraCoreTask *task,
                                             gboolean archived);
SakuraCoreWorkspace *sakura_core_workspace_from_snapshot(
	const SakuraSessionSnapshot *snapshot, GError **error);
gboolean sakura_core_workspace_sync_snapshot(
	const SakuraCoreWorkspace *workspace, SakuraSessionSnapshot *snapshot);

#endif /* SAKURA_CORE_H */
