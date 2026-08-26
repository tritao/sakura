#include <libintl.h>
#include <stdarg.h>

#include "sakura-private.h"

#define _(String) gettext(String)
#define CODEX_ICON_NAME "sakura-codex"
#define SAKURA_CONFIG_GROUP "sakura"

static void sakura_sidebar_save_groups(void);
static void sakura_sidebar_rename_group_cb(GtkWidget *widget, void *data);
static void sakura_sidebar_delete_group_cb(GtkWidget *widget, void *data);
static void sakura_sidebar_archive_cb(GtkWidget *widget, void *data);
static void sakura_sidebar_show_archived_cb(GtkWidget *widget, void *data);
static void sakura_sidebar_set_directory_cb(GtkWidget *widget, void *data);
static void sakura_sidebar_clear_directory_cb(GtkWidget *widget, void *data);
static void sakura_sidebar_close_page_cb(GtkWidget *widget, gpointer data);
static void sakura_sidebar_collapse_all_cb(GtkWidget *widget, void *data);
static void sakura_sidebar_rename_task_cb(GtkWidget *widget, void *data);
static void sakura_sidebar_delete_task_cb(GtkWidget *widget, void *data);
static void sakura_sidebar_move_page_to_task_cb(GtkWidget *widget, void *data);
static void sakura_sidebar_task_status_cb(GtkWidget *widget, void *data);
static SakuraPage *sakura_sidebar_first_task_page(SakuraTask *task);

static SakuraSidebarNode *sakura_sidebar_group_ancestor(SakuraSidebarNode *node);
static SakuraGroup *sakura_group_for_sidebar_node(SakuraSidebarNode *node);
static SakuraGroup *sakura_workspace_group_by_id(const gchar *id);
static const gchar *sakura_workspace_group_directory(SakuraGroup *group);
static void sakura_sidebar_group_id_data_free(gpointer data,
                                              GClosure *closure);
static void sakura_sidebar_report_move_failure(const gchar *message);
struct sakura_sidebar_action_target {
	SakuraSidebarNodeType type;
	gchar *id;
};
static struct sakura_sidebar_action_target *
sakura_sidebar_action_target_new(SakuraSidebarNode *node);
static void sakura_sidebar_action_target_free(gpointer data,
                                              GClosure *closure);
static void sakura_sidebar_action_target_destroy(gpointer data);
static SakuraPage *sakura_sidebar_page_by_id(const gchar *id);
static SakuraSidebarNode *
sakura_sidebar_action_target_node(struct sakura_sidebar_action_target *target);
static void sakura_sidebar_connect_context_action(GtkWidget *item,
                                                  const gchar *signal,
                                                  GCallback callback,
                                                  SakuraSidebarNode *node);
static SakuraSidebarNode *sakura_sidebar_group_node(SakuraGroup *group);
static guint sakura_workspace_next_group_order(SakuraGroup *parent);
static guint sakura_workspace_next_task_order(SakuraGroup *group,
                                              SakuraTask *parent);
static void sakura_sidebar_refresh_group_rows(void);
static void sakura_sidebar_refresh_tab_rows(void);
static void sakura_sidebar_update_spinner_timer(void);
static void sakura_sidebar_spinner_cell_data_cb(GtkTreeViewColumn *column,
                                                 GtkCellRenderer *renderer,
                                                 GtkTreeModel *model,
                                                 GtkTreeIter *iter,
                                                 gpointer data);
static void sakura_sidebar_map_cb(GtkWidget *widget, gpointer data);
static void sakura_sidebar_unmap_cb(GtkWidget *widget, gpointer data);
static void sakura_sidebar_show_page_panes(SakuraPage *page);
static void sakura_sidebar_hide_page_panes(SakuraPage *page);
static void sakura_sidebar_add_page(SakuraPage *page, SakuraSidebarNode *parent);
static void sakura_sidebar_insert_page_node(SakuraSidebarNode *node);
static void sakura_sidebar_remove_node_row(SakuraSidebarNode *node);
static gboolean sakura_sidebar_reorder_node_to_group(
                                      SakuraSidebarNode *source,
                                      SakuraSidebarNode *target,
                                      GtkTreeViewDropPosition position);
static gboolean sakura_sidebar_reorder_task_to_sibling(
                                      SakuraSidebarNode *source,
                                      SakuraSidebarNode *target,
                                      GtkTreeViewDropPosition position);
static gboolean sakura_sidebar_move_group_to_target(
                                      SakuraSidebarNode *source,
                                      SakuraSidebarNode *target,
                                      GtkTreeViewDropPosition position);
static void sakura_sidebar_update_model_order_for_parent(GtkTreeModel *model,
                                                          GtkTreeIter *parent_iter);
static void sakura_sidebar_drag_begin_cb(GtkWidget *widget,
                                          GdkDragContext *context,
                                          gpointer data);
static void sakura_sidebar_drag_end_cb(GtkWidget *widget,
                                        GdkDragContext *context,
                                        gpointer data);
static gboolean sakura_sidebar_drag_motion_cb(GtkWidget *widget,
                                               GdkDragContext *context,
                                               gint x, gint y, guint time,
                                               gpointer data);
static gboolean sakura_sidebar_drag_drop_cb(GtkWidget *widget,
                                             GdkDragContext *context,
                                             gint x, gint y, guint time,
                                             gpointer data);
typedef enum {
	SAKURA_SIDEBAR_DROP_PAGE_RELATIVE,
	SAKURA_SIDEBAR_DROP_PAGE_TO_GROUP,
	SAKURA_SIDEBAR_DROP_TASK_RELATIVE,
	SAKURA_SIDEBAR_DROP_GROUP,
	SAKURA_SIDEBAR_DROP_NODE_TO_GROUP
} SakuraSidebarDropKind;
struct sakura_sidebar_pending_drop {
	SakuraSidebarDropKind kind;
	SakuraSidebarNodeType source_type;
	SakuraSidebarNodeType target_type;
	gchar *source_id;
	gchar *target_id;
	GtkTreeViewDropPosition position;
};
static gboolean sakura_sidebar_pending_drop_idle(gpointer data);
static void sakura_sidebar_pending_drop_free(gpointer data);
static void sakura_sidebar_row_expansion_changed_cb(GtkTreeView *tree,
                                                     GtkTreeIter *iter,
                                                     GtkTreePath *path,
                                                     gpointer data);
static gboolean sakura_sidebar_event_is_expander(GtkTreeView *tree,
                                                  GtkTreePath *path,
                                                  GtkTreeViewColumn *column,
                                                  gint x);
struct sakura_sidebar_pending_toggle {
	GtkTreeView *tree;
	SakuraSidebarNodeType type;
	gchar *id;
	gboolean expanded;
};
static gboolean sakura_sidebar_toggle_row_idle(gpointer data);
static void sakura_sidebar_pending_toggle_free(gpointer data);
static void sakura_sidebar_queue_row_toggle(GtkTreeView *tree,
	                                           GtkTreePath *path);
static gboolean sakura_sidebar_node_expansion_kind(
	SakuraSidebarNode *node, SakuraSidebarExpansionKind *kind);
static gchar *sakura_sidebar_expansion_key(SakuraSidebarExpansionKind kind,
	                                           const gchar *id);
static void sakura_sidebar_ensure_expansion_state(void);
static void sakura_sidebar_sync_expansion_state_from_view(void);
static void sakura_sidebar_restore_expansion_keys(GHashTable *expanded);
static void sakura_sidebar_reveal_node(SakuraSidebarNode *node,
	                                       gboolean expand_node);
static void sakura_sidebar_select_node_now(SakuraSidebarNode *node);
static void sakura_sidebar_remember_selection(SakuraSidebarNode *node);
static SakuraSidebarNode *sakura_sidebar_node_from_row_reference(
	GtkTreeRowReference *row);
static SakuraSidebarNode *sakura_sidebar_find_node_by_identity(
	SakuraSidebarNodeType type, const gchar *id);
static gchar *sakura_pending_restore_terminal_id = NULL;


static gboolean
sakura_sidebar_group_is_archived(SakuraGroup *group)
{
	return sakura_workspace_model_group_is_archived(sakura.workspace, group);
}


static gboolean
sakura_sidebar_task_is_archived(SakuraTask *task)
{
	return sakura_workspace_model_task_is_archived(sakura.workspace, task);
}


static gboolean
sakura_sidebar_node_is_archived(SakuraSidebarNode *node)
{
	if (node == NULL)
		return FALSE;
	if (node->type == SAKURA_SIDEBAR_GROUP)
		return sakura_sidebar_group_is_archived(node->group);
	if (node->type == SAKURA_SIDEBAR_TASK)
		return sakura_sidebar_task_is_archived(node->task);
	if (node->type == SAKURA_SIDEBAR_PAGE && node->page != NULL)
		return node->page->archived ||
		       sakura_sidebar_task_is_archived(node->page->task) ||
		       sakura_sidebar_group_is_archived(node->page->group);
	if (node->type == SAKURA_SIDEBAR_TERMINAL && node->tab != NULL &&
	    node->tab->page != NULL)
		return node->tab->page->archived ||
		       sakura_sidebar_task_is_archived(node->tab->page->task) ||
		       sakura_sidebar_group_is_archived(node->tab->page->group);
	return FALSE;
}


const gchar *
sakura_task_status_label(SakuraTaskStatus status)
{
	switch (status) {
		case SAKURA_TASK_WORKING:
			return _("Working");
		case SAKURA_TASK_BLOCKED:
			return _("Blocked");
		case SAKURA_TASK_REVIEW:
			return _("Ready for review");
		case SAKURA_TASK_DONE:
			return _("Done");
		case SAKURA_TASK_READY:
		default:
			return _("Ready");
	}
}


const gchar *
sakura_task_status_symbol(SakuraTaskStatus status)
{
	switch (status) {
		case SAKURA_TASK_WORKING:
			return "●";
		case SAKURA_TASK_BLOCKED:
			return "!";
		case SAKURA_TASK_REVIEW:
			return "◌";
		case SAKURA_TASK_DONE:
			return "✓";
		case SAKURA_TASK_READY:
		default:
			return "○";
	}
}


const gchar *
sakura_task_status_color(SakuraTaskStatus status)
{
	switch (status) {
		case SAKURA_TASK_WORKING:
			return "#4c9be8";
		case SAKURA_TASK_BLOCKED:
			return "#d55e00";
		case SAKURA_TASK_REVIEW:
			return "#8b6fcb";
		case SAKURA_TASK_DONE:
			return "#4f9d69";
		case SAKURA_TASK_READY:
		default:
			return "#777777";
	}
}


static SakuraSidebarNode *
sakura_sidebar_group_ancestor(SakuraSidebarNode *node)
{
	return sakura_sidebar_group_node(sakura_group_for_sidebar_node(node));
}


static SakuraGroup *
sakura_group_for_sidebar_node(SakuraSidebarNode *node)
{
	while (node != NULL) {
		if (node->type == SAKURA_SIDEBAR_GROUP && node->group != NULL)
			return node->group;
		if (node->type == SAKURA_SIDEBAR_TASK && node->task != NULL &&
		    node->task->group != NULL)
			return node->task->group;
		if (node->type == SAKURA_SIDEBAR_PAGE && node->page != NULL &&
		    node->page->group != NULL)
			return node->page->group;
		if (node->type == SAKURA_SIDEBAR_TERMINAL && node->tab != NULL &&
		    node->tab->page != NULL && node->tab->page->group != NULL)
			return node->tab->page->group;
		node = node->parent;
	}
	return sakura.workspace->root_group;
}


static SakuraGroup *
sakura_workspace_group_by_id(const gchar *id)
{
	GList *link;

	if (sakura.workspace == NULL || id == NULL || id[0] == '\0')
		return NULL;
	if (g_strcmp0(id, "root") == 0)
		return sakura.workspace->root_group;
	for (link = sakura.workspace->groups; link != NULL; link = link->next) {
		SakuraGroup *group = link->data;

		if (group != NULL && g_strcmp0(group->id, id) == 0)
			return group;
	}
	return NULL;
}


static void
sakura_sidebar_group_id_data_free(gpointer data, GClosure *closure)
{
	(void)closure;
	g_free(data);
}


static const gchar *
sakura_workspace_group_directory(SakuraGroup *group)
{
	while (group != NULL) {
		if (group->directory != NULL && group->directory[0] != '\0')
			return group->directory;
		group = group->parent;
	}
	return NULL;
}


static struct sakura_sidebar_action_target *
sakura_sidebar_action_target_new(SakuraSidebarNode *node)
{
	struct sakura_sidebar_action_target *target;

	if (node == NULL || node->id == NULL || node->id[0] == '\0')
		return NULL;
	target = g_new0(struct sakura_sidebar_action_target, 1);
	target->type = node->type;
	target->id = g_strdup(node->id);
	return target;
}


static void
sakura_sidebar_action_target_free(gpointer data, GClosure *closure)
{
	struct sakura_sidebar_action_target *target = data;

	(void)closure;
	if (target == NULL)
		return;
	g_free(target->id);
	g_free(target);
}


static void
sakura_sidebar_action_target_destroy(gpointer data)
{
	sakura_sidebar_action_target_free(data, NULL);
}


static SakuraSidebarNode *
sakura_sidebar_action_target_node(struct sakura_sidebar_action_target *target)
{
	SakuraGroup *group;
	SakuraTask *task;
	SakuraTab *tab;

	if (target == NULL || target->id == NULL || sakura.workspace == NULL)
		return NULL;
	switch (target->type) {
	case SAKURA_SIDEBAR_GROUP:
		group = sakura_workspace_group_by_id(target->id);
		return group != NULL ? group->sidebar_node : NULL;
	case SAKURA_SIDEBAR_TASK:
		task = sakura_workspace_model_find_task(sakura.workspace, target->id);
		return task != NULL ? task->sidebar_node : NULL;
	case SAKURA_SIDEBAR_PAGE:
		{
			SakuraPage *page = sakura_sidebar_page_by_id(target->id);
			return page != NULL ? page->sidebar_node : NULL;
		}
	case SAKURA_SIDEBAR_TERMINAL:
		tab = sakura_find_pane_by_terminal_id(target->id);
		return tab != NULL ? tab->sidebar_node : NULL;
	default:
		return NULL;
	}
}


static SakuraPage *
sakura_sidebar_page_by_id(const gchar *id)
{
	guint index;

	if (id == NULL || sakura.workspace == NULL || sakura.workspace->pages == NULL)
		return NULL;
	for (index = 0; index < sakura.workspace->pages->len; index++) {
		SakuraPage *page = g_ptr_array_index(sakura.workspace->pages, index);

		if (page != NULL && g_strcmp0(page->id, id) == 0)
			return page;
	}
	return NULL;
}


static void
sakura_sidebar_connect_context_action(GtkWidget *item, const gchar *signal,
                                      GCallback callback,
                                      SakuraSidebarNode *node)
{
	struct sakura_sidebar_action_target *target =
		sakura_sidebar_action_target_new(node);

	if (target == NULL)
		return;
	g_signal_connect_data(item, signal, callback, target,
	                      sakura_sidebar_action_target_free, 0);
}


static SakuraSidebarNode *
sakura_sidebar_group_node(SakuraGroup *group)
{
	return group != NULL && group->sidebar_node != NULL
	     ? group->sidebar_node : sakura.sidebar_root;
}


static SakuraGroup *
sakura_active_group_model(void)
{
	if (sakura.active_group_scope != NULL &&
	    sakura.active_group_scope->group != NULL)
		return sakura.active_group_scope->group;
	if (sakura.workspace->active_group != NULL)
		return sakura.workspace->active_group;
	return sakura.workspace->root_group;
}


static gboolean
sakura_task_is_within(SakuraTask *task, SakuraTask *ancestor)
{
	while (task != NULL) {
		if (task == ancestor)
			return TRUE;
		task = task->parent;
	}
	return FALSE;
}


void
sakura_task_update_row(SakuraTask *task)
{
	SakuraSidebarNode *node;
	GtkTreeIter iter;
	gchar *subtitle;

	if (task == NULL || task->sidebar_node == NULL)
		return;
	node = task->sidebar_node;
	if (task->external_id != NULL && task->external_id[0] != '\0') {
		if (task->provider != NULL && task->provider[0] != '\0')
			subtitle = g_strdup_printf("%s · %s", task->provider, task->external_id);
		else
			subtitle = g_strdup(task->external_id);
	} else {
		subtitle = g_strdup(sakura_task_status_label(task->status));
	}
	g_free(node->title);
	g_free(node->subtitle);
	g_free(node->tooltip);
	node->title = g_strdup(task->title != NULL ? task->title : "");
	node->subtitle = subtitle;
	node->subtitle_is_directory = FALSE;
	node->tooltip = g_strdup_printf("%s\n%s", node->title,
	                                sakura_task_status_label(task->status));
	if (sakura_sidebar_get_iter(node, &iter))
		sakura_sidebar_set_node_row(node, &iter);
}


gboolean
sakura_sidebar_sync_page_to_agent(SakuraPage *page, SakuraGroup *group,
                                  SakuraTask *task)
{
	GError *error = NULL;
	gboolean result;

	if (sakura.agent_socket_path == NULL || page == NULL ||
	    page->id == NULL || page->id[0] == '\0')
		return TRUE;
	if (task != NULL)
		group = task->group;
	result = sakura_agent_update_page(
		&sakura, page->id, group != NULL ? group->id : NULL,
		task != NULL ? task->id : NULL, page->title, page->title_set_by_user,
		page->archived, &error);
	if (!result) {
		if (error != NULL)
			g_warning("Could not update page through sakura-agent: %s",
			          error->message);
		g_clear_error(&error);
	}
	return result;
}


static gboolean
sakura_sidebar_move_page_in_agent(SakuraPage *page, SakuraGroup *group,
                                  SakuraTask *task)
{
	GError *error = NULL;
	gboolean result;

	if (sakura.agent_socket_path == NULL || page == NULL)
		return TRUE;
	if (task != NULL)
		group = task->group;
	result = sakura_agent_move_page(
		&sakura, page->id, group != NULL ? group->id : "root",
		task != NULL ? task->id : NULL, &error);
	if (!result && error != NULL)
		g_warning("Could not move page through sakura-agent: %s",
		          error->message);
	g_clear_error(&error);
	return result;
}


void
sakura_task_attach_page(SakuraTask *task, SakuraPage *page)
{
	SakuraSidebarNode *node, *parent;
	SakuraGroup *group;

	if (task == NULL || page == NULL)
		return;
	group = sakura_workspace_model_group_for_task(sakura.workspace, task);
	if (page->task == task)
		return;
	if (!sakura_sidebar_move_page_in_agent(page, group, task))
		return;
	sakura_workspace_begin_mutation();
	node = page->sidebar_node;
	parent = task->sidebar_node != NULL ? task->sidebar_node
	                                  : sakura_sidebar_group_node(group);
	sakura_sidebar_cancel_pending_selection();
	sakura_sidebar_hide_page_panes(page);
	if (!sakura_workspace_model_attach_page(sakura.workspace, task, page)) {
		sakura_workspace_end_mutation();
		return;
	}
	if (node != NULL) {
		sakura_sidebar_remove_node_row(node);
		if (parent != NULL) {
			node->parent = parent;
			sakura_sidebar_insert_page_node(node);
			sakura_sidebar_show_page_panes(page);
		} else
			sakura_sidebar_free_node(node);
	}
	sakura_sidebar_update_page(page);
	sakura.workspace->active_task = task;
	sakura_sidebar_set_scope(sakura_sidebar_group_node(group));
	sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_STRUCTURE |
	                              SAKURA_WORKSPACE_CHANGE_SCOPE |
	                              SAKURA_WORKSPACE_CHANGE_SELECTION);
	sakura_session_mark_dirty();
	sakura_workspace_end_mutation();
}


void
sakura_task_detach_page(SakuraPage *page)
{
	SakuraSidebarNode *node, *group_node;
	SakuraGroup *group;
	SakuraTask *old_task;

	if (page == NULL || page->task == NULL)
		return;
	sakura_workspace_begin_mutation();
	node = page->sidebar_node;
	old_task = page->task;
	group = old_task != NULL ? sakura_workspace_model_group_for_task(sakura.workspace, old_task)
	                         : page->group;
	group_node = sakura_sidebar_group_node(group);
	if (!sakura_sidebar_move_page_in_agent(page, group, NULL))
		return;
	sakura_sidebar_cancel_pending_selection();
	sakura_sidebar_hide_page_panes(page);
	if (!sakura_workspace_model_detach_page(sakura.workspace, page)) {
		sakura_workspace_end_mutation();
		return;
	}
	if (sakura.workspace->active_task == old_task)
		sakura.workspace->active_task = NULL;
	if (node != NULL) {
		sakura_sidebar_remove_node_row(node);
		if (group_node != NULL) {
			node->parent = group_node;
			sakura_sidebar_insert_page_node(node);
			sakura_sidebar_show_page_panes(page);
		} else
			sakura_sidebar_free_node(node);
	}
	sakura_sidebar_update_page(page);
	sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_STRUCTURE |
	                              SAKURA_WORKSPACE_CHANGE_SCOPE);
	sakura_session_mark_dirty();
	sakura_workspace_end_mutation();
}


gchar *
sakura_sidebar_directory_for_node(SakuraSidebarNode *node)
{
	SakuraGroup *group;

	if (node == NULL)
		node = sakura_active_group_model() != NULL
	      ? sakura_sidebar_group_node(sakura_active_group_model()) : sakura.sidebar_root;
	group = sakura_group_for_sidebar_node(node);
	while (group != NULL) {
		if (group->directory != NULL && group->directory[0] != '\0' &&
		    g_file_test(group->directory, G_FILE_TEST_IS_DIR))
			return g_strdup(group->directory);
		group = group->parent;
	}
	return NULL;
}


static const gchar *
sakura_sidebar_group_directory(SakuraSidebarNode *node)
{
	SakuraGroup *group = sakura_group_for_sidebar_node(node);

	while (group != NULL) {
		if (group->directory != NULL && group->directory[0] != '\0')
			return group->directory;
		group = group->parent;
	}
	return NULL;
}


static gchar *
sakura_sidebar_directory_display(const gchar *directory)
{
	const gchar *home;

	if (directory == NULL || directory[0] == '\0')
		return NULL;
	home = g_get_home_dir();
	if (home != NULL && g_str_has_prefix(directory, home) &&
	    (directory[strlen(home)] == '\0' || directory[strlen(home)] == G_DIR_SEPARATOR))
		return g_strdup_printf("~%s", directory + strlen(home));
	return g_strdup(directory);
}


static gboolean
sakura_sidebar_paths_equal(const gchar *first, const gchar *second)
{
	gchar *canonical_first, *canonical_second;
	gboolean equal;

	if (first == NULL || first[0] == '\0' || second == NULL || second[0] == '\0')
		return FALSE;
	canonical_first = g_canonicalize_filename(first, NULL);
	canonical_second = g_canonicalize_filename(second, NULL);
	equal = g_strcmp0(canonical_first, canonical_second) == 0;
	g_free(canonical_first);
	g_free(canonical_second);
	return equal;
}


static void
sakura_sidebar_update_group_row(SakuraSidebarNode *node)
{
	GtkTreeIter iter;
	SakuraGroup *group;
	const gchar *directory, *display_directory;
	gchar *display, *tooltip;

	if (node == NULL || node->type != SAKURA_SIDEBAR_GROUP || node->group == NULL)
		return;
	group = node->group;
	g_free(node->id);
	node->id = g_strdup(group->id);
	g_free(node->title);
	node->title = g_strdup(group->title);
	directory = sakura_sidebar_group_directory(node);
	/* An inherited directory is useful context, but repeating it on every
	 * nested group makes the tree read like a list of paths. Show only the
	 * directory explicitly assigned to this group; the effective path remains
	 * available in the row tooltip. */
	display_directory = group->directory;
	display = sakura_sidebar_directory_display(display_directory);
	if (group->directory != NULL && group->directory[0] != '\0')
		tooltip = g_strdup_printf(_("Working directory: %s"), group->directory);
	else if (directory != NULL)
		tooltip = g_strdup_printf(_("Inherited working directory: %s"), directory);
	else
		tooltip = g_strdup(node->title != NULL ? node->title : "");

	g_free(node->subtitle);
	node->subtitle = display != NULL ? display : g_strdup("");
	node->subtitle_is_directory = display != NULL;
	g_free(node->tooltip);
	node->tooltip = tooltip;
	if (sakura_sidebar_get_iter(node, &iter))
		sakura_sidebar_set_node_row(node, &iter);
}


static void
sakura_sidebar_refresh_group_rows(void)
{
	GList *group;

	for (group = sakura.workspace->groups; group != NULL; group = group->next) {
		SakuraGroup *model_group = group->data;
		if (model_group->sidebar_node != NULL)
			sakura_sidebar_update_group_row(model_group->sidebar_node);
	}
}


static void
sakura_sidebar_refresh_tab_rows(void)
{
	guint index;

	if (sakura.workspace->panes == NULL)
		return;
	for (index = 0; index < sakura.workspace->panes->len; index++) {
		SakuraTab *tab = g_ptr_array_index(sakura.workspace->panes, index);
		if (tab != NULL && tab->sidebar_node != NULL)
			sakura_sidebar_update_tab(tab);
	}
}


static gboolean
sakura_workspace_validation_error(GError **error, const gchar *format, ...)
{
	va_list args;
	gchar *message;

	va_start(args, format);
	message = g_strdup_vprintf(format, args);
	va_end(args);
	g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, message);
	g_free(message);
	return FALSE;
}

static gboolean
sakura_workspace_validate_sidebar_projection_iter(GtkTreeModel *model,
                                                   GtkTreeIter *parent_iter,
                                                   SakuraSidebarNode *parent_node,
                                                   gboolean *seen_root,
                                                   GError **error)
{
	GtkTreeIter iter;
	gboolean valid;
	gboolean have_group_order = FALSE, have_task_order = FALSE;
	guint previous_group_order = 0, previous_task_order = 0;

	valid = parent_iter == NULL
	      ? gtk_tree_model_get_iter_first(model, &iter)
	      : gtk_tree_model_iter_children(model, &iter, parent_iter);
	while (valid) {
		SakuraSidebarNode *node = NULL;

		gtk_tree_model_get(model, &iter, SAKURA_SIDEBAR_COLUMN_NODE, &node, -1);
		if (node == NULL)
			return sakura_workspace_validation_error(
				error, "sidebar projection contains a row without a node");
		if (node->parent != parent_node)
			return sakura_workspace_validation_error(
				error, "sidebar projection parent pointer disagrees with GTK row");

		if (node == sakura.sidebar_root) {
			if (parent_node != NULL || *seen_root ||
			    node->type != SAKURA_SIDEBAR_GROUP ||
			    node->group != sakura.workspace->root_group ||
			    sakura.workspace->root_group == NULL ||
			    sakura.workspace->root_group->parent != NULL)
				return sakura_workspace_validation_error(
					error, "sidebar root projection is invalid");
			*seen_root = TRUE;
		} else if (node->type == SAKURA_SIDEBAR_GROUP) {
			if (node->group == NULL || node->group->sidebar_node != node ||
			    parent_node == NULL || parent_node->type != SAKURA_SIDEBAR_GROUP ||
			    node->group->parent != parent_node->group)
				return sakura_workspace_validation_error(
					error, "sidebar group projection disagrees with model parent");
			if (have_group_order && node->group->order < previous_group_order)
				return sakura_workspace_validation_error(
					error, "sidebar group projection order disagrees with model order");
			previous_group_order = node->group->order;
			have_group_order = TRUE;
		} else if (node->type == SAKURA_SIDEBAR_TASK) {
			if (node->task == NULL || node->task->sidebar_node != node ||
			    node->task->group == NULL || parent_node == NULL ||
			    (parent_node->type != SAKURA_SIDEBAR_GROUP &&
			     parent_node->type != SAKURA_SIDEBAR_TASK))
				return sakura_workspace_validation_error(
					error, "sidebar task projection has an invalid parent");
			if (parent_node->type == SAKURA_SIDEBAR_GROUP) {
				if (node->task->group != parent_node->group ||
				    (node->task->parent != NULL &&
				     node->task->parent->sidebar_node != NULL))
					return sakura_workspace_validation_error(
						error, "sidebar task projection disagrees with group model");
			} else if (node->task->parent != parent_node->task ||
			           node->task->group != parent_node->task->group) {
				return sakura_workspace_validation_error(
					error, "sidebar task projection disagrees with task model");
			}
			if (have_task_order && node->task->order < previous_task_order)
				return sakura_workspace_validation_error(
					error, "sidebar task projection order disagrees with model order");
			previous_task_order = node->task->order;
			have_task_order = TRUE;
		} else if (node->type == SAKURA_SIDEBAR_PAGE) {
			if (node->page == NULL || node->page->sidebar_node != node ||
			    parent_node == NULL ||
			    (parent_node->type != SAKURA_SIDEBAR_GROUP &&
			     parent_node->type != SAKURA_SIDEBAR_TASK))
				return sakura_workspace_validation_error(
					error, "sidebar page projection has an invalid parent");
			if (parent_node->type == SAKURA_SIDEBAR_GROUP) {
				if (node->page->group != parent_node->group ||
				    (node->page->task != NULL &&
				     node->page->task->sidebar_node != NULL))
					return sakura_workspace_validation_error(
						error, "sidebar page projection disagrees with group model");
			} else if (node->page->task != parent_node->task ||
			           node->page->group != parent_node->task->group) {
				return sakura_workspace_validation_error(
					error, "sidebar page projection disagrees with task model");
			}
		} else if (node->type == SAKURA_SIDEBAR_TERMINAL) {
			if (node->tab == NULL || node->tab->sidebar_node != node ||
			    parent_node == NULL || parent_node->type != SAKURA_SIDEBAR_PAGE ||
			    node->tab->page != parent_node->page)
				return sakura_workspace_validation_error(
					error, "sidebar terminal projection has an invalid parent");
		} else {
			return sakura_workspace_validation_error(
				error, "sidebar projection contains an unknown node type");
		}

		if (node->type == SAKURA_SIDEBAR_TERMINAL &&
		    gtk_tree_model_iter_has_child(model, &iter))
			return sakura_workspace_validation_error(
				error, "sidebar terminal projection unexpectedly has children");
		if (node->type != SAKURA_SIDEBAR_TERMINAL &&
		    gtk_tree_model_iter_has_child(model, &iter) &&
		    !sakura_workspace_validate_sidebar_projection_iter(
				model, &iter, node, seen_root, error))
			return FALSE;
		valid = gtk_tree_model_iter_next(model, &iter);
	}
	return TRUE;
}


static gboolean
sakura_workspace_validate_layout_widgets(const SakuraLayoutNode *node,
                                          const SakuraPage *page,
                                          GtkWidget *expected_parent,
                                          GHashTable *seen_nodes,
                                          GHashTable *seen_tabs,
                                          GError **error)
{
	SakuraTab *tab;
	GtkWidget *child1, *child2;

	if (node == NULL)
		return sakura_workspace_validation_error(error,
		                                        "layout has a missing node");
	if (g_hash_table_contains(seen_nodes, node))
		return sakura_workspace_validation_error(error,
		                                        "layout contains a duplicate node");
	g_hash_table_add(seen_nodes, (gpointer)node);
	if (node->page != page || node->widget == NULL ||
	    gtk_widget_get_parent(node->widget) != expected_parent)
		return sakura_workspace_validation_error(error,
		                                        "layout widget has invalid ownership");

	if (node->kind == SAKURA_LAYOUT_LEAF) {
		tab = node->data.leaf.tab;
		if (tab == NULL || tab->page != page || tab->layout_leaf != node ||
		    tab->hbox == NULL || node->widget != tab->hbox ||
		    g_hash_table_contains(seen_tabs, tab))
			return sakura_workspace_validation_error(error,
			                                        "layout leaf has invalid terminal ownership");
		g_hash_table_add(seen_tabs, tab);
		return TRUE;
	}
	if (node->kind != SAKURA_LAYOUT_SPLIT || !GTK_IS_PANED(node->widget))
		return sakura_workspace_validation_error(error,
		                                        "layout split has no GtkPaned widget");
	if ((node->data.split.direction == SAKURA_SPLIT_RIGHT &&
	     gtk_orientable_get_orientation(GTK_ORIENTABLE(node->widget)) !=
	     GTK_ORIENTATION_HORIZONTAL) ||
	    (node->data.split.direction == SAKURA_SPLIT_DOWN &&
	     gtk_orientable_get_orientation(GTK_ORIENTABLE(node->widget)) !=
	     GTK_ORIENTATION_VERTICAL))
		return sakura_workspace_validation_error(error,
		                                        "layout split orientation disagrees with GtkPaned");
	if (node->data.split.first == NULL || node->data.split.second == NULL)
		return sakura_workspace_validation_error(error,
		                                        "layout split has a missing child");
	child1 = gtk_paned_get_child1(GTK_PANED(node->widget));
	child2 = gtk_paned_get_child2(GTK_PANED(node->widget));
	if (child1 != node->data.split.first->widget ||
	    child2 != node->data.split.second->widget)
		return sakura_workspace_validation_error(error,
		                                        "GtkPaned children disagree with layout order");
	return sakura_workspace_validate_layout_widgets(node->data.split.first, page,
	                                                node->widget, seen_nodes,
	                                                seen_tabs, error) &&
	       sakura_workspace_validate_layout_widgets(node->data.split.second, page,
                                                node->widget, seen_nodes,
                                                seen_tabs, error);
}


