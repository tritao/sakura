#include "sakura-core.h"

static gint sakura_core_group_order_compare(gconstpointer first,
	                                         gconstpointer second);
static gint sakura_core_task_order_compare(gconstpointer first,
	                                        gconstpointer second);

static GQuark
sakura_core_workspace_error_quark(void)
{
	return g_quark_from_static_string("sakura-core-workspace-error");
}


static gboolean
sakura_core_workspace_contains_group(const SakuraCoreWorkspace *workspace,
	                                    const SakuraCoreGroup *group)
{
	if (workspace == NULL || workspace->groups == NULL || group == NULL)
		return FALSE;
	return g_ptr_array_find(workspace->groups, group, NULL);
}


static gboolean
sakura_core_workspace_contains_task(const SakuraCoreWorkspace *workspace,
	                                   const SakuraCoreTask *task)
{
	if (workspace == NULL || workspace->tasks == NULL || task == NULL)
		return FALSE;
	return g_ptr_array_find(workspace->tasks, task, NULL);
}


static gboolean
sakura_core_workspace_contains_terminal(const SakuraCoreWorkspace *workspace,
	                                        const SakuraCoreTerminal *terminal)
{
	if (workspace == NULL || workspace->terminals == NULL || terminal == NULL)
		return FALSE;
	return g_ptr_array_find(workspace->terminals, terminal, NULL);
}


SakuraCoreWorkspace *
sakura_core_workspace_new(void)
{
	SakuraCoreWorkspace *workspace = g_new0(SakuraCoreWorkspace, 1);

	workspace->groups = g_ptr_array_new_with_free_func(
		(GDestroyNotify)sakura_core_group_free);
	workspace->tasks = g_ptr_array_new_with_free_func(
		(GDestroyNotify)sakura_core_task_free);
	workspace->pages = g_ptr_array_new_with_free_func(
		(GDestroyNotify)sakura_core_page_free);
	workspace->terminals = g_ptr_array_new_with_free_func(
		(GDestroyNotify)sakura_core_terminal_free);
	return workspace;
}


void
sakura_core_workspace_free(SakuraCoreWorkspace *workspace)
{
	if (workspace == NULL)
		return;
	g_clear_pointer(&workspace->terminals, g_ptr_array_unref);
	g_clear_pointer(&workspace->pages, g_ptr_array_unref);
	g_clear_pointer(&workspace->tasks, g_ptr_array_unref);
	g_clear_pointer(&workspace->groups, g_ptr_array_unref);
	g_free(workspace);
}


SakuraCoreGroup *
sakura_core_group_new(const gchar *id,
	                  const gchar *title,
	                  SakuraCoreGroup *parent)
{
	SakuraCoreGroup *group = g_new0(SakuraCoreGroup, 1);

	group->id = g_strdup(id);
	group->title = g_strdup(title != NULL ? title : "");
	group->parent = parent;
	return group;
}


void
sakura_core_group_free(SakuraCoreGroup *group)
{
	if (group == NULL)
		return;
	g_free(group->id);
	g_free(group->title);
	g_free(group->directory);
	g_free(group);
}


SakuraCoreTask *
sakura_core_task_new(const gchar *id,
	                 const gchar *title,
	                 SakuraCoreGroup *group,
	                 SakuraCoreTask *parent)
{
	SakuraCoreTask *task = g_new0(SakuraCoreTask, 1);

	task->id = g_strdup(id);
	task->title = g_strdup(title != NULL ? title : "");
	task->provider = g_strdup("local");
	task->status = SAKURA_TASK_READY;
	task->group = group;
	task->parent = parent;
	return task;
}


void
sakura_core_task_free(SakuraCoreTask *task)
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


SakuraCorePage *
sakura_core_page_new(const gchar *id, SakuraCoreGroup *group,
                     SakuraCoreTask *task)
{
	SakuraCorePage *page = g_new0(SakuraCorePage, 1);

	page->id = g_strdup(id);
	page->title = g_strdup("");
	page->group = group;
	page->task = task;
	return page;
}


void
sakura_core_page_free(SakuraCorePage *page)
{
	if (page == NULL)
		return;
	g_free(page->id);
	g_free(page->title);
	g_free(page->root_layout_id);
	g_free(page->active_terminal_id);
	g_free(page);
}


SakuraCoreTerminal *
sakura_core_terminal_new(const gchar *id, const gchar *cwd,
	                       SakuraCoreGroup *group, SakuraCoreTask *task,
	                       guint cols, guint rows)
{
	SakuraCoreTerminal *terminal = g_new0(SakuraCoreTerminal, 1);

	terminal->id = g_strdup(id);
	terminal->cwd = g_strdup(cwd);
	terminal->group = group;
	terminal->task = task;
	terminal->cols = cols;
	terminal->rows = rows;
	terminal->status = SAKURA_TERMINAL_STARTING;
	return terminal;
}


void
sakura_core_terminal_free(SakuraCoreTerminal *terminal)
{
	if (terminal == NULL)
		return;
	g_free(terminal->id);
	g_free(terminal->cwd);
	g_free(terminal->title);
	g_free(terminal->codex_session_id);
	g_free(terminal->codex_reasoning_effort);
	g_free(terminal->tracking_token);
	g_free(terminal);
}


