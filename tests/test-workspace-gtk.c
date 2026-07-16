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
	g_assert_nonnull(sakura_layout_leaf_new(page, tab));
	tab->layout_leaf->widget = tab->hbox;
	gtk_box_pack_start(GTK_BOX(page->container), tab->hbox, TRUE, TRUE, 0);
	gtk_widget_show_all(page->container);
	page->tab_bar_tab = tab;
	return page;
}


static void
test_page_free(SakuraPage *page)
{
	SakuraTab *tab;

	if (page == NULL)
		return;
	tab = page->tab_bar_tab;
	sakura_page_free(page);
	if (tab != NULL) {
		g_free(tab->terminal_id);
		g_free(tab);
	}
}


static void
assert_workspace_consistent(void)
{
	gint count, index, current;

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

	g_free(new_tab->terminal_id);
	g_free(new_tab);
	teardown_workspace();
}


int
main(int argc, char **argv)
{
	gtk_test_init(&argc, &argv, NULL);
	g_test_add_func("/workspace/notebook-identity-reorder",
	                test_notebook_identity_and_reorder);
	g_test_add_func("/workspace/leaf-widget-split", test_leaf_widget_can_split);
	return g_test_run();
}
