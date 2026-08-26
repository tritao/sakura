#ifndef SAKURA_PRIVATE_H
#define SAKURA_PRIVATE_H

#include <stdbool.h>

#include <glib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <vte/vte.h>

#include "sakura-core.h"

#ifdef HAVE_WEBKITGTK
#include <webkit2/webkit2.h>
#endif

#define PALETTE_SIZE 16
#define SAKURA_CODEX_REASONING_EFFORT_DATA_KEY "sakura-codex-reasoning-effort"

typedef struct sakura_app SakuraApp;
typedef struct sakura_workspace_model SakuraWorkspaceModel;
typedef struct sakura_agent_terminal_start_result SakuraAgentTerminalStartResult;
typedef struct _SakuraControlClientConnection SakuraControlClientConnection;
typedef struct sakura_page SakuraPage;
typedef struct sakura_startup SakuraStartup;
/* Semantic vocabulary: a page is the user-facing session container, while a
 * tab is one terminal surface/pane inside that session. Keep the historical
 * names as aliases until the implementation can be migrated module by
 * module without changing persisted identifiers. */
typedef SakuraPage SakuraSession;
int sakura_run(int argc, char **argv);
typedef struct sakura_layout_node SakuraLayoutNode;
typedef struct sakura_sidebar_node SakuraSidebarNode;
typedef struct sakura_group SakuraGroup;
typedef struct sakura_tab SakuraTab;
typedef SakuraTab SakuraPane;
typedef struct sakura_task SakuraTask;
/* This is the saved workspace snapshot; it is not one page-level session. */

struct sakura_codex_name_query;

typedef enum {
	SHOW_TAB_BAR_ALWAYS,
	SHOW_TAB_BAR_MULTIPLE,
	SHOW_TAB_BAR_NEVER
} ShowTabBar;

typedef enum {
	SAKURA_OPEN_HERE_FILE_MANAGER,
	SAKURA_OPEN_HERE_EDITOR
} SakuraOpenHereKind;

typedef enum {
	SAKURA_CODEX_TRACKING_MISSING,
	SAKURA_CODEX_TRACKING_PARTIAL,
	SAKURA_CODEX_TRACKING_ENABLED
} SakuraCodexTrackingState;

typedef enum {
	SAKURA_LAYOUT_PRESET_TWO_COLUMNS,
	SAKURA_LAYOUT_PRESET_TWO_ROWS,
	SAKURA_LAYOUT_PRESET_GRID_2X2,
	SAKURA_LAYOUT_PRESET_MAIN_STACK
} SakuraLayoutPreset;

typedef enum {
	SAKURA_FOCUS_LEFT,
	SAKURA_FOCUS_RIGHT,
	SAKURA_FOCUS_UP,
	SAKURA_FOCUS_DOWN
} SakuraFocusDirection;

typedef enum {
	SAKURA_SIDEBAR_GROUP,
	SAKURA_SIDEBAR_TASK,
	SAKURA_SIDEBAR_PAGE,
	SAKURA_SIDEBAR_TERMINAL
} SakuraSidebarNodeType;

/* Stable context for Codex resume actions. The callback opens a modal dialog,
 * so it must not retain a projection row while that nested loop is running. */
typedef struct {
	gchar *group_id;
	gchar *task_id;
} SakuraCodexResumeTarget;

/* Selection requests are coalesced until GTK has finished the current
 * workspace operation. Higher-priority requests must not be overwritten by
 * incidental synchronization from a notebook or sidebar callback. */
typedef enum {
	SAKURA_SIDEBAR_SELECTION_SYNC,
	SAKURA_SIDEBAR_SELECTION_RESTORE,
	SAKURA_SIDEBAR_SELECTION_CREATION
} SakuraSidebarSelectionReason;

typedef enum {
	SAKURA_WORKSPACE_CHANGE_NONE = 0,
	SAKURA_WORKSPACE_CHANGE_STRUCTURE = 1 << 0,
	SAKURA_WORKSPACE_CHANGE_SCOPE = 1 << 1,
	SAKURA_WORKSPACE_CHANGE_SELECTION = 1 << 2,
	SAKURA_WORKSPACE_CHANGE_METADATA = 1 << 3,
	/* The sidebar/tab projection changed, but terminal geometry did not. */
	SAKURA_WORKSPACE_CHANGE_PROJECTION = 1 << 4,
	/* A visible terminal/layout chrome change requires window sizing. */
	SAKURA_WORKSPACE_CHANGE_GEOMETRY = 1 << 5
} SakuraWorkspaceChange;

typedef struct {
	const gchar *execute_command;
	gchar **xterm_args;
	gboolean login_shell;
	gboolean hold;
	gboolean execute_on_existing_tabs;
	gboolean suppress_current_cwd_fallback;
	gboolean defer_process_start;
	gboolean suppress_selection;
	const gchar *page_id;
	guint order;
	gboolean has_order;
	const gchar *codex_tracking_token;
	SakuraPage *target_page;
	SakuraLayoutNode *target_layout;
	gdouble target_ratio;
	SakuraSplitDirection split_direction;
} SakuraTabLaunchConfig;

enum {
	SAKURA_SIDEBAR_COLUMN_TITLE,
	SAKURA_SIDEBAR_COLUMN_SUBTITLE,
	SAKURA_SIDEBAR_COLUMN_MARKUP,
	SAKURA_SIDEBAR_COLUMN_ICON,
	SAKURA_SIDEBAR_COLUMN_ATTENTION_COLOR,
	SAKURA_SIDEBAR_COLUMN_ATTENTION_VISIBLE,
	SAKURA_SIDEBAR_COLUMN_STATUS_MARKUP,
	SAKURA_SIDEBAR_COLUMN_STATUS_MARKER_VISIBLE,
	SAKURA_SIDEBAR_COLUMN_STATUS_ACTIVE,
	SAKURA_SIDEBAR_COLUMN_STATUS_PULSE,
	SAKURA_SIDEBAR_COLUMN_TOOLTIP,
	SAKURA_SIDEBAR_COLUMN_NODE,
	SAKURA_SIDEBAR_N_COLUMNS
};

/* The workspace model owns the records and registries that define workspace
 * identity, hierarchy, and selection. Sidebar rows remain a GTK projection
 * held by SakuraApp; page/tab records may retain handles to their GTK surface,
 * but the projection does not own those records. */
struct sakura_workspace_model {
	SakuraGroup *root_group;
	GList *groups;
	GPtrArray *tabs;
	GPtrArray *pages;
	GPtrArray *panes;
	GPtrArray *tasks;
	SakuraGroup *active_group;
	SakuraTask *active_task;
	SakuraPage *active_page;
	SakuraTab *active_tab;
	guint next_group_id;
	guint next_task_id;
};

typedef enum {
	SAKURA_STARTUP_IDLE,
	SAKURA_STARTUP_SCHEDULED,
	SAKURA_STARTUP_RESTORING,
	SAKURA_STARTUP_READY
} SakuraStartupPhase;