gboolean
sakura_workspace_validate(GError **error)
{
	GHashTable *seen_pages, *seen_tabs, *seen_page_ids, *seen_terminal_ids;
	gint count, current, index;

	if (sakura.notebook == NULL || sakura.workspace->pages == NULL || sakura.workspace->tabs == NULL)
		return sakura_workspace_validation_error(error,
		                                        "workspace collections are not initialized");
	count = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));
	if ((guint)count != sakura.workspace->pages->len || (guint)count != sakura.workspace->tabs->len)
		return sakura_workspace_validation_error(
			error, "notebook/pages/tabs count mismatch: %d/%u/%u",
			count, sakura.workspace->pages->len, sakura.workspace->tabs->len);
	if (sakura.sidebar_root != NULL) {
		GList *group_link;
		gboolean seen_root = FALSE;

		if (sakura.sidebar_model == NULL || sakura.workspace->root_group == NULL ||
		    sakura.sidebar_root->group != sakura.workspace->root_group ||
		    sakura.workspace->root_group->sidebar_node != sakura.sidebar_root ||
		    sakura.workspace->root_group->parent != NULL)
			return sakura_workspace_validation_error(error,
		                                        "root group model/view link is invalid");
		for (group_link = sakura.workspace->groups; group_link != NULL; group_link = group_link->next) {
			SakuraGroup *group = group_link->data;
			SakuraSidebarNode *node = group != NULL ? group->sidebar_node : NULL;

			/* Archived groups are intentionally absent from the projection while
			 * the sidebar is filtered. Their model links remain valid and must not
			 * be mistaken for a broken model/view relationship. */
			if (group != NULL && group != sakura.workspace->root_group &&
			    !sakura.show_archived &&
			    sakura_workspace_model_group_is_archived(sakura.workspace, group))
				continue;
			if (group == NULL || node == NULL || node->type != SAKURA_SIDEBAR_GROUP ||
			    node->group != group ||
			    (group != sakura.workspace->root_group && group->parent == NULL) ||
			    (group != sakura.workspace->root_group &&
			     (node->parent == NULL || node->parent->group != group->parent)))
				return sakura_workspace_validation_error(error,
				                                    "group model/view link is invalid");
		}
		if (!sakura_workspace_validate_sidebar_projection_iter(
				GTK_TREE_MODEL(sakura.sidebar_model), NULL, NULL, &seen_root, error))
			return FALSE;
		if (!seen_root)
			return sakura_workspace_validation_error(
				error, "sidebar root model exists without a GTK row");
	}
	if (sakura.workspace->tasks != NULL) {
		for (index = 0; index < (gint)sakura.workspace->tasks->len; index++) {
			SakuraTask *task = g_ptr_array_index(sakura.workspace->tasks, index);
			SakuraSidebarNode *node = task != NULL ? task->sidebar_node : NULL;

			if (task == NULL || task->id == NULL || task->id[0] == '\0' ||
			    task->group == NULL || task->parent == task)
				return sakura_workspace_validation_error(
					error, "sidebar task identity is invalid at %d", index);
			if (node == NULL)
				continue;
			if (node->type != SAKURA_SIDEBAR_TASK || node->task != task ||
			    node->parent == NULL ||
			    (node->parent->type != SAKURA_SIDEBAR_GROUP &&
			     node->parent->type != SAKURA_SIDEBAR_TASK) ||
			    (task->parent == NULL &&
			     node->parent->type == SAKURA_SIDEBAR_TASK) ||
			    (task->parent != NULL && task->parent->sidebar_node != NULL &&
			     (node->parent->type != SAKURA_SIDEBAR_TASK ||
			      node->parent->task != task->parent)) ||
			    (task->parent != NULL && task->parent->sidebar_node == NULL &&
			     (node->parent->type != SAKURA_SIDEBAR_GROUP ||
			      node->parent->group != task->group)) ||
			    task->group != sakura_group_for_sidebar_node(node))
				return sakura_workspace_validation_error(
					error, "sidebar task identity is invalid at %d", index);
		}
	}

	seen_pages = g_hash_table_new(g_direct_hash, g_direct_equal);
	seen_tabs = g_hash_table_new(g_direct_hash, g_direct_equal);
	seen_page_ids = g_hash_table_new(g_str_hash, g_str_equal);
	seen_terminal_ids = g_hash_table_new(g_str_hash, g_str_equal);
	for (index = 0; index < count; index++) {
		SakuraPage *page = sakura_page_at_page(index);
		SakuraTab *tab = sakura_tab_at_page(index);
		GError *layout_error = NULL;
		GHashTable *seen_nodes, *seen_page_tabs;
		guint pane_index;

		if (page == NULL || tab == NULL) {
			g_hash_table_destroy(seen_pages);
			g_hash_table_destroy(seen_tabs);
			g_hash_table_destroy(seen_page_ids);
			g_hash_table_destroy(seen_terminal_ids);
			return sakura_workspace_validation_error(error,
			                                        "notebook page %d has no model object", index);
		}
		if (g_hash_table_contains(seen_pages, page) ||
		    g_hash_table_contains(seen_tabs, tab)) {
			g_hash_table_destroy(seen_pages);
			g_hash_table_destroy(seen_tabs);
			g_hash_table_destroy(seen_page_ids);
			g_hash_table_destroy(seen_terminal_ids);
			return sakura_workspace_validation_error(error,
			                                        "duplicate page or representative at %d", index);
		}
		if (g_ptr_array_index(sakura.workspace->pages, index) != page ||
		    g_ptr_array_index(sakura.workspace->tabs, index) != tab ||
		    page->id == NULL || page->id[0] == '\0' || tab->terminal_id == NULL ||
		    tab->terminal_id[0] == '\0' ||
		    g_hash_table_contains(seen_page_ids, page->id) ||
		    g_hash_table_contains(seen_terminal_ids, tab->terminal_id)) {
			g_hash_table_destroy(seen_pages);
			g_hash_table_destroy(seen_tabs);
			g_hash_table_destroy(seen_page_ids);
			g_hash_table_destroy(seen_terminal_ids);
			return sakura_workspace_validation_error(error,
			                                        "notebook identity cache is invalid at %d", index);
		}
		g_hash_table_add(seen_pages, page);
		g_hash_table_add(seen_page_ids, page->id);
		g_hash_table_add(seen_terminal_ids, tab->terminal_id);
		if (page->container != gtk_notebook_get_nth_page(
				GTK_NOTEBOOK(sakura.notebook), index) ||
		    tab->page != page || page->tab_bar_tab != tab ||
		    gtk_widget_get_parent(page->container) != sakura.notebook ||
		    !gtk_widget_get_visible(page->container) || page->active_tab == NULL ||
		    page->active_tab->page != page ||
		    !sakura_layout_contains_tab(page->layout_root, page->active_tab) ||
		    !sakura_layout_contains_tab(page->layout_root, tab) ||
		    sakura_page_for_tab(tab) != index) {
			g_hash_table_destroy(seen_pages);
			g_hash_table_destroy(seen_tabs);
			g_hash_table_destroy(seen_page_ids);
			g_hash_table_destroy(seen_terminal_ids);
			return sakura_workspace_validation_error(error,
			                                        "page identity invariant failed at %d", index);
		}
		if (page->panes == NULL || page->panes->len == 0) {
			g_hash_table_destroy(seen_pages);
			g_hash_table_destroy(seen_tabs);
			g_hash_table_destroy(seen_page_ids);
			g_hash_table_destroy(seen_terminal_ids);
			return sakura_workspace_validation_error(error,
			                                        "page %d has no terminal panes", index);
		}
		if (page->layout_root == NULL ||
		    sakura_layout_tab_count(page->layout_root) != page->panes->len) {
			g_hash_table_destroy(seen_pages);
			g_hash_table_destroy(seen_tabs);
			g_hash_table_destroy(seen_page_ids);
			g_hash_table_destroy(seen_terminal_ids);
			return sakura_workspace_validation_error(error,
			                                        "page %d pane/layout count mismatch", index);
		}
		seen_page_tabs = g_hash_table_new(g_direct_hash, g_direct_equal);
		for (pane_index = 0; pane_index < page->panes->len; pane_index++) {
			SakuraTab *pane = g_ptr_array_index(page->panes, pane_index);
			if (pane == NULL || pane->page != page || pane->layout_leaf == NULL ||
			    pane->hbox == NULL || g_hash_table_contains(seen_page_tabs, pane) ||
			    pane->terminal_id == NULL || pane->terminal_id[0] == '\0' ||
			    (pane != tab &&
			     g_hash_table_contains(seen_terminal_ids, pane->terminal_id))) {
				g_hash_table_destroy(seen_page_tabs);
				g_hash_table_destroy(seen_pages);
				g_hash_table_destroy(seen_tabs);
				g_hash_table_destroy(seen_page_ids);
				g_hash_table_destroy(seen_terminal_ids);
				return sakura_workspace_validation_error(
					error, "terminal leaf widget invariant failed at %d:%u",
					index, pane_index);
			}
			g_hash_table_add(seen_page_tabs, pane);
			if (pane != tab)
				g_hash_table_add(seen_terminal_ids, pane->terminal_id);
			if (pane->sidebar_node != NULL &&
			    (page->sidebar_node == NULL ||
			     pane->sidebar_node->type != SAKURA_SIDEBAR_TERMINAL ||
			     pane->sidebar_node->tab != pane ||
			     pane->sidebar_node->parent != page->sidebar_node)) {
				g_hash_table_destroy(seen_page_tabs);
				g_hash_table_destroy(seen_pages);
				g_hash_table_destroy(seen_tabs);
				g_hash_table_destroy(seen_page_ids);
				g_hash_table_destroy(seen_terminal_ids);
				return sakura_workspace_validation_error(error,
				                                        "sidebar terminal identity failed at %d:%u",
				                                        index, pane_index);
			}
		}
		seen_nodes = g_hash_table_new(g_direct_hash, g_direct_equal);
		if (!sakura_workspace_validate_layout_widgets(page->layout_root, page,
		                                               page->container, seen_nodes,
		                                               seen_tabs, error)) {
			g_hash_table_destroy(seen_nodes);
			g_hash_table_destroy(seen_page_tabs);
			g_hash_table_destroy(seen_pages);
			g_hash_table_destroy(seen_tabs);
			g_hash_table_destroy(seen_page_ids);
			g_hash_table_destroy(seen_terminal_ids);
			return FALSE;
		}
		g_hash_table_destroy(seen_nodes);
		g_hash_table_destroy(seen_page_tabs);
		for (pane_index = 0; pane_index < page->panes->len; pane_index++) {
			SakuraTab *pane = g_ptr_array_index(page->panes, pane_index);
			if (!g_hash_table_contains(seen_tabs, pane)) {
				g_hash_table_destroy(seen_pages);
				g_hash_table_destroy(seen_tabs);
				g_hash_table_destroy(seen_page_ids);
				g_hash_table_destroy(seen_terminal_ids);
				return sakura_workspace_validation_error(error,
				                                        "layout is missing page pane at %d:%u",
				                                        index, pane_index);
			}
		}
		if (!sakura_layout_validate(page, &layout_error)) {
			g_propagate_prefixed_error(error, layout_error,
			                           "page %d layout invalid: ", index);
			g_hash_table_destroy(seen_pages);
			g_hash_table_destroy(seen_tabs);
			g_hash_table_destroy(seen_page_ids);
			g_hash_table_destroy(seen_terminal_ids);
			return FALSE;
		}
		if (page->sidebar_node != NULL &&
		    (page->sidebar_node->type != SAKURA_SIDEBAR_PAGE ||
		     page->sidebar_node->page != page ||
		     page->sidebar_node->parent == NULL ||
		     (page->sidebar_node->parent->type != SAKURA_SIDEBAR_GROUP &&
		      page->sidebar_node->parent->type != SAKURA_SIDEBAR_TASK) ||
		     (page->task == NULL &&
		      page->sidebar_node->parent->type == SAKURA_SIDEBAR_TASK) ||
		     (page->task != NULL && page->task->sidebar_node != NULL &&
		      (page->sidebar_node->parent->type != SAKURA_SIDEBAR_TASK ||
		       page->sidebar_node->parent->task != page->task)) ||
		     (page->task != NULL && page->task->sidebar_node == NULL &&
		      (page->sidebar_node->parent->type != SAKURA_SIDEBAR_GROUP ||
		       page->sidebar_node->parent->group != sakura_workspace_model_group_for_task(sakura.workspace, page->task))) ||
		     page->group != (page->task != NULL
		                     ? sakura_workspace_model_group_for_task(sakura.workspace, page->task)
		                     : sakura_group_for_sidebar_node(page->sidebar_node)) ||
		     g_strcmp0(page->sidebar_node->id, page->id) != 0)) {
			g_hash_table_destroy(seen_pages);
			g_hash_table_destroy(seen_tabs);
			g_hash_table_destroy(seen_page_ids);
			g_hash_table_destroy(seen_terminal_ids);
			return sakura_workspace_validation_error(error,
			                                        "sidebar page identity failed at %d", index);
		}
	}
	if (sakura.workspace->panes != NULL) {
		GHashTable *seen_global_panes = g_hash_table_new(g_direct_hash, g_direct_equal);
		for (index = 0; index < (gint)sakura.workspace->panes->len; index++) {
			SakuraTab *pane = g_ptr_array_index(sakura.workspace->panes, index);
			if (pane == NULL || g_hash_table_contains(seen_global_panes, pane) ||
			    pane->page == NULL || pane->layout_leaf == NULL ||
			    !sakura_layout_contains_tab(pane->page->layout_root, pane)) {
				g_hash_table_destroy(seen_global_panes);
				g_hash_table_destroy(seen_pages);
				g_hash_table_destroy(seen_tabs);
				g_hash_table_destroy(seen_page_ids);
				g_hash_table_destroy(seen_terminal_ids);
				return sakura_workspace_validation_error(error,
				                                        "global pane registry is invalid at %d", index);
			}
			g_hash_table_add(seen_global_panes, pane);
		}
		if (g_hash_table_size(seen_global_panes) !=
		    g_hash_table_size(seen_tabs)) {
			g_hash_table_destroy(seen_global_panes);
			g_hash_table_destroy(seen_pages);
			g_hash_table_destroy(seen_tabs);
			g_hash_table_destroy(seen_page_ids);
			g_hash_table_destroy(seen_terminal_ids);
			return sakura_workspace_validation_error(error,
			                                        "global pane registry count mismatch");
		}
		g_hash_table_destroy(seen_global_panes);
	}
	g_hash_table_destroy(seen_pages);
	g_hash_table_destroy(seen_tabs);
	g_hash_table_destroy(seen_page_ids);
	g_hash_table_destroy(seen_terminal_ids);

	current = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	if (sakura.workspace->active_page != NULL &&
	    (current < 0 || sakura_page_at_page(current) != sakura.workspace->active_page ||
	     sakura.workspace->active_tab == NULL || sakura.workspace->active_tab->page != sakura.workspace->active_page))
		return sakura_workspace_validation_error(error,
		                                        "active page does not match GTK current page");
	return TRUE;
}

static SakuraTab *
sakura_sidebar_page_active_tab(SakuraPage *page)
{
	return sakura_session_active_pane(page);
}


static gchar *
sakura_sidebar_page_directory_summary(SakuraPage *page, SakuraTab *active)
{
	const gchar *common_directory = NULL;
	SakuraTab *representative = NULL;
	gboolean missing_directory = FALSE;
	guint index;
	gchar *display;
	const gchar *group_directory;

	if (page == NULL || page->panes == NULL || page->panes->len <= 1)
		return g_strdup(active != NULL && active->sidebar_node != NULL &&
	                       active->sidebar_node->subtitle != NULL
	                     ? active->sidebar_node->subtitle : "");

	for (index = 0; index < page->panes->len; index++) {
		SakuraTab *tab = g_ptr_array_index(page->panes, index);

		if (tab == NULL || tab->cwd == NULL || tab->cwd[0] == '\0') {
			missing_directory = TRUE;
			continue;
		}
		if (common_directory == NULL) {
			common_directory = tab->cwd;
			representative = tab;
		} else if (!sakura_sidebar_paths_equal(common_directory, tab->cwd)) {
			return g_strdup(_("Multiple directories"));
		}
	}
	if (common_directory == NULL || missing_directory)
		return missing_directory ? g_strdup(_("Multiple directories")) : g_strdup("");

	display = sakura_sidebar_directory_display(common_directory);
	group_directory = representative != NULL && representative->sidebar_node != NULL
	                ? sakura_sidebar_group_directory(representative->sidebar_node) : NULL;
	if (group_directory != NULL &&
	    sakura_sidebar_paths_equal(common_directory, group_directory)) {
		g_free(display);
		return g_strdup("");
	}
	return display != NULL ? display : g_strdup("");
}

static const gchar *
sakura_sidebar_tab_icon_name(SakuraTab *tab)
{
	GtkIconTheme *icon_theme;
	const gchar *icon_name;

	if (tab == NULL)
		return "utilities-terminal";

	if (tab->kind == SAKURA_TAB_CODEX) {
		icon_theme = gtk_icon_theme_get_default();
		if (icon_theme != NULL &&
		    gtk_icon_theme_has_icon(icon_theme, CODEX_ICON_NAME))
			return CODEX_ICON_NAME;
		return "utilities-terminal";
	}

	if (tab->kind != SAKURA_TAB_TOOL)
		return "utilities-terminal";

	icon_name = sakura_tool_icon_name(tab->tool);
	icon_theme = gtk_icon_theme_get_default();
	if (icon_theme != NULL && gtk_icon_theme_has_icon(icon_theme, icon_name))
		return icon_name;
	return "utilities-terminal";
}

static void
sakura_workspace_set_boolean(const gchar *key, gboolean value)
{
	if (sakura.cfg != NULL)
		g_key_file_set_boolean(sakura.cfg, SAKURA_CONFIG_GROUP, key, value);
	sakura.config_modified = TRUE;
}


static void
sakura_workspace_set_integer(const gchar *key, gint value)
{
	if (sakura.cfg != NULL)
		g_key_file_set_integer(sakura.cfg, SAKURA_CONFIG_GROUP, key, value);
	sakura.config_modified = TRUE;
}

gboolean
sakura_pane_is_in_active_scope (struct sakura_tab *sk_tab)
{
	if (sk_tab == NULL || sk_tab->sidebar_node == NULL)
		return FALSE;
	if (sakura.workspace->active_task != NULL &&
	    (sk_tab->page == NULL ||
	     !sakura_task_is_within(sk_tab->page->task, sakura.workspace->active_task)))
		return FALSE;
	if (!sakura.show_archived && sk_tab->page != NULL && sk_tab->page->archived)
		return FALSE;
	if (sakura_active_group_model() == NULL ||
	    sakura_active_group_model() == sakura.workspace->root_group)
		return TRUE;
	return sk_tab->page != NULL &&
	       sakura_workspace_model_group_for_session(sakura.workspace, sk_tab->page) == sakura_active_group_model();
}


gboolean
sakura_tab_is_in_active_scope (struct sakura_tab *sk_tab)
{
	return sakura_pane_is_in_active_scope(sk_tab);
}


struct sakura_sidebar_node *
sakura_sidebar_default_parent (void)
{
	struct sakura_tab *current_tab = NULL;
	gint page;

	if (sakura.notebook != NULL) {
		page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
		if (page >= 0)
			current_tab = sakura_tab_at_page(page);
	}

	/* A specific group is an insertion target even when the current page has
	 * not yet been moved into it, such as when the group is empty. */
	if (sakura.workspace->active_task != NULL) {
		if (sakura.workspace->active_task->sidebar_node != NULL)
			return sakura.workspace->active_task->sidebar_node;
		if (sakura_workspace_model_group_for_task(sakura.workspace, sakura.workspace->active_task) != NULL)
			return sakura_sidebar_group_node(sakura_workspace_model_group_for_task(sakura.workspace, sakura.workspace->active_task));
	}
	if (sakura_active_group_model() != NULL &&
	    sakura_active_group_model() != sakura.workspace->root_group &&
	    (current_tab == NULL || !sakura_tab_is_in_active_scope(current_tab)))
		return sakura_sidebar_group_node(sakura_active_group_model());

	/* With All terminals selected, retain the current tab's ownership group
	 * instead of treating the root scope as a real destination group. */
	if (current_tab != NULL && current_tab->page != NULL)
		return sakura_sidebar_group_node(sakura_workspace_model_group_for_session(sakura.workspace, current_tab->page));

	if (sakura_active_group_model() != NULL)
		return sakura_sidebar_group_node(sakura_active_group_model());
	return sakura_sidebar_selected_group();
}


void
sakura_sidebar_prepare_page_parent (SakuraPage *page, SakuraSidebarNode *parent)
{
	SakuraSidebarNode *page_parent;
	SakuraTask *task = NULL;
	SakuraGroup *group;

	if (page == NULL || sakura.workspace == NULL)
		return;

	page_parent = parent != NULL ? parent : sakura_sidebar_default_parent();
	if (page_parent != NULL && page_parent->type == SAKURA_SIDEBAR_TASK)
		task = page_parent->task;
	group = task != NULL
	      ? sakura_workspace_model_group_for_task(sakura.workspace, task)
	      : sakura_group_for_sidebar_node(page_parent);
	if (group == NULL)
		group = sakura.workspace->root_group;

	page->task = task;
	page->group = group;
}


void
sakura_select_pane (struct sakura_tab *sk_tab, gboolean focus)
{
	gint page, current_page;
	struct sakura_sidebar_node *scope;
	SakuraSidebarSelectionReason selection_reason;

	if (sk_tab == NULL || sk_tab->hbox == NULL || sakura.notebook == NULL)
		return;
	/* Any newer selection invalidates an idle focus request, including callers
	 * that intentionally select without moving keyboard focus. */
	sakura_focus_tab_cancel_pending();

	if (!sakura_pane_is_in_active_scope(sk_tab)) {
		/* A page owned by a task has the task node as its immediate parent.
		 * The tab strip is scoped by groups, so always walk to the owning
		 * group before changing scope. Selecting a tab also makes its task
		 * context authoritative when the previous context filtered it out. */
		if (sk_tab->page != NULL)
			sakura.workspace->active_task = sk_tab->page->task;
		scope = sk_tab->page != NULL
		      ? sakura_sidebar_group_node(sakura_workspace_model_group_for_session(sakura.workspace, sk_tab->page))
		      : sakura.sidebar_root;
		sakura_sidebar_set_scope(scope);
	}

	page = sakura_page_for_tab(sk_tab);
	if (page < 0)
		return;

	sakura.workspace_selection_cleared = FALSE;
	sakura.workspace->active_tab = sk_tab;
	sakura.workspace->active_page = sk_tab->page;
	if (sk_tab->page != NULL)
		sk_tab->page->active_tab = sk_tab;
	sakura_workspace_start_page_runtime(sk_tab->page);
	/* Selection can happen inside a workspace mutation, where the notebook's
	 * switch-page signal is intentionally suppressed. Keep scope history
	 * authoritative at the selection primitive instead of relying on that
	 * signal as a side effect. */
	sakura_remember_current_scope_tab(sk_tab);
	current_page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	if (current_page != page)
		gtk_notebook_set_current_page(GTK_NOTEBOOK(sakura.notebook), page);
	else {
		selection_reason = sakura.session_restoring
		                 ? SAKURA_SIDEBAR_SELECTION_RESTORE
		                 : SAKURA_SIDEBAR_SELECTION_SYNC;
		if (sk_tab->page != NULL && sk_tab->page->panes != NULL &&
		    sk_tab->page->panes->len <= 1)
			sakura_sidebar_queue_select_node_with_reason(
				sk_tab->page->sidebar_node, selection_reason);
		else
			sakura_sidebar_queue_select_node_with_reason(
				sk_tab->sidebar_node, selection_reason);
	}

	sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_SELECTION);
	sakura_sidebar_update_page(sk_tab->page);
	if (focus)
		sakura_focus_tab(sk_tab);
}


void
sakura_select_tab (struct sakura_tab *sk_tab, gboolean focus)
{
	sakura_select_pane(sk_tab, focus);
}


void
sakura_select_session (SakuraSession *session, gboolean focus)
{
	SakuraPane *pane = sakura_session_active_pane(session);

	if (pane != NULL)
		sakura_select_pane(pane, focus);
}


void
sakura_workspace_start_page_runtime(SakuraPage *page)
{
	if (page == NULL || page->panes == NULL || sakura.session_shutting_down)
		return;
	for (guint index = 0; index < page->panes->len; index++) {
		SakuraTab *tab = g_ptr_array_index(page->panes, index);
		if (tab != NULL && tab->runtime_deferred)
			sakura_tab_start_deferred_runtime_async(tab);
	}
}


static SakuraTab *
sakura_workspace_notebook_tab_for_widget(GtkWidget *widget_page, guint page_num)
{
	if (sakura.workspace != NULL && sakura.workspace->pages != NULL &&
	    widget_page != NULL) {
		for (guint index = 0; index < sakura.workspace->pages->len; index++) {
			SakuraPage *page = g_ptr_array_index(sakura.workspace->pages, index);

			if (page != NULL && page->container == widget_page)
				return page->active_tab != NULL ? page->active_tab
				                              : page->tab_bar_tab;
		}
	}
	return sakura_tab_at_page(page_num);
}


static void
sakura_workspace_capture_notebook_selection(SakuraTab *tab)
{
	g_clear_pointer(&sakura.workspace_pending_notebook_terminal_id, g_free);
	if (tab != NULL && tab->terminal_id != NULL)
		sakura.workspace_pending_notebook_terminal_id = g_strdup(tab->terminal_id);
}


void
sakura_workspace_reconcile_selection(void)
{
	SakuraWorkspaceModel *workspace = sakura.workspace;
	SakuraTab *active_tab = NULL;
	SakuraPage *active_page = NULL;
	SakuraTab *pending_tab = NULL;
	gint current, target = -1;

	if (workspace == NULL)
		return;

	if (sakura.workspace_selection_cleared) {
		/* An empty scope is a valid state. Do not let an automatic GTK
		 * neighbour selected during a mutation recreate a terminal selection. */
		workspace->active_page = NULL;
		workspace->active_tab = NULL;
	} else {
		active_page = workspace->active_page;
		active_tab = workspace->active_tab;
		if (active_tab != NULL && active_tab->page != NULL &&
		    active_tab->page->container != NULL && sakura.notebook != NULL &&
		    gtk_notebook_page_num(GTK_NOTEBOOK(sakura.notebook),
	                              active_tab->page->container) >= 0) {
			/* The active pane identifies its owning page. */
			active_page = active_tab->page;
		} else if (active_page != NULL && active_page->container != NULL &&
		           sakura.notebook != NULL &&
		           gtk_notebook_page_num(GTK_NOTEBOOK(sakura.notebook),
	                                  active_page->container) >= 0) {
			/* A page can survive a pane/layout mutation while its cached active
			 * pane is temporarily unset. Re-derive the pair from the page. */
			active_tab = sakura_sidebar_page_active_tab(active_page);
		} else if (sakura.workspace_pending_notebook_terminal_id != NULL) {
			/* The switch signal may have been the only selection event observed
			 * during a transaction. Recover it by stable terminal identity. */
			pending_tab = sakura_find_pane_by_terminal_id(
				sakura.workspace_pending_notebook_terminal_id);
			if (pending_tab != NULL && pending_tab->page != NULL) {
				active_page = pending_tab->page;
				active_tab = active_page->active_tab != NULL
				           ? active_page->active_tab : pending_tab;
			}
		}

		if (active_page == NULL || active_tab == NULL ||
		    active_tab->page != active_page) {
			active_page = NULL;
			active_tab = NULL;
		}
		workspace->active_page = active_page;
		workspace->active_tab = active_tab;
	}

	if (sakura.notebook != NULL) {
		current = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
		if (workspace->active_page != NULL &&
		    workspace->active_page->container != NULL)
			target = gtk_notebook_page_num(
				GTK_NOTEBOOK(sakura.notebook), workspace->active_page->container);
		if (target >= 0 && current != target)
			gtk_notebook_set_current_page(GTK_NOTEBOOK(sakura.notebook), target);
	}

	g_clear_pointer(&sakura.workspace_pending_notebook_terminal_id, g_free);
	sakura.workspace_selection_cleared = FALSE;
}


void
sakura_workspace_begin_mutation(void)
{
	if (sakura.workspace_mutation_depth == 0) {
		g_clear_pointer(&sakura.workspace_pending_notebook_terminal_id, g_free);
		sakura.workspace_selection_cleared = FALSE;
	}
	sakura.workspace_mutation_depth++;
}


void
sakura_workspace_reconcile(void)
{
	const guint view_changes = SAKURA_WORKSPACE_CHANGE_STRUCTURE |
	                           SAKURA_WORKSPACE_CHANGE_SCOPE |
	                           SAKURA_WORKSPACE_CHANGE_SELECTION |
	                           SAKURA_WORKSPACE_CHANGE_METADATA |
	                           SAKURA_WORKSPACE_CHANGE_PROJECTION;

	if (sakura_workspace_is_mutating() || sakura.workspace_reconciling)
		return;

	sakura.workspace_reconciling = TRUE;
	while (sakura.workspace_pending_changes != SAKURA_WORKSPACE_CHANGE_NONE) {
		guint changes = sakura.workspace_pending_changes;

		sakura.workspace_pending_changes = SAKURA_WORKSPACE_CHANGE_NONE;
		if ((changes & view_changes) != 0)
			sakura_tab_bar_refresh();
		if ((changes & (SAKURA_WORKSPACE_CHANGE_STRUCTURE |
		                SAKURA_WORKSPACE_CHANGE_SCOPE |
		                SAKURA_WORKSPACE_CHANGE_GEOMETRY)) != 0 &&
		    sakura.notebook != NULL &&
		    gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook)) > 0)
			sakura_set_size();
	}
	sakura.workspace_reconciling = FALSE;
}


void
sakura_workspace_mark_changed(SakuraWorkspaceChange changes)
{
	sakura.workspace_pending_changes |= (guint)changes;
	if (!sakura_workspace_is_mutating())
		sakura_workspace_reconcile();
}


static void
sakura_workspace_validate_after_mutation(void)
{
	GError *error = NULL;

	/* Startup and shutdown deliberately pass through partially-built GTK
	 * projections. Validate steady-state mutations once the session is live. */
	if (!sakura.session_ready || sakura.session_restoring ||
	    sakura.session_shutting_down || sakura.sidebar_model == NULL ||
	    sakura.notebook == NULL)
		return;
	if (!sakura_workspace_validate(&error)) {
		g_warning("Workspace invariant failed after mutation: %s",
		          error != NULL ? error->message : "unknown error");
		g_clear_error(&error);
	}
}


void
sakura_workspace_end_mutation(void)
{
	if (sakura.workspace_mutation_depth > 0)
		sakura.workspace_mutation_depth--;
	if (!sakura_workspace_is_mutating()) {
		sakura_workspace_reconcile_selection();
		sakura_workspace_reconcile();
		sakura_workspace_validate_after_mutation();
	}
}


gboolean
sakura_workspace_is_mutating(void)
{
	return sakura.workspace_mutation_depth > 0;
}


void
sakura_select_scope_default (void)
{
	struct sakura_tab *sk_tab = NULL;
	gint page;

	if (sakura.notebook == NULL || sakura.active_group_scope == NULL)
		return;

	if (sakura.workspace->active_tab != NULL &&
	    sakura_page_for_tab(sakura.workspace->active_tab) >= 0 &&
	    sakura_tab_is_in_active_scope(sakura.workspace->active_tab)) {
		sk_tab = sakura.workspace->active_tab;
	} else if (sakura_active_group_model() != NULL &&
	           sakura_active_group_model()->last_terminal_id != NULL) {
		sk_tab = sakura_find_pane_by_terminal_id(
			sakura_active_group_model()->last_terminal_id);
		if (sk_tab != NULL && !sakura_tab_is_in_active_scope(sk_tab))
			sk_tab = NULL;
	}

	if (sk_tab == NULL) {
		page = sakura_tab_bar_nth_visible_page(0);
		if (page >= 0)
			sk_tab = sakura_tab_at_page(page);
	}

	if (sk_tab != NULL) {
		sakura_select_tab(sk_tab, TRUE);
		return;
	}

	/* An empty scope has no active terminal, but the scope itself remains
	 * selected so the next terminal is created in the right group. */
	sakura.workspace->active_page = NULL;
	sakura.workspace->active_tab = NULL;
	sakura.workspace_selection_cleared = TRUE;
	sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_SELECTION);
	sakura_sidebar_queue_select_node(sakura.active_group_scope);
	if (!sakura_workspace_is_mutating())
		sakura_workspace_reconcile_selection();
}


void
sakura_remember_current_scope_tab (struct sakura_tab *current_tab)
{
	gint page;
	struct sakura_tab *sk_tab;

	if (sakura_active_group_model() == NULL || sakura.notebook == NULL)
		return;
	sk_tab = current_tab;
	if (sk_tab == NULL) {
		page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
		if (page < 0)
			return;
		sk_tab = sakura_tab_at_page(page);
	}
	if (sk_tab == NULL || !sakura_tab_is_in_active_scope(sk_tab) ||
	    sk_tab->terminal_id == NULL)
		return;
	if (g_strcmp0(sakura_active_group_model()->last_terminal_id,
	              sk_tab->terminal_id) != 0) {
		g_free(sakura_active_group_model()->last_terminal_id);
		sakura_active_group_model()->last_terminal_id = g_strdup(sk_tab->terminal_id);
	}
}


void
sakura_register_codex_icon (void)
{
	GtkIconTheme *icon_theme;
	gchar *icon_path;

	icon_theme = gtk_icon_theme_get_default();
	if (icon_theme == NULL)
		return;

	/* The development tree mirrors the hicolor layout, so GTK can resolve
	 * the same icon name without requiring a package installation. */
	icon_path = g_build_filename(SAKURA_SOURCE_ICON_DIR, "hicolor", "scalable",
	                             "apps", CODEX_ICON_NAME ".svg", NULL);
	if (g_file_test(icon_path, G_FILE_TEST_IS_REGULAR))
		gtk_icon_theme_prepend_search_path(icon_theme, SAKURA_SOURCE_ICON_DIR);
	g_free(icon_path);
}


void
sakura_sidebar_set_scope (struct sakura_sidebar_node *scope)
{
	SakuraGroup *group;

	if (scope == NULL || scope->type != SAKURA_SIDEBAR_GROUP)
		scope = sakura.sidebar_root;
	if (scope == NULL)
		return;
	group = scope->group != NULL ? scope->group : sakura.workspace->root_group;

	if (sakura.workspace->active_group == group) {
		sakura.active_group_scope = sakura_sidebar_group_node(group);
		sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_SCOPE);
		return;
	}

	sakura_remember_current_scope_tab(NULL);
	sakura.workspace->active_group = group;
	sakura.active_group_scope = sakura_sidebar_group_node(group);
	sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_SCOPE);
	sakura_session_mark_dirty();
}


void
sakura_sidebar_add_terminal (struct sakura_tab *sk_tab, struct sakura_sidebar_node *parent)
{
	struct sakura_sidebar_node *node;
	SakuraPage *page;
	SakuraSidebarNode *group;
	SakuraSidebarNode *page_parent;

	if (sk_tab == NULL || sk_tab->page == NULL)
		return;
	sakura_workspace_begin_mutation();
	page = sk_tab->page;
	group = sakura_sidebar_group_ancestor(parent);
	page_parent = parent != NULL && parent->type == SAKURA_SIDEBAR_TASK
	            ? parent : group;
	if (page->sidebar_node == NULL)
		sakura_sidebar_add_page(page, page_parent);
	else if (page->group == NULL)
		page->group = page->task != NULL
		            ? sakura_workspace_model_group_for_task(sakura.workspace, page->task)
		            : sakura_group_for_sidebar_node(page_parent);

	node = g_new0(struct sakura_sidebar_node, 1);
	node->type = SAKURA_SIDEBAR_TERMINAL;
	node->title = g_strdup(_("Terminal"));
	node->subtitle = g_strdup("");
	node->parent = page->sidebar_node;
	node->tab = sk_tab;
	sk_tab->sidebar_node = node;
	if (page->panes != NULL && page->panes->len > 1)
		sakura_sidebar_show_page_panes(page);
	sakura_tab_bar_add_tab(sk_tab);
	sakura_sidebar_update_tab(sk_tab);
	if (sakura.workspace->active_tab == sk_tab) {
		if (page->panes != NULL && page->panes->len <= 1)
			sakura_sidebar_queue_select_node(page->sidebar_node);
		else
			sakura_sidebar_queue_select_node(sk_tab->sidebar_node);
	}
	else if (sakura_tab_is_in_active_scope(sk_tab) &&
	         (sakura.workspace->active_tab == NULL ||
	          !sakura_tab_is_in_active_scope(sakura.workspace->active_tab)))
		sakura_select_tab(sk_tab, TRUE);
	sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_STRUCTURE |
	                              SAKURA_WORKSPACE_CHANGE_SELECTION);
	sakura_workspace_end_mutation();
}


static void
sakura_sidebar_add_page(SakuraPage *page, SakuraSidebarNode *parent)
{
	SakuraSidebarNode *node;
	SakuraSidebarNode *page_parent;

	if (page == NULL || page->sidebar_node != NULL)
		return;

	node = g_new0(SakuraSidebarNode, 1);
	node->type = SAKURA_SIDEBAR_PAGE;
	node->id = g_strdup(page->id);
	page_parent = parent != NULL && parent->type == SAKURA_SIDEBAR_TASK
	            ? parent : sakura_sidebar_group_ancestor(parent);
	node->parent = page_parent;
	sakura_sidebar_prepare_page_parent(page, page_parent);
	node->page = page;
	page->sidebar_node = node;
	sakura_sidebar_insert_page_node(node);
	sakura_sidebar_update_page(page);
}


static gboolean
sakura_focus_tab_cb (gpointer data)
{
	GtkWidget *vte;
	gboolean was_focus_syncing;

	(void)data;
	sakura.focus_tab_source_id = 0;
	vte = sakura.focus_tab_pending_vte;
	sakura.focus_tab_pending_vte = NULL;
	if (!sakura.session_shutting_down && vte != NULL &&
	    sakura.workspace != NULL && sakura.workspace->active_tab != NULL &&
	    sakura.workspace->active_tab->vte == vte &&
	    g_strcmp0(sakura.focus_tab_pending_terminal_id,
	              sakura.workspace->active_tab->terminal_id) == 0 &&
	    gtk_widget_get_visible(vte) && gtk_widget_get_realized(vte)) {
		was_focus_syncing = sakura.sidebar_focus_syncing;
		sakura.sidebar_focus_syncing = TRUE;
		gtk_widget_grab_focus(vte);
		sakura.sidebar_focus_syncing = was_focus_syncing;
		if (sakura.selection_intent_us != 0 &&
		    g_strcmp0(sakura.selection_intent_terminal_id,
		              sakura.workspace->active_tab->terminal_id) == 0 &&
		    g_getenv("SAKURA_LATENCY_TRACE") != NULL)
			g_message("selection-focus-latency-us=%" G_GINT64_FORMAT
			          " terminal=%s",
			          g_get_monotonic_time() - sakura.selection_intent_us,
			          sakura.workspace->active_tab->terminal_id);
	}
	g_clear_pointer(&sakura.focus_tab_pending_terminal_id, g_free);
	g_clear_pointer(&sakura.selection_intent_terminal_id, g_free);
	sakura.selection_intent_us = 0;
	g_clear_object(&vte);
	return G_SOURCE_REMOVE;
}


void
sakura_focus_tab_cancel_pending(void)
{
	if (sakura.focus_tab_source_id != 0) {
		g_source_remove(sakura.focus_tab_source_id);
		sakura.focus_tab_source_id = 0;
	}
	g_clear_object(&sakura.focus_tab_pending_vte);
	g_clear_pointer(&sakura.focus_tab_pending_terminal_id, g_free);
}


void
sakura_focus_tab (struct sakura_tab *sk_tab)
{
#ifdef HAVE_WEBKITGTK
	gboolean was_focus_syncing;
#endif

	if (sakura.session_shutting_down || sk_tab == NULL)
		return;
	sakura_focus_tab_cancel_pending();
	if (sk_tab->vte == NULL)
		return;

#ifdef HAVE_WEBKITGTK
	if (sk_tab->browser != NULL) {
		was_focus_syncing = sakura.sidebar_focus_syncing;
		sakura.sidebar_focus_syncing = TRUE;
		gtk_widget_grab_focus(sk_tab->browser);
		sakura.sidebar_focus_syncing = was_focus_syncing;
		return;
	}
#endif

	/* Let the tree view finish handling the click before moving focus to the
	 * selected terminal. Coalesce focus requests so an older selection cannot
	 * steal focus after a newer one. */
	sakura.focus_tab_pending_vte = g_object_ref(sk_tab->vte);
	sakura.focus_tab_pending_terminal_id = g_strdup(sk_tab->terminal_id);
	sakura.focus_tab_source_id = g_idle_add(sakura_focus_tab_cb, NULL);
}
void
sakura_sidebar_selection_changed_cb (GtkTreeSelection *selection, void *data)
{
	struct sakura_sidebar_node *node;

	if (sakura.session_shutting_down)
		return;
	/* GTK may select a neighboring row automatically when a model row is
	 * removed. That selection is an implementation detail of the mutation,
	 * not a user intent; the operation will select its authoritative result
	 * after the model is consistent again. */
	if (sakura.sidebar_syncing || sakura_workspace_is_mutating())
		return;
	/* A real sidebar interaction is authoritative. It must invalidate any
	 * lower-level request queued by a notebook or creation callback before we
	 * interpret the newly selected node. */
	sakura_sidebar_cancel_pending_selection();

	node = sakura_sidebar_selected_node();
	if (node == NULL)
		return;
	g_clear_pointer(&sakura.selection_intent_terminal_id, g_free);
	sakura.selection_intent_us = g_get_monotonic_time();
	if (node->type == SAKURA_SIDEBAR_TERMINAL && node->tab != NULL)
		sakura.selection_intent_terminal_id = g_strdup(node->tab->terminal_id);
	else if (node->page != NULL && node->page->active_tab != NULL)
		sakura.selection_intent_terminal_id =
			g_strdup(node->page->active_tab->terminal_id);
	sakura_ui_latency_trace_request_paint(sakura.selection_intent_terminal_id,
	                                      sakura.selection_intent_us);
	sakura_sidebar_remember_selection(node);
	if (node->type == SAKURA_SIDEBAR_GROUP) {
		sakura_workspace_begin_mutation();
		sakura.workspace->active_task = NULL;
		sakura_sidebar_set_scope(node);
		sakura_select_scope_default();
		sakura_workspace_end_mutation();
		return;
	}
	if (node->type == SAKURA_SIDEBAR_TASK) {
		SakuraPage *page;
		sakura_workspace_begin_mutation();
		sakura.workspace->active_task = node->task;
		sakura_sidebar_set_scope(sakura_sidebar_group_node(sakura_workspace_model_group_for_task(sakura.workspace, node->task)));
		page = sakura_sidebar_first_task_page(node->task);
		if (page != NULL && page->active_tab != NULL)
			sakura_select_session(page, TRUE);
		sakura_sidebar_queue_select_node(node);
		sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_SCOPE |
		                              SAKURA_WORKSPACE_CHANGE_SELECTION);
		sakura_workspace_end_mutation();
		sakura_session_mark_dirty();
		return;
	}
	if (node->type == SAKURA_SIDEBAR_PAGE) {
		if (node->page == NULL)
			return;
		sakura.workspace->active_task = node->page->task;
		sakura_select_session(node->page, TRUE);
		sakura_session_mark_dirty();
		return;
	}
	if (node->type != SAKURA_SIDEBAR_TERMINAL || node->tab == NULL)
		return;

	sakura.workspace->active_task = node->tab->page != NULL ? node->tab->page->task : NULL;
	sakura_select_pane(node->tab, TRUE);
	sakura_session_mark_dirty();
}


static gboolean
sakura_sidebar_primary_click_idle(gpointer data)
{
	SakuraSidebarNode *node;
	SakuraSidebarNodeType type = sakura.sidebar_primary_click_type;
	gchar *id = g_steal_pointer(&sakura.sidebar_primary_click_id);

	(void)data;
	sakura.sidebar_primary_click_source_id = 0;
	if (!sakura.session_shutting_down && id != NULL) {
		node = sakura_sidebar_find_node_by_identity(type, id);
		if (node != NULL) {
			/* Resolve the row only after GTK has released the event's internal
			 * tree path. The projection may have changed in the meantime. */
			sakura_sidebar_select_node_now(node);
			sakura_sidebar_selection_changed_cb(sakura.sidebar_selection, NULL);
		}
	}
	g_free(id);
	return G_SOURCE_REMOVE;
}


void
sakura_sidebar_cancel_primary_click(void)
{
	if (sakura.sidebar_primary_click_source_id != 0) {
		g_source_remove(sakura.sidebar_primary_click_source_id);
		sakura.sidebar_primary_click_source_id = 0;
	}
	g_clear_pointer(&sakura.sidebar_primary_click_id, g_free);
}


