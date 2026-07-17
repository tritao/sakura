#include <gtk/gtk.h>

#include "src/sakura-private.h"

static SakuraPage *
test_page_new(const gchar *page_id, const gchar *terminal_id)
{
	SakuraPage *page = sakura_page_new(page_id);
	SakuraTab *tab = g_new0(SakuraTab, 1);

	page->container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	tab->terminal_id = g_strdup(terminal_id);
	tab->hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	tab->label = gtk_label_new("Terminal");
	g_assert_nonnull(sakura_layout_leaf_new(page, tab));
	tab->layout_leaf->widget = tab->hbox;
	gtk_box_pack_start(GTK_BOX(page->container), tab->hbox, TRUE, TRUE, 0);
	gtk_widget_show_all(page->container);
	page->tab_bar_tab = tab;
	return page;
}


static void
test_sidebar_add_page(SakuraPage *page)
{
	SakuraSidebarNode *node = g_new0(SakuraSidebarNode, 1);

	node->type = SAKURA_SIDEBAR_PAGE;
	node->id = g_strdup(page->id);
	node->title = g_strdup("Terminal");
	node->subtitle = g_strdup("");
	node->tooltip = g_strdup("Terminal");
	node->parent = sakura.sidebar_root;
	node->page = page;
	page->group = sakura.root_group;
	page->sidebar_node = node;
	sakura_sidebar_insert_node(node);
}


static void
test_sidebar_add_tab(SakuraTab *tab)
{
	SakuraSidebarNode *node;

	if (tab == NULL || tab->page == NULL || tab->sidebar_node != NULL)
		return;
	node = g_new0(SakuraSidebarNode, 1);
	node->type = SAKURA_SIDEBAR_TERMINAL;
	node->id = g_strdup(tab->terminal_id);
	node->title = g_strdup("Terminal");
	node->subtitle = g_strdup("");
	node->tooltip = g_strdup("Terminal");
	node->parent = tab->page->sidebar_node;
	node->tab = tab;
	tab->sidebar_node = node;
	sakura_sidebar_insert_node(node);
}


static SakuraSidebarNode *
test_sidebar_add_group(const gchar *id, const gchar *title,
                       SakuraSidebarNode *parent)
{
	SakuraGroup *model_group = g_new0(SakuraGroup, 1);
	SakuraSidebarNode *node = g_new0(SakuraSidebarNode, 1);

	model_group->id = g_strdup(id);
	model_group->title = g_strdup(title);
	model_group->parent = parent != NULL && parent->group != NULL
	                   ? parent->group : sakura.root_group;
	node->type = SAKURA_SIDEBAR_GROUP;
	node->id = g_strdup(id);
	node->title = g_strdup(title);
	node->subtitle = g_strdup("");
	node->tooltip = g_strdup(title);
	node->parent = parent;
	node->group = model_group;
	model_group->sidebar_node = node;
	sakura.groups = g_list_append(sakura.groups, model_group);
	sakura_sidebar_insert_node(node);
	return node;
}


static SakuraSidebarNode *
test_sidebar_project_group(SakuraGroup *model_group, SakuraSidebarNode *parent)
{
	SakuraSidebarNode *node = g_new0(SakuraSidebarNode, 1);

	node->type = SAKURA_SIDEBAR_GROUP;
	node->id = g_strdup(model_group->id);
	node->title = g_strdup(model_group->title);
	node->subtitle = g_strdup("");
	node->tooltip = g_strdup(model_group->title);
	node->parent = parent;
	node->group = model_group;
	model_group->sidebar_node = node;
	sakura_sidebar_insert_node(node);
	return node;
}


static void
test_sidebar_remove_group(SakuraSidebarNode *group)
{
	SakuraGroup *model_group;
	GtkTreeIter iter;

	if (group == NULL)
		return;
	model_group = group->group;
	if (sakura_sidebar_get_iter(group, &iter))
		gtk_tree_store_remove(sakura.sidebar_model, &iter);
	sakura_sidebar_free_node(group);
	sakura.groups = g_list_remove(sakura.groups, model_group);
	sakura_group_free(model_group);
}


static void
setup_sidebar_fixture(void)
{
	SakuraSidebarNode *root;
	GType columns[] = {
		G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
		G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_BOOLEAN,
		G_TYPE_BOOLEAN, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_POINTER
	};
	guint index, pane_index;

	sakura.sidebar_model = gtk_tree_store_newv(G_N_ELEMENTS(columns), columns);
	sakura.sidebar_tree = gtk_tree_view_new_with_model(
		GTK_TREE_MODEL(sakura.sidebar_model));
	sakura.sidebar_selection = gtk_tree_view_get_selection(
		GTK_TREE_VIEW(sakura.sidebar_tree));
	root = g_new0(SakuraSidebarNode, 1);
	root->type = SAKURA_SIDEBAR_GROUP;
	root->id = g_strdup("root");
	root->title = g_strdup("All terminals");
	root->subtitle = g_strdup("");
	root->tooltip = g_strdup("All terminals");
	sakura.sidebar_root = root;
	sakura.root_group = g_new0(SakuraGroup, 1);
	sakura.root_group->id = g_strdup("root");
	sakura.root_group->title = g_strdup("All terminals");
	sakura.root_group->sidebar_node = root;
	root->group = sakura.root_group;
	sakura.groups = g_list_append(NULL, sakura.root_group);
	sakura.active_group = sakura.root_group;
	sakura.active_group_scope = root;
	sakura_sidebar_insert_node(root);

	for (index = 0; index < sakura.pages->len; index++) {
		SakuraPage *page = g_ptr_array_index(sakura.pages, index);
		test_sidebar_add_page(page);
		for (pane_index = 0; pane_index < page->panes->len; pane_index++)
			test_sidebar_add_tab(g_ptr_array_index(page->panes, pane_index));
	}
}


static void
teardown_sidebar_fixture(void)
{
	guint index, pane_index;

	if (sakura.pages != NULL) {
		for (index = 0; index < sakura.pages->len; index++) {
			SakuraPage *page = g_ptr_array_index(sakura.pages, index);
			for (pane_index = 0; page->panes != NULL &&
			     pane_index < page->panes->len; pane_index++) {
				SakuraTab *tab = g_ptr_array_index(page->panes, pane_index);
				if (tab != NULL && tab->sidebar_node != NULL) {
					sakura_sidebar_free_node(tab->sidebar_node);
					tab->sidebar_node = NULL;
				}
			}
			if (page->sidebar_node != NULL) {
				sakura_sidebar_free_node(page->sidebar_node);
				page->sidebar_node = NULL;
			}
		}
	}
	if (sakura.sidebar_root != NULL) {
		sakura_sidebar_free_node(sakura.sidebar_root);
		sakura.sidebar_root = NULL;
	}
	g_list_free_full(sakura.groups, (GDestroyNotify)sakura_group_free);
	sakura.groups = NULL;
	sakura.root_group = NULL;
	if (sakura.sidebar_tree != NULL)
		gtk_widget_destroy(sakura.sidebar_tree);
	if (sakura.sidebar_model != NULL)
		g_object_unref(sakura.sidebar_model);
	sakura.sidebar_tree = NULL;
	sakura.sidebar_model = NULL;
	sakura.sidebar_selection = NULL;
	sakura.active_group_scope = NULL;
	sakura.active_group = NULL;
}


static SakuraSessionLayoutRecord *
test_snapshot_layout_record(const SakuraSessionSnapshot *snapshot,
                            const gchar *id)
{
	guint index;

	for (index = 0; snapshot != NULL && index < snapshot->layouts->len; index++) {
		SakuraSessionLayoutRecord *record = g_ptr_array_index(snapshot->layouts, index);
		if (g_strcmp0(record->id, id) == 0)
			return record;
	}
	return NULL;
}


static SakuraSessionTabRecord *
test_snapshot_tab_record(const SakuraSessionSnapshot *snapshot,
                         const gchar *terminal_id)
{
	guint index;

	for (index = 0; snapshot != NULL && index < snapshot->tabs->len; index++) {
		SakuraSessionTabRecord *record = g_ptr_array_index(snapshot->tabs, index);
		if (g_strcmp0(record->terminal_id, terminal_id) == 0)
			return record;
	}
	return NULL;
}


static SakuraSessionGroupRecord *
test_snapshot_group_record(const SakuraSessionSnapshot *snapshot,
                           const gchar *group_id)
{
	guint index;

	for (index = 0; snapshot != NULL && index < snapshot->groups->len; index++) {
		SakuraSessionGroupRecord *record = g_ptr_array_index(snapshot->groups, index);
		if (g_strcmp0(record->id, group_id) == 0)
			return record;
	}
	return NULL;
}


static SakuraSessionLayoutRecord *
test_snapshot_leftmost_layout(const SakuraSessionSnapshot *snapshot,
                              SakuraSessionLayoutRecord *record)
{
	while (record != NULL && g_strcmp0(record->type, "split") == 0)
		record = test_snapshot_layout_record(snapshot, record->first_id);
	return record;
}


static gchar *
test_live_layout_signature(const SakuraLayoutNode *node)
{
	gchar *first, *second, *signature;

	if (node == NULL)
		return g_strdup("missing");
	if (node->kind == SAKURA_LAYOUT_LEAF)
		return g_strdup_printf("leaf:%s", node->data.leaf.tab->terminal_id);
	first = test_live_layout_signature(node->data.split.first);
	second = test_live_layout_signature(node->data.split.second);
	signature = g_strdup_printf("split:%d(%s,%s)",
	                            node->data.split.direction, first, second);
	g_free(first);
	g_free(second);
	return signature;
}