typedef struct {
	gchar *codex_session;
	gboolean codex_unsafe_mode;
	gboolean new_session;
	gboolean new_window;
	guint ntabs;
	gboolean fullscreen;
} SakuraStartupOptions;

typedef void (*SakuraStartupFinishedCallback)(gpointer data);

struct sakura_startup {
	GtkWidget *overlay;
	GtkWidget *spinner;
	GtkWidget *status_label;
	SakuraStartupPhase phase;
	SakuraStartupOptions options;
	bool preserve_failed_session;
	bool selected_terminal_ready_traced;
	guint pending_terminal_starts;
	guint restore_source_id;
	SakuraStartupFinishedCallback finished_callback;
	gpointer finished_data;
};

struct sakura_app {
	GtkWidget *main_window;
	SakuraStartup startup;
	GtkWidget *header_bar;
	GtkWidget *sidebar_paned;
	GtkWidget *sidebar;
	GtkWidget *sidebar_title;
	GtkWidget *sidebar_tree;
	GtkTreeViewColumn *sidebar_status_column;
	GtkCellRenderer *sidebar_spinner_renderer;
	GtkTreeStore *sidebar_model;
	GtkTreeSelection *sidebar_selection;
	SakuraSidebarNode *sidebar_root;
	SakuraWorkspaceModel *workspace;
	SakuraSidebarNode *sidebar_pending_insert_after;
	gboolean sidebar_syncing;
	gboolean sidebar_expansion_initialized;
	GHashTable *sidebar_expansion_keys;
	GtkTreeRowReference *sidebar_pending_selection;
	SakuraSidebarSelectionReason sidebar_pending_selection_reason;
	guint sidebar_selection_source_id;
	/* Sidebar selection is a projection of stable workspace identity. Keep the
	 * identity across a GtkTreeStore rebuild instead of accepting GTK's
	 * neighbour selection after a row disappears. */
	SakuraSidebarNodeType sidebar_selection_type;
	gchar *sidebar_selection_id;
	gboolean sidebar_selection_valid;
	/* Focus requested by Sakura must not be interpreted as a user focus event
	 * that writes a second sidebar selection. */
	guint focus_tab_source_id;
	GtkWidget *focus_tab_pending_vte;
	gchar *focus_tab_pending_terminal_id;
	gint64 selection_intent_us;
	gchar *selection_intent_terminal_id;
	gint64 last_user_interaction_us;
	gboolean ui_latency_trace_enabled;
	guint ui_latency_trace_source_id;
	gint64 ui_latency_trace_last_tick_us;
	gint64 ui_latency_trace_recent_activity_us;
	gint64 ui_latency_trace_recent_duration_us;
	const gchar *ui_latency_trace_recent_cause;
	GdkFrameClock *ui_latency_trace_frame_clock;
	gulong ui_latency_trace_after_paint_handler_id;
	gint64 ui_latency_trace_last_paint_us;
	gint64 ui_latency_trace_selection_paint_us;
	gchar *ui_latency_trace_selection_terminal_id;
	gboolean sidebar_focus_syncing;
	gboolean sidebar_visible;
	gint sidebar_width;
	guint sidebar_resize_source_id;
	GtkWidget *notebook;
	GtkWidget *content_box;
	GtkWidget *tab_bar_shell;
	GtkWidget *tab_bar_scope_label;
	GtkWidget *tab_bar_scrolled;
	GtkWidget *tab_bar;
	GtkWidget *tab_bar_new_button;
	GtkWidget *tab_bar_empty;
	SakuraSidebarNode *active_group_scope;
	bool show_archived;
	gboolean tab_bar_refreshing;
	GtkWidget *menu;
	GtkWidget *fade_window;  /* Window used for fading effect */
	PangoFontDescription *font;
	gdouble line_height; /* Font line height */
	GdkRGBA forecolors[NUM_COLORSETS];
	GdkRGBA backcolors[NUM_COLORSETS];
	GdkRGBA curscolors[NUM_COLORSETS];
	guint schemes[NUM_COLORSETS];  /* Selected color scheme for each colorset */
	const GdkRGBA *palette;
	guint palette_idx;
	GdkRGBA system_foreground;
	GdkRGBA system_background;
	GdkRGBA system_cursor;
	GdkRGBA system_palette[PALETTE_SIZE];
	bool have_system_colors;
	bool system_bold_is_bright;
	gint last_colorset;
	char *current_match;
	guint width;
	guint height;
	glong columns;
	glong rows;
	gint scroll_lines;
	VteCursorShape cursor_type;
	ShowTabBar show_tab_bar;         /* Show the tab bar: always, multiple, never */
	bool show_scrollbar;
	bool show_closebutton;
	bool new_tab_after_current;
	bool tabs_on_bottom;
	bool less_questions;
	bool copy_on_select;
	bool urgent_bell;
	bool audible_bell;
	bool blinking_cursor;
	bool fullscreen;
	bool config_modified;            /* Configuration has been modified */
	bool externally_modified;        /* Configuration file has been modified by another process */
	/* Keep top-level resize requests separate from user-driven resizes. GTK can
	 * deliver several configure events while a programmatic request settles;
	 * treating those as user input feeds a rounded-down terminal grid back into
	 * the next resize and progressively shrinks the window. */
	bool programmatic_resize;
	guint programmatic_resize_source_id;
	guint user_resize_source_id;
	bool disable_numbered_tabswitch; /* For disabling direct tabswitching key */
	bool use_fading;                 /* Fade the window when the focus change */
	bool scrollable_tabs;
	bool bold_is_bright;             /* Show bold characters as bright */
	bool dont_save;                  /* Don't save config file */
	bool first_run;                  /* To only execute commands first time sakura is launched */
	bool session_restoring;
	bool session_ready;
	bool session_shutting_down;
	bool session_restore_failed;
	guint workspace_mutation_depth;
	guint workspace_pending_changes;
	gboolean workspace_reconciling;
	gboolean agent_snapshot_reconciling;
	/* Notebook signals can arrive while a workspace mutation is changing the
	 * model. Keep the latest physical selection until the outer transaction
	 * reconciles the model and GTK again. */
	gchar *workspace_pending_notebook_terminal_id;
	gboolean workspace_selection_cleared;
	bool session_dirty;
	bool session_new_window;
	guint session_save_source_id;
	GThread *session_save_thread;
	GMutex session_save_mutex;
	GCond session_save_cond;
	gboolean session_save_worker_initialized;
	gboolean session_save_worker_stopping;
	gboolean session_save_worker_busy;
	gpointer session_save_pending_job;
	guint session_save_completion_source_id;
	guint64 session_change_generation;
	guint64 session_saved_generation;
	guint codex_tracking_source_id;
	GFileMonitor *codex_tracking_monitor;
	guint cwd_tracking_source_id;
	guint sidebar_spinner_source_id;
	guint sidebar_spinner_pulse;
	guint sidebar_primary_click_source_id;
	SakuraSidebarNodeType sidebar_primary_click_type;
	gchar *sidebar_primary_click_id;
	GSubprocess *codex_name_helper_process;
	GSubprocess *agent_process;
	GThreadPool *agent_terminal_start_pool;
	gboolean agent_terminal_start_stopping;
	gchar *agent_socket_path;
	gchar *agent_socket_path_override;
	guint agent_restart_source_id;
	bool agent_supervisor_stopping;
	GThread *agent_event_thread;
	SakuraControlClientConnection *agent_event_connection;
	GMutex agent_event_mutex;
	gboolean agent_event_mutex_initialized;
	gboolean agent_event_stopping;
	GQueue *agent_terminal_event_queue;
	gboolean agent_terminal_event_flush_scheduled;
	GThread *agent_command_thread;
	SakuraControlClientConnection *agent_command_connection;
	GCancellable *agent_command_cancellable;
	GMutex agent_command_mutex;
	GCond agent_command_cond;
	GQueue *agent_command_queue;
	gboolean agent_command_mutex_initialized;
	gboolean agent_command_stopping;
	GMutex agent_revision_mutex;
	gboolean agent_revision_mutex_initialized;
	guint64 agent_workspace_revision;
	GMutex agent_workspace_mutation_mutex;
	gboolean agent_workspace_mutation_mutex_initialized;
	GDataInputStream *codex_name_helper_output;
	GOutputStream *codex_name_helper_input;
	GQueue *codex_name_query_queue;
	struct sakura_codex_name_query *codex_name_query_in_flight;
	guint codex_name_helper_request_id;
	GtkWidget *item_copy_link;       /* We include here only the items which need to be hidden */
	GtkWidget *item_open_link;
	GtkWidget *item_open_mail;
	GtkWidget *open_link_separator;
	GtkWidget *item_select_text;
	GtkWidget *pane_menu;
	GtkWidget *pane_layout_menu;
	GtkWidget *pane_split_right;
	GtkWidget *pane_split_down;
	GtkWidget *pane_focus_left;
	GtkWidget *pane_focus_right;
	GtkWidget *pane_focus_up;
	GtkWidget *pane_focus_down;
	GtkWidget *pane_close;
	GtkWidget *pane_equalize;
	GtkWidget *pane_zoom;
	GKeyFile *cfg;
	GKeyFile *session_cfg;
	SakuraSessionSnapshot *session_snapshot;
	SakuraSessionSnapshot *agent_pending_snapshot;
	gchar *workspace_id;
	char *configfile;
	char *sessionfile;
	char *session_lock_path;
	int session_lock_fd;
	char *codex_tracking_dir;
	char *history_dir;
	char *bash_history_rc;
	char *icon;
	char *shell_path;
	char *editor_command;
	char *main_title;		/* Main window static title from user input */
	char *term;
	gchar *tab_default_title;
	gint add_tab_accelerator;
	gint del_tab_accelerator;
	gint switch_tab_accelerator;
	gint move_tab_accelerator;
	gint copy_accelerator;
	gint scrollbar_accelerator;
	gint open_url_accelerator;
	gint font_size_accelerator;
	gint set_tab_name_accelerator;
	gint search_accelerator;
	gint set_colorset_accelerator;
	gint new_window_accelerator;
	gint split_right_accelerator;
	gint split_down_accelerator;
	gint focus_pane_left_accelerator;
	gint focus_pane_right_accelerator;
	gint focus_pane_up_accelerator;
	gint focus_pane_down_accelerator;
	gint pane_close_accelerator;
	gint pane_equalize_accelerator;
	gint pane_zoom_accelerator;
	gint add_tab_key;
	gint del_tab_key;
	gint prev_tab_key;
	gint next_tab_key;
	gint copy_key;
	gint paste_key;
	gint scrollbar_key;
	gint set_tab_name_key;
	gint search_key;
	gint fullscreen_key;
	gint increase_font_size_key;
	gint decrease_font_size_key;
	gint split_right_key;
	gint split_down_key;
	gint focus_pane_left_key;
	gint focus_pane_right_key;
	gint focus_pane_up_key;
	gint focus_pane_down_key;
	gint pane_close_key;
	gint pane_equalize_key;
	gint pane_zoom_key;
	gint set_colorset_keys[NUM_COLORSETS];
	gint paste_button;
	gint menu_button;
	gint new_window_key;
	int orig_argc; /* Used for new windows */
	char **orig_argv; /* Used for new windows */
	VteRegex *http_vteregexp, *mail_vteregexp;
	char *word_chars;                /* Exceptions for word selection */
	char *argv[3];
};

