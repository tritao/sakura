#include "sakura-private.h"

#include <math.h>

#define SAKURA_LAYOUT_MIN_RATIO 0.05
#define SAKURA_LAYOUT_MAX_RATIO 0.95
#define SAKURA_LAYOUT_MAX_DEPTH 32

static guint next_layout_id;


static gboolean
sakura_page_id_in_use(const gchar *id)
{
#ifndef SAKURA_CORE_TEST
	guint index;

	if (id == NULL || sakura.pages == NULL)
		return FALSE;
	for (index = 0; index < sakura.pages->len; index++) {
		SakuraPage *page = g_ptr_array_index(sakura.pages, index);
		if (page != NULL && g_strcmp0(page->id, id) == 0)
			return TRUE;
	}
#else
	(void)id;
#endif
	return FALSE;
}


static gchar *
sakura_page_new_id(void)
{
	static guint next_page_id;
	gchar *id;

	do {
		id = g_strdup_printf("page-%u", ++next_page_id);
		if (!sakura_page_id_in_use(id))
			return id;
		g_free(id);
	} while (TRUE);
}


void
sakura_layout_paned_position_cb(GObject *object, GParamSpec *pspec, gpointer data)
{
	SakuraLayoutNode *node = data;
	GtkAllocation allocation;
	gint available, position;
	gdouble ratio;

	(void)pspec;
	if (node == NULL || node->kind != SAKURA_LAYOUT_SPLIT ||
	    !GTK_IS_PANED(object))
		return;
	gtk_widget_get_allocation(GTK_WIDGET(object), &allocation);
	available = node->data.split.direction == SAKURA_SPLIT_RIGHT
	          ? allocation.width : allocation.height;
	position = gtk_paned_get_position(GTK_PANED(object));
	if (available <= 0 || position <= 0)
		return;
	ratio = (gdouble)position / (gdouble)available;
	if (ratio >= 0.05 && ratio <= 0.95 && isfinite(ratio) &&
	    fabs(node->data.split.ratio - ratio) > 0.001) {
		node->data.split.ratio = ratio;
	#ifndef SAKURA_CORE_TEST
		sakura_session_mark_dirty();
	#endif
	}
}

static gchar *
sakura_layout_new_id(void)
{
	return g_strdup_printf("layout-%u", ++next_layout_id);
}


static gdouble
sakura_layout_normalize_ratio(gdouble ratio)
{
	if (!isfinite(ratio) || ratio < SAKURA_LAYOUT_MIN_RATIO ||
	    ratio > SAKURA_LAYOUT_MAX_RATIO)
		return 0.5;
	return ratio;
}


void
sakura_layout_set_ratio(SakuraLayoutNode *split, gdouble ratio)
{
	GtkAllocation allocation;
	gint available;
	gint position;

	if (split == NULL || split->kind != SAKURA_LAYOUT_SPLIT)
		return;
	split->data.split.ratio = sakura_layout_normalize_ratio(ratio);
	if (split->widget == NULL || !GTK_IS_PANED(split->widget))
		return;
	gtk_widget_get_allocation(split->widget, &allocation);
	available = split->data.split.direction == SAKURA_SPLIT_RIGHT
	          ? allocation.width : allocation.height;
	if (available <= 0)
		return;
	position = (gint)(available * split->data.split.ratio);
	gtk_paned_set_position(GTK_PANED(split->widget), position);
}


static SakuraLayoutNode *
sakura_layout_node_new(SakuraPage *page, SakuraLayoutKind kind)
{
	SakuraLayoutNode *node = g_new0(SakuraLayoutNode, 1);

	node->id = sakura_layout_new_id();
	node->kind = kind;
	node->page = page;
	return node;
}


static void
sakura_layout_node_free_shallow(SakuraLayoutNode *node)
{
	if (node == NULL)
		return;

	g_free(node->id);
	g_free(node);
}


static void
sakura_layout_node_free_recursive(SakuraLayoutNode *node)
{
	if (node == NULL)
		return;

	if (node->kind == SAKURA_LAYOUT_SPLIT) {
		sakura_layout_node_free_recursive(node->data.split.first);
		sakura_layout_node_free_recursive(node->data.split.second);
	} else if (node->data.leaf.tab != NULL &&
	           node->data.leaf.tab->layout_leaf == node) {
		node->data.leaf.tab->layout_leaf = NULL;
		node->data.leaf.tab->page = NULL;
	}

	sakura_layout_node_free_shallow(node);
}


