#include <math.h>

#include "sakura-core.h"

/* GTK-free session snapshot parsing, validation, and serialization. The
 * desktop session module retains only locking, save scheduling, and GTK
 * lifecycle policy; this file is the part a future agent can reuse. */

static gboolean
sakura_session_error(GError **error, GKeyFileError code, gchar *message)
{
	if (error != NULL)
		g_set_error_literal(error, G_KEY_FILE_ERROR, code, message);
	g_free(message);
	return FALSE;
}


static gboolean
sakura_session_group_ids_valid(const SakuraSessionSnapshot *snapshot,
                               GError **error)
{
	GHashTable *groups;
	guint index;

	groups = g_hash_table_new(g_str_hash, g_str_equal);
	for (index = 0; index < snapshot->groups->len; index++) {
		SakuraSessionGroupRecord *group = g_ptr_array_index(snapshot->groups, index);

		if (group->id == NULL || group->id[0] == '\0' ||
		    g_strcmp0(group->id, "root") == 0 ||
		    g_hash_table_contains(groups, group->id)) {
			g_hash_table_destroy(groups);
			return sakura_session_error(error, G_KEY_FILE_ERROR_INVALID_VALUE,
		                                g_strdup_printf("invalid or duplicate group id: %s",
	                                                 group->id != NULL ? group->id : "(null)"));
		}
		g_hash_table_add(groups, group->id);
	}

	for (index = 0; index < snapshot->groups->len; index++) {
		SakuraSessionGroupRecord *group = g_ptr_array_index(snapshot->groups, index);
		GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);
		const gchar *parent = group->parent_id;

		while (parent != NULL && parent[0] != '\0' && g_strcmp0(parent, "root") != 0) {
			SakuraSessionGroupRecord *parent_group;

			if (g_hash_table_contains(seen, parent)) {
				g_hash_table_destroy(seen);
				g_hash_table_destroy(groups);
				return sakura_session_error(error, G_KEY_FILE_ERROR_INVALID_VALUE,
				                            g_strdup_printf("cycle in group parents at: %s", parent));
			}
			g_hash_table_add(seen, (gpointer)parent);
			parent_group = NULL;
			for (guint parent_index = 0; parent_index < snapshot->groups->len;
			     parent_index++) {
				SakuraSessionGroupRecord *candidate =
					g_ptr_array_index(snapshot->groups, parent_index);
				if (g_strcmp0(candidate->id, parent) == 0) {
					parent_group = candidate;
					break;
				}
			}
			if (parent_group == NULL) {
				g_hash_table_destroy(seen);
				g_hash_table_destroy(groups);
				return sakura_session_error(error, G_KEY_FILE_ERROR_INVALID_VALUE,
				                            g_strdup_printf("group points to missing parent: %s", parent));
			}
			parent = parent_group->parent_id;
		}
		g_hash_table_destroy(seen);
	}

	g_hash_table_destroy(groups);
	return TRUE;
}


static gboolean
sakura_session_terminal_ids_valid(const SakuraSessionSnapshot *snapshot,
                                  GError **error)
{
	GHashTable *ids = g_hash_table_new(g_str_hash, g_str_equal);
	GHashTable *groups = g_hash_table_new(g_str_hash, g_str_equal);
	GHashTable *tasks = g_hash_table_new(g_str_hash, g_str_equal);
	guint index;

	for (index = 0; index < snapshot->groups->len; index++) {
		SakuraSessionGroupRecord *group = g_ptr_array_index(snapshot->groups, index);
		g_hash_table_add(groups, group->id);
	}
	for (index = 0; index < snapshot->tasks->len; index++) {
		SakuraSessionTaskRecord *task = g_ptr_array_index(snapshot->tasks, index);
		g_hash_table_add(tasks, task->id);
	}
	for (index = 0; index < snapshot->tabs->len; index++) {
		SakuraSessionTabRecord *tab = g_ptr_array_index(snapshot->tabs, index);

		if (tab->parent_id != NULL && tab->parent_id[0] != '\0' &&
		    g_strcmp0(tab->parent_id, "root") != 0 &&
		    !g_hash_table_contains(groups, tab->parent_id) &&
		    !g_hash_table_contains(tasks, tab->parent_id)) {
			g_hash_table_destroy(ids);
			g_hash_table_destroy(groups);
			g_hash_table_destroy(tasks);
			return sakura_session_error(error, G_KEY_FILE_ERROR_INVALID_VALUE,
			                                g_strdup_printf("terminal points to missing parent: %s",
		                                                 tab->parent_id));
		}
		if (tab->terminal_id != NULL && tab->terminal_id[0] != '\0') {
			if (g_hash_table_contains(ids, tab->terminal_id)) {
				g_hash_table_destroy(ids);
				g_hash_table_destroy(groups);
				g_hash_table_destroy(tasks);
				return sakura_session_error(error, G_KEY_FILE_ERROR_INVALID_VALUE,
				                            g_strdup_printf("duplicate terminal id: %s", tab->terminal_id));
			}
			g_hash_table_add(ids, tab->terminal_id);
		}
	}

	g_hash_table_destroy(ids);
	g_hash_table_destroy(groups);
	g_hash_table_destroy(tasks);
	return TRUE;
}


