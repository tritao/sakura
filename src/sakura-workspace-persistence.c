#include "sakura-private.h"

/* Convert model-owned workspace state into pure session records. This module
 * does not access the global app or materialize GTK projection nodes. */

static void
sakura_workspace_snapshot_append_page_panes(const SakuraPage *page,
                                             GPtrArray *result,
                                             GHashTable *seen_pages)
{
	if (page == NULL || !g_hash_table_add(seen_pages, (gpointer)page))
		return;
	for (guint index = 0; page->panes != NULL && index < page->panes->len;
	     index++) {
		SakuraTab *tab = g_ptr_array_index(page->panes, index);

		if (tab != NULL)
			g_ptr_array_add(result, tab);
	}
}


static gint
sakura_workspace_snapshot_task_order_compare(gconstpointer first,
                                              gconstpointer second)
{
	const SakuraTask *first_task = first;
	const SakuraTask *second_task = second;

	if (first_task->order < second_task->order)
		return -1;
	if (first_task->order > second_task->order)
		return 1;
	return 0;
}


static void
sakura_workspace_snapshot_append_task_panes(
	const SakuraWorkspaceModel *model, SakuraTask *task, GPtrArray *result,
	GHashTable *seen_tasks, GHashTable *seen_pages)
{
	GList *children = NULL, *task_link;

	if (model == NULL || task == NULL ||
	    !g_hash_table_add(seen_tasks, task))
		return;
	for (guint index = 0; model->tasks != NULL && index < model->tasks->len;
	     index++) {
		SakuraTask *child = g_ptr_array_index(model->tasks, index);

		if (child != NULL && child->parent == task)
			children = g_list_append(children, child);
	}
	children = g_list_sort(children,
	                       sakura_workspace_snapshot_task_order_compare);
	for (task_link = children; task_link != NULL; task_link = task_link->next)
		sakura_workspace_snapshot_append_task_panes(
			model, task_link->data, result, seen_tasks, seen_pages);
	g_list_free(children);
	for (guint index = 0; model->pages != NULL && index < model->pages->len;
	     index++) {
		SakuraPage *page = g_ptr_array_index(model->pages, index);

		if (page != NULL && page->task == task)
			sakura_workspace_snapshot_append_page_panes(page, result,
		                                             seen_pages);
	}
}


static gint
sakura_workspace_snapshot_group_order_compare(gconstpointer first,
                                               gconstpointer second)
{
	const SakuraGroup *first_group = first;
	const SakuraGroup *second_group = second;

	if (first_group->order < second_group->order)
		return -1;
	if (first_group->order > second_group->order)
		return 1;
	return 0;
}


static void
sakura_workspace_snapshot_append_group_panes(
	const SakuraWorkspaceModel *model, SakuraGroup *group, GPtrArray *result,
	GHashTable *seen_groups, GHashTable *seen_tasks, GHashTable *seen_pages)
{
	GList *child_groups = NULL, *child_group_link;
	GList *tasks = NULL, *task_link;

	if (model == NULL || group == NULL ||
	    !g_hash_table_add(seen_groups, group))
		return;
	for (GList *group_link = model->groups; group_link != NULL;
	     group_link = group_link->next) {
		SakuraGroup *child = group_link->data;

		if (child != NULL && child != model->root_group &&
		    (child->parent == group ||
		     (child->parent == NULL && group == model->root_group)))
			child_groups = g_list_append(child_groups, child);
	}
	child_groups = g_list_sort(child_groups,
	                           sakura_workspace_snapshot_group_order_compare);
	for (child_group_link = child_groups; child_group_link != NULL;
	     child_group_link = child_group_link->next)
		sakura_workspace_snapshot_append_group_panes(
			model, child_group_link->data, result, seen_groups, seen_tasks,
			seen_pages);
	g_list_free(child_groups);

	for (guint index = 0; model->tasks != NULL && index < model->tasks->len;
	     index++) {
		SakuraTask *task = g_ptr_array_index(model->tasks, index);

		if (task != NULL && task->parent == NULL &&
		    (task->group == group ||
		     (task->group == NULL && group == model->root_group)))
			tasks = g_list_append(tasks, task);
	}
	tasks = g_list_sort(tasks,
	                    sakura_workspace_snapshot_task_order_compare);
	for (task_link = tasks; task_link != NULL; task_link = task_link->next)
		sakura_workspace_snapshot_append_task_panes(
			model, task_link->data, result, seen_tasks, seen_pages);
	g_list_free(tasks);

	for (guint index = 0; model->pages != NULL && index < model->pages->len;
	     index++) {
		SakuraPage *page = g_ptr_array_index(model->pages, index);

		if (page != NULL && page->task == NULL &&
		    (page->group == group ||
		     (page->group == NULL && group == model->root_group)))
			sakura_workspace_snapshot_append_page_panes(page, result,
		                                             seen_pages);
	}
}