SakuraPage *
sakura_page_new(const gchar *id)
{
	SakuraPage *page = g_new0(SakuraPage, 1);

	page->id = id != NULL && id[0] != '\0'
	        ? g_strdup(id)
	        : sakura_page_new_id();
	page->panes = g_ptr_array_new();
	return page;
}


void
sakura_page_free(SakuraPage *page)
{
	if (page == NULL)
		return;

	sakura_layout_node_free_recursive(page->layout_root);
	g_clear_pointer(&page->panes, g_ptr_array_unref);
	g_free(page->id);
	g_free(page->title);
	g_free(page->last_active_terminal_id);
	g_free(page);
}


GtkWidget *
sakura_page_widget_for_tab(SakuraTab *tab)
{
	if (tab == NULL)
		return NULL;
	if (tab->page != NULL && tab->page->container != NULL)
		return tab->page->container;
	return tab->hbox;
}


SakuraLayoutNode *
sakura_layout_leaf_new(SakuraPage *page, SakuraTab *tab)
{
	SakuraLayoutNode *node;

	if (page == NULL || tab == NULL)
		return NULL;
	if (tab->page != NULL || tab->layout_leaf != NULL)
		return NULL;

	node = sakura_layout_node_new(page, SAKURA_LAYOUT_LEAF);
	node->data.leaf.tab = tab;
	tab->page = page;
	tab->layout_leaf = node;
	g_ptr_array_add(page->panes, tab);
	if (page->layout_root == NULL)
		page->layout_root = node;
	if (page->active_tab == NULL)
		page->active_tab = tab;
	return node;
}


SakuraLayoutNode *
sakura_layout_split_new(SakuraPage *page,
                       SakuraSplitDirection direction,
                       gdouble ratio,
                       SakuraLayoutNode *first,
                       SakuraLayoutNode *second)
{
	SakuraLayoutNode *node;

	if (page == NULL || first == NULL || second == NULL ||
	    first == second || first->parent != NULL || second->parent != NULL)
		return NULL;
	if (first->page != page || second->page != page)
		return NULL;

	node = sakura_layout_node_new(page, SAKURA_LAYOUT_SPLIT);
	node->data.split.direction = direction;
	node->data.split.ratio = sakura_layout_normalize_ratio(ratio);
	node->data.split.first = first;
	node->data.split.second = second;
	first->parent = node;
	second->parent = node;
	return node;
}


gboolean
sakura_layout_split_leaf(SakuraLayoutNode *leaf,
                         SakuraSplitDirection direction,
                         SakuraTab *new_tab)
{
	SakuraLayoutNode *new_leaf, *split, *old_parent;
	SakuraPage *page;

	if (leaf == NULL || leaf->kind != SAKURA_LAYOUT_LEAF || new_tab == NULL)
		return FALSE;
	page = leaf->page;
	old_parent = leaf->parent;
	if (old_parent != NULL && old_parent->kind != SAKURA_LAYOUT_SPLIT)
		return FALSE;
	new_leaf = sakura_layout_leaf_new(page, new_tab);
	if (new_leaf == NULL)
		return FALSE;

	split = sakura_layout_node_new(page, SAKURA_LAYOUT_SPLIT);
	if (split == NULL) {
		g_ptr_array_remove_fast(page->panes, new_tab);
		new_tab->page = NULL;
		new_tab->layout_leaf = NULL;
		sakura_layout_node_free_shallow(new_leaf);
		return FALSE;
	}
	split->data.split.direction = direction;
	split->data.split.ratio = 0.5;
	split->data.split.first = leaf;
	split->data.split.second = new_leaf;
	if (old_parent == NULL) {
		if (page->layout_root != leaf) {
			g_ptr_array_remove_fast(page->panes, new_tab);
			new_tab->page = NULL;
			new_tab->layout_leaf = NULL;
			sakura_layout_node_free_shallow(new_leaf);
			sakura_layout_node_free_shallow(split);
			return FALSE;
		}
		page->layout_root = split;
	} else if (old_parent->data.split.first == leaf) {
		old_parent->data.split.first = split;
		split->parent = old_parent;
	} else if (old_parent->data.split.second == leaf) {
		old_parent->data.split.second = split;
		split->parent = old_parent;
	} else {
		g_ptr_array_remove_fast(page->panes, new_tab);
		new_tab->page = NULL;
		new_tab->layout_leaf = NULL;
		sakura_layout_node_free_shallow(new_leaf);
		sakura_layout_node_free_shallow(split);
		return FALSE;
	}
	leaf->parent = split;
	new_leaf->parent = split;
	return TRUE;
}


