#include <glib.h>

#include "src/sakura-private.h"

static void
test_tool_ids_round_trip(void)
{
	static const SakuraToolKind tools[] = {
		SAKURA_TOOL_GITUI,
		SAKURA_TOOL_GH_DASH,
		SAKURA_TOOL_GH_PR,
		SAKURA_TOOL_GIT_COLA
	};
	guint index;

	for (index = 0; index < G_N_ELEMENTS(tools); index++) {
		const gchar *id = sakura_tool_id(tools[index]);

		g_assert_nonnull(id);
		g_assert_cmpint(sakura_tool_from_id(id), ==, tools[index]);
		g_assert_nonnull(sakura_tool_label(tools[index]));
		g_assert_nonnull(sakura_tool_executable(tools[index]));
		g_assert_nonnull(sakura_tool_icon_name(tools[index]));
	}
}


static void
test_tool_ids_reject_unknown_values(void)
{
	g_assert_cmpint(sakura_tool_from_id(NULL), ==, SAKURA_TOOL_NONE);
	g_assert_cmpint(sakura_tool_from_id("unknown-tool"), ==, SAKURA_TOOL_NONE);
	g_assert_null(sakura_tool_id(SAKURA_TOOL_NONE));
	g_assert_null(sakura_tool_executable(SAKURA_TOOL_NONE));
}


static void
test_tool_scope(void)
{
	g_assert_true(sakura_tool_requires_git_repository(SAKURA_TOOL_GITUI));
	g_assert_true(sakura_tool_requires_git_repository(SAKURA_TOOL_GIT_COLA));
	g_assert_false(sakura_tool_requires_git_repository(SAKURA_TOOL_GH_DASH));
	g_assert_false(sakura_tool_requires_git_repository(SAKURA_TOOL_GH_PR));
}


static void
test_codex_reasoning_efforts(void)
{
	static const gchar *valid[] = { "minimal", "low", "medium", "high", "xhigh" };
	guint index;

	for (index = 0; index < G_N_ELEMENTS(valid); index++) {
		g_assert_true(sakura_codex_reasoning_effort_is_valid(valid[index]));
		g_assert_nonnull(sakura_codex_reasoning_effort_label(valid[index]));
	}
	g_assert_false(sakura_codex_reasoning_effort_is_valid(NULL));
	g_assert_false(sakura_codex_reasoning_effort_is_valid("turbo"));
	g_assert_nonnull(sakura_codex_reasoning_effort_label(NULL));
}


