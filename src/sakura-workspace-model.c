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
sakura_workspace_model_group_is_child_of(SakuraWorkspaceModel *model,
                                          SakuraGroup *group,
                                          SakuraGroup *parent)
{
	return group != NULL &&
	       (group->parent == parent ||
	        (group->parent == NULL && parent == model->root_group));
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