void
sakura_sidebar_primary_click(SakuraSidebarNode *node)
{
	/* Button press is the earliest reliable indication of user intent, but GTK
	 * still owns the event's tree path until dispatch completes. Cancel stale
	 * synchronization now and defer any model/selection work until idle. */
	sakura_sidebar_cancel_pending_selection();
	sakura_sidebar_cancel_primary_click();
	if (node != NULL && node->id != NULL &&
	    node == sakura_sidebar_selected_node()) {
		sakura.sidebar_primary_click_type = node->type;
		sakura.sidebar_primary_click_id = g_strdup(node->id);
		sakura.sidebar_primary_click_source_id = g_idle_add(
			sakura_sidebar_primary_click_idle, NULL);
	}
}


struct sakura_sidebar_tool_target {
	struct sakura_sidebar_action_target *context;
	SakuraToolKind tool;
};


struct sakura_sidebar_move_target {
	gchar *terminal_id;
	gchar *group_id;
};


static void
sakura_sidebar_tool_target_free (gpointer data, GClosure *closure)
{
	struct sakura_sidebar_tool_target *target = data;

	(void)closure;
	if (target == NULL)
		return;
	sakura_sidebar_action_target_free(target->context, NULL);
	g_free(target);
}


static void
sakura_sidebar_move_target_free (gpointer data, GClosure *closure)
{
	struct sakura_sidebar_move_target *target = data;

	(void)closure;
	if (target == NULL)
		return;
	g_free(target->terminal_id);
	g_free(target->group_id);
	g_free(target);
}


static void
sakura_sidebar_prepare_context (struct sakura_sidebar_node *node)
{
	struct sakura_sidebar_node *scope;
	SakuraTask *previous_task;

	if (node == NULL)
		node = sakura.sidebar_root;
	previous_task = sakura.workspace->active_task;
	if (node->type == SAKURA_SIDEBAR_TASK)
		sakura.workspace->active_task = node->task;
	else if (node->type == SAKURA_SIDEBAR_PAGE)
		sakura.workspace->active_task = node->page != NULL ? node->page->task : NULL;
	else if (node->type == SAKURA_SIDEBAR_TERMINAL)
		sakura.workspace->active_task = node->tab != NULL && node->tab->page != NULL
	                   ? node->tab->page->task : NULL;
	else if (node->type == SAKURA_SIDEBAR_GROUP)
		sakura.workspace->active_task = NULL;
	if (node->type == SAKURA_SIDEBAR_TERMINAL || node->type == SAKURA_SIDEBAR_PAGE ||
	    node->type == SAKURA_SIDEBAR_TASK)
		scope = node->type == SAKURA_SIDEBAR_TASK
			? sakura_sidebar_group_node(sakura_workspace_model_group_for_task(sakura.workspace, node->task))
		      : node->type == SAKURA_SIDEBAR_PAGE
		      ? sakura_sidebar_group_node(sakura_workspace_model_group_for_session(sakura.workspace, node->page))
		      : node->tab != NULL ? sakura_sidebar_group_node(
		                              sakura_workspace_model_group_for_session(sakura.workspace, node->tab->page))
		                         : sakura.sidebar_root;
	else
		scope = node;
	if (scope == NULL)
		return;

	if (sakura_active_group_model() != (scope != NULL ? scope->group : NULL)) {
		sakura_sidebar_set_scope(scope);
		sakura_select_scope_default();
	} else if (node->type == SAKURA_SIDEBAR_TASK &&
	           previous_task != sakura.workspace->active_task) {
		/* Switching task context must also switch the task-filtered tab view,
		 * even when both tasks live in the same group. */
		sakura_select_scope_default();
	}
	if (node->type == SAKURA_SIDEBAR_TERMINAL && node->tab != NULL) {
		sakura_select_tab(node->tab, FALSE);
	} else if (node->type == SAKURA_SIDEBAR_PAGE) {
		sakura_select_session(node->page, FALSE);
	}
	if (previous_task != sakura.workspace->active_task)
		sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_SELECTION);
}


static void
sakura_new_tab_in_scope_cb (GtkWidget *widget, void *data)
{
	struct sakura_sidebar_action_target *target = data;
	SakuraSidebarNode *node = sakura_sidebar_action_target_node(target);
	SakuraSidebarNode *insert_after = NULL;

	if (target != NULL && node == NULL)
		return;
	sakura_workspace_begin_mutation();
	if (node != NULL && node->type == SAKURA_SIDEBAR_TERMINAL &&
	    node->parent != NULL && node->parent->type == SAKURA_SIDEBAR_PAGE)
		insert_after = node->parent;
	else if (node != NULL && node->type == SAKURA_SIDEBAR_PAGE)
		insert_after = node;
	sakura_sidebar_prepare_context(node);
	sakura.sidebar_pending_insert_after = insert_after;
	if (node != NULL && node->type == SAKURA_SIDEBAR_TASK) {
		sakura_new_tab_for_task(node->task);
	} else if (node != NULL && node->type == SAKURA_SIDEBAR_GROUP &&
	    node != sakura.sidebar_root)
		sakura_new_tab_for_group(node);
	else
		sakura_new_tab_cb(widget, NULL);
	sakura.sidebar_pending_insert_after = NULL;
	sakura_workspace_end_mutation();
}


static void
sakura_new_codex_in_scope_cb (GtkWidget *widget, void *data)
{
	struct sakura_sidebar_action_target *target = data;
	SakuraSidebarNode *node = sakura_sidebar_action_target_node(target);
	SakuraSidebarNode *parent;
	SakuraGroup *group;
	SakuraTask *task;
	const gchar *reasoning_effort;
	gchar *group_id;
	gchar *task_id;
	gchar *cwd;

	if (target != NULL && node == NULL)
		return;
	parent = sakura_sidebar_creation_parent_for_context(node);
	group = sakura_group_for_sidebar_node(parent);
	task = parent != NULL && parent->type == SAKURA_SIDEBAR_TASK
	     ? parent->task : NULL;
	group_id = g_strdup(group != NULL && group->id != NULL ? group->id : "root");
	task_id = g_strdup(task != NULL ? task->id : NULL);
	cwd = sakura_sidebar_directory_for_node(parent);
	reasoning_effort = widget != NULL
	                 ? g_object_get_data(G_OBJECT(widget),
	                                     SAKURA_CODEX_REASONING_EFFORT_DATA_KEY)
	                 : NULL;

	sakura_workspace_begin_mutation();
	sakura_sidebar_prepare_context(node);
	/* Changing scope may rebuild the sidebar projection. Resolve the captured
	 * stable ownership IDs again instead of retaining a pointer into the old
	 * tree, and pass the captured folder explicitly so neither active-tab nor
	 * process-CWD fallback can redirect the Codex workspace. */
	if (task_id != NULL) {
		task = sakura_workspace_model_find_task(sakura.workspace, task_id);
		parent = task != NULL ? task->sidebar_node : NULL;
	} else {
		parent = sakura_sidebar_find_group_by_id(group_id);
	}
	if (parent == NULL)
		parent = sakura.sidebar_root;
	sakura_session_accept_changes();
	sakura_add_tab_with_options(cwd, parent, NULL, FALSE, SAKURA_TAB_CODEX,
	                            SAKURA_TOOL_NONE, NULL, NULL, NULL, reasoning_effort,
	                            NULL, NULL, -1);
	sakura_workspace_end_mutation();
	g_free(cwd);
	g_free(task_id);
	g_free(group_id);
}


static void
sakura_resume_codex_in_scope_cb (GtkWidget *widget, void *data)
{
	struct sakura_sidebar_action_target *target = data;
	SakuraSidebarNode *node = sakura_sidebar_action_target_node(target);
	SakuraSidebarNode *parent = sakura_sidebar_creation_parent_for_context(node);
	SakuraGroup *group = sakura_group_for_sidebar_node(parent);
	SakuraCodexResumeTarget resume_target = {
		.group_id = g_strdup(group != NULL ? group->id : "root"),
		.task_id = g_strdup(parent != NULL &&
		                   parent->type == SAKURA_SIDEBAR_TASK &&
		                   parent->task != NULL ? parent->task->id : NULL)
	};

	if (target == NULL || node == NULL)
		goto out;
	sakura_sidebar_prepare_context(node);
	sakura_resume_codex_cb(widget, &resume_target);
out:
	g_free(resume_target.group_id);
	g_free(resume_target.task_id);
}


static void
sakura_open_here_context_cb (GtkWidget *widget, void *data)
{
	struct sakura_sidebar_action_target *target = data;
	SakuraSidebarNode *node = sakura_sidebar_action_target_node(target);
	SakuraOpenHereKind kind = GPOINTER_TO_INT(
		g_object_get_data(G_OBJECT(widget), "sakura-open-here-kind"));

	if (node == NULL)
		return;
	sakura_sidebar_prepare_context(node);
	sakura_open_here_cb(widget, GINT_TO_POINTER(kind));
}


static void
sakura_open_pr_context_cb (GtkWidget *widget, void *data)
{
	struct sakura_sidebar_action_target *target = data;
	SakuraSidebarNode *node = sakura_sidebar_action_target_node(target);

	if (node == NULL)
		return;
	sakura_sidebar_prepare_context(node);
	sakura_open_pr_cb(widget, NULL);
}


static void
sakura_new_tool_context_cb (GtkWidget *widget, void *data)
{
	struct sakura_sidebar_tool_target *target = data;
	SakuraSidebarNode *node;

	if (target == NULL)
		return;
	node = sakura_sidebar_action_target_node(target->context);
	if (node == NULL)
		return;
	sakura_sidebar_prepare_context(node);
	sakura_new_tool_cb(widget, GINT_TO_POINTER(target->tool));
}


static void
sakura_sidebar_move_terminal_cb (GtkWidget *widget, void *data)
{
	struct sakura_sidebar_move_target *target = data;
	SakuraTab *tab;
	SakuraSidebarNode *group;
	SakuraPage *page;

	if (target == NULL || target->terminal_id == NULL || target->group_id == NULL) {
		sakura_sidebar_report_move_failure(
			_("The session or destination group is no longer available."));
		return;
	}
	tab = sakura_find_pane_by_terminal_id(target->terminal_id);
	group = sakura_sidebar_find_group_by_id(target->group_id);
	if (tab == NULL || group == NULL || tab->sidebar_node == NULL ||
	    tab->page == NULL) {
		sakura_sidebar_report_move_failure(
			_("The session or destination group is no longer available."));
		return;
	}
	sakura_workspace_begin_mutation();
	page = tab->page;
	if (!sakura_sidebar_move_page_to_group(page, group)) {
		sakura_workspace_end_mutation();
		sakura_sidebar_report_move_failure(
			_("The workspace or sakura-agent rejected the move."));
		return;
	}

	if (sakura.workspace->active_tab == tab)
		sakura_select_tab(tab, TRUE);
	else
		sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_STRUCTURE);
	sakura_sidebar_save_groups();
	sakura_workspace_end_mutation();
}


static void
sakura_sidebar_report_move_failure(const gchar *message)
{
	sakura_error(_("Could not move the selected item.\n\n%s"),
	             message != NULL ? message :
	             _("The workspace rejected the requested move."));
}


gboolean
sakura_sidebar_move_page_to_group(SakuraPage *page, SakuraSidebarNode *group)
{
	SakuraSidebarNode *node;
	SakuraGroup *new_group;
	SakuraTask *old_task;
	gchar *new_group_id;

	if (page == NULL || group == NULL || group->type != SAKURA_SIDEBAR_GROUP)
		return FALSE;
	new_group = group->group != NULL ? group->group : sakura.workspace->root_group;
	if (new_group == NULL)
		return FALSE;
	new_group_id = g_strdup(new_group->id);
	if (page->task == NULL && page->group == new_group)
		goto success;
	if (!sakura_sidebar_move_page_in_agent(page, new_group, NULL))
		goto fail;
	new_group = sakura_workspace_group_by_id(new_group_id);
	group = new_group != NULL ? new_group->sidebar_node : NULL;
	if (new_group == NULL || group == NULL)
		goto fail;
	sakura_workspace_begin_mutation();
	node = page->sidebar_node;
	old_task = page->task;

	sakura_sidebar_cancel_pending_selection();
	sakura_sidebar_hide_page_panes(page);
	if (!sakura_workspace_model_move_page_to_group(sakura.workspace, page, new_group)) {
		sakura_workspace_end_mutation();
		goto fail;
	}
	if (sakura.workspace->active_task == old_task)
		sakura.workspace->active_task = NULL;
	if (node != NULL) {
		sakura_sidebar_remove_node_row(node);
		node->parent = group;
		sakura_sidebar_insert_page_node(node);
		sakura_sidebar_show_page_panes(page);
	}
	sakura_sidebar_update_page(page);
	sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_STRUCTURE |
	                              SAKURA_WORKSPACE_CHANGE_SCOPE);
	sakura_session_mark_dirty();
	sakura_workspace_end_mutation();
	success:
	g_free(new_group_id);
	return TRUE;
	fail:
	g_free(new_group_id);
	return FALSE;
}


static GtkWidget *
sakura_sidebar_open_here_menu_new (struct sakura_sidebar_node *node)
{
	GtkWidget *menu, *item;
	SakuraOpenHereKind kind;

	menu = gtk_menu_new();
	for (kind = SAKURA_OPEN_HERE_FILE_MANAGER;
	     kind <= SAKURA_OPEN_HERE_EDITOR; kind++) {
		item = gtk_menu_item_new_with_label(
			kind == SAKURA_OPEN_HERE_FILE_MANAGER ? _("File Manager") : _("Editor"));
		g_object_set_data(G_OBJECT(item), "sakura-open-here-kind",
		                  GINT_TO_POINTER(kind));
		sakura_sidebar_connect_context_action(
			item, "activate", G_CALLBACK(sakura_open_here_context_cb), node);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	}
	return menu;
}


static GtkWidget *
sakura_sidebar_move_terminal_menu_new (struct sakura_tab *sk_tab)
{
	GtkWidget *menu, *item;
	GList *group;

	menu = gtk_menu_new();
	for (group = sakura.workspace->groups; group != NULL; group = group->next) {
		SakuraGroup *model_group = group->data;
		struct sakura_sidebar_node *node = model_group->sidebar_node;
		struct sakura_sidebar_move_target *target;

		if (node == NULL || sk_tab == NULL || sk_tab->page == NULL ||
		    model_group == sakura_workspace_model_group_for_session(sakura.workspace, sk_tab->page))
			continue;
		item = gtk_menu_item_new_with_label(
			model_group == sakura.workspace->root_group ? _("All terminals") : model_group->title);
		target = g_new0(struct sakura_sidebar_move_target, 1);
		target->terminal_id = g_strdup(sk_tab->terminal_id);
		target->group_id = g_strdup(model_group->id);
		g_signal_connect_data(item, "activate",
		                      G_CALLBACK(sakura_sidebar_move_terminal_cb), target,
		                      sakura_sidebar_move_target_free, 0);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	}
	{
		GList *items = gtk_container_get_children(GTK_CONTAINER(menu));
		if (items == NULL) {
			gtk_widget_destroy(menu);
			return NULL;
		}
		g_list_free(items);
	}
	return menu;
}


static void
sakura_sidebar_task_status_cb(GtkWidget *widget, void *data)
{
	SakuraTask *task = data;
	SakuraTaskStatus status;

	if (task == NULL)
		return;
	status = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget),
	                                           "sakura-task-status"));
	if (status < SAKURA_TASK_READY || status > SAKURA_TASK_DONE)
		return;
	if (sakura.agent_socket_path != NULL) {
		GError *error = NULL;

		if (!sakura_agent_set_task_status(&sakura, task->id, status, &error)) {
			if (error != NULL)
				g_warning("Could not update task status through sakura-agent: %s",
				          error->message);
			g_clear_error(&error);
			return;
		}
	}
	sakura_workspace_begin_mutation();
	if (task->sidebar_node != NULL)
		sakura_sidebar_prepare_context(task->sidebar_node);
	else {
		sakura.workspace->active_task = task;
		sakura_sidebar_set_scope(sakura_sidebar_group_node(sakura_workspace_model_group_for_task(sakura.workspace, task)));
	}
	task->status = status;
	sakura_task_update_row(task);
	sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_METADATA |
	                              SAKURA_WORKSPACE_CHANGE_SELECTION);
	sakura_session_mark_dirty();
	sakura_workspace_end_mutation();
}


static GtkWidget *
sakura_sidebar_task_status_menu_new(SakuraTask *task)
{
	static const gchar *labels[] = {
		"Ready", "Working", "Blocked", "Ready for review", "Done"
	};
	GtkWidget *menu, *item;
	SakuraTaskStatus status;

	menu = gtk_menu_new();
	for (status = SAKURA_TASK_READY; status <= SAKURA_TASK_DONE; status++) {
		item = gtk_menu_item_new_with_label(_(labels[status]));
		g_object_set_data(G_OBJECT(item), "sakura-task-status",
		                  GINT_TO_POINTER(status));
		g_signal_connect(item, "activate",
		                 G_CALLBACK(sakura_sidebar_task_status_cb), task);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	}
	return menu;
}


static GtkWidget *
sakura_sidebar_tasks_menu_new(SakuraPage *page)
{
	GtkWidget *menu, *item;
	guint index;

	menu = gtk_menu_new();
	if (sakura.workspace->tasks == NULL)
		return menu;
	for (index = 0; index < sakura.workspace->tasks->len; index++) {
		SakuraTask *task = g_ptr_array_index(sakura.workspace->tasks, index);
		if (task == NULL)
			continue;
		item = gtk_menu_item_new_with_label(task->title != NULL
		                                    ? task->title : _("Untitled task"));
		if (page != NULL)
			g_object_set_data_full(G_OBJECT(item), "sakura-task-page-id",
			                       g_strdup(page->id), g_free);
		g_signal_connect_data(item, "activate",
		                      G_CALLBACK(sakura_sidebar_move_page_to_task_cb),
		                      g_strdup(task->id),
		                      sakura_sidebar_group_id_data_free, 0);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	}
	return menu;
}


static GtkWidget *
sakura_sidebar_tools_menu_new (struct sakura_sidebar_node *node)
{
	GtkWidget *menu, *item;
	const SakuraToolKind tools[] = {
		SAKURA_TOOL_GITUI, SAKURA_TOOL_GIT_COLA, SAKURA_TOOL_GH_DASH
	};
	guint tool_index;
	SakuraToolKind tool;
	const gchar *label;

	menu = gtk_menu_new();
	for (tool_index = 0; tool_index < G_N_ELEMENTS(tools); tool_index++) {
		tool = tools[tool_index];
		label = sakura_tool_label(tool);
		item = gtk_menu_item_new_with_label(label);
		{
			struct sakura_sidebar_tool_target *target = g_new0(
				struct sakura_sidebar_tool_target, 1);
			target->context = sakura_sidebar_action_target_new(node);
			if (target->context == NULL) {
				g_free(target);
				gtk_widget_destroy(item);
				continue;
			}
			target->tool = tool;
			g_signal_connect_data(item, "activate",
			                      G_CALLBACK(sakura_new_tool_context_cb), target,
			                      sakura_sidebar_tool_target_free, 0);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	}
	item = gtk_menu_item_new_with_label(_("Open pull request..."));
	sakura_sidebar_connect_context_action(
		item, "activate", G_CALLBACK(sakura_open_pr_context_cb), node);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	return menu;
}


static GtkWidget *
sakura_sidebar_codex_reasoning_menu_new(struct sakura_sidebar_node *node)
{
	static const gchar *efforts[] = { "low", "medium", "high", "xhigh" };
	const gchar *labels[] = { _("Fast"), _("Balanced"), _("Deep"), _("Max") };
	GtkWidget *menu = gtk_menu_new();
	guint index;

	for (index = 0; index < G_N_ELEMENTS(efforts); index++) {
		GtkWidget *item = gtk_menu_item_new_with_label(labels[index]);
		g_object_set_data(G_OBJECT(item), SAKURA_CODEX_REASONING_EFFORT_DATA_KEY,
		                  (gpointer)efforts[index]);
		sakura_sidebar_connect_context_action(
			item, "activate", G_CALLBACK(sakura_new_codex_in_scope_cb), node);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	}
	return menu;
}


static void
sakura_sidebar_close_terminal_cb(GtkWidget *widget, gpointer data)
{
	struct sakura_sidebar_action_target *target = data;
	struct sakura_sidebar_node *node = sakura_sidebar_action_target_node(target);
	SakuraTab *tab;
	gint page;

	(void)widget;
	if (node == NULL || node->type != SAKURA_SIDEBAR_TERMINAL)
		return;
	tab = node->tab;
	if (tab == NULL)
		return;
	if (tab->page != NULL && tab->page->panes != NULL &&
	    tab->page->panes->len > 1) {
		sakura_tab_delete_pane(tab);
		return;
	}
	page = sakura_page_for_tab(tab);
	if (page >= 0)
		sakura_close_tab(page);
}


static void
sakura_sidebar_close_page_cb(GtkWidget *widget, gpointer data)
{
	struct sakura_sidebar_action_target *target = data;
	SakuraSidebarNode *node = sakura_sidebar_action_target_node(target);

	if (node == NULL || node->type != SAKURA_SIDEBAR_PAGE || node->page == NULL ||
	    node->page->active_tab == NULL)
		return;
	sakura_close_tab_cb(widget, node->page->active_tab);
}


GtkWidget *
sakura_sidebar_context_menu_new (struct sakura_sidebar_node *node)
{
	GtkWidget *menu, *item, *submenu;
	struct sakura_sidebar_node *context_node = node != NULL
	                                           ? node : sakura.sidebar_root;
	struct sakura_sidebar_node *page_context =
		context_node != NULL && context_node->type == SAKURA_SIDEBAR_PAGE
		? context_node : NULL;
	if (context_node != NULL && context_node->type == SAKURA_SIDEBAR_PAGE &&
	    context_node->page != NULL && context_node->page->active_tab != NULL &&
	    context_node->page->active_tab->sidebar_node != NULL)
		context_node = context_node->page->active_tab->sidebar_node;

	menu = gtk_menu_new();
	if (context_node != NULL && context_node->type == SAKURA_SIDEBAR_TASK) {
		item = gtk_menu_item_new_with_label(_("New terminal"));
		sakura_sidebar_connect_context_action(
			item, "activate", G_CALLBACK(sakura_new_tab_in_scope_cb), context_node);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

		item = gtk_menu_item_new_with_label(_("New Codex session"));
		sakura_sidebar_connect_context_action(
			item, "activate", G_CALLBACK(sakura_new_codex_in_scope_cb), context_node);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
		item = gtk_menu_item_new_with_label(_("Start work"));
		g_signal_connect_data(item, "activate",
		                      G_CALLBACK(sakura_sidebar_task_start_cb),
		                      g_strdup(context_node->task->id),
		                      sakura_sidebar_group_id_data_free, 0);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

		item = gtk_menu_item_new_with_label(_("New subtask"));
		sakura_sidebar_connect_context_action(
			item, "activate", G_CALLBACK(sakura_sidebar_new_task_cb), context_node);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

		item = gtk_menu_item_new_with_label(_("Attach current session here"));
		g_signal_connect_data(item, "activate",
		                      G_CALLBACK(sakura_sidebar_attach_page_to_task_cb),
		                      g_strdup(context_node->task->id),
		                      sakura_sidebar_group_id_data_free, 0);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

		item = gtk_menu_item_new_with_label(_("Set status"));
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),
		                          sakura_sidebar_task_status_menu_new(context_node->task));
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
		item = gtk_menu_item_new_with_label(_("Rename task..."));
		g_signal_connect_data(item, "activate",
		                      G_CALLBACK(sakura_sidebar_rename_task_cb),
		                      g_strdup(context_node->task->id),
		                      sakura_sidebar_group_id_data_free, 0);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		if (sakura_sidebar_task_is_archived(context_node->task)) {
			item = gtk_menu_item_new_with_label(_("Restore task"));
			sakura_sidebar_connect_context_action(
				item, "activate", G_CALLBACK(sakura_sidebar_archive_cb), context_node);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
			item = gtk_menu_item_new_with_label(_("Delete permanently"));
			gtk_widget_set_sensitive(item,
			                         sakura_workspace_model_can_remove_task(
				                         sakura.workspace, context_node->task));
			g_signal_connect_data(item, "activate",
			                      G_CALLBACK(sakura_sidebar_delete_task_cb),
			                      g_strdup(context_node->task->id),
			                      sakura_sidebar_group_id_data_free, 0);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		} else {
			item = gtk_menu_item_new_with_label(_("Archive task"));
			sakura_sidebar_connect_context_action(
				item, "activate", G_CALLBACK(sakura_sidebar_archive_cb), context_node);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		}
	} else if (context_node != NULL && context_node->type == SAKURA_SIDEBAR_TERMINAL) {
		item = gtk_menu_item_new_with_label(_("New terminal"));
		sakura_sidebar_connect_context_action(
			item, "activate", G_CALLBACK(sakura_new_tab_in_scope_cb), context_node);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

		item = gtk_menu_item_new_with_label(_("New Codex session"));
		sakura_sidebar_connect_context_action(
			item, "activate", G_CALLBACK(sakura_new_codex_in_scope_cb), context_node);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

		item = gtk_menu_item_new_with_label(_("New Codex session with reasoning"));
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),
		                          sakura_sidebar_codex_reasoning_menu_new(context_node));
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

		item = gtk_menu_item_new_with_label(_("Resume session..."));
		sakura_sidebar_connect_context_action(
			item, "activate", G_CALLBACK(sakura_resume_codex_in_scope_cb), context_node);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

		item = gtk_menu_item_new_with_label(_("Open Here"));
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),
		                          sakura_sidebar_open_here_menu_new(context_node));
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

		item = gtk_menu_item_new_with_label(_("Tools"));
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),
		                          sakura_sidebar_tools_menu_new(context_node));
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

		submenu = sakura_sidebar_move_terminal_menu_new(context_node->tab);
		if (submenu != NULL) {
			item = gtk_menu_item_new_with_label(_("Move session to group"));
			gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		}
		submenu = sakura_sidebar_tasks_menu_new(
			context_node->tab != NULL ? context_node->tab->page : NULL);
		if (sakura.workspace->tasks != NULL && sakura.workspace->tasks->len > 0) {
			item = gtk_menu_item_new_with_label(_("Attach session to task"));
			gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		} else {
			gtk_widget_destroy(submenu);
		}

		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
		if (context_node->tab != NULL &&
		    context_node->tab->kind == SAKURA_TAB_CODEX) {
			item = sakura_codex_rename_menu_item_new(context_node->tab);
		} else {
			item = gtk_menu_item_new_with_label(_("Rename terminal..."));
			g_signal_connect_data(item, "activate",
			                      G_CALLBACK(sakura_set_name_dialog_cb),
			                      g_strdup(context_node->tab->terminal_id),
			                      sakura_sidebar_group_id_data_free, 0);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		if (context_node->tab != NULL && context_node->tab->page != NULL) {
			item = gtk_menu_item_new_with_label(
				context_node->tab->page->archived
				? _("Restore session") : _("Archive session"));
			sakura_sidebar_connect_context_action(
				item, "activate", G_CALLBACK(sakura_sidebar_archive_cb), context_node);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
		item = gtk_menu_item_new_with_label(
			page_context != NULL && page_context->page != NULL &&
			page_context->page->panes != NULL &&
			page_context->page->panes->len > 1
			? _("Close session")
			: page_context == NULL && context_node->tab != NULL &&
			  context_node->tab->page != NULL &&
			  context_node->tab->page->panes != NULL &&
			  context_node->tab->page->panes->len > 1
			? _("Close pane") : _("Close terminal"));
		if (page_context == NULL)
			sakura_sidebar_connect_context_action(
				item, "activate", G_CALLBACK(sakura_sidebar_close_terminal_cb),
				context_node);
		else
			sakura_sidebar_connect_context_action(
				item, "activate", G_CALLBACK(sakura_sidebar_close_page_cb),
				page_context);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	} else if (context_node != NULL && context_node->type == SAKURA_SIDEBAR_GROUP) {
		item = gtk_menu_item_new_with_label(
			context_node == sakura.sidebar_root ? _("New terminal") : _("New terminal here"));
		sakura_sidebar_connect_context_action(
			item, "activate", G_CALLBACK(sakura_new_tab_in_scope_cb), context_node);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

		item = gtk_menu_item_new_with_label(
			context_node == sakura.sidebar_root
			? _("New Codex session") : _("New Codex session here"));
		sakura_sidebar_connect_context_action(
			item, "activate", G_CALLBACK(sakura_new_codex_in_scope_cb), context_node);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

		item = gtk_menu_item_new_with_label(_("New Codex session with reasoning"));
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),
		                          sakura_sidebar_codex_reasoning_menu_new(context_node));
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

		item = gtk_menu_item_new_with_label(_("New subgroup"));
		sakura_sidebar_connect_context_action(
			item, "activate", G_CALLBACK(sakura_sidebar_new_group_cb), context_node);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

		item = gtk_menu_item_new_with_label(
			context_node == sakura.sidebar_root ? _("New task") : _("New task here"));
		sakura_sidebar_connect_context_action(
			item, "activate", G_CALLBACK(sakura_sidebar_new_task_cb), context_node);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

		if (context_node != sakura.sidebar_root) {
			item = gtk_menu_item_new_with_label(_("Open Here"));
			gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),
			                          sakura_sidebar_open_here_menu_new(context_node));
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		}

		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
		item = gtk_menu_item_new_with_label(
			context_node->group != NULL && context_node->group->directory != NULL &&
			context_node->group->directory[0] != '\0'
			? _("Change working directory...") : _("Set working directory..."));
		g_signal_connect_data(item, "activate",
		                      G_CALLBACK(sakura_sidebar_set_directory_cb),
		                      g_strdup(context_node->group != NULL
		                               ? context_node->group->id : NULL),
		                      sakura_sidebar_group_id_data_free, 0);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		item = gtk_menu_item_new_with_label(_("Clear working directory"));
		gtk_widget_set_sensitive(item, context_node->group != NULL &&
		                         context_node->group->directory != NULL &&
		                         context_node->group->directory[0] != '\0');
		g_signal_connect_data(item, "activate",
		                      G_CALLBACK(sakura_sidebar_clear_directory_cb),
		                      g_strdup(context_node->group != NULL
		                               ? context_node->group->id : NULL),
		                      sakura_sidebar_group_id_data_free, 0);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

		if (context_node == sakura.sidebar_root) {
			item = gtk_menu_item_new_with_label(_("Tools"));
			gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),
			                          sakura_sidebar_tools_menu_new(context_node));
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
			item = gtk_check_menu_item_new_with_label(_("Show archived"));
			gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),
			                              sakura.show_archived);
			g_signal_connect(item, "toggled",
			                 G_CALLBACK(sakura_sidebar_show_archived_cb), NULL);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		} else {
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
			item = gtk_menu_item_new_with_label(_("Rename group..."));
			sakura_sidebar_connect_context_action(
				item, "activate", G_CALLBACK(sakura_sidebar_rename_group_cb), context_node);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
			if (sakura_sidebar_group_is_archived(context_node->group)) {
				item = gtk_menu_item_new_with_label(_("Restore group"));
				sakura_sidebar_connect_context_action(
					item, "activate", G_CALLBACK(sakura_sidebar_archive_cb), context_node);
				gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
				item = gtk_menu_item_new_with_label(_("Delete permanently"));
				gtk_widget_set_sensitive(item,
				                         sakura_workspace_model_can_remove_group(
					                         sakura.workspace, context_node->group));
				sakura_sidebar_connect_context_action(
					item, "activate", G_CALLBACK(sakura_sidebar_delete_group_cb), context_node);
				gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
			} else {
				item = gtk_menu_item_new_with_label(_("Archive group"));
				sakura_sidebar_connect_context_action(
					item, "activate", G_CALLBACK(sakura_sidebar_archive_cb), context_node);
				gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
			}
		}
	}
	return menu;
}


static SakuraSidebarNode *
sakura_sidebar_drop_target_node(GtkWidget *widget, gint x, gint y,
                                GtkTreeViewDropPosition *drop_position)
{
	GtkTreeView *tree = GTK_TREE_VIEW(widget);
	GtkTreeModel *model = gtk_tree_view_get_model(tree);
	GtkTreePath *path = NULL;
	GtkTreeViewDropPosition position = GTK_TREE_VIEW_DROP_INTO_OR_AFTER;
	GtkTreeIter iter;
	SakuraSidebarNode *node = NULL;
	gboolean valid;

	valid = gtk_tree_view_get_dest_row_at_pos(tree, x, y, &path, &position);
	if (drop_position != NULL)
		*drop_position = position;
	if (valid && path != NULL &&
	    gtk_tree_model_get_iter(model, &iter, path))
		gtk_tree_model_get(model, &iter, SAKURA_SIDEBAR_COLUMN_NODE, &node, -1);
	if (path != NULL)
		gtk_tree_path_free(path);
	return node;
}


static SakuraSidebarNode *
sakura_sidebar_drop_target_group(GtkWidget *widget, gint x, gint y,
                                 GtkTreeViewDropPosition *drop_position)
{
	SakuraSidebarNode *node = sakura_sidebar_drop_target_node(
		widget, x, y, drop_position);

	return node != NULL && node->type == SAKURA_SIDEBAR_GROUP ? node : NULL;
}


static SakuraSidebarNode *
sakura_sidebar_drag_node(GtkWidget *widget)
{
	return sakura_sidebar_action_target_node(g_object_get_data(
		G_OBJECT(widget), "sakura-sidebar-drag-node"));
}


static SakuraPage *
sakura_sidebar_page_for_drag_node(SakuraSidebarNode *node)
{
	if (node == NULL)
		return NULL;
	if (node->type == SAKURA_SIDEBAR_PAGE)
		return node->page;
	return NULL;
}


static SakuraPage *
sakura_sidebar_page_for_drop_node(SakuraSidebarNode *node)
{
	if (node == NULL)
		return NULL;
	if (node->type == SAKURA_SIDEBAR_PAGE)
		return node->page;
	if (node->type == SAKURA_SIDEBAR_TERMINAL && node->tab != NULL)
		return node->tab->page;
	return NULL;
}


gboolean
sakura_sidebar_reorder_page_relative(SakuraPage *source, SakuraPage *target,
                                     GtkTreeViewDropPosition position)
{
	SakuraGroup *target_group;
	SakuraSidebarNode *target_group_node;
	GtkWidget *child;
	gint source_index, target_index, destination;
	gboolean after;

	if (source == NULL || target == NULL || source == target ||
	    source->active_tab == NULL || target->active_tab == NULL ||
	    sakura.notebook == NULL)
		return FALSE;
	target_group = sakura_workspace_model_group_for_session(sakura.workspace, target);
	target_group_node = sakura_sidebar_group_node(target_group);
	if (target_group_node == NULL ||
	    !sakura_sidebar_move_page_to_group(source, target_group_node))
		return FALSE;

	source_index = sakura_page_for_tab(source->active_tab);
	target_index = sakura_page_for_tab(target->active_tab);
	if (source_index < 0 || target_index < 0)
		return FALSE;
	after = position == GTK_TREE_VIEW_DROP_AFTER ||
	        position == GTK_TREE_VIEW_DROP_INTO_OR_AFTER;
	/* Compute the insertion point after removing the source from its old
	 * position. GtkNotebook uses this final zero-based position. */
	destination = target_index;
	if (source_index < target_index)
		destination--;
	if (after)
		destination++;
	child = gtk_notebook_get_nth_page(GTK_NOTEBOOK(sakura.notebook), source_index);
	if (child == NULL)
		return FALSE;
	gtk_notebook_reorder_child(GTK_NOTEBOOK(sakura.notebook), child, destination);
	sakura_notebook_sync_page_order();
	sakura_sidebar_rebuild_projection();
	sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_STRUCTURE |
	                              SAKURA_WORKSPACE_CHANGE_SELECTION);
	sakura_session_mark_dirty();
	return TRUE;
}


gboolean
sakura_sidebar_can_reorder_node_to_group(SakuraSidebarNode *source,
                                          SakuraSidebarNode *target)
{
	if (source == NULL || target == NULL || target->type != SAKURA_SIDEBAR_GROUP)
		return FALSE;
	if (source->type == SAKURA_SIDEBAR_GROUP)
		return source->parent == target->parent;
	if (source->type == SAKURA_SIDEBAR_TASK)
		return source->parent == target;
	return FALSE;
}


static gboolean
sakura_sidebar_group_drop_destination(SakuraSidebarNode *source,
                                      SakuraSidebarNode *target,
                                      GtkTreeViewDropPosition position,
                                      SakuraGroup **parent,
                                      SakuraGroup **sibling,
                                      gboolean *after)
{
	SakuraGroup *target_group, *destination_parent, *destination_sibling = NULL;
	gboolean destination_after = FALSE;

	if (parent != NULL)
		*parent = NULL;
	if (sibling != NULL)
		*sibling = NULL;
	if (after != NULL)
		*after = FALSE;
	if (source == NULL || source->type != SAKURA_SIDEBAR_GROUP ||
	    target == NULL || target->type != SAKURA_SIDEBAR_GROUP ||
	    source->group == NULL || target->group == NULL ||
	    sakura.workspace == NULL)
		return FALSE;

	target_group = target->group;
	if (position == GTK_TREE_VIEW_DROP_INTO_OR_BEFORE ||
	    position == GTK_TREE_VIEW_DROP_INTO_OR_AFTER) {
		destination_parent = target_group;
	} else {
		destination_parent = target_group->parent != NULL
		                    ? target_group->parent : sakura.workspace->root_group;
		destination_sibling = target_group;
		destination_after = position == GTK_TREE_VIEW_DROP_AFTER;
	}
	if (parent != NULL)
		*parent = destination_parent;
	if (sibling != NULL)
		*sibling = destination_sibling;
	if (after != NULL)
		*after = destination_after;
	return sakura_workspace_model_can_move_group(
		sakura.workspace, source->group, destination_parent, destination_sibling);
}


static gboolean
sakura_sidebar_move_group_to_target(SakuraSidebarNode *source,
                                    SakuraSidebarNode *target,
                                    GtkTreeViewDropPosition position)
{
	SakuraGroup *parent, *sibling, *source_group;
	gchar *source_id, *parent_id, *sibling_id;
	gboolean after;
	gboolean result = FALSE;

	if (!sakura_sidebar_group_drop_destination(source, target, position,
	                                           &parent, &sibling, &after))
		return FALSE;
	source_id = g_strdup(source->group->id);
	parent_id = g_strdup(parent->id);
	sibling_id = g_strdup(sibling != NULL ? sibling->id : NULL);
	if (sakura.agent_socket_path != NULL) {
		GError *error = NULL;

		/* Group hierarchy is also held by sakura-agent. Update it first so a
		 * later agent snapshot cannot restore the old parent after a restart. */
		if (!sakura_agent_move_group(&sakura, source_id, parent_id, sibling_id,
		                             after, &error)) {
			if (error != NULL)
				g_warning("Could not move group through sakura-agent: %s",
				          error->message);
			g_clear_error(&error);
			goto out;
		}
	}
	/* The agent request may have delivered a projection rebuild. Resolve all
	 * model objects again before entering the local mutation. */
	source_group = sakura_workspace_group_by_id(source_id);
	parent = sakura_workspace_group_by_id(parent_id);
	sibling = sibling_id != NULL ? sakura_workspace_group_by_id(sibling_id) : NULL;
	if (source_group == NULL || parent == NULL ||
	    (sibling_id != NULL && sibling == NULL))
		goto out;
	sakura_workspace_begin_mutation();
	sakura_sidebar_cancel_pending_selection();
	if (!sakura_workspace_model_move_group(sakura.workspace, source_group,
	                                       parent, sibling, after)) {
		sakura_workspace_end_mutation();
		goto out;
	}
	/* Rebuild the projection so the moved row and all of its descendants are
	 * materialized under the new model parent. Expansion keys are ID-based, so
	 * the user's expanded/collapsed state survives the reparenting. */
	sakura_sidebar_rebuild_projection();
	sakura_sidebar_save_groups();
	sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_STRUCTURE);
	sakura_session_mark_dirty();
	sakura_workspace_end_mutation();
	result = TRUE;