gboolean
sakura_core_workspace_set_root(SakuraCoreWorkspace *workspace,
	                            SakuraCoreGroup *root_group)
{
	if (workspace == NULL || root_group == NULL || workspace->root_group != NULL ||
	    root_group->parent != NULL || root_group->id == NULL ||
	    sakura_core_workspace_contains_group(workspace, root_group))
		return FALSE;
	workspace->root_group = root_group;
	g_ptr_array_add(workspace->groups, root_group);
	return TRUE;
}


static gboolean
sakura_core_workspace_group_is_child_of(const SakuraCoreWorkspace *workspace,
	                                        const SakuraCoreGroup *group,
	                                        const SakuraCoreGroup *parent)
{
	if (group == NULL || parent == NULL)
		return FALSE;
	return group->parent == parent ||
	       (group->parent == NULL && parent == workspace->root_group);
}


gboolean
sakura_core_workspace_add_group(SakuraCoreWorkspace *workspace,
	                             SakuraCoreGroup *group)
{
	if (workspace == NULL || group == NULL || group->id == NULL ||
	    workspace->groups == NULL || group == workspace->root_group ||
	    sakura_core_workspace_contains_group(workspace, group) ||
	    sakura_core_workspace_find_group(workspace, group->id) != NULL)
		return FALSE;
	if (group->parent == NULL)
		group->parent = workspace->root_group;
	if (group->parent != NULL &&
	    !sakura_core_workspace_contains_group(workspace, group->parent))
		return FALSE;
	g_ptr_array_add(workspace->groups, group);
	return TRUE;
}


static GList *
sakura_core_workspace_ordered_group_children(
	const SakuraCoreWorkspace *workspace, SakuraCoreGroup *parent,
	SakuraCoreGroup *exclude)
{
	GList *ordered = NULL;

	for (guint index = 0; workspace != NULL && workspace->groups != NULL &&
	                       index < workspace->groups->len; index++) {
		SakuraCoreGroup *group = g_ptr_array_index(workspace->groups, index);

		if (group != NULL && group != workspace->root_group && group != exclude &&
		    sakura_core_workspace_group_is_child_of(workspace, group, parent))
			ordered = g_list_prepend(ordered, group);
	}
	return g_list_sort(ordered, sakura_core_group_order_compare);
}


static void
sakura_core_workspace_normalize_group_orders(GList *ordered)
{
	for (GList *link = ordered; link != NULL; link = link->next)
		((SakuraCoreGroup *)link->data)->order = g_list_position(ordered, link);
}


gboolean
sakura_core_workspace_can_move_group(SakuraCoreWorkspace *workspace,
	                                   SakuraCoreGroup *source,
	                                   SakuraCoreGroup *parent,
	                                   SakuraCoreGroup *target)
{
	if (workspace == NULL || workspace->root_group == NULL || source == NULL ||
	    source == workspace->root_group ||
	    !sakura_core_workspace_contains_group(workspace, source))
		return FALSE;
	if (parent == NULL)
		parent = workspace->root_group;
	if (parent == source ||
	    !sakura_core_workspace_contains_group(workspace, parent))
		return FALSE;
	for (SakuraCoreGroup *candidate = parent; candidate != NULL;
	     candidate = candidate->parent) {
		if (candidate == source)
			return FALSE;
	}
	if (target == NULL)
		return TRUE;
	return target != workspace->root_group && target != source &&
	       sakura_core_workspace_contains_group(workspace, target) &&
	       sakura_core_workspace_group_is_child_of(workspace, target, parent);
}


gboolean
sakura_core_workspace_move_group(SakuraCoreWorkspace *workspace,
	                               SakuraCoreGroup *source,
	                               SakuraCoreGroup *parent,
	                               SakuraCoreGroup *target,
	                               gboolean after)
{
	GList *old_ordered, *new_ordered, *target_link;
	SakuraCoreGroup *old_parent;

	if (workspace == NULL)
		return FALSE;
	if (parent == NULL)
		parent = workspace->root_group;
	if (!sakura_core_workspace_can_move_group(workspace, source, parent, target))
		return FALSE;

	old_parent = source->parent != NULL ? source->parent : workspace->root_group;
	old_ordered = sakura_core_workspace_ordered_group_children(
		workspace, old_parent, source);
	new_ordered = sakura_core_workspace_ordered_group_children(
		workspace, parent, source);
	if (target == NULL) {
		new_ordered = g_list_append(new_ordered, source);
	} else {
		target_link = g_list_find(new_ordered, target);
		if (target_link == NULL) {
			g_list_free(old_ordered);
			g_list_free(new_ordered);
			return FALSE;
		}
		if (after && target_link->next != NULL)
			new_ordered = g_list_insert_before(new_ordered, target_link->next,
			                                  source);
		else if (after)
			new_ordered = g_list_append(new_ordered, source);
		else
			new_ordered = g_list_insert_before(new_ordered, target_link, source);
	}
	source->parent = parent;
	if (old_parent != parent)
		sakura_core_workspace_normalize_group_orders(old_ordered);
	sakura_core_workspace_normalize_group_orders(new_ordered);
	g_list_free(old_ordered);
	g_list_free(new_ordered);
	return TRUE;
}