static gchar *
test_snapshot_layout_signature(const SakuraSessionSnapshot *snapshot,
                               SakuraSessionLayoutRecord *record)
{
	gchar *first, *second, *signature;

	if (record == NULL)
		return g_strdup("missing");
	if (g_strcmp0(record->type, "leaf") == 0)
		return g_strdup_printf("leaf:%s", record->terminal_id);
	first = test_snapshot_layout_signature(
		snapshot, test_snapshot_layout_record(snapshot, record->first_id));
	second = test_snapshot_layout_signature(
		snapshot, test_snapshot_layout_record(snapshot, record->second_id));
	signature = g_strdup_printf("split:%d(%s,%s)", record->direction, first, second);
	g_free(first);
	g_free(second);
	return signature;
}


static SakuraTab *
test_page_pane_by_id(SakuraPage *page, const gchar *terminal_id)
{
	guint index;

	for (index = 0; page != NULL && index < page->panes->len; index++) {
		SakuraTab *tab = g_ptr_array_index(page->panes, index);
		if (g_strcmp0(tab->terminal_id, terminal_id) == 0)
			return tab;
	}
	return NULL;
}


static SakuraLayoutNode *
test_restore_layout_subtree(SakuraPage *page,
                            const SakuraSessionSnapshot *snapshot,
                            SakuraSessionLayoutRecord *record,
                            SakuraTab *anchor)
{
	SakuraSessionLayoutRecord *first_record, *second_record, *second_leaf;
	SakuraSessionTabRecord *tab_record;
	SakuraTab *second_tab;
	SakuraLayoutNode *first_node, *split;

	if (record == NULL || g_strcmp0(record->type, "leaf") == 0)
		return anchor->layout_leaf;
	first_record = test_snapshot_layout_record(snapshot, record->first_id);
	second_record = test_snapshot_layout_record(snapshot, record->second_id);
	first_node = test_restore_layout_subtree(page, snapshot, first_record, anchor);
	second_leaf = test_snapshot_leftmost_layout(snapshot, second_record);
	tab_record = test_snapshot_tab_record(snapshot, second_leaf->terminal_id);
	second_tab = g_new0(SakuraTab, 1);
	second_tab->terminal_id = g_strdup(tab_record->terminal_id);
	second_tab->hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	second_tab->label = gtk_label_new("Terminal");
	g_assert_true(sakura_layout_split_node_widgets(first_node, record->direction,
	                                               second_tab));
	split = second_tab->layout_leaf->parent;
	if (g_strcmp0(second_record->type, "split") == 0)
		g_assert_true(test_restore_layout_subtree(page, snapshot, second_record,
		                                          second_tab) != NULL);
	return split;
}


static void
setup_workspace_from_snapshot(const SakuraSessionSnapshot *snapshot)
{
	guint index;

	memset(&sakura, 0, sizeof(sakura));
	sakura.notebook = gtk_notebook_new();
	sakura.pages = g_ptr_array_new();
	sakura.tabs = g_ptr_array_new();
	for (index = 0; index < snapshot->pages->len; index++) {
		SakuraSessionPageRecord *page_record = g_ptr_array_index(snapshot->pages, index);
		SakuraSessionLayoutRecord *root_record = test_snapshot_layout_record(
			snapshot, page_record->root_layout_id);
		SakuraSessionLayoutRecord *root_leaf = test_snapshot_leftmost_layout(
			snapshot, root_record);
		SakuraPage *page = test_page_new(page_record->id, root_leaf->terminal_id);
		SakuraTab *tab = page->tab_bar_tab;

		page->title = g_strdup(page_record->title);
		page->title_set_by_user = page_record->title_set_by_user;
		g_ptr_array_add(sakura.pages, page);
		g_ptr_array_add(sakura.tabs, tab);
		gtk_notebook_append_page(GTK_NOTEBOOK(sakura.notebook), page->container, NULL);
		if (g_strcmp0(root_record->type, "split") == 0)
			test_restore_layout_subtree(page, snapshot, root_record, tab);
		if (page_record->active_terminal_id != NULL) {
			SakuraTab *active = test_page_pane_by_id(page,
			                                         page_record->active_terminal_id);
			if (active != NULL)
				page->active_tab = active;
		}
	}
	gtk_widget_show_all(sakura.notebook);
	if (snapshot->selected_page_id != NULL) {
		for (index = 0; index < sakura.pages->len; index++) {
			SakuraPage *page = g_ptr_array_index(sakura.pages, index);
			if (g_strcmp0(page->id, snapshot->selected_page_id) == 0) {
				gtk_notebook_set_current_page(GTK_NOTEBOOK(sakura.notebook), index);
				sakura.active_page = page;
				sakura.active_tab = snapshot->selected_terminal_id != NULL
				                ? test_page_pane_by_id(page,
				                                      snapshot->selected_terminal_id)
				                : page->active_tab;
				break;
			}
		}
	}
	if (sakura.active_page == NULL && sakura.pages->len > 0) {
		sakura.active_page = g_ptr_array_index(sakura.pages, 0);
		sakura.active_tab = sakura.active_page->active_tab;
		gtk_notebook_set_current_page(GTK_NOTEBOOK(sakura.notebook), 0);
	}
}


static void
test_page_free(SakuraPage *page)
{
	GPtrArray *tabs;
	guint index;

	if (page == NULL)
		return;
	tabs = g_ptr_array_new();
	for (index = 0; page->panes != NULL && index < page->panes->len; index++)
		g_ptr_array_add(tabs, g_ptr_array_index(page->panes, index));
	sakura_page_free(page);
	for (index = 0; index < tabs->len; index++) {
		SakuraTab *tab = g_ptr_array_index(tabs, index);
		g_free(tab->terminal_id);
		g_free(tab->cwd);
		g_free(tab);
	}
	g_ptr_array_unref(tabs);
}


static void
assert_workspace_consistent(void)
{
	gint count, index, current;
	GError *workspace_error = NULL;
	gboolean valid;

	valid = sakura_workspace_validate(&workspace_error);
	if (!valid && workspace_error != NULL)
		g_test_message("workspace validation failed: %s", workspace_error->message);
	g_assert_true(valid);
	g_assert_no_error(workspace_error);
	g_assert_nonnull(sakura.notebook);
	g_assert_nonnull(sakura.pages);
	g_assert_nonnull(sakura.tabs);
	count = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));
	g_assert_cmpuint(sakura.pages->len, ==, (guint)count);
	g_assert_cmpuint(sakura.tabs->len, ==, (guint)count);

	for (index = 0; index < count; index++) {
		SakuraPage *page = sakura_page_at_page(index);
		SakuraTab *tab = sakura_tab_at_page(index);
		GError *error = NULL;

		g_assert_nonnull(page);
		g_assert_nonnull(tab);
		g_assert_true(tab->page == page);
		g_assert_true(page->container ==
		              gtk_notebook_get_nth_page(GTK_NOTEBOOK(sakura.notebook), index));
		g_assert_true(gtk_widget_get_visible(page->container));
		g_assert_cmpint(sakura_page_for_tab(tab), ==, index);
		g_assert_nonnull(tab->layout_leaf);
		g_assert_true(tab->layout_leaf->widget == tab->hbox);
		g_assert_true(sakura_layout_validate(page, &error));
		g_assert_no_error(error);
	}

	current = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	if (sakura.active_page != NULL) {
		g_assert_cmpint(current, >=, 0);
		g_assert_true(sakura_page_at_page(current) == sakura.active_page);
		g_assert_nonnull(sakura.active_tab);
		g_assert_true(sakura.active_tab->page == sakura.active_page);
	}
}


static void
setup_workspace(void)
{
	SakuraPage *first, *second, *third;

	memset(&sakura, 0, sizeof(sakura));
	sakura.notebook = gtk_notebook_new();
	sakura.pages = g_ptr_array_new();
	sakura.tabs = g_ptr_array_new();

	first = test_page_new("page-a", "terminal-a");
	second = test_page_new("page-b", "terminal-b");
	third = test_page_new("page-c", "terminal-c");
	g_ptr_array_add(sakura.pages, first);
	g_ptr_array_add(sakura.pages, second);
	g_ptr_array_add(sakura.pages, third);
	g_ptr_array_add(sakura.tabs, first->tab_bar_tab);
	g_ptr_array_add(sakura.tabs, second->tab_bar_tab);
	g_ptr_array_add(sakura.tabs, third->tab_bar_tab);
	gtk_notebook_append_page(GTK_NOTEBOOK(sakura.notebook), first->container, NULL);
	gtk_notebook_append_page(GTK_NOTEBOOK(sakura.notebook), second->container, NULL);
	gtk_notebook_append_page(GTK_NOTEBOOK(sakura.notebook), third->container, NULL);
	gtk_widget_show_all(sakura.notebook);
	gtk_notebook_set_current_page(GTK_NOTEBOOK(sakura.notebook), 1);
	sakura.active_page = second;
	sakura.active_tab = second->active_tab;
}