out:
	g_free(source_id);
	g_free(parent_id);
	g_free(sibling_id);
	return result;
}


static void
sakura_sidebar_drag_begin_cb(GtkWidget *widget, GdkDragContext *context,
                             gpointer data)
{
	struct sakura_sidebar_action_target *target = g_object_get_data(
		G_OBJECT(widget), "sakura-sidebar-drag-node");

	(void)context;
	(void)data;
	/* Never fall back to GTK's current selection here. The row under the
	 * pointer is the only authoritative drag source; if its stable ID cannot
	 * be resolved after a projection rebuild, the drag must be rejected rather
	 * than moving whichever row happened to be selected. */
	if (target != NULL && sakura_sidebar_action_target_node(target) == NULL)
		g_debug("Sidebar drag source disappeared before drag began");
}


static void
sakura_sidebar_drag_end_cb(GtkWidget *widget, GdkDragContext *context,
                           gpointer data)
{
	(void)context;
	(void)data;
	g_object_set_data_full(G_OBJECT(widget), "sakura-sidebar-drag-node", NULL,
	                       NULL);
}


static gboolean
sakura_sidebar_drag_motion_cb(GtkWidget *widget, GdkDragContext *context,
                              gint x, gint y, guint time, gpointer data)
{
	GtkTreeViewDropPosition position = GTK_TREE_VIEW_DROP_INTO_OR_AFTER;
	SakuraSidebarNode *target = sakura_sidebar_drop_target_group(widget, x, y,
	                                                              &position);
	SakuraSidebarNode *source = sakura_sidebar_drag_node(widget);
	SakuraSidebarNode *raw_target = sakura_sidebar_drop_target_node(
		widget, x, y, &position);
	SakuraPage *source_page = sakura_sidebar_page_for_drag_node(source);
	SakuraPage *target_page = sakura_sidebar_page_for_drop_node(raw_target);

	(void)data;
	if (source_page != NULL && target_page != NULL && source_page != target_page) {
		gdk_drag_status(context, GDK_ACTION_MOVE, time);
		return TRUE;
	}
	if (source != NULL && source->type == SAKURA_SIDEBAR_TASK &&
	    raw_target != NULL && raw_target->type == SAKURA_SIDEBAR_TASK &&
	    raw_target != source && source->task != NULL && raw_target->task != NULL &&
	    source->task->group == raw_target->task->group &&
	    source->task->parent == raw_target->task->parent) {
		gdk_drag_status(context, GDK_ACTION_MOVE, time);
		return TRUE;
	}
	if (target != NULL) {
		if (sakura_sidebar_page_for_drag_node(source) != NULL) {
			gdk_drag_status(context, GDK_ACTION_MOVE, time);
			return TRUE;
		}
		if (source != NULL && source->type == SAKURA_SIDEBAR_GROUP) {
			if (!sakura_sidebar_group_drop_destination(source, target, position,
			                                           NULL, NULL, NULL)) {
				gdk_drag_status(context, 0, time);
				return TRUE;
			}
			gdk_drag_status(context, GDK_ACTION_MOVE, time);
			return TRUE;
		}
		if (!sakura_sidebar_can_reorder_node_to_group(source, target)) {
			gdk_drag_status(context, 0, time);
			return TRUE;
		}
		gdk_drag_status(context, GDK_ACTION_MOVE, time);
		return TRUE;
	}
	/* Prevent GtkTreeView's reorderable default handler from accepting a
	 * terminal/page row as a drop target. */
	gdk_drag_status(context, 0, time);
	return TRUE;
}


static void
sakura_sidebar_pending_drop_free(gpointer data)
{
	struct sakura_sidebar_pending_drop *pending = data;

	if (pending == NULL)
		return;
	g_free(pending->source_id);
	g_free(pending->target_id);
	g_free(pending);
}


static gboolean
sakura_sidebar_pending_drop_idle(gpointer data)
{
	struct sakura_sidebar_pending_drop *pending = data;
	SakuraSidebarNode *source, *target;
	SakuraPage *source_page, *target_page;
	SakuraTab *active_tab;
	gboolean moved = FALSE;

	if (pending == NULL || sakura.session_shutting_down)
		return G_SOURCE_REMOVE;
	source = sakura_sidebar_find_node_by_identity(
		pending->source_type, pending->source_id);
	target = sakura_sidebar_find_node_by_identity(
		pending->target_type, pending->target_id);
	if (source == NULL || target == NULL)
		goto failed;

	switch (pending->kind) {
	case SAKURA_SIDEBAR_DROP_PAGE_RELATIVE:
		source_page = sakura_sidebar_page_for_drag_node(source);
		target_page = sakura_sidebar_page_for_drop_node(target);
		active_tab = source_page != NULL ? source_page->active_tab : NULL;
		moved = sakura_sidebar_reorder_page_relative(
			source_page, target_page, pending->position);
		if (moved && sakura.workspace->active_tab == active_tab && active_tab != NULL)
			sakura_select_tab(active_tab, TRUE);
		break;
	case SAKURA_SIDEBAR_DROP_PAGE_TO_GROUP:
		source_page = sakura_sidebar_page_for_drag_node(source);
		active_tab = source_page != NULL ? source_page->active_tab : NULL;
		sakura_workspace_begin_mutation();
		moved = sakura_sidebar_move_page_to_group(source_page, target);
		if (moved && sakura.workspace->active_tab == active_tab && active_tab != NULL)
			sakura_select_tab(active_tab, TRUE);
		else if (moved)
			sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_STRUCTURE);
		if (moved)
			sakura_sidebar_save_groups();
		sakura_workspace_end_mutation();
		break;
	case SAKURA_SIDEBAR_DROP_TASK_RELATIVE:
		moved = sakura_sidebar_reorder_task_to_sibling(
			source, target, pending->position);
		break;
	case SAKURA_SIDEBAR_DROP_GROUP:
		moved = sakura_sidebar_move_group_to_target(
			source, target, pending->position);
		break;
	case SAKURA_SIDEBAR_DROP_NODE_TO_GROUP:
		moved = sakura_sidebar_reorder_node_to_group(
			source, target, pending->position);
		break;
	}
	if (moved)
		return G_SOURCE_REMOVE;

failed:
	sakura_sidebar_report_move_failure(
		_("The workspace changed before the drop could be applied. Please try again."));
	return G_SOURCE_REMOVE;
}


static gboolean
sakura_sidebar_drag_drop_cb(GtkWidget *widget, GdkDragContext *context,
                            gint x, gint y, guint time, gpointer data)
{
	GtkTreeViewDropPosition position = GTK_TREE_VIEW_DROP_INTO_OR_AFTER;
	SakuraSidebarNode *target = sakura_sidebar_drop_target_group(widget, x, y,
	                                                              &position);
	SakuraSidebarNode *source = sakura_sidebar_drag_node(widget);
	SakuraSidebarNode *raw_target = sakura_sidebar_drop_target_node(
		widget, x, y, &position);
	SakuraPage *page = sakura_sidebar_page_for_drag_node(source);
	SakuraPage *target_page = sakura_sidebar_page_for_drop_node(raw_target);
	struct sakura_sidebar_pending_drop *pending = NULL;

	(void)data;
	if (page != NULL && target_page != NULL && page != target_page) {
		pending = g_new0(struct sakura_sidebar_pending_drop, 1);
		pending->kind = SAKURA_SIDEBAR_DROP_PAGE_RELATIVE;
		pending->target_type = raw_target->type;
	}
	else if (source != NULL && source->type == SAKURA_SIDEBAR_TASK &&
	    raw_target != NULL && raw_target->type == SAKURA_SIDEBAR_TASK &&
	    raw_target != source && source->task != NULL && raw_target->task != NULL &&
	    source->task->group == raw_target->task->group &&
	    source->task->parent == raw_target->task->parent) {
		pending = g_new0(struct sakura_sidebar_pending_drop, 1);
		pending->kind = SAKURA_SIDEBAR_DROP_TASK_RELATIVE;
		pending->target_type = raw_target->type;
	}
	else if (target != NULL && page != NULL) {
		pending = g_new0(struct sakura_sidebar_pending_drop, 1);
		pending->kind = SAKURA_SIDEBAR_DROP_PAGE_TO_GROUP;
		pending->target_type = target->type;
	}
	if (target != NULL && source == NULL) {
		gtk_drag_finish(context, FALSE, FALSE, time);
		sakura_sidebar_report_move_failure(
			_("The dragged session is no longer available. Please try again."));
		return TRUE;
	}
	if (target != NULL && source != NULL &&
	    source->type == SAKURA_SIDEBAR_TERMINAL) {
		gtk_drag_finish(context, FALSE, FALSE, time);
		sakura_sidebar_report_move_failure(
			_("Individual panes cannot be assigned to groups. Drag the session row instead."));
		return TRUE;
	}
	if (target != NULL && source != NULL &&
	    source->type == SAKURA_SIDEBAR_GROUP) {
		if (sakura_sidebar_group_drop_destination(source, target, position,
		                                          NULL, NULL, NULL)) {
			pending = g_new0(struct sakura_sidebar_pending_drop, 1);
			pending->kind = SAKURA_SIDEBAR_DROP_GROUP;
			pending->target_type = target->type;
		}
	}
	else if (target != NULL &&
	         sakura_sidebar_can_reorder_node_to_group(source, target)) {
		pending = g_new0(struct sakura_sidebar_pending_drop, 1);
		pending->kind = SAKURA_SIDEBAR_DROP_NODE_TO_GROUP;
		pending->target_type = target->type;
	}
	if (pending != NULL) {
		pending->source_type = source->type;
		pending->source_id = g_strdup(source->id);
		pending->target_id = g_strdup(
			pending->kind == SAKURA_SIDEBAR_DROP_PAGE_RELATIVE
			? raw_target->id : target != NULL ? target->id : raw_target->id);
		pending->position = position;
		/* GTK still owns its drag source/destination paths here. Finish the
		 * event first; the idle callback resolves fresh rows by stable ID. */
		gtk_drag_finish(context, TRUE, FALSE, time);
		g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
		                sakura_sidebar_pending_drop_idle, pending,
		                sakura_sidebar_pending_drop_free);
		return TRUE;
	}
	gdk_drag_status(context, 0, time);
	gtk_drag_finish(context, FALSE, FALSE, time);
	if (target != NULL) {
		sakura_sidebar_report_move_failure(
			_("This item cannot be moved into the selected group."));
	}
	return TRUE;
}


static gboolean
sakura_sidebar_event_is_expander(GtkTreeView *tree,
                                 GtkTreePath *path,
                                 GtkTreeViewColumn *column,
                                 gint x)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	GdkRectangle cell_area;
	gint expander_width = 0;

	if (tree == NULL || path == NULL || column == NULL ||
	    column != gtk_tree_view_get_expander_column(tree) ||
	    !gtk_tree_view_get_show_expanders(tree))
		return FALSE;
	model = gtk_tree_view_get_model(tree);
	if (model == NULL || !gtk_tree_model_get_iter(model, &iter, path) ||
	    !gtk_tree_model_iter_has_child(model, &iter))
		return FALSE;
	gtk_tree_view_get_cell_area(tree, path, column, &cell_area);
	/* GTK lays out the expander immediately before the first renderer in the
	 * expander column. Use the actual cell geometry and theme expander size
	 * instead of a fixed screen coordinate; nested rows then work naturally. */
	gtk_widget_style_get(GTK_WIDGET(tree), "expander-size", &expander_width, NULL);
	if (expander_width <= 0)
		expander_width = 16;
	return x >= cell_area.x - expander_width - 4 && x < cell_area.x;
}


static gboolean
sakura_sidebar_toggle_row_idle(gpointer data)
{
	struct sakura_sidebar_pending_toggle *pending = data;
	SakuraSidebarNode *node;
	GtkTreeIter iter;
	GtkTreePath *path;

	if (pending == NULL || pending->tree == NULL ||
	    pending->tree != GTK_TREE_VIEW(sakura.sidebar_tree) ||
	    sakura.session_shutting_down || sakura.sidebar_model == NULL)
		return G_SOURCE_REMOVE;

	node = sakura_sidebar_find_node_by_identity(pending->type, pending->id);
	if (node == NULL || !sakura_sidebar_get_iter(node, &iter))
		return G_SOURCE_REMOVE;
	path = gtk_tree_model_get_path(GTK_TREE_MODEL(sakura.sidebar_model), &iter);
	if (path != NULL) {
		/* The tree view owns the current event's internal row path. Defer the
		 * actual mutation until after GTK finishes dispatching the button event,
		 * then resolve the row again by stable sidebar identity. */
		if (gtk_tree_view_row_expanded(pending->tree, path) != pending->expanded) {
			if (pending->expanded)
				gtk_tree_view_expand_row(pending->tree, path, FALSE);
			else
				gtk_tree_view_collapse_row(pending->tree, path);
		}
		gtk_tree_path_free(path);
	}
	return G_SOURCE_REMOVE;
}


static void
sakura_sidebar_pending_toggle_free(gpointer data)
{
	struct sakura_sidebar_pending_toggle *pending = data;

	if (pending == NULL)
		return;
	if (pending->tree != NULL)
		g_object_unref(pending->tree);
	g_free(pending->id);
	g_free(pending);
}


static void
sakura_sidebar_queue_row_toggle(GtkTreeView *tree, GtkTreePath *path)
{
	struct sakura_sidebar_pending_toggle *pending;
	GtkTreeIter iter;
	SakuraSidebarNode *node = NULL;

	if (tree == NULL || path == NULL || sakura.sidebar_model == NULL ||
	    !gtk_tree_model_get_iter(GTK_TREE_MODEL(sakura.sidebar_model), &iter, path))
		return;
	gtk_tree_model_get(GTK_TREE_MODEL(sakura.sidebar_model), &iter,
	                   SAKURA_SIDEBAR_COLUMN_NODE, &node, -1);
	if (node == NULL || node->id == NULL || node->id[0] == '\0')
		return;

	pending = g_new0(struct sakura_sidebar_pending_toggle, 1);
	pending->tree = GTK_TREE_VIEW(g_object_ref(tree));
	pending->type = node->type;
	pending->id = g_strdup(node->id);
	pending->expanded = !gtk_tree_view_row_expanded(tree, path);
	g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, sakura_sidebar_toggle_row_idle,
	                pending, sakura_sidebar_pending_toggle_free);
}


gboolean
sakura_sidebar_button_press_cb (GtkWidget *widget, GdkEventButton *event, void *data)
{
	GtkTreePath *path = NULL;
	GtkTreeViewColumn *column = NULL;
	GtkTreeIter iter;
	GtkWidget *menu;
	struct sakura_sidebar_node *node;
	gboolean expander_click;

	sakura.last_user_interaction_us = g_get_monotonic_time();

	/* GTK starts a drag before its default selection handling necessarily runs.
	 * Capture the row under the pointer so drag-begin cannot reuse a previously
	 * selected page or task when the user drags an unselected group. */
	if (event->button == GDK_BUTTON_PRIMARY && GTK_IS_TREE_VIEW(widget)) {
		if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), event->x, event->y,
		                                  &path, &column, NULL, NULL)) {
			if (gtk_tree_model_get_iter(GTK_TREE_MODEL(sakura.sidebar_model), &iter, path))
				gtk_tree_model_get(GTK_TREE_MODEL(sakura.sidebar_model), &iter,
				                   SAKURA_SIDEBAR_COLUMN_NODE, &node, -1);
			else
				node = NULL;
			expander_click = sakura_sidebar_event_is_expander(
				GTK_TREE_VIEW(widget), path, column, (gint)event->x);
			if (!expander_click && event->type == GDK_BUTTON_PRESS)
				sakura_sidebar_primary_click(node);
			if (!expander_click)
				g_object_set_data_full(
					G_OBJECT(widget), "sakura-sidebar-drag-node",
					sakura_sidebar_action_target_new(node),
					sakura_sidebar_action_target_destroy);
			else
				g_object_set_data_full(
					G_OBJECT(widget), "sakura-sidebar-drag-node", NULL, NULL);
			if (expander_click && event->type == GDK_BUTTON_PRESS) {
				sakura_sidebar_queue_row_toggle(GTK_TREE_VIEW(widget), path);
				gtk_tree_path_free(path);
				return TRUE;
			}
			gtk_tree_path_free(path);
			path = NULL;
		} else
			g_object_set_data_full(G_OBJECT(widget), "sakura-sidebar-drag-node", NULL,
			                       NULL);
	}

	/* A folder label is a row, not the expander itself. GTK therefore does
	 * not expand it when it receives a double click. Keep single-click
	 * activation intact and toggle only group rows on a primary double click. */
	if (event->button == GDK_BUTTON_PRIMARY &&
	    event->type == GDK_2BUTTON_PRESS &&
	    GTK_IS_TREE_VIEW(widget) &&
	    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), event->x, event->y,
	                                  &path, &column, NULL, NULL)) {
		if (sakura_sidebar_event_is_expander(
				GTK_TREE_VIEW(widget), path, column, (gint)event->x)) {
			gtk_tree_path_free(path);
			return TRUE;
		}
		if (gtk_tree_model_get_iter(GTK_TREE_MODEL(sakura.sidebar_model), &iter, path))
			gtk_tree_model_get(GTK_TREE_MODEL(sakura.sidebar_model), &iter,
			                   SAKURA_SIDEBAR_COLUMN_NODE, &node, -1);
		else
			node = NULL;

		if (node != NULL && node->type == SAKURA_SIDEBAR_GROUP) {
			sakura_sidebar_queue_row_toggle(GTK_TREE_VIEW(widget), path);
			gtk_tree_path_free(path);
			return TRUE;
		}
		gtk_tree_path_free(path);
		return FALSE;
	}

	if (event->button != GDK_BUTTON_SECONDARY)
		return FALSE;

	if (GTK_IS_TREE_VIEW(widget) &&
	    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), event->x, event->y,
	                                  &path, &column, NULL, NULL)) {
		/* Resolve the menu target from the row under the pointer without changing
		 * the active sidebar selection. Context actions use this node directly;
		 * right-clicking a group must not briefly select it and then move the
		 * highlight to one of its default children. */
		if (gtk_tree_model_get_iter(GTK_TREE_MODEL(sakura.sidebar_model), &iter, path))
			gtk_tree_model_get(GTK_TREE_MODEL(sakura.sidebar_model), &iter,
			                   SAKURA_SIDEBAR_COLUMN_NODE, &node, -1);
		else
			node = NULL;
	} else {
		node = sakura.sidebar_root;
	}
	/* A notebook/focus callback may have queued a programmatic selection before
	 * the context menu event arrived. Preserve the current selection while the
	 * menu is open instead of applying that stale request underneath it. */
	sakura_sidebar_cancel_pending_selection();

	menu = sakura_sidebar_context_menu_new(node);

	gtk_widget_show_all(menu);
	gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
	if (path != NULL)
		gtk_tree_path_free(path);

	return TRUE;
}


void
sakura_sidebar_new_group_cb (GtkWidget *widget, void *data)
{
	GtkWidget *dialog, *entry;
	struct sakura_sidebar_action_target *target = data;
	struct sakura_sidebar_node *parent, *node;
	struct sakura_sidebar_node *context_node =
		sakura_sidebar_action_target_node(target);
	SakuraGroup *parent_group;
	gchar *parent_group_id;
	const gchar *title;

	if (target != NULL && context_node == NULL)
		return;

	/* A context-menu invocation carries its clicked group explicitly. The
	 * toolbar has no context node, so derive its destination from the current
	 * terminal/task before falling back to the active scope. In particular, the
	 * All terminals scope must not turn a selected FreeCAD terminal into a root
	 * level group. */
	if (context_node != NULL) {
		parent = context_node->type == SAKURA_SIDEBAR_GROUP
		       ? context_node : sakura_sidebar_group_ancestor(context_node);
	} else {
		parent = sakura_sidebar_group_ancestor(sakura_sidebar_default_parent());
	}
	parent_group = sakura_group_for_sidebar_node(parent);
	parent_group_id = g_strdup(parent_group != NULL ? parent_group->id : "root");
	dialog = gtk_dialog_new_with_buttons(_("New group"),
	                                     GTK_WINDOW(sakura.main_window),
	                                     GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR,
	                                     _("_Cancel"), GTK_RESPONSE_CANCEL,
	                                     _("_Create"), GTK_RESPONSE_ACCEPT,
	                                     NULL);
	entry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(entry), _("Group name"));
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
	                   entry, FALSE, FALSE, 12);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
	gtk_widget_show_all(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		title = gtk_entry_get_text(GTK_ENTRY(entry));
		if (title[0] != '\0') {
			SakuraGroup *model_group;

			/* The dialog runs a nested GTK main loop. Resolve the model parent
			 * again after it closes; an agent snapshot may have rebuilt the
			 * sidebar and freed the original projection node. */
			parent_group = sakura_workspace_group_by_id(parent_group_id);
			parent = parent_group != NULL ? parent_group->sidebar_node : NULL;
			if (parent_group == NULL || parent == NULL)
				goto out;
			if (sakura.agent_socket_path != NULL) {
				GError *error = NULL;

				if (sakura_agent_create_group(&sakura, parent_group->id, title,
				                              NULL, &error)) {
					goto out;
				}
				if (error != NULL)
					g_warning("Could not create group through sakura-agent: %s",
					          error->message);
				g_clear_error(&error);
			}
			gchar *id = g_strdup_printf("group-%u", sakura.workspace->next_group_id++);

			sakura_workspace_begin_mutation();
			model_group = sakura_group_new(id, title, parent_group);
			model_group->order = sakura_workspace_next_group_order(parent_group);
			g_free(id);
			sakura_workspace_model_add_group(sakura.workspace, model_group);
			node = g_new0(struct sakura_sidebar_node, 1);
			node->type = SAKURA_SIDEBAR_GROUP;
			node->id = g_strdup(model_group->id);
			node->title = g_strdup(title);
			node->parent = parent != NULL ? parent : sakura.sidebar_root;
			node->group = model_group;
			model_group->sidebar_node = node;
			sakura_sidebar_insert_node(node);
			sakura_sidebar_reveal_node(node, TRUE);
			sakura_sidebar_select_node_now(node);
			sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_STRUCTURE |
			                              SAKURA_WORKSPACE_CHANGE_SELECTION);
			sakura_session_mark_dirty();
			sakura_sidebar_save_groups();
			sakura_workspace_end_mutation();
		}
	}
out:
	gtk_widget_destroy(dialog);
	g_free(parent_group_id);
}


SakuraSidebarNode *
sakura_sidebar_creation_parent_for_context(SakuraSidebarNode *context)
{
	if (context == NULL)
		context = sakura_sidebar_selected_node();
	if (context != NULL && context->type == SAKURA_SIDEBAR_TASK)
		return context;
	if (context != NULL && context->type == SAKURA_SIDEBAR_PAGE)
		return context->parent != NULL && context->parent->type == SAKURA_SIDEBAR_TASK
			? context->parent : sakura_sidebar_group_node(
			                       sakura_workspace_model_group_for_session(sakura.workspace, context->page));
	if (context != NULL && context->type == SAKURA_SIDEBAR_TERMINAL)
		return context->parent != NULL && context->parent->type == SAKURA_SIDEBAR_PAGE
		     ? sakura_sidebar_creation_parent_for_context(context->parent) :
		       sakura_sidebar_group_node(sakura_workspace_model_group_for_session(sakura.workspace,
			                       context->tab != NULL ? context->tab->page : NULL));
	return context != NULL ? context : sakura_sidebar_selected_group();
}


void
sakura_sidebar_new_task_cb(GtkWidget *widget, void *data)
{
	struct sakura_sidebar_action_target *target = data;
	SakuraSidebarNode *context = sakura_sidebar_action_target_node(target);
	SakuraSidebarNode *parent;
	SakuraGroup *parent_group;
	SakuraTask *parent_task;
	gchar *parent_group_id;
	gchar *parent_task_id;
	GtkWidget *dialog, *entry;
	const gchar *title;
	SakuraTask *task;
	SakuraSidebarNode *node;

	(void)widget;
	if (target != NULL && context == NULL)
		return;
	parent = sakura_sidebar_creation_parent_for_context(context);
	if (parent == NULL ||
	    (parent->type != SAKURA_SIDEBAR_GROUP && parent->type != SAKURA_SIDEBAR_TASK))
		parent = sakura.sidebar_root;
	parent_group = sakura_group_for_sidebar_node(parent);
	parent_group_id = g_strdup(parent_group != NULL ? parent_group->id : "root");
	parent_task = parent->type == SAKURA_SIDEBAR_TASK ? parent->task : NULL;
	parent_task_id = g_strdup(parent_task != NULL ? parent_task->id : NULL);
	dialog = gtk_dialog_new_with_buttons(
		_("New task"), GTK_WINDOW(sakura.main_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR,
		_("_Cancel"), GTK_RESPONSE_CANCEL,
		_("_Create"), GTK_RESPONSE_ACCEPT, NULL);
	entry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(entry), _("Task title"));
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
	                   entry, FALSE, FALSE, 12);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
	gtk_widget_show_all(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		title = gtk_entry_get_text(GTK_ENTRY(entry));
		if (title[0] != '\0') {
			/* Re-resolve both model parents after the nested dialog. */
			parent_group = sakura_workspace_group_by_id(parent_group_id);
			parent_task = parent_task_id != NULL
			            ? sakura_workspace_model_find_task(sakura.workspace,
			                                               parent_task_id) : NULL;
			parent = parent_task != NULL ? parent_task->sidebar_node
			          : parent_group != NULL ? parent_group->sidebar_node : NULL;
			if (parent_group == NULL || parent == NULL ||
			    (parent_task_id != NULL && parent_task == NULL))
				goto out;
			if (sakura.agent_socket_path != NULL) {
				GError *error = NULL;

				if (sakura_agent_create_task(
						&sakura, parent_group->id,
						parent_task != NULL ? parent_task->id : "root", title,
						"local", NULL, NULL, &error)) {
					goto out;
				}
				if (error != NULL)
					g_warning("Could not create task through sakura-agent: %s",
					          error->message);
				g_clear_error(&error);
			}
			sakura_workspace_begin_mutation();
			task = g_new0(SakuraTask, 1);
			task->id = g_strdup_printf("task-%u", sakura.workspace->next_task_id++);
			task->title = g_strdup(title);
			task->provider = g_strdup("local");
			task->status = SAKURA_TASK_READY;
			task->parent = parent_task;
			task->group = parent_group;
			task->order = sakura_workspace_next_task_order(task->group, task->parent);
			node = g_new0(SakuraSidebarNode, 1);
			node->type = SAKURA_SIDEBAR_TASK;
			node->id = g_strdup(task->id);
			node->parent = parent;
			node->task = task;
			task->sidebar_node = node;
			sakura_workspace_model_add_task(sakura.workspace, task);
			/* Keep the newly-created row in sync with the task model before it
			 * enters the tree. Rename uses this same synchronization path. */
			sakura_task_update_row(task);
			sakura_sidebar_insert_node(node);
			sakura_sidebar_reveal_node(node, TRUE);
			sakura_sidebar_select_node_now(node);
			sakura.workspace->active_task = task;
			sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_STRUCTURE |
			                              SAKURA_WORKSPACE_CHANGE_SELECTION);
			sakura_session_mark_dirty();
			sakura_workspace_end_mutation();
		}
	}
	out:
	gtk_widget_destroy(dialog);
	g_free(parent_group_id);
	g_free(parent_task_id);
}


static void
sakura_sidebar_rename_task_cb(GtkWidget *widget, void *data)
{
	const gchar *task_id = data;
	SakuraTask *task = sakura_workspace_model_find_task(sakura.workspace, task_id);
	gchar *stable_task_id;
	GtkWidget *dialog, *entry;
	const gchar *title;

	(void)widget;
	if (task == NULL)
		return;
	stable_task_id = g_strdup(task->id);
	dialog = gtk_dialog_new_with_buttons(
		_("Rename task"), GTK_WINDOW(sakura.main_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR,
		_("_Cancel"), GTK_RESPONSE_CANCEL,
		_("_Apply"), GTK_RESPONSE_ACCEPT, NULL);
	entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), task->title != NULL ? task->title : "");
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
	                   entry, FALSE, FALSE, 12);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
	gtk_widget_show_all(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		title = gtk_entry_get_text(GTK_ENTRY(entry));
		if (title[0] != '\0') {
			/* Re-resolve after the nested dialog and after any agent event. */
			task = sakura_workspace_model_find_task(sakura.workspace,
			                                        stable_task_id);
			if (task == NULL)
				goto out;
			if (sakura.agent_socket_path != NULL) {
				GError *error = NULL;

				if (sakura_agent_update_task(&sakura, task->id, title, &error))
					goto out;
				if (error != NULL)
					g_warning("Could not rename task through sakura-agent: %s",
					          error->message);
				g_clear_error(&error);
			}
			sakura_workspace_begin_mutation();
			g_free(task->title);
			task->title = g_strdup(title);
			sakura_task_update_row(task);
			sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_METADATA);
			sakura_session_mark_dirty();
			sakura_workspace_end_mutation();
		}
	}
	out:
	gtk_widget_destroy(dialog);
	g_free(stable_task_id);
}


static gboolean
sakura_sidebar_agent_archive_node(SakuraSidebarNode *node)
{
	GError *error = NULL;
	gboolean archived;
	gboolean result = FALSE;
	SakuraPage *page = NULL;

	if (node == NULL || sakura.agent_socket_path == NULL)
		return FALSE;
	if (node->type == SAKURA_SIDEBAR_TASK && node->task != NULL) {
		archived = !sakura_sidebar_task_is_archived(node->task);
		result = sakura_agent_set_task_archived(&sakura, node->task->id,
		                                      archived, &error);
	} else if (node->type == SAKURA_SIDEBAR_GROUP && node->group != NULL) {
		archived = !sakura_sidebar_group_is_archived(node->group);
		result = sakura_agent_set_group_archived(&sakura, node->group->id,
	                                        archived, &error);
	} else if (node->type == SAKURA_SIDEBAR_PAGE && node->page != NULL) {
		page = node->page;
	} else if (node->type == SAKURA_SIDEBAR_TERMINAL && node->tab != NULL) {
		page = node->tab->page;
	}
	if (page != NULL) {
		archived = !page->archived;
		result = sakura_agent_set_page_archived(
			&sakura, page->id, archived, &error);
	} else if (node->type != SAKURA_SIDEBAR_TASK &&
	           node->type != SAKURA_SIDEBAR_GROUP) {
		return FALSE;
	}
	if (result)
		return TRUE;
	if (error != NULL)
		g_warning("Could not change archive state through sakura-agent: %s",
		          error->message);
	g_clear_error(&error);
	return FALSE;
}


static void
sakura_sidebar_archive_cb(GtkWidget *widget, void *data)
{
	struct sakura_sidebar_action_target *target = data;
	SakuraSidebarNode *node = sakura_sidebar_action_target_node(target);
	gboolean archive;
	SakuraTask *task = NULL;
	SakuraGroup *group = NULL;
	SakuraPage *page = NULL;
	SakuraTask *active_task;
	SakuraGroup *active_group;
	gboolean was_active_page;

	(void)widget;
	if (node == NULL)
		return;
	if (sakura_sidebar_agent_archive_node(node))
		return;
	/* The agent request may process events before returning. Resolve the
	 * projection node again instead of retaining the pre-request pointer. */
	node = sakura_sidebar_action_target_node(target);
	if (node == NULL)
		return;
	if (node->type == SAKURA_SIDEBAR_TASK)
		task = node->task;
	else if (node->type == SAKURA_SIDEBAR_GROUP)
		group = node->group;
	else if (node->type == SAKURA_SIDEBAR_PAGE)
		page = node->page;
	else if (node->type == SAKURA_SIDEBAR_TERMINAL && node->tab != NULL)
		page = node->tab->page;
	else
		return;

	archive = task != NULL ? !sakura_sidebar_task_is_archived(task) :
	          group != NULL ? !sakura_sidebar_group_is_archived(group) :
	          page != NULL ? !page->archived : FALSE;
	active_task = sakura.workspace->active_task;
	active_group = sakura.workspace->active_group;
	was_active_page = page != NULL && sakura.workspace->active_page == page;
	sakura_workspace_begin_mutation();
	if (task != NULL)
		sakura_workspace_model_set_task_archived(sakura.workspace, task, archive);
	else if (group != NULL)
		sakura_workspace_model_set_group_archived(sakura.workspace, group, archive);
	else
		page->archived = archive;

	if (archive && task != NULL && sakura_task_is_within(active_task, task)) {
		sakura.workspace->active_task = NULL;
		if (task->group != NULL && !sakura_sidebar_group_is_archived(task->group)) {
			sakura.workspace->active_group = task->group;
			sakura.active_group_scope = task->group->sidebar_node;
		} else {
			sakura.workspace->active_group = sakura.workspace->root_group;
			sakura.active_group_scope = sakura.sidebar_root;
		}
	} else if (archive && group != NULL &&
	           (sakura_sidebar_group_is_archived(active_group) ||
	            (active_task != NULL &&
	             sakura_sidebar_group_is_archived(active_task->group)))) {
		sakura.workspace->active_task = NULL;
		sakura.workspace->active_group = sakura.workspace->root_group;
		sakura.active_group_scope = sakura.sidebar_root;
	}
	if (archive && page != NULL && was_active_page) {
		sakura.workspace->active_page = NULL;
		sakura.workspace->active_tab = NULL;
	}
	sakura_sidebar_rebuild_projection();
	sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_STRUCTURE |
	                              SAKURA_WORKSPACE_CHANGE_SCOPE |
	                              SAKURA_WORKSPACE_CHANGE_SELECTION |
	                              SAKURA_WORKSPACE_CHANGE_METADATA);
	sakura_session_mark_dirty();
	sakura_sidebar_save_groups();
	sakura_workspace_end_mutation();
	if (page != NULL || archive)
		sakura_select_scope_default();
}


static void
sakura_sidebar_delete_task_cb(GtkWidget *widget, void *data)
{
	const gchar *task_id = data;
	SakuraTask *task = sakura_workspace_model_find_task(sakura.workspace, task_id);
	gchar *stable_task_id;
	GtkTreeIter iter;
	gboolean was_active_task;
	GtkWidget *dialog;
	gint response;

	(void)widget;
	if (task == NULL)
		return;
	stable_task_id = g_strdup(task->id);
	if (!sakura_sidebar_task_is_archived(task))
		goto out_no_dialog;
	if (!sakura_workspace_model_can_remove_task(sakura.workspace, task)) {
		dialog = gtk_message_dialog_new(
			GTK_WINDOW(sakura.main_window), GTK_DIALOG_MODAL,
			GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
			_("Only empty archived tasks can be deleted permanently."));
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
		goto out_no_dialog;
	}
	dialog = gtk_message_dialog_new(GTK_WINDOW(sakura.main_window), GTK_DIALOG_MODAL,
	                                GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
	                                _("Delete this archived task permanently?"));
	gtk_message_dialog_format_secondary_text(
		GTK_MESSAGE_DIALOG(dialog),
		_("This action cannot be undone."));
	gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Cancel"), GTK_RESPONSE_CANCEL);
	gtk_dialog_add_button(GTK_DIALOG(dialog), _("Delete permanently"), GTK_RESPONSE_ACCEPT);
	response = gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
	if (response != GTK_RESPONSE_ACCEPT)
		goto out_no_dialog;

	/* Revalidate after confirmation; the task may have been changed or removed
	 * while the nested dialog was running. */
	task = sakura_workspace_model_find_task(sakura.workspace, stable_task_id);
	if (task == NULL || !sakura_sidebar_task_is_archived(task) ||
	    !sakura_workspace_model_can_remove_task(sakura.workspace, task))
		goto out_no_dialog;
	if (sakura.agent_socket_path != NULL) {
		GError *error = NULL;

		if (sakura_agent_delete_task(&sakura, task->id, &error))
			goto out_no_dialog;
		if (error != NULL)
			g_warning("Could not delete task through sakura-agent: %s",
			          error->message);
		g_clear_error(&error);
	}
	sakura_workspace_begin_mutation();
	sakura_sidebar_cancel_pending_selection();
	was_active_task = sakura.workspace->active_task == task;
	if (task->sidebar_node != NULL &&
	    sakura_sidebar_get_iter(task->sidebar_node, &iter))
		sakura_sidebar_remove_node_row(task->sidebar_node);
	if (was_active_task)
		sakura.workspace->active_task = NULL;
	if (task->sidebar_node != NULL) {
		sakura_sidebar_free_node(task->sidebar_node);
		task->sidebar_node = NULL;
	}
	sakura_workspace_model_remove_task(sakura.workspace, task);
	sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_STRUCTURE |
	                              SAKURA_WORKSPACE_CHANGE_SELECTION);
	if (was_active_task)
		sakura_select_scope_default();
	sakura_session_mark_dirty();
	sakura_workspace_end_mutation();
	out_no_dialog:
	g_free(stable_task_id);
}


void
sakura_sidebar_attach_page_to_task_cb(GtkWidget *widget, void *data)
{
	SakuraTask *task = sakura_workspace_model_find_task(sakura.workspace, data);
	SakuraTab *tab = sakura.workspace->active_tab;

	(void)widget;
	if (task == NULL || tab == NULL || tab->page == NULL)
		return;
	sakura_task_attach_page(task, tab->page);
	sakura.workspace->active_task = task;
	sakura_select_tab(tab, TRUE);
}


static void
sakura_sidebar_move_page_to_task_cb(GtkWidget *widget, void *data)
{
	SakuraTask *task = sakura_workspace_model_find_task(sakura.workspace, data);
	const gchar *page_id = g_object_get_data(G_OBJECT(widget), "sakura-task-page-id");
	SakuraPage *page = sakura_sidebar_page_by_id(page_id);

	if (task == NULL)
		return;
	if (page == NULL && sakura.workspace->active_tab != NULL)
		page = sakura.workspace->active_tab->page;
	if (page == NULL)
		return;
	sakura_task_attach_page(task, page);
	sakura.workspace->active_task = task;
	if (page->active_tab != NULL)
		sakura_select_tab(page->active_tab, TRUE);
}