static gboolean
sakura_session_task_ids_valid(const SakuraSessionSnapshot *snapshot,
                              GError **error)
{
	GHashTable *groups = g_hash_table_new(g_str_hash, g_str_equal);
	GHashTable *tasks = g_hash_table_new(g_str_hash, g_str_equal);
	guint index;

	for (index = 0; index < snapshot->groups->len; index++) {
		SakuraSessionGroupRecord *group = g_ptr_array_index(snapshot->groups, index);
		g_hash_table_add(groups, group->id);
	}
	for (index = 0; index < snapshot->tasks->len; index++) {
		SakuraSessionTaskRecord *task = g_ptr_array_index(snapshot->tasks, index);
		if (task->id == NULL || task->id[0] == '\0' ||
		    g_strcmp0(task->id, "root") == 0 ||
		    g_hash_table_contains(tasks, task->id) ||
		    g_hash_table_contains(groups, task->id)) {
			g_hash_table_destroy(groups);
			g_hash_table_destroy(tasks);
			return sakura_session_error(error, G_KEY_FILE_ERROR_INVALID_VALUE,
			                            g_strdup_printf("invalid or duplicate task id: %s",
			                                             task->id != NULL ? task->id : "(null)"));
		}
		if (task->group_id == NULL ||
		    (g_strcmp0(task->group_id, "root") != 0 &&
		     !g_hash_table_contains(groups, task->group_id))) {
			g_hash_table_destroy(groups);
			g_hash_table_destroy(tasks);
			return sakura_session_error(error, G_KEY_FILE_ERROR_INVALID_VALUE,
			                            g_strdup_printf("task points to missing group: %s",
			                                             task->group_id != NULL ? task->group_id : "(null)"));
		}
		g_hash_table_add(tasks, task->id);
	}

	for (index = 0; index < snapshot->tasks->len; index++) {
		SakuraSessionTaskRecord *task = g_ptr_array_index(snapshot->tasks, index);
		GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);
		const gchar *parent = task->parent_id;

		while (parent != NULL && parent[0] != '\0' &&
		       g_strcmp0(parent, "root") != 0 &&
		       !g_hash_table_contains(groups, parent)) {
			SakuraSessionTaskRecord *parent_task = NULL;

			if (g_hash_table_contains(seen, parent)) {
				g_hash_table_destroy(seen);
				g_hash_table_destroy(groups);
				g_hash_table_destroy(tasks);
				return sakura_session_error(error, G_KEY_FILE_ERROR_INVALID_VALUE,
				                            g_strdup_printf("cycle in task parents at: %s", parent));
			}
			g_hash_table_add(seen, (gpointer)parent);
			for (guint parent_index = 0; parent_index < snapshot->tasks->len;
			     parent_index++) {
				SakuraSessionTaskRecord *candidate =
					g_ptr_array_index(snapshot->tasks, parent_index);
				if (g_strcmp0(candidate->id, parent) == 0) {
					parent_task = candidate;
					break;
				}
			}
			if (parent_task == NULL) {
				g_hash_table_destroy(seen);
				g_hash_table_destroy(groups);
				g_hash_table_destroy(tasks);
				return sakura_session_error(error, G_KEY_FILE_ERROR_INVALID_VALUE,
				                            g_strdup_printf("task points to missing parent: %s", parent));
			}
			parent = parent_task->parent_id;
		}
		g_hash_table_destroy(seen);
	}

	g_hash_table_destroy(groups);
	g_hash_table_destroy(tasks);
	return TRUE;
}


static SakuraSessionLayoutRecord *
sakura_session_layout_record_by_id(const SakuraSessionSnapshot *snapshot,
                                   const gchar *id)
{
	guint index;

	if (snapshot == NULL || id == NULL)
		return NULL;
	for (index = 0; index < snapshot->layouts->len; index++) {
		SakuraSessionLayoutRecord *record =
			g_ptr_array_index(snapshot->layouts, index);
		if (record != NULL && g_strcmp0(record->id, id) == 0)
			return record;
	}
	return NULL;
}


static void
sakura_session_repair_page_layout_ids(const SakuraSessionSnapshot *snapshot,
                                      const gchar *layout_id,
                                      const gchar *page_id,
                                      GHashTable *seen)
{
	SakuraSessionLayoutRecord *record;

	if (snapshot == NULL || layout_id == NULL || page_id == NULL || seen == NULL ||
	    g_hash_table_contains(seen, layout_id))
		return;
	record = sakura_session_layout_record_by_id(snapshot, layout_id);
	if (record == NULL)
		return;
	g_hash_table_add(seen, (gpointer)record->id);
	g_free(record->page_id);
	record->page_id = g_strdup(page_id);
	if (g_strcmp0(record->type, "split") == 0) {
		sakura_session_repair_page_layout_ids(snapshot, record->first_id,
		                                      page_id, seen);
		sakura_session_repair_page_layout_ids(snapshot, record->second_id,
		                                      page_id, seen);
	}
}


static void
sakura_session_repair_duplicate_page_ids(SakuraSessionSnapshot *snapshot)
{
	GHashTable *used_ids;
	guint next_repaired_id = 0;
	guint index;

	if (snapshot == NULL || snapshot->pages == NULL || snapshot->pages->len == 0)
		return;
	used_ids = g_hash_table_new(g_str_hash, g_str_equal);
	for (index = 0; index < snapshot->pages->len; index++) {
		SakuraSessionPageRecord *page = g_ptr_array_index(snapshot->pages, index);
		gchar *new_id;

		if (page->id != NULL && page->id[0] != '\0' &&
		    !g_hash_table_contains(used_ids, page->id)) {
			g_hash_table_add(used_ids, page->id);
			continue;
		}

		do {
			new_id = g_strdup_printf("page-recovered-%u", ++next_repaired_id);
		} while (g_hash_table_contains(used_ids, new_id));
		g_debug("Repairing duplicate saved page id '%s' as '%s'",
		        page->id != NULL && page->id[0] != '\0' ? page->id : "(missing)",
		        new_id);
		g_free(page->id);
		page->id = new_id;
		g_hash_table_add(used_ids, page->id);
		{
			GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);
			sakura_session_repair_page_layout_ids(snapshot, page->root_layout_id,
			                                      page->id, seen);
			g_hash_table_destroy(seen);
		}
	}
	g_hash_table_destroy(used_ids);
}