static void
test_session_snapshot_round_trip(void)
{
	SakuraSessionSnapshot *source = sakura_session_snapshot_new();
	SakuraSessionSnapshot *loaded = sakura_session_snapshot_new();
	SakuraSessionGroupRecord *group = g_new0(SakuraSessionGroupRecord, 1);
	SakuraSessionTaskRecord *task_record = g_new0(SakuraSessionTaskRecord, 1);
	SakuraSessionTabRecord *tab = g_new0(SakuraSessionTabRecord, 1);
	SakuraSessionSidebarExpansionRecord *expanded_group = g_new0(
		SakuraSessionSidebarExpansionRecord, 1);
	SakuraSessionSidebarExpansionRecord *expanded_session = g_new0(
		SakuraSessionSidebarExpansionRecord, 1);
	GKeyFile *key_file = g_key_file_new();
	GError *error = NULL;

	group->id = g_strdup("group-a");
	group->parent_id = g_strdup("root");
	group->title = g_strdup("Alpha");
	group->directory = g_strdup("/tmp/sakura-group");
	group->archived = TRUE;
	g_ptr_array_add(source->groups, group);
	task_record->id = g_strdup("task-a");
	task_record->parent_id = g_strdup("group-a");
	task_record->group_id = g_strdup("group-a");
	task_record->title = g_strdup("Implement task tree");
	task_record->provider = g_strdup("local");
	task_record->status = SAKURA_TASK_WORKING;
	task_record->archived = TRUE;
	g_ptr_array_add(source->tasks, task_record);

	tab->parent_id = g_strdup("task-a");
	tab->cwd = g_strdup("/tmp");
	tab->title = g_strdup("Working");
	tab->terminal_id = g_strdup("terminal-1");
	tab->tool_id = g_strdup("gitui");
	tab->codex_session_id = g_strdup("session-1");
	tab->codex_session_name = g_strdup("Planning");
	tab->codex_reasoning_effort = g_strdup("high");
	tab->colorset = 4;
	tab->kind = SAKURA_TAB_TOOL;
	tab->title_set_by_user = TRUE;
	tab->status = SAKURA_TAB_STATUS_READY;
	tab->attention = TRUE;
	tab->attention_timestamp = 123456789;
	g_ptr_array_add(source->tabs, tab);
	source->selected_terminal = 0;
	source->selected_terminal_id = g_strdup("terminal-1");
	source->selected_task_id = g_strdup("task-a");
	g_free(source->active_group_id);
	source->active_group_id = g_strdup("group-a");
	source->root_directory = g_strdup("/tmp");
	source->sidebar_visible = FALSE;
	source->sidebar_width = 280;
	source->show_archived = TRUE;
	source->sidebar_expansion_saved = TRUE;
	expanded_group->id = g_strdup("group-a");
	expanded_group->kind = SAKURA_SIDEBAR_EXPANSION_GROUP;
	g_ptr_array_add(source->expanded_sidebar_nodes, expanded_group);
	expanded_session->id = g_strdup("page-a");
	expanded_session->kind = SAKURA_SIDEBAR_EXPANSION_SESSION;
	g_ptr_array_add(source->expanded_sidebar_nodes, expanded_session);

	sakura_session_snapshot_save(source, key_file);
	g_assert_true(sakura_session_snapshot_load(key_file, loaded, &error));
	g_assert_no_error(error);
	g_assert_cmpuint(loaded->groups->len, ==, 1);
	g_assert_cmpuint(loaded->tasks->len, ==, 1);
	g_assert_cmpuint(loaded->tabs->len, ==, 1);
	g_assert_cmpint(loaded->selected_terminal, ==, 0);
	g_assert_cmpstr(loaded->selected_terminal_id, ==, "terminal-1");
	g_assert_cmpstr(loaded->selected_task_id, ==, "task-a");
	g_assert_cmpstr(loaded->active_group_id, ==, "group-a");
	g_assert_cmpstr(loaded->workspace_id, ==, source->workspace_id);
	g_assert_cmpstr(loaded->root_directory, ==, "/tmp");
	g_assert_false(loaded->sidebar_visible);
	g_assert_cmpint(loaded->sidebar_width, ==, 280);
	g_assert_true(loaded->show_archived);
	g_assert_true(loaded->sidebar_expansion_saved);
	g_assert_cmpuint(loaded->expanded_sidebar_nodes->len, ==, 2);

	{
		SakuraSessionGroupRecord *loaded_group = g_ptr_array_index(loaded->groups, 0);
		SakuraSessionTaskRecord *loaded_task = g_ptr_array_index(loaded->tasks, 0);
		SakuraSessionTabRecord *loaded_tab = g_ptr_array_index(loaded->tabs, 0);
		g_assert_cmpstr(loaded_group->title, ==, "Alpha");
		g_assert_cmpstr(loaded_group->directory, ==, "/tmp/sakura-group");
		g_assert_true(loaded_group->archived);
		g_assert_cmpstr(loaded_task->parent_id, ==, "group-a");
		g_assert_cmpstr(loaded_task->title, ==, "Implement task tree");
		g_assert_cmpint(loaded_task->status, ==, SAKURA_TASK_WORKING);
		g_assert_true(loaded_task->archived);
		g_assert_cmpstr(loaded_tab->cwd, ==, "/tmp");
		g_assert_cmpstr(loaded_tab->tool_id, ==, "gitui");
		g_assert_cmpstr(loaded_tab->codex_session_id, ==, "session-1");
		g_assert_cmpstr(loaded_tab->codex_session_name, ==, "Planning");
		g_assert_cmpstr(loaded_tab->codex_reasoning_effort, ==, "high");
		g_assert_cmpint(loaded_tab->colorset, ==, 4);
		g_assert_true(loaded_tab->title_set_by_user);
		g_assert_cmpint(loaded_tab->status, ==, SAKURA_TAB_STATUS_READY);
		g_assert_true(loaded_tab->attention);
		g_assert_cmpint(loaded_tab->attention_timestamp, ==, 123456789);
		{
			SakuraSessionSidebarExpansionRecord *loaded_group_expansion =
				g_ptr_array_index(loaded->expanded_sidebar_nodes, 0);
			SakuraSessionSidebarExpansionRecord *loaded_session_expansion =
				g_ptr_array_index(loaded->expanded_sidebar_nodes, 1);
			g_assert_cmpstr(loaded_group_expansion->id, ==, "group-a");
			g_assert_cmpint(loaded_group_expansion->kind, ==,
			                SAKURA_SIDEBAR_EXPANSION_GROUP);
			g_assert_cmpstr(loaded_session_expansion->id, ==, "page-a");
			g_assert_cmpint(loaded_session_expansion->kind, ==,
			                SAKURA_SIDEBAR_EXPANSION_SESSION);
		}
	}

	g_key_file_free(key_file);
	sakura_session_snapshot_free(source);
	sakura_session_snapshot_free(loaded);
}


