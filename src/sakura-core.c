#include "sakura-core.h"

static void
sakura_session_group_record_free(gpointer data)
{
	SakuraSessionGroupRecord *record = data;

	if (record == NULL)
		return;
	g_free(record->id);
	g_free(record->parent_id);
	g_free(record->title);
	g_free(record->directory);
	g_free(record);
}


static void
sakura_session_task_record_free(gpointer data)
{
	SakuraSessionTaskRecord *record = data;

	if (record == NULL)
		return;
	g_free(record->id);
	g_free(record->parent_id);
	g_free(record->group_id);
	g_free(record->title);
	g_free(record->provider);
	g_free(record->external_id);
	g_free(record->url);
	g_free(record);
}


static void
sakura_session_tab_record_free(gpointer data)
{
	SakuraSessionTabRecord *record = data;

	if (record == NULL)
		return;
	g_free(record->parent_id);
	g_free(record->cwd);
	g_free(record->title);
	g_free(record->terminal_id);
	g_free(record->tool_id);
	g_free(record->tool_target);
	g_free(record->codex_session_id);
	g_free(record->codex_session_name);
	g_free(record->codex_model);
	g_free(record->codex_reasoning_effort);
	g_free(record->codex_tracking_token);
	g_free(record->page_id);
	g_free(record);
}


static void
sakura_session_page_record_free(gpointer data)
{
	SakuraSessionPageRecord *record = data;

	if (record == NULL)
		return;
	g_free(record->id);
	g_free(record->parent_id);
	g_free(record->group_id);
	g_free(record->title);
	g_free(record->root_layout_id);
	g_free(record->active_terminal_id);
	g_free(record->task_id);
	g_free(record);
}


static void
sakura_session_layout_record_free(gpointer data)
{
	SakuraSessionLayoutRecord *record = data;

	if (record == NULL)
		return;
	g_free(record->id);
	g_free(record->page_id);
	g_free(record->type);
	g_free(record->first_id);
	g_free(record->second_id);
	g_free(record->terminal_id);
	g_free(record);
}


void
sakura_session_job_record_free(SakuraSessionJobRecord *record)
{
	if (record == NULL)
		return;
	g_free(record->name);
	g_free(record->session_name);
	g_free(record->schedule);
	g_free(record->timezone);
	g_free(record->prompt_file);
	g_free(record->overlap_policy);
	g_free(record->missed_run_policy);
	g_free(record->last_status);
	g_free(record->last_error);
	g_free(record);
}


SakuraSessionJobRecord *
sakura_session_job_record_copy(const SakuraSessionJobRecord *record)
{
	SakuraSessionJobRecord *copy;

	if (record == NULL)
		return NULL;
	copy = g_new0(SakuraSessionJobRecord, 1);
	*copy = *record;
	copy->name = g_strdup(record->name);
	copy->session_name = g_strdup(record->session_name);
	copy->schedule = g_strdup(record->schedule);
	copy->timezone = g_strdup(record->timezone);
	copy->prompt_file = g_strdup(record->prompt_file);
	copy->overlap_policy = g_strdup(record->overlap_policy);
	copy->missed_run_policy = g_strdup(record->missed_run_policy);
	copy->last_status = g_strdup(record->last_status);
	copy->last_error = g_strdup(record->last_error);
	return copy;
}


static void
sakura_session_sidebar_expansion_record_free(gpointer data)
{
	SakuraSessionSidebarExpansionRecord *record = data;

	if (record == NULL)
		return;
	g_free(record->id);
	g_free(record);
}


SakuraSessionSnapshot *
sakura_session_snapshot_new(void)
{
	SakuraSessionSnapshot *snapshot = g_new0(SakuraSessionSnapshot, 1);

	snapshot->groups = g_ptr_array_new_with_free_func(sakura_session_group_record_free);
	snapshot->tasks = g_ptr_array_new_with_free_func(sakura_session_task_record_free);
	snapshot->tabs = g_ptr_array_new_with_free_func(sakura_session_tab_record_free);
	snapshot->pages = g_ptr_array_new_with_free_func(sakura_session_page_record_free);
	snapshot->layouts = g_ptr_array_new_with_free_func(sakura_session_layout_record_free);
	snapshot->expanded_sidebar_nodes = g_ptr_array_new_with_free_func(
		sakura_session_sidebar_expansion_record_free);
	snapshot->jobs = g_ptr_array_new_with_free_func(
		(GDestroyNotify)sakura_session_job_record_free);
	snapshot->workspace_id = g_uuid_string_random();
	snapshot->selected_terminal = -1;
	snapshot->active_group_id = g_strdup("root");
	snapshot->sidebar_visible = TRUE;
	snapshot->sidebar_width = 200;
	snapshot->show_archived = FALSE;
	snapshot->sidebar_expansion_saved = FALSE;
	return snapshot;
}


void
sakura_session_snapshot_free(SakuraSessionSnapshot *snapshot)
{
	if (snapshot == NULL)
		return;
	g_clear_pointer(&snapshot->groups, g_ptr_array_unref);
	g_clear_pointer(&snapshot->tasks, g_ptr_array_unref);
	g_clear_pointer(&snapshot->tabs, g_ptr_array_unref);
	g_clear_pointer(&snapshot->pages, g_ptr_array_unref);
	g_clear_pointer(&snapshot->layouts, g_ptr_array_unref);
	g_clear_pointer(&snapshot->expanded_sidebar_nodes, g_ptr_array_unref);
	g_clear_pointer(&snapshot->jobs, g_ptr_array_unref);
	g_free(snapshot->workspace_id);
	g_free(snapshot->selected_terminal_id);
	g_free(snapshot->selected_page_id);
	g_free(snapshot->selected_task_id);
	g_free(snapshot->active_group_id);
	g_free(snapshot->root_directory);
	g_free(snapshot);
}