static void
teardown_workspace(void)
{
	GPtrArray *pages = g_ptr_array_new();
	guint index;

	for (index = 0; index < sakura.pages->len; index++)
		g_ptr_array_add(pages, g_ptr_array_index(sakura.pages, index));
	if (sakura.sidebar_model != NULL)
		teardown_sidebar_fixture();
	gtk_widget_destroy(sakura.notebook);
	for (index = 0; index < pages->len; index++)
		test_page_free(g_ptr_array_index(pages, index));
	g_ptr_array_unref(pages);
	g_ptr_array_unref(sakura.pages);
	g_ptr_array_unref(sakura.tabs);
	memset(&sakura, 0, sizeof(sakura));
}


static void
test_notebook_identity_and_reorder(void)
{
	GtkWidget *third_child;

	setup_workspace();
	assert_workspace_consistent();

	third_child = gtk_notebook_get_nth_page(GTK_NOTEBOOK(sakura.notebook), 2);
	gtk_notebook_reorder_child(GTK_NOTEBOOK(sakura.notebook), third_child, 0);
	g_assert_true(sakura_notebook_sync_page_order());
	g_assert_cmpstr(sakura_tab_at_page(0)->terminal_id, ==, "terminal-c");
	assert_workspace_consistent();
	teardown_workspace();
}


static void
test_leaf_widget_can_split(void)
{
	SakuraPage *page;
	SakuraTab *new_tab;

	setup_workspace();
	page = sakura_page_at_page(1);
	new_tab = g_new0(SakuraTab, 1);
	new_tab->terminal_id = g_strdup("terminal-split");
	new_tab->hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	g_assert_true(sakura_layout_split_node_widgets(
		page->active_tab->layout_leaf, SAKURA_SPLIT_RIGHT, new_tab));
	g_assert_cmpuint(page->panes->len, ==, 2);
	g_assert_true(new_tab->page == page);
	g_assert_nonnull(new_tab->layout_leaf);
	g_assert_true(new_tab->layout_leaf->widget == new_tab->hbox);
	g_assert_true(gtk_widget_get_visible(new_tab->hbox));
	assert_workspace_consistent();

	teardown_workspace();
}


static void
test_generated_page_id_avoids_existing_page(void)
{
	SakuraPage *seed;
	SakuraPage *reserved;
	SakuraPage *candidate;
	gchar *reserved_id;
	guint seed_number;

	setup_workspace();
	seed = sakura_page_new(NULL);
	g_assert_true(g_str_has_prefix(seed->id, "page-"));
	seed_number = (guint)g_ascii_strtoull(seed->id + strlen("page-"), NULL, 10);
	g_ptr_array_add(sakura.pages, seed);
	reserved_id = g_strdup_printf("page-%u", seed_number + 1);
	reserved = sakura_page_new(reserved_id);
	g_ptr_array_add(sakura.pages, reserved);
	candidate = sakura_page_new(NULL);
	g_assert_cmpstr(candidate->id, !=, seed->id);
	g_assert_cmpstr(candidate->id, !=, reserved->id);

	sakura_page_free(candidate);
	g_ptr_array_remove(sakura.pages, reserved);
	g_ptr_array_remove(sakura.pages, seed);
	sakura_page_free(reserved);
	sakura_page_free(seed);
	g_free(reserved_id);
	teardown_workspace();
}


static void
test_select_and_detach_by_identity(void)
{
	SakuraPage *selected, *removed;

	setup_workspace();
	selected = sakura_page_at_page(2);
	sakura.active_page = selected;
	sakura.active_tab = selected->active_tab;
	gtk_notebook_set_current_page(GTK_NOTEBOOK(sakura.notebook),
	                              sakura_page_for_tab(selected->active_tab));
	assert_workspace_consistent();

	removed = sakura_page_at_page(1);
	g_assert_true(sakura_notebook_detach_page(removed));
	g_assert_cmpuint(sakura.pages->len, ==, 2);
	g_assert_cmpuint(sakura.tabs->len, ==, 2);
	g_assert_cmpint(gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook)), ==, 2);
	g_assert_cmpstr(sakura_tab_at_page(0)->terminal_id, ==, "terminal-a");
	g_assert_cmpstr(sakura_tab_at_page(1)->terminal_id, ==, "terminal-c");
	assert_workspace_consistent();
	test_page_free(removed);
	teardown_workspace();
}


static void
test_snapshot_destroy_restore_equivalence(void)
{
	SakuraSessionSnapshot *source, *loaded;
	SakuraPage *split_page;
	SakuraTab *split_tab;
	GKeyFile *key_file;
	GError *error = NULL;
	guint index;

	setup_workspace();
	split_page = sakura_page_at_page(1);
	split_tab = g_new0(SakuraTab, 1);
	split_tab->terminal_id = g_strdup("terminal-roundtrip-split");
	split_tab->hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	split_tab->label = gtk_label_new("Terminal");
	g_assert_true(sakura_layout_split_node_widgets(
		split_page->active_tab->layout_leaf, SAKURA_SPLIT_DOWN, split_tab));
	split_page->active_tab = split_tab;
	sakura.active_page = split_page;
	sakura.active_tab = split_tab;
	setup_sidebar_fixture();
	assert_workspace_consistent();

	source = sakura_workspace_snapshot_new();
	g_assert_cmpuint(source->pages->len, ==, 3);
	g_assert_cmpuint(source->tabs->len, ==, 4);
	g_assert_cmpuint(source->layouts->len, ==, 5);
	g_assert_cmpstr(source->selected_terminal_id, ==, "terminal-roundtrip-split");
	key_file = g_key_file_new();
	sakura_session_snapshot_save(source, key_file);
	loaded = sakura_session_snapshot_new();
	g_assert_true(sakura_session_snapshot_load(key_file, loaded, &error));
	g_assert_no_error(error);
	g_assert_cmpuint(loaded->pages->len, ==, source->pages->len);
	g_assert_cmpuint(loaded->tabs->len, ==, source->tabs->len);
	g_assert_cmpuint(loaded->layouts->len, ==, source->layouts->len);
	g_assert_cmpstr(loaded->selected_terminal_id, ==, source->selected_terminal_id);

	teardown_workspace();
	setup_workspace_from_snapshot(loaded);
	setup_sidebar_fixture();
	assert_workspace_consistent();
	for (index = 0; index < loaded->pages->len; index++) {
		SakuraSessionPageRecord *record = g_ptr_array_index(loaded->pages, index);
		SakuraPage *page = g_ptr_array_index(sakura.pages, index);
		gchar *expected = test_snapshot_layout_signature(
			loaded, test_snapshot_layout_record(loaded, record->root_layout_id));
		gchar *actual = test_live_layout_signature(page->layout_root);
		g_assert_cmpstr(page->id, ==, record->id);
		g_assert_cmpstr(actual, ==, expected);
		g_free(actual);
		g_free(expected);
	}
	g_assert_cmpstr(sakura.active_tab->terminal_id, ==,
	                loaded->selected_terminal_id);

	g_key_file_free(key_file);
	sakura_session_snapshot_free(source);
	sakura_session_snapshot_free(loaded);
	teardown_workspace();
}


static gchar *
test_sidebar_column(SakuraTab *tab, guint column)
{
	GtkTreeIter iter;
	gchar *value = NULL;

	g_assert_nonnull(tab);
	g_assert_nonnull(tab->sidebar_node);
	g_assert_true(sakura_sidebar_get_iter(tab->sidebar_node, &iter));
	gtk_tree_model_get(GTK_TREE_MODEL(sakura.sidebar_model), &iter,
	                   column, &value, -1);
	return value;
}


static guint
test_sidebar_uint_column(SakuraTab *tab, guint column)
{
	GtkTreeIter iter;
	guint value = 0;

	g_assert_nonnull(tab);
	g_assert_nonnull(tab->sidebar_node);
	g_assert_true(sakura_sidebar_get_iter(tab->sidebar_node, &iter));
	gtk_tree_model_get(GTK_TREE_MODEL(sakura.sidebar_model), &iter,
	                   column, &value, -1);
	return value;
}


static gboolean
test_menu_has_label(GtkWidget *menu, const gchar *label)
{
	GList *items, *item;
	gboolean found = FALSE;

	items = gtk_container_get_children(GTK_CONTAINER(menu));
	for (item = items; item != NULL; item = item->next) {
		GtkWidget *child;

		if (!GTK_IS_MENU_ITEM(item->data))
			continue;
		child = gtk_bin_get_child(GTK_BIN(item->data));
		if (GTK_IS_LABEL(child) &&
		    g_strcmp0(gtk_label_get_text(GTK_LABEL(child)), label) == 0) {
			found = TRUE;
			break;
		}
	}
	g_list_free(items);
	return found;
}


static GtkWidget *
test_menu_item_for_label(GtkWidget *menu, const gchar *label)
{
	GList *items, *item;
	GtkWidget *result = NULL;

	items = gtk_container_get_children(GTK_CONTAINER(menu));
	for (item = items; item != NULL; item = item->next) {
		GtkWidget *child;

		if (!GTK_IS_MENU_ITEM(item->data))
			continue;
		child = gtk_bin_get_child(GTK_BIN(item->data));
		if (GTK_IS_LABEL(child) &&
		    g_strcmp0(gtk_label_get_text(GTK_LABEL(child)), label) == 0) {
			result = item->data;
			break;
		}
	}
	g_list_free(items);
	return result;
}