struct sakura_sidebar_node {
	SakuraSidebarNodeType type;
	gchar *id;
	gchar *title;
	gchar *subtitle;
	gboolean subtitle_is_directory;
	gchar *tooltip;
	SakuraSidebarNode *parent;
	SakuraGroup *group;
	SakuraTask *task;
	SakuraPage *page;
	SakuraTab *tab;
	GtkTreeRowReference *row;
};

struct sakura_group {
	gchar *id;
	gchar *title;
	gchar *directory;
	gchar *last_terminal_id;
	SakuraGroup *parent;
	guint order; /* Sibling order in the workspace model. */
	gboolean archived;
	SakuraSidebarNode *sidebar_node; /* Current projection row, if materialized. */
};

struct sakura_task {
	gchar *id;
	gchar *title;
	gchar *provider;
	gchar *external_id;
	gchar *url;
	SakuraTaskStatus status;
	SakuraSidebarNode *sidebar_node;
	SakuraTask *parent;
	SakuraGroup *group; /* Explicit owning group; not separately owned. */
	guint order; /* Sibling order in the workspace model. */
	gboolean archived;
};

struct sakura_page {
	gchar *id;
	gchar *title;
	gboolean title_set_by_user;
	gboolean archived;
	GtkWidget *container;
	SakuraLayoutNode *layout_root;
	SakuraTab *active_tab;
	SakuraSidebarNode *sidebar_node;
	SakuraTask *task;
	SakuraGroup *group; /* Explicit owning group; not separately owned. */
	gchar *last_active_terminal_id;
	SakuraTab *tab_bar_tab;
	GPtrArray *panes;
	gboolean zoomed;
	/* True when this GTK page was materialized from an agent snapshot. */
	gboolean agent_owned;
};

struct sakura_layout_node {
	gchar *id;
	SakuraLayoutKind kind;
	SakuraLayoutNode *parent;
	SakuraPage *page;
	GtkWidget *widget;
	gboolean ratio_applied;
	union {
		struct {
			SakuraTab *tab;
		} leaf;
		struct {
			SakuraSplitDirection direction;
			gdouble ratio;
			SakuraLayoutNode *first;
			SakuraLayoutNode *second;
		} split;
	} data;
};

