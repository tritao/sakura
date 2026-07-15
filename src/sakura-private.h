#ifndef SAKURA_PRIVATE_H
#define SAKURA_PRIVATE_H

#include <stdbool.h>

#include <glib.h>
#include <gtk/gtk.h>
#include <vte/vte.h>

#ifdef HAVE_WEBKITGTK
#include <webkit2/webkit2.h>
#endif

#define PALETTE_SIZE 16
#define NUM_COLORSETS 6

typedef struct sakura_app SakuraApp;
typedef struct sakura_sidebar_node SakuraSidebarNode;
typedef struct sakura_tab SakuraTab;
typedef struct sakura_session_snapshot SakuraSessionSnapshot;

struct sakura_codex_name_query;

typedef enum {
	SHOW_TAB_BAR_ALWAYS,
	SHOW_TAB_BAR_MULTIPLE,
	SHOW_TAB_BAR_NEVER
} ShowTabBar;

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
	SAKURA_OPEN_HERE_FILE_MANAGER,
	SAKURA_OPEN_HERE_EDITOR
} SakuraOpenHereKind;

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
	SAKURA_CODEX_TRACKING_MISSING,
	SAKURA_CODEX_TRACKING_PARTIAL,
	SAKURA_CODEX_TRACKING_ENABLED
} SakuraCodexTrackingState;

typedef enum {
	SAKURA_SIDEBAR_GROUP,
	SAKURA_SIDEBAR_TERMINAL
} SakuraSidebarNodeType;