static void
test_session_snapshot_rejects_group_cycle(void)
{
	SakuraSessionSnapshot *snapshot = sakura_session_snapshot_new();
	SakuraSessionGroupRecord *first = g_new0(SakuraSessionGroupRecord, 1);
	SakuraSessionGroupRecord *second = g_new0(SakuraSessionGroupRecord, 1);
	GKeyFile *key_file = g_key_file_new();
	GError *error = NULL;

	first->id = g_strdup("group-a");
	first->parent_id = g_strdup("group-b");
	first->title = g_strdup("A");
	second->id = g_strdup("group-b");
	second->parent_id = g_strdup("group-a");
	second->title = g_strdup("B");
	g_ptr_array_add(snapshot->groups, first);
	g_ptr_array_add(snapshot->groups, second);

	sakura_session_snapshot_save(snapshot, key_file);
	g_assert_false(sakura_session_snapshot_load(key_file, snapshot, &error));
	g_assert_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE);
	g_clear_error(&error);

	g_key_file_free(key_file);
	sakura_session_snapshot_free(snapshot);
}


static void
test_session_snapshot_rejects_task_cycle(void)
{
	SakuraSessionSnapshot *snapshot = sakura_session_snapshot_new();
	SakuraSessionTaskRecord *first = g_new0(SakuraSessionTaskRecord, 1);
	SakuraSessionTaskRecord *second = g_new0(SakuraSessionTaskRecord, 1);
	GKeyFile *key_file = g_key_file_new();
	GError *error = NULL;

	first->id = g_strdup("task-a");
	first->parent_id = g_strdup("task-b");
	first->group_id = g_strdup("root");
	second->id = g_strdup("task-b");
	second->parent_id = g_strdup("task-a");
	second->group_id = g_strdup("root");
	g_ptr_array_add(snapshot->tasks, first);
	g_ptr_array_add(snapshot->tasks, second);

	sakura_session_snapshot_save(snapshot, key_file);
	g_assert_false(sakura_session_snapshot_load(key_file, snapshot, &error));
	g_assert_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE);
	g_clear_error(&error);

	g_key_file_free(key_file);
	sakura_session_snapshot_free(snapshot);
}