static void
test_sidebar_hides_redundant_directory(void)
{
	SakuraTab *same, *different;
	gchar *subtitle, *tooltip;

	setup_workspace();
	setup_sidebar_fixture();
	sakura.panes = g_ptr_array_new();
	for (guint index = 0; index < sakura.pages->len; index++) {
		SakuraPage *page = g_ptr_array_index(sakura.pages, index);
		for (guint pane_index = 0; pane_index < page->panes->len; pane_index++)
			g_ptr_array_add(sakura.panes, g_ptr_array_index(page->panes, pane_index));
	}
	sakura.cfg = g_key_file_new();
	sakura.root_group->directory = g_strdup("/tmp");

	same = sakura_page_at_page(0)->active_tab;
	same->cwd = g_strdup("/tmp");
	sakura_sidebar_model_reordered_cb(NULL, NULL, NULL, NULL, NULL);
	subtitle = test_sidebar_column(same, SAKURA_SIDEBAR_COLUMN_SUBTITLE);
	tooltip = test_sidebar_column(same, SAKURA_SIDEBAR_COLUMN_TOOLTIP);
	g_assert_cmpstr(subtitle, ==, "");
	g_assert_true(g_strstr_len(tooltip, -1, "/tmp") != NULL);
	g_free(subtitle);
	g_free(tooltip);

	different = sakura_page_at_page(1)->active_tab;
	different->cwd = g_strdup("/var");
	sakura_sidebar_model_reordered_cb(NULL, NULL, NULL, NULL, NULL);
	subtitle = test_sidebar_column(different, SAKURA_SIDEBAR_COLUMN_SUBTITLE);
	g_assert_cmpstr(subtitle, ==, "/var");
	g_free(subtitle);

	g_free(sakura.root_group->directory);
	sakura.root_group->directory = g_strdup("/directory-that-does-not-exist");
	g_free(same->cwd);
	same->cwd = g_strdup("/directory-that-does-not-exist");
	sakura_sidebar_model_reordered_cb(NULL, NULL, NULL, NULL, NULL);
	subtitle = test_sidebar_column(same, SAKURA_SIDEBAR_COLUMN_SUBTITLE);
	g_assert_cmpstr(subtitle, ==, "/directory-that-does-not-exist");
	g_free(subtitle);

	g_key_file_free(sakura.cfg);
	sakura.cfg = NULL;
	g_ptr_array_free(sakura.panes, TRUE);
	sakura.panes = NULL;
	teardown_workspace();
}


static void
test_sidebar_pulses_nested_rows(void)
{
	SakuraPage *page;
	SakuraTab *tab;
	guint before, after;

	setup_workspace();
	page = sakura_page_at_page(1);
	tab = g_new0(SakuraTab, 1);
	tab->terminal_id = g_strdup("terminal-spinner");
	tab->hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	tab->label = gtk_label_new("Terminal");
	g_assert_true(sakura_layout_split_node_widgets(
		page->active_tab->layout_leaf, SAKURA_SPLIT_DOWN, tab));
	page->active_tab = tab;
	sakura.active_page = page;
	sakura.active_tab = tab;
	setup_sidebar_fixture();

	tab->status = SAKURA_TAB_STATUS_RUNNING;
	sakura.sidebar_spinner_pulse = 41;
	sakura_sidebar_update_tab(tab);
	before = test_sidebar_uint_column(tab, SAKURA_SIDEBAR_COLUMN_STATUS_PULSE);
	sakura_sidebar_spinner_pulse_cb(NULL);
	after = test_sidebar_uint_column(tab, SAKURA_SIDEBAR_COLUMN_STATUS_PULSE);
	g_assert_cmpuint(before, ==, 41);
	g_assert_cmpuint(after, ==, 42);

	teardown_workspace();
}


static void
test_sidebar_selects_created_tab(void)
{
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	SakuraSidebarNode *group;
	SakuraSidebarNode *node = NULL;
	SakuraPage *page;
	SakuraTab *tab;

	setup_workspace();
	setup_sidebar_fixture();
	group = test_sidebar_add_group("group-created", "Created", sakura.sidebar_root);
	page = sakura_page_at_page(1);
	g_assert_true(sakura_sidebar_move_page_to_group(page, group));
	sakura.active_group_scope = group;
	tab = page->active_tab;

	/* Reproduce the state immediately before the first terminal appears in an
	 * empty group. Selection used to update the tree without refreshing these
	 * main-pane widgets, leaving the empty placeholder on screen. */
	sakura.tab_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	sakura.tab_bar_scope_label = gtk_label_new("");
	sakura.tab_bar_scrolled = gtk_scrolled_window_new(NULL, NULL);
	sakura.tab_bar_shell = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	sakura.tab_bar_empty = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_hide(sakura.notebook);
	gtk_widget_show(sakura.tab_bar_empty);

	sakura_sidebar_select_created_tab(tab);
	g_assert_true(gtk_tree_selection_get_selected(sakura.sidebar_selection,
	                                               &model, &iter));
	gtk_tree_model_get(model, &iter, SAKURA_SIDEBAR_COLUMN_NODE, &node, -1);
	g_assert_true(node == tab->page->sidebar_node);
	g_assert_cmpuint(sakura_tab_bar_visible_count(), ==, 1);
	g_assert_true(gtk_widget_get_visible(sakura.notebook));
	g_assert_false(gtk_widget_get_visible(sakura.tab_bar_empty));

	gtk_widget_destroy(sakura.tab_bar);
	gtk_widget_destroy(sakura.tab_bar_scope_label);
	gtk_widget_destroy(sakura.tab_bar_scrolled);
	gtk_widget_destroy(sakura.tab_bar_shell);
	gtk_widget_destroy(sakura.tab_bar_empty);
	sakura.tab_bar = NULL;
	sakura.tab_bar_scope_label = NULL;
	sakura.tab_bar_scrolled = NULL;
	sakura.tab_bar_shell = NULL;
	sakura.tab_bar_empty = NULL;
	sakura.active_group_scope = sakura.sidebar_root;
	g_assert_true(sakura_sidebar_move_page_to_group(page, sakura.sidebar_root));
	test_sidebar_remove_group(group);
	teardown_workspace();
}


static void
test_workspace_reconciles_at_outer_mutation_boundary(void)
{
	setup_workspace();
	setup_sidebar_fixture();
	sakura.tab_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	sakura.tab_bar_scope_label = gtk_label_new("");
	sakura.tab_bar_scrolled = gtk_scrolled_window_new(NULL, NULL);
	sakura.tab_bar_shell = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	sakura.tab_bar_empty = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_hide(sakura.notebook);
	gtk_widget_show(sakura.tab_bar_empty);

	sakura_workspace_begin_mutation();
	sakura_workspace_begin_mutation();
	sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_SELECTION);
	g_assert_false(gtk_widget_get_visible(sakura.notebook));
	g_assert_true(gtk_widget_get_visible(sakura.tab_bar_empty));
	g_assert_cmpuint(sakura.workspace_mutation_depth, ==, 2);
	g_assert_cmpuint(sakura.workspace_pending_changes, !=,
	                 SAKURA_WORKSPACE_CHANGE_NONE);

	sakura_workspace_end_mutation();
	g_assert_false(gtk_widget_get_visible(sakura.notebook));
	g_assert_true(gtk_widget_get_visible(sakura.tab_bar_empty));
	g_assert_cmpuint(sakura.workspace_mutation_depth, ==, 1);

	sakura_workspace_end_mutation();
	g_assert_true(gtk_widget_get_visible(sakura.notebook));
	g_assert_false(gtk_widget_get_visible(sakura.tab_bar_empty));
	g_assert_cmpuint(sakura.workspace_mutation_depth, ==, 0);
	g_assert_cmpuint(sakura.workspace_pending_changes, ==,
	                 SAKURA_WORKSPACE_CHANGE_NONE);

	gtk_widget_destroy(sakura.tab_bar);
	gtk_widget_destroy(sakura.tab_bar_scope_label);
	gtk_widget_destroy(sakura.tab_bar_scrolled);
	gtk_widget_destroy(sakura.tab_bar_shell);
	gtk_widget_destroy(sakura.tab_bar_empty);
	sakura.tab_bar = NULL;
	sakura.tab_bar_scope_label = NULL;
	sakura.tab_bar_scrolled = NULL;
	sakura.tab_bar_shell = NULL;
	sakura.tab_bar_empty = NULL;
	teardown_workspace();
}


