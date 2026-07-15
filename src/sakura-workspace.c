#include <libintl.h>

#include "sakura-private.h"

#define _(String) gettext(String)
#define CODEX_ICON_NAME "sakura-codex"
#define SAKURA_CONFIG_GROUP "sakura"

static void sakura_sidebar_save_groups(void);
static void sakura_sidebar_rename_group_cb(GtkWidget *widget, void *data);
static void sakura_sidebar_delete_group_cb(GtkWidget *widget, void *data);

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
sakura_tab_is_in_active_scope (struct sakura_tab *sk_tab)
{
	struct sakura_sidebar_node *node;

	if (sk_tab == NULL || sk_tab->sidebar_node == NULL ||
	    sakura.active_group_scope == NULL ||
	    sakura.active_group_scope == sakura.sidebar_root)
		return sk_tab != NULL;

	for (node = sk_tab->sidebar_node->parent; node != NULL; node = node->parent) {
		if (node == sakura.active_group_scope)
			return TRUE;
	}
	return FALSE;
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
	if (sakura.active_group_scope != NULL &&
	    sakura.active_group_scope != sakura.sidebar_root &&
	    (current_tab == NULL || !sakura_tab_is_in_active_scope(current_tab)))
		return sakura.active_group_scope;

	/* With All terminals selected, retain the current tab's ownership group
	 * instead of treating the root scope as a real destination group. */
	if (current_tab != NULL && current_tab->sidebar_node != NULL &&
	    current_tab->sidebar_node->parent != NULL)
		return current_tab->sidebar_node->parent;

	if (sakura.active_group_scope != NULL)
		return sakura.active_group_scope;
	return sakura_sidebar_selected_group();
}


void
sakura_select_tab (struct sakura_tab *sk_tab, gboolean focus)
{
	gint page, current_page;
	struct sakura_sidebar_node *scope;

	if (sk_tab == NULL || sk_tab->hbox == NULL || sakura.notebook == NULL)
		return;

	if (!sakura_tab_is_in_active_scope(sk_tab)) {
		scope = sk_tab->sidebar_node != NULL && sk_tab->sidebar_node->parent != NULL
		      ? sk_tab->sidebar_node->parent : sakura.sidebar_root;
		sakura_sidebar_set_scope(scope);
	}

	page = gtk_notebook_page_num(GTK_NOTEBOOK(sakura.notebook), sk_tab->hbox);
	if (page < 0)
		return;

	sakura.active_tab = sk_tab;
	current_page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	if (current_page != page)
		gtk_notebook_set_current_page(GTK_NOTEBOOK(sakura.notebook), page);
	else
		sakura_sidebar_queue_select_node(sk_tab->sidebar_node);

	sakura_tab_bar_refresh();
	if (focus)
		sakura_focus_tab(sk_tab);
}


void
sakura_select_scope_default (void)
{
	struct sakura_tab *sk_tab = NULL;
	gint page;

	if (sakura.notebook == NULL || sakura.active_group_scope == NULL)
		return;

	if (sakura.active_tab != NULL &&
	    gtk_notebook_page_num(GTK_NOTEBOOK(sakura.notebook), sakura.active_tab->hbox) >= 0 &&
	    sakura_tab_is_in_active_scope(sakura.active_tab)) {
		sk_tab = sakura.active_tab;
	} else if (sakura.active_group_scope->last_terminal_id != NULL) {
		page = sakura_find_tab_by_terminal_id(
			sakura.active_group_scope->last_terminal_id);
		if (page >= 0)
			sk_tab = sakura_tab_at_page(page);
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
	sakura.active_tab = NULL;
	sakura_tab_bar_refresh();
	sakura_sidebar_queue_select_node(sakura.active_group_scope);
}


void
sakura_remember_current_scope_tab (struct sakura_tab *current_tab)
{
	gint page;
	struct sakura_tab *sk_tab;

	if (sakura.active_group_scope == NULL || sakura.notebook == NULL)
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
	if (g_strcmp0(sakura.active_group_scope->last_terminal_id, sk_tab->terminal_id) != 0) {
		g_free(sakura.active_group_scope->last_terminal_id);
		sakura.active_group_scope->last_terminal_id = g_strdup(sk_tab->terminal_id);
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
	if (scope == NULL || scope->type != SAKURA_SIDEBAR_GROUP)
		scope = sakura.sidebar_root;
	if (scope == NULL)
		return;

	if (sakura.active_group_scope == scope) {
		sakura_tab_bar_refresh();
		return;
	}

	sakura_remember_current_scope_tab(NULL);
	sakura.active_group_scope = scope;
	sakura_tab_bar_refresh();
	if (sakura.notebook != NULL && gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook)) > 0)
		sakura_set_size();
	sakura_session_mark_dirty();
}


void
sakura_sidebar_add_terminal (struct sakura_tab *sk_tab, struct sakura_sidebar_node *parent)
{
	struct sakura_sidebar_node *node;

	node = g_new0(struct sakura_sidebar_node, 1);
	node->type = SAKURA_SIDEBAR_TERMINAL;
	node->title = g_strdup(_("Terminal"));
	node->subtitle = g_strdup("");
	node->parent = parent != NULL ? parent : sakura.sidebar_root;
	node->tab = sk_tab;
	sk_tab->sidebar_node = node;
	sakura_sidebar_insert_node(node);
	sakura_tab_bar_add_tab(sk_tab);
	sakura_sidebar_update_tab(sk_tab);
	if (sakura.active_tab == sk_tab)
		sakura_sidebar_queue_select_node(sk_tab->sidebar_node);
	else if (sakura_tab_is_in_active_scope(sk_tab) &&
	         (sakura.active_tab == NULL ||
	          !sakura_tab_is_in_active_scope(sakura.active_tab)))
		sakura_select_tab(sk_tab, TRUE);
	else
		sakura_tab_bar_refresh();
	sakura_set_size();
}


static gboolean
sakura_focus_tab_cb (gpointer data)
{
	GtkWidget *vte = GTK_WIDGET(data);

	if (gtk_widget_get_visible(vte) && gtk_widget_get_realized(vte))
		gtk_widget_grab_focus(vte);
	g_object_unref(vte);
	return G_SOURCE_REMOVE;
}