static void
test_session_snapshot_rejects_inconsistent_task_page_selection(void)
{
	SakuraSessionSnapshot *source = sakura_session_snapshot_new();
	SakuraSessionSnapshot *loaded = sakura_session_snapshot_new();
	SakuraSessionGroupRecord *group = g_new0(SakuraSessionGroupRecord, 1);
	SakuraSessionTaskRecord *task = g_new0(SakuraSessionTaskRecord, 1);
	SakuraSessionPageRecord *page = g_new0(SakuraSessionPageRecord, 1);
	SakuraSessionLayoutRecord *layout = g_new0(SakuraSessionLayoutRecord, 1);
	SakuraSessionTabRecord *tab = g_new0(SakuraSessionTabRecord, 1);
	GKeyFile *key_file = g_key_file_new();
	GError *error = NULL;

	group->id = g_strdup("group-a");
	group->parent_id = g_strdup("root");
	group->title = g_strdup("Alpha");
	g_ptr_array_add(source->groups, group);
	task->id = g_strdup("task-a");
	task->parent_id = g_strdup("group-a");
	task->group_id = g_strdup("group-a");
	task->title = g_strdup("Task");
	task->provider = g_strdup("local");
	g_ptr_array_add(source->tasks, task);
	page->id = g_strdup("page-a");
	page->parent_id = g_strdup("task-a");
	page->task_id = g_strdup("task-a");
	page->root_layout_id = g_strdup("layout-a");
	g_ptr_array_add(source->pages, page);
	layout->id = g_strdup("layout-a");
	layout->page_id = g_strdup("page-a");
	layout->type = g_strdup("leaf");
	layout->terminal_id = g_strdup("terminal-a");
	g_ptr_array_add(source->layouts, layout);
	tab->parent_id = g_strdup("task-a");
	tab->terminal_id = g_strdup("terminal-a");
	g_ptr_array_add(source->tabs, tab);
	source->selected_task_id = g_strdup("task-a");
	g_free(source->active_group_id);
	source->active_group_id = g_strdup("group-a");

	sakura_session_snapshot_save(source, key_file);
	/* A task-owned page must name that task as its serialized parent. */
	g_key_file_set_string(key_file, "Page0", "parent", "group-a");
	g_assert_false(sakura_session_snapshot_load(key_file, loaded, &error));
	g_assert_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE);
	g_clear_error(&error);

	/* A page directly under a task cannot omit its task binding. */
	g_key_file_set_string(key_file, "Page0", "parent", "task-a");
	g_key_file_remove_key(key_file, "Page0", "task_id", NULL);
	g_assert_false(sakura_session_snapshot_load(key_file, loaded, &error));
	g_assert_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE);
	g_clear_error(&error);

	/* Persisted task selection must refer to a task in the same snapshot. */
	g_key_file_set_string(key_file, "Page0", "task_id", "task-a");
	g_key_file_set_string(key_file, "Session", "selected_task_id", "task-missing");
	g_assert_false(sakura_session_snapshot_load(key_file, loaded, &error));
	g_assert_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE);
	g_clear_error(&error);

	g_key_file_free(key_file);
	sakura_session_snapshot_free(source);
	sakura_session_snapshot_free(loaded);
}