static SakuraPage *
sakura_sidebar_first_task_page(SakuraTask *task)
{
	guint index;

	if (task == NULL || sakura.workspace->pages == NULL)
		return NULL;
	for (index = 0; index < sakura.workspace->pages->len; index++) {
		SakuraPage *page = g_ptr_array_index(sakura.workspace->pages, index);
		if (page != NULL && page->task == task)
			return page;
	}
	if (sakura.workspace->tasks != NULL) {
		for (index = 0; index < sakura.workspace->tasks->len; index++) {
			SakuraTask *child = g_ptr_array_index(sakura.workspace->tasks, index);
			SakuraPage *page;

			if (child == NULL || child->parent != task)
				continue;
			page = sakura_sidebar_first_task_page(child);
			if (page != NULL)
				return page;
		}
	}
	return NULL;
}


void
sakura_sidebar_task_start_cb(GtkWidget *widget, void *data)
{
	SakuraTask *task = sakura_workspace_model_find_task(sakura.workspace, data);
	SakuraPage *page;

	(void)widget;
	if (task == NULL)
		return;
	if (sakura.agent_socket_path != NULL) {
		GError *error = NULL;

		if (!sakura_agent_set_task_status(
				&sakura, task->id, SAKURA_TASK_WORKING, &error)) {
			if (error != NULL)
				g_warning("Could not start task through sakura-agent: %s",
				          error->message);
			g_clear_error(&error);
			return;
		}
	}
	sakura_workspace_begin_mutation();
	if (task->sidebar_node != NULL)
		sakura_sidebar_prepare_context(task->sidebar_node);
	else {
		sakura.workspace->active_task = task;
		sakura_sidebar_set_scope(sakura_sidebar_group_node(sakura_workspace_model_group_for_task(sakura.workspace, task)));
	}
	task->status = SAKURA_TASK_WORKING;
	sakura_task_update_row(task);
	page = sakura_sidebar_first_task_page(task);
	if (page != NULL && page->active_tab != NULL)
		sakura_select_tab(page->active_tab, TRUE);
	else
		sakura_new_tab_for_task(task);
	sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_METADATA |
	                              SAKURA_WORKSPACE_CHANGE_SELECTION);
	sakura_session_mark_dirty();
	sakura_workspace_end_mutation();
}


static void
sakura_sidebar_set_directory_cb(GtkWidget *widget, void *data)
{
	SakuraGroup *group;
	gchar *group_id = NULL;
	GtkWidget *dialog;
	const gchar *current_directory;
	gchar *directory = NULL;
	gchar *canonical = NULL;

	(void)widget;
	group_id = g_strdup(data);
	if (group_id == NULL)
		return;
	group = sakura_workspace_group_by_id(group_id);
	if (group == NULL) {
		g_free(group_id);
		return;
	}
	/* gtk_dialog_run() enters a nested main loop. Agent events can rebuild the
	 * sidebar during that loop. Keep only the stable model identity across the
	 * dialog and resolve the live group after it closes. */

	dialog = gtk_file_chooser_dialog_new(
		_("Set group working directory"), GTK_WINDOW(sakura.main_window),
		GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
		_("_Cancel"), GTK_RESPONSE_CANCEL,
		_("_Select"), GTK_RESPONSE_ACCEPT,
		NULL);
	gtk_file_chooser_set_create_folders(GTK_FILE_CHOOSER(dialog), TRUE);
	current_directory = sakura_workspace_group_directory(group);
	if (current_directory != NULL &&
	    g_file_test(current_directory, G_FILE_TEST_IS_DIR))
		gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), current_directory);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		directory = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		if (directory != NULL && g_file_test(directory, G_FILE_TEST_IS_DIR)) {
			group = sakura_workspace_group_by_id(group_id);
			if (group == NULL) {
				g_warning("Could not set directory: group no longer exists");
				goto out;
			}
			canonical = g_canonicalize_filename(directory, NULL);
			if (sakura.agent_socket_path != NULL) {
				GError *error = NULL;

				if (sakura_agent_update_group(
						&sakura, group->id, group->title,
						canonical, &error)) {
					goto out;
				}
				if (error != NULL)
					g_warning("Could not set group directory through sakura-agent: %s",
					          error->message);
				g_clear_error(&error);
			}
			sakura_workspace_begin_mutation();
			g_free(group->directory);
			group->directory = canonical;
			canonical = NULL;
			sakura_sidebar_refresh_group_rows();
			sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_METADATA);
			sakura_session_mark_dirty();
			sakura_sidebar_save_groups();
			sakura_workspace_end_mutation();
		}
	}

out:
	gtk_widget_destroy(dialog);
	g_free(directory);
	g_free(canonical);
	g_free(group_id);
}


static void
sakura_sidebar_clear_directory_cb(GtkWidget *widget, void *data)
{
	SakuraGroup *group;
	gchar *group_id;

	(void)widget;
	group_id = g_strdup(data);
	group = sakura_workspace_group_by_id(group_id);
	if (group == NULL || group->directory == NULL) {
		g_free(group_id);
		return;
	}
	if (sakura.agent_socket_path != NULL) {
		GError *error = NULL;

		if (sakura_agent_update_group(&sakura, group->id,
		                              group->title, NULL, &error)) {
			g_free(group_id);
			return;
		}
		if (error != NULL)
			g_warning("Could not clear group directory through sakura-agent: %s",
			          error->message);
		g_clear_error(&error);
	}
	sakura_workspace_begin_mutation();
	g_clear_pointer(&group->directory, g_free);
	sakura_sidebar_refresh_group_rows();
	sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_METADATA);
	sakura_session_mark_dirty();
	sakura_sidebar_save_groups();
	sakura_workspace_end_mutation();
	g_free(group_id);
}


static void
sakura_sidebar_rename_group_cb (GtkWidget *widget, void *data)
{
	struct sakura_sidebar_action_target *target = data;
	struct sakura_sidebar_node *node = sakura_sidebar_action_target_node(target);
	SakuraGroup *group;
	gchar *group_id;
	GtkWidget *dialog, *entry;
	GtkTreeIter iter;
	const gchar *title;

	if (node == NULL || node->type != SAKURA_SIDEBAR_GROUP)
		return;
	group_id = g_strdup(node->group != NULL ? node->group->id : NULL);
	if (group_id == NULL)
		return;

	dialog = gtk_dialog_new_with_buttons(_("Rename group"),
	                                     GTK_WINDOW(sakura.main_window),
	                                     GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR,
	                                     _("_Cancel"), GTK_RESPONSE_CANCEL,
	                                     _("_Apply"), GTK_RESPONSE_ACCEPT,
	                                     NULL);
	entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), node->title);
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
	                   entry, FALSE, FALSE, 12);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
	gtk_widget_show_all(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		title = gtk_entry_get_text(GTK_ENTRY(entry));
		if (title[0] != '\0') {
			/* Re-resolve the group after the nested dialog. */
			group = sakura_workspace_group_by_id(group_id);
			node = group != NULL ? group->sidebar_node : NULL;
			if (group == NULL || node == NULL)
				goto out;
			if (sakura.agent_socket_path != NULL) {
				GError *error = NULL;

				if (sakura_agent_update_group(
						&sakura, group->id, title,
						group->directory, &error)) {
					goto out;
				}
				if (error != NULL)
					g_warning("Could not rename group through sakura-agent: %s",
					          error->message);
				g_clear_error(&error);
			}
			sakura_workspace_begin_mutation();
			g_free(group->title);
			group->title = g_strdup(title);
			sakura_sidebar_update_group_row(node);
			if (sakura_sidebar_get_iter(node, &iter))
				sakura_sidebar_set_node_row(node, &iter);
			sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_METADATA);
			sakura_sidebar_save_groups();
			sakura_workspace_end_mutation();
		}
	}
	out:
	gtk_widget_destroy(dialog);
	g_free(group_id);
}


static void
sakura_sidebar_delete_group_cb (GtkWidget *widget, void *data)
{
	struct sakura_sidebar_action_target *target = data;
	struct sakura_sidebar_node *node = sakura_sidebar_action_target_node(target);
	SakuraGroup *model_group;
	gchar *group_id;
	GtkTreeIter iter;
	GtkWidget *dialog;
	gint response;

	if (node == NULL || node == sakura.sidebar_root ||
	    node->type != SAKURA_SIDEBAR_GROUP ||
	    !sakura_sidebar_get_iter(node, &iter))
		return;
	if (!sakura_sidebar_group_is_archived(node->group))
		return;
	group_id = g_strdup(node->group != NULL ? node->group->id : NULL);
	if (group_id == NULL)
		return;

	if (gtk_tree_model_iter_has_child(GTK_TREE_MODEL(sakura.sidebar_model), &iter) ||
	    !sakura_workspace_model_can_remove_group(sakura.workspace, node->group)) {
		GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(sakura.main_window),
		                                           GTK_DIALOG_MODAL,
		                                           GTK_MESSAGE_INFO,
		                                           GTK_BUTTONS_OK,
		                                           _("Only empty archived groups can be deleted permanently."));
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
		g_free(group_id);
		return;
	}
	dialog = gtk_message_dialog_new(GTK_WINDOW(sakura.main_window), GTK_DIALOG_MODAL,
	                                GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
	                                _("Delete this archived group permanently?"));
	gtk_message_dialog_format_secondary_text(
		GTK_MESSAGE_DIALOG(dialog),
		_("This action cannot be undone."));
	gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Cancel"), GTK_RESPONSE_CANCEL);
	gtk_dialog_add_button(GTK_DIALOG(dialog), _("Delete permanently"), GTK_RESPONSE_ACCEPT);
	response = gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
	if (response != GTK_RESPONSE_ACCEPT)
		goto out;

	/* The confirmation dialog also runs a nested main loop. Revalidate the
	 * model object and reacquire its iterator before mutating the tree. */
	model_group = sakura_workspace_group_by_id(group_id);
	node = model_group != NULL ? model_group->sidebar_node : NULL;
	if (model_group == NULL || node == NULL || node == sakura.sidebar_root ||
	    !sakura_sidebar_group_is_archived(model_group) ||
	    !sakura_sidebar_get_iter(node, &iter) ||
	    gtk_tree_model_iter_has_child(GTK_TREE_MODEL(sakura.sidebar_model), &iter) ||
	    !sakura_workspace_model_can_remove_group(sakura.workspace, model_group))
		goto out;
	if (sakura.agent_socket_path != NULL) {
		GError *error = NULL;

		if (sakura_agent_delete_group(&sakura, model_group->id, &error))
			goto out;
		if (error != NULL)
			g_warning("Could not delete group through sakura-agent: %s",
			          error->message);
		g_clear_error(&error);
	}

	sakura_workspace_begin_mutation();
	if (sakura_active_group_model() == model_group) {
		sakura_sidebar_set_scope(sakura.sidebar_root);
		sakura_select_scope_default();
	}
	gtk_tree_store_remove(sakura.sidebar_model, &iter);
	sakura_sidebar_free_node(node);
	sakura_workspace_model_remove_group(sakura.workspace, model_group);
	sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_STRUCTURE |
	                              SAKURA_WORKSPACE_CHANGE_SCOPE |
	                              SAKURA_WORKSPACE_CHANGE_SELECTION);
	sakura_session_mark_dirty();
	sakura_sidebar_save_groups();
	sakura_workspace_end_mutation();
	out:
	g_free(group_id);
}


struct sakura_sidebar_node *
sakura_sidebar_find_group_by_id (const gchar *id)
{
	GList *group;

	if (id == NULL || sakura.workspace == NULL)
		return NULL;
	if (g_strcmp0(id, "root") == 0)
		return sakura.workspace->root_group != NULL
		     ? sakura.workspace->root_group->sidebar_node : NULL;
	for (group = sakura.workspace->groups; group != NULL; group = group->next) {
		SakuraGroup *model_group = group->data;
		if (g_strcmp0(model_group->id, id) == 0)
			return model_group->sidebar_node;
	}
	return NULL;
}


static SakuraSidebarNode *
sakura_sidebar_find_container_by_id(const gchar *id)
{
	SakuraSidebarNode *node;
	SakuraTask *task;

	if (id == NULL)
		return NULL;
	node = sakura_sidebar_find_group_by_id(id);
	if (node != NULL)
		return node;
	task = sakura_workspace_model_find_task(sakura.workspace, id);
	return task != NULL ? task->sidebar_node : NULL;
}


static SakuraSidebarNode *
sakura_workspace_restore_page_parent(const SakuraSessionPageRecord *page_record,
	                                    const SakuraSessionTabRecord *tab_record)
{
	SakuraSidebarNode *parent;

	if (page_record != NULL && page_record->task_id != NULL &&
	    page_record->task_id[0] != '\0') {
		parent = sakura_sidebar_find_container_by_id(page_record->task_id);
		if (parent != NULL)
			return parent;
	}
	if (page_record != NULL && page_record->group_id != NULL &&
	    page_record->group_id[0] != '\0') {
		parent = sakura_sidebar_find_container_by_id(page_record->group_id);
		if (parent != NULL)
			return parent;
	}
	return tab_record != NULL
	     ? sakura_sidebar_find_container_by_id(tab_record->parent_id) : NULL;
}


static guint
sakura_workspace_next_group_order(SakuraGroup *parent)
{
	guint next = 0;

	for (GList *group_link = sakura.workspace->groups; group_link != NULL;
	     group_link = group_link->next) {
		SakuraGroup *group = group_link->data;

		if (group != NULL && group != sakura.workspace->root_group &&
		    group->parent == parent && group->order >= next)
			next = group->order + 1;
	}
	return next;
}


static guint
sakura_workspace_next_task_order(SakuraGroup *group, SakuraTask *parent)
{
	guint index, next = 0;

	for (index = 0; sakura.workspace->tasks != NULL && index < sakura.workspace->tasks->len; index++) {
		SakuraTask *task = g_ptr_array_index(sakura.workspace->tasks, index);

		if (task != NULL && task->group == group && task->parent == parent &&
		    task->order >= next)
			next = task->order + 1;
	}
	return next;
}


static void
sakura_sidebar_update_model_order_for_parent(GtkTreeModel *model,
                                             GtkTreeIter *parent_iter)
{
	GtkTreeIter iter;
	gboolean valid;
	guint group_order = 0, task_order = 0;

	valid = parent_iter == NULL
		? gtk_tree_model_get_iter_first(model, &iter)
		: gtk_tree_model_iter_children(model, &iter, parent_iter);
	while (valid) {
		SakuraSidebarNode *node = NULL;

		gtk_tree_model_get(model, &iter, SAKURA_SIDEBAR_COLUMN_NODE, &node, -1);
		if (node != NULL && node->type == SAKURA_SIDEBAR_GROUP &&
		    node->group != NULL && node != sakura.sidebar_root)
			node->group->order = group_order++;
		else if (node != NULL && node->type == SAKURA_SIDEBAR_TASK &&
		         node->task != NULL)
			node->task->order = task_order++;
		valid = gtk_tree_model_iter_next(model, &iter);
	}
}


static void
sakura_sidebar_sync_projection_links_for_iter (GtkTreeModel *model,
                                               GtkTreeIter *parent_iter,
                                               struct sakura_sidebar_node *parent_node)
{
	GtkTreeIter iter;
	gboolean valid;

	valid = parent_iter == NULL
		? gtk_tree_model_get_iter_first(model, &iter)
		: gtk_tree_model_iter_children(model, &iter, parent_iter);
	while (valid) {
		struct sakura_sidebar_node *node = NULL;

		gtk_tree_model_get(model, &iter, SAKURA_SIDEBAR_COLUMN_NODE, &node, -1);
		if (node != NULL) {
			/* The root is a model row, but must never become its own parent. */
			node->parent = node == sakura.sidebar_root
				? NULL
				: (parent_node != NULL ? parent_node : sakura.sidebar_root);
			if (node->type == SAKURA_SIDEBAR_GROUP) {
				if (node->group != NULL) {
					node->group->sidebar_node = node;
				}
				sakura_sidebar_update_group_row(node);
			} else if (node->type == SAKURA_SIDEBAR_TASK) {
				/* The task registry owns parent/group. A sidebar row only
				 * re-establishes the optional projection link. */
				if (node->task != NULL)
					node->task->sidebar_node = node;
			} else if (node->type == SAKURA_SIDEBAR_PAGE) {
				/* Page ownership is also model state. Do not infer it from a
				 * potentially stale or partial sidebar projection. */
				if (node->page != NULL)
					node->page->sidebar_node = node;
			}
			if (node->type == SAKURA_SIDEBAR_GROUP ||
			    node->type == SAKURA_SIDEBAR_TASK ||
			    node->type == SAKURA_SIDEBAR_PAGE)
				sakura_sidebar_sync_projection_links_for_iter(model, &iter, node);
		}
		valid = gtk_tree_model_iter_next(model, &iter);
	}
}


void
sakura_sidebar_sync_projection_links (void)
{
	if (sakura.sidebar_model != NULL)
		sakura_sidebar_sync_projection_links_for_iter(GTK_TREE_MODEL(sakura.sidebar_model), NULL, NULL);
}


static void
sakura_sidebar_save_groups (void)
{
	GPtrArray *ids, *parents, *titles, *directories;
	GPtrArray *ordered_groups;
	guint index;

	sakura_session_accept_changes();
	sakura_sidebar_refresh_group_rows();
	sakura_sidebar_refresh_tab_rows();
	if (sakura.cfg == NULL)
		return;

	ids = g_ptr_array_new_with_free_func(g_free);
	parents = g_ptr_array_new_with_free_func(g_free);
	titles = g_ptr_array_new_with_free_func(g_free);
	directories = g_ptr_array_new_with_free_func(g_free);
	ordered_groups = sakura_workspace_model_ordered_groups(sakura.workspace);
	for (index = 0; index < ordered_groups->len; index++) {
		SakuraGroup *group = g_ptr_array_index(ordered_groups, index);

		g_ptr_array_add(ids, g_strdup(group->id));
		g_ptr_array_add(parents, g_strdup(group->parent != NULL
		                                  ? group->parent->id : "root"));
		g_ptr_array_add(titles, g_strdup(group->title != NULL ? group->title : ""));
		g_ptr_array_add(directories, g_strdup(group->directory != NULL
		                                     ? group->directory : ""));
	}

	if (ids->len == 0) {
		g_key_file_remove_key(sakura.cfg, SAKURA_CONFIG_GROUP, "sidebar_group_ids", NULL);
		g_key_file_remove_key(sakura.cfg, SAKURA_CONFIG_GROUP, "sidebar_group_parents", NULL);
		g_key_file_remove_key(sakura.cfg, SAKURA_CONFIG_GROUP, "sidebar_group_titles", NULL);
		g_key_file_remove_key(sakura.cfg, SAKURA_CONFIG_GROUP,
		                      "sidebar_group_directories", NULL);
	} else {
		g_key_file_set_string_list(sakura.cfg, SAKURA_CONFIG_GROUP, "sidebar_group_ids",
		                           (const gchar * const *)ids->pdata, ids->len);
		g_key_file_set_string_list(sakura.cfg, SAKURA_CONFIG_GROUP, "sidebar_group_parents",
		                           (const gchar * const *)parents->pdata, parents->len);
		g_key_file_set_string_list(sakura.cfg, SAKURA_CONFIG_GROUP, "sidebar_group_titles",
		                           (const gchar * const *)titles->pdata, titles->len);
		g_key_file_set_string_list(sakura.cfg, SAKURA_CONFIG_GROUP,
		                           "sidebar_group_directories",
		                           (const gchar * const *)directories->pdata,
		                           directories->len);
	}
	if (sakura.workspace->root_group != NULL && sakura.workspace->root_group->directory != NULL &&
	    sakura.workspace->root_group->directory[0] != '\0')
		g_key_file_set_string(sakura.cfg, SAKURA_CONFIG_GROUP,
		                      "sidebar_root_directory", sakura.workspace->root_group->directory);
	else
		g_key_file_remove_key(sakura.cfg, SAKURA_CONFIG_GROUP,
		                      "sidebar_root_directory", NULL);
	sakura.config_modified = TRUE;
	sakura_session_mark_dirty();
	g_ptr_array_free(ids, TRUE);
	g_ptr_array_free(parents, TRUE);
	g_ptr_array_free(titles, TRUE);
	g_ptr_array_free(directories, TRUE);
	g_ptr_array_unref(ordered_groups);
}


static SakuraSessionLayoutRecord *
sakura_workspace_layout_record(GHashTable *records, const gchar *id)
{
	return id != NULL ? g_hash_table_lookup(records, id) : NULL;
}


static SakuraSessionTabRecord *
sakura_workspace_tab_record(GHashTable *records, const gchar *terminal_id)
{
	return terminal_id != NULL ? g_hash_table_lookup(records, terminal_id) : NULL;
}


static void
sakura_workspace_restore_tab_state(SakuraTab *tab,
                                    const SakuraSessionTabRecord *record)
{
	if (tab == NULL || record == NULL)
		return;
	sakura_tab_restore_state(tab, record->status, record->attention,
	                         record->attention_timestamp);
}


static gboolean
sakura_workspace_restore_should_defer(const gchar *page_id,
                                       const gchar *terminal_id)
{
	const gchar *selected_page_id = NULL;

	if (!sakura.session_restoring)
		return FALSE;
	if (sakura.session_snapshot != NULL) {
		selected_page_id = sakura.session_snapshot->selected_page_id;
		/* Older layout snapshots did not always persist selected_page_id.
		 * Their selected terminal is also the active terminal of its page, so
		 * recover the page identity before deciding which whole layout to defer. */
		if ((selected_page_id == NULL || selected_page_id[0] == '\0') &&
		    sakura.session_snapshot->selected_terminal_id != NULL &&
		    sakura.session_snapshot->pages != NULL) {
			for (guint index = 0;
			     index < sakura.session_snapshot->pages->len; index++) {
				SakuraSessionPageRecord *record = g_ptr_array_index(
					sakura.session_snapshot->pages, index);
				if (g_strcmp0(record->active_terminal_id,
				              sakura.session_snapshot->selected_terminal_id) == 0) {
					selected_page_id = record->id;
					break;
				}
			}
		}
	}
	if (selected_page_id != NULL && selected_page_id[0] != '\0' &&
	    page_id != NULL && page_id[0] != '\0')
		return g_strcmp0(page_id, selected_page_id) != 0;
	if (sakura.session_snapshot != NULL &&
	    sakura.session_snapshot->selected_terminal_id != NULL &&
	    sakura.session_snapshot->selected_terminal_id[0] != '\0' &&
	    terminal_id != NULL && terminal_id[0] != '\0')
		return g_strcmp0(terminal_id,
	                  sakura.session_snapshot->selected_terminal_id) != 0;
	return FALSE;
}


void
sakura_workspace_finish_restore(void)
{
	SakuraTab *tab;

	if (sakura_pending_restore_terminal_id == NULL)
		return;
	tab = sakura_find_pane_by_terminal_id(sakura_pending_restore_terminal_id);
	if (tab != NULL)
		sakura_select_tab(tab, FALSE);
	g_clear_pointer(&sakura_pending_restore_terminal_id, g_free);
}


static SakuraSessionLayoutRecord *
sakura_workspace_layout_leftmost(GHashTable *records,
                                 SakuraSessionLayoutRecord *record)
{
	while (record != NULL && g_strcmp0(record->type, "split") == 0)
		record = sakura_workspace_layout_record(records, record->first_id);
	return record;
}


static void
sakura_workspace_discard_pages(void)
{
	while (sakura.workspace->pages != NULL && sakura.workspace->pages->len > 0) {
		if (!sakura_tab_delete_page(0)) {
			g_warning("Could not discard a partially restored Sakura page");
			break;
		}
	}
}


static SakuraLayoutNode *
sakura_workspace_restore_layout_subtree(SakuraPage *page,
                                        GHashTable *layout_records,
                                        GHashTable *tab_records,
                                        SakuraSessionLayoutRecord *record,
                                        SakuraTab *anchor,
                                        SakuraLayoutNode *boundary)
{
	SakuraSessionLayoutRecord *first_record, *second_record, *second_leaf;
	SakuraSessionTabRecord *second_tab_record;
	SakuraTabLaunchConfig config = { 0 };
	SakuraTab *second_tab;
	SakuraLayoutNode *first, *split;
	SakuraSidebarNode *parent;

	if (page == NULL || record == NULL || anchor == NULL ||
	    anchor->layout_leaf == NULL)
		return NULL;
	if (g_strcmp0(record->type, "leaf") == 0)
		return anchor->layout_leaf;
	first_record = sakura_workspace_layout_record(layout_records, record->first_id);
	second_record = sakura_workspace_layout_record(layout_records, record->second_id);
	if (first_record == NULL || second_record == NULL)
		return NULL;
	first = sakura_workspace_restore_layout_subtree(page, layout_records,
	                                                tab_records, first_record,
	                                                anchor, boundary);
	if (first == NULL)
		return NULL;
	second_leaf = sakura_workspace_layout_leftmost(layout_records, second_record);
	second_tab_record = second_leaf != NULL
	                 ? sakura_workspace_tab_record(tab_records, second_leaf->terminal_id) : NULL;
	if (second_tab_record == NULL)
		return NULL;
	config.target_page = page;
	config.target_layout = first;
	config.target_ratio = record->ratio;
	config.split_direction = record->direction;
	config.defer_process_start = sakura_workspace_restore_should_defer(
		page->id, second_tab_record->terminal_id);
	config.order = second_tab_record->order;
	config.has_order = TRUE;
	config.codex_tracking_token = second_tab_record->codex_tracking_token;
	parent = sakura_sidebar_find_container_by_id(second_tab_record->parent_id);
		sakura_tab_add_with_options(second_tab_record->cwd, parent,
		                            second_tab_record->title,
		                            second_tab_record->title_set_by_user,
		                            second_tab_record->kind,
		                            sakura_tool_from_id(second_tab_record->tool_id),
		                            second_tab_record->codex_session_id,
		                            second_tab_record->codex_session_name,
		                            second_tab_record->codex_model,
		                            second_tab_record->codex_reasoning_effort,
		                            second_tab_record->tool_target,
		                            second_tab_record->terminal_id,
		                            second_tab_record->colorset, &config);
	second_tab = sakura_find_pane_by_terminal_id(second_tab_record->terminal_id);
	if (second_tab == NULL || second_tab->layout_leaf == NULL)
		return NULL;
	sakura_workspace_restore_tab_state(second_tab, second_tab_record);
	split = second_tab->layout_leaf->parent;
	if (split == NULL)
		return NULL;
	if (g_strcmp0(second_record->type, "split") == 0 &&
	    sakura_workspace_restore_layout_subtree(page, layout_records,
	                                             tab_records, second_record,
	                                             second_tab, split) == NULL)
		return NULL;
	(void)boundary;
	return split;
}


static gboolean
sakura_workspace_restore_layout_snapshot(SakuraSessionSnapshot *snapshot)
{
	GHashTable *layout_records, *tab_records, *page_records;
	gboolean failed = FALSE;
	guint index;

	layout_records = g_hash_table_new(g_str_hash, g_str_equal);
	tab_records = g_hash_table_new(g_str_hash, g_str_equal);
	page_records = g_hash_table_new(g_str_hash, g_str_equal);
	for (index = 0; index < snapshot->layouts->len; index++) {
		SakuraSessionLayoutRecord *record = g_ptr_array_index(snapshot->layouts, index);
		g_hash_table_insert(layout_records, record->id, record);
	}
	for (index = 0; index < snapshot->tabs->len; index++) {
		SakuraSessionTabRecord *record = g_ptr_array_index(snapshot->tabs, index);
		g_hash_table_insert(tab_records, record->terminal_id, record);
	}
	for (index = 0; index < snapshot->pages->len; index++) {
		SakuraSessionPageRecord *record = g_ptr_array_index(snapshot->pages, index);
		g_hash_table_insert(page_records, record->id, record);
	}

	/* Build each notebook page from its leftmost layout leaf, then recursively
	 * wrap existing subtrees with the remaining leaves. */
	for (index = 0; index < snapshot->pages->len; index++) {
		SakuraSessionPageRecord *page_record = g_ptr_array_index(snapshot->pages, index);
		SakuraSessionLayoutRecord *root = sakura_workspace_layout_record(
			layout_records, page_record->root_layout_id);
		SakuraSessionLayoutRecord *root_leaf = sakura_workspace_layout_leftmost(
			layout_records, root);
		SakuraSessionTabRecord *tab_record;
		SakuraTab *tab;
		SakuraSidebarNode *parent;
		SakuraTabLaunchConfig config = { 0 };

		if (root_leaf == NULL || root_leaf->terminal_id == NULL) {
			failed = TRUE;
			break;
		}
		tab_record = sakura_workspace_tab_record(tab_records, root_leaf->terminal_id);
		if (tab_record == NULL) {
			failed = TRUE;
			break;
		}
		parent = sakura_workspace_restore_page_parent(page_record, tab_record);
		config.defer_process_start = sakura_workspace_restore_should_defer(
			page_record->id, tab_record->terminal_id);
		config.order = tab_record->order;
		config.has_order = TRUE;
		config.codex_tracking_token = tab_record->codex_tracking_token;
		sakura_tab_add_with_options(tab_record->cwd, parent,
		                            tab_record->title,
		                            tab_record->title_set_by_user,
		                            tab_record->kind,
		                            sakura_tool_from_id(tab_record->tool_id),
		                            tab_record->codex_session_id,
		                            tab_record->codex_session_name,
		                            tab_record->codex_model,
		                            tab_record->codex_reasoning_effort,
		                            tab_record->tool_target,
		                            tab_record->terminal_id,
		                            tab_record->colorset, &config);
		tab = sakura_find_pane_by_terminal_id(tab_record->terminal_id);
		if (tab == NULL || tab->page == NULL) {
			failed = TRUE;
			break;
		}
		sakura_workspace_restore_tab_state(tab, tab_record);
		g_free(tab->page->id);
		tab->page->id = g_strdup(page_record->id);
		tab->page->title = g_strdup(page_record->title);
		tab->page->title_set_by_user = page_record->title_set_by_user;
		tab->page->archived = page_record->archived;
		if (tab->page->sidebar_node != NULL) {
			g_free(tab->page->sidebar_node->id);
			tab->page->sidebar_node->id = g_strdup(tab->page->id);
			sakura_sidebar_update_page(tab->page);
		}
		if (root != NULL && g_strcmp0(root->type, "split") == 0 &&
		    sakura_workspace_restore_layout_subtree(tab->page, layout_records,
		                                             tab_records, root, tab, NULL) == NULL) {
			failed = TRUE;
			break;
		}
	}
	if (failed) {
		sakura_workspace_discard_pages();
		g_hash_table_destroy(layout_records);
		g_hash_table_destroy(tab_records);
		g_hash_table_destroy(page_records);
		return FALSE;
	}
	/* Restoration may insert pages relative to the current GTK page. Rebuild
	 * the persistence caches from the notebook widgets before resolving the
	 * saved selection. */
	sakura_notebook_sync_page_order();
	sakura_sidebar_rebuild_projection();
	/* Splitting while restoring naturally makes the newest leaf active. Restore
	 * each page's own active pane after the complete tree exists; the global
	 * selection below may then choose a different page without losing these
	 * per-page focus anchors. */
	for (index = 0; index < snapshot->pages->len; index++) {
		SakuraSessionPageRecord *page_record =
			g_ptr_array_index(snapshot->pages, index);
		SakuraPage *page = NULL;
		SakuraTab *active = NULL;

		for (guint page_index = 0;
		     sakura.workspace->pages != NULL &&
		     page_index < sakura.workspace->pages->len; page_index++) {
			SakuraPage *candidate =
				g_ptr_array_index(sakura.workspace->pages, page_index);
			if (candidate != NULL &&
			    g_strcmp0(candidate->id, page_record->id) == 0) {
				page = candidate;
				break;
			}
		}
		if (page == NULL || page_record->active_terminal_id == NULL)
			continue;
		active = sakura_find_pane_by_terminal_id(
			page_record->active_terminal_id);
		if (active == NULL || active->page != page)
			continue;
		page->active_tab = active;
		g_free(page->last_active_terminal_id);
		page->last_active_terminal_id = g_strdup(active->terminal_id);
	}

	if (snapshot->selected_terminal_id != NULL) {
		SakuraTab *selected = sakura_find_pane_by_terminal_id(snapshot->selected_terminal_id);
		if (selected != NULL) {
			sakura_select_tab(selected, FALSE);
			g_free(sakura_pending_restore_terminal_id);
			sakura_pending_restore_terminal_id = g_strdup(selected->terminal_id);
		}
	}
	g_hash_table_destroy(layout_records);
	g_hash_table_destroy(tab_records);
	g_hash_table_destroy(page_records);
	return sakura.workspace->pages != NULL && sakura.workspace->pages->len > 0;
}


gboolean
sakura_workspace_restore_snapshot (SakuraSessionSnapshot *snapshot)
{
	gint selected_terminal, restored = 0;
	guint i;
	gboolean previous_show_archived;
	gboolean restored_layout;

	if (snapshot == NULL)
		return FALSE;
	if (snapshot->tabs->len == 0) {
		return FALSE;
	}
	previous_show_archived = sakura.show_archived;
	/* Materialize archived parents while restoring so page ownership is
	 * reconstructed from the saved hierarchy rather than falling back to root. */
	sakura.show_archived = TRUE;
	if (snapshot->pages != NULL && snapshot->pages->len > 0 &&
	    snapshot->layouts != NULL && snapshot->layouts->len > 0)
	{
		restored_layout = sakura_workspace_restore_layout_snapshot(snapshot);
		sakura.show_archived = previous_show_archived;
		sakura_sidebar_rebuild_projection();
		if (sakura.workspace->active_tab != NULL &&
		    sakura_tab_is_in_active_scope(sakura.workspace->active_tab))
			sakura_tab_bar_refresh();
		else
			sakura_select_scope_default();
		return restored_layout;
	}

	selected_terminal = snapshot->selected_terminal;
	for (i = 0; i < snapshot->tabs->len; i++) {
		SakuraSessionTabRecord *record = g_ptr_array_index(snapshot->tabs, i);
		struct sakura_sidebar_node *parent =
			sakura_sidebar_find_container_by_id(record->parent_id);
		SakuraTabKind tab_kind = record->kind;
		SakuraToolKind tool_kind = SAKURA_TOOL_NONE;
		gchar *cwd = g_strdup(record->cwd);
		gint restored_page;
		gboolean title_set = record->title_set_by_user &&
		                     record->title != NULL && record->title[0] != '\0';
		SakuraTabLaunchConfig config = { 0 };

		if (tab_kind == SAKURA_TAB_CODEX &&
		    (record->codex_session_id == NULL || record->codex_session_id[0] == '\0'))
			tab_kind = SAKURA_TAB_SHELL;
		else if (tab_kind == SAKURA_TAB_TOOL) {
			tool_kind = sakura_tool_from_id(record->tool_id);
			if (!sakura_tool_is_available(tool_kind)) {
				tab_kind = SAKURA_TAB_SHELL;
				tool_kind = SAKURA_TOOL_NONE;
			}
		}

		if (cwd != NULL && (cwd[0] == '\0' || !g_file_test(cwd, G_FILE_TEST_IS_DIR))) {
			g_free(cwd);
			cwd = NULL;
		}
		config.defer_process_start = sakura_workspace_restore_should_defer(
			NULL, record->terminal_id);
		config.order = record->order;
		config.has_order = TRUE;
		config.codex_tracking_token = record->codex_tracking_token;
		sakura_workspace_begin_mutation();
		sakura_tab_add_with_options(cwd, parent, title_set ? record->title : NULL,
		                            title_set, tab_kind, tool_kind,
		                            tab_kind == SAKURA_TAB_CODEX ? record->codex_session_id : NULL,
		                            tab_kind == SAKURA_TAB_CODEX ? record->codex_session_name : NULL,
		                            tab_kind == SAKURA_TAB_CODEX ? record->codex_model : NULL,
		                            tab_kind == SAKURA_TAB_CODEX ? record->codex_reasoning_effort : NULL,
		                            tab_kind == SAKURA_TAB_TOOL ? record->tool_target : NULL,
		                            sakura_terminal_id_is_valid(record->terminal_id)
		                            ? record->terminal_id : NULL,
		                            record->colorset, &config);
		sakura_workspace_end_mutation();
		SakuraTab *restored_tab = sakura_find_pane_by_terminal_id(record->terminal_id);
		restored_page = restored_tab != NULL ? sakura_page_for_tab(restored_tab) : -1;
		if (restored_page >= 0)
			sakura_workspace_restore_tab_state(restored_tab, record);
		if (selected_terminal == (gint)i)
			selected_terminal = restored;
		restored++;
		g_free(cwd);
	}
	sakura.show_archived = previous_show_archived;
	sakura_sidebar_rebuild_projection();

	if (restored > 0) {
		const gchar *selected_id = snapshot->selected_terminal_id;
		gint selected_page;
		struct sakura_tab *selected_tab = NULL;
		if ((selected_id == NULL || selected_id[0] == '\0') &&
		    snapshot->selected_terminal >= 0 &&
		    (guint)snapshot->selected_terminal < snapshot->tabs->len) {
			SakuraSessionTabRecord *selected_record = g_ptr_array_index(
				snapshot->tabs, snapshot->selected_terminal);
			selected_id = selected_record->terminal_id;
		}
		selected_tab = sakura_find_pane_by_terminal_id(selected_id);
		selected_page = selected_tab != NULL ? sakura_page_for_tab(selected_tab) : -1;
		if (selected_page < 0 && selected_terminal >= 0 && selected_terminal < restored)
			selected_page = selected_terminal;
		if (selected_tab == NULL && selected_page >= 0)
			selected_tab = sakura_tab_at_page(selected_page);
		if (selected_tab != NULL && sakura_tab_is_in_active_scope(selected_tab))
			sakura_select_tab(selected_tab, FALSE);
		else
			sakura_select_scope_default();
	}
	if (restored == 0)
		sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_STRUCTURE |
		                              SAKURA_WORKSPACE_CHANGE_SCOPE);
	return restored > 0;
}


typedef struct {
	SakuraSessionSnapshot *snapshot;
	SakuraWorkspaceRestoreCallback callback;
	gpointer callback_data;
	GHashTable *layout_records;
	GHashTable *tab_records;
	GArray *restore_order;
	gboolean restore_layout;
	gboolean failed;
	gboolean previous_show_archived;
	guint index;
	guint restored;
	guint source_id;
} SakuraWorkspaceRestoreJob;

static SakuraWorkspaceRestoreJob *sakura_workspace_restore_job;
static gboolean sakura_workspace_restore_job_step(gpointer data);

#define SAKURA_RESTORE_INPUT_GRACE_US (120 * 1000)


static void
sakura_workspace_restore_job_schedule(SakuraWorkspaceRestoreJob *job,
                                      guint delay_ms)
{
	if (job == NULL || job->source_id != 0)
		return;
	if (delay_ms > 0)
		job->source_id = g_timeout_add_full(
			G_PRIORITY_LOW, delay_ms, sakura_workspace_restore_job_step,
			job, NULL);
	else
		job->source_id = g_idle_add_full(
			G_PRIORITY_LOW, sakura_workspace_restore_job_step, job, NULL);
}


static void
sakura_workspace_restore_job_free(SakuraWorkspaceRestoreJob *job)
{
	if (job == NULL)
		return;
	if (job->source_id != 0) {
		g_source_remove(job->source_id);
		job->source_id = 0;
	}
	g_clear_pointer(&job->layout_records, g_hash_table_destroy);
	g_clear_pointer(&job->tab_records, g_hash_table_destroy);
	g_clear_pointer(&job->restore_order, g_array_unref);
	g_free(job);
}