gboolean
sakura_core_workspace_can_remove_group(SakuraCoreWorkspace *workspace,
	                                    SakuraCoreGroup *group)
{
	if (workspace == NULL || group == NULL || group == workspace->root_group ||
	    !sakura_core_workspace_contains_group(workspace, group))
		return FALSE;
	for (guint index = 0; index < workspace->groups->len; index++) {
		SakuraCoreGroup *candidate = g_ptr_array_index(workspace->groups, index);

		if (candidate != NULL && candidate != group &&
		    sakura_core_workspace_group_is_child_of(workspace, candidate, group))
			return FALSE;
	}
	for (guint index = 0; index < workspace->tasks->len; index++) {
		SakuraCoreTask *task = g_ptr_array_index(workspace->tasks, index);

		if (task != NULL && task->group == group)
			return FALSE;
	}
	for (guint index = 0; index < workspace->terminals->len; index++) {
		SakuraCoreTerminal *terminal = g_ptr_array_index(
			workspace->terminals, index);

		if (terminal != NULL && terminal->group == group)
			return FALSE;
	}
	for (guint index = 0; index < workspace->pages->len; index++) {
		SakuraCorePage *page = g_ptr_array_index(workspace->pages, index);

		if (page != NULL && page->group == group)
			return FALSE;
	}
	return TRUE;
}


gboolean
sakura_core_workspace_remove_group(SakuraCoreWorkspace *workspace,
	                               SakuraCoreGroup *group)
{
	if (!sakura_core_workspace_can_remove_group(workspace, group) ||
	    !g_ptr_array_remove(workspace->groups, group))
		return FALSE;
	if (workspace->active_group == group)
		workspace->active_group = workspace->root_group;
	return TRUE;
}


gboolean
sakura_core_workspace_add_task(SakuraCoreWorkspace *workspace,
	                            SakuraCoreTask *task)
{
	if (workspace == NULL || task == NULL || task->id == NULL ||
	    workspace->tasks == NULL ||
	    sakura_core_workspace_find_task(workspace, task->id) != NULL)
		return FALSE;
	if (task->group == NULL)
		task->group = workspace->root_group;
	if (task->group != NULL &&
	    !sakura_core_workspace_contains_group(workspace, task->group))
		return FALSE;
	if (task->parent != NULL) {
		if (!sakura_core_workspace_contains_task(workspace, task->parent))
			return FALSE;
		if (task->parent->group != task->group)
			return FALSE;
	}
	g_ptr_array_add(workspace->tasks, task);
	return TRUE;
}


gboolean
sakura_core_workspace_can_remove_task(SakuraCoreWorkspace *workspace,
	                                   SakuraCoreTask *task)
{
	if (workspace == NULL || task == NULL ||
	    !sakura_core_workspace_contains_task(workspace, task))
		return FALSE;
	for (guint index = 0; index < workspace->tasks->len; index++) {
		SakuraCoreTask *candidate = g_ptr_array_index(workspace->tasks, index);

		if (candidate != NULL && candidate != task && candidate->parent == task)
			return FALSE;
	}
	for (guint index = 0; index < workspace->terminals->len; index++) {
		SakuraCoreTerminal *terminal = g_ptr_array_index(
			workspace->terminals, index);

		if (terminal != NULL && terminal->task == task)
			return FALSE;
	}
	for (guint index = 0; index < workspace->pages->len; index++) {
		SakuraCorePage *page = g_ptr_array_index(workspace->pages, index);

		if (page != NULL && page->task == task)
			return FALSE;
	}
	return TRUE;
}


gboolean
sakura_core_workspace_remove_task(SakuraCoreWorkspace *workspace,
	                              SakuraCoreTask *task)
{
	if (!sakura_core_workspace_can_remove_task(workspace, task) ||
	    !g_ptr_array_remove(workspace->tasks, task))
		return FALSE;
	if (workspace->active_task == task)
		workspace->active_task = NULL;
	return TRUE;
}


static GList *
sakura_core_workspace_ordered_task_children(
	const SakuraCoreWorkspace *workspace, SakuraCoreGroup *group,
	SakuraCoreTask *parent, SakuraCoreTask *exclude)
{
	GList *ordered = NULL;

	for (guint index = 0; workspace != NULL && workspace->tasks != NULL &&
	                       index < workspace->tasks->len; index++) {
		SakuraCoreTask *task = g_ptr_array_index(workspace->tasks, index);

		if (task != NULL && task != exclude && task->group == group &&
		    task->parent == parent)
			ordered = g_list_prepend(ordered, task);
	}
	return g_list_sort(ordered, sakura_core_task_order_compare);
}