static void
test_sidebar_selection_priority(void)
{
	SakuraPage *page_a, *page_b, *page_c;
	GtkTreeIter iter;
	GtkTreePath *path;

	setup_workspace();
	setup_sidebar_fixture();
	g_signal_connect(sakura.sidebar_selection, "changed",
	                 G_CALLBACK(sakura_sidebar_selection_changed_cb), NULL);
	page_a = sakura_page_at_page(0);
	page_b = sakura_page_at_page(1);
	page_c = sakura_page_at_page(2);

	/* Notebook synchronization can produce several requests while a page is
	 * being created or restored. The authoritative request must survive the
	 * later, lower-priority sync noise. */
	sakura_sidebar_queue_select_node_with_reason(
		page_a->sidebar_node, SAKURA_SIDEBAR_SELECTION_SYNC);
	sakura_sidebar_queue_select_node_with_reason(
		page_b->sidebar_node, SAKURA_SIDEBAR_SELECTION_CREATION);
	sakura_sidebar_queue_select_node_with_reason(
		page_c->sidebar_node, SAKURA_SIDEBAR_SELECTION_SYNC);
	while (g_main_context_pending(NULL))
		g_main_context_iteration(NULL, FALSE);

	g_assert_true(sakura_sidebar_selected_node() == page_b->sidebar_node);

	/* A real user selection cancels an internal request, even when that
	 * request has the higher internal priority. */
	sakura_sidebar_queue_select_node_with_reason(
		page_a->sidebar_node, SAKURA_SIDEBAR_SELECTION_CREATION);
	g_assert_true(sakura_sidebar_get_iter(page_c->sidebar_node, &iter));
	path = gtk_tree_model_get_path(GTK_TREE_MODEL(sakura.sidebar_model), &iter);
	gtk_tree_selection_select_path(sakura.sidebar_selection, path);
	gtk_tree_path_free(path);
	while (g_main_context_pending(NULL))
		g_main_context_iteration(NULL, FALSE);
	g_assert_true(sakura_sidebar_selected_node() == page_c->sidebar_node);
	teardown_workspace();
}


static void
test_close_active_page_preserves_group_scope(void)
{
	SakuraSidebarNode *group_a, *group_b;
	SakuraPage *page_a, *page_b, *page_c;

	setup_workspace();
	setup_sidebar_fixture();
	group_a = test_sidebar_add_group("group-a", "Alpha", sakura.sidebar_root);
	group_b = test_sidebar_add_group("group-b", "Beta", sakura.sidebar_root);
	page_a = sakura_page_at_page(0);
	page_b = sakura_page_at_page(1);
	page_c = sakura_page_at_page(2);
	g_assert_true(sakura_sidebar_move_page_to_group(page_a, group_a));
	g_assert_true(sakura_sidebar_move_page_to_group(page_b, group_b));
	g_assert_true(sakura_sidebar_move_page_to_group(page_c, group_a));
	g_assert_true(page_a->group == group_a->group);
	g_assert_true(page_b->group == group_b->group);
	g_assert_true(page_c->group == group_a->group);
	sakura.active_group_scope = group_a;
	sakura.active_page = page_a;
	sakura.active_tab = page_a->active_tab;
	gtk_notebook_set_current_page(GTK_NOTEBOOK(sakura.notebook), 0);
	group_a->group->last_terminal_id = g_strdup(page_a->active_tab->terminal_id);
	g_signal_connect(sakura.notebook, "switch-page",
	                 G_CALLBACK(sakura_switch_page_cb), NULL);

	/* The physical next notebook page belongs to Beta. The replacement must
	 * still come from Alpha after GTK emits its removal-time switch signal. */
	g_assert_true(sakura_tab_delete_page(0));
	g_assert_true(sakura.active_group_scope == group_a);
	g_assert_true(sakura.active_page == page_c);
	g_assert_true(sakura.active_tab == page_c->active_tab);
	g_assert_cmpstr(group_a->group->last_terminal_id, ==,
	                page_c->active_tab->terminal_id);
	g_assert_cmpstr(sakura_tab_at_page(0)->terminal_id, ==,
	                page_b->active_tab->terminal_id);
	g_assert_cmpstr(sakura_tab_at_page(1)->terminal_id, ==,
	                page_c->active_tab->terminal_id);

	sakura_sidebar_cancel_pending_selection();
	test_sidebar_remove_group(group_a);
	test_sidebar_remove_group(group_b);
	teardown_workspace();
}


static void
test_selecting_terminal_switches_group_scope(void)
{
	SakuraSidebarNode *group_a, *group_b;
	SakuraPage *page_a, *page_b, *page_c;

	setup_workspace();
	setup_sidebar_fixture();
	group_a = test_sidebar_add_group("group-a", "Alpha", sakura.sidebar_root);
	group_b = test_sidebar_add_group("group-b", "Beta", sakura.sidebar_root);
	page_a = sakura_page_at_page(0);
	page_b = sakura_page_at_page(1);
	page_c = sakura_page_at_page(2);
	g_assert_true(sakura_sidebar_move_page_to_group(page_a, group_a));
	g_assert_true(sakura_sidebar_move_page_to_group(page_b, group_b));
	g_assert_true(sakura_sidebar_move_page_to_group(page_c, group_a));
	g_assert_true(sakura_group_for_session(page_a) == group_a->group);
	g_assert_true(sakura_group_for_session(page_b) == group_b->group);
	g_assert_true(sakura_group_for_session(page_c) == group_a->group);

	sakura.active_group_scope = group_a;
	sakura.active_task = NULL;
	sakura.active_page = page_a;
	sakura.active_tab = page_a->active_tab;
	gtk_notebook_set_current_page(GTK_NOTEBOOK(sakura.notebook), 0);

	/* Selecting a terminal in another group must select that group. Passing the
	 * terminal's immediate page node to sakura_sidebar_set_scope() would be
	 * normalized to the root and expose every terminal in the tab bar. */
	sakura_select_tab(page_b->active_tab, FALSE);
	g_assert_true(sakura.active_group_scope == group_b);
	g_assert_null(sakura.active_task);
	g_assert_true(sakura_tab_is_in_active_scope(page_b->active_tab));
	g_assert_false(sakura_tab_is_in_active_scope(page_a->active_tab));
	g_assert_false(sakura_tab_is_in_active_scope(page_c->active_tab));
	g_assert_cmpuint(sakura_tab_bar_visible_count(), ==, 1);

	test_sidebar_remove_group(group_a);
	test_sidebar_remove_group(group_b);
	teardown_workspace();
}


static void
test_sidebar_collapses_split_session_panes(void)
{
	SakuraPage *page;
	SakuraTab *pane;
	GtkTreeIter iter;
	GtkTreePath *path;

	setup_workspace();
	page = sakura_page_at_page(1);
	pane = g_new0(SakuraTab, 1);
	pane->terminal_id = g_strdup("terminal-collapsed-pane");
	pane->hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	pane->label = gtk_label_new("Terminal");
	g_assert_true(sakura_layout_split_node_widgets(
		page->active_tab->layout_leaf, SAKURA_SPLIT_DOWN, pane));
	g_assert_true(sakura_session_for_pane(pane) == page);
	g_assert_true(sakura_session_active_pane(page) == page->active_tab);
	setup_sidebar_fixture();
	sakura_sidebar_apply_default_expansion();

	g_assert_true(sakura_sidebar_get_iter(page->sidebar_node, &iter));
	g_assert_cmpint(gtk_tree_model_iter_n_children(
		GTK_TREE_MODEL(sakura.sidebar_model), &iter), ==, 2);
	path = gtk_tree_model_get_path(GTK_TREE_MODEL(sakura.sidebar_model), &iter);
	g_assert_false(gtk_tree_view_row_expanded(GTK_TREE_VIEW(sakura.sidebar_tree),
	                                           path));
	g_assert_true(gtk_tree_view_expand_row(GTK_TREE_VIEW(sakura.sidebar_tree),
	                                       path, FALSE));
	g_assert_true(gtk_tree_view_row_expanded(GTK_TREE_VIEW(sakura.sidebar_tree),
	                                         path));
	gtk_tree_path_free(path);

	/* The pane rows stay in the model and become available when the user
	 * expands the session, rather than being deleted from the hierarchy. */
	g_assert_nonnull(pane->sidebar_node);
	teardown_workspace();
}


static void
test_sidebar_task_owns_page(void)
{
	SakuraTask *task;
	SakuraSidebarNode *node;
	SakuraPage *page;
	GtkTreeIter node_iter;

	setup_workspace();
	setup_sidebar_fixture();
	sakura.tasks = g_ptr_array_new_with_free_func((GDestroyNotify)sakura_task_free);
	task = g_new0(SakuraTask, 1);
	task->id = g_strdup("task-sidebar");
	task->title = g_strdup("Task page");
	task->provider = g_strdup("local");
	task->status = SAKURA_TASK_READY;
	task->group = sakura.root_group;
	node = g_new0(SakuraSidebarNode, 1);
	node->type = SAKURA_SIDEBAR_TASK;
	node->id = g_strdup(task->id);
	node->title = g_strdup(task->title);
	node->parent = sakura.sidebar_root;
	node->task = task;
	task->sidebar_node = node;
	g_ptr_array_add(sakura.tasks, task);
	sakura_sidebar_insert_node(node);

	page = sakura_page_at_page(1);
	sakura_task_attach_page(task, page);
	g_assert_true(page->task == task);
	g_assert_true(page->group == sakura.root_group);
	g_assert_true(page->sidebar_node->parent == node);
	g_assert_true(sakura_tab_is_in_active_scope(page->active_tab));
	assert_workspace_consistent();

	sakura_task_detach_page(page);
	g_assert_null(page->task);
	g_assert_true(page->group == sakura.root_group);
	g_assert_true(page->sidebar_node->parent == sakura.sidebar_root);
	assert_workspace_consistent();

	if (sakura_sidebar_get_iter(node, &node_iter))
		gtk_tree_store_remove(sakura.sidebar_model, &node_iter);
	sakura_sidebar_free_node(node);
	task->sidebar_node = NULL;
	sakura.active_task = NULL;
	g_ptr_array_unref(sakura.tasks);
	sakura.tasks = NULL;
	teardown_workspace();
}