struct sakura_tab {
	GtkWidget *hbox;
	GtkWidget *label;
	GtkWidget *spinner;
	GtkWidget *tab_title_hbox;
	GtkWidget *tab_event_box;
	GtkWidget *tab_close_button;
	GtkWidget *tab_button;
	GtkWidget *tab_item;
	GtkWidget *tab_button_icon;
	GtkWidget *tab_button_label;
	GtkWidget *tab_button_status;
	GtkWidget *tab_button_spinner;
	GtkWidget *tab_button_close;
	GtkWidget *vte;      /* Reference to VTE terminal */
	GtkWidget *terminal_overlay;
	GtkWidget *runtime_placeholder;
	GtkWidget *scrollbar;
	VtePty *agent_pty;  /* VTE's local proxy for an agent-owned PTY */
	int agent_proxy_slave_fd;
	guint agent_proxy_input_source_id;
	guint agent_cols;
	guint agent_rows;
	guint64 agent_last_output_offset;
	gboolean agent_backed;
	gboolean agent_start_pending;
	gboolean agent_terminal_exited;
	gboolean agent_terminal_lost;
	gboolean runtime_deferred;
	gboolean runtime_start_pending;
	guint runtime_start_source_id;
	guint order;
	gboolean has_order;
#ifdef HAVE_WEBKITGTK
	GtkWidget *browser;
	GtkWidget *browser_back;
	GtkWidget *browser_forward;
#endif
	GtkBorder padding;   /* inner-property data */
	bool label_set_byuser;
	int colorset;
	GPid pid;           /* pid of the forked process */
	gulong exit_handler_id;
	gchar *cwd;
	gchar *host;
	gchar *raw_title;
	gchar *user_title;  /* Persisted title when the user renamed this tab. */
	gchar *terminal_id;
	SakuraTabKind kind;
	SakuraToolKind tool;
	gchar *tool_target;
	gchar *codex_session_id;
	gchar *codex_session_name;
	gchar *codex_model;
	gchar *codex_reasoning_effort;
	gchar *codex_resume_cwd;
	gchar *codex_turn_id;
	gchar *codex_interrupt_turn_id;
	gchar *codex_tracking_token;
	gboolean codex_start_pending;
	gboolean codex_resume_cwd_query_active;
	gboolean codex_resume_cwd_lookup_done;
	gboolean codex_session_query_active;
	guint codex_name_retry_source_id;
	guint codex_name_retry_count;
	gboolean codex_interrupt_requested;
	SakuraTabStatus status;
	gboolean attention;
	gint64 attention_timestamp;
	gboolean attention_restore_pending;
	gboolean text_selection_mode;
	gboolean hold;
	SakuraSidebarNode *sidebar_node;
	SakuraPage *page;
	SakuraLayoutNode *layout_leaf;
};

extern SakuraApp sakura;

SakuraPage *sakura_page_new(const gchar *id);
void sakura_page_free(SakuraPage *page);
SakuraSession *sakura_session_for_pane(SakuraPane *pane);
SakuraPane *sakura_session_active_pane(SakuraSession *session);
GtkWidget *sakura_page_widget_for_tab(SakuraTab *tab);
SakuraLayoutNode *sakura_layout_leaf_new(SakuraPage *page, SakuraTab *tab);
SakuraLayoutNode *sakura_layout_split_new(SakuraPage *page,
                                           SakuraSplitDirection direction,
                                           gdouble ratio,
                                           SakuraLayoutNode *first,
                                           SakuraLayoutNode *second);
gboolean sakura_layout_split_leaf(SakuraLayoutNode *leaf,
                                  SakuraSplitDirection direction,
                                  SakuraTab *new_tab);
gboolean sakura_layout_split_leaf_widgets(SakuraLayoutNode *leaf,
                                           SakuraSplitDirection direction,
                                           SakuraTab *new_tab);
gboolean sakura_layout_split_node_widgets(SakuraLayoutNode *node,
                                          SakuraSplitDirection direction,
                                          SakuraTab *new_tab);
void sakura_layout_set_ratio(SakuraLayoutNode *split, gdouble ratio);
gboolean sakura_layout_remove_leaf_widgets(SakuraLayoutNode *leaf);
void sakura_layout_set_zoomed(SakuraPage *page, SakuraTab *tab, gboolean zoomed);
void sakura_layout_paned_position_cb(GObject *object, GParamSpec *pspec,
                                     gpointer data);
gboolean sakura_layout_remove_leaf(SakuraLayoutNode *leaf);
gboolean sakura_layout_contains_tab(const SakuraLayoutNode *node,
                                    const SakuraTab *tab);
guint sakura_layout_tab_count(const SakuraLayoutNode *node);
void sakura_layout_foreach_tab(const SakuraLayoutNode *node,
                               GFunc callback,
                               gpointer user_data);
gboolean sakura_layout_validate(const SakuraPage *page, GError **error);

const gchar *sakura_tab_status_label(SakuraTabStatus status);
const gchar *sakura_tab_status_color(SakuraTabStatus status);
const gchar *sakura_tab_status_symbol(SakuraTabStatus status);
gboolean sakura_tab_is_current(SakuraTab *tab);
gboolean sakura_tab_can_split(SakuraTab *tab);
SakuraTab *sakura_tab_new(void);
void sakura_tab_free(SakuraTab *tab);
void sakura_tab_disconnect_exit_handler(SakuraTab *tab);
SakuraTab *sakura_tab_for_vte(VteTerminal *vte);
SakuraTab *sakura_find_pane_by_terminal_id(const gchar *terminal_id);
gboolean sakura_tab_start_agent_terminal(SakuraTab *tab, const gchar *cwd);
gboolean sakura_tab_start_deferred_runtime(SakuraTab *tab);
void sakura_tab_start_deferred_runtime_async(SakuraTab *tab);
gboolean sakura_tab_restart_agent_terminal(SakuraTab *tab);
void sakura_tab_agent_feed_output(SakuraTab *tab, const guint8 *data,
                                  gsize data_length, guint64 start_offset,
                                  guint64 end_offset);
gboolean sakura_tab_resume_agent_terminal(SakuraTab *tab);
void sakura_tab_agent_status(SakuraTab *tab, guint status,
                             const gchar *message);
void sakura_tab_sync_agent_size(SakuraTab *tab);
void sakura_tab_close_agent_terminal(SakuraTab *tab);
void sakura_set_tab_label_text(const gchar *title, gint page);
void sakura_set_window_title(const gchar *title);
gboolean sakura_update_tab_cwd(SakuraTab *tab);
void sakura_update_tab_metadata(SakuraTab *tab, const gchar *raw_title);
gboolean sakura_terminal_id_is_valid(const gchar *terminal_id);
gchar *sakura_generate_terminal_id(void);
gchar *sakura_history_file_for_tab(const SakuraTab *tab);
void sakura_prepare_history_file(SakuraTab *tab);
void sakura_remove_history_file(SakuraTab *tab);
void sakura_tab_create_widgets(SakuraTab *tab);
void sakura_tab_configure_terminal(SakuraTab *tab);
gboolean sakura_bash_integration_enabled(void);
void sakura_spawn_callback(VteTerminal *vte, GPid pid, GError *error,
                           gpointer user_data);
void sakura_tab_spawn_shell(SakuraTab *tab, const gchar *cwd, gchar **env,
                            gboolean login_shell);
void sakura_tab_spawn_codex(SakuraTab *tab, const gchar *cwd, gchar **env);
void sakura_build_command(const gchar *execute_command, gchar **xterm_args,
                          int *command_argc, gchar ***command_argv);