static GPtrArray *
sakura_workspace_snapshot_ordered_panes(const SakuraWorkspaceModel *model)
{
	GPtrArray *panes = g_ptr_array_new();
	GHashTable *seen_groups = g_hash_table_new(g_direct_hash, g_direct_equal);
	GHashTable *seen_tasks = g_hash_table_new(g_direct_hash, g_direct_equal);
	GHashTable *seen_pages = g_hash_table_new(g_direct_hash, g_direct_equal);
	GPtrArray *ordered_groups;

	if (model == NULL)
		goto out;
	sakura_workspace_snapshot_append_group_panes(
		model, model->root_group, panes, seen_groups, seen_tasks, seen_pages);
	ordered_groups = sakura_workspace_model_ordered_groups(model);
	for (guint index = 0; index < ordered_groups->len; index++)
		sakura_workspace_snapshot_append_group_panes(
			model, g_ptr_array_index(ordered_groups, index), panes,
			seen_groups, seen_tasks, seen_pages);
	g_ptr_array_unref(ordered_groups);
	for (guint index = 0; model->tasks != NULL && index < model->tasks->len;
	     index++)
		sakura_workspace_snapshot_append_task_panes(
			model, g_ptr_array_index(model->tasks, index), panes, seen_tasks,
			seen_pages);
	for (guint index = 0; model->pages != NULL && index < model->pages->len;
	     index++)
		sakura_workspace_snapshot_append_page_panes(
			g_ptr_array_index(model->pages, index), panes, seen_pages);
out:
	g_hash_table_destroy(seen_groups);
	g_hash_table_destroy(seen_tasks);
	g_hash_table_destroy(seen_pages);
	return panes;
}


static void
sakura_workspace_snapshot_append_layout(SakuraSessionSnapshot *snapshot,
                                        SakuraPage *page,
                                        SakuraLayoutNode *node)
{
	SakuraSessionLayoutRecord *record;

	if (snapshot == NULL || page == NULL || node == NULL)
		return;
	record = g_new0(SakuraSessionLayoutRecord, 1);
	record->id = g_strdup(node->id);
	record->page_id = g_strdup(page->id);
	record->ratio = node->kind == SAKURA_LAYOUT_SPLIT
	             ? node->data.split.ratio : SAKURA_LAYOUT_DEFAULT_RATIO;
	if (node->kind == SAKURA_LAYOUT_LEAF) {
		record->type = g_strdup("leaf");
		record->terminal_id = g_strdup(node->data.leaf.tab != NULL
	                              ? node->data.leaf.tab->terminal_id : NULL);
	} else {
		record->type = g_strdup("split");
		record->direction = node->data.split.direction;
		record->first_id = g_strdup(node->data.split.first != NULL
	                           ? node->data.split.first->id : NULL);
		record->second_id = g_strdup(node->data.split.second != NULL
	                            ? node->data.split.second->id : NULL);
	}
	g_ptr_array_add(snapshot->layouts, record);
	if (node->kind == SAKURA_LAYOUT_SPLIT) {
		sakura_workspace_snapshot_append_layout(snapshot, page,
		                                        node->data.split.first);
		sakura_workspace_snapshot_append_layout(snapshot, page,
		                                        node->data.split.second);
	}
}


static SakuraGroup *
sakura_workspace_snapshot_group_for_page(const SakuraWorkspaceModel *model,
                                         const SakuraPage *page)
{
	if (model == NULL || page == NULL)
		return model != NULL ? model->root_group : NULL;
	if (page->group != NULL)
		return page->group;
	if (page->task != NULL && page->task->group != NULL)
		return page->task->group;
	return model->root_group;
}