static void
test_task_model_survives_sidebar_projection(void)
{
	SakuraTask *task;
	SakuraSidebarNode *node;
	SakuraPage *page;
	SakuraSessionSnapshot *snapshot;
	GtkTreeIter iter;

	setup_workspace();
	setup_sidebar_fixture();
	sakura.tasks = g_ptr_array_new_with_free_func((GDestroyNotify)sakura_task_free);
	task = g_new0(SakuraTask, 1);
	task->id = g_strdup("task-model-only");
	task->title = g_strdup("Model-only task");
	task->status = SAKURA_TASK_READY;
	task->group = sakura.root_group;
	g_ptr_array_add(sakura.tasks, task);
	page = sakura_page_at_page(1);

	/* A task can own a page before its sidebar projection exists. */
	sakura_task_attach_page(task, page);
	g_assert_null(task->sidebar_node);
	g_assert_true(page->task == task);
	g_assert_true(page->sidebar_node->parent == sakura.sidebar_root);
	sakura_task_update_row(task);
	assert_workspace_consistent();
	snapshot = sakura_workspace_snapshot_new();
	g_assert_cmpuint(snapshot->tasks->len, ==, 1);
	g_assert_cmpstr(((SakuraSessionTaskRecord *)g_ptr_array_index(
		snapshot->tasks, 0))->id, ==, "task-model-only");
	sakura_session_snapshot_free(snapshot);

	/* Materializing the projection must move the existing page beneath the
	 * newly-created task row without changing model ownership. */
	sakura_sidebar_rebuild_projection();
	node = task->sidebar_node;
	g_assert_nonnull(node);
	g_assert_true(page->sidebar_node->parent == node);
	assert_workspace_consistent();

	/* Removing the projection must not remove the model task. */
	g_assert_true(sakura_sidebar_get_iter(node, &iter));
	gtk_tree_store_remove(sakura.sidebar_model, &iter);
	sakura_sidebar_free_node(node);
	g_assert_null(task->sidebar_node);
	g_assert_true(sakura_task_find_by_id("task-model-only") == task);
	sakura_task_detach_page(page);
	g_assert_null(page->task);
	g_assert_true(page->sidebar_node->parent == sakura.sidebar_root);
	assert_workspace_consistent();

	g_ptr_array_unref(sakura.tasks);
	sakura.tasks = NULL;
	teardown_workspace();
}


static void
test_group_model_survives_sidebar_projection(void)
{
	SakuraSidebarNode *group_row, *replacement;
	SakuraGroup *model_group;
	SakuraPage *page;
	GtkTreeIter iter;

	setup_workspace();
	setup_sidebar_fixture();
	group_row = test_sidebar_add_group("group-model", "Model group",
	                                  sakura.sidebar_root);
	page = sakura_page_at_page(1);
	g_assert_true(sakura_sidebar_move_page_to_group(page, group_row));
	model_group = group_row->group;

	g_assert_true(sakura_sidebar_get_iter(group_row, &iter));
	gtk_tree_store_remove(sakura.sidebar_model, &iter);
	sakura_sidebar_free_node(group_row);
	g_assert_null(model_group->sidebar_node);
	g_assert_true(page->group == model_group);

	replacement = test_sidebar_project_group(model_group, sakura.sidebar_root);
	g_assert_true(model_group->sidebar_node == replacement);
	g_assert_true(page->group == model_group);
	if (page->sidebar_node->row != NULL) {
		gtk_tree_row_reference_free(page->sidebar_node->row);
		page->sidebar_node->row = NULL;
	}
	page->sidebar_node->parent = replacement;
	sakura_sidebar_insert_node(page->sidebar_node);
	sakura_sidebar_sync_parents();
	assert_workspace_consistent();

	g_assert_true(sakura_sidebar_move_page_to_group(page, sakura.sidebar_root));
	test_sidebar_remove_group(replacement);
	teardown_workspace();
}


static void
test_sidebar_rebuilds_nested_model_projection(void)
{
	SakuraGroup *group;
	SakuraTask *parent_task, *child_task;
	SakuraPage *page;
	SakuraSessionSnapshot *snapshot;
	SakuraSessionGroupRecord *group_record;
	SakuraSessionTabRecord *tab_record;
	SakuraSessionTaskRecord *child_record;
	gchar **saved_group_ids;
	gsize saved_group_count = 0;
	GtkTreeIter group_iter;

	setup_workspace();
	setup_sidebar_fixture();
	sakura.tasks = g_ptr_array_new_with_free_func((GDestroyNotify)sakura_task_free);
	group = g_new0(SakuraGroup, 1);
	group->id = g_strdup("projection-group");
	group->title = g_strdup("Projection group");
	group->parent = sakura.root_group;
	sakura.groups = g_list_append(sakura.groups, group);

	parent_task = g_new0(SakuraTask, 1);
	parent_task->id = g_strdup("projection-parent");
	parent_task->title = g_strdup("Parent task");
	parent_task->provider = g_strdup("local");
	parent_task->status = SAKURA_TASK_READY;
	parent_task->group = group;
	child_task = g_new0(SakuraTask, 1);
	child_task->id = g_strdup("projection-child");
	child_task->title = g_strdup("Child task");
	child_task->provider = g_strdup("local");
	child_task->status = SAKURA_TASK_WORKING;
	child_task->parent = parent_task;
	child_task->group = group;
	/* Deliberately reverse registry order to exercise parent-first projection. */
	g_ptr_array_add(sakura.tasks, child_task);
	g_ptr_array_add(sakura.tasks, parent_task);

	page = sakura_page_at_page(1);
	page->task = child_task;
	page->group = group;
	sakura_sidebar_rebuild_projection();

	g_assert_nonnull(group->sidebar_node);
	g_assert_nonnull(parent_task->sidebar_node);
	g_assert_nonnull(child_task->sidebar_node);
	g_assert_true(parent_task->sidebar_node->parent == group->sidebar_node);
	g_assert_true(child_task->sidebar_node->parent == parent_task->sidebar_node);
	g_assert_true(page->sidebar_node->parent == child_task->sidebar_node);
	g_assert_true(page->sidebar_node->page == page);
	assert_workspace_consistent();

	/* Rebuilding is idempotent and must preserve the same model hierarchy. */
	sakura_sidebar_rebuild_projection();
	g_assert_true(parent_task->sidebar_node->parent == group->sidebar_node);
	g_assert_true(child_task->sidebar_node->parent == parent_task->sidebar_node);
	g_assert_true(page->sidebar_node->parent == child_task->sidebar_node);
	assert_workspace_consistent();

	/* Snapshot generation must not depend on the projection being present. */
	g_assert_true(sakura_sidebar_get_iter(group->sidebar_node, &group_iter));
	gtk_tree_store_remove(sakura.sidebar_model, &group_iter);
	sakura_sidebar_free_node(group->sidebar_node);
	g_assert_null(group->sidebar_node);
	sakura.cfg = g_key_file_new();
	sakura_sidebar_model_reordered_cb(NULL, NULL, NULL, NULL, NULL);
	saved_group_ids = g_key_file_get_string_list(
		sakura.cfg, "sakura", "sidebar_group_ids", &saved_group_count, NULL);
	g_assert_cmpuint(saved_group_count, ==, 1);
	g_assert_cmpstr(saved_group_ids[0], ==, "projection-group");
	g_strfreev(saved_group_ids);
	g_key_file_free(sakura.cfg);
	sakura.cfg = NULL;
	snapshot = sakura_workspace_snapshot_new();
	g_assert_cmpuint(snapshot->groups->len, ==, 1);
	group_record = g_ptr_array_index(snapshot->groups, 0);
	g_assert_cmpstr(group_record->id, ==, "projection-group");
	g_assert_cmpstr(group_record->parent_id, ==, "root");
	g_assert_cmpuint(snapshot->tabs->len, ==, 3);
	tab_record = test_snapshot_tab_record(snapshot, "terminal-b");
	g_assert_nonnull(tab_record);
	g_assert_cmpstr(tab_record->parent_id, ==, "projection-child");
	g_assert_cmpuint(snapshot->tasks->len, ==, 2);
	for (guint index = 0; index < snapshot->tasks->len; index++) {
		SakuraSessionTaskRecord *record = g_ptr_array_index(snapshot->tasks, index);
		if (g_strcmp0(record->id, "projection-child") == 0) {
			child_record = record;
			break;
		}
	}
	g_assert_nonnull(child_record);
	g_assert_cmpstr(child_record->id, ==, "projection-child");
	g_assert_cmpstr(child_record->parent_id, ==, "projection-parent");
	sakura_session_snapshot_free(snapshot);
	sakura_sidebar_rebuild_projection();

	page->task = NULL;
	page->group = sakura.root_group;
	g_ptr_array_unref(sakura.tasks);
	sakura.tasks = NULL;
	teardown_workspace();
}