gchar **sakura_tab_build_environment(SakuraTab *tab, gboolean login_shell);
gboolean sakura_tab_spawn_command(SakuraTab *tab, const gchar *cwd,
                                   gchar **env, const gchar *execute_command,
                                   gchar **xterm_args);
void sakura_set_text_selection_mode(SakuraTab *tab, gboolean enabled);
gboolean sakura_tab_keypress_cb(GtkWidget *widget, GdkEventKey *event,
                                gpointer data);
gboolean sakura_key_press_cb(GtkWidget *widget, GdkEventKey *event,
                             gpointer user_data);
void sakura_tab_title_changed_cb(GtkWidget *widget, void *data);
gboolean sakura_pane_focus_in_cb(GtkWidget *widget, GdkEventFocus *event,
                                 gpointer data);
gboolean sakura_tab_is_in_active_scope(SakuraTab *tab);
gboolean sakura_pane_is_in_active_scope(SakuraPane *pane);
SakuraSidebarNode *sakura_sidebar_default_parent(void);
void sakura_sidebar_prepare_page_parent(SakuraPage *page,
                                        SakuraSidebarNode *parent);
void sakura_sidebar_set_scope(SakuraSidebarNode *scope);
gboolean sakura_sidebar_sync_page_to_agent(SakuraPage *page,
                                           SakuraGroup *group,
                                           SakuraTask *task);
void sakura_sidebar_add_terminal(SakuraTab *tab, SakuraSidebarNode *parent);
void sakura_sidebar_update_page(SakuraPage *page);
void sakura_sidebar_remove_page(SakuraPage *page);
void sakura_select_scope_default(void);
void sakura_remember_current_scope_tab(SakuraTab *tab);
void sakura_focus_tab(SakuraTab *tab);
void sakura_register_codex_icon(void);
void sakura_set_size(void);
void sakura_update_geometry_hints(void);
void sakura_ui_latency_trace_start(void);
void sakura_ui_latency_trace_stop(void);
gint64 sakura_ui_latency_trace_begin(void);
void sakura_ui_latency_trace_end(const gchar *cause, gint64 started_us);
void sakura_ui_latency_trace_request_paint(const gchar *terminal_id,
                                           gint64 selection_us);
void sakura_ui_latency_trace_milestone(const gchar *name);
gboolean sakura_term_buttonpressed_cb(GtkWidget *widget, GdkEventButton *event,
                                      gpointer user_data);
gboolean sakura_term_buttonreleased_cb(GtkWidget *widget, GdkEventButton *event,
                                       gpointer user_data);
void sakura_beep_cb(GtkWidget *widget, void *data);
void sakura_increase_font_cb(GtkWidget *widget, void *data);
void sakura_decrease_font_cb(GtkWidget *widget, void *data);
void sakura_child_exited_cb(GtkWidget *widget, void *data);
void sakura_eof_cb(GtkWidget *widget, void *data);
gboolean sakura_notebook_scroll_cb(GtkWidget *widget, GdkEventScroll *event);
void sakura_switch_page_cb(GtkWidget *widget, GtkWidget *widget_page,
                           guint page_num, void *data);
void sakura_page_removed_cb(GtkWidget *widget, void *data);
gboolean sakura_notebook_focus_cb(GtkWindow *window, GdkEvent *event, void *data);
gboolean sakura_label_clicked_cb(GtkWidget *widget, GdkEventButton *event,
                                  void *data);
void sakura_closebutton_clicked_cb(GtkWidget *widget, void *data);
void sakura_destroy(void);
void sakura_set_font(void);
void sakura_set_colors(void);
void sakura_config_done(void);
void sakura_search(const char *pattern, bool reverse);
void sakura_copy(void);
void sakura_paste(void);
void sakura_paste_primary(void);
void sakura_tab_set_status(SakuraTab *tab, SakuraTabStatus status,
                           gboolean attention);
void sakura_tab_mark_attention(SakuraTab *tab);
void sakura_tab_handle_bell(SakuraTab *tab);
void sakura_tab_clear_attention(SakuraTab *tab);
void sakura_tab_restore_state(SakuraTab *tab, SakuraTabStatus status,
                              gboolean attention, gint64 attention_timestamp);
SakuraTab *sakura_tab_at_page(gint page);
SakuraPage *sakura_page_at_page(gint page);
gint sakura_page_for_tab(SakuraTab *tab);
gboolean sakura_notebook_sync_page_order(void);
gboolean sakura_notebook_detach_page(SakuraPage *page);
gint sakura_find_tab_by_terminal_id(const gchar *terminal_id);
void sakura_notebook_page_reordered_cb(GtkNotebook *notebook, GtkWidget *child,
                                       guint page_num, void *data);
guint sakura_tab_bar_visible_count(void);
gint sakura_tab_bar_nth_visible_page(guint visible_index);
gboolean sakura_tab_bar_select_relative(gint direction);
void sakura_tab_bar_refresh(void);
void sakura_tab_bar_update_tab(SakuraTab *tab);
void sakura_tab_bar_add_tab(SakuraTab *tab);
void sakura_tab_bar_remove_tab(SakuraTab *tab);
void sakura_select_tab(SakuraTab *tab, gboolean focus);
void sakura_select_pane(SakuraPane *pane, gboolean focus);
void sakura_select_session(SakuraSession *session, gboolean focus);
void sakura_new_tab_cb(GtkWidget *widget, void *data);
void sakura_new_tab_for_group(SakuraSidebarNode *group);
void sakura_new_tab_for_task(SakuraTask *task);
void sakura_split_current_cb(GtkWidget *widget, void *data);
void sakura_focus_direction_cb(GtkWidget *widget, void *data);
void sakura_toggle_zoom_current_cb(GtkWidget *widget, void *data);
void sakura_equalize_current_cb(GtkWidget *widget, void *data);
void sakura_new_codex_cb(GtkWidget *widget, void *data);
void sakura_resume_codex_cb(GtkWidget *widget, void *data);
void sakura_attach_codex_cb(GtkWidget *widget, void *data);
void sakura_refresh_codex_name_cb(GtkWidget *widget, void *data);
void sakura_rename_codex_session_cb(GtkWidget *widget, void *data);
GtkWidget *sakura_codex_rename_menu_item_new(SakuraTab *tab);
void sakura_install_codex_hook_cb(GtkWidget *widget, void *data);
void sakura_close_tab_cb(GtkWidget *widget, void *data);
void sakura_sidebar_init(gboolean restore_session);
void sakura_sidebar_selection_changed_cb(GtkTreeSelection *selection, void *data);
void sakura_sidebar_primary_click(SakuraSidebarNode *node);
void sakura_sidebar_cancel_primary_click(void);
gboolean sakura_sidebar_button_press_cb(GtkWidget *widget, GdkEventButton *event,
                                         void *data);