static SakuraSessionPageRecord *
sakura_workspace_snapshot_page_record(const SakuraWorkspaceModel *model,
                                      SakuraPage *page)
{
	SakuraSessionPageRecord *record;
	SakuraTab *representative;
	SakuraGroup *group;

	if (model == NULL || page == NULL)
		return NULL;
	record = g_new0(SakuraSessionPageRecord, 1);
	record->id = g_strdup(page->id);
	representative = page->tab_bar_tab;
	group = sakura_workspace_snapshot_group_for_page(model, page);
	record->group_id = g_strdup(group != NULL ? group->id : "root");
	record->parent_id = g_strdup(page->task != NULL
	                           ? page->task->id
	                           : group != NULL ? group->id : "root");
	record->task_id = g_strdup(page->task != NULL ? page->task->id : NULL);
	record->title = g_strdup(page->title != NULL ? page->title :
	                          representative != NULL && representative->user_title != NULL
	                        ? representative->user_title : "");
	record->title_set_by_user = page->title_set_by_user ||
	                            (representative != NULL && representative->label_set_byuser);
	record->archived = page->archived;
	record->root_layout_id = g_strdup(page->layout_root != NULL
	                                ? page->layout_root->id : NULL);
	if (page->active_tab != NULL)
		record->active_terminal_id = g_strdup(page->active_tab->terminal_id);
	return record;
}