gboolean
sakura_core_workspace_move_task(SakuraCoreWorkspace *workspace,
	                              SakuraCoreTask *source,
	                              SakuraCoreGroup *group,
	                              SakuraCoreTask *parent,
	                              SakuraCoreTask *target,
	                              gboolean after)
{
	GList *old_ordered, *new_ordered, *target_link;
	SakuraCoreTask *candidate;
	SakuraCoreGroup *old_group;
	SakuraCoreTask *old_parent;

	if (workspace == NULL || source == NULL || group == NULL ||
	    !sakura_core_workspace_contains_task(workspace, source) ||
	    !sakura_core_workspace_contains_group(workspace, group) ||
	    source->group != group ||
	    (parent != NULL && (!sakura_core_workspace_contains_task(workspace, parent) ||
	                        parent->group != group)) ||
	    (target != NULL && (!sakura_core_workspace_contains_task(workspace, target) ||
	                        target == source || target->group != group ||
	                        target->parent != parent)))
		return FALSE;
	for (candidate = parent; candidate != NULL; candidate = candidate->parent) {
		if (candidate == source)
			return FALSE;
	}
	old_group = source->group;
	old_parent = source->parent;
	old_ordered = sakura_core_workspace_ordered_task_children(
		workspace, old_group, old_parent, source);
	new_ordered = sakura_core_workspace_ordered_task_children(
		workspace, group, parent, source);
	if (target == NULL)
		new_ordered = g_list_append(new_ordered, source);
	else {
		target_link = g_list_find(new_ordered, target);
		if (target_link == NULL) {
			g_list_free(old_ordered);
			g_list_free(new_ordered);
			return FALSE;
		}
		if (after && target_link->next != NULL)
			new_ordered = g_list_insert_before(new_ordered, target_link->next, source);
		else if (after)
			new_ordered = g_list_append(new_ordered, source);
		else
			new_ordered = g_list_insert_before(new_ordered, target_link, source);
	}
	source->group = group;
	source->parent = parent;
	if (old_group != group || old_parent != parent)
		for (GList *link = old_ordered; link != NULL; link = link->next)
			((SakuraCoreTask *)link->data)->order = g_list_position(old_ordered, link);
	for (GList *link = new_ordered; link != NULL; link = link->next)
		((SakuraCoreTask *)link->data)->order = g_list_position(new_ordered, link);
	g_list_free(old_ordered);
	g_list_free(new_ordered);
	return TRUE;
}


gboolean
sakura_core_workspace_add_terminal(SakuraCoreWorkspace *workspace,
	                                  SakuraCoreTerminal *terminal)
{
	if (workspace == NULL || terminal == NULL || terminal->id == NULL ||
	    workspace->terminals == NULL ||
	    sakura_core_workspace_contains_terminal(workspace, terminal) ||
	    sakura_core_workspace_find_terminal(workspace, terminal->id) != NULL)
		return FALSE;
	if (terminal->group == NULL)
		terminal->group = workspace->root_group;
	if (terminal->group != NULL &&
	    !sakura_core_workspace_contains_group(workspace, terminal->group))
		return FALSE;
	if (terminal->task != NULL &&
	    !sakura_core_workspace_contains_task(workspace, terminal->task))
		return FALSE;
	if (terminal->task != NULL && terminal->task->group != terminal->group)
		return FALSE;
	g_ptr_array_add(workspace->terminals, terminal);
	return TRUE;
}


gboolean
sakura_core_workspace_remove_terminal(SakuraCoreWorkspace *workspace,
	                                     SakuraCoreTerminal *terminal)
{
	if (workspace == NULL || terminal == NULL ||
	    !sakura_core_workspace_contains_terminal(workspace, terminal))
		return FALSE;
	return g_ptr_array_remove(workspace->terminals, terminal);
}


SakuraCoreGroup *
sakura_core_workspace_find_group(SakuraCoreWorkspace *workspace,
	                             const gchar *id)
{
	if (workspace == NULL || id == NULL || id[0] == '\0' ||
	    g_strcmp0(id, "root") == 0)
		return workspace != NULL ? workspace->root_group : NULL;
	for (guint index = 0; workspace->groups != NULL &&
	                       index < workspace->groups->len; index++) {
		SakuraCoreGroup *group = g_ptr_array_index(workspace->groups, index);

		if (group != NULL && g_strcmp0(group->id, id) == 0)
			return group;
	}
	return NULL;
}


SakuraCoreTask *
sakura_core_workspace_find_task(SakuraCoreWorkspace *workspace,
	                            const gchar *id)
{
	if (workspace == NULL || id == NULL || workspace->tasks == NULL)
		return NULL;
	for (guint index = 0; index < workspace->tasks->len; index++) {
		SakuraCoreTask *task = g_ptr_array_index(workspace->tasks, index);

		if (task != NULL && g_strcmp0(task->id, id) == 0)
			return task;
	}
	return NULL;
}


SakuraCorePage *
sakura_core_workspace_find_page(SakuraCoreWorkspace *workspace,
                                 const gchar *id)
{
	if (workspace == NULL || id == NULL || workspace->pages == NULL)
		return NULL;
	for (guint index = 0; index < workspace->pages->len; index++) {
		SakuraCorePage *page = g_ptr_array_index(workspace->pages, index);

		if (page != NULL && g_strcmp0(page->id, id) == 0)
			return page;
	}
	return NULL;
}


gboolean
sakura_core_workspace_add_page(SakuraCoreWorkspace *workspace,
                               SakuraCorePage *page)
{
	if (workspace == NULL || page == NULL || page->id == NULL ||
	    workspace->pages == NULL ||
	    sakura_core_workspace_find_page(workspace, page->id) != NULL)
		return FALSE;
	if (page->group == NULL)
		page->group = workspace->root_group;
	if (page->group != NULL &&
	    !sakura_core_workspace_contains_group(workspace, page->group))
		return FALSE;
	if (page->task != NULL &&
	    !sakura_core_workspace_contains_task(workspace, page->task))
		return FALSE;
	if (page->task != NULL && page->task->group != page->group)
		return FALSE;
	g_ptr_array_add(workspace->pages, page);
	return TRUE;
}


gboolean
sakura_core_workspace_remove_page(SakuraCoreWorkspace *workspace,
                                  SakuraCorePage *page)
{
	if (workspace == NULL || page == NULL || workspace->pages == NULL ||
	    !g_ptr_array_find(workspace->pages, page, NULL))
		return FALSE;
	return g_ptr_array_remove(workspace->pages, page);
}