gboolean
sakura_layout_split_leaf_widgets(SakuraLayoutNode *leaf,
                                 SakuraSplitDirection direction,
                                 SakuraTab *new_tab)
{
	SakuraLayoutNode *old_parent, *split, *new_leaf;
	SakuraPage *page;
	GtkWidget *old_widget, *paned;
	gboolean old_is_first = FALSE;

	if (leaf == NULL || leaf->kind != SAKURA_LAYOUT_LEAF ||
	    leaf->widget == NULL || new_tab == NULL || new_tab->hbox == NULL)
		return FALSE;
	page = leaf->page;
	old_parent = leaf->parent;
	old_widget = leaf->widget;
	if (old_parent != NULL) {
		if (old_parent->kind != SAKURA_LAYOUT_SPLIT || old_parent->widget == NULL)
			return FALSE;
		old_is_first = old_parent->data.split.first == leaf;
		if (!old_is_first && old_parent->data.split.second != leaf)
			return FALSE;
	}

	if (!sakura_layout_split_leaf(leaf, direction, new_tab))
		return FALSE;
	split = leaf->parent;
	new_leaf = split != NULL && split->kind == SAKURA_LAYOUT_SPLIT
	         ? split->data.split.second : NULL;
	if (split == NULL || new_leaf == NULL) {
		/* This should be unreachable after a successful model operation. */
		return FALSE;
	}

	paned = gtk_paned_new(direction == SAKURA_SPLIT_RIGHT
	                     ? GTK_ORIENTATION_HORIZONTAL
	                     : GTK_ORIENTATION_VERTICAL);
	gtk_widget_set_hexpand(paned, TRUE);
	gtk_widget_set_vexpand(paned, TRUE);

	/* Hold the old surface while removing it from its current container. */
	g_object_ref(old_widget);
	if (old_parent == NULL) {
		gtk_container_remove(GTK_CONTAINER(page->container), old_widget);
		gtk_box_pack_start(GTK_BOX(page->container), paned, TRUE, TRUE, 0);
	} else {
		gtk_container_remove(GTK_CONTAINER(old_parent->widget), old_widget);
		if (old_is_first)
			gtk_paned_pack1(GTK_PANED(old_parent->widget), paned, TRUE, FALSE);
		else
			gtk_paned_pack2(GTK_PANED(old_parent->widget), paned, TRUE, FALSE);
	}
	gtk_paned_pack1(GTK_PANED(paned), old_widget, TRUE, FALSE);
	gtk_paned_pack2(GTK_PANED(paned), new_tab->hbox, TRUE, FALSE);
	g_object_unref(old_widget);

	leaf->widget = old_widget;
	new_leaf->widget = new_tab->hbox;
	split->widget = paned;
	g_signal_connect(paned, "notify::position",
	                 G_CALLBACK(sakura_layout_paned_position_cb), split);
	gtk_widget_show_all(paned);
	return TRUE;
}