static void
test_session_layout_round_trip(void)
{
	SakuraSessionSnapshot *source = sakura_session_snapshot_new();
	SakuraSessionSnapshot *loaded = sakura_session_snapshot_new();
	SakuraSessionPageRecord *page = g_new0(SakuraSessionPageRecord, 1);
	SakuraSessionLayoutRecord *split = g_new0(SakuraSessionLayoutRecord, 1);
	SakuraSessionLayoutRecord *first = g_new0(SakuraSessionLayoutRecord, 1);
	SakuraSessionLayoutRecord *second = g_new0(SakuraSessionLayoutRecord, 1);
	SakuraSessionTabRecord *a = g_new0(SakuraSessionTabRecord, 1);
	SakuraSessionTabRecord *b = g_new0(SakuraSessionTabRecord, 1);
	GKeyFile *key_file = g_key_file_new();
	GError *error = NULL;

	page->id = g_strdup("page-1");
	page->parent_id = g_strdup("root");
	page->root_layout_id = g_strdup("layout-root");
	page->active_terminal_id = g_strdup("terminal-b");
	page->archived = TRUE;
	g_ptr_array_add(source->pages, page);
	split->id = g_strdup("layout-root");
	split->page_id = g_strdup("page-1");
	split->type = g_strdup("split");
	split->direction = SAKURA_SPLIT_RIGHT;
	split->ratio = 0.63;
	split->first_id = g_strdup("layout-a");
	split->second_id = g_strdup("layout-b");
	g_ptr_array_add(source->layouts, split);
	first->id = g_strdup("layout-a");
	first->page_id = g_strdup("page-1");
	first->type = g_strdup("leaf");
	first->terminal_id = g_strdup("terminal-a");
	g_ptr_array_add(source->layouts, first);
	second->id = g_strdup("layout-b");
	second->page_id = g_strdup("page-1");
	second->type = g_strdup("leaf");
	second->terminal_id = g_strdup("terminal-b");
	g_ptr_array_add(source->layouts, second);
	a->terminal_id = g_strdup("terminal-a");
	a->parent_id = g_strdup("root");
	b->terminal_id = g_strdup("terminal-b");
	b->parent_id = g_strdup("root");
	g_ptr_array_add(source->tabs, a);
	g_ptr_array_add(source->tabs, b);
	source->selected_page_id = g_strdup("page-1");
	source->selected_terminal_id = g_strdup("terminal-b");

	sakura_session_snapshot_save(source, key_file);
	g_assert_true(sakura_session_snapshot_load(key_file, loaded, &error));
	g_assert_no_error(error);
	g_assert_cmpuint(loaded->pages->len, ==, 1);
	g_assert_cmpuint(loaded->layouts->len, ==, 3);
	g_assert_cmpstr(loaded->selected_page_id, ==, "page-1");
	g_assert_true(((SakuraSessionPageRecord *)g_ptr_array_index(loaded->pages, 0))->archived);
	g_assert_cmpstr(((SakuraSessionPageRecord *)g_ptr_array_index(loaded->pages, 0))->active_terminal_id,
	               ==, "terminal-b");
	g_assert_cmpfloat(((SakuraSessionLayoutRecord *)g_ptr_array_index(loaded->layouts, 0))->ratio,
	                  ==, 0.63);
	g_assert_cmpstr(((SakuraSessionLayoutRecord *)g_ptr_array_index(loaded->layouts, 0))->second_id,
	               ==, "layout-b");

	g_key_file_free(key_file);
	sakura_session_snapshot_free(source);
	sakura_session_snapshot_free(loaded);
}