SakuraCoreTerminal *
sakura_core_workspace_find_terminal(SakuraCoreWorkspace *workspace,
	                                  const gchar *id)
{
	if (workspace == NULL || id == NULL || workspace->terminals == NULL)
		return NULL;
	for (guint index = 0; index < workspace->terminals->len; index++) {
		SakuraCoreTerminal *terminal = g_ptr_array_index(
			workspace->terminals, index);

		if (terminal != NULL && g_strcmp0(terminal->id, id) == 0)
			return terminal;
	}
	return NULL;
}


static gint
sakura_core_group_order_compare(gconstpointer first,
	                              gconstpointer second)
{
	const SakuraCoreGroup *first_group = first;
	const SakuraCoreGroup *second_group = second;

	return first_group->order < second_group->order ? -1
	     : first_group->order > second_group->order ? 1 : 0;
}


static void
sakura_core_append_ordered_groups(const SakuraCoreWorkspace *workspace,
	                                 const SakuraCoreGroup *parent,
	                                 GPtrArray *ordered,
	                                 GHashTable *seen)
{
	GList *children = NULL;

	for (guint index = 0; workspace != NULL && workspace->groups != NULL &&
	                       index < workspace->groups->len; index++) {
		SakuraCoreGroup *group = g_ptr_array_index(workspace->groups, index);

		if (group != NULL && group != workspace->root_group &&
		    sakura_core_workspace_group_is_child_of(workspace, group, parent))
			children = g_list_prepend(children, group);
	}
	children = g_list_sort(children, sakura_core_group_order_compare);
	for (GList *link = children; link != NULL; link = link->next) {
		SakuraCoreGroup *group = link->data;

		if (g_hash_table_add(seen, group)) {
			g_ptr_array_add(ordered, group);
			sakura_core_append_ordered_groups(workspace, group, ordered, seen);
		}
	}
	g_list_free(children);
}


GPtrArray *
sakura_core_workspace_ordered_groups(const SakuraCoreWorkspace *workspace)
{
	GPtrArray *ordered = g_ptr_array_new();
	GHashTable *seen = g_hash_table_new(g_direct_hash, g_direct_equal);

	if (workspace != NULL) {
		sakura_core_append_ordered_groups(workspace, workspace->root_group,
		                                  ordered, seen);
		for (guint index = 0; workspace->groups != NULL &&
		                       index < workspace->groups->len; index++) {
			SakuraCoreGroup *group = g_ptr_array_index(workspace->groups, index);

			if (group != NULL && group != workspace->root_group &&
			    g_hash_table_add(seen, group))
				g_ptr_array_add(ordered, group);
		}
	}
	g_hash_table_destroy(seen);
	return ordered;
}


static gint
sakura_core_task_order_compare(gconstpointer first,
	                             gconstpointer second)
{
	const SakuraCoreTask *first_task = first;
	const SakuraCoreTask *second_task = second;

	return first_task->order < second_task->order ? -1
	     : first_task->order > second_task->order ? 1 : 0;
}


static void
sakura_core_append_task_subtree(const SakuraCoreWorkspace *workspace,
	                               SakuraCoreTask *parent,
	                               GPtrArray *ordered,
	                               GHashTable *seen)
{
	GList *children = NULL;

	if (parent == NULL || !g_hash_table_add(seen, parent))
		return;
	g_ptr_array_add(ordered, parent);
	for (guint index = 0; workspace != NULL && workspace->tasks != NULL &&
	                       index < workspace->tasks->len; index++) {
		SakuraCoreTask *task = g_ptr_array_index(workspace->tasks, index);

		if (task != NULL && task->parent == parent)
			children = g_list_prepend(children, task);
	}
	children = g_list_sort(children, sakura_core_task_order_compare);
	for (GList *link = children; link != NULL; link = link->next)
		sakura_core_append_task_subtree(workspace, link->data, ordered, seen);
	g_list_free(children);
}


static void
sakura_core_append_ordered_tasks_for_group(const SakuraCoreWorkspace *workspace,
	                                          SakuraCoreGroup *group,
	                                          GPtrArray *ordered,
	                                          GHashTable *seen)
{
	GList *tasks = NULL;

	for (guint index = 0; workspace != NULL && workspace->tasks != NULL &&
	                       index < workspace->tasks->len; index++) {
		SakuraCoreTask *task = g_ptr_array_index(workspace->tasks, index);

		if (task != NULL && task->parent == NULL &&
		    (task->group == group ||
		     (task->group == NULL && group == workspace->root_group)))
			tasks = g_list_prepend(tasks, task);
	}
	tasks = g_list_sort(tasks, sakura_core_task_order_compare);
	for (GList *link = tasks; link != NULL; link = link->next)
		sakura_core_append_task_subtree(workspace, link->data, ordered, seen);
	g_list_free(tasks);
}