static void
test_sidebar_order_survives_snapshot_roundtrip(void)
{
	SakuraSidebarNode *group_a, *group_b;
	SakuraGroup *model_group_a, *model_group_b;
	SakuraTask *task_a, *task_b;
	SakuraSessionSnapshot *source, *loaded;
	SakuraSessionGroupRecord *group_record;
	SakuraSessionTaskRecord *task_record;
	GtkTreeIter first_iter, second_iter;
	GKeyFile *key_file;
	GError *error = NULL;

	setup_workspace();
	setup_sidebar_fixture();
	sakura.cfg = g_key_file_new();
	group_a = test_sidebar_add_group("ordered-group-a", "Group A",
	                                sakura.sidebar_root);
	group_b = test_sidebar_add_group("ordered-group-b", "Group B",
	                                sakura.sidebar_root);
	model_group_a = group_a->group;
	model_group_b = group_b->group;
	group_a->group->order = 0;
	group_b->group->order = 1;
	g_assert_true(sakura_sidebar_get_iter(group_a, &first_iter));
	g_assert_true(sakura_sidebar_get_iter(group_b, &second_iter));
	gtk_tree_store_move_before(sakura.sidebar_model, &second_iter, &first_iter);
	sakura_sidebar_model_reordered_cb(NULL, NULL, NULL, NULL, NULL);
	g_assert_cmpuint(group_b->group->order, ==, 0);
	g_assert_cmpuint(group_a->group->order, ==, 1);

	sakura.tasks = g_ptr_array_new_with_free_func((GDestroyNotify)sakura_task_free);
	task_a = g_new0(SakuraTask, 1);
	task_a->id = g_strdup("ordered-task-a");
	task_a->title = g_strdup("Task A");
	task_a->provider = g_strdup("local");
	task_a->status = SAKURA_TASK_READY;
	task_a->group = group_a->group;
	task_a->order = 0;
	task_b = g_new0(SakuraTask, 1);
	task_b->id = g_strdup("ordered-task-b");
	task_b->title = g_strdup("Task B");
	task_b->provider = g_strdup("local");
	task_b->status = SAKURA_TASK_READY;
	task_b->group = group_a->group;
	task_b->order = 1;
	g_ptr_array_add(sakura.tasks, task_a);
	g_ptr_array_add(sakura.tasks, task_b);
	sakura_sidebar_rebuild_projection();
	group_a = model_group_a->sidebar_node;
	group_b = model_group_b->sidebar_node;
	g_assert_true(sakura_sidebar_get_iter(task_a->sidebar_node, &first_iter));
	g_assert_true(sakura_sidebar_get_iter(task_b->sidebar_node, &second_iter));
	gtk_tree_store_move_before(sakura.sidebar_model, &second_iter, &first_iter);
	sakura_sidebar_model_reordered_cb(NULL, NULL, NULL, NULL, NULL);
	g_assert_cmpuint(task_b->order, ==, 0);
	g_assert_cmpuint(task_a->order, ==, 1);

	source = sakura_workspace_snapshot_new();
	g_assert_cmpuint(source->groups->len, ==, 2);
	group_record = g_ptr_array_index(source->groups, 0);
	g_assert_cmpstr(group_record->id, ==, "ordered-group-b");
	g_assert_cmpuint(group_record->order, ==, 0);
	group_record = test_snapshot_group_record(source, "ordered-group-a");
	g_assert_nonnull(group_record);
	g_assert_cmpuint(group_record->order, ==, 1);
	g_assert_cmpuint(source->tasks->len, ==, 2);
	task_record = g_ptr_array_index(source->tasks, 0);
	g_assert_cmpstr(task_record->id, ==, "ordered-task-b");
	g_assert_cmpuint(task_record->order, ==, 0);
	task_record = g_ptr_array_index(source->tasks, 1);
	g_assert_cmpstr(task_record->id, ==, "ordered-task-a");
	g_assert_cmpuint(task_record->order, ==, 1);

	key_file = g_key_file_new();
	sakura_session_snapshot_save(source, key_file);
	loaded = sakura_session_snapshot_new();
	g_assert_true(sakura_session_snapshot_load(key_file, loaded, &error));
	g_assert_no_error(error);
	g_assert_cmpuint(loaded->groups->len, ==, source->groups->len);
	g_assert_cmpuint(loaded->tasks->len, ==, source->tasks->len);
	g_assert_cmpuint(((SakuraSessionGroupRecord *)g_ptr_array_index(
		loaded->groups, 0))->order, ==, 0);
	g_assert_cmpuint(((SakuraSessionTaskRecord *)g_ptr_array_index(
		loaded->tasks, 0))->order, ==, 0);
	sakura_session_snapshot_free(loaded);
	sakura_session_snapshot_free(source);
	g_key_file_free(key_file);

	for (guint index = 0; index < sakura.tasks->len; index++) {
		SakuraTask *task = g_ptr_array_index(sakura.tasks, index);
		if (task->sidebar_node != NULL) {
			GtkTreeIter iter;
			if (sakura_sidebar_get_iter(task->sidebar_node, &iter))
				gtk_tree_store_remove(sakura.sidebar_model, &iter);
			sakura_sidebar_free_node(task->sidebar_node);
			task->sidebar_node = NULL;
		}
	}
	g_ptr_array_unref(sakura.tasks);
	sakura.tasks = NULL;
	test_sidebar_remove_group(group_a);
	test_sidebar_remove_group(group_b);
	g_key_file_free(sakura.cfg);
	sakura.cfg = NULL;
	teardown_workspace();
}


static void
test_sidebar_creation_parent_preserves_context(void)
{
	SakuraSidebarNode root = { 0 };
	SakuraSidebarNode freecad = { 0 };
	SakuraSidebarNode bim_walls = { 0 };
	SakuraSidebarNode task = { 0 };
	SakuraSidebarNode page = { 0 };
	SakuraSidebarNode terminal = { 0 };

	root.type = SAKURA_SIDEBAR_GROUP;
	freecad.type = SAKURA_SIDEBAR_GROUP;
	freecad.parent = &root;
	bim_walls.type = SAKURA_SIDEBAR_GROUP;
	bim_walls.parent = &freecad;
	task.type = SAKURA_SIDEBAR_TASK;
	task.parent = &bim_walls;
	page.type = SAKURA_SIDEBAR_PAGE;
	page.parent = &task;
	terminal.type = SAKURA_SIDEBAR_TERMINAL;
	terminal.parent = &page;

	/* A context-menu action must use the row that opened it, even when the
	 * currently selected tab belongs to a nested group. */
	g_assert_true(sakura_sidebar_creation_parent_for_context(&freecad) == &freecad);
	g_assert_true(sakura_sidebar_creation_parent_for_context(&bim_walls) == &bim_walls);
	g_assert_true(sakura_sidebar_creation_parent_for_context(&task) == &task);
	g_assert_true(sakura_sidebar_creation_parent_for_context(&page) == &task);
	g_assert_true(sakura_sidebar_creation_parent_for_context(&terminal) == &task);
}


static void
test_sidebar_move_page_preserves_whole_page_parent(void)
{
	SakuraPage *page;
	SakuraTab *pane;
	SakuraSidebarNode *group;

	setup_workspace();
	page = sakura_page_at_page(1);
	pane = g_new0(SakuraTab, 1);
	pane->terminal_id = g_strdup("terminal-move-pane");
	pane->hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	pane->label = gtk_label_new("Terminal");
	g_assert_true(sakura_layout_split_node_widgets(
		page->active_tab->layout_leaf, SAKURA_SPLIT_DOWN, pane));
	setup_sidebar_fixture();
	group = test_sidebar_add_group("freecad", "FreeCAD", sakura.sidebar_root);

	g_assert_true(sakura_sidebar_move_page_to_group(page, group));
	g_assert_true(page->sidebar_node->parent == group);
	g_assert_true(sakura_sidebar_get_iter(page->sidebar_node, &(GtkTreeIter){ 0 }));
	for (guint index = 0; index < page->panes->len; index++) {
		SakuraTab *tab = g_ptr_array_index(page->panes, index);
		g_assert_true(tab->sidebar_node->parent == page->sidebar_node);
	}

	/* The persisted/model parent must agree with the GTK tree after a move. */
	sakura_sidebar_sync_parents();
	g_assert_true(page->sidebar_node->parent == group);
	assert_workspace_consistent();

	test_sidebar_remove_group(group);
	teardown_workspace();
}


static void
test_sidebar_page_context_menu_names_page_close(void)
{
	SakuraPage *page;
	SakuraTab *pane;
	GtkWidget *menu;

	setup_workspace();
	page = sakura_page_at_page(1);
	pane = g_new0(SakuraTab, 1);
	pane->terminal_id = g_strdup("terminal-menu-pane");
	pane->hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	pane->label = gtk_label_new("Terminal");
	g_assert_true(sakura_layout_split_node_widgets(
		page->active_tab->layout_leaf, SAKURA_SPLIT_DOWN, pane));
	setup_sidebar_fixture();

	menu = sakura_sidebar_context_menu_new(page->sidebar_node);
	g_assert_true(test_menu_has_label(menu, "Close session"));
	g_assert_false(test_menu_has_label(menu, "Close terminal"));
	gtk_widget_destroy(menu);

	teardown_workspace();
}


static void
test_sidebar_terminal_context_menu_closes_only_pane(void)
{
	SakuraPage *page;
	SakuraTab *pane;
	GtkWidget *menu;
	GtkWidget *item;

	setup_workspace();
	page = sakura_page_at_page(1);
	pane = g_new0(SakuraTab, 1);
	pane->terminal_id = g_strdup("terminal-pane-close");
	pane->hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	pane->label = gtk_label_new("Terminal");
	g_assert_true(sakura_layout_split_node_widgets(
		page->active_tab->layout_leaf, SAKURA_SPLIT_DOWN, pane));
	setup_sidebar_fixture();

	menu = sakura_sidebar_context_menu_new(pane->sidebar_node);
	g_assert_true(test_menu_has_label(menu, "Close pane"));
	g_assert_false(test_menu_has_label(menu, "Close terminal"));
	item = test_menu_item_for_label(menu, "Close pane");
	g_assert_nonnull(item);
	g_signal_emit_by_name(item, "activate");
	g_assert_cmpuint(page->panes->len, ==, 1);
	g_assert_true(sakura_page_at_page(1) == page);
	assert_workspace_consistent();
	gtk_widget_destroy(menu);

	teardown_workspace();
}