void
sakura_focus_tab (struct sakura_tab *sk_tab)
{
	if (sk_tab == NULL || sk_tab->vte == NULL)
		return;

#ifdef HAVE_WEBKITGTK
	if (sk_tab->browser != NULL) {
		gtk_widget_grab_focus(sk_tab->browser);
		return;
	}
#endif

	/* Let the tree view finish handling the click before moving focus to the
	 * selected terminal. Keep the widget alive until the idle callback runs. */
	g_idle_add(sakura_focus_tab_cb, g_object_ref(sk_tab->vte));
}
void
sakura_sidebar_selection_changed_cb (GtkTreeSelection *selection, void *data)
{
	struct sakura_sidebar_node *node;

	if (sakura.sidebar_syncing)
		return;

	node = sakura_sidebar_selected_node();
	if (node == NULL)
		return;
	if (node->type == SAKURA_SIDEBAR_GROUP) {
		sakura_sidebar_set_scope(node);
		sakura_select_scope_default();
		return;
	}
	if (node->type != SAKURA_SIDEBAR_TERMINAL || node->tab == NULL)
		return;

	sakura_select_tab(node->tab, TRUE);
	sakura_session_mark_dirty();
}


struct sakura_sidebar_tool_target {
	struct sakura_sidebar_node *node;
	SakuraToolKind tool;
};


struct sakura_sidebar_move_target {
	struct sakura_tab *tab;
	struct sakura_sidebar_node *group;
};


static void
sakura_sidebar_tool_target_free (gpointer data, GClosure *closure)
{
	(void)closure;
	g_free(data);
}


static void
sakura_sidebar_move_target_free (gpointer data, GClosure *closure)
{
	(void)closure;
	g_free(data);
}


static void
sakura_sidebar_prepare_context (struct sakura_sidebar_node *node)
{
	struct sakura_sidebar_node *scope;

	if (node == NULL)
		node = sakura.sidebar_root;
	if (node->type == SAKURA_SIDEBAR_TERMINAL)
		scope = node->parent != NULL ? node->parent : sakura.sidebar_root;
	else
		scope = node;
	if (scope == NULL)
		return;

	if (sakura.active_group_scope != scope) {
		sakura_sidebar_set_scope(scope);
		sakura_select_scope_default();
	}
	if (node->type == SAKURA_SIDEBAR_TERMINAL && node->tab != NULL)
		sakura_select_tab(node->tab, FALSE);
}


static void
sakura_new_tab_in_scope_cb (GtkWidget *widget, void *data)
{
	sakura_sidebar_prepare_context(data);
	sakura_new_tab_cb(widget, NULL);
}


static void
sakura_new_codex_in_scope_cb (GtkWidget *widget, void *data)
{
	sakura_sidebar_prepare_context(data);
	sakura_new_codex_cb(widget, NULL);
}


static void
sakura_resume_codex_in_scope_cb (GtkWidget *widget, void *data)
{
	sakura_sidebar_prepare_context(data);
	sakura_resume_codex_cb(widget, NULL);
}


static void
sakura_open_here_context_cb (GtkWidget *widget, void *data)
{
	SakuraOpenHereKind kind = GPOINTER_TO_INT(
		g_object_get_data(G_OBJECT(widget), "sakura-open-here-kind"));

	sakura_sidebar_prepare_context(data);
	sakura_open_here_cb(widget, GINT_TO_POINTER(kind));
}


static void
sakura_open_pr_context_cb (GtkWidget *widget, void *data)
{
	sakura_sidebar_prepare_context(data);
	sakura_open_pr_cb(widget, NULL);
}


static void
sakura_new_tool_context_cb (GtkWidget *widget, void *data)
{
	struct sakura_sidebar_tool_target *target = data;

	if (target == NULL)
		return;
	sakura_sidebar_prepare_context(target->node);
	sakura_new_tool_cb(widget, GINT_TO_POINTER(target->tool));
}


static void
sakura_sidebar_move_terminal_cb (GtkWidget *widget, void *data)
{
	struct sakura_sidebar_move_target *target = data;
	struct sakura_sidebar_node *node, *old_parent;
	GtkTreeIter old_iter, parent_iter, new_iter;
	GtkTreePath *path;

	if (target == NULL || target->tab == NULL || target->group == NULL ||
	    target->tab->sidebar_node == NULL ||
	    target->tab->sidebar_node->parent == target->group)
		return;
	node = target->tab->sidebar_node;
	old_parent = node->parent;
	if (!sakura_sidebar_get_iter(node, &old_iter) ||
	    !sakura_sidebar_get_iter(target->group, &parent_iter))
		return;

	sakura_sidebar_cancel_pending_selection();
	sakura.sidebar_syncing = TRUE;
	if (node->row != NULL) {
		gtk_tree_row_reference_free(node->row);
		node->row = NULL;
	}
	gtk_tree_store_remove(sakura.sidebar_model, &old_iter);
	if (old_parent != NULL &&
	    g_strcmp0(old_parent->last_terminal_id, node->tab->terminal_id) == 0)
		g_clear_pointer(&old_parent->last_terminal_id, g_free);
	node->parent = target->group;
	gtk_tree_store_append(sakura.sidebar_model, &new_iter,
		                  &parent_iter);
	path = gtk_tree_model_get_path(GTK_TREE_MODEL(sakura.sidebar_model), &new_iter);
	node->row = gtk_tree_row_reference_new(GTK_TREE_MODEL(sakura.sidebar_model), path);
	gtk_tree_path_free(path);
	sakura_sidebar_set_node_row(node, &new_iter);
	sakura.sidebar_syncing = FALSE;

	if (sakura.active_tab == target->tab)
		sakura_select_tab(target->tab, TRUE);
	else
		sakura_tab_bar_refresh();
	sakura_sidebar_save_groups();
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
		g_signal_connect(item, "activate",
		                 G_CALLBACK(sakura_open_here_context_cb), node);
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
	for (group = sakura.sidebar_groups; group != NULL; group = group->next) {
		struct sakura_sidebar_node *node = group->data;
		struct sakura_sidebar_move_target *target;

		if (node == NULL || node == sk_tab->sidebar_node->parent)
			continue;
		item = gtk_menu_item_new_with_label(
			node == sakura.sidebar_root ? _("All terminals") : node->title);
		target = g_new0(struct sakura_sidebar_move_target, 1);
		target->tab = sk_tab;
		target->group = node;
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
			target->node = node;
			target->tool = tool;
			g_signal_connect_data(item, "activate",
			                      G_CALLBACK(sakura_new_tool_context_cb), target,
			                      sakura_sidebar_tool_target_free, 0);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	}
	item = gtk_menu_item_new_with_label(_("Open pull request..."));
	g_signal_connect(item, "activate", G_CALLBACK(sakura_open_pr_context_cb), node);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	return menu;
}