GtkWidget *sakura_sidebar_context_menu_new(SakuraSidebarNode *node);
void sakura_sidebar_new_group_cb(GtkWidget *widget, void *data);
void sakura_sidebar_new_task_cb(GtkWidget *widget, void *data);
void sakura_sidebar_attach_page_to_task_cb(GtkWidget *widget, void *data);
void sakura_sidebar_task_start_cb(GtkWidget *widget, void *data);
void sakura_sidebar_model_reordered_cb(GtkTreeModel *model, GtkTreePath *path,
                                       GtkTreeIter *iter, gint *new_order,
                                       void *data);
void sakura_sidebar_toggle_cb(GtkWidget *widget, void *data);
void sakura_sidebar_paned_position_cb(GObject *object, GParamSpec *pspec,
                                      void *data);
void sakura_sidebar_resize_settled_cb_remove(void);
SakuraSidebarNode *sakura_sidebar_find_group_by_id(const gchar *id);
gchar *sakura_sidebar_directory_for_node(SakuraSidebarNode *node);
SakuraSidebarNode *sakura_sidebar_creation_parent_for_context(
                                      SakuraSidebarNode *context);
gboolean sakura_sidebar_move_page_to_group(SakuraPage *page,
                                            SakuraSidebarNode *group);
gboolean sakura_sidebar_reorder_page_relative(
    SakuraPage *source, SakuraPage *target, GtkTreeViewDropPosition position);
gboolean sakura_sidebar_can_reorder_node_to_group(SakuraSidebarNode *source,
                                                    SakuraSidebarNode *target);
void sakura_sidebar_sync_projection_links(void);
gboolean sakura_workspace_restore_snapshot(SakuraSessionSnapshot *snapshot);
typedef void (*SakuraWorkspaceRestoreCallback)(gboolean success, gpointer data);
gboolean sakura_workspace_restore_snapshot_async(
	SakuraSessionSnapshot *snapshot, SakuraWorkspaceRestoreCallback callback,
	gpointer data);
void sakura_workspace_restore_snapshot_async_cancel(void);
void sakura_workspace_finish_restore(void);
gboolean sakura_workspace_validate(GError **error);
gboolean sakura_cwd_tracking_poll_cb(gpointer data);
void sakura_set_name_dialog_cb(GtkWidget *widget, void *data);
void sakura_setname_entry_changed_cb(GtkWidget *widget, void *data);
void sakura_copy_cb(GtkWidget *widget, void *data);
void sakura_paste_cb(GtkWidget *widget, void *data);
void sakura_select_text_cb(GtkWidget *widget, void *data);
void sakura_search_dialog(void);
gboolean sakura_close_tab(gint page);
void sakura_tab_move_relative(gint direction);
void sakura_new_tool_cb(GtkWidget *widget, void *data);
void sakura_apply_layout_preset_cb(GtkWidget *widget, void *data);
void sakura_open_pr_cb(GtkWidget *widget, void *data);
void sakura_open_here_cb(GtkWidget *widget, void *data);
void sakura_open_url_cb(GtkWidget *widget, void *data);
void sakura_open_mail_cb(GtkWidget *widget, void *data);
void sakura_copy_url_cb(GtkWidget *widget, void *data);
void sakura_copy_pr_url_cb(GtkWidget *widget, void *data);
void sakura_tab_spawn_tool(SakuraTab *tab, const gchar *cwd, gchar **env);
void sakura_tab_resume_codex_with_cwd(SakuraTab *tab, const gchar *fallback_cwd);
gboolean sakura_tab_start_process(SakuraTab *tab, const gchar *cwd, gchar **env,
                                   SakuraTabKind kind, SakuraToolKind tool,
                                   const gchar *execute_command, gchar **xterm_args,
                                   gboolean allow_execute);
void sakura_tab_add_with_options(const gchar *restore_cwd,
                                 SakuraSidebarNode *restore_parent,
                                 const gchar *restore_title,
                                 gboolean restore_title_set,
                                 SakuraTabKind restore_kind,
                                 SakuraToolKind restore_tool,
                                 const gchar *restore_codex_session_id,
                                 const gchar *restore_codex_session_name,
                                 const gchar *restore_codex_model,
                                 const gchar *restore_codex_reasoning_effort,
                                 const gchar *restore_tool_target,
                                 const gchar *restore_terminal_id,
                                 gint restore_colorset,
                                 const SakuraTabLaunchConfig *launch_config);
gboolean sakura_tab_delete_page(gint page);
void sakura_tab_delete_pane(SakuraTab *tab);
gboolean sakura_codex_session_id_is_uuid(const gchar *value);
gboolean sakura_codex_reasoning_effort_is_valid(const gchar *value);
const gchar *sakura_codex_reasoning_effort_label(const gchar *value);
gboolean sakura_codex_tracking_poll_cb(gpointer data);
void sakura_codex_tracking_changed_cb(GFileMonitor *monitor, GFile *file,
                                      GFile *other_file,
                                      GFileMonitorEvent event_type,
                                      gpointer data);
gboolean sakura_codex_interrupt_matches_event(const SakuraTab *tab,
                                               const gchar *event_name,
                                               const gchar *turn_id);
SakuraTab *sakura_find_codex_tab_by_tracking_token(const gchar *token);
gchar *sakura_find_codex_name_helper(void);
void sakura_codex_name_helper_shutdown(void);
void sakura_codex_sync_name(SakuraTab *tab);
void sakura_codex_resolve_resume_cwd_async(SakuraTab *tab,
                                           const gchar *fallback_cwd);
void sakura_codex_set_name_async(SakuraTab *tab, const gchar *name);
SakuraCodexTrackingState sakura_codex_tracking_state(void);
gboolean sakura_codex_ensure_tracking(GError **error);
void sakura_codex_tracking_menu_update(GtkWidget *item);
void sakura_codex_tracking_status_cb(GtkWidget *widget, void *data);
GtkWidget *sakura_open_here_menu_new(void);
void sakura_error(const char *format, ...);
gchar *sakura_get_term_cwd(SakuraTab *tab);
gchar *sakura_get_term_cwd_osc7(SakuraTab *tab);
void sakura_session_accept_changes(void);
int sakura_session_lock_acquire(SakuraApp *app, const gchar *sessionfile);
gboolean sakura_session_backup_existing(const gchar *sessionfile);
gboolean sakura_session_confirm_new_instance(SakuraApp *app);
gboolean sakura_session_start_new_instance(SakuraApp *app);
void sakura_add_tab(void);
void sakura_add_tab_with_options(const gchar *restore_cwd,
                                 SakuraSidebarNode *sidebar_parent,
                                 const gchar *restore_title,
                                 gboolean restore_title_set,
                                 SakuraTabKind restore_kind,
                                 SakuraToolKind restore_tool,
                                 const gchar *restore_codex_session,
                                 const gchar *restore_codex_name,
                                 const gchar *restore_codex_model,
                                 const gchar *restore_codex_reasoning_effort,
                                 const gchar *restore_tool_target,
                                 const gchar *restore_terminal_id,
                                 gint restore_colorset);