typedef struct {
	const gchar *execute_command;
	gchar **xterm_args;
	gboolean login_shell;
	gboolean hold;
	gboolean execute_on_existing_tabs;
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

struct sakura_app {
	GtkWidget *main_window;
	GtkWidget *header_bar;
	GtkWidget *sidebar_paned;
	GtkWidget *sidebar;
	GtkWidget *sidebar_title;
	GtkWidget *sidebar_tree;
	GtkTreeStore *sidebar_model;
	GtkTreeSelection *sidebar_selection;
	SakuraSidebarNode *sidebar_root;
	GList *sidebar_groups;
	guint sidebar_next_group_id;
	gboolean sidebar_syncing;
	GtkTreeRowReference *sidebar_pending_selection;
	guint sidebar_selection_source_id;
	gboolean sidebar_visible;
	gint sidebar_width;
	GtkWidget *notebook;
	GPtrArray *tabs;               /* Stable tab ownership, in notebook order */
	GtkWidget *content_box;
	GtkWidget *tab_bar_shell;
	GtkWidget *tab_bar_scope_label;
	GtkWidget *tab_bar_scrolled;
	GtkWidget *tab_bar;
	GtkWidget *tab_bar_new_button;
	GtkWidget *tab_bar_empty;
	SakuraSidebarNode *active_group_scope;
	SakuraTab *active_tab;
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
	bool resized;
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
	bool session_dirty;
	bool session_new_window;
	guint session_save_source_id;
	guint codex_tracking_source_id;
	guint cwd_tracking_source_id;
	guint sidebar_spinner_source_id;
	guint sidebar_spinner_pulse;
	GSubprocess *codex_name_helper_process;
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
	GKeyFile *cfg;
	GKeyFile *session_cfg;
	SakuraSessionSnapshot *session_snapshot;
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
	gchar *tooltip;
	gchar *last_terminal_id;
	SakuraSidebarNode *parent;
	SakuraTab *tab;
	GtkTreeRowReference *row;
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
	GtkWidget *scrollbar;
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
	gchar *terminal_id;
	SakuraTabKind kind;
	SakuraToolKind tool;
	gchar *tool_target;
	gchar *codex_session_id;
	gchar *codex_session_name;
	gchar *codex_tracking_token;
	gboolean codex_name_query_active;
	guint codex_name_retry_source_id;
	guint codex_name_retry_count;
	gboolean codex_interrupt_requested;
	SakuraTabStatus status;
	gboolean attention;
	gint64 attention_timestamp;
	gboolean text_selection_mode;
	gboolean hold;
	SakuraSidebarNode *sidebar_node;
};

extern SakuraApp sakura;

const gchar *sakura_tab_status_label(SakuraTabStatus status);
const gchar *sakura_tab_status_color(SakuraTabStatus status);
const gchar *sakura_tab_status_symbol(SakuraTabStatus status);
gboolean sakura_tab_is_current(SakuraTab *tab);
SakuraTab *sakura_tab_new(void);
void sakura_tab_free(SakuraTab *tab);
SakuraTab *sakura_tab_for_vte(VteTerminal *vte);
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
void sakura_tab_title_changed_cb(GtkWidget *widget, void *data);
gboolean sakura_tab_is_in_active_scope(SakuraTab *tab);
SakuraSidebarNode *sakura_sidebar_default_parent(void);
void sakura_sidebar_set_scope(SakuraSidebarNode *scope);
void sakura_sidebar_add_terminal(SakuraTab *tab, SakuraSidebarNode *parent);
void sakura_select_scope_default(void);
void sakura_remember_current_scope_tab(SakuraTab *tab);
void sakura_focus_tab(SakuraTab *tab);
void sakura_register_codex_icon(void);
void sakura_set_size(void);
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
void sakura_tab_clear_attention(SakuraTab *tab);
void sakura_tab_restore_state(SakuraTab *tab, SakuraTabStatus status,
                              gboolean attention, gint64 attention_timestamp);
SakuraTab *sakura_tab_at_page(gint page);
gint sakura_page_for_tab(SakuraTab *tab);
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
void sakura_new_tab_cb(GtkWidget *widget, void *data);
void sakura_new_codex_cb(GtkWidget *widget, void *data);
void sakura_resume_codex_cb(GtkWidget *widget, void *data);
void sakura_attach_codex_cb(GtkWidget *widget, void *data);
void sakura_refresh_codex_name_cb(GtkWidget *widget, void *data);
void sakura_rename_codex_session_cb(GtkWidget *widget, void *data);
void sakura_install_codex_hook_cb(GtkWidget *widget, void *data);
void sakura_close_tab_cb(GtkWidget *widget, void *data);
void sakura_sidebar_init(gboolean restore_session);
void sakura_sidebar_selection_changed_cb(GtkTreeSelection *selection, void *data);
gboolean sakura_sidebar_button_press_cb(GtkWidget *widget, GdkEventButton *event,
                                         void *data);
void sakura_sidebar_new_group_cb(GtkWidget *widget, void *data);
void sakura_sidebar_model_reordered_cb(GtkTreeModel *model, GtkTreePath *path,
                                       GtkTreeIter *iter, gint *new_order,
                                       void *data);
void sakura_sidebar_toggle_cb(GtkWidget *widget, void *data);
void sakura_sidebar_paned_position_cb(GObject *object, GParamSpec *pspec,
                                      void *data);
void sakura_sidebar_collect_terminals(GtkTreeModel *model, GtkTreeIter *parent,
                                      GPtrArray *terminals);
SakuraSidebarNode *sakura_sidebar_find_group_by_id(const gchar *id);
void sakura_sidebar_collect_groups(GtkTreeModel *model, GtkTreeIter *parent,
                                   GPtrArray *ids, GPtrArray *parents,
                                   GPtrArray *titles);
void sakura_sidebar_sync_parents(void);
SakuraSessionSnapshot *sakura_workspace_snapshot_new(void);
gboolean sakura_workspace_restore_snapshot(SakuraSessionSnapshot *snapshot);
gboolean sakura_cwd_tracking_poll_cb(gpointer data);
void sakura_set_name_dialog_cb(GtkWidget *widget, void *data);
void sakura_setname_entry_changed_cb(GtkWidget *widget, void *data);
void sakura_copy_cb(GtkWidget *widget, void *data);
void sakura_paste_cb(GtkWidget *widget, void *data);
void sakura_select_text_cb(GtkWidget *widget, void *data);
void sakura_search_dialog(void);
void sakura_close_tab(gint page);
void sakura_tab_move_relative(gint direction);
void sakura_new_tool_cb(GtkWidget *widget, void *data);
void sakura_open_pr_cb(GtkWidget *widget, void *data);
void sakura_open_here_cb(GtkWidget *widget, void *data);
void sakura_open_url_cb(GtkWidget *widget, void *data);
void sakura_open_mail_cb(GtkWidget *widget, void *data);
void sakura_copy_url_cb(GtkWidget *widget, void *data);
void sakura_copy_pr_url_cb(GtkWidget *widget, void *data);
void sakura_tab_spawn_tool(SakuraTab *tab, const gchar *cwd, gchar **env);
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
                                 const gchar *restore_tool_target,
                                 const gchar *restore_terminal_id,
                                 const SakuraTabLaunchConfig *launch_config);
