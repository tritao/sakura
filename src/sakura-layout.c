#include "sakura-private.h"

#include <math.h>

#define SAKURA_LAYOUT_MIN_RATIO 0.05
#define SAKURA_LAYOUT_MAX_RATIO 0.95
#define SAKURA_LAYOUT_MAX_DEPTH 32

static guint next_layout_id;

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
	static guint next_page_id;
	SakuraPage *page = g_new0(SakuraPage, 1);

	page->id = id != NULL && id[0] != '\0'
	        ? g_strdup(id)
	        : g_strdup_printf("page-%u", ++next_page_id);
	return page;
}


void
sakura_page_free(SakuraPage *page)
{
	if (page == NULL)
		return;

	sakura_layout_node_free_recursive(page->layout_root);
	g_free(page->id);
	g_free(page->title);
	g_free(page->last_active_terminal_id);
	g_free(page);
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
	if (page->layout_root == NULL)
		page->layout_root = node;
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
		tab->layout_leaf = NULL;
		tab->page = NULL;
	}
	if (page->active_tab == tab)
		page->active_tab = sibling->kind == SAKURA_LAYOUT_LEAF
		                 ? sibling->data.leaf.tab : NULL;
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