static void
test_session_rejects_disconnected_layout(void)
{
	SakuraSessionSnapshot *source = sakura_session_snapshot_new();
	SakuraSessionSnapshot *loaded = sakura_session_snapshot_new();
	SakuraSessionPageRecord *page = g_new0(SakuraSessionPageRecord, 1);
	SakuraSessionLayoutRecord *root = g_new0(SakuraSessionLayoutRecord, 1);
	SakuraSessionLayoutRecord *orphan = g_new0(SakuraSessionLayoutRecord, 1);
	SakuraSessionTabRecord *first_tab = g_new0(SakuraSessionTabRecord, 1);
	SakuraSessionTabRecord *orphan_tab = g_new0(SakuraSessionTabRecord, 1);
	GKeyFile *key_file = g_key_file_new();
	GError *error = NULL;

	page->id = g_strdup("page-1");
	page->parent_id = g_strdup("root");
	page->root_layout_id = g_strdup("layout-root");
	g_ptr_array_add(source->pages, page);
	root->id = g_strdup("layout-root");
	root->page_id = g_strdup("page-1");
	root->type = g_strdup("leaf");
	root->terminal_id = g_strdup("terminal-first");
	g_ptr_array_add(source->layouts, root);
	orphan->id = g_strdup("layout-orphan");
	orphan->page_id = g_strdup("page-1");
	orphan->type = g_strdup("leaf");
	orphan->terminal_id = g_strdup("terminal-orphan");
	g_ptr_array_add(source->layouts, orphan);
	first_tab->parent_id = g_strdup("root");
	first_tab->terminal_id = g_strdup("terminal-first");
	g_ptr_array_add(source->tabs, first_tab);
	orphan_tab->parent_id = g_strdup("root");
	orphan_tab->terminal_id = g_strdup("terminal-orphan");
	g_ptr_array_add(source->tabs, orphan_tab);

	sakura_session_snapshot_save(source, key_file);
	g_assert_false(sakura_session_snapshot_load(key_file, loaded, &error));
	g_assert_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE);
	g_clear_error(&error);

	g_key_file_free(key_file);
	sakura_session_snapshot_free(source);
	sakura_session_snapshot_free(loaded);
}


static void
test_session_repairs_duplicate_page_ids(void)
{
	SakuraSessionSnapshot *source = sakura_session_snapshot_new();
	SakuraSessionSnapshot *loaded = sakura_session_snapshot_new();
	SakuraSessionPageRecord *first_page = g_new0(SakuraSessionPageRecord, 1);
	SakuraSessionPageRecord *second_page = g_new0(SakuraSessionPageRecord, 1);
	SakuraSessionLayoutRecord *first_layout = g_new0(SakuraSessionLayoutRecord, 1);
	SakuraSessionLayoutRecord *second_layout = g_new0(SakuraSessionLayoutRecord, 1);
	SakuraSessionTabRecord *first_tab = g_new0(SakuraSessionTabRecord, 1);
	SakuraSessionTabRecord *second_tab = g_new0(SakuraSessionTabRecord, 1);
	GKeyFile *key_file = g_key_file_new();
	GError *error = NULL;

	first_page->id = g_strdup("page-1");
	first_page->parent_id = g_strdup("root");
	first_page->title = g_strdup("First");
	first_page->root_layout_id = g_strdup("layout-1");
	first_page->active_terminal_id = g_strdup("terminal-1");
	second_page->id = g_strdup("page-2");
	second_page->parent_id = g_strdup("root");
	second_page->title = g_strdup("Second");
	second_page->root_layout_id = g_strdup("layout-2");
	second_page->active_terminal_id = g_strdup("terminal-2");
	g_ptr_array_add(source->pages, first_page);
	g_ptr_array_add(source->pages, second_page);

	first_layout->id = g_strdup("layout-1");
	first_layout->page_id = g_strdup("page-1");
	first_layout->type = g_strdup("leaf");
	first_layout->ratio = 0.5;
	first_layout->terminal_id = g_strdup("terminal-1");
	second_layout->id = g_strdup("layout-2");
	second_layout->page_id = g_strdup("page-2");
	second_layout->type = g_strdup("leaf");
	second_layout->ratio = 0.5;
	second_layout->terminal_id = g_strdup("terminal-2");
	g_ptr_array_add(source->layouts, first_layout);
	g_ptr_array_add(source->layouts, second_layout);

	first_tab->parent_id = g_strdup("root");
	first_tab->cwd = g_strdup("/tmp");
	first_tab->title = g_strdup("First");
	first_tab->terminal_id = g_strdup("terminal-1");
	second_tab->parent_id = g_strdup("root");
	second_tab->cwd = g_strdup("/tmp");
	second_tab->title = g_strdup("Second");
	second_tab->terminal_id = g_strdup("terminal-2");
	g_ptr_array_add(source->tabs, first_tab);
	g_ptr_array_add(source->tabs, second_tab);

	sakura_session_snapshot_save(source, key_file);
	/* Simulate the historical collision: the second page and its layout both
	 * carry the first page's identifier. */
	g_key_file_set_string(key_file, "Page1", "id", "page-1");
	g_key_file_set_string(key_file, "Layout1", "page", "page-1");

	g_assert_true(sakura_session_snapshot_load(key_file, loaded, &error));
	g_assert_no_error(error);
	g_assert_cmpstr(((SakuraSessionPageRecord *)g_ptr_array_index(loaded->pages, 0))->id,
	               ==, "page-1");
	g_assert_cmpstr(((SakuraSessionPageRecord *)g_ptr_array_index(loaded->pages, 1))->id,
	               ==, "page-recovered-1");
	g_assert_cmpstr(((SakuraSessionLayoutRecord *)g_ptr_array_index(loaded->layouts, 1))->page_id,
	               ==, "page-recovered-1");

	g_key_file_free(key_file);
	sakura_session_snapshot_free(source);
	sakura_session_snapshot_free(loaded);
}


