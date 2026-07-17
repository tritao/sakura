#include "sakura-private.h"


SakuraWorkspaceModel *
sakura_workspace_model_new(void)
{
	SakuraWorkspaceModel *model = g_new0(SakuraWorkspaceModel, 1);

	model->tabs = g_ptr_array_new();
	model->pages = g_ptr_array_new();
	model->panes = g_ptr_array_new();
	model->tasks = g_ptr_array_new_with_free_func(
		(GDestroyNotify)sakura_task_free);
	return model;
}


void
sakura_workspace_model_free(SakuraWorkspaceModel *model)
{
	if (model == NULL)
		return;
	if (model->pages != NULL) {
		for (guint index = 0; index < model->pages->len; index++)
			sakura_page_free(g_ptr_array_index(model->pages, index));
	}
	if (model->panes != NULL) {
		for (guint index = 0; index < model->panes->len; index++)
			sakura_tab_free(g_ptr_array_index(model->panes, index));
	}
	g_clear_pointer(&model->tabs, g_ptr_array_unref);
	g_clear_pointer(&model->pages, g_ptr_array_unref);
	g_clear_pointer(&model->panes, g_ptr_array_unref);
	g_clear_pointer(&model->tasks, g_ptr_array_unref);
	g_list_free_full(model->groups, (GDestroyNotify)sakura_group_free);
	g_free(model);
}


gboolean
sakura_workspace_model_set_root(SakuraWorkspaceModel *model,
                                SakuraGroup *root_group)
{
	if (model == NULL || root_group == NULL || model->root_group != NULL)
		return FALSE;
	root_group->parent = NULL;
	model->root_group = root_group;
	model->groups = g_list_append(model->groups, root_group);
	return TRUE;
}


static gboolean
sakura_workspace_model_group_belongs_to(const SakuraWorkspaceModel *model,
                                        const SakuraGroup *group,
                                        const SakuraGroup *parent)
{
	return group != NULL &&
	       (group->parent == parent ||
	        (group->parent == NULL && model != NULL &&
	         parent == model->root_group));
}