gboolean
sakura_layout_split_node_widgets(SakuraLayoutNode *node,
                                 SakuraSplitDirection direction,
                                 SakuraTab *new_tab)
{
	SakuraLayoutNode *old_parent, *split, *new_leaf;
	SakuraPage *page;
	GtkWidget *old_widget, *paned;
	gboolean old_is_first = FALSE;

	if (node == NULL || node->page == NULL || node->widget == NULL ||
	    new_tab == NULL || new_tab->hbox == NULL)
		return FALSE;
	page = node->page;
	old_parent = node->parent;
	if (old_parent != NULL) {
		if (old_parent->kind != SAKURA_LAYOUT_SPLIT || old_parent->widget == NULL)
			return FALSE;
		old_is_first = old_parent->data.split.first == node;
		if (!old_is_first && old_parent->data.split.second != node)
			return FALSE;
	}
	if (node->kind == SAKURA_LAYOUT_LEAF) {
		if (!sakura_layout_split_leaf(node, direction, new_tab))
			return FALSE;
	} else {
		new_leaf = sakura_layout_leaf_new(page, new_tab);
		if (new_leaf == NULL)
			return FALSE;
		split = sakura_layout_node_new(page, SAKURA_LAYOUT_SPLIT);
		if (split == NULL) {
			g_ptr_array_remove_fast(page->panes, new_tab);
			new_tab->page = NULL;
			new_tab->layout_leaf = NULL;
			sakura_layout_node_free_shallow(new_leaf);
			return FALSE;
		}
		split->data.split.direction = direction;
		split->data.split.ratio = 0.5;
		split->data.split.first = node;
		split->data.split.second = new_leaf;
		if (old_parent == NULL) {
			if (page->layout_root != node) {
				g_ptr_array_remove_fast(page->panes, new_tab);
				new_tab->page = NULL;
				new_tab->layout_leaf = NULL;
				sakura_layout_node_free_shallow(new_leaf);
				sakura_layout_node_free_shallow(split);
				return FALSE;
			}
			page->layout_root = split;
		} else if (old_is_first) {
			old_parent->data.split.first = split;
			split->parent = old_parent;
		} else {
			old_parent->data.split.second = split;
			split->parent = old_parent;
		}
		node->parent = split;
		new_leaf->parent = split;
	}

	/* In the leaf case the model operation has already populated split/new_leaf. */
	split = new_tab->layout_leaf != NULL ? new_tab->layout_leaf->parent : NULL;
	new_leaf = new_tab->layout_leaf;
	if (split == NULL || new_leaf == NULL)
		return FALSE;
	old_widget = node->widget;
	paned = gtk_paned_new(direction == SAKURA_SPLIT_RIGHT
	                     ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL);
	gtk_widget_set_hexpand(paned, TRUE);
	gtk_widget_set_vexpand(paned, TRUE);
	g_object_ref(old_widget);
	if (old_parent == NULL) {
		gtk_container_remove(GTK_CONTAINER(page->container), old_widget);
		gtk_box_pack_start(GTK_BOX(page->container), paned, TRUE, TRUE, 0);
	} else {
		gtk_container_remove(GTK_CONTAINER(old_parent->widget), old_widget);
		if (old_is_first)
			gtk_paned_pack1(GTK_PANED(old_parent->widget), paned, TRUE, FALSE);
		else
			gtk_paned_pack2(GTK_PANED(old_parent->widget), paned, TRUE, FALSE);
	}
	gtk_paned_pack1(GTK_PANED(paned), old_widget, TRUE, FALSE);
	gtk_paned_pack2(GTK_PANED(paned), new_tab->hbox, TRUE, FALSE);
	g_object_unref(old_widget);
	node->widget = old_widget;
	new_leaf->widget = new_tab->hbox;
	split->widget = paned;
	g_signal_connect(paned, "notify::position",
	                 G_CALLBACK(sakura_layout_paned_position_cb), split);
	gtk_widget_show_all(paned);
	return TRUE;
}


static SakuraLayoutNode *
sakura_layout_first_leaf(SakuraLayoutNode *node)
{
	if (node == NULL)
		return NULL;
	while (node->kind == SAKURA_LAYOUT_SPLIT)
		node = node->data.split.first;
	return node;
}


static gboolean
sakura_layout_node_contains(const SakuraLayoutNode *node,
                            const SakuraLayoutNode *target)
{
	if (node == NULL || target == NULL)
		return FALSE;
	if (node == target)
		return TRUE;
	return node->kind == SAKURA_LAYOUT_SPLIT &&
	       (sakura_layout_node_contains(node->data.split.first, target) ||
	        sakura_layout_node_contains(node->data.split.second, target));
}


static void
sakura_layout_set_visible(SakuraLayoutNode *node,
                          SakuraLayoutNode *target)
{
	if (node == NULL)
		return;
	if (node->kind == SAKURA_LAYOUT_LEAF) {
		if (node->widget != NULL)
			gtk_widget_set_visible(node->widget, node == target);
		return;
	}
	if (sakura_layout_node_contains(node->data.split.first, target)) {
		if (node->data.split.second != NULL && node->data.split.second->widget != NULL)
			gtk_widget_hide(node->data.split.second->widget);
		sakura_layout_set_visible(node->data.split.first, target);
	} else {
		if (node->data.split.first != NULL && node->data.split.first->widget != NULL)
			gtk_widget_hide(node->data.split.first->widget);
		sakura_layout_set_visible(node->data.split.second, target);
	}
}


