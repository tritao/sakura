#include <glib.h>

#include "sakura-core.h"

static void
test_snapshot_defaults_and_ownership(void)
{
	SakuraSessionSnapshot *snapshot = sakura_session_snapshot_new();
	SakuraSessionGroupRecord *group = g_new0(SakuraSessionGroupRecord, 1);

	g_assert_nonnull(snapshot);
	g_assert_nonnull(snapshot->groups);
	g_assert_nonnull(snapshot->tasks);
	g_assert_nonnull(snapshot->tabs);
	g_assert_nonnull(snapshot->pages);
	g_assert_nonnull(snapshot->layouts);
	g_assert_cmpint(snapshot->selected_terminal, ==, -1);
	g_assert_cmpstr(snapshot->active_group_id, ==, "root");
	g_assert_true(snapshot->sidebar_visible);
	g_assert_cmpint(snapshot->sidebar_width, ==, 200);
	g_assert_false(snapshot->show_archived);

	group->id = g_strdup("group-a");
	group->title = g_strdup("A");
	g_ptr_array_add(snapshot->groups, group);
	g_assert_cmpuint(snapshot->groups->len, ==, 1);

	sakura_session_snapshot_free(snapshot);
}


static void
test_workspace_hierarchy_and_ordering(void)
{
	SakuraCoreWorkspace *workspace = sakura_core_workspace_new();
	SakuraCoreGroup *root = sakura_core_group_new("root", "All", NULL);
	SakuraCoreGroup *group_a = sakura_core_group_new("group-a", "A", root);
	SakuraCoreGroup *group_b = sakura_core_group_new("group-b", "B", root);
	SakuraCoreGroup *group_child = sakura_core_group_new("group-child", "C", group_a);
	SakuraCoreTask *task_a = sakura_core_task_new("task-a", "A", group_a, NULL);
	SakuraCoreTask *task_b = sakura_core_task_new("task-b", "B", group_a, NULL);
	SakuraCoreTask *task_child = sakura_core_task_new("task-child", "C", group_a, task_a);
	GPtrArray *groups;
	GPtrArray *tasks;

	g_assert_true(sakura_core_workspace_set_root(workspace, root));
	group_a->order = 1;
	group_b->order = 0;
	group_child->order = 0;
	g_assert_true(sakura_core_workspace_add_group(workspace, group_a));
	g_assert_true(sakura_core_workspace_add_group(workspace, group_b));
	g_assert_true(sakura_core_workspace_add_group(workspace, group_child));

	task_a->order = 2;
	task_b->order = 1;
	task_child->order = 0;
	g_assert_true(sakura_core_workspace_add_task(workspace, task_a));
	g_assert_true(sakura_core_workspace_add_task(workspace, task_b));
	g_assert_true(sakura_core_workspace_add_task(workspace, task_child));

	groups = sakura_core_workspace_ordered_groups(workspace);
	g_assert_cmpuint(groups->len, ==, 3);
	g_assert_true(g_ptr_array_index(groups, 0) == group_b);
	g_assert_true(g_ptr_array_index(groups, 1) == group_a);
	g_assert_true(g_ptr_array_index(groups, 2) == group_child);
	g_ptr_array_unref(groups);

	tasks = sakura_core_workspace_ordered_tasks(workspace);
	g_assert_cmpuint(tasks->len, ==, 3);
	g_assert_true(g_ptr_array_index(tasks, 0) == task_b);
	g_assert_true(g_ptr_array_index(tasks, 1) == task_a);
	g_assert_true(g_ptr_array_index(tasks, 2) == task_child);
	g_ptr_array_unref(tasks);

	sakura_core_workspace_set_group_archived(workspace, group_a, TRUE);
	g_assert_true(sakura_core_workspace_group_is_archived(workspace, group_child));
	g_assert_true(sakura_core_workspace_task_is_archived(workspace, task_child));
	g_assert_false(sakura_core_workspace_group_is_archived(workspace, group_b));
	g_assert_false(sakura_core_workspace_can_remove_group(workspace, group_a));

	sakura_core_workspace_set_group_archived(workspace, group_a, FALSE);
	sakura_core_workspace_set_task_archived(workspace, task_a, TRUE);
	g_assert_true(sakura_core_workspace_task_is_archived(workspace, task_child));
	g_assert_false(sakura_core_workspace_task_is_archived(workspace, task_b));

	sakura_core_workspace_free(workspace);
}