static void
test_session_snapshot_uses_safe_optional_defaults(void)
{
	SakuraSessionSnapshot *snapshot = sakura_session_snapshot_new();
	GKeyFile *key_file = g_key_file_new();
	GError *error = NULL;

	/* Version 1 sessions did not persist the newer selection/sidebar fields. */
	g_key_file_set_integer(key_file, "Session", "version", 1);
	g_key_file_set_integer(key_file, "Session", "group_count", 0);
	g_key_file_set_integer(key_file, "Session", "terminal_count", 0);
	snapshot->selected_terminal = 42;
	g_clear_pointer(&snapshot->selected_terminal_id, g_free);
	snapshot->selected_terminal_id = g_strdup("stale-terminal");
	g_clear_pointer(&snapshot->active_group_id, g_free);
	snapshot->active_group_id = g_strdup("stale-group");
	snapshot->sidebar_visible = FALSE;
	snapshot->sidebar_width = 480;

	g_assert_true(sakura_session_snapshot_load(key_file, snapshot, &error));
	g_assert_no_error(error);
	g_assert_cmpint(snapshot->selected_terminal, ==, -1);
	g_assert_null(snapshot->selected_terminal_id);
	g_assert_cmpstr(snapshot->active_group_id, ==, "root");
	g_assert_true(snapshot->sidebar_visible);
	g_assert_cmpint(snapshot->sidebar_width, ==, 200);
	g_assert_nonnull(snapshot->workspace_id);

	g_key_file_free(key_file);
	sakura_session_snapshot_free(snapshot);
}


static void
test_session_snapshot_preserves_previous_on_failure(void)
{
	SakuraSessionSnapshot *snapshot = sakura_session_snapshot_new();
	GKeyFile *key_file = g_key_file_new();
	GError *error = NULL;

	g_clear_pointer(&snapshot->active_group_id, g_free);
	snapshot->active_group_id = g_strdup("keep-me");
	g_key_file_set_integer(key_file, "Session", "version", 3);
	g_key_file_set_integer(key_file, "Session", "group_count", 1);
	g_key_file_set_integer(key_file, "Session", "terminal_count", 0);
	/* The group has no id, so validation must fail after parsing. */
	g_key_file_set_string(key_file, "Group0", "parent", "root");

	g_assert_false(sakura_session_snapshot_load(key_file, snapshot, &error));
	g_assert_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE);
	g_assert_cmpstr(snapshot->active_group_id, ==, "keep-me");
	g_clear_error(&error);

	g_key_file_free(key_file);
	sakura_session_snapshot_free(snapshot);
}