GPtrArray *
sakura_core_workspace_ordered_tasks(const SakuraCoreWorkspace *workspace)
{
	GPtrArray *ordered = g_ptr_array_new();
	GPtrArray *groups;
	GHashTable *seen;

	if (workspace == NULL || workspace->tasks == NULL)
		return ordered;
	seen = g_hash_table_new(g_direct_hash, g_direct_equal);
	sakura_core_append_ordered_tasks_for_group(workspace,
	                                          workspace->root_group,
	                                          ordered, seen);
	groups = sakura_core_workspace_ordered_groups(workspace);
	for (guint index = 0; index < groups->len; index++)
		sakura_core_append_ordered_tasks_for_group(
			workspace, g_ptr_array_index(groups, index), ordered, seen);
	g_ptr_array_unref(groups);
	for (guint index = 0; index < workspace->tasks->len; index++)
		sakura_core_append_task_subtree(workspace,
		                                g_ptr_array_index(workspace->tasks, index),
		                                ordered, seen);
	g_hash_table_destroy(seen);
	return ordered;
}


static gboolean
sakura_core_group_is_within(const SakuraCoreGroup *group,
	                          const SakuraCoreGroup *ancestor)
{
	GHashTable *seen = g_hash_table_new(g_direct_hash, g_direct_equal);
	gboolean result = FALSE;

	while (group != NULL && g_hash_table_add(seen, (gpointer)group)) {
		if (group == ancestor) {
			result = TRUE;
			break;
		}
		group = group->parent;
	}
	g_hash_table_destroy(seen);
	return result;
}


static gboolean
sakura_core_task_is_within(const SakuraCoreTask *task,
	                         const SakuraCoreTask *ancestor)
{
	GHashTable *seen = g_hash_table_new(g_direct_hash, g_direct_equal);
	gboolean result = FALSE;

	while (task != NULL && g_hash_table_add(seen, (gpointer)task)) {
		if (task == ancestor) {
			result = TRUE;
			break;
		}
		task = task->parent;
	}
	g_hash_table_destroy(seen);
	return result;
}


gboolean
sakura_core_workspace_group_is_archived(const SakuraCoreWorkspace *workspace,
	                                     const SakuraCoreGroup *group)
{
	(void)workspace;
	while (group != NULL) {
		if (group->archived)
			return TRUE;
		group = group->parent;
	}
	return FALSE;
}


gboolean
sakura_core_workspace_task_is_archived(const SakuraCoreWorkspace *workspace,
                                    const SakuraCoreTask *task)
{
	const SakuraCoreTask *candidate = task;
	SakuraCoreGroup *group;

	if (task == NULL)
		return FALSE;
	while (candidate != NULL) {
		if (candidate->archived)
			return TRUE;
		candidate = candidate->parent;
	}
	group = task->group;
	return sakura_core_workspace_group_is_archived(workspace, group);
}


void
sakura_core_workspace_set_group_archived(SakuraCoreWorkspace *workspace,
	                                     SakuraCoreGroup *group,
	                                     gboolean archived)
{
	if (workspace == NULL || group == NULL)
		return;
	for (guint index = 0; index < workspace->groups->len; index++) {
		SakuraCoreGroup *candidate = g_ptr_array_index(workspace->groups, index);

		if (candidate != NULL && sakura_core_group_is_within(candidate, group))
			candidate->archived = archived;
	}
	for (guint index = 0; index < workspace->tasks->len; index++) {
		SakuraCoreTask *task = g_ptr_array_index(workspace->tasks, index);

		if (task != NULL && sakura_core_group_is_within(task->group, group))
			task->archived = archived;
	}
}


void
sakura_core_workspace_set_task_archived(SakuraCoreWorkspace *workspace,
	                                    SakuraCoreTask *task,
	                                    gboolean archived)
{
	if (workspace == NULL || task == NULL)
		return;
	for (guint index = 0; index < workspace->tasks->len; index++) {
		SakuraCoreTask *candidate = g_ptr_array_index(workspace->tasks, index);

		if (candidate != NULL && sakura_core_task_is_within(candidate, task))
			candidate->archived = archived;
	}
}


static gboolean
sakura_core_workspace_add_snapshot_group(SakuraCoreWorkspace *workspace,
	                                       const SakuraSessionGroupRecord *record)
{
	SakuraCoreGroup *group;
	SakuraCoreGroup *parent;

	if (record == NULL || record->id == NULL ||
	    sakura_core_workspace_find_group(workspace, record->id) != NULL)
		return FALSE;
	parent = sakura_core_workspace_find_group(workspace, record->parent_id);
	if (parent == NULL)
		parent = workspace->root_group;
	group = sakura_core_group_new(record->id, record->title, parent);
	group->directory = g_strdup(record->directory);
	group->order = record->order;
	group->archived = record->archived;
	if (!sakura_core_workspace_add_group(workspace, group)) {
		sakura_core_group_free(group);
		return FALSE;
	}
	return TRUE;
}