static gboolean
sakura_workspace_restore_layout_page(SakuraWorkspaceRestoreJob *job,
                                      SakuraSessionPageRecord *page_record,
                                      guint saved_index)
{
	SakuraSessionLayoutRecord *root;
	SakuraSessionLayoutRecord *root_leaf;
	SakuraSessionTabRecord *tab_record;
	SakuraTab *tab;
	SakuraSidebarNode *parent;
	SakuraTabLaunchConfig config = { 0 };

	if (job == NULL || page_record == NULL)
		return FALSE;
	root = sakura_workspace_layout_record(job->layout_records,
	                                      page_record->root_layout_id);
	root_leaf = sakura_workspace_layout_leftmost(job->layout_records, root);
	if (root_leaf == NULL || root_leaf->terminal_id == NULL)
		return FALSE;
	tab_record = sakura_workspace_tab_record(job->tab_records,
	                                         root_leaf->terminal_id);
	if (tab_record == NULL)
		return FALSE;
	parent = sakura_workspace_restore_page_parent(page_record, tab_record);
	config.defer_process_start = sakura_workspace_restore_should_defer(
		page_record->id, tab_record->terminal_id);
	config.order = tab_record->order;
	config.has_order = TRUE;
	config.codex_tracking_token = tab_record->codex_tracking_token;
	sakura_tab_add_with_options(tab_record->cwd, parent,
	                            tab_record->title,
	                            tab_record->title_set_by_user,
	                            tab_record->kind,
	                            sakura_tool_from_id(tab_record->tool_id),
	                            tab_record->codex_session_id,
	                            tab_record->codex_session_name,
	                            tab_record->codex_model,
	                            tab_record->codex_reasoning_effort,
	                            tab_record->tool_target,
	                            tab_record->terminal_id,
	                            tab_record->colorset, &config);
	tab = sakura_find_pane_by_terminal_id(tab_record->terminal_id);
	if (tab == NULL || tab->page == NULL)
		return FALSE;
	/* The selected page is deliberately materialized first. Put every page at
	 * its persisted notebook position as it arrives so that prioritization does
	 * not become a visible or persisted reorder. */
	gtk_notebook_reorder_child(GTK_NOTEBOOK(sakura.notebook),
	                           tab->page->container, (gint)saved_index);
	sakura_workspace_restore_tab_state(tab, tab_record);
	g_free(tab->page->id);
	tab->page->id = g_strdup(page_record->id);
	g_free(tab->page->title);
	tab->page->title = g_strdup(page_record->title);
	tab->page->title_set_by_user = page_record->title_set_by_user;
	tab->page->archived = page_record->archived;
	if (tab->page->sidebar_node != NULL) {
		g_free(tab->page->sidebar_node->id);
		tab->page->sidebar_node->id = g_strdup(tab->page->id);
		sakura_sidebar_update_page(tab->page);
	}
	if (root != NULL && g_strcmp0(root->type, "split") == 0 &&
	    sakura_workspace_restore_layout_subtree(
			tab->page, job->layout_records, job->tab_records, root, tab, NULL) == NULL)
		return FALSE;
	return TRUE;
}


static gboolean
sakura_workspace_restore_tab_record(SakuraWorkspaceRestoreJob *job,
                                     SakuraSessionTabRecord *record,
                                     guint saved_index)
{
	SakuraSidebarNode *parent;
	SakuraTabKind tab_kind;
	SakuraToolKind tool_kind = SAKURA_TOOL_NONE;
	gchar *cwd;
	gboolean title_set;
	SakuraTab *restored_tab;
	SakuraTabLaunchConfig config = { 0 };

	if (job == NULL || record == NULL)
		return FALSE;
	parent = sakura_sidebar_find_container_by_id(record->parent_id);
	tab_kind = record->kind;
	cwd = g_strdup(record->cwd);
	title_set = record->title_set_by_user && record->title != NULL &&
	            record->title[0] != '\0';
	if (tab_kind == SAKURA_TAB_CODEX &&
	    (record->codex_session_id == NULL || record->codex_session_id[0] == '\0'))
		tab_kind = SAKURA_TAB_SHELL;
	else if (tab_kind == SAKURA_TAB_TOOL) {
		tool_kind = sakura_tool_from_id(record->tool_id);
		if (!sakura_tool_is_available(tool_kind)) {
			tab_kind = SAKURA_TAB_SHELL;
			tool_kind = SAKURA_TOOL_NONE;
		}
	}
	if (cwd != NULL && (cwd[0] == '\0' || !g_file_test(cwd, G_FILE_TEST_IS_DIR))) {
		g_free(cwd);
		cwd = NULL;
	}
	config.defer_process_start = sakura_workspace_restore_should_defer(
		NULL, record->terminal_id);
	config.order = record->order;
	config.has_order = TRUE;
	config.codex_tracking_token = record->codex_tracking_token;
	sakura_workspace_begin_mutation();
	sakura_tab_add_with_options(cwd, parent, title_set ? record->title : NULL,
	                            title_set, tab_kind, tool_kind,
	                            tab_kind == SAKURA_TAB_CODEX
	                            ? record->codex_session_id : NULL,
	                            tab_kind == SAKURA_TAB_CODEX
	                            ? record->codex_session_name : NULL,
	                            tab_kind == SAKURA_TAB_CODEX
	                            ? record->codex_model : NULL,
	                            tab_kind == SAKURA_TAB_CODEX
	                            ? record->codex_reasoning_effort : NULL,
	                            tab_kind == SAKURA_TAB_TOOL
	                            ? record->tool_target : NULL,
	                            sakura_terminal_id_is_valid(record->terminal_id)
	                            ? record->terminal_id : NULL,
	                            record->colorset, &config);
	sakura_workspace_end_mutation();
	restored_tab = sakura_find_pane_by_terminal_id(record->terminal_id);
	if (restored_tab != NULL) {
		gtk_notebook_reorder_child(GTK_NOTEBOOK(sakura.notebook),
		                           restored_tab->page->container,
		                           (gint)saved_index);
		sakura_workspace_restore_tab_state(restored_tab, record);
	}
	g_free(cwd);
	if (restored_tab == NULL)
		return FALSE;
	job->restored++;
	return TRUE;
}


static void
sakura_workspace_restore_job_finalize(SakuraWorkspaceRestoreJob *job,
                                      gboolean success)
{
	SakuraSessionSnapshot *snapshot;
	SakuraTab *selected_tab = NULL;
	const gchar *selected_terminal_id = NULL;

	if (job == NULL)
		return;
	snapshot = job->snapshot;
	if (!success || job->failed)
		sakura_workspace_discard_pages();
	else if (job->restore_layout) {
		sakura_notebook_sync_page_order();
		sakura_sidebar_rebuild_projection();
		for (guint index = 0; index < snapshot->pages->len; index++) {
			SakuraSessionPageRecord *page_record =
				g_ptr_array_index(snapshot->pages, index);
			SakuraPage *page = NULL;
			SakuraTab *active;

			for (guint page_index = 0;
			     sakura.workspace->pages != NULL &&
			     page_index < sakura.workspace->pages->len; page_index++) {
				SakuraPage *candidate = g_ptr_array_index(
					sakura.workspace->pages, page_index);
				if (candidate != NULL &&
				    g_strcmp0(candidate->id, page_record->id) == 0) {
					page = candidate;
					break;
				}
			}
			if (page == NULL || page_record->active_terminal_id == NULL)
				continue;
			active = sakura_find_pane_by_terminal_id(
				page_record->active_terminal_id);
			if (active == NULL || active->page != page)
				continue;
			page->active_tab = active;
			g_free(page->last_active_terminal_id);
			page->last_active_terminal_id = g_strdup(active->terminal_id);
		}
	} else {
		sakura_sidebar_rebuild_projection();
	}
	if (success && !job->failed) {
		selected_terminal_id = snapshot->selected_terminal_id;
		if ((selected_terminal_id == NULL || selected_terminal_id[0] == '\0') &&
		    snapshot->selected_terminal >= 0 &&
		    snapshot->tabs != NULL &&
		    (guint)snapshot->selected_terminal < snapshot->tabs->len) {
			SakuraSessionTabRecord *selected_record = g_ptr_array_index(
				snapshot->tabs, snapshot->selected_terminal);
			selected_terminal_id = selected_record != NULL
			                     ? selected_record->terminal_id : NULL;
		}
		selected_tab = sakura_find_pane_by_terminal_id(selected_terminal_id);
	}
	if (selected_tab != NULL) {
		sakura_select_tab(selected_tab, FALSE);
		g_free(sakura_pending_restore_terminal_id);
		sakura_pending_restore_terminal_id = g_strdup(selected_tab->terminal_id);
	} else if (success && !job->failed) {
		if (sakura.workspace->active_tab != NULL &&
		    sakura_tab_is_in_active_scope(sakura.workspace->active_tab))
			sakura_tab_bar_refresh();
		else
			sakura_select_scope_default();
	}
	if (job->restored == 0)
		sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_STRUCTURE |
		                              SAKURA_WORKSPACE_CHANGE_SCOPE);
	sakura.show_archived = job->previous_show_archived;
	sakura_sidebar_rebuild_projection();
	if (job->callback != NULL)
		job->callback(success && !job->failed && job->restored > 0,
		              job->callback_data);
	sakura_workspace_restore_job = NULL;
	job->source_id = 0;
	sakura_workspace_restore_job_free(job);
}


static gboolean
sakura_workspace_restore_job_step(gpointer data)
{
	SakuraWorkspaceRestoreJob *job = data;
	SakuraSessionSnapshot *snapshot;
	gboolean step_success;
	gint64 trace_started;
	gint64 now;
	guint restore_index;

	if (job == NULL)
		return G_SOURCE_REMOVE;
	job->source_id = 0;
	if (sakura.session_shutting_down)
		return G_SOURCE_REMOVE;
	now = g_get_monotonic_time();
	if (sakura.last_user_interaction_us != 0 &&
	    now - sakura.last_user_interaction_us < SAKURA_RESTORE_INPUT_GRACE_US) {
		gint64 remaining_us = SAKURA_RESTORE_INPUT_GRACE_US -
		                      (now - sakura.last_user_interaction_us);
		sakura_workspace_restore_job_schedule(
			job, (guint)MAX(1, (remaining_us + 999) / 1000));
		return G_SOURCE_REMOVE;
	}
	snapshot = job->snapshot;
	trace_started = sakura_ui_latency_trace_begin();
	if (job->restore_layout) {
		if (job->index >= job->restore_order->len) {
			sakura_workspace_restore_job_finalize(job, TRUE);
			return G_SOURCE_REMOVE;
		}
		restore_index = g_array_index(job->restore_order, guint, job->index);
		step_success = sakura_workspace_restore_layout_page(job,
			g_ptr_array_index(snapshot->pages, restore_index), restore_index);
	} else {
		if (job->index >= job->restore_order->len) {
			sakura_workspace_restore_job_finalize(job, TRUE);
			return G_SOURCE_REMOVE;
		}
		restore_index = g_array_index(job->restore_order, guint, job->index);
		step_success = sakura_workspace_restore_tab_record(job,
			g_ptr_array_index(snapshot->tabs, restore_index), restore_index);
	}
	sakura_ui_latency_trace_end(
		job->index == 0 ? "workspace-restore-selected" :
		                  "workspace-restore-background",
		trace_started);
	if (!step_success) {
		job->failed = TRUE;
		sakura_workspace_restore_job_finalize(job, FALSE);
		return G_SOURCE_REMOVE;
	}
	if (job->restore_layout)
		job->restored++;
	job->index++;
	sakura_workspace_restore_job_schedule(job, 0);
	return G_SOURCE_REMOVE;
}


gboolean
sakura_workspace_restore_snapshot_async(
	SakuraSessionSnapshot *snapshot, SakuraWorkspaceRestoreCallback callback,
	gpointer data)
{
	SakuraWorkspaceRestoreJob *job;

	if (sakura_workspace_restore_job != NULL || snapshot == NULL ||
	    snapshot->tabs == NULL || snapshot->tabs->len == 0)
		return FALSE;
	job = g_new0(SakuraWorkspaceRestoreJob, 1);
	job->snapshot = snapshot;
	job->callback = callback;
	job->callback_data = data;
	job->restore_layout = snapshot->pages != NULL && snapshot->pages->len > 0 &&
	                     snapshot->layouts != NULL && snapshot->layouts->len > 0;
	job->previous_show_archived = sakura.show_archived;
	sakura.show_archived = TRUE;
	job->layout_records = g_hash_table_new(g_str_hash, g_str_equal);
	job->tab_records = g_hash_table_new(g_str_hash, g_str_equal);
	job->restore_order = g_array_new(FALSE, FALSE, sizeof(guint));
	if (snapshot->layouts != NULL) {
		for (guint index = 0; index < snapshot->layouts->len; index++) {
			SakuraSessionLayoutRecord *record =
				g_ptr_array_index(snapshot->layouts, index);
			g_hash_table_insert(job->layout_records, record->id, record);
		}
	}
	for (guint index = 0; index < snapshot->tabs->len; index++) {
		SakuraSessionTabRecord *record =
			g_ptr_array_index(snapshot->tabs, index);
		g_hash_table_insert(job->tab_records, record->terminal_id, record);
	}
	{
		GPtrArray *records = job->restore_layout ? snapshot->pages : snapshot->tabs;
		gint selected_index = -1;

		for (guint index = 0; index < records->len; index++) {
			if (job->restore_layout) {
				SakuraSessionPageRecord *record = g_ptr_array_index(records, index);
				if (g_strcmp0(record->id, snapshot->selected_page_id) == 0 ||
				    ((snapshot->selected_page_id == NULL ||
				      snapshot->selected_page_id[0] == '\0') &&
				     g_strcmp0(record->active_terminal_id,
				               snapshot->selected_terminal_id) == 0)) {
					selected_index = (gint)index;
					break;
				}
			} else {
				SakuraSessionTabRecord *record = g_ptr_array_index(records, index);
				if (g_strcmp0(record->terminal_id,
				              snapshot->selected_terminal_id) == 0 ||
				    (snapshot->selected_terminal_id == NULL &&
				     snapshot->selected_terminal == (gint)index)) {
					selected_index = (gint)index;
					break;
				}
			}
		}
		if (selected_index >= 0) {
			guint value = (guint)selected_index;
			g_array_append_val(job->restore_order, value);
		}
		for (guint index = 0; index < records->len; index++) {
			guint value = index;
			if ((gint)index != selected_index)
				g_array_append_val(job->restore_order, value);
		}
	}
	sakura_workspace_restore_job = job;
	sakura_workspace_restore_job_schedule(job, 0);
	return TRUE;
}


void
sakura_workspace_restore_snapshot_async_cancel(void)
{
	SakuraWorkspaceRestoreJob *job = sakura_workspace_restore_job;

	if (job == NULL)
		return;
	sakura_workspace_restore_job = NULL;
	sakura.show_archived = job->previous_show_archived;
	sakura_workspace_restore_job_free(job);
}
void
sakura_sidebar_model_reordered_cb (GtkTreeModel *model, GtkTreePath *path,
                                   GtkTreeIter *iter, gint *new_order, void *data)
{
	GtkTreeIter parent_iter;
	GtkTreeIter *parent = iter;

	(void)new_order;
	(void)data;
	sakura_workspace_begin_mutation();
	if (model != NULL) {
		if (parent == NULL && path != NULL &&
		    gtk_tree_model_get_iter(model, &parent_iter, path))
			parent = &parent_iter;
		sakura_sidebar_update_model_order_for_parent(model, parent);
	}
	sakura_sidebar_save_groups();
	sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_STRUCTURE);
	sakura_session_mark_dirty();
	sakura_workspace_end_mutation();
}


void
sakura_sidebar_toggle_cb (GtkWidget *widget, void *data)
{
	sakura.sidebar_visible = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget));
	if (sakura.sidebar != NULL)
		gtk_widget_set_visible(sakura.sidebar, sakura.sidebar_visible);
	sakura_sidebar_update_spinner_timer();
	sakura_workspace_set_boolean("sidebar_visible", sakura.sidebar_visible);
	sakura_session_mark_dirty();
}


static void
sakura_sidebar_collapse_all_cb(GtkWidget *widget, void *data)
{
	(void)widget;
	(void)data;
	sakura_sidebar_collapse_all();
}


static void
sakura_sidebar_show_archived_cb(GtkWidget *widget, void *data)
{
	gboolean show_archived;

	(void)data;
	if (widget == NULL || !GTK_IS_CHECK_MENU_ITEM(widget))
		return;
	show_archived = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget));
	if (sakura.show_archived == show_archived)
		return;

	sakura_workspace_begin_mutation();
	sakura.show_archived = show_archived;
	if (!show_archived) {
		if (sakura_sidebar_group_is_archived(sakura.workspace->active_group)) {
			sakura.workspace->active_group = sakura.workspace->root_group;
			sakura.active_group_scope = sakura.sidebar_root;
		}
		if (sakura_sidebar_task_is_archived(sakura.workspace->active_task))
			sakura.workspace->active_task = NULL;
	}
	sakura_sidebar_rebuild_projection();
	sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_PROJECTION |
	                              SAKURA_WORKSPACE_CHANGE_SELECTION);
	sakura_workspace_set_boolean("show_archived", show_archived);
	sakura_session_mark_dirty();
	sakura_workspace_end_mutation();
	if (!show_archived)
		sakura_select_scope_default();
}


void
sakura_sidebar_paned_position_cb (GObject *object, GParamSpec *pspec, void *data)
{
	if (sakura.sidebar_paned != NULL && sakura.sidebar_visible) {
		sakura.sidebar_width = gtk_paned_get_position(GTK_PANED(sakura.sidebar_paned));
		sakura_workspace_set_integer("sidebar_width", sakura.sidebar_width);
	}
	sakura_session_mark_dirty();
}


static void
sakura_sidebar_row_expansion_changed_cb(GtkTreeView *tree,
                                         GtkTreeIter *iter,
                                         GtkTreePath *path,
                                         gpointer data)
{
	SakuraSidebarNode *node = NULL;
	SakuraSidebarExpansionKind kind;
	GtkTreePath *parent_path = NULL;
	gchar *key;
	gboolean parent_collapsed = FALSE;

	(void)data;
	if (tree != GTK_TREE_VIEW(sakura.sidebar_tree) || sakura.sidebar_syncing ||
	    sakura.session_restoring || sakura.session_shutting_down ||
	    sakura.sidebar_model == NULL || iter == NULL || path == NULL)
		return;
	gtk_tree_model_get(GTK_TREE_MODEL(sakura.sidebar_model), iter,
	                   SAKURA_SIDEBAR_COLUMN_NODE, &node, -1);
	if (!sakura_sidebar_node_expansion_kind(node, &kind) ||
	    node->id == NULL || node->id[0] == '\0')
		return;
	sakura_sidebar_ensure_expansion_state();
	key = sakura_sidebar_expansion_key(kind, node->id);
	if (key == NULL)
		return;
	if (gtk_tree_view_row_expanded(tree, path)) {
		g_hash_table_add(sakura.sidebar_expansion_keys, key);
		/* Reopening a group should restore any nested rows that were expanded
		 * before the group was collapsed. GTK collapses those descendants as a
		 * side effect, but their stable-ID choices remain in the table. */
		{
			gboolean was_syncing = sakura.sidebar_syncing;

			sakura.sidebar_syncing = TRUE;
			sakura_sidebar_restore_expansion_keys(sakura.sidebar_expansion_keys);
			sakura.sidebar_syncing = was_syncing;
		}
	} else {
		/* Collapsing a parent hides its descendants and GTK may emit a
		 * row-collapsed notification for those rows as well. Preserve their
		 * stable-ID expansion choices; only a row whose visible parent remains
		 * expanded represents an explicit user collapse of that row. */
		if (gtk_tree_path_get_depth(path) > 1) {
			parent_path = gtk_tree_path_copy(path);
			gtk_tree_path_up(parent_path);
			parent_collapsed = !gtk_tree_view_row_expanded(tree, parent_path);
			gtk_tree_path_free(parent_path);
		}
		if (!parent_collapsed)
			g_hash_table_remove(sakura.sidebar_expansion_keys, key);
		g_free(key);
	}
	sakura.sidebar_expansion_initialized = TRUE;
	sakura_session_mark_dirty();
}


void
sakura_sidebar_init (gboolean restore_session)
{
	GtkWidget *sidebar_box, *toolbar, *title, *tools_button, *open_here_button,
	          *collapse_all, *new_terminal, *new_group, *new_task;
	GtkWidget *tools_menu, *tool_item;
	GtkWidget *tab_shell, *scope_label, *tab_scrolled, *tab_bar, *tab_new;
	GtkWidget *empty_state, *empty_label, *empty_new;
	GtkWidget *scrolled;
	GtkCellRenderer *icon_renderer, *attention_renderer, *status_renderer,
	                *spinner_renderer, *text_renderer;
	GtkTreeViewColumn *column;
	gchar **group_ids = NULL, **group_parents = NULL, **group_titles = NULL,
	       **group_directories = NULL;
	gsize n_ids = 0, n_parents = 0, n_titles = 0, n_directories = 0;
	gsize i, n_groups;
	gboolean session_has_groups;

	sakura.sidebar_model = gtk_tree_store_new(SAKURA_SIDEBAR_N_COLUMNS,
	                                         G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
	                                         G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN,
	                                         G_TYPE_STRING,
	                                         G_TYPE_BOOLEAN,
	                                         G_TYPE_BOOLEAN,
	                                         G_TYPE_UINT,
	                                         G_TYPE_STRING,
	                                         G_TYPE_POINTER);
	sakura.sidebar_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(sakura.sidebar_model));
	sakura.sidebar_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(sakura.sidebar_tree));
	gtk_tree_selection_set_mode(sakura.sidebar_selection, GTK_SELECTION_SINGLE);
	g_signal_connect(sakura.sidebar_tree, "row-expanded",
	                 G_CALLBACK(sakura_sidebar_row_expansion_changed_cb), NULL);
	g_signal_connect(sakura.sidebar_tree, "row-collapsed",
	                 G_CALLBACK(sakura_sidebar_row_expansion_changed_cb), NULL);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(sakura.sidebar_tree), FALSE);
	gtk_tree_view_set_enable_tree_lines(GTK_TREE_VIEW(sakura.sidebar_tree), FALSE);
	gtk_tree_view_set_activate_on_single_click(GTK_TREE_VIEW(sakura.sidebar_tree), TRUE);
	gtk_tree_view_set_reorderable(GTK_TREE_VIEW(sakura.sidebar_tree), TRUE);
	g_signal_connect(sakura.sidebar_tree, "drag-begin",
	                 G_CALLBACK(sakura_sidebar_drag_begin_cb), NULL);
	g_signal_connect(sakura.sidebar_tree, "drag-end",
	                 G_CALLBACK(sakura_sidebar_drag_end_cb), NULL);
	g_signal_connect(sakura.sidebar_tree, "drag-motion",
	                 G_CALLBACK(sakura_sidebar_drag_motion_cb), NULL);
	g_signal_connect(sakura.sidebar_tree, "drag-drop",
	                 G_CALLBACK(sakura_sidebar_drag_drop_cb), NULL);
	gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(sakura.sidebar_tree), SAKURA_SIDEBAR_COLUMN_TOOLTIP);
	gtk_widget_set_name(sakura.sidebar_tree, "terminal-sidebar");

	icon_renderer = gtk_cell_renderer_pixbuf_new();
	attention_renderer = gtk_cell_renderer_text_new();
	/* Keep the attention stripe, but leave a little breathing room above and
	 * below it so a busy workspace does not look continuously highlighted. */
	g_object_set(attention_renderer, "text", " ", "xalign", 0.5, "yalign", 0.5,
	             "xpad", 0, "ypad", 0, NULL);
	gtk_cell_renderer_set_fixed_size(attention_renderer, 4, 14);
	status_renderer = gtk_cell_renderer_text_new();
	g_object_set(status_renderer, "xalign", 0.5, "yalign", 0.5,
	             "xpad", 0, "ypad", 0, NULL);
	gtk_cell_renderer_set_fixed_size(status_renderer, 16, 16);
	spinner_renderer = gtk_cell_renderer_spinner_new();
	g_object_set(spinner_renderer, "xalign", 0.5, "yalign", 0.5,
	             "xpad", 0, "ypad", 0, NULL);
	gtk_cell_renderer_set_fixed_size(spinner_renderer, 16, 16);
	text_renderer = gtk_cell_renderer_text_new();
	g_object_set(text_renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_spacing(column, 6);
	gtk_tree_view_column_pack_start(column, attention_renderer, FALSE);
	gtk_tree_view_column_add_attribute(column, attention_renderer, "background",
	                                  SAKURA_SIDEBAR_COLUMN_ATTENTION_COLOR);
	gtk_tree_view_column_add_attribute(column, attention_renderer, "background-set",
	                                  SAKURA_SIDEBAR_COLUMN_ATTENTION_VISIBLE);
	gtk_tree_view_column_add_attribute(column, attention_renderer, "visible",
	                                  SAKURA_SIDEBAR_COLUMN_ATTENTION_VISIBLE);
	gtk_tree_view_column_pack_start(column, icon_renderer, FALSE);
	gtk_tree_view_column_add_attribute(column, icon_renderer, "icon-name", SAKURA_SIDEBAR_COLUMN_ICON);
	gtk_tree_view_column_pack_start(column, status_renderer, FALSE);
	gtk_tree_view_column_add_attribute(column, status_renderer, "markup", SAKURA_SIDEBAR_COLUMN_STATUS_MARKUP);
	gtk_tree_view_column_add_attribute(column, status_renderer, "visible",
	                                  SAKURA_SIDEBAR_COLUMN_STATUS_MARKER_VISIBLE);
	gtk_tree_view_column_pack_start(column, spinner_renderer, FALSE);
	gtk_tree_view_column_add_attribute(column, spinner_renderer, "active",
	                                  SAKURA_SIDEBAR_COLUMN_STATUS_ACTIVE);
	gtk_tree_view_column_add_attribute(column, spinner_renderer, "visible",
	                                  SAKURA_SIDEBAR_COLUMN_STATUS_ACTIVE);
	gtk_tree_view_column_set_cell_data_func(
		column, spinner_renderer,
		(GtkTreeCellDataFunc)sakura_sidebar_spinner_cell_data_cb,
		NULL, NULL);
	sakura.sidebar_status_column = column;
	sakura.sidebar_spinner_renderer = spinner_renderer;
	gtk_tree_view_column_pack_start(column, text_renderer, TRUE);
	gtk_tree_view_column_add_attribute(column, text_renderer, "markup", SAKURA_SIDEBAR_COLUMN_MARKUP);
	gtk_tree_view_append_column(GTK_TREE_VIEW(sakura.sidebar_tree), column);

	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scrolled), sakura.sidebar_tree);

	toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	title = gtk_label_new(_("Workspace"));
	sakura.sidebar_title = title;
	gtk_label_set_xalign(GTK_LABEL(title), 0.0);
	gtk_widget_set_margin_start(title, 6);
	gtk_widget_set_margin_end(title, 6);
	gtk_box_pack_start(GTK_BOX(toolbar), title, TRUE, TRUE, 0);
	tools_button = gtk_menu_button_new();
	gtk_button_set_image(GTK_BUTTON(tools_button),
	                     gtk_image_new_from_icon_name("applications-system", GTK_ICON_SIZE_MENU));
	gtk_button_set_relief(GTK_BUTTON(tools_button), GTK_RELIEF_NONE);
	gtk_widget_set_tooltip_text(tools_button, _("Tools"));
	tools_menu = gtk_menu_new();
	tool_item = gtk_menu_item_new_with_label(_("GitUI"));
	g_signal_connect(tool_item, "activate", G_CALLBACK(sakura_new_tool_cb),
	                 GINT_TO_POINTER(SAKURA_TOOL_GITUI));
	gtk_menu_shell_append(GTK_MENU_SHELL(tools_menu), tool_item);
	tool_item = gtk_menu_item_new_with_label(_("Git Cola"));
	g_signal_connect(tool_item, "activate", G_CALLBACK(sakura_new_tool_cb),
	                 GINT_TO_POINTER(SAKURA_TOOL_GIT_COLA));
	gtk_menu_shell_append(GTK_MENU_SHELL(tools_menu), tool_item);
	tool_item = gtk_menu_item_new_with_label(_("GitHub Dashboard"));
	g_signal_connect(tool_item, "activate", G_CALLBACK(sakura_new_tool_cb),
	                 GINT_TO_POINTER(SAKURA_TOOL_GH_DASH));
	gtk_menu_shell_append(GTK_MENU_SHELL(tools_menu), tool_item);
	tool_item = gtk_menu_item_new_with_label(_("Open pull request..."));
	g_signal_connect(tool_item, "activate", G_CALLBACK(sakura_open_pr_cb), NULL);
	gtk_menu_shell_append(GTK_MENU_SHELL(tools_menu), tool_item);
	gtk_menu_button_set_popup(GTK_MENU_BUTTON(tools_button), tools_menu);
	gtk_box_pack_start(GTK_BOX(toolbar), tools_button, FALSE, FALSE, 0);
	open_here_button = gtk_menu_button_new();
	gtk_button_set_image(GTK_BUTTON(open_here_button),
	                     gtk_image_new_from_icon_name("folder-open", GTK_ICON_SIZE_MENU));
	gtk_button_set_relief(GTK_BUTTON(open_here_button), GTK_RELIEF_NONE);
	gtk_widget_set_tooltip_text(open_here_button, _("Open Here"));
	gtk_menu_button_set_popup(GTK_MENU_BUTTON(open_here_button), sakura_open_here_menu_new());
	gtk_box_pack_start(GTK_BOX(toolbar), open_here_button, FALSE, FALSE, 0);
	collapse_all = gtk_button_new_from_icon_name("go-up", GTK_ICON_SIZE_MENU);
	gtk_button_set_relief(GTK_BUTTON(collapse_all), GTK_RELIEF_NONE);
	gtk_widget_set_tooltip_text(collapse_all, _("Collapse all groups"));
	gtk_box_pack_start(GTK_BOX(toolbar), collapse_all, FALSE, FALSE, 0);
	new_terminal = gtk_button_new_from_icon_name("utilities-terminal", GTK_ICON_SIZE_MENU);
	gtk_button_set_relief(GTK_BUTTON(new_terminal), GTK_RELIEF_NONE);
	gtk_widget_set_tooltip_text(new_terminal, _("New terminal"));
	gtk_box_pack_start(GTK_BOX(toolbar), new_terminal, FALSE, FALSE, 0);
	new_group = gtk_button_new_from_icon_name("folder-new", GTK_ICON_SIZE_MENU);
	gtk_button_set_relief(GTK_BUTTON(new_group), GTK_RELIEF_NONE);
	gtk_widget_set_tooltip_text(new_group, _("New group"));
	gtk_box_pack_start(GTK_BOX(toolbar), new_group, FALSE, FALSE, 0);
	new_task = gtk_button_new_from_icon_name("list-add", GTK_ICON_SIZE_MENU);
	gtk_button_set_relief(GTK_BUTTON(new_task), GTK_RELIEF_NONE);
	gtk_widget_set_tooltip_text(new_task, _("New task"));
	gtk_box_pack_start(GTK_BOX(toolbar), new_task, FALSE, FALSE, 0);

	sidebar_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(sidebar_box), toolbar, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(sidebar_box), scrolled, TRUE, TRUE, 0);
	sakura.sidebar = sidebar_box;
	g_signal_connect(sidebar_box, "map", G_CALLBACK(sakura_sidebar_map_cb), NULL);
	g_signal_connect(sidebar_box, "unmap", G_CALLBACK(sakura_sidebar_unmap_cb), NULL);

	sakura.sidebar_root = g_new0(struct sakura_sidebar_node, 1);
	sakura_workspace_model_set_root(sakura.workspace,
	                                sakura_group_new("root", _("All terminals"), NULL));
	sakura.sidebar_root->type = SAKURA_SIDEBAR_GROUP;
	sakura.sidebar_root->id = g_strdup(sakura.workspace->root_group->id);
	sakura.sidebar_root->title = g_strdup(sakura.workspace->root_group->title);
	sakura.sidebar_root->group = sakura.workspace->root_group;
	sakura.workspace->root_group->sidebar_node = sakura.sidebar_root;
	sakura.workspace->next_group_id = 1;
	sakura.workspace->next_task_id = 1;
	sakura_sidebar_insert_node(sakura.sidebar_root);

	session_has_groups = restore_session &&
		sakura.session_snapshot != NULL;
	if (!session_has_groups &&
	    g_key_file_has_key(sakura.cfg, SAKURA_CONFIG_GROUP,
                        "show_archived", NULL))
		sakura.show_archived = g_key_file_get_boolean(
			sakura.cfg, SAKURA_CONFIG_GROUP, "show_archived", NULL);
	if (session_has_groups) {
		if (sakura.session_snapshot->root_directory != NULL)
			sakura.workspace->root_group->directory =
				g_strdup(sakura.session_snapshot->root_directory);
	} else {
		sakura.workspace->root_group->directory = g_key_file_get_string(
			sakura.cfg, SAKURA_CONFIG_GROUP, "sidebar_root_directory", NULL);
	}
	sakura_sidebar_update_group_row(sakura.sidebar_root);
	if (session_has_groups) {
		if (!sakura_workspace_model_restore_snapshot(
				sakura.workspace, sakura.session_snapshot))
			g_warning("Could not restore workspace hierarchy model");
	} else {
		group_ids = g_key_file_get_string_list(sakura.cfg, SAKURA_CONFIG_GROUP, "sidebar_group_ids", &n_ids, NULL);
		group_parents = g_key_file_get_string_list(sakura.cfg, SAKURA_CONFIG_GROUP, "sidebar_group_parents", &n_parents, NULL);
		group_titles = g_key_file_get_string_list(sakura.cfg, SAKURA_CONFIG_GROUP, "sidebar_group_titles", &n_titles, NULL);
		group_directories = g_key_file_get_string_list(sakura.cfg, SAKURA_CONFIG_GROUP,
		                                              "sidebar_group_directories",
		                                              &n_directories, NULL);
		n_groups = MIN(n_ids, MIN(n_parents, n_titles));
		if (group_directories == NULL)
			group_directories = g_new0(gchar *, n_groups + 1);
	}
	if (!session_has_groups) {
		for (i = 0; i < n_groups; i++) {
			struct sakura_sidebar_node *node, *parent;
			SakuraGroup *model_group, *parent_group;

			parent = sakura_sidebar_find_group_by_id(group_parents[i]);
			if (parent == NULL)
				parent = sakura.sidebar_root;
			parent_group = parent->group != NULL
			             ? parent->group : sakura.workspace->root_group;
			model_group = sakura_group_new(group_ids[i], group_titles[i],
			                              parent_group);
			model_group->order = i;
			if (i < n_directories)
				model_group->directory = g_strdup(group_directories[i]);
			sakura_workspace_model_add_group(sakura.workspace, model_group);
			node = g_new0(struct sakura_sidebar_node, 1);
			node->type = SAKURA_SIDEBAR_GROUP;
			node->id = g_strdup(model_group->id);
			node->title = g_strdup(model_group->title);
			node->group = model_group;
			model_group->sidebar_node = node;
			node->parent = parent;
			sakura_sidebar_insert_node(node);
		}
	}
	g_strfreev(group_ids);
	g_strfreev(group_parents);
	g_strfreev(group_titles);
	g_strfreev(group_directories);

	sakura_sidebar_rebuild_projection();

	/* Keep the notebook as the live terminal host, but expose a scoped tab strip
	 * above it. This lets group filtering change navigation without reparenting
	 * or restarting any terminal process. */
	tab_shell = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_widget_set_name(tab_shell, "sakura-tab-bar");
	scope_label = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(scope_label), 0.0);
	gtk_widget_set_margin_start(scope_label, 6);
	gtk_widget_set_margin_end(scope_label, 4);
	gtk_box_pack_start(GTK_BOX(tab_shell), scope_label, FALSE, FALSE, 0);
	tab_scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(tab_scrolled),
	                               GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(tab_scrolled), GTK_SHADOW_NONE);
	tab_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	gtk_container_add(GTK_CONTAINER(tab_scrolled), tab_bar);
	gtk_box_pack_start(GTK_BOX(tab_shell), tab_scrolled, TRUE, TRUE, 0);
	tab_new = gtk_button_new_from_icon_name("utilities-terminal", GTK_ICON_SIZE_MENU);
	gtk_button_set_relief(GTK_BUTTON(tab_new), GTK_RELIEF_NONE);
	gtk_widget_set_tooltip_text(tab_new, _("New terminal"));
	gtk_box_pack_start(GTK_BOX(tab_shell), tab_new, FALSE, FALSE, 0);

	empty_state = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_halign(empty_state, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(empty_state, GTK_ALIGN_CENTER);
	empty_label = gtk_label_new(_("No terminals in this group"));
	empty_new = gtk_button_new_with_label(_("New terminal"));
	gtk_box_pack_start(GTK_BOX(empty_state), empty_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(empty_state), empty_new, FALSE, FALSE, 0);

	sakura.content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	sakura.tab_bar_shell = tab_shell;
	sakura.tab_bar_scope_label = scope_label;
	sakura.tab_bar_scrolled = tab_scrolled;
	sakura.tab_bar = tab_bar;
	sakura.tab_bar_new_button = tab_new;
	sakura.tab_bar_empty = empty_state;
	sakura.workspace->active_group = sakura.workspace->root_group;
	sakura.active_group_scope = sakura.sidebar_root;
	gtk_box_pack_start(GTK_BOX(sakura.content_box), tab_shell, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(sakura.content_box), sakura.notebook, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(sakura.content_box), empty_state, TRUE, TRUE, 0);
	gtk_widget_hide(empty_state);
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(sakura.notebook), FALSE);

	if (restore_session && sakura.session_snapshot != NULL) {
		const gchar *active_group_id = sakura.session_snapshot->active_group_id;
		struct sakura_sidebar_node *saved_scope = sakura_sidebar_find_group_by_id(active_group_id);
		if (saved_scope != NULL) {
			sakura.workspace->active_group = saved_scope->group;
			sakura.active_group_scope = saved_scope;
		}
		if (sakura.session_snapshot->selected_task_id != NULL)
			sakura.workspace->active_task = sakura_workspace_model_find_task(sakura.workspace,
				sakura.session_snapshot->selected_task_id);
	}

	g_signal_connect(new_terminal, "clicked", G_CALLBACK(sakura_new_tab_cb), NULL);
	g_signal_connect(collapse_all, "clicked",
	                 G_CALLBACK(sakura_sidebar_collapse_all_cb), NULL);
	g_signal_connect(new_group, "clicked", G_CALLBACK(sakura_sidebar_new_group_cb), NULL);
	g_signal_connect(new_task, "clicked", G_CALLBACK(sakura_sidebar_new_task_cb), NULL);
	g_signal_connect(tab_new, "clicked", G_CALLBACK(sakura_new_tab_cb), NULL);
	g_signal_connect(empty_new, "clicked", G_CALLBACK(sakura_new_tab_cb), NULL);
	g_signal_connect(sakura.sidebar_selection, "changed",
	                 G_CALLBACK(sakura_sidebar_selection_changed_cb), NULL);
	g_signal_connect(sakura.sidebar_model, "rows-reordered",
	                 G_CALLBACK(sakura_sidebar_model_reordered_cb), NULL);
	g_signal_connect(sakura.sidebar_tree, "button-press-event",
	                 G_CALLBACK(sakura_sidebar_button_press_cb), NULL);
	gtk_widget_add_events(sakura.sidebar, GDK_BUTTON_PRESS_MASK);
	g_signal_connect(sakura.sidebar, "button-press-event",
	                 G_CALLBACK(sakura_sidebar_button_press_cb), NULL);

	sakura.sidebar_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_pack1(GTK_PANED(sakura.sidebar_paned), sakura.sidebar, FALSE, FALSE);
	gtk_paned_pack2(GTK_PANED(sakura.sidebar_paned), sakura.content_box, TRUE, FALSE);
	gtk_paned_set_position(GTK_PANED(sakura.sidebar_paned), sakura.sidebar_width);
	g_signal_connect(sakura.sidebar_paned, "notify::position",
	                 G_CALLBACK(sakura_sidebar_paned_position_cb), NULL);
	gtk_widget_show_all(sakura.sidebar_paned);
	if (!sakura.sidebar_visible)
		gtk_widget_hide(sakura.sidebar);
	sakura_tab_bar_refresh();
}