static GtkWidget *
sakura_sidebar_context_menu_new (struct sakura_sidebar_node *node)
{
	GtkWidget *menu, *item, *submenu;
	struct sakura_sidebar_node *context_node = node != NULL
	                                           ? node : sakura.sidebar_root;

	menu = gtk_menu_new();
	if (context_node != NULL && context_node->type == SAKURA_SIDEBAR_TERMINAL) {
		item = gtk_menu_item_new_with_label(_("New terminal in group"));
		g_signal_connect(item, "activate", G_CALLBACK(sakura_new_tab_in_scope_cb),
		                 context_node);
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
			item = gtk_menu_item_new_with_label(_("Move to group"));
			gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		}

		submenu = gtk_menu_new();
		item = gtk_menu_item_new_with_label(_("New Codex session"));
		g_signal_connect(item, "activate",
		                 G_CALLBACK(sakura_new_codex_in_scope_cb), context_node);
		gtk_menu_shell_append(GTK_MENU_SHELL(submenu), item);
		item = gtk_menu_item_new_with_label(_("Resume session..."));
		g_signal_connect(item, "activate",
		                 G_CALLBACK(sakura_resume_codex_in_scope_cb), context_node);
		gtk_menu_shell_append(GTK_MENU_SHELL(submenu), item);
		item = gtk_menu_item_new_with_label(_("Codex"));
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
		item = gtk_menu_item_new_with_label(_("Rename terminal..."));
		g_signal_connect(item, "activate", G_CALLBACK(sakura_set_name_dialog_cb),
		                 context_node->tab);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		if (context_node->tab != NULL && context_node->tab->kind == SAKURA_TAB_CODEX) {
			item = gtk_menu_item_new_with_label(_("Rename Codex session..."));
			g_signal_connect(item, "activate",
			                 G_CALLBACK(sakura_rename_codex_session_cb),
			                 context_node->tab);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
		item = gtk_menu_item_new_with_label(_("Close terminal"));
		g_signal_connect(item, "activate", G_CALLBACK(sakura_close_tab_cb),
		                 context_node->tab);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	} else if (context_node != NULL && context_node->type == SAKURA_SIDEBAR_GROUP) {
		item = gtk_menu_item_new_with_label(
			context_node == sakura.sidebar_root ? _("New terminal") : _("New terminal here"));
		g_signal_connect(item, "activate", G_CALLBACK(sakura_new_tab_in_scope_cb),
		                 context_node);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

		item = gtk_menu_item_new_with_label(
			context_node == sakura.sidebar_root
			? _("New Codex session") : _("New Codex session here"));
		g_signal_connect(item, "activate", G_CALLBACK(sakura_new_codex_in_scope_cb),
		                 context_node);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

		item = gtk_menu_item_new_with_label(_("New subgroup"));
		g_signal_connect(item, "activate", G_CALLBACK(sakura_sidebar_new_group_cb),
		                 context_node);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

		if (context_node != sakura.sidebar_root) {
			item = gtk_menu_item_new_with_label(_("Open Here"));
			gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),
			                          sakura_sidebar_open_here_menu_new(context_node));
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		}

		if (context_node == sakura.sidebar_root) {
			item = gtk_menu_item_new_with_label(_("Tools"));
			gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),
			                          sakura_sidebar_tools_menu_new(context_node));
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		} else {
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
			item = gtk_menu_item_new_with_label(_("Rename group..."));
			g_signal_connect(item, "activate",
			                 G_CALLBACK(sakura_sidebar_rename_group_cb), context_node);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
			item = gtk_menu_item_new_with_label(_("Delete group"));
			g_signal_connect(item, "activate",
			                 G_CALLBACK(sakura_sidebar_delete_group_cb), context_node);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		}
	}
	return menu;
}


gboolean
sakura_sidebar_button_press_cb (GtkWidget *widget, GdkEventButton *event, void *data)
{
	GtkTreePath *path = NULL;
	GtkTreeViewColumn *column = NULL;
	GtkTreeIter iter;
	GtkWidget *menu;
	struct sakura_sidebar_node *node;

	/* A folder label is a row, not the expander itself. GTK therefore does
	 * not expand it when it receives a double click. Keep single-click
	 * activation intact and toggle only group rows on a primary double click. */
	if (event->button == GDK_BUTTON_PRIMARY &&
	    event->type == GDK_2BUTTON_PRESS &&
	    GTK_IS_TREE_VIEW(widget) &&
	    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), event->x, event->y,
	                                  &path, &column, NULL, NULL)) {
		if (gtk_tree_model_get_iter(GTK_TREE_MODEL(sakura.sidebar_model), &iter, path))
			gtk_tree_model_get(GTK_TREE_MODEL(sakura.sidebar_model), &iter,
			                   SAKURA_SIDEBAR_COLUMN_NODE, &node, -1);
		else
			node = NULL;

		if (node != NULL && node->type == SAKURA_SIDEBAR_GROUP) {
			if (gtk_tree_view_row_expanded(GTK_TREE_VIEW(widget), path))
				gtk_tree_view_collapse_row(GTK_TREE_VIEW(widget), path);
			else
				gtk_tree_view_expand_row(GTK_TREE_VIEW(widget), path, FALSE);
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
		/* Resolve the menu target from the row under the pointer. The selection
		 * change callback may run asynchronously, so the selected row can still
		 * refer to the previous group at this point. */
		if (gtk_tree_model_get_iter(GTK_TREE_MODEL(sakura.sidebar_model), &iter, path))
			gtk_tree_model_get(GTK_TREE_MODEL(sakura.sidebar_model), &iter,
			                   SAKURA_SIDEBAR_COLUMN_NODE, &node, -1);
		else
			node = NULL;
		gtk_tree_selection_select_path(sakura.sidebar_selection, path);
	} else {
		gtk_tree_selection_unselect_all(sakura.sidebar_selection);
		node = sakura.sidebar_root;
	}

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
	GtkTreeIter iter;
	struct sakura_sidebar_node *parent, *node;
	struct sakura_sidebar_node *context_node = data;
	const gchar *title;

	if (context_node != NULL && context_node->type == SAKURA_SIDEBAR_TERMINAL)
		context_node = context_node->parent;
	parent = context_node != NULL
	       ? context_node
	       : (sakura.active_group_scope != NULL
	          ? sakura.active_group_scope : sakura_sidebar_selected_group());
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
			node = g_new0(struct sakura_sidebar_node, 1);
			node->type = SAKURA_SIDEBAR_GROUP;
			node->id = g_strdup_printf("group-%u", sakura.sidebar_next_group_id++);
			node->title = g_strdup(title);
			node->parent = parent != NULL ? parent : sakura.sidebar_root;
			sakura.sidebar_groups = g_list_append(sakura.sidebar_groups, node);
			sakura_sidebar_insert_node(node);
			if (sakura_sidebar_get_iter(node, &iter)) {
				GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(sakura.sidebar_model), &iter);
				gtk_tree_view_expand_row(GTK_TREE_VIEW(sakura.sidebar_tree), path, FALSE);
				gtk_tree_selection_select_path(sakura.sidebar_selection, path);
				gtk_tree_path_free(path);
			}
			sakura_sidebar_save_groups();
		}
	}
	gtk_widget_destroy(dialog);
}