static gint
sakura_workspace_model_group_order_compare(gconstpointer first,
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
sakura_workspace_model_append_ordered_groups(
	const SakuraWorkspaceModel *model, SakuraGroup *parent,
	GPtrArray *groups, GHashTable *seen)
{
	GList *children = NULL, *group_link;

	if (model == NULL)
		return;
	for (group_link = model->groups; group_link != NULL;
	     group_link = group_link->next) {
		SakuraGroup *group = group_link->data;

		if (group != NULL && group != model->root_group &&
		    sakura_workspace_model_group_belongs_to(model, group, parent))
			children = g_list_append(children, group);
	}
	children = g_list_sort(children,
	                       sakura_workspace_model_group_order_compare);
	for (group_link = children; group_link != NULL; group_link = group_link->next) {
		SakuraGroup *group = group_link->data;

		if (g_hash_table_add(seen, group)) {
			g_ptr_array_add(groups, group);
			sakura_workspace_model_append_ordered_groups(model, group,
			                                             groups, seen);
		}
	}
	g_list_free(children);
}


GPtrArray *
sakura_workspace_model_ordered_groups(const SakuraWorkspaceModel *model)
{
	GPtrArray *groups = g_ptr_array_new();
	GHashTable *seen = g_hash_table_new(g_direct_hash, g_direct_equal);
	GList *group_link;

	if (model != NULL) {
		sakura_workspace_model_append_ordered_groups(model,
		                                             model->root_group,
		                                             groups, seen);
		/* Preserve malformed or partially migrated model entries instead of
		 * dropping them from persistence. Valid hierarchies were emitted above. */
		for (group_link = model->groups; group_link != NULL;
		     group_link = group_link->next) {
			SakuraGroup *group = group_link->data;

			if (group != NULL && group != model->root_group &&
			    g_hash_table_add(seen, group))
				g_ptr_array_add(groups, group);
		}
	}
	g_hash_table_destroy(seen);
	return groups;
}


static gint
sakura_workspace_model_task_order_compare(gconstpointer first,
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
sakura_workspace_model_append_task_subtree(const SakuraWorkspaceModel *model,
                                            SakuraTask *parent,
                                            GPtrArray *tasks,
                                            GHashTable *seen)
{
	GList *children = NULL, *task_link;

	if (model == NULL || parent == NULL || !g_hash_table_add(seen, parent))
		return;
	g_ptr_array_add(tasks, parent);
	for (guint index = 0; index < model->tasks->len; index++) {
		SakuraTask *child = g_ptr_array_index(model->tasks, index);

		if (child != NULL && child->parent == parent)
			children = g_list_append(children, child);
	}
	children = g_list_sort(children,
	                       sakura_workspace_model_task_order_compare);
	for (task_link = children; task_link != NULL; task_link = task_link->next)
		sakura_workspace_model_append_task_subtree(model, task_link->data,
		                                           tasks, seen);
	g_list_free(children);
}


static gboolean
sakura_workspace_model_task_belongs_to(const SakuraWorkspaceModel *model,
                                       const SakuraTask *task,
                                       const SakuraGroup *group)
{
	return task != NULL &&
	       (task->group == group ||
	        (task->group == NULL && model != NULL &&
	         group == model->root_group));
}


static void
sakura_workspace_model_append_ordered_tasks_for_group(
	const SakuraWorkspaceModel *model, SakuraGroup *group,
	GPtrArray *tasks, GHashTable *seen)
{
	GList *children = NULL, *task_link;

	if (model == NULL || model->tasks == NULL)
		return;
	for (guint index = 0; index < model->tasks->len; index++) {
		SakuraTask *task = g_ptr_array_index(model->tasks, index);

		if (task != NULL && task->parent == NULL &&
		    sakura_workspace_model_task_belongs_to(model, task, group))
			children = g_list_append(children, task);
	}
	children = g_list_sort(children,
	                       sakura_workspace_model_task_order_compare);
	for (task_link = children; task_link != NULL; task_link = task_link->next)
		sakura_workspace_model_append_task_subtree(model, task_link->data,
		                                           tasks, seen);
	g_list_free(children);
}


GPtrArray *
sakura_workspace_model_ordered_tasks(const SakuraWorkspaceModel *model)
{
	GPtrArray *tasks = g_ptr_array_new();
	GPtrArray *ordered_groups;
	GHashTable *seen;

	if (model == NULL || model->tasks == NULL)
		return tasks;
	seen = g_hash_table_new(g_direct_hash, g_direct_equal);
	sakura_workspace_model_append_ordered_tasks_for_group(model,
	                                                       model->root_group,
	                                                       tasks, seen);
	ordered_groups = sakura_workspace_model_ordered_groups(model);
	for (guint index = 0; index < ordered_groups->len; index++)
		sakura_workspace_model_append_ordered_tasks_for_group(
			model, g_ptr_array_index(ordered_groups, index), tasks, seen);
	g_ptr_array_unref(ordered_groups);
	/* Preserve malformed or partially migrated task entries instead of
	 * dropping them from persistence. */
	for (guint index = 0; index < model->tasks->len; index++)
		sakura_workspace_model_append_task_subtree(
		model, g_ptr_array_index(model->tasks, index), tasks, seen);
	g_hash_table_destroy(seen);
	return tasks;
}


static SakuraGroup *
sakura_workspace_model_find_group(SakuraWorkspaceModel *model,
                                  const gchar *id)
{
	if (model == NULL || id == NULL || id[0] == '\0' ||
	    g_strcmp0(id, "root") == 0)
		return model != NULL ? model->root_group : NULL;
	for (GList *link = model->groups; link != NULL; link = link->next) {
		SakuraGroup *group = link->data;

		if (group != NULL && g_strcmp0(group->id, id) == 0)
			return group;
	}
	return NULL;
}


static void
sakura_workspace_model_update_group_id(SakuraWorkspaceModel *model,
                                        SakuraGroup *group)
{
	gchar *end = NULL;
	guint id_number;

	if (model == NULL || group == NULL ||
	    !g_str_has_prefix(group->id, "group-"))
		return;
	id_number = (guint)g_ascii_strtoull(group->id + strlen("group-"), &end, 10);
	if (end != group->id + strlen("group-") &&
	    id_number >= model->next_group_id)
		model->next_group_id = id_number + 1;
}


static void
sakura_workspace_model_update_task_id(SakuraWorkspaceModel *model,
                                       SakuraTask *task)
{
	gchar *end = NULL;
	guint id_number;

	if (model == NULL || task == NULL ||
	    !g_str_has_prefix(task->id, "task-"))
		return;
	id_number = (guint)g_ascii_strtoull(task->id + strlen("task-"), &end, 10);
	if (end != task->id + strlen("task-") &&
	    id_number >= model->next_task_id)
		model->next_task_id = id_number + 1;
}


gboolean
sakura_workspace_model_restore_snapshot(SakuraWorkspaceModel *model,
                                         const SakuraSessionSnapshot *snapshot)
{
	guint remaining, pass;

	if (model == NULL || snapshot == NULL || model->root_group == NULL)
		return FALSE;

	remaining = snapshot->groups != NULL ? snapshot->groups->len : 0;
	for (pass = 0; remaining > 0 && pass <= remaining; pass++) {
		gboolean progress = FALSE;

		for (guint index = 0; index < snapshot->groups->len; index++) {
			SakuraSessionGroupRecord *record =
				g_ptr_array_index(snapshot->groups, index);
			SakuraGroup *parent, *group;

			if (record == NULL || record->id == NULL ||
			    sakura_workspace_model_find_group(model, record->id) != NULL)
				continue;
			parent = sakura_workspace_model_find_group(model, record->parent_id);
			if (parent == NULL)
				continue;
			group = sakura_group_new(record->id, record->title, parent);
			group->directory = g_strdup(record->directory);
			group->order = record->order;
			if (!sakura_workspace_model_add_group(model, group)) {
				sakura_group_free(group);
				continue;
			}
			sakura_workspace_model_update_group_id(model, group);
			remaining--;
			progress = TRUE;
		}
		if (!progress)
			break;
	}
	/* Preserve malformed/orphaned group records without allowing a parent cycle
	 * to prevent the rest of the workspace from being restored. */
	if (remaining > 0) {
		for (guint index = 0; index < snapshot->groups->len; index++) {
			SakuraSessionGroupRecord *record =
				g_ptr_array_index(snapshot->groups, index);
			SakuraGroup *group;

			if (record == NULL || record->id == NULL ||
			    sakura_workspace_model_find_group(model, record->id) != NULL)
				continue;
			group = sakura_group_new(record->id, record->title,
			                         model->root_group);
			group->directory = g_strdup(record->directory);
			group->order = record->order;
			if (sakura_workspace_model_add_group(model, group))
				sakura_workspace_model_update_group_id(model, group);
			else
				sakura_group_free(group);
		}
	}

	remaining = snapshot->tasks != NULL ? snapshot->tasks->len : 0;
	for (pass = 0; remaining > 0 && pass <= remaining; pass++) {
		gboolean progress = FALSE;

		for (guint index = 0; index < snapshot->tasks->len; index++) {
			SakuraSessionTaskRecord *record =
				g_ptr_array_index(snapshot->tasks, index);
			SakuraTask *parent, *task;
			SakuraGroup *group, *parent_group;

			if (record == NULL || record->id == NULL ||
			    sakura_workspace_model_find_task(model, record->id) != NULL)
				continue;
			parent = record->parent_id != NULL &&
			         g_strcmp0(record->parent_id, "root") != 0
			       ? sakura_workspace_model_find_task(model, record->parent_id)
			       : NULL;
			parent_group = parent == NULL && record->parent_id != NULL &&
			               record->parent_id[0] != '\0' &&
			               g_strcmp0(record->parent_id, "root") != 0
			             ? sakura_workspace_model_find_group(model, record->parent_id)
			             : NULL;
			if (record->parent_id != NULL && record->parent_id[0] != '\0' &&
			    g_strcmp0(record->parent_id, "root") != 0 &&
			    parent == NULL && parent_group == NULL)
				continue;
			group = parent != NULL ? parent->group
			                     : parent_group != NULL ? parent_group
			                     : sakura_workspace_model_find_group(model, record->group_id);
			if (group == NULL)
				continue;
			task = g_new0(SakuraTask, 1);
			task->id = g_strdup(record->id);
			task->title = g_strdup(record->title != NULL ? record->title : "");
			task->provider = g_strdup(record->provider != NULL ? record->provider : "local");
			task->external_id = g_strdup(record->external_id);
			task->url = g_strdup(record->url);
			task->status = record->status;
			task->order = record->order;
			task->parent = parent;
			task->group = group;
			if (!sakura_workspace_model_add_task(model, task)) {
				sakura_task_free(task);
				continue;
			}
			sakura_workspace_model_update_task_id(model, task);
			remaining--;
			progress = TRUE;
		}
		if (!progress)
			break;
	}
	/* Cyclic or orphaned task parents become top-level tasks in their recorded
	 * group. The data remains visible and can be repaired by the user. */
	if (remaining > 0) {
		for (guint index = 0; index < snapshot->tasks->len; index++) {
			SakuraSessionTaskRecord *record =
				g_ptr_array_index(snapshot->tasks, index);
			SakuraTask *task;
			SakuraGroup *group;

			if (record == NULL || record->id == NULL ||
			    sakura_workspace_model_find_task(model, record->id) != NULL)
				continue;
			group = sakura_workspace_model_find_group(model, record->group_id);
			if (group == NULL)
				group = model->root_group;
			task = g_new0(SakuraTask, 1);
			task->id = g_strdup(record->id);
			task->title = g_strdup(record->title != NULL ? record->title : "");
			task->provider = g_strdup(record->provider != NULL ? record->provider : "local");
			task->external_id = g_strdup(record->external_id);
			task->url = g_strdup(record->url);
			task->status = record->status;
			task->order = record->order;
			task->group = group;
			if (!sakura_workspace_model_add_task(model, task)) {
				sakura_task_free(task);
				continue;
			}
			sakura_workspace_model_update_task_id(model, task);
		}
	}
	return TRUE;
}


static gboolean
sakura_workspace_model_group_is_child_of(SakuraWorkspaceModel *model,
                                          SakuraGroup *group,
                                          SakuraGroup *parent)
{
	return group != NULL &&
	       (group->parent == parent ||
	        (group->parent == NULL && parent == model->root_group));
}


static void
sakura_workspace_model_normalize_task_orders(SakuraWorkspaceModel *model,
                                             SakuraGroup *group,
                                             SakuraTask *parent)
{
	GList *siblings = NULL, *link;
	guint order = 0;

	if (model->tasks == NULL)
		return;
	for (guint index = 0; index < model->tasks->len; index++) {
		SakuraTask *task = g_ptr_array_index(model->tasks, index);

		if (task != NULL && task->group == group && task->parent == parent)
			siblings = g_list_prepend(siblings, task);
	}
	siblings = g_list_sort(siblings, sakura_workspace_model_task_order_compare);
	for (link = siblings; link != NULL; link = link->next)
		((SakuraTask *)link->data)->order = order++;
	g_list_free(siblings);
}


SakuraGroup *
sakura_group_new(const gchar *id, const gchar *title, SakuraGroup *parent)
{
	SakuraGroup *group = g_new0(SakuraGroup, 1);

	group->id = g_strdup(id);
	group->title = g_strdup(title != NULL ? title : "");
	group->parent = parent;
	return group;
}


void
sakura_group_free(SakuraGroup *group)
{
	if (group == NULL)
		return;
	g_free(group->id);
	g_free(group->title);
	g_free(group->directory);
	g_free(group->last_terminal_id);
	g_free(group);
}


gboolean
sakura_workspace_model_add_group(SakuraWorkspaceModel *model,
                                 SakuraGroup *group)
{
	GList *link;

	if (model == NULL || group == NULL || group == model->root_group ||
	    group->id == NULL)
		return FALSE;
	for (link = model->groups; link != NULL; link = link->next) {
		SakuraGroup *existing = link->data;

		if (existing != NULL && g_strcmp0(existing->id, group->id) == 0)
			return FALSE;
	}
	model->groups = g_list_append(model->groups, group);
	return TRUE;
}


gboolean
sakura_workspace_model_can_remove_group(SakuraWorkspaceModel *model,
                                        SakuraGroup *group)
{
	if (model == NULL || group == NULL || group == model->root_group ||
	    model->groups == NULL ||
	    g_list_find(model->groups, group) == NULL)
		return FALSE;
	if (model->groups != NULL) {
		for (GList *link = model->groups; link != NULL; link = link->next) {
			SakuraGroup *candidate = link->data;

			if (candidate != NULL && candidate != group &&
			    sakura_workspace_model_group_is_child_of(model, candidate, group))
				return FALSE;
		}
	}
	if (model->tasks != NULL) {
		for (guint index = 0; index < model->tasks->len; index++) {
			SakuraTask *task = g_ptr_array_index(model->tasks, index);

			if (task != NULL && task->group == group)
				return FALSE;
		}
	}
	if (model->pages != NULL) {
		for (guint index = 0; index < model->pages->len; index++) {
			SakuraPage *page = g_ptr_array_index(model->pages, index);

			if (page != NULL && page->group == group)
				return FALSE;
		}
	}
	return TRUE;
}


gboolean
sakura_workspace_model_remove_group(SakuraWorkspaceModel *model,
                                    SakuraGroup *group)
{
	if (!sakura_workspace_model_can_remove_group(model, group))
		return FALSE;
	model->groups = g_list_remove(model->groups, group);
	sakura_group_free(group);
	return TRUE;
}


gboolean
sakura_workspace_model_add_task(SakuraWorkspaceModel *model,
                                SakuraTask *task)
{
	if (model == NULL || task == NULL || task->id == NULL ||
	    sakura_workspace_model_find_task(model, task->id) != NULL)
		return FALSE;
	if (model->tasks == NULL)
		model->tasks = g_ptr_array_new_with_free_func((GDestroyNotify)sakura_task_free);
	g_ptr_array_add(model->tasks, task);
	return TRUE;
}


gboolean
sakura_workspace_model_can_remove_task(SakuraWorkspaceModel *model,
                                       SakuraTask *task)
{
	if (model == NULL || task == NULL || model->tasks == NULL ||
	    sakura_workspace_model_find_task(model, task->id) != task)
		return FALSE;
	if (model->tasks != NULL) {
		for (guint index = 0; index < model->tasks->len; index++) {
			SakuraTask *candidate = g_ptr_array_index(model->tasks, index);

			if (candidate != NULL && candidate->parent == task)
				return FALSE;
		}
	}
	if (model->pages != NULL) {
		for (guint index = 0; index < model->pages->len; index++) {
			SakuraPage *page = g_ptr_array_index(model->pages, index);

			if (page != NULL && page->task == task)
				return FALSE;
		}
	}
	return TRUE;
}


gboolean
sakura_workspace_model_remove_task(SakuraWorkspaceModel *model,
                                   SakuraTask *task)
{
	if (!sakura_workspace_model_can_remove_task(model, task) ||
	    !g_ptr_array_remove(model->tasks, task))
		return FALSE;
	return TRUE;
}


SakuraTask *
sakura_workspace_model_find_task(SakuraWorkspaceModel *model,
                                 const gchar *id)
{
	if (model == NULL || id == NULL || model->tasks == NULL)
		return NULL;
	for (guint index = 0; index < model->tasks->len; index++) {
		SakuraTask *task = g_ptr_array_index(model->tasks, index);

		if (task != NULL && g_strcmp0(task->id, id) == 0)
			return task;
	}
	return NULL;
}


void
sakura_task_free(SakuraTask *task)
{
	if (task == NULL)
		return;
	g_free(task->id);
	g_free(task->title);
	g_free(task->provider);
	g_free(task->external_id);
	g_free(task->url);
	g_free(task);
}


SakuraGroup *
sakura_workspace_model_group_for_task(SakuraWorkspaceModel *model,
                                      SakuraTask *task)
{
	if (model == NULL)
		return NULL;
	if (task == NULL || task->group == NULL)
		return model->root_group;
	return task->group;
}


SakuraGroup *
sakura_workspace_model_group_for_session(SakuraWorkspaceModel *model,
                                         SakuraSession *session)
{
	if (model == NULL)
		return NULL;
	if (session == NULL)
		return model->root_group;
	if (session->group != NULL)
		return session->group;
	if (session->task != NULL)
		return sakura_workspace_model_group_for_task(model, session->task);
	return model->root_group;
}


gboolean
sakura_workspace_model_move_page_to_group(SakuraWorkspaceModel *model,
                                          SakuraPage *page,
                                          SakuraGroup *group)
{
	SakuraGroup *old_group;

	if (model == NULL || page == NULL || group == NULL)
		return FALSE;
	if (g_list_find(model->groups, group) == NULL)
		return FALSE;
	if (page->task == NULL && page->group == group)
		return TRUE;
	old_group = page->group;
	page->task = NULL;
	page->group = group;
	if (old_group != NULL && page->active_tab != NULL &&
	    g_strcmp0(old_group->last_terminal_id,
              page->active_tab->terminal_id) == 0)
		g_clear_pointer(&old_group->last_terminal_id, g_free);
	return TRUE;
}


gboolean
sakura_workspace_model_attach_page(SakuraWorkspaceModel *model,
                                   SakuraTask *task,
                                   SakuraPage *page)
{
	if (model == NULL || task == NULL || page == NULL || page->task == task ||
	    sakura_workspace_model_find_task(model, task->id) != task)
		return FALSE;
	page->task = task;
	page->group = sakura_workspace_model_group_for_task(model, task);
	return TRUE;
}


gboolean
sakura_workspace_model_detach_page(SakuraWorkspaceModel *model,
                                   SakuraPage *page)
{
	SakuraTask *task;

	if (model == NULL || page == NULL || page->task == NULL)
		return FALSE;
	task = page->task;
	page->task = NULL;
	page->group = sakura_workspace_model_group_for_task(model, task);
	return TRUE;
}


gboolean
sakura_workspace_model_reorder_group(SakuraWorkspaceModel *model,
                                      SakuraGroup *source,
                                      SakuraGroup *target,
                                      gboolean after)
{
	GList *ordered = NULL, *link, *target_link;
	SakuraGroup *parent;

	if (model == NULL || source == NULL || target == NULL || source == target ||
	    source == model->root_group || target == model->root_group)
		return FALSE;
	if (g_list_find(model->groups, source) == NULL ||
	    g_list_find(model->groups, target) == NULL)
		return FALSE;
	parent = source->parent != NULL ? source->parent : model->root_group;
	if (!sakura_workspace_model_group_is_child_of(model, target, parent))
		return FALSE;
	for (link = model->groups; link != NULL; link = link->next) {
		SakuraGroup *group = link->data;

		if (group != NULL && group != source &&
		    sakura_workspace_model_group_is_child_of(model, group, parent))
			ordered = g_list_prepend(ordered, group);
	}
	ordered = g_list_sort(ordered, sakura_workspace_model_group_order_compare);
	target_link = g_list_find(ordered, target);
	if (target_link == NULL) {
		g_list_free(ordered);
		return FALSE;
	}
	if (after) {
		if (target_link->next != NULL)
			ordered = g_list_insert_before(ordered, target_link->next, source);
		else
			ordered = g_list_append(ordered, source);
	} else {
		ordered = g_list_insert_before(ordered, target_link, source);
	}
	for (link = ordered; link != NULL; link = link->next)
		((SakuraGroup *)link->data)->order = g_list_position(ordered, link);
	g_list_free(ordered);
	return TRUE;
}


gboolean
sakura_workspace_model_append_task(SakuraWorkspaceModel *model,
                                   SakuraTask *task,
                                   SakuraGroup *group)
{
	if (model == NULL || task == NULL || group == NULL || task->group != group ||
	    task->parent != NULL)
		return FALSE;
	if (g_list_find(model->groups, group) == NULL)
		return FALSE;
	if (sakura_workspace_model_find_task(model, task->id) != task)
		return FALSE;
	task->order = G_MAXUINT;
	sakura_workspace_model_normalize_task_orders(model, group, NULL);
	return TRUE;
}
