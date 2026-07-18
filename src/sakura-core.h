#ifndef SAKURA_CORE_H
#define SAKURA_CORE_H

#include <glib.h>

#define SAKURA_SESSION_VERSION 7
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

typedef struct sakura_session_snapshot SakuraSessionSnapshot;

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
	gchar *title;
	gboolean title_set_by_user;
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

struct sakura_session_snapshot {
	GPtrArray *groups;
	GPtrArray *tasks;
	GPtrArray *tabs;
	GPtrArray *pages;
	GPtrArray *layouts;
	gint selected_terminal;
	gchar *selected_terminal_id;
	gchar *selected_page_id;
	gchar *selected_task_id;
	gchar *active_group_id;
	gchar *root_directory;
	gboolean sidebar_visible;
	gint sidebar_width;
	gboolean show_archived;
};

SakuraSessionSnapshot *sakura_session_snapshot_new(void);
void sakura_session_snapshot_free(SakuraSessionSnapshot *snapshot);
gboolean sakura_session_snapshot_load(GKeyFile *key_file,
                                      SakuraSessionSnapshot *snapshot,
                                      GError **error);
void sakura_session_snapshot_save(const SakuraSessionSnapshot *snapshot,
                                  GKeyFile *key_file);

#endif /* SAKURA_CORE_H */
