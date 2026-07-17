#include "sakura-private.h"


static gboolean
sakura_workspace_model_group_is_child_of(SakuraGroup *group,
                                          SakuraGroup *parent)
{
	return group != NULL &&
	       (group->parent == parent ||
	        (group->parent == NULL && parent == sakura.root_group));
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
sakura_workspace_model_normalize_task_orders(SakuraGroup *group,
                                             SakuraTask *parent)
{
	GList *siblings = NULL, *link;
	guint order = 0;

	if (sakura.tasks == NULL)
		return;
	for (guint index = 0; index < sakura.tasks->len; index++) {
		SakuraTask *task = g_ptr_array_index(sakura.tasks, index);

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
sakura_workspace_model_add_group(SakuraGroup *group)
{
	GList *link;

	if (group == NULL || group == sakura.root_group || group->id == NULL)
		return FALSE;
	for (link = sakura.groups; link != NULL; link = link->next) {
		SakuraGroup *existing = link->data;

		if (existing != NULL && g_strcmp0(existing->id, group->id) == 0)
			return FALSE;
	}
	sakura.groups = g_list_append(sakura.groups, group);
	return TRUE;
}


gboolean
sakura_workspace_model_can_remove_group(SakuraGroup *group)
{
	if (group == NULL || group == sakura.root_group || sakura.groups == NULL ||
	    g_list_find(sakura.groups, group) == NULL)
		return FALSE;
	if (sakura.groups != NULL) {
		for (GList *link = sakura.groups; link != NULL; link = link->next) {
			SakuraGroup *candidate = link->data;

			if (candidate != NULL && candidate != group &&
			    sakura_workspace_model_group_is_child_of(candidate, group))
				return FALSE;
		}
	}
	if (sakura.tasks != NULL) {
		for (guint index = 0; index < sakura.tasks->len; index++) {
			SakuraTask *task = g_ptr_array_index(sakura.tasks, index);

			if (task != NULL && task->group == group)
				return FALSE;
		}
	}
	if (sakura.pages != NULL) {
		for (guint index = 0; index < sakura.pages->len; index++) {
			SakuraPage *page = g_ptr_array_index(sakura.pages, index);

			if (page != NULL && page->group == group)
				return FALSE;
		}
	}
	return TRUE;
}


gboolean
sakura_workspace_model_remove_group(SakuraGroup *group)
{
	if (!sakura_workspace_model_can_remove_group(group))
		return FALSE;
	sakura.groups = g_list_remove(sakura.groups, group);
	sakura_group_free(group);
	return TRUE;
}


gboolean
sakura_workspace_model_add_task(SakuraTask *task)
{
	if (task == NULL || task->id == NULL || sakura_task_find_by_id(task->id) != NULL)
		return FALSE;
	if (sakura.tasks == NULL)
		sakura.tasks = g_ptr_array_new_with_free_func((GDestroyNotify)sakura_task_free);
	g_ptr_array_add(sakura.tasks, task);
	return TRUE;
}


gboolean
sakura_workspace_model_can_remove_task(SakuraTask *task)
{
	if (task == NULL || sakura.tasks == NULL ||
	    sakura_task_find_by_id(task->id) != task)
		return FALSE;
	if (sakura.tasks != NULL) {
		for (guint index = 0; index < sakura.tasks->len; index++) {
			SakuraTask *candidate = g_ptr_array_index(sakura.tasks, index);

			if (candidate != NULL && candidate->parent == task)
				return FALSE;
		}
	}
	if (sakura.pages != NULL) {
		for (guint index = 0; index < sakura.pages->len; index++) {
			SakuraPage *page = g_ptr_array_index(sakura.pages, index);

			if (page != NULL && page->task == task)
				return FALSE;
		}
	}
	return TRUE;
}


gboolean
sakura_workspace_model_remove_task(SakuraTask *task)
{
	if (!sakura_workspace_model_can_remove_task(task) ||
	    !g_ptr_array_remove(sakura.tasks, task))
		return FALSE;
	return TRUE;
}


SakuraTask *
sakura_task_find_by_id(const gchar *id)
{
	if (id == NULL || sakura.tasks == NULL)
		return NULL;
	for (guint index = 0; index < sakura.tasks->len; index++) {
		SakuraTask *task = g_ptr_array_index(sakura.tasks, index);

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
sakura_group_for_task(SakuraTask *task)
{
	if (task == NULL || task->group == NULL)
		return sakura.root_group;
	return task->group;
}


SakuraGroup *
sakura_group_for_session(SakuraSession *session)
{
	if (session == NULL)
		return sakura.root_group;
	if (session->group != NULL)
		return session->group;
	if (session->task != NULL)
		return sakura_group_for_task(session->task);
	return sakura.root_group;
}


gboolean
sakura_workspace_model_move_page_to_group(SakuraPage *page,
                                           SakuraGroup *group)
{
	SakuraGroup *old_group;

	if (page == NULL || group == NULL ||
	    (page->task == NULL && page->group == group))
		return FALSE;
	if (g_list_find(sakura.groups, group) == NULL)
		return FALSE;
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
sakura_workspace_model_attach_page(SakuraTask *task, SakuraPage *page)
{
	if (task == NULL || page == NULL || page->task == task ||
	    sakura_task_find_by_id(task->id) != task)
		return FALSE;
	page->task = task;
	page->group = sakura_group_for_task(task);
	return TRUE;
}


gboolean
sakura_workspace_model_detach_page(SakuraPage *page)
{
	SakuraTask *task;

	if (page == NULL || page->task == NULL)
		return FALSE;
	task = page->task;
	page->task = NULL;
	page->group = sakura_group_for_task(task);
	return TRUE;
}


gboolean
sakura_workspace_model_reorder_group(SakuraGroup *source,
                                      SakuraGroup *target,
                                      gboolean after)
{
	GList *ordered = NULL, *link, *target_link;
	SakuraGroup *parent;

	if (source == NULL || target == NULL || source == target ||
	    source == sakura.root_group || target == sakura.root_group)
		return FALSE;
	if (g_list_find(sakura.groups, source) == NULL ||
	    g_list_find(sakura.groups, target) == NULL)
		return FALSE;
	parent = source->parent != NULL ? source->parent : sakura.root_group;
	if (!sakura_workspace_model_group_is_child_of(target, parent))
		return FALSE;
	for (link = sakura.groups; link != NULL; link = link->next) {
		SakuraGroup *group = link->data;

		if (group != NULL && group != source &&
		    sakura_workspace_model_group_is_child_of(group, parent))
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
sakura_workspace_model_append_task(SakuraTask *task, SakuraGroup *group)
{
	if (task == NULL || group == NULL || task->group != group || task->parent != NULL)
		return FALSE;
	if (g_list_find(sakura.groups, group) == NULL)
		return FALSE;
	if (sakura_task_find_by_id(task->id) != task)
		return FALSE;
	task->order = G_MAXUINT;
	sakura_workspace_model_normalize_task_orders(group, NULL);
	return TRUE;
}