static void
sakura_sidebar_rename_group_cb (GtkWidget *widget, void *data)
{
	struct sakura_sidebar_node *node = data;
	GtkWidget *dialog, *entry;
	GtkTreeIter iter;
	const gchar *title;

	if (node == NULL || node->type != SAKURA_SIDEBAR_GROUP)
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
			g_free(node->title);
			node->title = g_strdup(title);
			if (sakura_sidebar_get_iter(node, &iter))
				sakura_sidebar_set_node_row(node, &iter);
			sakura_tab_bar_refresh();
			sakura_sidebar_save_groups();
		}
	}
	gtk_widget_destroy(dialog);
}


static void
sakura_sidebar_delete_group_cb (GtkWidget *widget, void *data)
{
	struct sakura_sidebar_node *node = data;
	GtkTreeIter iter;

	if (node == NULL || node == sakura.sidebar_root ||
	    node->type != SAKURA_SIDEBAR_GROUP ||
	    !sakura_sidebar_get_iter(node, &iter))
		return;

	if (gtk_tree_model_iter_has_child(GTK_TREE_MODEL(sakura.sidebar_model), &iter)) {
		GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(sakura.main_window),
		                                           GTK_DIALOG_MODAL,
		                                           GTK_MESSAGE_INFO,
		                                           GTK_BUTTONS_OK,
		                                           _("Only empty terminal groups can be deleted."));
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
		return;
	}

	if (sakura.active_group_scope == node) {
		sakura_sidebar_set_scope(sakura.sidebar_root);
		sakura_select_scope_default();
	}
	gtk_tree_store_remove(sakura.sidebar_model, &iter);
	sakura.sidebar_groups = g_list_remove(sakura.sidebar_groups, node);
	sakura_sidebar_free_node(node);
	sakura_sidebar_save_groups();
}


struct sakura_sidebar_node *
sakura_sidebar_find_group_by_id (const gchar *id)
{
	GList *group;

	if (id == NULL)
		return NULL;
	for (group = sakura.sidebar_groups; group != NULL; group = group->next) {
		struct sakura_sidebar_node *node = group->data;
		if (g_strcmp0(node->id, id) == 0)
			return node;
	}
	return NULL;
}


void
sakura_sidebar_collect_groups (GtkTreeModel *model, GtkTreeIter *parent,
                               GPtrArray *ids, GPtrArray *parents, GPtrArray *titles)
{
	GtkTreeIter iter;
	gboolean valid;

	valid = parent == NULL
		? gtk_tree_model_get_iter_first(model, &iter)
		: gtk_tree_model_iter_children(model, &iter, parent);
	while (valid) {
		struct sakura_sidebar_node *node = NULL;
		gtk_tree_model_get(model, &iter, SAKURA_SIDEBAR_COLUMN_NODE, &node, -1);
		if (node != NULL && node->type == SAKURA_SIDEBAR_GROUP) {
			if (node != sakura.sidebar_root) {
				g_ptr_array_add(ids, g_strdup(node->id));
				g_ptr_array_add(parents, g_strdup(node->parent != NULL ? node->parent->id : "root"));
				g_ptr_array_add(titles, g_strdup(node->title));
			}
			sakura_sidebar_collect_groups(model, &iter, ids, parents, titles);
		}
		valid = gtk_tree_model_iter_next(model, &iter);
	}
}


static void
sakura_sidebar_sync_parents_for_iter (GtkTreeModel *model, GtkTreeIter *parent_iter,
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
			if (node->type == SAKURA_SIDEBAR_GROUP)
				sakura_sidebar_sync_parents_for_iter(model, &iter, node);
		}
		valid = gtk_tree_model_iter_next(model, &iter);
	}
}


void
sakura_sidebar_sync_parents (void)
{
	if (sakura.sidebar_model != NULL)
		sakura_sidebar_sync_parents_for_iter(GTK_TREE_MODEL(sakura.sidebar_model), NULL, NULL);
}


static void
sakura_sidebar_save_groups (void)
{
	GPtrArray *ids, *parents, *titles;

	sakura_session_accept_changes();
	sakura_sidebar_sync_parents();

	ids = g_ptr_array_new_with_free_func(g_free);
	parents = g_ptr_array_new_with_free_func(g_free);
	titles = g_ptr_array_new_with_free_func(g_free);
	sakura_sidebar_collect_groups(GTK_TREE_MODEL(sakura.sidebar_model), NULL,
	                              ids, parents, titles);

	if (ids->len == 0) {
		g_key_file_remove_key(sakura.cfg, SAKURA_CONFIG_GROUP, "sidebar_group_ids", NULL);
		g_key_file_remove_key(sakura.cfg, SAKURA_CONFIG_GROUP, "sidebar_group_parents", NULL);
		g_key_file_remove_key(sakura.cfg, SAKURA_CONFIG_GROUP, "sidebar_group_titles", NULL);
	} else {
		g_key_file_set_string_list(sakura.cfg, SAKURA_CONFIG_GROUP, "sidebar_group_ids",
		                           (const gchar * const *)ids->pdata, ids->len);
		g_key_file_set_string_list(sakura.cfg, SAKURA_CONFIG_GROUP, "sidebar_group_parents",
		                           (const gchar * const *)parents->pdata, parents->len);
		g_key_file_set_string_list(sakura.cfg, SAKURA_CONFIG_GROUP, "sidebar_group_titles",
		                           (const gchar * const *)titles->pdata, titles->len);
	}
	sakura.config_modified = TRUE;
	sakura_session_mark_dirty();
	g_ptr_array_free(ids, TRUE);
	g_ptr_array_free(parents, TRUE);
	g_ptr_array_free(titles, TRUE);
}