static void
sakura_sidebar_spinner_cell_data_cb(GtkTreeViewColumn *column,
	                                  GtkCellRenderer *renderer,
	                                  GtkTreeModel *model,
	                                  GtkTreeIter *iter,
	                                  gpointer data)
{
	(void)column;
	(void)model;
	(void)iter;
	(void)data;
	g_object_set(renderer, "pulse", sakura.sidebar_spinner_pulse, NULL);
}


static gboolean
sakura_sidebar_model_has_running(GtkTreeModel *model, GtkTreeIter *parent)
{
	GtkTreeIter iter;
	gboolean valid;

	valid = parent == NULL ? gtk_tree_model_get_iter_first(model, &iter)
	                       : gtk_tree_model_iter_children(model, &iter, parent);
	while (valid) {
		gboolean active = FALSE;
		gtk_tree_model_get(model, &iter,
		                   SAKURA_SIDEBAR_COLUMN_STATUS_ACTIVE, &active, -1);
		if (active || (gtk_tree_model_iter_has_child(model, &iter) &&
		               sakura_sidebar_model_has_running(model, &iter)))
			return TRUE;
		valid = gtk_tree_model_iter_next(model, &iter);
	}
	return FALSE;
}


struct sakura_sidebar_spinner_redraw {
	GtkTreeView *tree;
	GtkTreePath *start;
	GtkTreePath *end;
	GdkRectangle visible;
};


static gboolean
sakura_sidebar_redraw_running_row(GtkTreeModel *model, GtkTreePath *path,
	                                GtkTreeIter *iter, gpointer data)
{
	struct sakura_sidebar_spinner_redraw *redraw = data;
	gboolean active = FALSE;
	GdkRectangle area;
	gint renderer_x = 0, renderer_width = 0;
	gint widget_x, widget_y;

	if (gtk_tree_path_compare(path, redraw->start) < 0 ||
	    gtk_tree_path_compare(path, redraw->end) > 0)
		return FALSE;
	gtk_tree_model_get(model, iter,
	                   SAKURA_SIDEBAR_COLUMN_STATUS_ACTIVE, &active, -1);
	if (active) {
		gtk_tree_view_get_cell_area(redraw->tree, path, NULL, &area);
		if (area.y + area.height <= redraw->visible.y ||
		    area.y >= redraw->visible.y + redraw->visible.height)
			return FALSE;
		if (sakura.sidebar_status_column != NULL &&
		    sakura.sidebar_spinner_renderer != NULL &&
		    gtk_tree_view_column_cell_get_position(
			    sakura.sidebar_status_column,
			    sakura.sidebar_spinner_renderer,
			    &renderer_x, &renderer_width) && renderer_width > 0) {
			area.x += renderer_x;
			area.width = renderer_width;
		}
		gtk_tree_view_convert_tree_to_widget_coords(
			redraw->tree, area.x, area.y, &widget_x, &widget_y);
		gtk_widget_queue_draw_area(GTK_WIDGET(redraw->tree),
		                           widget_x, widget_y, area.width, area.height);
	}
	return FALSE;
}


gboolean
sakura_sidebar_spinner_pulse_cb(gpointer data)
{
	GtkTreeModel *model;
	GtkTreePath *start = NULL, *end = NULL;
	struct sakura_sidebar_spinner_redraw redraw;

	(void)data;
	if (sakura.session_shutting_down || sakura.sidebar_model == NULL ||
	    sakura.sidebar_tree == NULL || sakura.sidebar == NULL ||
	    !gtk_widget_get_mapped(sakura.sidebar)) {
		sakura.sidebar_spinner_source_id = 0;
		return G_SOURCE_REMOVE;
	}
	model = GTK_TREE_MODEL(sakura.sidebar_model);
	if (!sakura_sidebar_model_has_running(model, NULL)) {
		sakura.sidebar_spinner_source_id = 0;
		return G_SOURCE_REMOVE;
	}
	sakura.sidebar_spinner_pulse++;
	if (gtk_tree_view_get_visible_range(GTK_TREE_VIEW(sakura.sidebar_tree),
	                                    &start, &end)) {
		redraw.tree = GTK_TREE_VIEW(sakura.sidebar_tree);
		redraw.start = start;
		redraw.end = end;
		gtk_tree_view_get_visible_rect(redraw.tree, &redraw.visible);
		gtk_tree_model_foreach(model, sakura_sidebar_redraw_running_row, &redraw);
		gtk_tree_path_free(start);
		gtk_tree_path_free(end);
	}
	return G_SOURCE_CONTINUE;
}


void
sakura_sidebar_spinner_stop(void)
{
	if (sakura.sidebar_spinner_source_id != 0) {
		g_source_remove(sakura.sidebar_spinner_source_id);
		sakura.sidebar_spinner_source_id = 0;
	}
}


static void
sakura_sidebar_update_spinner_timer(void)
{
	if (sakura.sidebar_spinner_source_id == 0 && sakura.sidebar_model != NULL &&
	    sakura.sidebar != NULL && gtk_widget_get_mapped(sakura.sidebar) &&
	    sakura_sidebar_model_has_running(GTK_TREE_MODEL(sakura.sidebar_model), NULL))
		sakura.sidebar_spinner_source_id = g_timeout_add(
			160, sakura_sidebar_spinner_pulse_cb, NULL);
}


static void
sakura_sidebar_map_cb(GtkWidget *widget, gpointer data)
{
	(void)widget;
	(void)data;
	sakura_sidebar_update_spinner_timer();
}


static void
sakura_sidebar_unmap_cb(GtkWidget *widget, gpointer data)
{
	(void)widget;
	(void)data;
	sakura_sidebar_spinner_stop();
}


gboolean
sakura_notebook_scroll_cb (GtkWidget *widget, GdkEventScroll *event)
{
	/* Scrolling the notebook while the pointer is over a VTE interferes with
	 * terminal input methods. Keep the callback as an intentional no-op. */
	(void)widget;
	(void)event;
	return FALSE;
}


void
sakura_switch_page_cb (GtkWidget *widget, GtkWidget *widget_page,
                       guint page_num, void *data)
{
	SakuraPage *page;
	SakuraTab *tab;
	SakuraSidebarSelectionReason selection_reason;

	(void)widget;
	(void)widget_page;
	(void)data;
	if (sakura.session_shutting_down)
		return;
	tab = sakura_workspace_notebook_tab_for_widget(widget_page, page_num);
	/* Page removal is a transaction. GTK may emit switch-page while the old
	 * page is being detached. Preserve the event until the outer transaction
	 * can reconcile it with the authoritative workspace selection. */
	if (sakura_workspace_is_mutating()) {
		sakura_workspace_capture_notebook_selection(tab);
		return;
	}
	/* Don't use gtk_notebook_get_current_page here; GTK still reports the
	 * previous page while this callback is dispatched. */
	if (tab == NULL)
		return;
	page = tab->page;
	if (page != NULL && page->active_tab != NULL)
		tab = page->active_tab;
	sakura.workspace_selection_cleared = FALSE;
	sakura.workspace->active_tab = tab;
	sakura.workspace->active_page = page;
	sakura_workspace_start_page_runtime(page);
	sakura_remember_current_scope_tab(tab);
	if (!sakura.session_restoring)
		sakura_tab_clear_attention(tab);
	/* A notebook switch can be triggered while a sidebar click is still being
	 * dispatched. Queue the tree update so the original click's target wins
	 * over any intermediate scope/fallback switch. */
	selection_reason = sakura.session_restoring
	                 ? SAKURA_SIDEBAR_SELECTION_RESTORE
	                 : SAKURA_SIDEBAR_SELECTION_SYNC;
	if (tab->page != NULL && tab->page->panes != NULL && tab->page->panes->len <= 1)
		sakura_sidebar_queue_select_node_with_reason(
			tab->page->sidebar_node, selection_reason);
	else
		sakura_sidebar_queue_select_node_with_reason(
			tab->sidebar_node, selection_reason);
	sakura_sidebar_update_page(tab->page);
	sakura_codex_sync_name(tab);
	sakura_update_geometry_hints();

	/* Update the window title when a new tab is selected, but don't when a user
	 * supplied a static title. */
	if (!sakura.main_title && tab->label != NULL) {
		const gchar *title = gtk_label_get_text(GTK_LABEL(tab->label));
		if (title != NULL && title[0] != '\0')
			sakura_set_window_title(title);
	}
	sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_SELECTION);
	sakura_session_mark_dirty();
}


void
sakura_page_removed_cb (GtkWidget *widget, void *data)
{
	(void)widget;
	(void)data;
	if (sakura.session_shutting_down)
		return;
	if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook)) == 1)
		/* If the first tab is disabled, recalculate the terminal surface. */
		sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_STRUCTURE);
}


gboolean
sakura_cwd_tracking_poll_cb (gpointer data)
{
	gint page, pages;

	if (sakura.session_shutting_down) {
		sakura.cwd_tracking_source_id = 0;
		return G_SOURCE_REMOVE;
	}
	if (sakura.notebook == NULL)
		return G_SOURCE_CONTINUE;

	pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));
	for (page = 0; page < pages; page++) {
		struct sakura_tab *sk_tab = sakura_tab_at_page(page);

		if (sakura_update_tab_cwd(sk_tab))
			sakura_sidebar_update_tab(sk_tab);
	}

	return G_SOURCE_CONTINUE;
}



static SakuraTabStatus
sakura_sidebar_page_status(SakuraPage *page)
{
	SakuraTabStatus status = SAKURA_TAB_STATUS_NONE;
	guint index;

	if (page == NULL || page->panes == NULL)
		return status;
	for (index = 0; index < page->panes->len; index++) {
		SakuraTab *tab = g_ptr_array_index(page->panes, index);
		SakuraTabStatus candidate;

		if (tab == NULL)
			continue;
		candidate = tab->status;
		/* Match the workspace priority: attention/error first, then ready,
		 * working, idle and finally no status. */
		if (candidate == SAKURA_TAB_STATUS_ERROR)
			return candidate;
		if (candidate == SAKURA_TAB_STATUS_NEEDS_APPROVAL &&
		    status != SAKURA_TAB_STATUS_ERROR)
			status = candidate;
		else if (candidate == SAKURA_TAB_STATUS_INTERRUPTED &&
		         status != SAKURA_TAB_STATUS_ERROR &&
		         status != SAKURA_TAB_STATUS_NEEDS_APPROVAL)
			status = candidate;
		else if (candidate == SAKURA_TAB_STATUS_READY &&
		         status != SAKURA_TAB_STATUS_ERROR &&
		         status != SAKURA_TAB_STATUS_NEEDS_APPROVAL &&
		         status != SAKURA_TAB_STATUS_INTERRUPTED)
			status = candidate;
		else if (candidate == SAKURA_TAB_STATUS_RUNNING &&
		         (status == SAKURA_TAB_STATUS_NONE ||
		          status == SAKURA_TAB_STATUS_IDLE))
			status = candidate;
		else if (candidate == SAKURA_TAB_STATUS_IDLE &&
		         status == SAKURA_TAB_STATUS_NONE)
			status = candidate;
	}
	return status;
}

void
sakura_sidebar_set_node_row(SakuraSidebarNode *node, GtkTreeIter *iter)
{
	const gchar *icon_name;
	const gchar *status_label, *status_color, *status_symbol, *attention_color = NULL;
	const gchar *reasoning_effort = NULL;
	gchar *escaped_title, *escaped_subtitle, *title_markup, *markup;
	gchar *status_markup = NULL;
	gchar *tooltip_markup = NULL;
	gboolean status_running, status_marker_visible, attention_visible = FALSE;
	gboolean archived;
	SakuraTabStatus status = node != NULL && node->tab != NULL
	                       ? node->tab->status : SAKURA_TAB_STATUS_NONE;
	SakuraTaskStatus task_status = node != NULL && node->task != NULL
	                            ? node->task->status : SAKURA_TASK_READY;
	gboolean task_row = node != NULL && node->type == SAKURA_SIDEBAR_TASK;
	SakuraPage *page = node != NULL ? node->page : NULL;
	SakuraTab *status_tab = node != NULL ? node->tab : NULL;
	guint index;

	icon_name = "utilities-terminal";
	if (node->type == SAKURA_SIDEBAR_GROUP) {
		icon_name = "folder";
	} else if (task_row) {
		icon_name = "checkbox";
	} else if (node->type == SAKURA_SIDEBAR_PAGE) {
		icon_name = "tab-new";
		status = sakura_sidebar_page_status(page);
		/* A single-pane page is represented by its page row, so preserve the
		 * pane's semantic icon there. Split pages keep the page icon and expose
		 * each pane as a child row below it. */
		if (page != NULL && page->panes != NULL && page->panes->len <= 1)
			icon_name = sakura_sidebar_tab_icon_name(
				sakura_sidebar_page_active_tab(page));
	} else {
		icon_name = sakura_sidebar_tab_icon_name(node->tab);
	}
	if (status_tab == NULL && node->type == SAKURA_SIDEBAR_PAGE && page != NULL &&
	    page->panes != NULL && page->panes->len == 1)
		status_tab = g_ptr_array_index(page->panes, 0);

	escaped_title = g_markup_escape_text(node->title != NULL ? node->title : "", -1);
	escaped_subtitle = g_markup_escape_text(node->subtitle != NULL ? node->subtitle : "", -1);
	if (!task_row && status_tab != NULL &&
	    (status_tab->runtime_deferred || status_tab->runtime_start_pending ||
	     status_tab->codex_resume_cwd_query_active))
		status_label = (status_tab->runtime_start_pending ||
		                status_tab->codex_resume_cwd_query_active)
	                   ? (status_tab->kind == SAKURA_TAB_CODEX
	                      ? _("Loading Codex session…")
	                      : _("Starting terminal…"))
	                   : _("Click to resume");
	else
		status_label = task_row ? sakura_task_status_label(task_status) :
		               (status != SAKURA_TAB_STATUS_NONE ? sakura_tab_status_label(status) : NULL);
	if (node->tab != NULL && node->tab->kind == SAKURA_TAB_CODEX &&
	    sakura_codex_reasoning_effort_is_valid(node->tab->codex_reasoning_effort))
		reasoning_effort = sakura_codex_reasoning_effort_label(
			node->tab->codex_reasoning_effort);
	status_running = task_row ? task_status == SAKURA_TASK_WORKING
	                            : (((status_tab == NULL || !status_tab->runtime_deferred) &&
                                status == SAKURA_TAB_STATUS_RUNNING) ||
	                              (status_tab != NULL &&
	                              (status_tab->agent_start_pending ||
	                               status_tab->codex_start_pending ||
	                               status_tab->codex_resume_cwd_query_active ||
	                               status_tab->runtime_start_pending)));
	status_color = task_row ? sakura_task_status_color(task_status) :
	              (status != SAKURA_TAB_STATUS_NONE ? sakura_tab_status_color(status) : NULL);
	status_symbol = task_row ? sakura_task_status_symbol(task_status) :
	               (status != SAKURA_TAB_STATUS_NONE ? sakura_tab_status_symbol(status) : NULL);
	if (!task_row && status_tab != NULL && status_tab->runtime_deferred &&
	    !status_tab->runtime_start_pending) {
		status_color = "#8c8c8c";
		status_symbol = "▶";
	}
	if (!status_running && status_color != NULL && status_symbol != NULL)
		status_markup = g_strdup_printf("<span foreground=\"%s\">%s</span>",
		                                status_color, status_symbol);
	status_marker_visible = status_markup != NULL;
	if (node->type == SAKURA_SIDEBAR_PAGE && page != NULL && page->panes != NULL) {
		for (index = 0; index < page->panes->len; index++) {
			SakuraTab *tab = g_ptr_array_index(page->panes, index);
			if (tab != NULL && tab->attention) {
				attention_visible = TRUE;
				break;
			}
		}
	} else if (node->tab != NULL && node->tab->attention) {
		attention_visible = TRUE;
	}
	if (task_row && (task_status == SAKURA_TASK_BLOCKED ||
	                 task_status == SAKURA_TASK_REVIEW))
		attention_visible = TRUE;
	if (attention_visible) {
		attention_color = status_color != NULL ? status_color : "#5b9bd5";
	}
	if (attention_visible)
		title_markup = g_strdup_printf("<b>%s</b>", escaped_title);
	else
		title_markup = g_strdup(escaped_title);
	archived = sakura_sidebar_node_is_archived(node);
	if (archived) {
		gchar *muted_title = g_strdup_printf(
			"<span foreground=\"#888888\">%s</span>", title_markup);
		g_free(title_markup);
		title_markup = muted_title;
	}
	if (node->subtitle != NULL && node->subtitle[0] != '\0' &&
	    node->subtitle_is_directory)
		markup = g_strdup_printf(
			"%s <span foreground=\"#888888\"><small>· %s</small></span>",
			title_markup, escaped_subtitle);
	else if (node->subtitle != NULL && node->subtitle[0] != '\0')
		markup = g_strdup_printf("%s\n<small>%s</small>", title_markup, escaped_subtitle);
	else
		markup = g_strdup(title_markup);
	{
		const gchar *base_tooltip = node->tooltip != NULL ? node->tooltip : node->title;
		base_tooltip = base_tooltip != NULL ? base_tooltip : "";
		if (status_label != NULL && reasoning_effort != NULL)
			tooltip_markup = g_markup_printf_escaped("%s\n%s\n%s: %s",
			                                        base_tooltip, status_label,
			                                        _("Reasoning"), reasoning_effort);
		else if (status_label != NULL)
			tooltip_markup = g_markup_printf_escaped("%s\n%s",
			                                        base_tooltip, status_label);
		else if (reasoning_effort != NULL)
			tooltip_markup = g_markup_printf_escaped("%s\n%s: %s",
			                                        base_tooltip, _("Reasoning"),
			                                        reasoning_effort);
		else
			tooltip_markup = g_markup_escape_text(base_tooltip, -1);
	}

	gtk_tree_store_set(sakura.sidebar_model, iter,
	                   SAKURA_SIDEBAR_COLUMN_TITLE, node->title,
	                   SAKURA_SIDEBAR_COLUMN_SUBTITLE, node->subtitle,
	                   SAKURA_SIDEBAR_COLUMN_MARKUP, markup,
	                   SAKURA_SIDEBAR_COLUMN_ICON, icon_name,
	                   SAKURA_SIDEBAR_COLUMN_ATTENTION_COLOR, attention_color,
	                   SAKURA_SIDEBAR_COLUMN_ATTENTION_VISIBLE, attention_visible,
	                   SAKURA_SIDEBAR_COLUMN_STATUS_MARKUP, status_markup,
	                   SAKURA_SIDEBAR_COLUMN_STATUS_MARKER_VISIBLE,
	                   status_marker_visible,
	                   SAKURA_SIDEBAR_COLUMN_STATUS_ACTIVE, status_running,
	                   SAKURA_SIDEBAR_COLUMN_STATUS_PULSE, 0,
	                   SAKURA_SIDEBAR_COLUMN_TOOLTIP, tooltip_markup,
	                   SAKURA_SIDEBAR_COLUMN_NODE, node,
	                   -1);
	sakura_sidebar_update_spinner_timer();

	g_free(escaped_title);
	g_free(escaped_subtitle);
	g_free(title_markup);
	g_free(markup);
	g_free(status_markup);
	g_free(tooltip_markup);
}


void
sakura_sidebar_update_page(SakuraPage *page)
{
	SakuraSidebarNode *node;
	SakuraTab *active;
	GtkTreeIter iter;
	gchar *base_title, *title, *subtitle, *tooltip;
	guint pane_count;

	if (page == NULL || page->sidebar_node == NULL)
		return;
	node = page->sidebar_node;
	active = sakura_sidebar_page_active_tab(page);
	pane_count = page->panes != NULL ? page->panes->len : 0;

	if (page->title_set_by_user && page->title != NULL && page->title[0] != '\0')
		base_title = g_strdup(page->title);
	else if (active != NULL && active->sidebar_node != NULL &&
	         active->sidebar_node->title != NULL)
		base_title = g_strdup(active->sidebar_node->title);
	else if (page->title != NULL && page->title[0] != '\0')
		base_title = g_strdup(page->title);
	else
		base_title = g_strdup(_("Terminal"));
	g_strstrip(base_title);

	if (pane_count > 1)
		title = g_strdup_printf(_("%s (%u panes)"), base_title, pane_count);
	else
		title = g_strdup(base_title);
	subtitle = sakura_sidebar_page_directory_summary(page, active);
	if (pane_count > 1)
		tooltip = subtitle[0] != '\0'
		        ? g_strdup_printf("%s\n%s", title, subtitle) : g_strdup(title);
	else
		tooltip = g_strdup(active != NULL && active->sidebar_node != NULL &&
		                  active->sidebar_node->tooltip != NULL
		                ? active->sidebar_node->tooltip : title);

	g_free(node->title);
	g_free(node->subtitle);
	g_free(node->tooltip);
	node->title = title;
	node->subtitle = subtitle;
	node->subtitle_is_directory = pane_count > 1
	                           ? subtitle[0] != '\0'
	                           : active != NULL && active->sidebar_node != NULL &&
	                             active->sidebar_node->subtitle_is_directory;
	node->tooltip = tooltip;
	if (sakura_sidebar_get_iter(node, &iter))
		sakura_sidebar_set_node_row(node, &iter);
	g_free(base_title);
}


gboolean
sakura_sidebar_get_iter(SakuraSidebarNode *node, GtkTreeIter *iter)
{
	GtkTreePath *path;
	gboolean valid;

	if (node == NULL || node->row == NULL || sakura.sidebar_model == NULL)
		return FALSE;

	path = gtk_tree_row_reference_get_path(node->row);
	if (path == NULL)
		return FALSE;

	valid = gtk_tree_model_get_iter(GTK_TREE_MODEL(sakura.sidebar_model), iter, path);
	gtk_tree_path_free(path);
	return valid;
}


void
sakura_sidebar_free_node(SakuraSidebarNode *node)
{
	if (node == NULL)
		return;

	if (node->type == SAKURA_SIDEBAR_GROUP && node->group != NULL &&
	    node->group->sidebar_node == node)
		node->group->sidebar_node = NULL;
	if (node->type == SAKURA_SIDEBAR_TASK && node->task != NULL &&
	    node->task->sidebar_node == node)
		node->task->sidebar_node = NULL;
	if (node->type == SAKURA_SIDEBAR_PAGE && node->page != NULL &&
	    node->page->sidebar_node == node)
		node->page->sidebar_node = NULL;
	if (node->type == SAKURA_SIDEBAR_TERMINAL && node->tab != NULL &&
	    node->tab->sidebar_node == node)
		node->tab->sidebar_node = NULL;
	if (node->row != NULL)
		gtk_tree_row_reference_free(node->row);
	g_free(node->id);
	g_free(node->title);
	g_free(node->subtitle);
	g_free(node->tooltip);
	g_free(node);
}


void
sakura_sidebar_update_attention_count(void)
{
	guint count = 0;
	guint index;
	gchar *label;

	if (sakura.sidebar_title == NULL || sakura.workspace->panes == NULL)
		return;

	for (index = 0; index < sakura.workspace->panes->len; index++) {
		SakuraTab *tab = g_ptr_array_index(sakura.workspace->panes, index);
		if (tab != NULL && tab->attention)
			count++;
	}

	if (count == 0) {
		gtk_label_set_text(GTK_LABEL(sakura.sidebar_title), _("Workspace"));
		return;
	}

	label = g_strdup_printf(_("Workspace (%u need attention)"), count);
	gtk_label_set_text(GTK_LABEL(sakura.sidebar_title), label);
	g_free(label);
}


static void
sakura_sidebar_insert_node_relative(SakuraSidebarNode *node,
                                     SakuraSidebarNode *sibling)
{
	GtkTreeIter iter, parent_iter;
	GtkTreeIter sibling_iter;
	GtkTreeIter *parent = NULL;
	GtkTreePath *path;
	gboolean inserted_after = FALSE;

	if (node->parent != NULL && sakura_sidebar_get_iter(node->parent, &parent_iter))
		parent = &parent_iter;
	if (sibling != NULL && sibling->parent == node->parent &&
	    sakura_sidebar_get_iter(sibling, &sibling_iter)) {
		gtk_tree_store_insert_after(sakura.sidebar_model, &iter, parent,
		                            &sibling_iter);
		inserted_after = TRUE;
	}
	if (!inserted_after)
		gtk_tree_store_append(sakura.sidebar_model, &iter, parent);

	if (node->type == SAKURA_SIDEBAR_GROUP)
		sakura_sidebar_update_group_row(node);
	sakura_sidebar_set_node_row(node, &iter);

	path = gtk_tree_model_get_path(GTK_TREE_MODEL(sakura.sidebar_model), &iter);
	node->row = gtk_tree_row_reference_new(GTK_TREE_MODEL(sakura.sidebar_model), path);
	gtk_tree_path_free(path);

	/* The root row is the stable entry point for the sidebar. Keep it
	 * reachable while the initial projection is assembled, but do not expand
	 * arbitrary group parents as a side effect of inserting their children. */
	if (!sakura.session_restoring && node->parent == sakura.sidebar_root) {
		if (sakura_sidebar_get_iter(sakura.sidebar_root, &parent_iter)) {
			path = gtk_tree_model_get_path(
				GTK_TREE_MODEL(sakura.sidebar_model), &parent_iter);
			gtk_tree_view_expand_row(GTK_TREE_VIEW(sakura.sidebar_tree), path,
			                         FALSE);
			gtk_tree_path_free(path);
		}
	}
}


void
sakura_sidebar_insert_node(SakuraSidebarNode *node)
{
	sakura_sidebar_insert_node_relative(node, NULL);
}


void
sakura_sidebar_insert_node_after(SakuraSidebarNode *node,
                                 SakuraSidebarNode *sibling)
{
	sakura_sidebar_insert_node_relative(node, sibling);
}


/* Page rows are a projection of the notebook page order. Reparenting a page
 * must preserve that order within its new sidebar parent; appending here would
 * make the GTK projection disagree with the persisted model order and cause a
 * later coordinate-based selection to land on a different page. An explicit
 * "new tab after current" request remains authoritative for newly-created
 * pages. */
static void
sakura_sidebar_insert_page_node(SakuraSidebarNode *node)
{
	SakuraSidebarNode *previous = NULL;

	if (node == NULL || node->type != SAKURA_SIDEBAR_PAGE)
		return;
	if (sakura.sidebar_pending_insert_after != NULL &&
	    sakura.sidebar_pending_insert_after->parent == node->parent) {
		sakura_sidebar_insert_node_after(node,
		                                 sakura.sidebar_pending_insert_after);
		sakura.sidebar_pending_insert_after = NULL;
		return;
	}
	for (guint index = 0; sakura.workspace != NULL &&
	                     sakura.workspace->pages != NULL &&
	                     index < sakura.workspace->pages->len; index++) {
		SakuraPage *page = g_ptr_array_index(sakura.workspace->pages, index);

		if (page == node->page)
			break;
		if (page != NULL && page->sidebar_node != NULL &&
		    page->sidebar_node->parent == node->parent)
			previous = page->sidebar_node;
	}
	sakura_sidebar_insert_node_after(node, previous);
	sakura.sidebar_pending_insert_after = NULL;
}


static void
sakura_sidebar_reveal_node(SakuraSidebarNode *node, gboolean expand_node)
{
	GPtrArray *ancestors;
	SakuraSidebarNode *current;

	if (node == NULL || sakura.sidebar_tree == NULL ||
	    sakura.sidebar_model == NULL)
		return;
	ancestors = g_ptr_array_new();
	for (current = node->parent; current != NULL; current = current->parent)
		g_ptr_array_add(ancestors, current);
	for (gint index = (gint)ancestors->len - 1; index >= 0; index--) {
		SakuraSidebarNode *ancestor = g_ptr_array_index(ancestors, index);
		GtkTreeIter iter;
		GtkTreePath *path;

		if (!sakura_sidebar_get_iter(ancestor, &iter))
			continue;
		path = gtk_tree_model_get_path(GTK_TREE_MODEL(sakura.sidebar_model),
		                               &iter);
		gtk_tree_view_expand_row(GTK_TREE_VIEW(sakura.sidebar_tree), path, FALSE);
		gtk_tree_path_free(path);
	}
	if (expand_node) {
		GtkTreeIter iter;
		GtkTreePath *path;

		if (sakura_sidebar_get_iter(node, &iter)) {
			path = gtk_tree_model_get_path(
				GTK_TREE_MODEL(sakura.sidebar_model), &iter);
			gtk_tree_view_expand_row(GTK_TREE_VIEW(sakura.sidebar_tree), path,
			                         FALSE);
			gtk_tree_path_free(path);
		}
	}
	g_ptr_array_unref(ancestors);
}


static gboolean
sakura_sidebar_reorder_task_to_sibling(SakuraSidebarNode *source,
                                        SakuraSidebarNode *target,
                                        GtkTreeViewDropPosition position)
{
	gchar *source_id, *target_id, *group_id, *parent_id;
	SakuraTask *source_task, *target_task;
	gboolean after, result = FALSE;

	if (source == NULL || target == NULL || source == target ||
	    source->type != SAKURA_SIDEBAR_TASK ||
	    target->type != SAKURA_SIDEBAR_TASK || source->task == NULL ||
	    target->task == NULL || source->task->group != target->task->group ||
	    source->task->parent != target->task->parent)
		return FALSE;
	after = position == GTK_TREE_VIEW_DROP_AFTER ||
	        position == GTK_TREE_VIEW_DROP_INTO_OR_AFTER;
	source_id = g_strdup(source->task->id);
	target_id = g_strdup(target->task->id);
	group_id = g_strdup(source->task->group->id);
	parent_id = g_strdup(source->task->parent != NULL
	                     ? source->task->parent->id : NULL);
	if (sakura.agent_socket_path != NULL) {
		GError *error = NULL;

		if (!sakura_agent_move_task(&sakura, source_id, group_id, parent_id,
		                            target_id, after, &error)) {
			if (error != NULL)
				g_warning("Could not reorder task through sakura-agent: %s",
				          error->message);
			g_clear_error(&error);
			goto out;
		}
	}
	source_task = sakura_workspace_model_find_task(sakura.workspace, source_id);
	target_task = sakura_workspace_model_find_task(sakura.workspace, target_id);
	if (source_task == NULL || target_task == NULL)
		goto out;
	sakura_workspace_begin_mutation();
	sakura_sidebar_cancel_pending_selection();
	if (!sakura_workspace_model_reorder_task(sakura.workspace, source_task,
	                                         target_task, after)) {
		sakura_workspace_end_mutation();
		goto out;
	}
	/* Reproject from the domain order so GTK cannot retain a different native
	 * drag order. Stable selection and expansion IDs survive the rebuild. */
	sakura_sidebar_rebuild_projection();
	sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_STRUCTURE);
	sakura_session_mark_dirty();
	sakura_workspace_end_mutation();
	result = TRUE;
out:
	g_free(source_id);
	g_free(target_id);
	g_free(group_id);
	g_free(parent_id);
	return result;
}


static gboolean
sakura_sidebar_reorder_node_to_group(SakuraSidebarNode *source,
                                     SakuraSidebarNode *target,
                                     GtkTreeViewDropPosition position)
{
	SakuraSidebarNode *parent;
	GtkTreeIter source_iter, target_iter;
	gboolean after = position == GTK_TREE_VIEW_DROP_AFTER ||
	                 position == GTK_TREE_VIEW_DROP_INTO_OR_AFTER;

	if (source == NULL || target == NULL || source == target ||
	    !sakura_sidebar_can_reorder_node_to_group(source, target))
		return FALSE;
	parent = source->type == SAKURA_SIDEBAR_GROUP ? target->parent : target;
	if (parent == NULL)
		return FALSE;

	sakura_workspace_begin_mutation();
	sakura_sidebar_cancel_pending_selection();
	if (!sakura_sidebar_get_iter(source, &source_iter)) {
		sakura_workspace_end_mutation();
		return FALSE;
	}
	if (source->type == SAKURA_SIDEBAR_TASK) {
		if (sakura.agent_socket_path != NULL) {
			GError *error = NULL;
			gchar *task_id = g_strdup(source->task->id);
			gchar *group_id = g_strdup(parent->group->id);

			if (!sakura_agent_move_task(&sakura, task_id, group_id, NULL, NULL,
			                            FALSE, &error)) {
				if (error != NULL)
					g_warning("Could not reorder task through sakura-agent: %s",
					          error->message);
				g_clear_error(&error);
				g_free(task_id);
				g_free(group_id);
				sakura_workspace_end_mutation();
				return FALSE;
			}
			{
				SakuraTask *resolved_task = sakura_workspace_model_find_task(
					sakura.workspace, task_id);
				SakuraGroup *resolved_group = sakura_workspace_group_by_id(group_id);

				source = resolved_task != NULL ? resolved_task->sidebar_node : NULL;
				parent = resolved_group != NULL ? resolved_group->sidebar_node : NULL;
			}
			g_free(task_id);
			g_free(group_id);
			if (source == NULL || parent == NULL ||
			    !sakura_sidebar_get_iter(source, &source_iter)) {
				sakura_workspace_end_mutation();
				return FALSE;
			}
		}
		if (!sakura_workspace_model_append_task(sakura.workspace, source->task, parent->group)) {
			sakura_workspace_end_mutation();
			return FALSE;
		}
		/* A task row owns a page subtree. Moving the existing GTK row keeps all
		 * descendant row references valid. NULL means append within the parent. */
		gtk_tree_store_move_before(sakura.sidebar_model, &source_iter, NULL);
	} else if (!sakura_sidebar_get_iter(target, &target_iter) ||
	           !sakura_workspace_model_reorder_group(sakura.workspace, source->group,
	                                                  target->group, after)) {
		sakura_workspace_end_mutation();
		return FALSE;
	} else if (after)
		gtk_tree_store_move_after(sakura.sidebar_model, &source_iter, &target_iter);
	else
		gtk_tree_store_move_before(sakura.sidebar_model, &source_iter, &target_iter);
	sakura_sidebar_save_groups();
	sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_STRUCTURE);
	sakura_session_mark_dirty();
	sakura_workspace_end_mutation();
	return TRUE;
}


static void
sakura_sidebar_collect_projection_nodes(GtkTreeModel *model,
                                        GtkTreeIter *parent,
                                        GPtrArray *nodes,
                                        GHashTable *seen)
{
	GtkTreeIter iter;
	gboolean valid;

	if (model == NULL || nodes == NULL || seen == NULL)
		return;
	valid = parent == NULL
	      ? gtk_tree_model_get_iter_first(model, &iter)
	      : gtk_tree_model_iter_children(model, &iter, parent);
	while (valid) {
		SakuraSidebarNode *node = NULL;

		gtk_tree_model_get(model, &iter, SAKURA_SIDEBAR_COLUMN_NODE, &node, -1);
		if (node != NULL && g_hash_table_add(seen, node))
			g_ptr_array_add(nodes, node);
		if (gtk_tree_model_iter_has_child(model, &iter))
			sakura_sidebar_collect_projection_nodes(model, &iter, nodes, seen);
		valid = gtk_tree_model_iter_next(model, &iter);
	}
}


static gboolean
sakura_sidebar_node_expansion_kind(SakuraSidebarNode *node,
                                    SakuraSidebarExpansionKind *kind)
{
	if (node == NULL || kind == NULL)
		return FALSE;
	switch (node->type) {
		case SAKURA_SIDEBAR_GROUP:
			*kind = SAKURA_SIDEBAR_EXPANSION_GROUP;
			return TRUE;
		case SAKURA_SIDEBAR_TASK:
			*kind = SAKURA_SIDEBAR_EXPANSION_TASK;
			return TRUE;
		case SAKURA_SIDEBAR_PAGE:
			*kind = SAKURA_SIDEBAR_EXPANSION_SESSION;
			return TRUE;
		default:
			return FALSE;
	}
}


static gchar *
sakura_sidebar_expansion_key(SakuraSidebarExpansionKind kind,
                              const gchar *id)
{
	const gchar *prefix;

	if (id == NULL || id[0] == '\0')
		return NULL;
	prefix = kind == SAKURA_SIDEBAR_EXPANSION_GROUP ? "group"
	        : kind == SAKURA_SIDEBAR_EXPANSION_TASK ? "task" : "session";
	return g_strdup_printf("%s:%s", prefix, id);
}


static void
sakura_sidebar_ensure_expansion_state(void)
{
	if (sakura.sidebar_expansion_keys == NULL)
		sakura.sidebar_expansion_keys = g_hash_table_new_full(
			g_str_hash, g_str_equal, g_free, NULL);
}


static void
sakura_sidebar_collect_expanded_keys(GtkTreeModel *model,
                                     GtkTreeIter *parent,
                                     GHashTable *expanded,
                                     GHashTable *previous,
                                     gboolean ancestors_expanded)
{
	GtkTreeIter iter;
	gboolean valid;

	if (model == NULL || expanded == NULL)
		return;
	valid = parent == NULL
	      ? gtk_tree_model_get_iter_first(model, &iter)
	      : gtk_tree_model_iter_children(model, &iter, parent);
	while (valid) {
		SakuraSidebarNode *node = NULL;
		SakuraSidebarExpansionKind kind;
		GtkTreePath *path;
		gboolean row_expanded = TRUE;

		gtk_tree_model_get(model, &iter, SAKURA_SIDEBAR_COLUMN_NODE, &node, -1);
		if (node != NULL && sakura_sidebar_node_expansion_kind(node, &kind)) {
			gchar *key;

			path = gtk_tree_model_get_path(model, &iter);
			row_expanded = gtk_tree_view_row_expanded(
				GTK_TREE_VIEW(sakura.sidebar_tree), path);
			key = sakura_sidebar_expansion_key(kind, node->id);
			if (ancestors_expanded) {
				if (row_expanded && key != NULL)
					g_hash_table_add(expanded, key);
				else
					g_free(key);
			} else if (key != NULL && previous != NULL &&
			           g_hash_table_contains(previous, key)) {
				/* GTK hides descendants when a parent is collapsed, but that does
				 * not mean the user collapsed each descendant. Keep its stable-ID
				 * expansion choice so reopening the parent restores the subtree. */
				g_hash_table_add(expanded, key);
			} else {
				g_free(key);
			}
			gtk_tree_path_free(path);
		}
		if (gtk_tree_model_iter_has_child(model, &iter))
			sakura_sidebar_collect_expanded_keys(
				model, &iter, expanded, previous,
				ancestors_expanded && row_expanded);
		valid = gtk_tree_model_iter_next(model, &iter);
	}
}