static gboolean
sakura_session_layout_reachable(const SakuraSessionLayoutRecord *record,
                                const gchar *page_id,
                                GHashTable *layouts,
                                GHashTable *seen,
                                guint depth)
{
	SakuraSessionLayoutRecord *first, *second;

	if (record == NULL || page_id == NULL || layouts == NULL || seen == NULL ||
	    depth > SAKURA_LAYOUT_MAX_DEPTH ||
	    g_strcmp0(record->page_id, page_id) != 0 ||
	    g_hash_table_contains(seen, record->id))
		return FALSE;
	g_hash_table_add(seen, record->id);
	if (g_strcmp0(record->type, "leaf") == 0)
		return TRUE;
	first = g_hash_table_lookup(layouts, record->first_id);
	second = g_hash_table_lookup(layouts, record->second_id);
	return sakura_session_layout_reachable(first, page_id, layouts, seen,
	                                       depth + 1) &&
	       sakura_session_layout_reachable(second, page_id, layouts, seen,
	                                       depth + 1);
}


static gboolean
sakura_session_layout_valid(const SakuraSessionSnapshot *snapshot,
                             GError **error)
{
	GHashTable *pages = g_hash_table_new(g_str_hash, g_str_equal);
	GHashTable *layouts = g_hash_table_new(g_str_hash, g_str_equal);
	GHashTable *terminals = g_hash_table_new(g_str_hash, g_str_equal);
	GHashTable *children = g_hash_table_new(g_str_hash, g_str_equal);
	GHashTable *leaf_terminals = g_hash_table_new(g_str_hash, g_str_equal);
	GHashTable *reachable = g_hash_table_new(g_str_hash, g_str_equal);
	GHashTable *groups = g_hash_table_new(g_str_hash, g_str_equal);
	GHashTable *tasks = g_hash_table_new(g_str_hash, g_str_equal);
	guint index;
	for (index = 0; index < snapshot->groups->len; index++) {
		SakuraSessionGroupRecord *group = g_ptr_array_index(snapshot->groups, index);
		g_hash_table_add(groups, group->id);
	}
	for (index = 0; index < snapshot->tasks->len; index++) {
		SakuraSessionTaskRecord *task = g_ptr_array_index(snapshot->tasks, index);
		g_hash_table_add(tasks, task->id);
	}
	if (snapshot->active_group_id != NULL &&
	    g_strcmp0(snapshot->active_group_id, "root") != 0 &&
	    !g_hash_table_contains(groups, snapshot->active_group_id))
		goto invalid;
	if (snapshot->selected_task_id != NULL && snapshot->selected_task_id[0] != '\0' &&
	    !g_hash_table_contains(tasks, snapshot->selected_task_id))
		goto invalid;

	/* Version 4 sessions can contain terminals without the page/layout
	 * representation introduced at the same time. Still validate the
	 * selection IDs above, but retain compatibility with that format. */
	if (snapshot->pages->len == 0 && snapshot->layouts->len == 0) {
		g_hash_table_destroy(pages);
		g_hash_table_destroy(layouts);
		g_hash_table_destroy(terminals);
		g_hash_table_destroy(children);
		g_hash_table_destroy(leaf_terminals);
		g_hash_table_destroy(reachable);
		g_hash_table_destroy(groups);
		g_hash_table_destroy(tasks);
		return TRUE;
	}

	for (index = 0; index < snapshot->pages->len; index++) {
		SakuraSessionPageRecord *page = g_ptr_array_index(snapshot->pages, index);
		if (page->id == NULL || page->id[0] == '\0' ||
		    g_hash_table_contains(pages, page->id))
			goto invalid;
		g_hash_table_add(pages, page->id);
	}
	for (index = 0; index < snapshot->pages->len; index++) {
		SakuraSessionPageRecord *page = g_ptr_array_index(snapshot->pages, index);
		gboolean has_task = page->task_id != NULL && page->task_id[0] != '\0';
		gboolean parent_is_task = page->parent_id != NULL &&
		                          page->parent_id[0] != '\0' &&
		                          g_strcmp0(page->parent_id, "root") != 0 &&
		                          g_hash_table_contains(tasks, page->parent_id);

		if (page->parent_id != NULL && g_strcmp0(page->parent_id, "root") != 0 &&
		    !g_hash_table_contains(groups, page->parent_id) &&
		    !g_hash_table_contains(tasks, page->parent_id))
			goto invalid;
		if (has_task && !g_hash_table_contains(tasks, page->task_id))
			goto invalid;
		if (has_task && g_strcmp0(page->parent_id, page->task_id) != 0)
			goto invalid;
		if (!has_task && parent_is_task)
			goto invalid;
	}
	for (index = 0; index < snapshot->tabs->len; index++) {
		SakuraSessionTabRecord *tab = g_ptr_array_index(snapshot->tabs, index);
		if (tab->terminal_id != NULL && tab->terminal_id[0] != '\0')
			g_hash_table_add(terminals, tab->terminal_id);
	}
	for (index = 0; index < snapshot->pages->len; index++) {
		SakuraSessionPageRecord *page = g_ptr_array_index(snapshot->pages, index);
		if (page->active_terminal_id != NULL &&
		    page->active_terminal_id[0] != '\0' &&
		    !g_hash_table_contains(terminals, page->active_terminal_id))
			goto invalid;
	}
	for (index = 0; index < snapshot->layouts->len; index++) {
		SakuraSessionLayoutRecord *layout = g_ptr_array_index(snapshot->layouts, index);
		if (layout->id == NULL || layout->id[0] == '\0' ||
		    g_hash_table_contains(layouts, layout->id) ||
		    layout->page_id == NULL || !g_hash_table_contains(pages, layout->page_id) ||
		    layout->type == NULL ||
		    (g_strcmp0(layout->type, "leaf") != 0 &&
		     g_strcmp0(layout->type, "split") != 0))
			goto invalid;
		if (g_strcmp0(layout->type, "leaf") == 0) {
			if (layout->terminal_id == NULL ||
			    !g_hash_table_contains(terminals, layout->terminal_id) ||
			    g_hash_table_contains(leaf_terminals, layout->terminal_id))
				goto invalid;
			g_hash_table_add(leaf_terminals, layout->terminal_id);
		} else if (layout->first_id == NULL || layout->second_id == NULL ||
		           (layout->direction != SAKURA_SPLIT_RIGHT &&
		            layout->direction != SAKURA_SPLIT_DOWN) ||
		           !isfinite(layout->ratio) ||
		           layout->ratio < SAKURA_LAYOUT_MIN_RATIO ||
		           layout->ratio > SAKURA_LAYOUT_MAX_RATIO ||
		           g_strcmp0(layout->first_id, layout->second_id) == 0)
			goto invalid;
		g_hash_table_insert(layouts, layout->id, layout);
	}
	for (index = 0; index < snapshot->layouts->len; index++) {
		SakuraSessionLayoutRecord *layout = g_ptr_array_index(snapshot->layouts, index);
		if (g_strcmp0(layout->type, "split") != 0)
			continue;
		SakuraSessionLayoutRecord *first = g_hash_table_lookup(layouts, layout->first_id);
		SakuraSessionLayoutRecord *second = g_hash_table_lookup(layouts, layout->second_id);
		if (first == NULL || second == NULL ||
		    g_strcmp0(first->page_id, layout->page_id) != 0 ||
		    g_strcmp0(second->page_id, layout->page_id) != 0 ||
		    g_hash_table_contains(children, first->id) ||
		    g_hash_table_contains(children, second->id))
			goto invalid;
		g_hash_table_add(children, first->id);
		g_hash_table_add(children, second->id);
	}
	for (index = 0; index < snapshot->pages->len; index++) {
		SakuraSessionPageRecord *page = g_ptr_array_index(snapshot->pages, index);
		if (page->root_layout_id == NULL ||
		    !g_hash_table_contains(layouts, page->root_layout_id) ||
		    g_hash_table_contains(children, page->root_layout_id))
			goto invalid;
		if (!sakura_session_layout_reachable(
				g_hash_table_lookup(layouts, page->root_layout_id), page->id,
				layouts, reachable, 0))
			goto invalid;
		if (page->active_terminal_id != NULL &&
		    page->active_terminal_id[0] != '\0') {
			SakuraSessionLayoutRecord *active_layout = NULL;

			for (guint layout_index = 0;
			     layout_index < snapshot->layouts->len; layout_index++) {
				SakuraSessionLayoutRecord *candidate =
					g_ptr_array_index(snapshot->layouts, layout_index);
				if (g_strcmp0(candidate->type, "leaf") == 0 &&
				    g_strcmp0(candidate->terminal_id,
				              page->active_terminal_id) == 0) {
					active_layout = candidate;
					break;
				}
			}
			if (active_layout == NULL ||
			    g_strcmp0(active_layout->page_id, page->id) != 0)
				goto invalid;
		}
	}
	if (g_hash_table_size(reachable) != snapshot->layouts->len)
		goto invalid;
	if (g_hash_table_size(leaf_terminals) != g_hash_table_size(terminals))
		goto invalid;
	g_hash_table_destroy(pages);
	g_hash_table_destroy(layouts);
	g_hash_table_destroy(terminals);
	g_hash_table_destroy(children);
	g_hash_table_destroy(leaf_terminals);
	g_hash_table_destroy(reachable);
	g_hash_table_destroy(groups);
	g_hash_table_destroy(tasks);
	return TRUE;

invalid:
	g_hash_table_destroy(pages);
	g_hash_table_destroy(layouts);
	g_hash_table_destroy(terminals);
	g_hash_table_destroy(children);
	g_hash_table_destroy(leaf_terminals);
	g_hash_table_destroy(reachable);
	g_hash_table_destroy(groups);
	g_hash_table_destroy(tasks);
	return sakura_session_error(error, G_KEY_FILE_ERROR_INVALID_VALUE,
	                            g_strdup("invalid page or layout record"));
}