void
sakura_layout_set_zoomed(SakuraPage *page, SakuraTab *tab, gboolean zoomed)
{
	if (page == NULL || page->layout_root == NULL)
		return;
	page->zoomed = zoomed;
	if (!zoomed) {
		gtk_widget_show_all(page->layout_root->widget != NULL
		                   ? page->layout_root->widget : page->container);
		return;
	}
	if (tab == NULL || tab->page != page || tab->layout_leaf == NULL)
		return;
	sakura_layout_set_visible(page->layout_root, tab->layout_leaf);
}


gboolean
sakura_layout_remove_leaf_widgets(SakuraLayoutNode *leaf)
{
	SakuraLayoutNode *parent, *grandparent, *sibling;
	GtkWidget *parent_widget, *sibling_widget, *container;
	gboolean sibling_is_first;

	if (leaf == NULL || leaf->kind != SAKURA_LAYOUT_LEAF ||
	    leaf->page == NULL || leaf->parent == NULL)
		return FALSE;
	parent = leaf->parent;
	if (parent->kind != SAKURA_LAYOUT_SPLIT || parent->widget == NULL)
		return FALSE;
	sibling_is_first = parent->data.split.first == leaf;
	sibling = sibling_is_first ? parent->data.split.second : parent->data.split.first;
	if (sibling == NULL || sibling->widget == NULL)
		return FALSE;
	grandparent = parent->parent;
	parent_widget = parent->widget;
	sibling_widget = sibling->widget;

	/* Keep the surviving surface alive while the split widget is removed. */
	g_object_ref(sibling_widget);
	/* Detach the surviving branch first. Removing the parent paned directly
	 * would destroy a nested GtkPaned together with its children, leaving the
	 * model pointing at an empty widget when that branch is reparented. */
	gtk_container_remove(GTK_CONTAINER(parent_widget), sibling_widget);
	if (grandparent == NULL) {
		container = leaf->page->container;
		gtk_container_remove(GTK_CONTAINER(container), parent_widget);
		gtk_box_pack_start(GTK_BOX(container), sibling_widget, TRUE, TRUE, 0);
	} else {
		if (grandparent->kind != SAKURA_LAYOUT_SPLIT ||
		    grandparent->widget == NULL ||
		    (grandparent->data.split.first != parent &&
	     grandparent->data.split.second != parent)) {
			g_object_unref(sibling_widget);
			return FALSE;
		}
		container = grandparent->widget;
		gtk_container_remove(GTK_CONTAINER(container), parent_widget);
		if (grandparent->data.split.first == parent)
			gtk_paned_pack1(GTK_PANED(container), sibling_widget, TRUE, FALSE);
		else
			gtk_paned_pack2(GTK_PANED(container), sibling_widget, TRUE, FALSE);
	}
	g_object_unref(sibling_widget);
	return TRUE;
}


gboolean
sakura_layout_remove_leaf(SakuraLayoutNode *leaf)
{
	SakuraLayoutNode *parent, *sibling, *grandparent;
	SakuraPage *page;
	SakuraTab *tab;

	if (leaf == NULL || leaf->kind != SAKURA_LAYOUT_LEAF || leaf->page == NULL)
		return FALSE;
	page = leaf->page;
	parent = leaf->parent;
	tab = leaf->data.leaf.tab;
	if (parent == NULL) {
		if (page->layout_root != leaf)
			return FALSE;
		page->layout_root = NULL;
		page->active_tab = NULL;
		if (tab != NULL && tab->layout_leaf == leaf) {
			if (page->panes != NULL)
				g_ptr_array_remove_fast(page->panes, tab);
			tab->layout_leaf = NULL;
			tab->page = NULL;
		}
		sakura_layout_node_free_shallow(leaf);
		return TRUE;
	}
	if (parent->kind != SAKURA_LAYOUT_SPLIT)
		return FALSE;

	sibling = parent->data.split.first == leaf
	         ? parent->data.split.second
	         : parent->data.split.first;
	if (sibling == NULL)
		return FALSE;
	grandparent = parent->parent;
	if (grandparent == NULL) {
		if (page->layout_root != parent)
			return FALSE;
		page->layout_root = sibling;
		sibling->parent = NULL;
	} else {
		if (grandparent->kind != SAKURA_LAYOUT_SPLIT)
			return FALSE;
		if (grandparent->data.split.first == parent)
			grandparent->data.split.first = sibling;
		else if (grandparent->data.split.second == parent)
			grandparent->data.split.second = sibling;
		else
			return FALSE;
		sibling->parent = grandparent;
	}

	if (tab != NULL && tab->layout_leaf == leaf) {
		if (page->panes != NULL)
			g_ptr_array_remove_fast(page->panes, tab);
		tab->layout_leaf = NULL;
		tab->page = NULL;
	}
	if (page->active_tab == tab)
		page->active_tab = sakura_layout_first_leaf(sibling)->data.leaf.tab;
	parent->data.split.first = NULL;
	parent->data.split.second = NULL;
	sakura_layout_node_free_shallow(parent);
	sakura_layout_node_free_shallow(leaf);
	return TRUE;
}