void sakura_sidebar_update_tab(SakuraTab *tab);
void sakura_workspace_start_page_runtime(SakuraPage *page);
void sakura_sidebar_update_attention_count(void);
gboolean sakura_sidebar_spinner_pulse_cb(gpointer data);
void sakura_sidebar_spinner_stop(void);
gboolean sakura_sidebar_get_iter(SakuraSidebarNode *node, GtkTreeIter *iter);
void sakura_sidebar_free_node(SakuraSidebarNode *node);
void sakura_sidebar_insert_node(SakuraSidebarNode *node);
void sakura_sidebar_insert_node_after(SakuraSidebarNode *node,
                                      SakuraSidebarNode *sibling);
SakuraSidebarNode *sakura_sidebar_selected_node(void);
SakuraSidebarNode *sakura_sidebar_selected_group(void);
void sakura_sidebar_remove_tab(SakuraTab *tab);
void sakura_sidebar_queue_select_node(SakuraSidebarNode *node);
void sakura_sidebar_queue_select_node_with_reason(
	SakuraSidebarNode *node, SakuraSidebarSelectionReason reason);
void sakura_sidebar_select_created_tab(SakuraTab *tab);
void sakura_sidebar_cancel_pending_selection(void);
void sakura_focus_tab_cancel_pending(void);
void sakura_sidebar_apply_default_expansion(void);
void sakura_sidebar_collapse_all(void);
void sakura_sidebar_rebuild_projection(void);
void sakura_sidebar_capture_expansion(SakuraSessionSnapshot *snapshot);
void sakura_workspace_begin_mutation(void);
void sakura_workspace_end_mutation(void);
gboolean sakura_workspace_is_mutating(void);
void sakura_workspace_mark_changed(SakuraWorkspaceChange changes);
void sakura_workspace_reconcile(void);
void sakura_workspace_reconcile_selection(void);
void sakura_sidebar_set_node_row(SakuraSidebarNode *node, GtkTreeIter *iter);
const gchar *sakura_task_status_label(SakuraTaskStatus status);
const gchar *sakura_task_status_symbol(SakuraTaskStatus status);
const gchar *sakura_task_status_color(SakuraTaskStatus status);
SakuraWorkspaceModel *sakura_workspace_model_new(void);
void sakura_workspace_model_free(SakuraWorkspaceModel *model);
gboolean sakura_workspace_model_set_root(SakuraWorkspaceModel *model,
                                          SakuraGroup *root_group);
gboolean sakura_workspace_model_restore_snapshot(
	SakuraWorkspaceModel *model, const SakuraSessionSnapshot *snapshot);
/* Agent snapshots intentionally contain only agent-owned workspace entities.
 * Merge them without treating desktop-owned pages/layouts as missing. */
gboolean sakura_workspace_model_merge_agent_snapshot(
	SakuraWorkspaceModel *model, const SakuraSessionSnapshot *snapshot);
GPtrArray *sakura_workspace_model_ordered_groups(
	const SakuraWorkspaceModel *model);
GPtrArray *sakura_workspace_model_ordered_tasks(
	const SakuraWorkspaceModel *model);
SakuraTask *sakura_workspace_model_find_task(SakuraWorkspaceModel *model,
                                              const gchar *id);
SakuraGroup *sakura_group_new(const gchar *id, const gchar *title,
                              SakuraGroup *parent);
SakuraGroup *sakura_workspace_model_group_for_task(SakuraWorkspaceModel *model,
                                                    SakuraTask *task);
SakuraGroup *sakura_workspace_model_group_for_session(
	SakuraWorkspaceModel *model, SakuraSession *session);
void sakura_group_free(SakuraGroup *group);
gboolean sakura_workspace_model_add_group(SakuraWorkspaceModel *model,
                                           SakuraGroup *group);
gboolean sakura_workspace_model_can_remove_group(SakuraWorkspaceModel *model,
                                                  SakuraGroup *group);
gboolean sakura_workspace_model_remove_group(SakuraWorkspaceModel *model,
                                              SakuraGroup *group);
gboolean sakura_workspace_model_add_task(SakuraWorkspaceModel *model,
                                          SakuraTask *task);
gboolean sakura_workspace_model_can_remove_task(SakuraWorkspaceModel *model,
                                                 SakuraTask *task);
gboolean sakura_workspace_model_remove_task(SakuraWorkspaceModel *model,
                                             SakuraTask *task);
gboolean sakura_workspace_model_move_page_to_group(SakuraWorkspaceModel *model,
                                                    SakuraPage *page,
                                                    SakuraGroup *group);
gboolean sakura_workspace_model_attach_page(SakuraWorkspaceModel *model,
                                             SakuraTask *task,
                                             SakuraPage *page);
gboolean sakura_workspace_model_detach_page(SakuraWorkspaceModel *model,
                                             SakuraPage *page);
gboolean sakura_workspace_model_reorder_group(SakuraWorkspaceModel *model,
                                               SakuraGroup *source,
                                               SakuraGroup *target,
                                               gboolean after);
gboolean sakura_workspace_model_can_move_group(SakuraWorkspaceModel *model,
                                                SakuraGroup *source,
                                                SakuraGroup *parent,
                                                SakuraGroup *target);
gboolean sakura_workspace_model_move_group(SakuraWorkspaceModel *model,
                                            SakuraGroup *source,
                                            SakuraGroup *parent,
                                            SakuraGroup *target,
                                            gboolean after);
gboolean sakura_workspace_model_append_task(SakuraWorkspaceModel *model,
                                             SakuraTask *task,
                                             SakuraGroup *group);
gboolean sakura_workspace_model_reorder_task(SakuraWorkspaceModel *model,
                                              SakuraTask *source,
                                              SakuraTask *target,
                                              gboolean after);
gboolean sakura_workspace_model_group_is_archived(const SakuraWorkspaceModel *model,
                                                   const SakuraGroup *group);
gboolean sakura_workspace_model_task_is_archived(const SakuraWorkspaceModel *model,
                                                  const SakuraTask *task);
void sakura_workspace_model_set_group_archived(SakuraWorkspaceModel *model,
                                                SakuraGroup *group,
                                                gboolean archived);
void sakura_workspace_model_set_task_archived(SakuraWorkspaceModel *model,
                                               SakuraTask *task,
                                               gboolean archived);
SakuraSessionSnapshot *sakura_workspace_model_snapshot_new(
	const SakuraWorkspaceModel *model, gboolean sidebar_visible,
	gint sidebar_width);
void sakura_task_update_row(SakuraTask *task);
void sakura_task_attach_page(SakuraTask *task, SakuraPage *page);
void sakura_task_detach_page(SakuraPage *page);
void sakura_task_free(SakuraTask *task);
void sakura_session_mark_dirty(void);
void sakura_session_flush(void);
void sakura_session_save_shutdown(void);
gboolean sakura_session_write_snapshot(SakuraApp *app,
                                       const SakuraSessionSnapshot *snapshot);
gboolean sakura_session_load_file(SakuraApp *app, gboolean restore_session);
void sakura_session_prepare_bash_integration(SakuraApp *app);
void sakura_sanitize_working_directory(void);
void sakura_startup_init_ui(void);
void sakura_startup_begin(const SakuraStartupOptions *options,
                          SakuraStartupFinishedCallback callback,
                          gpointer data);