static void
sakura_sidebar_sync_expansion_state_from_view(void)
{
	GHashTable *previous;
	GHashTableIter hash_iter;
	gpointer key;

	sakura_sidebar_ensure_expansion_state();
	previous = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	g_hash_table_iter_init(&hash_iter, sakura.sidebar_expansion_keys);
	while (g_hash_table_iter_next(&hash_iter, &key, NULL))
		g_hash_table_add(previous, g_strdup(key));
	g_hash_table_remove_all(sakura.sidebar_expansion_keys);
	if (sakura.sidebar_tree != NULL && sakura.sidebar_model != NULL)
		sakura_sidebar_collect_expanded_keys(
			GTK_TREE_MODEL(sakura.sidebar_model), NULL,
			sakura.sidebar_expansion_keys, previous, TRUE);
	g_hash_table_destroy(previous);
	sakura.sidebar_expansion_initialized = TRUE;
}


static void
sakura_sidebar_collect_expanded_records(GtkTreeModel *model,
                                        GtkTreeIter *parent,
                                        GPtrArray *records)
{
	GtkTreeIter iter;
	gboolean valid;

	if (model == NULL || records == NULL)
		return;
	valid = parent == NULL
	      ? gtk_tree_model_get_iter_first(model, &iter)
	      : gtk_tree_model_iter_children(model, &iter, parent);
	while (valid) {
		SakuraSidebarNode *node = NULL;
		SakuraSidebarExpansionKind kind;
		gchar *key = NULL;

		gtk_tree_model_get(model, &iter, SAKURA_SIDEBAR_COLUMN_NODE, &node, -1);
		if (node != NULL && sakura_sidebar_node_expansion_kind(node, &kind))
			key = sakura_sidebar_expansion_key(kind, node->id);
		if (key != NULL &&
		    g_hash_table_contains(sakura.sidebar_expansion_keys, key)) {
			SakuraSessionSidebarExpansionRecord *record = g_new0(
				SakuraSessionSidebarExpansionRecord, 1);
			record->id = g_strdup(node->id);
			record->kind = kind;
			g_ptr_array_add(records, record);
		}
		g_free(key);
		if (gtk_tree_model_iter_has_child(model, &iter))
			sakura_sidebar_collect_expanded_records(model, &iter, records);
		valid = gtk_tree_model_iter_next(model, &iter);
	}
}


static GHashTable *
sakura_sidebar_expansion_keys_from_snapshot(
	const SakuraSessionSnapshot *snapshot)
{
	GHashTable *expanded = g_hash_table_new_full(g_str_hash, g_str_equal,
	                                               g_free, NULL);

	for (guint index = 0;
	     snapshot != NULL && snapshot->expanded_sidebar_nodes != NULL &&
	     index < snapshot->expanded_sidebar_nodes->len; index++) {
		SakuraSessionSidebarExpansionRecord *record = g_ptr_array_index(
			snapshot->expanded_sidebar_nodes, index);
		gchar *key = record != NULL
		           ? sakura_sidebar_expansion_key(record->kind, record->id) : NULL;
		if (key != NULL)
			g_hash_table_add(expanded, key);
	}
	return expanded;
}


static void
sakura_sidebar_replace_expansion_state(GHashTable *expanded)
{
	GHashTableIter iter;
	gpointer key;

	sakura_sidebar_ensure_expansion_state();
	g_hash_table_remove_all(sakura.sidebar_expansion_keys);
	if (expanded == NULL)
		return;
	g_hash_table_iter_init(&iter, expanded);
	while (g_hash_table_iter_next(&iter, &key, NULL))
		g_hash_table_add(sakura.sidebar_expansion_keys, g_strdup(key));
}


static void
sakura_sidebar_apply_expansion_keys(GtkTreeModel *model,
                                    GtkTreeIter *parent,
                                    GHashTable *expanded)
{
	GtkTreeIter iter;
	gboolean valid;

	if (model == NULL || expanded == NULL)
		return;
	valid = parent == NULL
	      ? gtk_tree_model_get_iter_first(model, &iter)
	      : gtk_tree_model_iter_children(model, &iter, parent);
	while (valid) {
		SakuraSidebarNode *node = NULL;
		SakuraSidebarExpansionKind kind;
		GtkTreePath *path;
		gchar *key = NULL;
		gboolean expandable = FALSE;
		gboolean should_expand = FALSE;

		gtk_tree_model_get(model, &iter, SAKURA_SIDEBAR_COLUMN_NODE, &node, -1);
		if (node != NULL && sakura_sidebar_node_expansion_kind(node, &kind)) {
			expandable = TRUE;
			key = sakura_sidebar_expansion_key(kind, node->id);
		}
		path = gtk_tree_model_get_path(model, &iter);
		should_expand = key != NULL && g_hash_table_contains(expanded, key);
		if (should_expand &&
		    !gtk_tree_view_row_expanded(GTK_TREE_VIEW(sakura.sidebar_tree), path))
			gtk_tree_view_expand_row(GTK_TREE_VIEW(sakura.sidebar_tree), path, FALSE);
		if (gtk_tree_model_iter_has_child(model, &iter))
			sakura_sidebar_apply_expansion_keys(model, &iter, expanded);
		if (expandable && !should_expand)
			gtk_tree_view_collapse_row(GTK_TREE_VIEW(sakura.sidebar_tree), path);
		g_free(key);
		gtk_tree_path_free(path);
		valid = gtk_tree_model_iter_next(model, &iter);
	}
}


static void
sakura_sidebar_restore_expansion_keys(GHashTable *expanded)
{
	if (sakura.sidebar_tree == NULL || sakura.sidebar_model == NULL ||
	    expanded == NULL)
		return;
	gtk_tree_view_collapse_all(GTK_TREE_VIEW(sakura.sidebar_tree));
	sakura_sidebar_apply_expansion_keys(GTK_TREE_MODEL(sakura.sidebar_model),
	                                    NULL, expanded);
}


void
sakura_sidebar_capture_expansion(SakuraSessionSnapshot *snapshot)
{
	if (snapshot == NULL || snapshot->expanded_sidebar_nodes == NULL)
		return;
	/* User expansion is tracked by stable node IDs while the view is live. Sync
	 * once at the persistence boundary as a fallback for tests and any GTK
	 * interaction that happened before the row signal was delivered. Rebuilds do
	 * not use the tree as their source of truth. */
	sakura_sidebar_sync_expansion_state_from_view();
	g_ptr_array_set_size(snapshot->expanded_sidebar_nodes, 0);
	snapshot->sidebar_expansion_saved = TRUE;
	if (sakura.sidebar_model != NULL)
		sakura_sidebar_collect_expanded_records(
			GTK_TREE_MODEL(sakura.sidebar_model), NULL,
			snapshot->expanded_sidebar_nodes);
}


static SakuraSidebarNode *
sakura_sidebar_project_group_node(SakuraGroup *group,
                                  SakuraSidebarNode *parent)
{
	SakuraSidebarNode *node;

	if (group == NULL || group->sidebar_node != NULL)
		return group != NULL ? group->sidebar_node : NULL;
	node = g_new0(SakuraSidebarNode, 1);
	node->type = SAKURA_SIDEBAR_GROUP;
	node->id = g_strdup(group->id);
	node->title = g_strdup(group->title != NULL ? group->title : "");
	node->subtitle = g_strdup("");
	node->tooltip = g_strdup(node->title);
	node->parent = parent;
	node->group = group;
	group->sidebar_node = node;
	sakura_sidebar_insert_node(node);
	sakura_sidebar_update_group_row(node);
	return node;
}


static SakuraSidebarNode *
sakura_sidebar_project_task_node(SakuraTask *task,
                                 SakuraSidebarNode *parent)
{
	SakuraSidebarNode *node;

	if (task == NULL || task->sidebar_node != NULL)
		return task != NULL ? task->sidebar_node : NULL;
	node = g_new0(SakuraSidebarNode, 1);
	node->type = SAKURA_SIDEBAR_TASK;
	node->id = g_strdup(task->id);
	node->title = g_strdup(task->title != NULL ? task->title : "");
	node->subtitle = g_strdup("");
	node->tooltip = g_strdup(node->title);
	node->parent = parent != NULL ? parent : sakura.sidebar_root;
	node->task = task;
	task->sidebar_node = node;
	sakura_sidebar_insert_node(node);
	sakura_task_update_row(task);
	return node;
}


static SakuraSidebarNode *
sakura_sidebar_project_page_node(SakuraPage *page,
                                 SakuraSidebarNode *parent)
{
	SakuraSidebarNode *node;

	if (page == NULL || page->sidebar_node != NULL)
		return page != NULL ? page->sidebar_node : NULL;
	node = g_new0(SakuraSidebarNode, 1);
	node->type = SAKURA_SIDEBAR_PAGE;
	node->id = g_strdup(page->id);
	node->title = g_strdup("");
	node->subtitle = g_strdup("");
	node->tooltip = g_strdup("");
	node->parent = parent != NULL ? parent : sakura.sidebar_root;
	node->page = page;
	page->sidebar_node = node;
	sakura_sidebar_insert_node(node);
	sakura_sidebar_update_page(page);
	return node;
}


static SakuraSidebarNode *
sakura_sidebar_project_tab_node(SakuraTab *tab, SakuraSidebarNode *parent)
{
	SakuraSidebarNode *node;

	if (tab == NULL || tab->sidebar_node != NULL || parent == NULL)
		return tab != NULL ? tab->sidebar_node : NULL;
	node = g_new0(SakuraSidebarNode, 1);
	node->type = SAKURA_SIDEBAR_TERMINAL;
	node->id = g_strdup(tab->terminal_id != NULL ? tab->terminal_id : "");
	node->title = g_strdup("");
	node->subtitle = g_strdup("");
	node->tooltip = g_strdup("");
	node->parent = parent;
	node->tab = tab;
	tab->sidebar_node = node;
	/* Keep a terminal projection for metadata and tab-bar state, but expose it
	 * in the tree only when the page has multiple panes. */
	if (tab->page != NULL && tab->page->panes != NULL &&
	    tab->page->panes->len > 1)
		sakura_sidebar_insert_node(node);
	sakura_sidebar_update_tab(tab);
	return node;
}


void
sakura_sidebar_rebuild_projection(void)
{
	GHashTable *seen;
	GPtrArray *nodes;
	GPtrArray *ordered_groups, *ordered_tasks;
	SakuraGroup *active_group;
	SakuraSidebarNode *selected_node;
	SakuraSidebarNodeType selected_type;
	gchar *selected_id = NULL;
	gboolean selected_valid = FALSE;
	GList *group_link;
	gboolean was_syncing;
	guint index, remaining, pass;
	gint64 trace_started;

	if (sakura.sidebar_model == NULL)
		return;
	trace_started = sakura_ui_latency_trace_begin();
	sakura_sidebar_ensure_expansion_state();
	if (!sakura.sidebar_expansion_initialized && sakura.session_snapshot != NULL &&
	    sakura.session_snapshot->sidebar_expansion_saved) {
		GHashTable *saved = sakura_sidebar_expansion_keys_from_snapshot(
			sakura.session_snapshot);
		sakura_sidebar_replace_expansion_state(saved);
		g_hash_table_destroy(saved);
		sakura.sidebar_expansion_initialized = TRUE;
	}

	active_group = sakura.workspace->active_group != NULL ? sakura.workspace->active_group : sakura.workspace->root_group;
	if (!sakura.show_archived && sakura_sidebar_group_is_archived(active_group))
		active_group = sakura.workspace->root_group;
	if (!sakura.show_archived &&
	    sakura_sidebar_task_is_archived(sakura.workspace->active_task))
		sakura.workspace->active_task = NULL;
	/* Capture identity before clearing the projection. A pending request is
	 * more authoritative than GTK's current row; both are still valid only
	 * until gtk_tree_store_clear() below invalidates their projection nodes. */
	selected_node = sakura.sidebar_pending_selection != NULL
	               ? sakura_sidebar_node_from_row_reference(
	                     sakura.sidebar_pending_selection)
	               : sakura_sidebar_selected_node();
	if (selected_node != NULL)
		sakura_sidebar_remember_selection(selected_node);
	/* A one-pane session is represented by its page row only. Older live
	 * projections may still have its terminal row selected when rebuilding;
	 * preserve that selection by promoting it to the owning page. */
	if (sakura.sidebar_selection_valid &&
	    sakura.sidebar_selection_type == SAKURA_SIDEBAR_TERMINAL &&
	    selected_node != NULL && selected_node->tab != NULL &&
	    selected_node->tab->page != NULL &&
	    selected_node->tab->page->panes != NULL &&
	    selected_node->tab->page->panes->len <= 1) {
		g_free(sakura.sidebar_selection_id);
		sakura.sidebar_selection_id = g_strdup(selected_node->tab->page->id);
		sakura.sidebar_selection_type = SAKURA_SIDEBAR_PAGE;
	}
	if (sakura.sidebar_selection_valid) {
		selected_type = sakura.sidebar_selection_type;
		selected_id = g_strdup(sakura.sidebar_selection_id);
		selected_valid = TRUE;
	}
	was_syncing = sakura.sidebar_syncing;
	sakura_sidebar_cancel_pending_selection();
	sakura.sidebar_pending_insert_after = NULL;
	sakura.sidebar_syncing = TRUE;
	/* Remove the projection's old GTK selection before deleting rows. This
	 * prevents GtkTreeSelection from choosing a neighbouring row while the
	 * replacement projection is being assembled. */
	if (sakura.sidebar_selection != NULL)
		gtk_tree_selection_unselect_all(sakura.sidebar_selection);
	seen = g_hash_table_new(g_direct_hash, g_direct_equal);
	nodes = g_ptr_array_new();
	sakura_sidebar_collect_projection_nodes(GTK_TREE_MODEL(sakura.sidebar_model),
	                                       NULL, nodes, seen);
	if (sakura.workspace->groups != NULL) {
		for (group_link = sakura.workspace->groups; group_link != NULL; group_link = group_link->next) {
			SakuraGroup *group = group_link->data;
			if (group != NULL && group->sidebar_node != NULL &&
			    g_hash_table_add(seen, group->sidebar_node))
				g_ptr_array_add(nodes, group->sidebar_node);
		}
	}
	if (sakura.workspace->tasks != NULL) {
		for (index = 0; index < sakura.workspace->tasks->len; index++) {
			SakuraTask *task = g_ptr_array_index(sakura.workspace->tasks, index);
			if (task != NULL && task->sidebar_node != NULL &&
			    g_hash_table_add(seen, task->sidebar_node))
				g_ptr_array_add(nodes, task->sidebar_node);
		}
	}
	if (sakura.workspace->pages != NULL) {
		for (index = 0; index < sakura.workspace->pages->len; index++) {
			SakuraPage *page = g_ptr_array_index(sakura.workspace->pages, index);
			guint pane_index;

			if (page == NULL)
				continue;
			if (page->sidebar_node != NULL &&
			    g_hash_table_add(seen, page->sidebar_node))
				g_ptr_array_add(nodes, page->sidebar_node);
			for (pane_index = 0; page->panes != NULL &&
			     pane_index < page->panes->len; pane_index++) {
				SakuraTab *tab = g_ptr_array_index(page->panes, pane_index);
				if (tab != NULL && tab->sidebar_node != NULL &&
				    g_hash_table_add(seen, tab->sidebar_node))
					g_ptr_array_add(nodes, tab->sidebar_node);
			}
		}
	}
	gtk_tree_store_clear(sakura.sidebar_model);
	for (index = 0; index < nodes->len; index++)
		sakura_sidebar_free_node(g_ptr_array_index(nodes, index));
	g_ptr_array_unref(nodes);
	g_hash_table_destroy(seen);
	sakura.sidebar_root = NULL;

	if (sakura.workspace->root_group != NULL) {
		SakuraSidebarNode *root = sakura_sidebar_project_group_node(
			sakura.workspace->root_group, NULL);
		sakura.sidebar_root = root;
	}
	ordered_groups = sakura_workspace_model_ordered_groups(sakura.workspace);

	/* Parent groups are model links. Defer a child until its parent projection
	 * exists so the sidebar never has to infer group ownership from row order. */
	remaining = 0;
	for (index = 0; index < ordered_groups->len; index++) {
		SakuraGroup *group = g_ptr_array_index(ordered_groups, index);
		if (group != NULL &&
		    (sakura.show_archived || !sakura_sidebar_group_is_archived(group)))
			remaining++;
	}
	for (pass = 0; remaining > 0 && pass <= ordered_groups->len; pass++) {
		gboolean progress = FALSE;
		for (index = 0; index < ordered_groups->len; index++) {
			SakuraGroup *group = g_ptr_array_index(ordered_groups, index);
			SakuraSidebarNode *parent;

			if (group == NULL || group->sidebar_node != NULL ||
			    (!sakura.show_archived && sakura_sidebar_group_is_archived(group)))
				continue;
			parent = group->parent != NULL ? group->parent->sidebar_node
			                              : sakura.sidebar_root;
			if (group->parent != NULL && parent == NULL)
				continue;
			sakura_sidebar_project_group_node(group,
			                                  parent != NULL ? parent : sakura.sidebar_root);
			remaining--;
			progress = TRUE;
		}
		if (!progress)
			break;
	}
	if (remaining > 0) {
		/* A corrupt parent cycle should not leave the entire projection empty.
		 * Repair only the model link needed to restore a valid root projection. */
		for (index = 0; index < ordered_groups->len; index++) {
			SakuraGroup *group = g_ptr_array_index(ordered_groups, index);
			if (group == NULL || group->sidebar_node != NULL ||
			    (!sakura.show_archived && sakura_sidebar_group_is_archived(group)))
				continue;
			if (group->parent == NULL || group->parent->sidebar_node != NULL) {
				group->parent = sakura.workspace->root_group;
				sakura_sidebar_project_group_node(group, sakura.sidebar_root);
			}
		}
	}

	/* Tasks are projected in parent order even if the model registry was loaded
	 * from a snapshot whose record order differs from the hierarchy. */
	ordered_tasks = sakura_workspace_model_ordered_tasks(sakura.workspace);
	remaining = 0;
	for (index = 0; index < ordered_tasks->len; index++) {
		SakuraTask *task = g_ptr_array_index(ordered_tasks, index);
		if (task != NULL &&
		    (sakura.show_archived || !sakura_sidebar_task_is_archived(task)))
			remaining++;
	}
	for (pass = 0; remaining > 0 && pass <= ordered_tasks->len; pass++) {
		gboolean progress = FALSE;
		for (index = 0; index < ordered_tasks->len; index++) {
			SakuraTask *task = g_ptr_array_index(ordered_tasks, index);
			SakuraSidebarNode *parent;

			if (task == NULL || task->sidebar_node != NULL ||
			    (!sakura.show_archived && sakura_sidebar_task_is_archived(task)))
				continue;
			if (task->parent != NULL) {
				parent = task->parent->sidebar_node;
				if (parent == NULL)
					continue;
			} else {
				parent = task->group != NULL ? task->group->sidebar_node
				                            : sakura.sidebar_root;
			}
			sakura_sidebar_project_task_node(task,
			                                parent != NULL ? parent : sakura.sidebar_root);
			remaining--;
			progress = TRUE;
		}
		if (!progress)
			break;
	}
	if (remaining > 0) {
		for (index = 0; index < ordered_tasks->len; index++) {
			SakuraTask *task = g_ptr_array_index(ordered_tasks, index);
			SakuraSidebarNode *parent;

			if (task == NULL || task->sidebar_node != NULL ||
			    (!sakura.show_archived && sakura_sidebar_task_is_archived(task)))
				continue;
			parent = task->group != NULL ? task->group->sidebar_node
			                            : sakura.sidebar_root;
			if (parent == NULL || parent->row != NULL)
				sakura_sidebar_project_task_node(task,
				                                parent != NULL ? parent : sakura.sidebar_root);
		}
	}
	g_ptr_array_unref(ordered_tasks);
	g_ptr_array_unref(ordered_groups);

	if (sakura.workspace->pages != NULL) {
		for (index = 0; index < sakura.workspace->pages->len; index++) {
			SakuraPage *page = g_ptr_array_index(sakura.workspace->pages, index);
			SakuraSidebarNode *parent;
			SakuraGroup *group;
			guint pane_index;

			if (page == NULL ||
			    (!sakura.show_archived &&
			     (page->archived ||
			      sakura_sidebar_task_is_archived(page->task) ||
			      sakura_sidebar_group_is_archived(page->group))))
				continue;
			group = page->group != NULL ? page->group : sakura_workspace_model_group_for_session(sakura.workspace, page);
			if (page->task != NULL && page->task->sidebar_node != NULL)
				parent = page->task->sidebar_node;
			else
				parent = group != NULL ? group->sidebar_node : sakura.sidebar_root;
			sakura_sidebar_project_page_node(page,
			                                parent != NULL ? parent : sakura.sidebar_root);
			for (pane_index = 0; page->panes != NULL &&
			     pane_index < page->panes->len; pane_index++)
				sakura_sidebar_project_tab_node(
					g_ptr_array_index(page->panes, pane_index), page->sidebar_node);
		}
	}

	sakura.workspace->active_group = active_group != NULL ? active_group : sakura.workspace->root_group;
	sakura.active_group_scope = sakura_sidebar_group_node(sakura.workspace->active_group);
	if (sakura.sidebar_expansion_initialized)
		sakura_sidebar_restore_expansion_keys(sakura.sidebar_expansion_keys);
	else
		sakura_sidebar_apply_default_expansion();
	sakura.sidebar_expansion_initialized = TRUE;
	if (selected_valid) {
		selected_node = sakura_sidebar_find_node_by_identity(selected_type,
		                                                    selected_id);
		if (selected_node != NULL)
			sakura_sidebar_select_node_now(selected_node);
		else {
			/* The selected model entity may have been archived or deleted. Do not
			 * allow GTK to invent a replacement row in that case. */
			sakura.sidebar_selection_valid = FALSE;
			g_clear_pointer(&sakura.sidebar_selection_id, g_free);
		}
	}
	sakura.sidebar_syncing = was_syncing;
	g_free(selected_id);
	sakura_ui_latency_trace_end("sidebar-projection", trace_started);
}


static void
sakura_sidebar_remove_node_row(SakuraSidebarNode *node)
{
	GtkTreeIter iter;

	if (node == NULL || node->row == NULL || sakura.sidebar_model == NULL)
		return;
	if (sakura_sidebar_get_iter(node, &iter))
		gtk_tree_store_remove(sakura.sidebar_model, &iter);
	gtk_tree_row_reference_free(node->row);
	node->row = NULL;
}


static void
sakura_sidebar_show_page_panes(SakuraPage *page)
{
	guint index;

	if (page == NULL || page->sidebar_node == NULL ||
	    page->panes == NULL || page->panes->len <= 1)
		return;
	for (index = 0; index < page->panes->len; index++) {
		SakuraTab *tab = g_ptr_array_index(page->panes, index);
		if (tab != NULL && tab->sidebar_node != NULL &&
		    tab->sidebar_node->row == NULL)
			sakura_sidebar_insert_node(tab->sidebar_node);
	}
}


void
sakura_sidebar_apply_default_expansion(void)
{
	GtkTreeModel *model;
	GList *group_link;
	guint index;
	gchar *key;

	if (sakura.sidebar_tree == NULL || sakura.sidebar_model == NULL)
		return;
	model = GTK_TREE_MODEL(sakura.sidebar_model);
	sakura_sidebar_ensure_expansion_state();
	g_hash_table_remove_all(sakura.sidebar_expansion_keys);
	key = sakura_sidebar_expansion_key(SAKURA_SIDEBAR_EXPANSION_GROUP,
	                                   sakura.sidebar_root != NULL
	                                   ? sakura.sidebar_root->id : NULL);
	if (key != NULL)
		g_hash_table_add(sakura.sidebar_expansion_keys, key);
	for (group_link = sakura.workspace != NULL ? sakura.workspace->groups : NULL;
	     group_link != NULL; group_link = group_link->next) {
		SakuraGroup *group = group_link->data;

		key = sakura_sidebar_expansion_key(SAKURA_SIDEBAR_EXPANSION_GROUP,
		                                   group != NULL ? group->id : NULL);
		if (key != NULL)
			g_hash_table_add(sakura.sidebar_expansion_keys, key);
	}
	for (index = 0; sakura.workspace != NULL &&
	     sakura.workspace->tasks != NULL &&
	     index < sakura.workspace->tasks->len; index++) {
		SakuraTask *task = g_ptr_array_index(sakura.workspace->tasks, index);

		key = sakura_sidebar_expansion_key(SAKURA_SIDEBAR_EXPANSION_TASK,
		                                   task != NULL ? task->id : NULL);
		if (key != NULL)
			g_hash_table_add(sakura.sidebar_expansion_keys, key);
	}
	gtk_tree_view_collapse_all(GTK_TREE_VIEW(sakura.sidebar_tree));
	sakura_sidebar_apply_expansion_keys(model, NULL, sakura.sidebar_expansion_keys);
	sakura.sidebar_expansion_initialized = TRUE;
}


void
sakura_sidebar_collapse_all(void)
{
	if (sakura.sidebar_tree == NULL)
		return;
	sakura_sidebar_ensure_expansion_state();
	g_hash_table_remove_all(sakura.sidebar_expansion_keys);
	gtk_tree_view_collapse_all(GTK_TREE_VIEW(sakura.sidebar_tree));
	sakura.sidebar_expansion_initialized = TRUE;
	sakura_session_mark_dirty();
}


static void
sakura_sidebar_hide_page_panes(SakuraPage *page)
{
	guint index;

	if (page == NULL || page->panes == NULL)
		return;
	for (index = 0; index < page->panes->len; index++) {
		SakuraTab *tab = g_ptr_array_index(page->panes, index);
		if (tab != NULL && tab->sidebar_node != NULL)
			sakura_sidebar_remove_node_row(tab->sidebar_node);
	}
}


void
sakura_sidebar_remove_page(SakuraPage *page)
{
	SakuraSidebarNode *node;

	if (page == NULL || page->sidebar_node == NULL)
		return;
	node = page->sidebar_node;
	sakura_sidebar_hide_page_panes(page);
	sakura_sidebar_remove_node_row(node);
	page->sidebar_node = NULL;
	sakura_sidebar_free_node(node);
}


SakuraSidebarNode *
sakura_sidebar_selected_node(void)
{
	GtkTreeIter iter;
	GtkTreeModel *model;
	SakuraSidebarNode *node = NULL;

	if (sakura.sidebar_selection == NULL ||
	    !gtk_tree_selection_get_selected(sakura.sidebar_selection, &model, &iter))
		return NULL;

	gtk_tree_model_get(model, &iter, SAKURA_SIDEBAR_COLUMN_NODE, &node, -1);
	return node;
}


static void
sakura_sidebar_remember_selection(SakuraSidebarNode *node)
{
	if (node == NULL || node->id == NULL || node->id[0] == '\0')
		return;
	if (sakura.sidebar_selection_valid &&
	    sakura.sidebar_selection_type == node->type &&
	    g_strcmp0(sakura.sidebar_selection_id, node->id) == 0)
		return;
	g_free(sakura.sidebar_selection_id);
	sakura.sidebar_selection_id = g_strdup(node->id);
	sakura.sidebar_selection_type = node->type;
	sakura.sidebar_selection_valid = TRUE;
}


static SakuraSidebarNode *
sakura_sidebar_node_from_row_reference(GtkTreeRowReference *row)
{
	GtkTreePath *path;
	GtkTreeIter iter;
	SakuraSidebarNode *node = NULL;

	if (row == NULL || sakura.sidebar_model == NULL)
		return NULL;
	path = gtk_tree_row_reference_get_path(row);
	if (path == NULL)
		return NULL;
	if (gtk_tree_model_get_iter(GTK_TREE_MODEL(sakura.sidebar_model), &iter, path))
		gtk_tree_model_get(GTK_TREE_MODEL(sakura.sidebar_model), &iter,
		                   SAKURA_SIDEBAR_COLUMN_NODE, &node, -1);
	gtk_tree_path_free(path);
	return node;
}


static SakuraSidebarNode *
sakura_sidebar_find_node_by_identity(SakuraSidebarNodeType type,
                                     const gchar *id)
{
	SakuraGroup *group;
	SakuraTask *task;
	SakuraPage *page;
	SakuraTab *tab;

	if (id == NULL || id[0] == '\0')
		return NULL;
	switch (type) {
	case SAKURA_SIDEBAR_GROUP:
		group = sakura_workspace_group_by_id(id);
		return group != NULL ? group->sidebar_node : NULL;
	case SAKURA_SIDEBAR_TASK:
		task = sakura_workspace_model_find_task(sakura.workspace, id);
		return task != NULL ? task->sidebar_node : NULL;
	case SAKURA_SIDEBAR_PAGE:
		page = sakura_sidebar_page_by_id(id);
		return page != NULL ? page->sidebar_node : NULL;
	case SAKURA_SIDEBAR_TERMINAL:
		tab = sakura_find_pane_by_terminal_id(id);
		return tab != NULL ? tab->sidebar_node : NULL;
	default:
		return NULL;
	}
}


SakuraSidebarNode *
sakura_sidebar_selected_group(void)
{
	SakuraSidebarNode *node = sakura_sidebar_selected_node();

	if (node == NULL)
		return sakura.sidebar_root;
	if (node->type == SAKURA_SIDEBAR_TASK)
		return sakura_sidebar_group_node(sakura_workspace_model_group_for_task(sakura.workspace, node->task));
	if (node->type == SAKURA_SIDEBAR_PAGE)
		return sakura_sidebar_group_node(sakura_workspace_model_group_for_session(sakura.workspace, node->page));
	if (node->type == SAKURA_SIDEBAR_TERMINAL)
		return sakura_sidebar_group_node(sakura_workspace_model_group_for_session(sakura.workspace,
			                       node->tab != NULL ? node->tab->page : NULL));
	return sakura_sidebar_group_ancestor(node);
}


void
sakura_sidebar_update_tab(SakuraTab *tab)
{
	GtkTreeIter iter;
	SakuraSidebarNode *node;
	gchar *title, *subtitle, *tooltip, *display_path, *full_subtitle;
	gchar *group_directory;
	gboolean hide_directory;
	gint page;

	if (tab == NULL || tab->sidebar_node == NULL)
		return;

	node = tab->sidebar_node;
	if (tab->label_set_byuser) {
		title = g_strdup(gtk_label_get_text(GTK_LABEL(tab->label)));
		g_strstrip(title);
	} else if (tab->kind == SAKURA_TAB_CODEX) {
		title = tab->codex_session_name != NULL && tab->codex_session_name[0] != '\0'
		       ? g_strdup(tab->codex_session_name) : g_strdup(_("Codex"));
	} else if (tab->kind == SAKURA_TAB_TOOL &&
	           (tab->tool == SAKURA_TOOL_GH_DASH || tab->tool == SAKURA_TOOL_GH_PR)) {
		title = g_strdup(sakura_tool_label(tab->tool));
	} else if (tab->cwd != NULL && tab->cwd[0] != '\0') {
		if (g_strcmp0(tab->cwd, g_get_home_dir()) == 0)
			title = g_strdup("~");
		else
			title = g_path_get_basename(tab->cwd);
	} else {
		page = sakura_page_for_tab(tab);
		title = g_strdup_printf(_("Terminal %d"), page >= 0 ? page + 1 : 1);
	}

	display_path = NULL;
	if (tab->kind != SAKURA_TAB_TOOL && tab->cwd != NULL && tab->cwd[0] != '\0') {
		const gchar *home = g_get_home_dir();
		if (home != NULL && g_str_has_prefix(tab->cwd, home) &&
		    (tab->cwd[strlen(home)] == '\0' || tab->cwd[strlen(home)] == '/'))
			display_path = g_strdup_printf("~%s", tab->cwd + strlen(home));
		else
			display_path = g_strdup(tab->cwd);
	}
	if (tab->host != NULL && display_path != NULL)
		full_subtitle = g_strdup_printf("%s · %s", tab->host, display_path);
	else if (tab->host != NULL)
		full_subtitle = g_strdup(tab->host);
	else if (display_path != NULL)
		full_subtitle = g_strdup(display_path);
	else
		full_subtitle = g_strdup("");

	group_directory = sakura_sidebar_directory_for_node(node);
	hide_directory = display_path != NULL && group_directory != NULL &&
	                 sakura_sidebar_paths_equal(tab->cwd, group_directory);
	if (hide_directory && tab->host == NULL)
		subtitle = g_strdup("");
	else if (hide_directory)
		subtitle = g_strdup(tab->host);
	else
		subtitle = g_strdup(full_subtitle);
	node->subtitle_is_directory = display_path != NULL && !hide_directory;

	if (tab->raw_title != NULL && tab->raw_title[0] != '\0' && full_subtitle[0] != '\0')
		tooltip = g_strdup_printf("%s\n%s", tab->raw_title, full_subtitle);
	else if (tab->raw_title != NULL && tab->raw_title[0] != '\0')
		tooltip = g_strdup(tab->raw_title);
	else
		tooltip = g_strdup(full_subtitle);

	g_free(node->title);
	g_free(node->subtitle);
	g_free(node->tooltip);
	node->title = title;
	node->subtitle = subtitle;
	node->tooltip = tooltip;
	if (sakura_sidebar_get_iter(node, &iter))
		sakura_sidebar_set_node_row(node, &iter);
	sakura_sidebar_update_page(tab->page);
	sakura_tab_bar_update_tab(tab);
	g_free(display_path);
	g_free(full_subtitle);
	g_free(group_directory);
	sakura_session_mark_dirty();
}


void
sakura_sidebar_remove_tab(SakuraTab *tab)
{
	SakuraPage *page;
	SakuraGroup *group;
	SakuraSidebarNode *node;

	if (tab == NULL || tab->sidebar_node == NULL)
		return;
	page = tab->page;
	node = tab->sidebar_node;
	group = sakura_workspace_model_group_for_session(sakura.workspace, page);

	sakura_tab_bar_remove_tab(tab);
	if (group != NULL &&
	    g_strcmp0(group->last_terminal_id, tab->terminal_id) == 0)
		g_clear_pointer(&group->last_terminal_id, g_free);
	sakura_sidebar_remove_node_row(node);
	sakura_sidebar_free_node(node);
	tab->sidebar_node = NULL;
	if (page != NULL && page->panes != NULL) {
		if (page->panes->len == 2)
			sakura_sidebar_hide_page_panes(page);
		sakura_sidebar_update_page(page);
	}
	sakura_sidebar_update_attention_count();
}


static void
sakura_sidebar_select_node_now(SakuraSidebarNode *node)
{
	GtkTreePath *path;
	gboolean was_syncing;

	if (sakura.sidebar_selection == NULL || node == NULL || node->row == NULL)
		return;

	path = gtk_tree_row_reference_get_path(node->row);
	if (path == NULL)
		return;

	sakura_sidebar_remember_selection(node);
	was_syncing = sakura.sidebar_syncing;
	sakura.sidebar_syncing = TRUE;
	gtk_tree_selection_select_path(sakura.sidebar_selection, path);
	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(sakura.sidebar_tree), path, NULL, FALSE, 0, 0);
	sakura.sidebar_syncing = was_syncing;
	gtk_tree_path_free(path);
}


void
sakura_sidebar_select_created_tab(SakuraTab *tab)
{
	SakuraSidebarNode *node;

	if (tab == NULL || tab->sidebar_node == NULL)
		return;
	node = tab->page != NULL && tab->page->panes != NULL &&
	       tab->page->panes->len <= 1
	     ? tab->page->sidebar_node : tab->sidebar_node;
	if (node == NULL)
		return;
	/* Creation is authoritative: discard a scope/notebook selection queued
	 * while the new page and its sidebar rows were being assembled. */
	sakura_sidebar_cancel_pending_selection();
	sakura_sidebar_reveal_node(node, FALSE);
	sakura_sidebar_select_node_now(node);
	sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_SELECTION);
}


void
sakura_sidebar_cancel_pending_selection(void)
{
	if (sakura.sidebar_selection_source_id != 0) {
		g_source_remove(sakura.sidebar_selection_source_id);
		sakura.sidebar_selection_source_id = 0;
	}
	if (sakura.sidebar_pending_selection != NULL) {
		gtk_tree_row_reference_free(sakura.sidebar_pending_selection);
		sakura.sidebar_pending_selection = NULL;
	}
	sakura.sidebar_pending_selection_reason = SAKURA_SIDEBAR_SELECTION_SYNC;
}


static gboolean
sakura_sidebar_select_pending_cb(gpointer data)
{
	GtkTreePath *path;
	GtkTreeRowReference *row;
	GtkTreeIter iter;
	SakuraSidebarNode *node = NULL;

	(void)data;
	sakura.sidebar_selection_source_id = 0;
	row = sakura.sidebar_pending_selection;
	sakura.sidebar_pending_selection = NULL;
	if (row == NULL)
		return G_SOURCE_REMOVE;

	path = gtk_tree_row_reference_get_path(row);
	gtk_tree_row_reference_free(row);
	if (path == NULL)
		return G_SOURCE_REMOVE;

	if (gtk_tree_model_get_iter(GTK_TREE_MODEL(sakura.sidebar_model), &iter, path))
		gtk_tree_model_get(GTK_TREE_MODEL(sakura.sidebar_model), &iter,
		                   SAKURA_SIDEBAR_COLUMN_NODE, &node, -1);
	if (node != NULL)
		sakura_sidebar_select_node_now(node);
	gtk_tree_path_free(path);
	return G_SOURCE_REMOVE;
}


void
sakura_sidebar_queue_select_node_with_reason(SakuraSidebarNode *node,
                                             SakuraSidebarSelectionReason reason)
{
	GtkTreePath *path;

	if (sakura.sidebar_selection == NULL || node == NULL || node->row == NULL)
		return;
	if (sakura.sidebar_pending_selection != NULL &&
	    reason < sakura.sidebar_pending_selection_reason)
		return;

	path = gtk_tree_row_reference_get_path(node->row);
	if (path == NULL)
		return;

	sakura_sidebar_cancel_pending_selection();
	sakura.sidebar_pending_selection = gtk_tree_row_reference_new(
		GTK_TREE_MODEL(sakura.sidebar_model), path);
	sakura.sidebar_pending_selection_reason = reason;
	sakura.sidebar_selection_source_id = g_idle_add(sakura_sidebar_select_pending_cb, NULL);
	gtk_tree_path_free(path);
}


void
sakura_sidebar_queue_select_node(SakuraSidebarNode *node)
{
	sakura_sidebar_queue_select_node_with_reason(
		node, SAKURA_SIDEBAR_SELECTION_SYNC);
}