gboolean
sakura_layout_contains_tab(const SakuraLayoutNode *node, const SakuraTab *tab)
{
	if (node == NULL || tab == NULL)
		return FALSE;
	if (node->kind == SAKURA_LAYOUT_LEAF)
		return node->data.leaf.tab == tab;
	return sakura_layout_contains_tab(node->data.split.first, tab) ||
	       sakura_layout_contains_tab(node->data.split.second, tab);
}


guint
sakura_layout_tab_count(const SakuraLayoutNode *node)
{
	if (node == NULL)
		return 0;
	if (node->kind == SAKURA_LAYOUT_LEAF)
		return 1;
	return sakura_layout_tab_count(node->data.split.first) +
	       sakura_layout_tab_count(node->data.split.second);
}


void
sakura_layout_foreach_tab(const SakuraLayoutNode *node,
                          GFunc callback,
                          gpointer user_data)
{
	if (node == NULL || callback == NULL)
		return;
	if (node->kind == SAKURA_LAYOUT_LEAF) {
		callback(node->data.leaf.tab, user_data);
		return;
	}
	sakura_layout_foreach_tab(node->data.split.first, callback, user_data);
	sakura_layout_foreach_tab(node->data.split.second, callback, user_data);
}


static gboolean
sakura_layout_validate_node(const SakuraLayoutNode *node,
                            const SakuraPage *page,
                            GHashTable *seen,
                            guint depth,
                            GError **error)
{
	if (node == NULL)
		return g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
	                       "layout contains a missing node"), FALSE;
	if (depth > SAKURA_LAYOUT_MAX_DEPTH)
		return g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
	                       "layout exceeds maximum depth"), FALSE;
	if (node->page != page)
		return g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
	                       "layout node belongs to another page"), FALSE;
	if (g_hash_table_contains(seen, node))
		return g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
	                       "layout contains a cycle or duplicate node"), FALSE;
	g_hash_table_add(seen, (gpointer)node);

	if (node->kind == SAKURA_LAYOUT_LEAF) {
		if (node->data.leaf.tab == NULL || node->data.leaf.tab->page != page ||
		    node->data.leaf.tab->layout_leaf != node)
			return g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
		                       "layout leaf has an invalid terminal"), FALSE;
		return TRUE;
	}
	if (node->kind != SAKURA_LAYOUT_SPLIT ||
	    node->data.split.first == NULL || node->data.split.second == NULL ||
	    node->data.split.first == node->data.split.second ||
	    node->data.split.first->parent != node ||
	    node->data.split.second->parent != node ||
	    !isfinite(node->data.split.ratio) ||
	    node->data.split.ratio < SAKURA_LAYOUT_MIN_RATIO ||
	    node->data.split.ratio > SAKURA_LAYOUT_MAX_RATIO)
		return g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
		                   "layout split is invalid"), FALSE;

	return sakura_layout_validate_node(node->data.split.first, page, seen,
	                                    depth + 1, error) &&
	       sakura_layout_validate_node(node->data.split.second, page, seen,
	                                    depth + 1, error);
}


gboolean
sakura_layout_validate(const SakuraPage *page, GError **error)
{
	GHashTable *seen;
	gboolean valid;

	if (page == NULL || page->layout_root == NULL) {
		g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
		            "page has no layout root");
		return FALSE;
	}
	if (page->layout_root->parent != NULL) {
		g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
		            "layout root has a parent");
		return FALSE;
	}
	seen = g_hash_table_new(g_direct_hash, g_direct_equal);
	valid = sakura_layout_validate_node(page->layout_root, page, seen, 0, error);
	g_hash_table_destroy(seen);
	return valid;
}