void
sakura_sidebar_collect_terminals (GtkTreeModel *model, GtkTreeIter *parent,
                                  GPtrArray *terminals)
{
	GtkTreeIter iter;
	gboolean valid;

	valid = parent == NULL
		? gtk_tree_model_get_iter_first(model, &iter)
		: gtk_tree_model_iter_children(model, &iter, parent);
	while (valid) {
		struct sakura_sidebar_node *node = NULL;
		gtk_tree_model_get(model, &iter, SAKURA_SIDEBAR_COLUMN_NODE, &node, -1);
		if (node != NULL && node->type == SAKURA_SIDEBAR_TERMINAL)
			g_ptr_array_add(terminals, node);
		else if (node != NULL && node->type == SAKURA_SIDEBAR_GROUP)
			sakura_sidebar_collect_terminals(model, &iter, terminals);
		valid = gtk_tree_model_iter_next(model, &iter);
	}
}
gboolean
sakura_workspace_restore_snapshot (SakuraSessionSnapshot *snapshot)
{
	gint selected_terminal, restored = 0;
	guint i;

	if (snapshot == NULL)
		return FALSE;
	if (snapshot->tabs->len == 0) {
		return FALSE;
	}

	selected_terminal = snapshot->selected_terminal;
	for (i = 0; i < snapshot->tabs->len; i++) {
		SakuraSessionTabRecord *record = g_ptr_array_index(snapshot->tabs, i);
		struct sakura_sidebar_node *parent =
			sakura_sidebar_find_group_by_id(record->parent_id);
		SakuraTabKind tab_kind = record->kind;
		SakuraToolKind tool_kind = SAKURA_TOOL_NONE;
		gchar *cwd = g_strdup(record->cwd);
		gboolean title_set = record->title_set_by_user &&
		                     record->title != NULL && record->title[0] != '\0';

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
		sakura_add_tab_with_options(cwd, parent, title_set ? record->title : NULL,
		                            title_set, tab_kind, tool_kind,
		                            tab_kind == SAKURA_TAB_CODEX ? record->codex_session_id : NULL,
		                            tab_kind == SAKURA_TAB_CODEX ? record->codex_session_name : NULL,
		                            tab_kind == SAKURA_TAB_TOOL ? record->tool_target : NULL,
		                            sakura_terminal_id_is_valid(record->terminal_id)
		                            ? record->terminal_id : NULL);
		if (selected_terminal == (gint)i)
			selected_terminal = restored;
		restored++;
		g_free(cwd);
	}

	if (restored > 0) {
		gint selected_page = sakura_find_tab_by_terminal_id(snapshot->selected_terminal_id);
		struct sakura_tab *selected_tab = NULL;
		if (selected_page < 0 && selected_terminal >= 0 && selected_terminal < restored)
			selected_page = selected_terminal;
		if (selected_page >= 0)
			selected_tab = sakura_tab_at_page(selected_page);
		if (selected_tab != NULL && sakura_tab_is_in_active_scope(selected_tab))
			sakura_select_tab(selected_tab, FALSE);
		else
			sakura_select_scope_default();
	}
	if (restored == 0)
		sakura_tab_bar_refresh();
	return restored > 0;
}
SakuraSessionSnapshot *
sakura_workspace_snapshot_new(void)
{
	SakuraSessionSnapshot *snapshot;
	GPtrArray *group_ids, *group_parents, *group_titles, *terminals;
	gint selected_page, selected_terminal = -1;
	guint index;

	sakura_sidebar_sync_parents();
	snapshot = sakura_session_snapshot_new();
	group_ids = g_ptr_array_new_with_free_func(g_free);
	group_parents = g_ptr_array_new_with_free_func(g_free);
	group_titles = g_ptr_array_new_with_free_func(g_free);
	terminals = g_ptr_array_new();
	sakura_sidebar_collect_groups(GTK_TREE_MODEL(sakura.sidebar_model), NULL,
	                              group_ids, group_parents, group_titles);
	sakura_sidebar_collect_terminals(GTK_TREE_MODEL(sakura.sidebar_model), NULL, terminals);

	selected_page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	if (selected_page >= 0) {
		SakuraTab *selected_tab = sakura_tab_at_page(selected_page);
		for (index = 0; index < terminals->len; index++) {
			SakuraSidebarNode *node = g_ptr_array_index(terminals, index);
			if (node->tab == selected_tab) {
				selected_terminal = (gint)index;
				break;
			}
		}
		if (selected_tab != NULL && selected_tab->terminal_id != NULL)
			snapshot->selected_terminal_id = g_strdup(selected_tab->terminal_id);
	}

	snapshot->selected_terminal = selected_terminal;
	g_free(snapshot->active_group_id);
	snapshot->active_group_id = g_strdup(sakura.active_group_scope != NULL &&
	                                    sakura.active_group_scope->id != NULL
	                                    ? sakura.active_group_scope->id : "root");
	snapshot->sidebar_visible = sakura.sidebar_visible;
	snapshot->sidebar_width = sakura.sidebar_paned != NULL
	                         ? gtk_paned_get_position(GTK_PANED(sakura.sidebar_paned))
	                         : sakura.sidebar_width;

	for (index = 0; index < group_ids->len; index++) {
		SakuraSessionGroupRecord *group = g_new0(SakuraSessionGroupRecord, 1);
		group->id = g_strdup(g_ptr_array_index(group_ids, index));
		group->parent_id = g_strdup(g_ptr_array_index(group_parents, index));
		group->title = g_strdup(g_ptr_array_index(group_titles, index));
		g_ptr_array_add(snapshot->groups, group);
	}

	for (index = 0; index < terminals->len; index++) {
		SakuraSidebarNode *node = g_ptr_array_index(terminals, index);
		SakuraTab *tab = node->tab;
		SakuraSessionTabRecord *record = g_new0(SakuraSessionTabRecord, 1);
		const gchar *title = gtk_label_get_text(GTK_LABEL(tab->label));

		record->parent_id = g_strdup(node->parent != NULL ? node->parent->id : "root");
		record->cwd = g_strdup(tab->cwd != NULL ? tab->cwd : "");
		record->terminal_id = g_strdup(tab->terminal_id != NULL ? tab->terminal_id : "");
		record->kind = tab->kind;
		record->tool_id = tab->kind == SAKURA_TAB_TOOL
		               ? g_strdup(sakura_tool_id(tab->tool)) : NULL;
		record->tool_target = tab->kind == SAKURA_TAB_TOOL
		                   ? g_strdup(tab->tool_target) : NULL;
		record->codex_session_id = tab->kind == SAKURA_TAB_CODEX
		                        ? g_strdup(tab->codex_session_id) : NULL;
		record->codex_session_name = tab->kind == SAKURA_TAB_CODEX
		                          ? g_strdup(tab->codex_session_name) : NULL;
		record->title_set_by_user = tab->label_set_byuser;
		record->title = tab->label_set_byuser ? g_strdup(title != NULL ? title : "") : NULL;
		g_ptr_array_add(snapshot->tabs, record);
	}

	g_ptr_array_free(group_ids, TRUE);
	g_ptr_array_free(group_parents, TRUE);
	g_ptr_array_free(group_titles, TRUE);
	g_ptr_array_free(terminals, TRUE);
	return snapshot;
}


void
sakura_sidebar_model_reordered_cb (GtkTreeModel *model, GtkTreePath *path,
                                   GtkTreeIter *iter, gint *new_order, void *data)
{
	sakura_sidebar_save_groups();
	sakura_tab_bar_refresh();
	sakura_session_mark_dirty();
}