static void
test_layout_split_and_collapse(void)
{
	SakuraPage *page = sakura_page_new("page-layout");
	SakuraTab first = { 0 };
	SakuraTab second = { 0 };
	SakuraLayoutNode *root;
	GError *error = NULL;

	root = sakura_layout_leaf_new(page, &first);
	g_assert_nonnull(root);
	g_assert_true(sakura_layout_validate(page, &error));
	g_assert_no_error(error);
	g_assert_true(sakura_layout_split_leaf(root, SAKURA_SPLIT_RIGHT, &second));
	g_assert_cmpuint(sakura_layout_tab_count(page->layout_root), ==, 2);
	g_assert_true(sakura_layout_contains_tab(page->layout_root, &first));
	g_assert_true(sakura_layout_contains_tab(page->layout_root, &second));
	g_assert_true(sakura_layout_validate(page, &error));
	g_assert_no_error(error);

	g_assert_true(sakura_layout_remove_leaf(second.layout_leaf));
	g_assert_cmpuint(sakura_layout_tab_count(page->layout_root), ==, 1);
	g_assert_true(sakura_layout_contains_tab(page->layout_root, &first));
	g_assert_null(second.page);
	g_assert_null(second.layout_leaf);
	g_assert_true(sakura_layout_validate(page, &error));
	g_assert_no_error(error);

	sakura_page_free(page);
}


static void
test_layout_rejects_invalid_tree(void)
{
	SakuraPage *page = sakura_page_new("page-invalid");
	SakuraTab first = { 0 };
	SakuraTab second = { 0 };
	SakuraLayoutNode *first_leaf;
	SakuraLayoutNode *second_leaf;
	SakuraLayoutNode *split;
	GError *error = NULL;

	first_leaf = sakura_layout_leaf_new(page, &first);
	second_leaf = sakura_layout_leaf_new(page, &second);
	g_assert_nonnull(first_leaf);
	g_assert_nonnull(second_leaf);
	/* Deliberately create an invalid duplicate-child split for validation. */
	split = sakura_layout_split_new(page, SAKURA_SPLIT_DOWN, 0.5,
	                               first_leaf, second_leaf);
	g_assert_nonnull(split);
	page->layout_root = split;
	split->data.split.second = first_leaf;
	first_leaf->parent = split;
	g_assert_false(sakura_layout_validate(page, &error));
	g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL);
	g_clear_error(&error);

	/* Restore ownership before freeing the page. */
	split->data.split.second = second_leaf;
	second_leaf->parent = split;
	sakura_page_free(page);
}


int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/tools/ids/round-trip", test_tool_ids_round_trip);
	g_test_add_func("/tools/ids/unknown", test_tool_ids_reject_unknown_values);
	g_test_add_func("/tools/scope", test_tool_scope);
	g_test_add_func("/codex/reasoning-efforts", test_codex_reasoning_efforts);
	g_test_add_func("/session/snapshot/round-trip", test_session_snapshot_round_trip);
	g_test_add_func("/session/snapshot/reject-cycle", test_session_snapshot_rejects_group_cycle);
	g_test_add_func("/session/snapshot/reject-task-cycle", test_session_snapshot_rejects_task_cycle);
	g_test_add_func("/session/snapshot/reject-task-page-selection-invariants",
	                test_session_snapshot_rejects_inconsistent_task_page_selection);
	g_test_add_func("/session/snapshot/layout-round-trip", test_session_layout_round_trip);
	g_test_add_func("/session/snapshot/reject-disconnected-layout",
	                test_session_rejects_disconnected_layout);
	g_test_add_func("/session/snapshot/repairs-duplicate-page-ids",
	                test_session_repairs_duplicate_page_ids);
	g_test_add_func("/session/snapshot/optional-defaults", test_session_snapshot_uses_safe_optional_defaults);
	g_test_add_func("/session/snapshot/preserve-on-failure", test_session_snapshot_preserves_previous_on_failure);
	g_test_add_func("/layout/split-and-collapse", test_layout_split_and_collapse);
	g_test_add_func("/layout/reject-invalid-tree", test_layout_rejects_invalid_tree);
	return g_test_run();
}