static void
test_workspace_restore_snapshot(void)
{
	SakuraSessionSnapshot *snapshot = sakura_session_snapshot_new();
	SakuraSessionGroupRecord *group = g_new0(SakuraSessionGroupRecord, 1);
	SakuraSessionTaskRecord *task = g_new0(SakuraSessionTaskRecord, 1);
	SakuraSessionTaskRecord *child = g_new0(SakuraSessionTaskRecord, 1);
	SakuraSessionPageRecord *page = g_new0(SakuraSessionPageRecord, 1);
	SakuraSessionTabRecord *tab = g_new0(SakuraSessionTabRecord, 1);
	SakuraCoreWorkspace *workspace;
	SakuraCorePage *restored_page;
	SakuraCoreTerminal *restored_terminal;
	SakuraSessionSnapshot *roundtrip;
	GError *error = NULL;

	snapshot->root_directory = g_strdup("/tmp/project");
	g_free(snapshot->active_group_id);
	snapshot->active_group_id = g_strdup("group-a");
	group->id = g_strdup("group-a");
	group->parent_id = g_strdup("root");
	group->title = g_strdup("A");
	g_ptr_array_add(snapshot->groups, group);
	task->id = g_strdup("task-a");
	task->parent_id = g_strdup("group-a");
	task->group_id = g_strdup("group-a");
	task->title = g_strdup("A");
	child->id = g_strdup("task-child");
	child->parent_id = g_strdup("task-a");
	child->group_id = g_strdup("group-a");
	child->title = g_strdup("Child");
	/* The restore path must not depend on serialized record order. */
	g_ptr_array_add(snapshot->tasks, child);
	g_ptr_array_add(snapshot->tasks, task);
	page->id = g_strdup("page-a");
	page->parent_id = g_strdup("task-a");
	page->group_id = g_strdup("group-a");
	page->task_id = g_strdup("task-a");
	page->title = g_strdup("Build terminal");
	page->active_terminal_id = g_strdup("terminal-a");
	g_ptr_array_add(snapshot->pages, page);
	tab->terminal_id = g_strdup("terminal-a");
	tab->page_id = g_strdup("page-a");
	tab->parent_id = g_strdup("task-a");
	tab->cwd = g_strdup("/tmp/project");
	tab->kind = SAKURA_TAB_CODEX;
	tab->codex_session_id = g_strdup("session-a");
	tab->codex_session_name = g_strdup("Agent session");
	tab->codex_model = g_strdup("gpt-5.6-luna");
	tab->codex_reasoning_effort = g_strdup("xhigh");
	tab->codex_tracking_token = g_strdup("tracking-a");
	tab->runtime_status = SAKURA_TERMINAL_RUNNING;
	tab->resume_on_start = TRUE;
	tab->order = 4;
	g_ptr_array_add(snapshot->tabs, tab);

	workspace = sakura_core_workspace_from_snapshot(snapshot, &error);
	g_assert_no_error(error);
	g_assert_nonnull(workspace);
	g_assert_cmpstr(workspace->root_group->directory, ==, "/tmp/project");
	g_assert_true(workspace->active_group ==
	             sakura_core_workspace_find_group(workspace, "group-a"));
	g_assert_true(sakura_core_workspace_find_task(workspace, "task-child")->parent ==
	             sakura_core_workspace_find_task(workspace, "task-a"));
	restored_page = sakura_core_workspace_find_page(workspace, "page-a");
	g_assert_nonnull(restored_page);
	g_assert_true(restored_page->group ==
	              sakura_core_workspace_find_group(workspace, "group-a"));
	g_assert_true(restored_page->task ==
	              sakura_core_workspace_find_task(workspace, "task-a"));
	g_assert_cmpstr(restored_page->active_terminal_id, ==, "terminal-a");
	restored_terminal = sakura_core_workspace_find_terminal(workspace,
	                                                       "terminal-a");
	g_assert_nonnull(restored_terminal);
	g_assert_cmpint(restored_terminal->status, ==, SAKURA_TERMINAL_EXITED);
	g_assert_true(restored_terminal->resume_on_start);
	g_assert_cmpstr(restored_terminal->codex_session_id, ==, "session-a");
	g_assert_cmpstr(restored_terminal->codex_session_name, ==, "Agent session");
	g_assert_cmpstr(restored_terminal->codex_model, ==, "gpt-5.6-luna");
	g_assert_cmpstr(restored_terminal->codex_reasoning_effort, ==, "xhigh");
	g_assert_cmpstr(restored_terminal->tracking_token, ==, "tracking-a");
	roundtrip = sakura_session_snapshot_new();
	g_assert_true(sakura_core_workspace_sync_snapshot(workspace, roundtrip));
	g_assert_cmpuint(roundtrip->tabs->len, ==, 1);
	tab = g_ptr_array_index(roundtrip->tabs, 0);
	g_assert_cmpstr(tab->terminal_id, ==, "terminal-a");
	g_assert_cmpstr(tab->page_id, ==, "page-a");
	g_assert_cmpstr(tab->parent_id, ==, "task-a");
	g_assert_cmpstr(tab->codex_session_name, ==, "Agent session");
	g_assert_cmpuint(tab->runtime_status, ==, SAKURA_TERMINAL_EXITED);
	g_assert_true(tab->resume_on_start);
	sakura_session_snapshot_free(roundtrip);
	g_assert_false(sakura_core_workspace_can_remove_task(
		workspace, sakura_core_workspace_find_task(workspace, "task-a")));
	g_assert_false(sakura_core_workspace_can_remove_group(
		workspace, sakura_core_workspace_find_group(workspace, "group-a")));
	g_assert_true(sakura_core_workspace_remove_page(workspace, restored_page));
	g_assert_null(sakura_core_workspace_find_page(workspace, "page-a"));

	sakura_core_workspace_free(workspace);
	sakura_session_snapshot_free(snapshot);
}


int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/core/snapshot/defaults-and-ownership",
	                test_snapshot_defaults_and_ownership);
	g_test_add_func("/core/workspace/hierarchy-and-ordering",
	                test_workspace_hierarchy_and_ordering);
	g_test_add_func("/core/workspace/restore-snapshot",
	                test_workspace_restore_snapshot);
	return g_test_run();
}