static gboolean
sakura_core_workspace_add_snapshot_page(
	SakuraCoreWorkspace *workspace, const SakuraSessionPageRecord *record)
{
	SakuraCoreTask *task;
	SakuraCoreGroup *group;
	SakuraCorePage *page;

	if (record == NULL || record->id == NULL ||
	    sakura_core_workspace_find_page(workspace, record->id) != NULL)
		return FALSE;
	task = record->task_id != NULL
	     ? sakura_core_workspace_find_task(workspace, record->task_id) : NULL;
	if (record->task_id != NULL && record->task_id[0] != '\0' &&
	    g_strcmp0(record->task_id, "root") != 0 && task == NULL)
		return FALSE;
	group = record->group_id != NULL
	      ? sakura_core_workspace_find_group(workspace, record->group_id) : NULL;
	if (task != NULL)
		group = task->group;
	if (group == NULL && record->parent_id != NULL) {
		group = sakura_core_workspace_find_group(workspace, record->parent_id);
		if (group == NULL) {
			task = sakura_core_workspace_find_task(workspace,
			                                       record->parent_id);
			if (task != NULL)
				group = task->group;
		}
	}
	if (group == NULL)
		group = workspace->root_group;
	page = sakura_core_page_new(record->id, group, task);
	g_free(page->title);
	page->title = g_strdup(record->title != NULL ? record->title : "");
	page->title_set_by_user = record->title_set_by_user;
	page->archived = record->archived;
	page->root_layout_id = g_strdup(record->root_layout_id);
	page->active_terminal_id = g_strdup(record->active_terminal_id);
	if (!sakura_core_workspace_add_page(workspace, page)) {
		sakura_core_page_free(page);
		return FALSE;
	}
	return TRUE;
}


SakuraCoreWorkspace *
sakura_core_workspace_from_snapshot(const SakuraSessionSnapshot *snapshot,
	                                GError **error)
{
	SakuraCoreWorkspace *workspace;
	SakuraCoreGroup *root;
	guint remaining;

	if (snapshot == NULL) {
		g_set_error_literal(error, sakura_core_workspace_error_quark(), 1,
		                    "Cannot restore a NULL session snapshot");
		return NULL;
	}
	workspace = sakura_core_workspace_new();
	root = sakura_core_group_new("root", "All terminals", NULL);
	root->directory = g_strdup(snapshot->root_directory);
	if (!sakura_core_workspace_set_root(workspace, root)) {
		sakura_core_group_free(root);
		sakura_core_workspace_free(workspace);
		g_set_error_literal(error, sakura_core_workspace_error_quark(), 2,
		                    "Cannot create the workspace root group");
		return NULL;
	}
	remaining = snapshot->groups != NULL ? snapshot->groups->len : 0;
	for (guint pass = 0; remaining > 0 && pass <= remaining; pass++) {
		gboolean progress = FALSE;

		for (guint index = 0; index < snapshot->groups->len; index++) {
			SakuraSessionGroupRecord *record =
				g_ptr_array_index(snapshot->groups, index);

			if (record != NULL && record->id != NULL &&
			    sakura_core_workspace_find_group(workspace, record->id) == NULL &&
			    (record->parent_id == NULL || record->parent_id[0] == '\0' ||
			     sakura_core_workspace_find_group(workspace,
			                                    record->parent_id) != NULL) &&
			    sakura_core_workspace_add_snapshot_group(workspace, record)) {
				remaining--;
				progress = TRUE;
			}
		}
		if (!progress)
			break;
	}
	/* Orphans and cycles remain visible under root, as in the desktop model. */
	if (remaining > 0 && snapshot->groups != NULL) {
		for (guint index = 0; index < snapshot->groups->len; index++) {
			SakuraSessionGroupRecord *record =
				g_ptr_array_index(snapshot->groups, index);
			SakuraCoreGroup *group;

			if (record == NULL || record->id == NULL ||
			    sakura_core_workspace_find_group(workspace, record->id) != NULL)
				continue;
			group = sakura_core_group_new(record->id, record->title,
			                              workspace->root_group);
			group->directory = g_strdup(record->directory);
			group->order = record->order;
			group->archived = record->archived;
			if (sakura_core_workspace_add_group(workspace, group))
				remaining--;
			else
				sakura_core_group_free(group);
		}
	}

	remaining = snapshot->tasks != NULL ? snapshot->tasks->len : 0;
	for (guint pass = 0; remaining > 0 && pass <= remaining; pass++) {
		gboolean progress = FALSE;

		for (guint index = 0; index < snapshot->tasks->len; index++) {
			SakuraSessionTaskRecord *record =
				g_ptr_array_index(snapshot->tasks, index);
			SakuraCoreTask *parent = NULL;
			SakuraCoreGroup *group = NULL;
			SakuraCoreTask *task;

			if (record == NULL || record->id == NULL ||
			    sakura_core_workspace_find_task(workspace, record->id) != NULL)
				continue;
			if (record->parent_id != NULL && record->parent_id[0] != '\0' &&
			    g_strcmp0(record->parent_id, "root") != 0) {
				parent = sakura_core_workspace_find_task(workspace,
				                                      record->parent_id);
				if (parent != NULL)
					group = parent->group;
				else
					group = sakura_core_workspace_find_group(
						workspace, record->parent_id);
				if (group == NULL)
					continue;
			} else {
				group = sakura_core_workspace_find_group(workspace,
				                                      record->group_id);
			}
			if (group == NULL)
				continue;
			task = sakura_core_task_new(record->id, record->title, group, parent);
			g_free(task->provider);
			task->provider = g_strdup(record->provider != NULL
			                           ? record->provider : "local");
			task->external_id = g_strdup(record->external_id);
			task->url = g_strdup(record->url);
			task->status = record->status;
			task->order = record->order;
			task->archived = record->archived;
			if (sakura_core_workspace_add_task(workspace, task)) {
				remaining--;
				progress = TRUE;
			} else {
				sakura_core_task_free(task);
			}
		}
		if (!progress)
			break;
	}
	if (remaining > 0 && snapshot->tasks != NULL) {
		for (guint index = 0; index < snapshot->tasks->len; index++) {
			SakuraSessionTaskRecord *record =
				g_ptr_array_index(snapshot->tasks, index);
			SakuraCoreGroup *group;
			SakuraCoreTask *task;

			if (record == NULL || record->id == NULL ||
			    sakura_core_workspace_find_task(workspace, record->id) != NULL)
				continue;
			group = sakura_core_workspace_find_group(workspace, record->group_id);
			if (group == NULL)
				group = workspace->root_group;
			task = sakura_core_task_new(record->id, record->title, group, NULL);
			g_free(task->provider);
			task->provider = g_strdup(record->provider != NULL
			                           ? record->provider : "local");
			task->external_id = g_strdup(record->external_id);
			task->url = g_strdup(record->url);
			task->status = record->status;
			task->order = record->order;
			task->archived = record->archived;
			if (sakura_core_workspace_add_task(workspace, task))
				remaining--;
			else
				sakura_core_task_free(task);
		}
	}
	if (snapshot->pages != NULL) {
		for (guint index = 0; index < snapshot->pages->len; index++)
			sakura_core_workspace_add_snapshot_page(
				workspace, g_ptr_array_index(snapshot->pages, index));
	}
	workspace->active_group = sakura_core_workspace_find_group(
		workspace, snapshot->active_group_id);
	if (workspace->active_group == NULL)
		workspace->active_group = workspace->root_group;
	workspace->active_task = sakura_core_workspace_find_task(
		workspace, snapshot->selected_task_id);
	return workspace;
}