static void
test_restore_order_reconciliation(void)
{
	SakuraPage *first, *second, *third;
	gpointer cached_page, cached_tab;

	setup_workspace();
	first = sakura_page_at_page(0);
	second = sakura_page_at_page(1);
	third = sakura_page_at_page(2);

	/* Model a restore path that inserted GTK children correctly but left the
	 * persistence caches in snapshot order. */
	cached_page = g_ptr_array_remove_index(sakura.pages, 2);
	g_ptr_array_insert(sakura.pages, 0, cached_page);
	cached_tab = g_ptr_array_remove_index(sakura.tabs, 2);
	g_ptr_array_insert(sakura.tabs, 0, cached_tab);
	g_assert_true(sakura_page_at_page(0) == first);
	g_assert_true(sakura_page_at_page(1) == second);
	g_assert_true(sakura_page_at_page(2) == third);
	g_assert_true(sakura_notebook_sync_page_order());
	g_assert_true(g_ptr_array_index(sakura.pages, 0) == first);
	g_assert_true(g_ptr_array_index(sakura.pages, 1) == second);
	g_assert_true(g_ptr_array_index(sakura.pages, 2) == third);
	assert_workspace_consistent();
	teardown_workspace();
}


static void
test_remove_random_pane(SakuraPage *page, GRand *random)
{
	SakuraTab *pane;
	guint pane_index;

	if (page == NULL || page->panes == NULL || page->panes->len < 2)
		return;
	pane_index = g_rand_int_range(random, 0, page->panes->len);
	pane = g_ptr_array_index(page->panes, pane_index);
	if (pane == page->active_tab)
		page->active_tab = g_ptr_array_index(page->panes,
		                                     pane_index == 0 ? 1 : 0);
	if (page->tab_bar_tab == pane)
		page->tab_bar_tab = page->active_tab;
	g_assert_true(sakura_layout_remove_leaf_widgets(pane->layout_leaf));
	g_assert_true(sakura_layout_remove_leaf(pane->layout_leaf));
	g_assert_true(sakura_notebook_sync_page_order());
	if (sakura.active_tab == pane)
		sakura.active_tab = page->active_tab;
	g_free(pane->terminal_id);
	g_free(pane);
}


static void
test_seeded_workspace_operations(void)
{
	GRand *random = g_rand_new_with_seed(20260716);
	GPtrArray *detached = g_ptr_array_new();
	guint step;

	setup_workspace();
	for (step = 0; step < 120; step++) {
		guint count = sakura.pages->len;
		guint operation = g_rand_int_range(random, 0, 7);
		if (operation == 0 && count > 1) {
			guint from = g_rand_int_range(random, 0, count);
			guint to = g_rand_int_range(random, 0, count);
			GtkWidget *child = gtk_notebook_get_nth_page(
				GTK_NOTEBOOK(sakura.notebook), (gint)from);
			gtk_notebook_reorder_child(GTK_NOTEBOOK(sakura.notebook), child, (gint)to);
			g_assert_true(sakura_notebook_sync_page_order());
		} else if (operation == 1) {
			guint target = g_rand_int_range(random, 0, count);
			SakuraPage *page = sakura_page_at_page((gint)target);
			sakura.active_page = page;
			sakura.active_tab = page->active_tab;
			gtk_notebook_set_current_page(GTK_NOTEBOOK(sakura.notebook), (gint)target);
		} else if (operation == 2) {
			guint target = g_rand_int_range(random, 0, count);
			SakuraPage *page = sakura_page_at_page((gint)target);
			if (page->panes->len < 4) {
				SakuraTab *pane = g_new0(SakuraTab, 1);
				pane->terminal_id = g_strdup_printf("random-pane-%u", step);
				pane->hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
				g_assert_true(sakura_layout_split_node_widgets(
					page->active_tab->layout_leaf,
					g_rand_int_range(random, 0, 2) == 0
					? SAKURA_SPLIT_RIGHT : SAKURA_SPLIT_DOWN,
					pane));
			}
		} else if (operation == 3) {
			guint target = g_rand_int_range(random, 0, count);
			SakuraPage *page = sakura_page_at_page((gint)target);
			if (page->panes->len > 1)
				test_remove_random_pane(page, random);
		} else if (operation == 4 && count > 1) {
			guint target = g_rand_int_range(random, 0, count);
			SakuraPage *page = sakura_page_at_page((gint)target);
			if (page == sakura.active_page)
				continue;
			g_assert_true(sakura_notebook_detach_page(page));
			g_ptr_array_add(detached, page);
		} else if (operation == 5 && count > 1) {
			/* Exercise the restore path's cache reconciliation: GTK remains the
			 * source of truth while the arrays briefly hold a stale order. */
			gpointer cached_page = g_ptr_array_remove_index(sakura.pages, count - 1);
			gpointer cached_tab = g_ptr_array_remove_index(sakura.tabs, count - 1);
			g_ptr_array_insert(sakura.pages, 0, cached_page);
			g_ptr_array_insert(sakura.tabs, 0, cached_tab);
			g_assert_true(sakura_notebook_sync_page_order());
		} else if (operation == 6) {
			SakuraPage *page = sakura_page_at_page(
				g_rand_int_range(random, 0, count));
			SakuraTab *pane = g_ptr_array_index(
				page->panes, g_rand_int_range(random, 0, page->panes->len));
			page->active_tab = pane;
			sakura.active_page = page;
			sakura.active_tab = pane;
			gtk_notebook_set_current_page(GTK_NOTEBOOK(sakura.notebook),
			                              sakura_page_for_tab(pane));
		}
		assert_workspace_consistent();
	}

	for (step = 0; step < detached->len; step++)
		test_page_free(g_ptr_array_index(detached, step));
	g_ptr_array_unref(detached);
	g_rand_free(random);
	teardown_workspace();
}


int
main(int argc, char **argv)
{
	gtk_test_init(&argc, &argv, NULL);
	g_test_add_func("/workspace/notebook-identity-reorder",
	                test_notebook_identity_and_reorder);
	g_test_add_func("/workspace/leaf-widget-split", test_leaf_widget_can_split);
	g_test_add_func("/workspace/generated-page-id-avoids-existing",
	                test_generated_page_id_avoids_existing_page);
	g_test_add_func("/workspace/select-detach-identity",
	                test_select_and_detach_by_identity);
	g_test_add_func("/workspace/snapshot-destroy-restore",
	                test_snapshot_destroy_restore_equivalence);
	g_test_add_func("/workspace/sidebar-directory-subtitles",
	                test_sidebar_hides_redundant_directory);
	g_test_add_func("/workspace/sidebar-pulses-nested-rows",
	                test_sidebar_pulses_nested_rows);
	g_test_add_func("/workspace/sidebar-selects-created-tab",
	                test_sidebar_selects_created_tab);
	g_test_add_func("/workspace/reconciles-at-outer-mutation-boundary",
	                test_workspace_reconciles_at_outer_mutation_boundary);
	g_test_add_func("/workspace/sidebar-selection-priority",
	                test_sidebar_selection_priority);
	g_test_add_func("/workspace/close-active-page-preserves-group",
	                test_close_active_page_preserves_group_scope);
	g_test_add_func("/workspace/select-terminal-switches-group-scope",
	                test_selecting_terminal_switches_group_scope);
	g_test_add_func("/workspace/sidebar-collapses-split-session-panes",
	                test_sidebar_collapses_split_session_panes);
	g_test_add_func("/workspace/sidebar-task-owns-page",
	                test_sidebar_task_owns_page);
	g_test_add_func("/workspace/task-model-survives-sidebar-projection",
	                test_task_model_survives_sidebar_projection);
	g_test_add_func("/workspace/group-model-survives-sidebar-projection",
	                test_group_model_survives_sidebar_projection);
	g_test_add_func("/workspace/sidebar-rebuilds-nested-model-projection",
	                test_sidebar_rebuilds_nested_model_projection);
	g_test_add_func("/workspace/sidebar-order-snapshot-roundtrip",
	                test_sidebar_order_survives_snapshot_roundtrip);
	g_test_add_func("/workspace/sidebar-creation-parent-context",
	                test_sidebar_creation_parent_preserves_context);
	g_test_add_func("/workspace/sidebar-move-page-parent",
	                test_sidebar_move_page_preserves_whole_page_parent);
	g_test_add_func("/workspace/sidebar-page-context-menu-close-label",
	                test_sidebar_page_context_menu_names_page_close);
	g_test_add_func("/workspace/sidebar-terminal-context-menu-close-pane",
	                test_sidebar_terminal_context_menu_closes_only_pane);
	g_test_add_func("/workspace/restore-order-reconciliation",
	                test_restore_order_reconciliation);
	g_test_add_func("/workspace/seeded-operations",
	                test_seeded_workspace_operations);
	return g_test_run();
}