static gboolean
sakura_session_snapshot_load_into(GKeyFile *key_file,
                                   SakuraSessionSnapshot *snapshot,
                                   GError **error)
{
	gint version, group_count, task_count = 0, terminal_count, page_count = 0,
	     layout_count = 0, expanded_sidebar_count = 0;
	gint index;

	if (key_file == NULL || snapshot == NULL)
		return sakura_session_error(error, G_KEY_FILE_ERROR_INVALID_VALUE,
		                            g_strdup("session snapshot arguments are invalid"));

	version = g_key_file_get_integer(key_file, "Session", "version", error);
	if (error != NULL && *error != NULL)
		return FALSE;
	if (version < 1 || version > SAKURA_SESSION_VERSION)
		return sakura_session_error(error, G_KEY_FILE_ERROR_INVALID_VALUE,
		                            g_strdup_printf("unsupported session version: %d", version));

	group_count = g_key_file_get_integer(key_file, "Session", "group_count", error);
	if (error != NULL && *error != NULL)
		return FALSE;
	terminal_count = g_key_file_get_integer(key_file, "Session", "terminal_count", error);
	if (error != NULL && *error != NULL)
		return FALSE;
	if (group_count < 0 || terminal_count < 0)
		return sakura_session_error(error, G_KEY_FILE_ERROR_INVALID_VALUE,
			g_strdup("session counts cannot be negative"));
	if (version >= 4) {
		page_count = g_key_file_get_integer(key_file, "Session", "page_count", error);
		if (error != NULL && *error != NULL)
			return FALSE;
		layout_count = g_key_file_get_integer(key_file, "Session", "layout_count", error);
		if (error != NULL && *error != NULL)
			return FALSE;
		if (page_count < 0 || layout_count < 0)
			return sakura_session_error(error, G_KEY_FILE_ERROR_INVALID_VALUE,
			                            g_strdup("page and layout counts cannot be negative"));
	}
	if (version >= 5) {
		task_count = g_key_file_get_integer(key_file, "Session", "task_count", error);
		if (error != NULL && *error != NULL)
			return FALSE;
		if (task_count < 0)
			return sakura_session_error(error, G_KEY_FILE_ERROR_INVALID_VALUE,
			                            g_strdup("task count cannot be negative"));
	}
	if (version >= 9 &&
	    g_key_file_has_key(key_file, "Session", "expanded_sidebar_count", NULL)) {
		expanded_sidebar_count = g_key_file_get_integer(
			key_file, "Session", "expanded_sidebar_count", error);
		if (error != NULL && *error != NULL)
			return FALSE;
		if (expanded_sidebar_count < 0)
			return sakura_session_error(error, G_KEY_FILE_ERROR_INVALID_VALUE,
			                            g_strdup("expanded sidebar count cannot be negative"));
		snapshot->sidebar_expansion_saved = TRUE;
	} else {
		snapshot->sidebar_expansion_saved = FALSE;
	}

	if (g_key_file_has_key(key_file, "Session", "selected_terminal", NULL))
		snapshot->selected_terminal = g_key_file_get_integer(key_file, "Session",
	                                                     "selected_terminal", NULL);
	g_free(snapshot->selected_terminal_id);
	snapshot->selected_terminal_id = g_key_file_get_string(key_file, "Session",
	                                                       "selected_terminal_id", NULL);
	g_free(snapshot->selected_page_id);
	snapshot->selected_page_id = version >= 4
	                           ? g_key_file_get_string(key_file, "Session",
	                                                   "selected_page_id", NULL) : NULL;
	g_free(snapshot->selected_task_id);
	snapshot->selected_task_id = version >= 5
	                           ? g_key_file_get_string(key_file, "Session",
	                                                   "selected_task_id", NULL) : NULL;
	g_free(snapshot->active_group_id);
	snapshot->active_group_id = g_key_file_get_string(key_file, "Session",
	                                                  "active_group_id", NULL);
	if (snapshot->active_group_id == NULL)
		snapshot->active_group_id = g_strdup("root");
	g_free(snapshot->root_directory);
	snapshot->root_directory = g_key_file_get_string(key_file, "Session",
	                                                 "root_directory", NULL);
	if (g_key_file_has_key(key_file, "Session", "sidebar_visible", NULL))
		snapshot->sidebar_visible = g_key_file_get_boolean(key_file, "Session",
	                                                   "sidebar_visible", NULL);
	if (g_key_file_has_key(key_file, "Session", "sidebar_width", NULL))
		snapshot->sidebar_width = g_key_file_get_integer(key_file, "Session",
                                                 "sidebar_width", NULL);
	if (version >= 7 && g_key_file_has_key(key_file, "Session", "show_archived", NULL))
		snapshot->show_archived = g_key_file_get_boolean(key_file, "Session",
	                                                   "show_archived", NULL);

	g_ptr_array_set_size(snapshot->groups, 0);
	g_ptr_array_set_size(snapshot->tasks, 0);
	g_ptr_array_set_size(snapshot->tabs, 0);
	g_ptr_array_set_size(snapshot->pages, 0);
	g_ptr_array_set_size(snapshot->layouts, 0);
	g_ptr_array_set_size(snapshot->expanded_sidebar_nodes, 0);
	for (index = 0; index < group_count; index++) {
		gchar *section = g_strdup_printf("Group%d", index);
		SakuraSessionGroupRecord *group = g_new0(SakuraSessionGroupRecord, 1);
		group->id = g_key_file_get_string(key_file, section, "id", NULL);
		group->parent_id = g_key_file_get_string(key_file, section, "parent", NULL);
		group->title = g_key_file_get_string(key_file, section, "title", NULL);
		group->directory = g_key_file_get_string(key_file, section, "directory", NULL);
		if (version >= 7 && g_key_file_has_key(key_file, section, "archived", NULL))
			group->archived = g_key_file_get_boolean(key_file, section, "archived", NULL);
		group->order = index;
		if (g_key_file_has_key(key_file, section, "order", NULL)) {
			gint order = g_key_file_get_integer(key_file, section, "order", NULL);
			if (order >= 0)
				group->order = order;
		}
		if (group->parent_id == NULL)
			group->parent_id = g_strdup("root");
		if (group->title == NULL)
			group->title = g_strdup("");
		g_ptr_array_add(snapshot->groups, group);
		g_free(section);
	}
	for (index = 0; index < task_count; index++) {
		gchar *section = g_strdup_printf("Task%d", index);
		SakuraSessionTaskRecord *task = g_new0(SakuraSessionTaskRecord, 1);
		gint status;

		task->id = g_key_file_get_string(key_file, section, "id", NULL);
		task->parent_id = g_key_file_get_string(key_file, section, "parent", NULL);
		task->group_id = g_key_file_get_string(key_file, section, "group", NULL);
		task->title = g_key_file_get_string(key_file, section, "title", NULL);
		task->provider = g_key_file_get_string(key_file, section, "provider", NULL);
		task->external_id = g_key_file_get_string(key_file, section, "external_id", NULL);
		task->url = g_key_file_get_string(key_file, section, "url", NULL);
		if (version >= 7 && g_key_file_has_key(key_file, section, "archived", NULL))
			task->archived = g_key_file_get_boolean(key_file, section, "archived", NULL);
		task->order = index;
		if (g_key_file_has_key(key_file, section, "order", NULL)) {
			gint order = g_key_file_get_integer(key_file, section, "order", NULL);
			if (order >= 0)
				task->order = order;
		}
		status = g_key_file_get_integer(key_file, section, "status", NULL);
		task->status = status >= SAKURA_TASK_READY && status <= SAKURA_TASK_DONE
		             ? status : SAKURA_TASK_READY;
		if (task->parent_id == NULL)
			task->parent_id = g_strdup("root");
		if (task->group_id == NULL)
			task->group_id = g_strdup("root");
		if (task->title == NULL)
			task->title = g_strdup("");
		if (task->provider == NULL)
			task->provider = g_strdup("local");
		g_ptr_array_add(snapshot->tasks, task);
		g_free(section);
	}
	for (index = 0; index < page_count; index++) {
		gchar *section = g_strdup_printf("Page%d", index);
		SakuraSessionPageRecord *page = g_new0(SakuraSessionPageRecord, 1);
		page->id = g_key_file_get_string(key_file, section, "id", NULL);
		page->parent_id = g_key_file_get_string(key_file, section, "parent", NULL);
		page->title = g_key_file_get_string(key_file, section, "title", NULL);
		page->root_layout_id = g_key_file_get_string(key_file, section, "root_layout", NULL);
		page->active_terminal_id = g_key_file_get_string(key_file, section,
		                                                "active_terminal_id", NULL);
		page->task_id = version >= 5
		              ? g_key_file_get_string(key_file, section, "task_id", NULL) : NULL;
		page->title_set_by_user = g_key_file_get_boolean(key_file, section,
		                                               "title_set_by_user", NULL);
		if (version >= 8 && g_key_file_has_key(key_file, section, "archived", NULL))
			page->archived = g_key_file_get_boolean(key_file, section, "archived", NULL);
		if (page->parent_id == NULL)
			page->parent_id = g_strdup("root");
		g_ptr_array_add(snapshot->pages, page);
		g_free(section);
	}
	for (index = 0; index < layout_count; index++) {
		gchar *section = g_strdup_printf("Layout%d", index);
		gchar *direction;
		SakuraSessionLayoutRecord *layout = g_new0(SakuraSessionLayoutRecord, 1);
		layout->id = g_key_file_get_string(key_file, section, "id", NULL);
		layout->page_id = g_key_file_get_string(key_file, section, "page", NULL);
		layout->type = g_key_file_get_string(key_file, section, "type", NULL);
		direction = g_key_file_get_string(key_file, section, "direction", NULL);
		layout->direction = g_strcmp0(direction, "down") == 0
		                 ? SAKURA_SPLIT_DOWN : SAKURA_SPLIT_RIGHT;
		g_free(direction);
		layout->ratio = g_key_file_get_double(key_file, section, "ratio", NULL);
		if (layout->ratio == 0.0)
			layout->ratio = SAKURA_LAYOUT_DEFAULT_RATIO;
		layout->first_id = g_key_file_get_string(key_file, section, "first", NULL);
		layout->second_id = g_key_file_get_string(key_file, section, "second", NULL);
		layout->terminal_id = g_key_file_get_string(key_file, section, "terminal_id", NULL);
		g_ptr_array_add(snapshot->layouts, layout);
		g_free(section);
	}
	for (index = 0; index < expanded_sidebar_count; index++) {
		gchar *section = g_strdup_printf("ExpandedSidebar%d", index);
		gchar *kind = g_key_file_get_string(key_file, section, "kind", NULL);
		SakuraSessionSidebarExpansionRecord *record = g_new0(
			SakuraSessionSidebarExpansionRecord, 1);
		const gchar *id = NULL;

		record->id = g_key_file_get_string(key_file, section, "id", NULL);
		if (g_strcmp0(kind, "group") == 0) {
			record->kind = SAKURA_SIDEBAR_EXPANSION_GROUP;
			id = record->id;
		} else if (g_strcmp0(kind, "task") == 0) {
			record->kind = SAKURA_SIDEBAR_EXPANSION_TASK;
			id = record->id;
		} else if (g_strcmp0(kind, "session") == 0) {
			record->kind = SAKURA_SIDEBAR_EXPANSION_SESSION;
			id = record->id;
		}
		/* Unknown or incomplete rows are ignored so a stale view-only entry
		 * cannot make an otherwise valid workspace fail to restore. */
		if (id != NULL && id[0] != '\0')
			g_ptr_array_add(snapshot->expanded_sidebar_nodes, record);
		else {
			g_free(record->id);
			g_free(record);
		}
		g_free(kind);
		g_free(section);
	}

	for (index = 0; index < terminal_count; index++) {
		gchar *section = g_strdup_printf("Terminal%d", index);
		SakuraSessionTabRecord *tab = g_new0(SakuraSessionTabRecord, 1);
		gchar *kind = g_key_file_get_string(key_file, section, "kind", NULL);

		/* Older session files did not persist the terminal colorset. Keep the
		 * sentinel so restore can fall back to the configured colorset. */
		tab->colorset = -1;
		tab->parent_id = g_key_file_get_string(key_file, section, "parent", NULL);
		tab->cwd = g_key_file_get_string(key_file, section, "cwd", NULL);
		tab->title = g_key_file_get_string(key_file, section, "title", NULL);
		tab->terminal_id = g_key_file_get_string(key_file, section, "terminal_id", NULL);
		tab->tool_id = g_key_file_get_string(key_file, section, "tool", NULL);
		tab->tool_target = g_key_file_get_string(key_file, section, "tool_target", NULL);
		tab->codex_session_id = g_key_file_get_string(key_file, section,
	                                                "codex_session_id", NULL);
		tab->codex_session_name = g_key_file_get_string(key_file, section,
		                                                  "codex_session_name", NULL);
		tab->codex_reasoning_effort = g_key_file_get_string(key_file, section,
		                                                    "codex_reasoning_effort", NULL);
		if (g_key_file_has_key(key_file, section, "colorset", NULL)) {
			gint colorset = g_key_file_get_integer(key_file, section, "colorset", NULL);
			if (colorset >= 0 && colorset < NUM_COLORSETS)
				tab->colorset = colorset;
		}
		tab->title_set_by_user = g_key_file_get_boolean(key_file, section,
	                                                 "title_set_by_user", NULL);
		if (g_key_file_has_key(key_file, section, "status", NULL)) {
			gint status = g_key_file_get_integer(key_file, section, "status", NULL);
			if (status >= SAKURA_TAB_STATUS_NONE && status <= SAKURA_TAB_STATUS_ERROR)
				tab->status = status;
		}
		tab->attention = g_key_file_get_boolean(key_file, section, "attention", NULL);
		if (g_key_file_has_key(key_file, section, "attention_timestamp", NULL))
			tab->attention_timestamp = g_key_file_get_int64(
				key_file, section, "attention_timestamp", NULL);
		tab->kind = g_strcmp0(kind, "codex") == 0
		          ? SAKURA_TAB_CODEX
		          : g_strcmp0(kind, "tool") == 0 || g_strcmp0(kind, "gitui") == 0
		          ? SAKURA_TAB_TOOL : SAKURA_TAB_SHELL;
		if (g_strcmp0(kind, "gitui") == 0 && tab->tool_id == NULL)
			tab->tool_id = g_strdup("gitui");
		if (tab->parent_id == NULL)
			tab->parent_id = g_strdup("root");
		g_ptr_array_add(snapshot->tabs, tab);
		g_free(kind);
		g_free(section);
	}
	sakura_session_repair_duplicate_page_ids(snapshot);

	return sakura_session_group_ids_valid(snapshot, error) &&
	       sakura_session_task_ids_valid(snapshot, error) &&
	       sakura_session_terminal_ids_valid(snapshot, error) &&
	       (version < 4 || sakura_session_layout_valid(snapshot, error));
}