void
sakura_sidebar_toggle_cb (GtkWidget *widget, void *data)
{
	sakura.sidebar_visible = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget));
	if (sakura.sidebar != NULL)
		gtk_widget_set_visible(sakura.sidebar, sakura.sidebar_visible);
	sakura_workspace_set_boolean("sidebar_visible", sakura.sidebar_visible);
	sakura_session_mark_dirty();
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
void
sakura_sidebar_init (gboolean restore_session)
{
	GtkWidget *sidebar_box, *toolbar, *title, *tools_button, *open_here_button,
	          *new_terminal, *new_group;
	GtkWidget *tools_menu, *tool_item;
	GtkWidget *tab_shell, *scope_label, *tab_scrolled, *tab_bar, *tab_new;
	GtkWidget *empty_state, *empty_label, *empty_new;
	GtkWidget *scrolled;
	GtkCellRenderer *icon_renderer, *status_renderer, *spinner_renderer, *text_renderer;
	GtkTreeViewColumn *column;
	gchar **group_ids, **group_parents, **group_titles;
	gsize n_ids = 0, n_parents = 0, n_titles = 0;
	gsize i, n_groups;
	gboolean session_has_groups;

	sakura.sidebar_model = gtk_tree_store_new(SAKURA_SIDEBAR_N_COLUMNS,
	                                         G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
	                                         G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN,
	                                         G_TYPE_BOOLEAN,
	                                         G_TYPE_UINT,
	                                         G_TYPE_STRING,
	                                         G_TYPE_POINTER);
	sakura.sidebar_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(sakura.sidebar_model));
	sakura.sidebar_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(sakura.sidebar_tree));
	gtk_tree_selection_set_mode(sakura.sidebar_selection, GTK_SELECTION_SINGLE);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(sakura.sidebar_tree), FALSE);
	gtk_tree_view_set_enable_tree_lines(GTK_TREE_VIEW(sakura.sidebar_tree), TRUE);
	gtk_tree_view_set_activate_on_single_click(GTK_TREE_VIEW(sakura.sidebar_tree), TRUE);
	gtk_tree_view_set_reorderable(GTK_TREE_VIEW(sakura.sidebar_tree), TRUE);
	gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(sakura.sidebar_tree), SAKURA_SIDEBAR_COLUMN_TOOLTIP);
	gtk_widget_set_name(sakura.sidebar_tree, "terminal-sidebar");

	icon_renderer = gtk_cell_renderer_pixbuf_new();
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
	gtk_tree_view_column_pack_start(column, icon_renderer, FALSE);
	gtk_tree_view_column_add_attribute(column, icon_renderer, "icon-name", SAKURA_SIDEBAR_COLUMN_ICON);
	gtk_tree_view_column_pack_start(column, status_renderer, FALSE);
	gtk_tree_view_column_add_attribute(column, status_renderer, "markup", SAKURA_SIDEBAR_COLUMN_STATUS_MARKUP);
	gtk_tree_view_column_add_attribute(column, status_renderer, "visible",
	                                  SAKURA_SIDEBAR_COLUMN_STATUS_MARKER_VISIBLE);
	gtk_tree_view_column_pack_start(column, spinner_renderer, FALSE);
	gtk_tree_view_column_add_attribute(column, spinner_renderer, "active", SAKURA_SIDEBAR_COLUMN_STATUS_ACTIVE);
	gtk_tree_view_column_add_attribute(column, spinner_renderer, "visible", SAKURA_SIDEBAR_COLUMN_STATUS_ACTIVE);
	gtk_tree_view_column_add_attribute(column, spinner_renderer, "pulse", SAKURA_SIDEBAR_COLUMN_STATUS_PULSE);
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
	new_terminal = gtk_button_new_from_icon_name("utilities-terminal", GTK_ICON_SIZE_MENU);
	gtk_button_set_relief(GTK_BUTTON(new_terminal), GTK_RELIEF_NONE);
	gtk_widget_set_tooltip_text(new_terminal, _("New terminal"));
	gtk_box_pack_start(GTK_BOX(toolbar), new_terminal, FALSE, FALSE, 0);
	new_group = gtk_button_new_from_icon_name("folder-new", GTK_ICON_SIZE_MENU);
	gtk_button_set_relief(GTK_BUTTON(new_group), GTK_RELIEF_NONE);
	gtk_widget_set_tooltip_text(new_group, _("New group"));
	gtk_box_pack_start(GTK_BOX(toolbar), new_group, FALSE, FALSE, 0);

	sidebar_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(sidebar_box), toolbar, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(sidebar_box), scrolled, TRUE, TRUE, 0);
	sakura.sidebar = sidebar_box;

	sakura.sidebar_root = g_new0(struct sakura_sidebar_node, 1);
	sakura.sidebar_root->type = SAKURA_SIDEBAR_GROUP;
	sakura.sidebar_root->id = g_strdup("root");
	sakura.sidebar_root->title = g_strdup(_("All terminals"));
	sakura.sidebar_next_group_id = 1;
	sakura.sidebar_groups = g_list_append(sakura.sidebar_groups, sakura.sidebar_root);
	sakura_sidebar_insert_node(sakura.sidebar_root);

	session_has_groups = restore_session &&
		sakura.session_snapshot != NULL;
	if (session_has_groups) {
		n_groups = sakura.session_snapshot->groups->len;
		group_ids = g_new0(gchar *, n_groups + 1);
		group_parents = g_new0(gchar *, n_groups + 1);
		group_titles = g_new0(gchar *, n_groups + 1);
		for (i = 0; i < n_groups; i++) {
			SakuraSessionGroupRecord *record =
				g_ptr_array_index(sakura.session_snapshot->groups, i);
			group_ids[i] = g_strdup(record->id);
			group_parents[i] = g_strdup(record->parent_id);
			group_titles[i] = g_strdup(record->title);
		}
	} else {
		group_ids = g_key_file_get_string_list(sakura.cfg, SAKURA_CONFIG_GROUP, "sidebar_group_ids", &n_ids, NULL);
		group_parents = g_key_file_get_string_list(sakura.cfg, SAKURA_CONFIG_GROUP, "sidebar_group_parents", &n_parents, NULL);
		group_titles = g_key_file_get_string_list(sakura.cfg, SAKURA_CONFIG_GROUP, "sidebar_group_titles", &n_titles, NULL);
		n_groups = MIN(n_ids, MIN(n_parents, n_titles));
	}
	for (i = 0; i < n_groups; i++) {
		struct sakura_sidebar_node *node, *parent;
		gchar *end = NULL;
		guint id_number;

		parent = sakura_sidebar_find_group_by_id(group_parents[i]);
		if (parent == NULL)
			parent = sakura.sidebar_root;
		node = g_new0(struct sakura_sidebar_node, 1);
		node->type = SAKURA_SIDEBAR_GROUP;
		node->id = g_strdup(group_ids[i]);
		node->title = g_strdup(group_titles[i]);
		node->parent = parent;
		sakura.sidebar_groups = g_list_append(sakura.sidebar_groups, node);
		sakura_sidebar_insert_node(node);

		if (g_str_has_prefix(node->id, "group-")) {
			id_number = (guint)g_ascii_strtoull(node->id + strlen("group-"), &end, 10);
			if (end != node->id + strlen("group-") && id_number >= sakura.sidebar_next_group_id)
				sakura.sidebar_next_group_id = id_number + 1;
		}
	}
	g_strfreev(group_ids);
	g_strfreev(group_parents);
	g_strfreev(group_titles);

	gtk_tree_view_expand_all(GTK_TREE_VIEW(sakura.sidebar_tree));

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
	sakura.active_group_scope = sakura.sidebar_root;
	gtk_box_pack_start(GTK_BOX(sakura.content_box), tab_shell, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(sakura.content_box), sakura.notebook, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(sakura.content_box), empty_state, TRUE, TRUE, 0);
	gtk_widget_hide(empty_state);
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(sakura.notebook), FALSE);

	if (restore_session && sakura.session_snapshot != NULL) {
		const gchar *active_group_id = sakura.session_snapshot->active_group_id;
		struct sakura_sidebar_node *saved_scope = sakura_sidebar_find_group_by_id(active_group_id);
		if (saved_scope != NULL)
			sakura.active_group_scope = saved_scope;
	}

	g_signal_connect(new_terminal, "clicked", G_CALLBACK(sakura_new_tab_cb), NULL);
	g_signal_connect(new_group, "clicked", G_CALLBACK(sakura_sidebar_new_group_cb), NULL);
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
	sakura.sidebar_spinner_source_id = g_timeout_add(100,
	                                                 sakura_sidebar_spinner_pulse_cb,
	                                                 NULL);
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
	SakuraTab *tab;

	(void)widget;
	(void)widget_page;
	(void)data;
	/* Don't use gtk_notebook_get_current_page here; GTK still reports the
	 * previous page while this callback is dispatched. */
	tab = sakura_tab_at_page(page_num);
	if (tab == NULL)
		return;
	sakura.active_tab = tab;
	sakura_remember_current_scope_tab(tab);
	sakura_tab_clear_attention(tab);
	/* A notebook switch can be triggered while a sidebar click is still being
	 * dispatched. Queue the tree update so the original click's target wins
	 * over any intermediate scope/fallback switch. */
	sakura_sidebar_queue_select_node(tab->sidebar_node);
	sakura_codex_sync_name(tab);

	/* Update the window title when a new tab is selected, but don't when a user
	 * supplied a static title. */
	if (!sakura.main_title && tab->label != NULL) {
		const gchar *title = gtk_label_get_text(GTK_LABEL(tab->label));
		if (title != NULL && title[0] != '\0')
			sakura_set_window_title(title);
	}
	sakura_tab_bar_refresh();
}