void sakura_startup_stop(void);
void sakura_startup_terminal_start_pending(SakuraApp *app, gboolean pending);
/* The sakura_agent_* functions are the desktop backend boundary. GTK code
 * uses this facade and does not depend on socket framing or protobuf-c. */
gboolean sakura_agent_start(SakuraApp *app);
void sakura_agent_apply_pending_snapshot(SakuraApp *app);
struct sakura_agent_terminal_start_result {
	SakuraApp *app;
	gchar *requested_terminal_id;
	gboolean attached;
	gchar *created_terminal_id;
	guint8 *replay_data;
	gsize replay_length;
	guint64 replay_start_offset;
	guint64 replay_end_offset;
	guint attached_cols;
	guint attached_rows;
	guint attached_status;
	GError *error;
};
typedef void (*SakuraAgentTerminalStartCallback)(
	SakuraAgentTerminalStartResult *result, gpointer data);
gboolean sakura_agent_start_terminal_async(
	SakuraApp *app, const gchar *terminal_id, const gchar *page_id,
	const gchar *group_id,
	const gchar *task_id, const gchar *cwd, guint cols, guint rows,
	SakuraTabKind kind, const gchar *resume_session_id,
	const gchar *model,
	const gchar *reasoning_effort, const gchar *tracking_token,
	guint order, gboolean has_order,
	SakuraAgentTerminalStartCallback callback, gpointer data,
	GError **error);
void sakura_agent_terminal_start_result_free(
	SakuraAgentTerminalStartResult *result);
gboolean sakura_agent_create_group(SakuraApp *app, const gchar *parent_id,
                                   const gchar *title, const gchar *directory,
                                   GError **error);
gboolean sakura_agent_update_group(SakuraApp *app, const gchar *group_id,
                                   const gchar *title, const gchar *directory,
                                   GError **error);
gboolean sakura_agent_move_group(SakuraApp *app, const gchar *group_id,
                                 const gchar *parent_id, const gchar *target_id,
                                 gboolean after, GError **error);
gboolean sakura_agent_set_group_archived(SakuraApp *app,
                                         const gchar *group_id,
                                         gboolean archived, GError **error);
gboolean sakura_agent_delete_group(SakuraApp *app, const gchar *group_id,
                                   GError **error);
gboolean sakura_agent_create_task(SakuraApp *app, const gchar *group_id,
                                  const gchar *parent_id, const gchar *title,
                                  const gchar *provider,
                                  const gchar *external_id, const gchar *url,
                                  GError **error);
gboolean sakura_agent_update_task(SakuraApp *app, const gchar *task_id,
                                  const gchar *title, GError **error);
gboolean sakura_agent_move_task(SakuraApp *app, const gchar *task_id,
	                             const gchar *group_id,
	                             const gchar *parent_id,
	                             const gchar *target_id,
	                             gboolean after, GError **error);
gboolean sakura_agent_update_page(SakuraApp *app, const gchar *page_id,
                                  const gchar *group_id, const gchar *task_id,
                                  const gchar *title, gboolean title_set_by_user,
                                  gboolean archived, GError **error);
gboolean sakura_agent_move_page(SakuraApp *app, const gchar *page_id,
                                const gchar *group_id, const gchar *task_id,
                                GError **error);
gboolean sakura_agent_rename_page(SakuraApp *app, const gchar *page_id,
                                  const gchar *title,
                                  gboolean title_set_by_user, GError **error);
gboolean sakura_agent_set_page_archived(SakuraApp *app,
                                        const gchar *page_id,
                                        gboolean archived, GError **error);
gboolean sakura_agent_set_task_status(SakuraApp *app, const gchar *task_id,
	                                  SakuraTaskStatus status, GError **error);
gboolean sakura_agent_delete_page(SakuraApp *app, const gchar *page_id,
                                  GError **error);
gboolean sakura_agent_set_task_archived(SakuraApp *app,
                                        const gchar *task_id,
                                        gboolean archived, GError **error);
gboolean sakura_agent_delete_task(SakuraApp *app, const gchar *task_id,
                                  GError **error);
gboolean sakura_agent_create_terminal(
	SakuraApp *app, const gchar *requested_terminal_id,
	const gchar *page_id, const gchar *group_id, const gchar *task_id,
	const gchar *cwd,
	guint cols, guint rows, gchar **created_terminal_id, GError **error);
gboolean sakura_agent_restart_terminal(
	SakuraApp *app, const gchar *terminal_id, const gchar *page_id,
	const gchar *group_id,
	const gchar *task_id, const gchar *cwd, guint cols, guint rows,
	SakuraTabKind kind, const gchar *resume_session_id,
	const gchar *model,
	const gchar *reasoning_effort, const gchar *tracking_token,
	guint order, gboolean has_order,
	GError **error);
gboolean sakura_agent_attach_terminal(
	SakuraApp *app, const gchar *terminal_id, guint cols, guint rows,
	guint8 **replay_data, gsize *replay_length, guint *attached_cols,
	guint *attached_rows, guint *status, GError **error);
gboolean sakura_agent_attach_terminal_after_offset(
	SakuraApp *app, const gchar *terminal_id, guint cols, guint rows,
	guint64 after_output_offset, guint8 **replay_data, gsize *replay_length,
	guint64 *replay_start_offset, guint64 *replay_end_offset,
	guint *attached_cols, guint *attached_rows, guint *status, GError **error);
gboolean sakura_agent_attach_terminal_from_oldest(
	SakuraApp *app, const gchar *terminal_id, guint cols, guint rows,
	guint8 **replay_data, gsize *replay_length, guint64 *replay_start_offset,
	guint64 *replay_end_offset, guint *attached_cols, guint *attached_rows,
	guint *status, GError **error);
gboolean sakura_agent_terminal_input(SakuraApp *app, const gchar *terminal_id,
                                     const guint8 *data, gsize data_length,
                                     GError **error);
gboolean sakura_agent_terminal_resize(SakuraApp *app,
                                      const gchar *terminal_id,
                                      guint cols, guint rows, GError **error);
gboolean sakura_agent_close_terminal(SakuraApp *app, const gchar *terminal_id,
                                     GError **error);
gboolean sakura_agent_detach_terminal(SakuraApp *app, const gchar *terminal_id,
                                       GError **error);
void sakura_agent_stop(SakuraApp *app);

const gchar *sakura_tool_label(SakuraToolKind tool);
const gchar *sakura_tool_id(SakuraToolKind tool);
SakuraToolKind sakura_tool_from_id(const gchar *id);
const gchar *sakura_tool_executable(SakuraToolKind tool);
const gchar *sakura_tool_icon_name(SakuraToolKind tool);
gboolean sakura_tool_requires_git_repository(SakuraToolKind tool);
gchar *sakura_find_tool_executable(SakuraToolKind tool);
gboolean sakura_tool_is_available(SakuraToolKind tool);
SakuraTab *sakura_find_tool_tab(SakuraToolKind tool, const gchar *cwd);
SakuraTab *sakura_find_tool_target_tab(SakuraToolKind tool, const gchar *target);


#endif /* SAKURA_PRIVATE_H */