gboolean
sakura_core_workspace_sync_snapshot(const SakuraCoreWorkspace *workspace,
	                                  SakuraSessionSnapshot *snapshot)
{
	GPtrArray *groups;
	GPtrArray *tasks;

	if (workspace == NULL || snapshot == NULL || snapshot->groups == NULL ||
	    snapshot->tasks == NULL || snapshot->pages == NULL)
		return FALSE;
	g_ptr_array_set_size(snapshot->groups, 0);
	g_ptr_array_set_size(snapshot->tasks, 0);
	g_ptr_array_set_size(snapshot->pages, 0);
	groups = sakura_core_workspace_ordered_groups(workspace);
	for (guint index = 0; index < groups->len; index++) {
		SakuraCoreGroup *model_group = g_ptr_array_index(groups, index);
		SakuraSessionGroupRecord *group = g_new0(SakuraSessionGroupRecord, 1);

		group->id = g_strdup(model_group->id);
		group->parent_id = g_strdup(model_group->parent != NULL
		                            ? model_group->parent->id : "root");
		group->title = g_strdup(model_group->title);
		group->directory = g_strdup(model_group->directory);
		group->order = model_group->order;
		group->archived = model_group->archived;
		g_ptr_array_add(snapshot->groups, group);
	}
	g_ptr_array_unref(groups);

	tasks = sakura_core_workspace_ordered_tasks(workspace);
	for (guint index = 0; index < tasks->len; index++) {
		SakuraCoreTask *model_task = g_ptr_array_index(tasks, index);
		SakuraSessionTaskRecord *task = g_new0(SakuraSessionTaskRecord, 1);
		SakuraCoreGroup *group = model_task->group != NULL
		                       ? model_task->group : workspace->root_group;

		task->id = g_strdup(model_task->id);
		task->parent_id = g_strdup(model_task->parent != NULL
		                           ? model_task->parent->id
		                           : group != NULL ? group->id : "root");
		task->group_id = g_strdup(group != NULL ? group->id : "root");
		task->title = g_strdup(model_task->title);
		task->provider = g_strdup(model_task->provider);
		task->external_id = g_strdup(model_task->external_id);
		task->url = g_strdup(model_task->url);
		task->status = model_task->status;
		task->order = model_task->order;
		task->archived = model_task->archived;
		g_ptr_array_add(snapshot->tasks, task);
	}
	g_ptr_array_unref(tasks);

	for (guint index = 0; workspace->pages != NULL &&
	                     index < workspace->pages->len; index++) {
		SakuraCorePage *model_page = g_ptr_array_index(workspace->pages, index);
		SakuraSessionPageRecord *page;
		SakuraCoreGroup *group;

		if (model_page == NULL || model_page->id == NULL)
			continue;
		page = g_new0(SakuraSessionPageRecord, 1);
		group = model_page->group != NULL ? model_page->group
		                               : workspace->root_group;
		page->id = g_strdup(model_page->id);
		page->group_id = g_strdup(group != NULL ? group->id : "root");
		page->parent_id = g_strdup(model_page->task != NULL
		                           ? model_page->task->id
		                           : page->group_id);
		page->task_id = g_strdup(model_page->task != NULL
		                         ? model_page->task->id : NULL);
		page->title = g_strdup(model_page->title);
		page->title_set_by_user = model_page->title_set_by_user;
		page->archived = model_page->archived;
		page->root_layout_id = g_strdup(model_page->root_layout_id);
		page->active_terminal_id = g_strdup(model_page->active_terminal_id);
		g_ptr_array_add(snapshot->pages, page);
	}
	g_free(snapshot->active_group_id);
	snapshot->active_group_id = g_strdup(
		workspace->active_group != NULL ? workspace->active_group->id : "root");
	g_free(snapshot->root_directory);
	snapshot->root_directory = g_strdup(workspace->root_group != NULL
	                                  ? workspace->root_group->directory : NULL);
	return TRUE;
}