void
sakura_page_removed_cb (GtkWidget *widget, void *data)
{
	(void)widget;
	(void)data;
	if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook)) == 1)
		/* If the first tab is disabled, recalculate the terminal size. */
		sakura_set_size();
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



gboolean
sakura_sidebar_spinner_pulse_cb(gpointer data)
{
	GtkTreeIter iter;
	GtkTreeModel *model;

	(void)data;
	if (sakura.session_shutting_down || sakura.sidebar_model == NULL) {
		sakura.sidebar_spinner_source_id = 0;
		return G_SOURCE_REMOVE;
	}

	sakura.sidebar_spinner_pulse++;
	model = GTK_TREE_MODEL(sakura.sidebar_model);
	if (gtk_tree_model_get_iter_first(model, &iter)) {
		do {
			gboolean active;

			gtk_tree_model_get(model, &iter,
			                   SAKURA_SIDEBAR_COLUMN_STATUS_ACTIVE, &active,
			                   -1);
			if (active)
				gtk_tree_store_set(sakura.sidebar_model, &iter,
				                   SAKURA_SIDEBAR_COLUMN_STATUS_PULSE,
				                   sakura.sidebar_spinner_pulse,
				                   -1);
		} while (gtk_tree_model_iter_next(model, &iter));
	}

	return G_SOURCE_CONTINUE;
}

void
sakura_sidebar_set_node_row(SakuraSidebarNode *node, GtkTreeIter *iter)
{
	const gchar *icon_name;
	const gchar *status_label, *status_color, *status_symbol;
	GtkIconTheme *icon_theme;
	gchar *escaped_title, *escaped_subtitle, *markup, *status_markup = NULL;
	gchar *tooltip_markup = NULL;
	gboolean status_running, status_marker_visible;

	icon_name = "utilities-terminal";
	if (node->type == SAKURA_SIDEBAR_GROUP) {
		icon_name = "folder";
	} else if (node->tab != NULL && node->tab->kind == SAKURA_TAB_CODEX) {
		/* Keep a stock-terminal fallback for uninstalled/source-tree runs. */
		icon_theme = gtk_icon_theme_get_default();
		if (icon_theme != NULL && gtk_icon_theme_has_icon(icon_theme, CODEX_ICON_NAME))
			icon_name = CODEX_ICON_NAME;
	} else if (node->tab != NULL && node->tab->kind == SAKURA_TAB_TOOL) {
		icon_theme = gtk_icon_theme_get_default();
		if (icon_theme != NULL &&
		    gtk_icon_theme_has_icon(icon_theme, sakura_tool_icon_name(node->tab->tool)))
			icon_name = sakura_tool_icon_name(node->tab->tool);
	}

	escaped_title = g_markup_escape_text(node->title != NULL ? node->title : "", -1);
	escaped_subtitle = g_markup_escape_text(node->subtitle != NULL ? node->subtitle : "", -1);
	status_label = node->tab != NULL ? sakura_tab_status_label(node->tab->status) : NULL;
	status_running = node->tab != NULL && node->tab->status == SAKURA_TAB_STATUS_RUNNING;
	status_color = node->tab != NULL ? sakura_tab_status_color(node->tab->status) : NULL;
	status_symbol = node->tab != NULL ? sakura_tab_status_symbol(node->tab->status) : NULL;
	if (!status_running && status_color != NULL && status_symbol != NULL)
		status_markup = g_strdup_printf("<span foreground=\"%s\">%s</span>",
		                                status_color, status_symbol);
	status_marker_visible = status_markup != NULL;
	if (node->subtitle != NULL && node->subtitle[0] != '\0')
		markup = g_strdup_printf("%s\n<small>%s</small>", escaped_title, escaped_subtitle);
	else
		markup = g_strdup(escaped_title);
	{
		const gchar *base_tooltip = node->tooltip != NULL ? node->tooltip : node->title;
		base_tooltip = base_tooltip != NULL ? base_tooltip : "";
		if (status_label != NULL)
			tooltip_markup = g_markup_printf_escaped("%s\n%s",
			                                        base_tooltip, status_label);
		else
			tooltip_markup = g_markup_escape_text(base_tooltip, -1);
	}

	gtk_tree_store_set(sakura.sidebar_model, iter,
	                   SAKURA_SIDEBAR_COLUMN_TITLE, node->title,
	                   SAKURA_SIDEBAR_COLUMN_SUBTITLE, node->subtitle,
	                   SAKURA_SIDEBAR_COLUMN_MARKUP, markup,
	                   SAKURA_SIDEBAR_COLUMN_ICON, icon_name,
	                   SAKURA_SIDEBAR_COLUMN_STATUS_MARKUP, status_markup,
	                   SAKURA_SIDEBAR_COLUMN_STATUS_MARKER_VISIBLE,
	                   status_marker_visible,
	                   SAKURA_SIDEBAR_COLUMN_STATUS_ACTIVE, status_running,
	                   SAKURA_SIDEBAR_COLUMN_STATUS_PULSE,
	                   status_running ? sakura.sidebar_spinner_pulse : 0,
	                   SAKURA_SIDEBAR_COLUMN_TOOLTIP, tooltip_markup,
	                   SAKURA_SIDEBAR_COLUMN_NODE, node,
	                   -1);

	g_free(escaped_title);
	g_free(escaped_subtitle);
	g_free(markup);
	g_free(status_markup);
	g_free(tooltip_markup);
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

	if (node->row != NULL)
		gtk_tree_row_reference_free(node->row);
	g_free(node->id);
	g_free(node->title);
	g_free(node->subtitle);
	g_free(node->tooltip);
	g_free(node->last_terminal_id);
	g_free(node);
}