gboolean
sakura_session_snapshot_load(GKeyFile *key_file,
                              SakuraSessionSnapshot *snapshot,
                              GError **error)
{
	SakuraSessionSnapshot *parsed;
	SakuraSessionSnapshot previous;

	if (snapshot == NULL)
		return sakura_session_error(error, G_KEY_FILE_ERROR_INVALID_VALUE,
		                            g_strdup("session snapshot arguments are invalid"));

	parsed = sakura_session_snapshot_new();
	if (!sakura_session_snapshot_load_into(key_file, parsed, error)) {
		sakura_session_snapshot_free(parsed);
		return FALSE;
	}

	/* Commit atomically at the record level. A malformed or truncated session
	 * must never partially replace a snapshot that the caller may still use. */
	previous = *snapshot;
	*snapshot = *parsed;
	*parsed = previous;
	sakura_session_snapshot_free(parsed);
	return TRUE;
}


void
sakura_session_snapshot_save(const SakuraSessionSnapshot *snapshot,
                             GKeyFile *key_file)
{
	guint index;

	g_key_file_set_integer(key_file, "Session", "version", SAKURA_SESSION_VERSION);
	g_key_file_set_integer(key_file, "Session", "group_count", snapshot->groups->len);
	g_key_file_set_integer(key_file, "Session", "task_count", snapshot->tasks->len);
	g_key_file_set_integer(key_file, "Session", "page_count", snapshot->pages->len);
	g_key_file_set_integer(key_file, "Session", "layout_count", snapshot->layouts->len);
	g_key_file_set_integer(key_file, "Session", "terminal_count", snapshot->tabs->len);
	g_key_file_set_integer(key_file, "Session", "selected_terminal",
	                      snapshot->selected_terminal);
	if (snapshot->selected_terminal_id != NULL)
		g_key_file_set_string(key_file, "Session", "selected_terminal_id",
		                      snapshot->selected_terminal_id);
	if (snapshot->selected_page_id != NULL)
		g_key_file_set_string(key_file, "Session", "selected_page_id",
		                      snapshot->selected_page_id);
	if (snapshot->selected_task_id != NULL)
		g_key_file_set_string(key_file, "Session", "selected_task_id",
		                      snapshot->selected_task_id);
	g_key_file_set_string(key_file, "Session", "active_group_id",
	                      snapshot->active_group_id != NULL ? snapshot->active_group_id : "root");
	if (snapshot->root_directory != NULL && snapshot->root_directory[0] != '\0')
		g_key_file_set_string(key_file, "Session", "root_directory", snapshot->root_directory);
	else
		g_key_file_remove_key(key_file, "Session", "root_directory", NULL);
	g_key_file_set_boolean(key_file, "Session", "sidebar_visible", snapshot->sidebar_visible);
	g_key_file_set_integer(key_file, "Session", "sidebar_width", snapshot->sidebar_width);
	g_key_file_set_boolean(key_file, "Session", "show_archived", snapshot->show_archived);
	if (snapshot->sidebar_expansion_saved) {
		g_key_file_set_integer(key_file, "Session", "expanded_sidebar_count",
		                      snapshot->expanded_sidebar_nodes != NULL
		                      ? snapshot->expanded_sidebar_nodes->len : 0);
		for (index = 0; snapshot->expanded_sidebar_nodes != NULL &&
		              index < snapshot->expanded_sidebar_nodes->len; index++) {
			SakuraSessionSidebarExpansionRecord *record = g_ptr_array_index(
				snapshot->expanded_sidebar_nodes, index);
			gchar *section = g_strdup_printf("ExpandedSidebar%u", index);
			const gchar *kind = record->kind == SAKURA_SIDEBAR_EXPANSION_GROUP
			                  ? "group"
			                  : record->kind == SAKURA_SIDEBAR_EXPANSION_TASK
			                  ? "task" : "session";

			g_key_file_set_string(key_file, section, "kind", kind);
			g_key_file_set_string(key_file, section, "id",
			                      record->id != NULL ? record->id : "");
			g_free(section);
		}
	} else {
		g_key_file_remove_key(key_file, "Session", "expanded_sidebar_count", NULL);
	}

	for (index = 0; index < snapshot->groups->len; index++) {
		SakuraSessionGroupRecord *group = g_ptr_array_index(snapshot->groups, index);
		gchar *section = g_strdup_printf("Group%u", index);
		g_key_file_set_string(key_file, section, "id", group->id != NULL ? group->id : "");
		g_key_file_set_string(key_file, section, "parent",
		                      group->parent_id != NULL ? group->parent_id : "root");
		g_key_file_set_integer(key_file, section, "order", group->order);
		g_key_file_set_string(key_file, section, "title", group->title != NULL ? group->title : "");
		if (group->directory != NULL && group->directory[0] != '\0')
			g_key_file_set_string(key_file, section, "directory", group->directory);
		else
			g_key_file_remove_key(key_file, section, "directory", NULL);
		g_key_file_set_boolean(key_file, section, "archived", group->archived);
		g_free(section);
	}
	for (index = 0; index < snapshot->tasks->len; index++) {
		SakuraSessionTaskRecord *task = g_ptr_array_index(snapshot->tasks, index);
		gchar *section = g_strdup_printf("Task%u", index);
		g_key_file_set_string(key_file, section, "id", task->id != NULL ? task->id : "");
		g_key_file_set_string(key_file, section, "parent",
		                      task->parent_id != NULL ? task->parent_id : "root");
		g_key_file_set_string(key_file, section, "group",
		                      task->group_id != NULL ? task->group_id : "root");
		g_key_file_set_integer(key_file, section, "order", task->order);
		g_key_file_set_string(key_file, section, "title", task->title != NULL ? task->title : "");
		g_key_file_set_string(key_file, section, "provider",
		                      task->provider != NULL ? task->provider : "local");
		if (task->external_id != NULL && task->external_id[0] != '\0')
			g_key_file_set_string(key_file, section, "external_id", task->external_id);
		if (task->url != NULL && task->url[0] != '\0')
			g_key_file_set_string(key_file, section, "url", task->url);
		g_key_file_set_integer(key_file, section, "status", task->status);
		g_key_file_set_boolean(key_file, section, "archived", task->archived);
		g_free(section);
	}
	for (index = 0; index < snapshot->pages->len; index++) {
		SakuraSessionPageRecord *page = g_ptr_array_index(snapshot->pages, index);
		gchar *section = g_strdup_printf("Page%u", index);
		g_key_file_set_string(key_file, section, "id", page->id != NULL ? page->id : "");
		g_key_file_set_string(key_file, section, "parent",
		                      page->parent_id != NULL ? page->parent_id : "root");
		g_key_file_set_string(key_file, section, "title", page->title != NULL ? page->title : "");
		g_key_file_set_boolean(key_file, section, "title_set_by_user", page->title_set_by_user);
		g_key_file_set_boolean(key_file, section, "archived", page->archived);
		g_key_file_set_string(key_file, section, "root_layout",
		                      page->root_layout_id != NULL ? page->root_layout_id : "");
		if (page->active_terminal_id != NULL)
			g_key_file_set_string(key_file, section, "active_terminal_id", page->active_terminal_id);
		if (page->task_id != NULL && page->task_id[0] != '\0')
			g_key_file_set_string(key_file, section, "task_id", page->task_id);
		g_free(section);
	}
	for (index = 0; index < snapshot->layouts->len; index++) {
		SakuraSessionLayoutRecord *layout = g_ptr_array_index(snapshot->layouts, index);
		gchar *section = g_strdup_printf("Layout%u", index);
		g_key_file_set_string(key_file, section, "id", layout->id != NULL ? layout->id : "");
		g_key_file_set_string(key_file, section, "page", layout->page_id != NULL ? layout->page_id : "");
		g_key_file_set_string(key_file, section, "type", layout->type != NULL ? layout->type : "leaf");
		g_key_file_set_string(key_file, section, "direction",
		                      layout->direction == SAKURA_SPLIT_DOWN ? "down" : "right");
		g_key_file_set_double(key_file, section, "ratio", layout->ratio);
		if (layout->first_id != NULL)
			g_key_file_set_string(key_file, section, "first", layout->first_id);
		if (layout->second_id != NULL)
			g_key_file_set_string(key_file, section, "second", layout->second_id);
		if (layout->terminal_id != NULL)
			g_key_file_set_string(key_file, section, "terminal_id", layout->terminal_id);
		g_free(section);
	}

	for (index = 0; index < snapshot->tabs->len; index++) {
		SakuraSessionTabRecord *tab = g_ptr_array_index(snapshot->tabs, index);
		gchar *section = g_strdup_printf("Terminal%u", index);
		const gchar *kind = tab->kind == SAKURA_TAB_CODEX ? "codex" :
		                    tab->kind == SAKURA_TAB_TOOL ? "tool" : "shell";

		g_key_file_set_string(key_file, section, "parent",
		                      tab->parent_id != NULL ? tab->parent_id : "root");
		g_key_file_set_string(key_file, section, "cwd", tab->cwd != NULL ? tab->cwd : "");
		g_key_file_set_string(key_file, section, "terminal_id",
		                      tab->terminal_id != NULL ? tab->terminal_id : "");
		g_key_file_set_string(key_file, section, "kind", kind);
		if (tab->tool_id != NULL)
			g_key_file_set_string(key_file, section, "tool", tab->tool_id);
		if (tab->tool_target != NULL && tab->tool_target[0] != '\0')
			g_key_file_set_string(key_file, section, "tool_target", tab->tool_target);
		if (tab->codex_session_id != NULL && tab->codex_session_id[0] != '\0')
			g_key_file_set_string(key_file, section, "codex_session_id", tab->codex_session_id);
		if (tab->codex_session_name != NULL && tab->codex_session_name[0] != '\0')
			g_key_file_set_string(key_file, section, "codex_session_name", tab->codex_session_name);
		if (tab->codex_reasoning_effort != NULL && tab->codex_reasoning_effort[0] != '\0')
			g_key_file_set_string(key_file, section, "codex_reasoning_effort",
			                      tab->codex_reasoning_effort);
		if (tab->colorset >= 0 && tab->colorset < NUM_COLORSETS)
			g_key_file_set_integer(key_file, section, "colorset", tab->colorset);
		g_key_file_set_boolean(key_file, section, "title_set_by_user", tab->title_set_by_user);
		g_key_file_set_integer(key_file, section, "status", tab->status);
		g_key_file_set_boolean(key_file, section, "attention", tab->attention);
		if (tab->attention_timestamp > 0)
			g_key_file_set_int64(key_file, section, "attention_timestamp",
			                     tab->attention_timestamp);
		if (tab->title_set_by_user)
			g_key_file_set_string(key_file, section, "title", tab->title != NULL ? tab->title : "");
		g_free(section);
	}
}