SakuraSessionSnapshot *
sakura_workspace_model_snapshot_new(const SakuraWorkspaceModel *model,
                                     gboolean sidebar_visible,
                                     gint sidebar_width)
{
	SakuraSessionSnapshot *snapshot;
	GPtrArray *model_panes, *ordered_groups, *ordered_tasks;
	SakuraTab *selected_tab;
	gint selected_terminal = -1;

	if (model == NULL)
		return NULL;
	snapshot = sakura_session_snapshot_new();
	model_panes = sakura_workspace_snapshot_ordered_panes(model);
	selected_tab = model->active_tab != NULL ? model->active_tab
	                                           : model->active_page != NULL
	                                           ? model->active_page->active_tab : NULL;
	for (guint index = 0; index < model_panes->len; index++) {
		if (g_ptr_array_index(model_panes, index) == selected_tab) {
			selected_terminal = (gint)index;
			break;
		}
	}
	if (selected_tab != NULL && selected_tab->terminal_id != NULL) {
		snapshot->selected_terminal_id = g_strdup(selected_tab->terminal_id);
		if (selected_tab->page != NULL) {
			snapshot->selected_page_id = g_strdup(selected_tab->page->id);
			if (selected_tab->page->task != NULL)
				snapshot->selected_task_id =
					g_strdup(selected_tab->page->task->id);
		}
	}
	if (model->active_task != NULL && model->active_task->id != NULL) {
		g_free(snapshot->selected_task_id);
		snapshot->selected_task_id = g_strdup(model->active_task->id);
	}
	snapshot->selected_terminal = selected_terminal;
	g_free(snapshot->active_group_id);
	snapshot->active_group_id = g_strdup(model->active_group != NULL &&
	                                    model->active_group->id != NULL
	                                    ? model->active_group->id : "root");
	snapshot->root_directory = g_strdup(model->root_group != NULL
	                                  ? model->root_group->directory : NULL);
	snapshot->sidebar_visible = sidebar_visible;
	snapshot->sidebar_width = sidebar_width;

	ordered_groups = sakura_workspace_model_ordered_groups(model);
	for (guint index = 0; index < ordered_groups->len; index++) {
		SakuraGroup *model_group = g_ptr_array_index(ordered_groups, index);
		SakuraSessionGroupRecord *group = g_new0(SakuraSessionGroupRecord, 1);

		group->id = g_strdup(model_group->id);
		group->parent_id = g_strdup(model_group->parent != NULL
		                            ? model_group->parent->id : "root");
		group->title = g_strdup(model_group->title != NULL
		                      ? model_group->title : "");
		group->directory = g_strdup(model_group->directory != NULL
		                           ? model_group->directory : "");
		group->order = model_group->order;
		group->archived = model_group->archived;
		g_ptr_array_add(snapshot->groups, group);
	}
	g_ptr_array_unref(ordered_groups);

	ordered_tasks = sakura_workspace_model_ordered_tasks(model);
	for (guint index = 0; index < ordered_tasks->len; index++) {
		SakuraTask *model_task = g_ptr_array_index(ordered_tasks, index);
		SakuraSessionTaskRecord *task = g_new0(SakuraSessionTaskRecord, 1);
		SakuraGroup *group = model_task->group != NULL
		                   ? model_task->group : model->root_group;

		task->id = g_strdup(model_task->id);
		task->parent_id = g_strdup(model_task->parent != NULL
		                           ? model_task->parent->id
		                           : group != NULL ? group->id : "root");
		task->group_id = g_strdup(group != NULL && group->id != NULL
		                         ? group->id : "root");
		task->title = g_strdup(model_task->title);
		task->provider = g_strdup(model_task->provider);
		task->external_id = g_strdup(model_task->external_id);
		task->url = g_strdup(model_task->url);
		task->status = model_task->status;
		task->order = model_task->order;
		task->archived = model_task->archived;
		g_ptr_array_add(snapshot->tasks, task);
	}
	g_ptr_array_unref(ordered_tasks);

	for (guint index = 0; index < model_panes->len; index++) {
		SakuraTab *tab = g_ptr_array_index(model_panes, index);
		SakuraSessionTabRecord *record = g_new0(SakuraSessionTabRecord, 1);
		const gchar *title = tab->user_title != NULL ? tab->user_title : "";
		SakuraGroup *group = sakura_workspace_snapshot_group_for_page(
			model, tab->page);

		record->parent_id = g_strdup(tab->page != NULL && tab->page->task != NULL
		                           ? tab->page->task->id
		                           : group != NULL ? group->id : "root");
		record->cwd = g_strdup(tab->cwd != NULL ? tab->cwd : "");
		record->terminal_id = g_strdup(tab->terminal_id != NULL
		                              ? tab->terminal_id : "");
		record->page_id = g_strdup(tab->page != NULL ? tab->page->id : NULL);
		record->order = index;
		record->cols = tab->agent_cols;
		record->rows = tab->agent_rows;
		if (tab->vte != NULL) {
			guint visible_cols = vte_terminal_get_column_count(VTE_TERMINAL(tab->vte));
			guint visible_rows = vte_terminal_get_row_count(VTE_TERMINAL(tab->vte));

			if (visible_cols > 0)
				record->cols = visible_cols;
			if (visible_rows > 0)
				record->rows = visible_rows;
		}
		record->kind = tab->kind;
		record->tool_id = tab->kind == SAKURA_TAB_TOOL
		               ? g_strdup(sakura_tool_id(tab->tool)) : NULL;
		record->tool_target = tab->kind == SAKURA_TAB_TOOL
		                   ? g_strdup(tab->tool_target) : NULL;
		record->codex_session_id = tab->kind == SAKURA_TAB_CODEX
		                        ? g_strdup(tab->codex_session_id) : NULL;
		record->codex_session_name = tab->kind == SAKURA_TAB_CODEX
		                          ? g_strdup(tab->codex_session_name) : NULL;
		record->codex_session_name_set_by_user = tab->kind == SAKURA_TAB_CODEX &&
			tab->codex_session_name_set_by_user;
		record->codex_model = tab->kind == SAKURA_TAB_CODEX
		                    ? g_strdup(tab->codex_model) : NULL;
		record->codex_reasoning_effort = tab->kind == SAKURA_TAB_CODEX
		                              ? g_strdup(tab->codex_reasoning_effort) : NULL;
		record->resume_on_start = tab->kind == SAKURA_TAB_CODEX &&
		                          tab->agent_backed &&
		                          tab->page != NULL && !tab->page->archived &&
		                          tab->codex_session_id != NULL &&
		                          tab->codex_session_id[0] != '\0';
		record->colorset = tab->colorset;
		record->title_set_by_user = tab->label_set_byuser;
		record->title = tab->label_set_byuser
		              ? g_strdup(title != NULL ? title : "") : NULL;
		record->status = tab->status;
		record->attention = tab->attention;
		record->attention_timestamp = tab->attention_timestamp;
		g_ptr_array_add(snapshot->tabs, record);
	}

	for (guint index = 0; model->pages != NULL && index < model->pages->len;
	     index++) {
		SakuraPage *page = g_ptr_array_index(model->pages, index);
		SakuraSessionPageRecord *page_record =
			sakura_workspace_snapshot_page_record(model, page);

		if (page_record != NULL)
			g_ptr_array_add(snapshot->pages, page_record);
		if (page != NULL)
			sakura_workspace_snapshot_append_layout(snapshot, page,
			                                      page->layout_root);
	}
	g_ptr_array_unref(model_panes);
	return snapshot;
}