void sakura_tab_delete_page(gint page);
gboolean sakura_codex_session_id_is_uuid(const gchar *value);
gboolean sakura_codex_tracking_poll_cb(gpointer data);
SakuraTab *sakura_find_codex_tab_by_tracking_token(const gchar *token);
gchar *sakura_find_codex_name_helper(void);
void sakura_codex_name_helper_shutdown(void);
void sakura_codex_sync_name(SakuraTab *tab);
void sakura_codex_set_name_async(SakuraTab *tab, const gchar *name);
SakuraCodexTrackingState sakura_codex_tracking_state(void);
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
void sakura_add_tab_with_options(const gchar *restore_cwd,
                                 SakuraSidebarNode *sidebar_parent,
                                 const gchar *restore_title,
                                 gboolean restore_title_set,
                                 SakuraTabKind restore_kind,
                                 SakuraToolKind restore_tool,
                                 const gchar *restore_codex_session,
                                 const gchar *restore_codex_name,
                                 const gchar *restore_tool_target,
                                 const gchar *restore_terminal_id);
void sakura_sidebar_update_tab(SakuraTab *tab);
void sakura_sidebar_update_attention_count(void);
gboolean sakura_sidebar_get_iter(SakuraSidebarNode *node, GtkTreeIter *iter);
void sakura_sidebar_free_node(SakuraSidebarNode *node);
void sakura_sidebar_insert_node(SakuraSidebarNode *node);
SakuraSidebarNode *sakura_sidebar_selected_node(void);
SakuraSidebarNode *sakura_sidebar_selected_group(void);
void sakura_sidebar_remove_tab(SakuraTab *tab);
void sakura_sidebar_queue_select_node(SakuraSidebarNode *node);
void sakura_sidebar_cancel_pending_selection(void);
void sakura_sidebar_set_node_row(SakuraSidebarNode *node, GtkTreeIter *iter);
gboolean sakura_sidebar_spinner_pulse_cb(gpointer data);
void sakura_session_mark_dirty(void);
void sakura_session_flush(void);
gboolean sakura_session_write_snapshot(SakuraApp *app,
                                       const SakuraSessionSnapshot *snapshot);
gboolean sakura_session_load_file(SakuraApp *app, gboolean restore_session);
void sakura_session_prepare_bash_integration(SakuraApp *app);

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

typedef struct {
	gchar *id;
	gchar *parent_id;
	gchar *title;
} SakuraSessionGroupRecord;

typedef struct {
	gchar *parent_id;
	gchar *cwd;
	gchar *title;
	gchar *terminal_id;
	gchar *tool_id;
	gchar *tool_target;
	gchar *codex_session_id;
	gchar *codex_session_name;
	SakuraTabKind kind;
	gboolean title_set_by_user;
	SakuraTabStatus status;
	gboolean attention;
	gint64 attention_timestamp;
} SakuraSessionTabRecord;

struct sakura_session_snapshot {
	GPtrArray *groups;
	GPtrArray *tabs;
	gint selected_terminal;
	gchar *selected_terminal_id;
	gchar *active_group_id;
	gboolean sidebar_visible;
	gint sidebar_width;
};

SakuraSessionSnapshot *sakura_session_snapshot_new(void);
void sakura_session_snapshot_free(SakuraSessionSnapshot *snapshot);
gboolean sakura_session_snapshot_load(GKeyFile *key_file,
                                      SakuraSessionSnapshot *snapshot,
                                      GError **error);
void sakura_session_snapshot_save(const SakuraSessionSnapshot *snapshot,
                                  GKeyFile *key_file);

#endif /* SAKURA_PRIVATE_H */