void
sakura_sidebar_update_attention_count(void)
{
	guint count = 0;
	guint index;
	gchar *label;

	if (sakura.sidebar_title == NULL || sakura.tabs == NULL)
		return;

	for (index = 0; index < sakura.tabs->len; index++) {
		SakuraTab *tab = g_ptr_array_index(sakura.tabs, index);
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


void
sakura_sidebar_insert_node(SakuraSidebarNode *node)
{
	GtkTreeIter iter, parent_iter;
	GtkTreeIter *parent = NULL;
	GtkTreePath *path;

	if (node->parent != NULL && sakura_sidebar_get_iter(node->parent, &parent_iter))
		parent = &parent_iter;

	gtk_tree_store_append(sakura.sidebar_model, &iter, parent);
	sakura_sidebar_set_node_row(node, &iter);

	path = gtk_tree_model_get_path(GTK_TREE_MODEL(sakura.sidebar_model), &iter);
	node->row = gtk_tree_row_reference_new(GTK_TREE_MODEL(sakura.sidebar_model), path);
	gtk_tree_path_free(path);

	if (node->parent != NULL && sakura_sidebar_get_iter(node->parent, &parent_iter)) {
		path = gtk_tree_model_get_path(GTK_TREE_MODEL(sakura.sidebar_model), &parent_iter);
		gtk_tree_view_expand_row(GTK_TREE_VIEW(sakura.sidebar_tree), path, FALSE);
		gtk_tree_path_free(path);
	}
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


SakuraSidebarNode *
sakura_sidebar_selected_group(void)
{
	SakuraSidebarNode *node = sakura_sidebar_selected_node();

	if (node == NULL)
		return sakura.sidebar_root;
	if (node->type == SAKURA_SIDEBAR_GROUP)
		return node;
	if (node->parent != NULL)
		return node->parent;
	return sakura.sidebar_root;
}


void
sakura_sidebar_update_tab(SakuraTab *tab)
{
	GtkTreeIter iter;
	SakuraSidebarNode *node;
	gchar *title, *subtitle, *tooltip, *display_path;
	gint page;

	if (tab == NULL || tab->sidebar_node == NULL ||
	    !sakura_sidebar_get_iter(tab->sidebar_node, &iter))
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
		subtitle = g_strdup_printf("%s · %s", tab->host, display_path);
	else if (tab->host != NULL)
		subtitle = g_strdup(tab->host);
	else if (display_path != NULL)
		subtitle = g_strdup(display_path);
	else
		subtitle = g_strdup("");

	if (tab->raw_title != NULL && tab->raw_title[0] != '\0' && subtitle[0] != '\0')
		tooltip = g_strdup_printf("%s\n%s", tab->raw_title, subtitle);
	else if (tab->raw_title != NULL && tab->raw_title[0] != '\0')
		tooltip = g_strdup(tab->raw_title);
	else
		tooltip = g_strdup(subtitle);

	g_free(node->title);
	g_free(node->subtitle);
	g_free(node->tooltip);
	node->title = title;
	node->subtitle = subtitle;
	node->tooltip = tooltip;
	sakura_sidebar_set_node_row(node, &iter);
	sakura_tab_bar_update_tab(tab);
	g_free(display_path);
	sakura_session_mark_dirty();
}


void
sakura_sidebar_remove_tab(SakuraTab *tab)
{
	GtkTreeIter iter;

	if (tab == NULL || tab->sidebar_node == NULL)
		return;

	sakura_tab_bar_remove_tab(tab);
	if (tab->sidebar_node->parent != NULL &&
	    g_strcmp0(tab->sidebar_node->parent->last_terminal_id, tab->terminal_id) == 0)
		g_clear_pointer(&tab->sidebar_node->parent->last_terminal_id, g_free);
	if (sakura_sidebar_get_iter(tab->sidebar_node, &iter))
		gtk_tree_store_remove(sakura.sidebar_model, &iter);
	sakura_sidebar_free_node(tab->sidebar_node);
	tab->sidebar_node = NULL;
	sakura_sidebar_update_attention_count();
}


static void
sakura_sidebar_select_node_now(SakuraSidebarNode *node)
{
	GtkTreePath *path;

	if (sakura.sidebar_selection == NULL || node == NULL || node->row == NULL)
		return;

	path = gtk_tree_row_reference_get_path(node->row);
	if (path == NULL)
		return;

	sakura.sidebar_syncing = TRUE;
	gtk_tree_selection_select_path(sakura.sidebar_selection, path);
	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(sakura.sidebar_tree), path, NULL, FALSE, 0, 0);
	sakura.sidebar_syncing = FALSE;
	gtk_tree_path_free(path);
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
sakura_sidebar_queue_select_node(SakuraSidebarNode *node)
{
	GtkTreePath *path;

	if (sakura.sidebar_selection == NULL || node == NULL || node->row == NULL)
		return;

	path = gtk_tree_row_reference_get_path(node->row);
	if (path == NULL)
		return;

	sakura_sidebar_cancel_pending_selection();
	sakura.sidebar_pending_selection = gtk_tree_row_reference_new(
		GTK_TREE_MODEL(sakura.sidebar_model), path);
	sakura.sidebar_selection_source_id = g_idle_add(sakura_sidebar_select_pending_cb, NULL);
	gtk_tree_path_free(path);
}
