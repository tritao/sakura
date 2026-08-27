#include <locale.h>
#include <stdlib.h>
#include <string.h>

#include "sakura-control-client.h"
#include "sakura-control-discovery.h"
#include "sakura-control-transport.h"

typedef struct {
	gchar *title;
	gchar *session_name;
	gchar *working_directory;
	gchar *prompt_file;
	gchar *model;
	gchar *reasoning;
	gchar *goal_file;
	gchar *goal_policy;
} SakuraCtlManifestEntry;

static void
manifest_entry_free(SakuraCtlManifestEntry *entry)
{
	if (entry == NULL)
		return;
	g_free(entry->title);
	g_free(entry->session_name);
	g_free(entry->working_directory);
	g_free(entry->prompt_file);
	g_free(entry->model);
	g_free(entry->reasoning);
	g_free(entry->goal_file);
	g_free(entry->goal_policy);
	g_free(entry);
}

static gchar *
yaml_scalar(const gchar *value)
{
	gchar *result = g_strdup(value != NULL ? value : "");
	gsize length;

	g_strstrip(result);
	length = strlen(result);
	if (length >= 2 && ((result[0] == '"' && result[length - 1] == '"') ||
	                    (result[0] == '\'' && result[length - 1] == '\''))) {
		result[length - 1] = '\0';
		memmove(result, result + 1, length - 1);
	}
	return result;
}

static GPtrArray *
parse_manifest(const gchar *path, GError **error)
{
	g_autofree gchar *contents = NULL;
	g_auto(GStrv) lines = NULL;
	GPtrArray *entries;
	g_autoptr(GHashTable) session_names = g_hash_table_new(g_str_hash, g_str_equal);
	SakuraCtlManifestEntry *current = NULL;

	if (!g_file_get_contents(path, &contents, NULL, error))
		return NULL;
	entries = g_ptr_array_new_with_free_func((GDestroyNotify)manifest_entry_free);
	lines = g_strsplit(contents, "\n", -1);
	for (guint i = 0; lines[i] != NULL; i++) {
		gchar *line = g_strstrip(lines[i]);
		gchar *separator;
		gchar *key;
		g_autofree gchar *value = NULL;

		if (line[0] == '\0' || line[0] == '#' || strcmp(line, "sessions:") == 0)
			continue;
		if (g_str_has_prefix(line, "- ")) {
			current = g_new0(SakuraCtlManifestEntry, 1);
			g_ptr_array_add(entries, current);
			line = g_strstrip(line + 2);
		}
		if (current == NULL || (separator = strchr(line, ':')) == NULL)
			continue;
		*separator = '\0';
		key = g_strstrip(line);
		value = yaml_scalar(separator + 1);
		if (strcmp(key, "title") == 0 || strcmp(key, "name") == 0)
			current->title = g_steal_pointer(&value);
		else if (strcmp(key, "session_name") == 0 ||
		         strcmp(key, "session-name") == 0)
			current->session_name = g_steal_pointer(&value);
		else if (strcmp(key, "working_directory") == 0 ||
		         strcmp(key, "working-directory") == 0 || strcmp(key, "cwd") == 0)
			current->working_directory = g_steal_pointer(&value);
		else if (strcmp(key, "prompt_file") == 0 ||
		         strcmp(key, "prompt-file") == 0)
			current->prompt_file = g_steal_pointer(&value);
		else if (strcmp(key, "model") == 0)
			current->model = g_steal_pointer(&value);
		else if (strcmp(key, "reasoning") == 0)
			current->reasoning = g_steal_pointer(&value);
		else if (strcmp(key, "goal_file") == 0 || strcmp(key, "goal-file") == 0)
			current->goal_file = g_steal_pointer(&value);
		else if (strcmp(key, "goal_policy") == 0 || strcmp(key, "goal-policy") == 0)
			current->goal_policy = g_steal_pointer(&value);
	}
	for (guint i = 0; i < entries->len; i++) {
		SakuraCtlManifestEntry *entry = g_ptr_array_index(entries, i);
		if (entry->title == NULL || entry->title[0] == '\0' ||
		    entry->working_directory == NULL || entry->working_directory[0] == '\0') {
			g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			            "manifest session %u requires title and working_directory", i + 1);
			g_ptr_array_unref(entries);
			return NULL;
		}
		if (entry->goal_file != NULL && entry->goal_file[0] != '\0') {
			if (entry->goal_policy == NULL || entry->goal_policy[0] == '\0')
				entry->goal_policy = g_strdup("start-if-none");
			if (strcmp(entry->goal_policy, "start-if-none") != 0) {
				g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
				            "manifest session %u has unsupported goal_policy '%s'",
				            i + 1, entry->goal_policy);
				g_ptr_array_unref(entries);
				return NULL;
			}
			if (!g_path_is_absolute(entry->goal_file)) {
				g_autofree gchar *directory = g_path_get_dirname(path);
				gchar *resolved = g_canonicalize_filename(entry->goal_file, directory);

				g_free(entry->goal_file);
				entry->goal_file = resolved;
			}
			if (!g_file_test(entry->goal_file, G_FILE_TEST_IS_REGULAR)) {
				g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
				            "goal file was not found: %s", entry->goal_file);
				g_ptr_array_unref(entries);
				return NULL;
			}
		}
		if (entry->session_name != NULL && entry->session_name[0] != '\0' &&
		    !g_hash_table_add(session_names, entry->session_name)) {
			g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			            "manifest contains duplicate session_name '%s'",
			            entry->session_name);
			g_ptr_array_unref(entries);
			return NULL;
		}
	}
	if (entries->len == 0) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                    "manifest contains no sessions");
		g_ptr_array_unref(entries);
		return NULL;
	}
	return entries;
}

static gboolean
request_snapshot(SakuraControlClientConnection *connection,
                 SakuraSessionSnapshot **snapshot, GError **error)
{
	g_autoptr(GByteArray) request = g_byte_array_new();
	g_autoptr(GByteArray) response = NULL;
	g_autofree gchar *request_id = g_uuid_string_random();
	guint64 sequence = 0;

	*snapshot = NULL;
	return sakura_control_encode_get_snapshot_request(request_id, request) &&
	       sakura_control_client_request(connection, request, &response, error) &&
	       sakura_control_decode_snapshot_response(response->data, response->len,
	                                               &sequence, snapshot, error);
}

static gboolean
request_workspace_change(SakuraControlClientConnection *connection,
                         GByteArray *request, SakuraSessionSnapshot **snapshot,
                         GError **error)
{
	g_autoptr(GByteArray) response = NULL;
	guint64 sequence = 0;

	*snapshot = NULL;
	return sakura_control_client_request(connection, request, &response, error) &&
	       sakura_control_decode_snapshot_response(response->data, response->len,
	                                               &sequence, snapshot, error);
}

static SakuraSessionGroupRecord *
find_group(SakuraSessionSnapshot *snapshot, const gchar *name, GError **error)
{
	SakuraSessionGroupRecord *match = NULL;

	for (guint i = 0; snapshot != NULL && snapshot->groups != NULL &&
	                 i < snapshot->groups->len; i++) {
		SakuraSessionGroupRecord *group = g_ptr_array_index(snapshot->groups, i);

		if (group == NULL || group->archived || g_strcmp0(group->title, name) != 0)
			continue;
		if (match != NULL) {
			g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
			            "more than one group is named '%s'; use --group ID", name);
			return NULL;
		}
		match = group;
	}
	return match;
}

static gboolean
create_group(SakuraControlClientConnection *connection, const gchar *name,
             const gchar *directory, SakuraSessionSnapshot **snapshot,
             GError **error)
{
	g_autoptr(GByteArray) request = g_byte_array_new();
	g_autofree gchar *request_id = g_uuid_string_random();

	return sakura_control_encode_create_group_request(
		request_id, "root", name, directory, request) &&
	       request_workspace_change(connection, request, snapshot, error);
}

static gboolean
rename_page(SakuraControlClientConnection *connection, const gchar *page_id,
            const gchar *title, gboolean title_set_by_user, GError **error)
{
	g_autoptr(GByteArray) request = g_byte_array_new();
	g_autofree gchar *request_id = g_uuid_string_random();
	SakuraSessionSnapshot *snapshot = NULL;
	gboolean success;

	success = sakura_control_encode_rename_page_request(
		request_id, page_id, title, title_set_by_user, request) &&
	          request_workspace_change(connection, request, &snapshot, error);
	sakura_session_snapshot_free(snapshot);
	return success;
}

static gchar *
json_escape(const gchar *value)
{
	GString *escaped = g_string_new(NULL);

	for (const guchar *cursor = (const guchar *)(value != NULL ? value : "");
	     *cursor != '\0'; cursor++) {
		switch (*cursor) {
		case '\\': g_string_append(escaped, "\\\\"); break;
		case '"': g_string_append(escaped, "\\\""); break;
		case '\n': g_string_append(escaped, "\\n"); break;
		case '\r': g_string_append(escaped, "\\r"); break;
		case '\t': g_string_append(escaped, "\\t"); break;
		default:
			if (*cursor < 0x20)
				g_string_append_printf(escaped, "\\u%04x", *cursor);
			else
				g_string_append_c(escaped, (gchar)*cursor);
		}
	}
	return g_string_free(escaped, FALSE);
}

static gchar *
wait_for_codex_session_id(SakuraControlClientConnection *connection,
                          const gchar *terminal_id, GError **error)
{
	const gchar *timeout_override = g_getenv("SAKURA_CTL_READY_TIMEOUT_MS");
	guint64 timeout_ms = 30000;
	gint64 deadline;

	if (timeout_override != NULL && timeout_override[0] != '\0') {
		gchar *end = NULL;
		guint64 parsed = g_ascii_strtoull(timeout_override, &end, 10);

		if (end != timeout_override && end != NULL && *end == '\0' && parsed > 0)
			timeout_ms = parsed;
	}
	deadline = g_get_monotonic_time() + (gint64)timeout_ms * 1000;

	do {
		SakuraSessionSnapshot *snapshot = NULL;
		gchar *session_id = NULL;

		if (!request_snapshot(connection, &snapshot, error))
			return NULL;
		for (guint i = 0; snapshot->tabs != NULL && i < snapshot->tabs->len; i++) {
			SakuraSessionTabRecord *tab = g_ptr_array_index(snapshot->tabs, i);
			if (tab != NULL && g_strcmp0(tab->terminal_id, terminal_id) == 0 &&
			    tab->codex_session_id != NULL && tab->codex_session_id[0] != '\0') {
				session_id = g_strdup(tab->codex_session_id);
				break;
			}
		}
		sakura_session_snapshot_free(snapshot);
		if (session_id != NULL)
			return session_id;
		g_usleep(100 * 1000);
	} while (g_get_monotonic_time() < deadline);
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
	                    "timed out waiting for the Codex session ID");
	return NULL;
}

static gboolean
set_codex_session_name(const gchar *session_id, const gchar *name,
                       GError **error)
{
	g_autofree gchar *helper = g_find_program_in_path(
		"sakura-codex-session-name");
	g_autofree gchar *stderr_text = NULL;
	gint wait_status = 0;
	gchar *argv[] = { NULL, "--set-name", (gchar *)session_id, (gchar *)name, NULL };

	if (helper == NULL && g_file_test(SAKURA_CODEX_NAME_HELPER_BUILD_PATH,
	                                  G_FILE_TEST_IS_EXECUTABLE))
		helper = g_strdup(SAKURA_CODEX_NAME_HELPER_BUILD_PATH);
	if (helper == NULL) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		                    "sakura-codex-session-name was not found");
		return FALSE;
	}
	argv[0] = helper;
	if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL, NULL,
	                  &stderr_text, &wait_status, error))
		return FALSE;
	if (!g_spawn_check_wait_status(wait_status, error)) {
		if (stderr_text != NULL && stderr_text[0] != '\0')
			g_prefix_error(error, "%s: ", g_strstrip(stderr_text));
		return FALSE;
	}
	return TRUE;
}

static gchar *
prepare_codex_thread(const SakuraCtlManifestEntry *entry, GError **error)
{
	g_autofree gchar *helper = g_find_program_in_path("sakura-codex-app-server");
	g_autofree gchar *stdout_text = NULL;
	g_autofree gchar *stderr_text = NULL;
	g_autoptr(GPtrArray) args = g_ptr_array_new_with_free_func(g_free);
	gint wait_status = 0;

	if (helper == NULL && g_file_test(SAKURA_CODEX_APP_SERVER_HELPER_BUILD_PATH,
	                                  G_FILE_TEST_IS_EXECUTABLE))
		helper = g_strdup(SAKURA_CODEX_APP_SERVER_HELPER_BUILD_PATH);
	if (helper == NULL) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		                    "sakura-codex-app-server was not found");
		return NULL;
	}
	g_ptr_array_add(args, g_steal_pointer(&helper));
	g_ptr_array_add(args, g_strdup("--working-directory"));
	g_ptr_array_add(args, g_strdup(entry->working_directory));
	g_ptr_array_add(args, g_strdup("--prompt-file"));
	g_ptr_array_add(args, g_strdup(entry->prompt_file));
	if (entry->model != NULL && entry->model[0] != '\0') {
		g_ptr_array_add(args, g_strdup("--model"));
		g_ptr_array_add(args, g_strdup(entry->model));
	}
	if (entry->reasoning != NULL && entry->reasoning[0] != '\0') {
		g_ptr_array_add(args, g_strdup("--reasoning"));
		g_ptr_array_add(args, g_strdup(entry->reasoning));
	}
	if (entry->session_name != NULL && entry->session_name[0] != '\0') {
		g_ptr_array_add(args, g_strdup("--session-name"));
		g_ptr_array_add(args, g_strdup(entry->session_name));
	}
	g_ptr_array_add(args, NULL);
	if (!g_spawn_sync(NULL, (gchar **)args->pdata, NULL, G_SPAWN_DEFAULT,
	                  NULL, NULL, &stdout_text, &stderr_text, &wait_status,
	                  error) || !g_spawn_check_wait_status(wait_status, error)) {
		if (error != NULL && *error != NULL && stderr_text != NULL &&
		    stderr_text[0] != '\0')
			g_prefix_error(error, "%s: ", g_strstrip(stderr_text));
		return NULL;
	}
	g_strstrip(stdout_text);
	if (stdout_text[0] == '\0') {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                    "Codex app-server returned no thread ID");
		return NULL;
	}
	return g_steal_pointer(&stdout_text);
}

static gchar *
run_codex_goal(const gchar *session_id, const gchar *action,
               const gchar *goal_file, const gchar *title, GError **error)
{
	g_autofree gchar *helper = g_find_program_in_path("sakura-codex-app-server");
	g_autofree gchar *stdout_text = NULL;
	g_autofree gchar *stderr_text = NULL;
	g_autoptr(GPtrArray) args = g_ptr_array_new_with_free_func(g_free);
	gint wait_status = 0;

	if (helper == NULL && g_file_test(SAKURA_CODEX_APP_SERVER_HELPER_BUILD_PATH,
	                                  G_FILE_TEST_IS_EXECUTABLE))
		helper = g_strdup(SAKURA_CODEX_APP_SERVER_HELPER_BUILD_PATH);
	if (helper == NULL) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		                    "sakura-codex-app-server was not found");
		return NULL;
	}
	g_ptr_array_add(args, g_steal_pointer(&helper));
	g_ptr_array_add(args, g_strdup("--thread-id"));
	g_ptr_array_add(args, g_strdup(session_id));
	g_ptr_array_add(args, g_strdup("--goal-action"));
	g_ptr_array_add(args, g_strdup(action));
	if (goal_file != NULL && goal_file[0] != '\0') {
		g_ptr_array_add(args, g_strdup("--goal-file"));
		g_ptr_array_add(args, g_strdup(goal_file));
	}
	if (title != NULL && title[0] != '\0') {
		g_ptr_array_add(args, g_strdup("--title"));
		g_ptr_array_add(args, g_strdup(title));
	}
	g_ptr_array_add(args, NULL);
	if (!g_spawn_sync(NULL, (gchar **)args->pdata, NULL, G_SPAWN_DEFAULT,
	                  NULL, NULL, &stdout_text, &stderr_text, &wait_status,
	                  error) || !g_spawn_check_wait_status(wait_status, error)) {
		if (error != NULL && *error != NULL && stderr_text != NULL &&
		    stderr_text[0] != '\0')
			g_prefix_error(error, "%s: ", g_strstrip(stderr_text));
		return NULL;
	}
	g_strstrip(stdout_text);
	return g_steal_pointer(&stdout_text);
}

static SakuraSessionPageRecord *snapshot_page_for_tab(
	SakuraSessionSnapshot *snapshot, const SakuraSessionTabRecord *tab);

static GPtrArray *
find_manifest_identity_matches(SakuraSessionSnapshot *snapshot,
                               const gchar *group_id,
                               const SakuraCtlManifestEntry *entry)
{
	g_autofree gchar *wanted_cwd = g_canonicalize_filename(
		entry->working_directory, NULL);
	GPtrArray *matches = g_ptr_array_new();

	for (guint i = 0; snapshot != NULL && snapshot->pages != NULL &&
	                 i < snapshot->pages->len; i++) {
		SakuraSessionPageRecord *page = g_ptr_array_index(snapshot->pages, i);

		if (page == NULL || g_strcmp0(page->group_id, group_id) != 0)
			continue;
		for (guint j = 0; snapshot->tabs != NULL && j < snapshot->tabs->len; j++) {
			SakuraSessionTabRecord *tab = g_ptr_array_index(snapshot->tabs, j);
			g_autofree gchar *actual_cwd = tab != NULL && tab->cwd != NULL
			                             ? g_canonicalize_filename(tab->cwd, NULL)
			                             : NULL;
			gboolean has_name = tab != NULL && tab->codex_session_name != NULL &&
			                    tab->codex_session_name[0] != '\0';
			gboolean stable_match = entry->session_name != NULL &&
			                        entry->session_name[0] != '\0' && has_name &&
			                        g_strcmp0(tab->codex_session_name,
			                                  entry->session_name) == 0;
			gboolean legacy_match = !has_name &&
			                        g_strcmp0(page->title, entry->title) == 0 &&
			                        g_strcmp0(actual_cwd, wanted_cwd) == 0;

			if (tab != NULL && tab->kind == SAKURA_TAB_CODEX &&
			    g_strcmp0(tab->page_id, page->id) == 0 &&
			    (stable_match || legacy_match))
				g_ptr_array_add(matches, tab);
		}
	}
	return matches;
}

static gboolean
manifest_terminal_is_healthy(SakuraSessionSnapshot *snapshot,
                             const SakuraSessionTabRecord *tab,
                             const SakuraCtlManifestEntry *entry)
{
	g_autofree gchar *wanted_cwd = g_canonicalize_filename(
		entry->working_directory, NULL);
	g_autofree gchar *actual_cwd = tab != NULL && tab->cwd != NULL
	                             ? g_canonicalize_filename(tab->cwd, NULL) : NULL;
	SakuraSessionPageRecord *page = snapshot_page_for_tab(snapshot, tab);

	return tab != NULL && page != NULL && !page->archived &&
	       g_strcmp0(actual_cwd, wanted_cwd) == 0 &&
	       tab->runtime_status == SAKURA_TERMINAL_RUNNING &&
	       tab->codex_session_id != NULL && tab->codex_session_id[0] != '\0' &&
	       g_strcmp0(tab->codex_model, entry->model) == 0 &&
	       g_strcmp0(tab->codex_reasoning_effort, entry->reasoning) == 0;
}

static gboolean
snapshot_terminal_is_visible(const SakuraSessionSnapshot *snapshot,
                             const SakuraSessionTabRecord *tab)
{
	for (guint index = 0; snapshot != NULL && snapshot->pages != NULL &&
	                     index < snapshot->pages->len; index++) {
		SakuraSessionPageRecord *page = g_ptr_array_index(snapshot->pages, index);

		if (page != NULL && g_strcmp0(page->id, tab->page_id) == 0)
			return !page->archived;
	}
	return FALSE;
}

static void
inherit_terminal_dimensions(const SakuraSessionSnapshot *snapshot,
                            gint *columns, gint *rows)
{
	const SakuraSessionTabRecord *best = NULL;
	guint64 best_area = 0;

	for (guint index = 0; snapshot != NULL && snapshot->tabs != NULL &&
	                     index < snapshot->tabs->len; index++) {
		SakuraSessionTabRecord *candidate = g_ptr_array_index(snapshot->tabs, index);
		guint64 area;

		if (candidate == NULL || candidate->cols == 0 || candidate->rows == 0 ||
		    candidate->runtime_status != SAKURA_TERMINAL_RUNNING ||
		    !snapshot_terminal_is_visible(snapshot, candidate))
			continue;
		if (snapshot->selected_terminal_id != NULL &&
		    g_strcmp0(candidate->terminal_id, snapshot->selected_terminal_id) == 0) {
			best = candidate;
			break;
		}
		area = (guint64)candidate->cols * candidate->rows;
		if (best == NULL || area > best_area) {
			best = candidate;
			best_area = area;
		}
	}
	if (*columns < 0)
		*columns = best != NULL ? (gint)best->cols : 80;
	if (*rows < 0)
		*rows = best != NULL ? (gint)best->rows : 24;
}

static SakuraSessionPageRecord *
snapshot_page_for_tab(SakuraSessionSnapshot *snapshot,
                      const SakuraSessionTabRecord *tab)
{
	for (guint index = 0; snapshot != NULL && snapshot->pages != NULL &&
	                     index < snapshot->pages->len; index++) {
		SakuraSessionPageRecord *page = g_ptr_array_index(snapshot->pages, index);

		if (page != NULL && g_strcmp0(page->id, tab->page_id) == 0)
			return page;
	}
	return NULL;
}

static GPtrArray *
find_goal_sessions(SakuraSessionSnapshot *snapshot, const gchar *group_id,
                   const gchar *title, const gchar *session_name)
{
	GPtrArray *matches = g_ptr_array_new();

	for (guint index = 0; snapshot != NULL && snapshot->tabs != NULL &&
	                     index < snapshot->tabs->len; index++) {
		SakuraSessionTabRecord *tab = g_ptr_array_index(snapshot->tabs, index);
		SakuraSessionPageRecord *page = tab != NULL
		                               ? snapshot_page_for_tab(snapshot, tab) : NULL;

		if (tab == NULL || page == NULL || page->archived ||
		    tab->kind != SAKURA_TAB_CODEX || tab->codex_session_id == NULL ||
		    tab->codex_session_id[0] == '\0' ||
		    (group_id != NULL && group_id[0] != '\0' &&
		     g_strcmp0(page->group_id, group_id) != 0) ||
		    (title != NULL && title[0] != '\0' &&
		     g_strcmp0(page->title, title) != 0) ||
		    (session_name != NULL && session_name[0] != '\0' &&
		     g_strcmp0(tab->codex_session_name, session_name) != 0))
			continue;
		g_ptr_array_add(matches, tab);
	}
	return matches;
}

static gboolean
apply_manifest(SakuraControlClientConnection *connection,
               const gchar *manifest_path, const gchar *workspace_id,
               const gchar *group_id, const gchar *group_name,
               const gchar *print_format, gboolean dry_run, GError **error)
{
	g_autoptr(GPtrArray) entries = parse_manifest(manifest_path, error);
	g_autofree gchar *self = g_file_read_link("/proc/self/exe", error);
	gboolean had_conflict = FALSE;

	if (entries == NULL || self == NULL)
		return FALSE;
	for (guint i = 0; i < entries->len; i++) {
		SakuraCtlManifestEntry *entry = g_ptr_array_index(entries, i);
		SakuraSessionSnapshot *snapshot = NULL;
		g_autoptr(GPtrArray) identity_matches = NULL;
		SakuraSessionTabRecord *match = NULL;
		GPtrArray *args;
		g_autofree gchar *stdout_text = NULL;
		g_autofree gchar *stderr_text = NULL;
		g_autofree gchar *prepared_session_id = NULL;
		gint wait_status = 0;

		if (!request_snapshot(connection, &snapshot, error))
			return FALSE;
		identity_matches = find_manifest_identity_matches(snapshot, group_id, entry);
		if (identity_matches->len == 1)
			match = g_ptr_array_index(identity_matches, 0);
		if (identity_matches->len > 1 ||
		    (match != NULL && !manifest_terminal_is_healthy(snapshot, match, entry))) {
			if (print_format != NULL && strcmp(print_format, "json") == 0) {
				g_autofree gchar *j_title = json_escape(entry->title);
				g_autofree gchar *j_name = json_escape(entry->session_name);
				g_print("{\"status\":\"conflict\",\"title\":\"%s\","
				        "\"session_name\":\"%s\"}\n", j_title, j_name);
			} else
				g_print("conflict\t%s\t%s\n", entry->session_name != NULL
				        ? entry->session_name : "-", entry->title);
			had_conflict = TRUE;
			sakura_session_snapshot_free(snapshot);
			if (dry_run)
				continue;
			g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
			            "manifest identity '%s' is ambiguous or stale",
			            entry->session_name != NULL ? entry->session_name
			                                        : entry->title);
			return FALSE;
		}
		if (match != NULL) {
			if (!dry_run && entry->goal_file != NULL && entry->goal_file[0] != '\0') {
				g_autofree gchar *goal_output = run_codex_goal(
					match->codex_session_id, entry->goal_policy,
					entry->goal_file, entry->title, error);

				if (goal_output == NULL) {
					sakura_session_snapshot_free(snapshot);
					return FALSE;
				}
			}
			if (print_format != NULL && strcmp(print_format, "json") == 0) {
				g_autofree gchar *j_title = json_escape(entry->title);
				g_autofree gchar *j_terminal = json_escape(match->terminal_id);
				g_print("{\"status\":\"%s\",\"title\":\"%s\","
				        "\"terminal_id\":\"%s\"}\n",
				        dry_run ? "reuse" : "reused", j_title, j_terminal);
			} else
				g_print("%s\t%s\t%s\n", dry_run ? "reuse" : "reused",
				        entry->title, match->terminal_id);
			sakura_session_snapshot_free(snapshot);
			continue;
		}
		if (dry_run) {
			if (print_format != NULL && strcmp(print_format, "json") == 0) {
				g_autofree gchar *j_title = json_escape(entry->title);
				g_autofree gchar *j_name = json_escape(entry->session_name);
				g_print("{\"status\":\"create\",\"title\":\"%s\","
				        "\"session_name\":\"%s\"}\n", j_title, j_name);
			} else
				g_print("create\t%s\t%s\n", entry->session_name != NULL
				        ? entry->session_name : "-", entry->title);
			sakura_session_snapshot_free(snapshot);
			continue;
		}
		sakura_session_snapshot_free(snapshot);
		args = g_ptr_array_new_with_free_func(g_free);
		g_ptr_array_add(args, g_strdup(self));
		g_ptr_array_add(args, g_strdup("codex"));
		g_ptr_array_add(args, g_strdup("--workspace"));
		g_ptr_array_add(args, g_strdup(workspace_id));
		g_ptr_array_add(args, g_strdup("--group"));
		g_ptr_array_add(args, g_strdup(group_id));
		g_ptr_array_add(args, g_strdup("--title"));
		g_ptr_array_add(args, g_strdup(entry->title));
		g_ptr_array_add(args, g_strdup("--dynamic-title"));
		g_ptr_array_add(args, g_strdup("--working-directory"));
		g_ptr_array_add(args, g_strdup(entry->working_directory));
		if (entry->prompt_file != NULL && entry->prompt_file[0] != '\0') {
			prepared_session_id = prepare_codex_thread(entry, error);
			if (prepared_session_id == NULL) {
				g_ptr_array_unref(args);
				return FALSE;
			}
			g_ptr_array_add(args, g_strdup("--resume"));
			g_ptr_array_add(args, g_strdup(prepared_session_id));
			g_ptr_array_add(args, g_strdup("--prompt-file"));
			g_ptr_array_add(args, g_strdup(entry->prompt_file));
		}
		if (entry->reasoning != NULL && entry->reasoning[0] != '\0') {
			g_ptr_array_add(args, g_strdup("--reasoning"));
			g_ptr_array_add(args, g_strdup(entry->reasoning));
		}
		if (entry->model != NULL && entry->model[0] != '\0') {
			g_ptr_array_add(args, g_strdup("--model"));
			g_ptr_array_add(args, g_strdup(entry->model));
		}
		if (entry->session_name != NULL && entry->session_name[0] != '\0') {
			g_ptr_array_add(args, g_strdup("--session-name"));
			g_ptr_array_add(args, g_strdup(entry->session_name));
		}
		g_ptr_array_add(args, g_strdup("--print"));
		g_ptr_array_add(args, g_strdup(print_format != NULL ? print_format : "id"));
		g_ptr_array_add(args, NULL);
		if (!g_spawn_sync(NULL, (gchar **)args->pdata, NULL, G_SPAWN_DEFAULT,
		                  NULL, NULL, &stdout_text, &stderr_text, &wait_status,
		                  error) || !g_spawn_check_wait_status(wait_status, error)) {
			if (error != NULL && *error != NULL && stderr_text != NULL &&
			    stderr_text[0] != '\0')
				g_prefix_error(error, "%s: ", g_strstrip(stderr_text));
			g_ptr_array_unref(args);
			return FALSE;
		}
		g_print("%s", stdout_text != NULL ? stdout_text : "");
		g_ptr_array_unref(args);
		if (entry->goal_file != NULL && entry->goal_file[0] != '\0') {
			SakuraSessionSnapshot *created_snapshot = NULL;
			SakuraSessionTabRecord *created_match;

			if (!request_snapshot(connection, &created_snapshot, error))
				return FALSE;
			g_autoptr(GPtrArray) created_matches =
				find_manifest_identity_matches(created_snapshot, group_id, entry);
			created_match = created_matches->len == 1
			              ? g_ptr_array_index(created_matches, 0) : NULL;
			if (created_match == NULL ||
			    !manifest_terminal_is_healthy(created_snapshot, created_match, entry)) {
				sakura_session_snapshot_free(created_snapshot);
				g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
				            "created session '%s' was not found for goal setup",
				            entry->title);
				return FALSE;
			}
			g_autofree gchar *goal_output = run_codex_goal(
				created_match->codex_session_id, entry->goal_policy,
				entry->goal_file, entry->title, error);
			sakura_session_snapshot_free(created_snapshot);
			if (goal_output == NULL)
				return FALSE;
		}
	}
	(void)group_name;
	if (dry_run && had_conflict) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
		                    "manifest dry-run found conflicts");
		return FALSE;
	}
	return TRUE;
}

static SakuraControlWorkspaceEndpoint *
select_endpoint(GPtrArray *endpoints, const gchar *workspace,
                const gchar *session, GError **error)
{
	SakuraControlWorkspaceEndpoint *match = NULL;

	for (guint i = 0; endpoints != NULL && i < endpoints->len; i++) {
		SakuraControlWorkspaceEndpoint *candidate = g_ptr_array_index(endpoints, i);
		gboolean selected = workspace == NULL || workspace[0] == '\0' ||
		                    g_str_has_prefix(candidate->workspace_id, workspace);

		if (selected && session != NULL && session[0] != '\0') {
			g_autofree gchar *requested = g_canonicalize_filename(session, NULL);
			g_autofree gchar *actual = candidate->session_path != NULL
				? g_canonicalize_filename(candidate->session_path, NULL) : NULL;
			selected = g_strcmp0(requested, actual) == 0;
		}
		if (!selected)
			continue;
		if (match != NULL) {
			GString *message = g_string_new(
				"more than one workspace matched; use --workspace with one of:");
			for (guint j = 0; endpoints != NULL && j < endpoints->len; j++) {
				SakuraControlWorkspaceEndpoint *listed = g_ptr_array_index(endpoints, j);
				g_string_append_printf(message, "\n  %s\t%s", listed->workspace_id,
				                       listed->session_path != NULL
				                       ? listed->session_path : "-");
			}
			g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
			                    message->str);
			g_string_free(message, TRUE);
			return NULL;
		}
		match = candidate;
	}
	if (match == NULL)
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		                    "no running Sakura workspace matched");
	return match;
}

static void
print_workspaces(GPtrArray *endpoints)
{
	for (guint i = 0; endpoints != NULL && i < endpoints->len; i++) {
		SakuraControlWorkspaceEndpoint *endpoint = g_ptr_array_index(endpoints, i);

		g_print("%s\t%s\t%s\n", endpoint->workspace_id,
		        endpoint->session_path != NULL ? endpoint->session_path : "-",
		        endpoint->socket_path);
	}
}

int
main(int argc, char **argv)
{
	gchar *workspace = NULL, *session = NULL, *socket_path = NULL;
	gchar *cwd = NULL, *group_id = NULL, *group_name = NULL, *task_id = NULL;
	gchar *model = NULL, *reasoning = NULL, *resume = NULL, *title = NULL;
	gchar *session_name = NULL, *prompt_file = NULL, *print_format = NULL;
	gchar *goal_file = NULL;
	gchar *manifest = NULL, *delete_page_id = NULL, *terminal_target = NULL;
	gchar **arguments = NULL;
	gint columns = -1, rows = -1;
	GOptionContext *context = NULL;
	GError *error = NULL;
	GPtrArray *endpoints = NULL;
	SakuraControlWorkspaceEndpoint direct = { 0 }, *endpoint = NULL;
	SakuraControlClientConnection *connection = NULL;
	g_autofree gchar *page_id = NULL, *terminal_id = NULL, *launch_cwd = NULL;
	g_autofree gchar *codex_session_id = NULL;
	SakuraSessionSnapshot *snapshot = NULL;
	SakuraSessionGroupRecord *resolved_group = NULL;
	const gchar *command, *goal_action = NULL;
	gboolean codex, goal, success, apply = FALSE, create_group_if_missing = FALSE;
	gboolean dynamic_title = FALSE, dry_run = FALSE;
	gboolean created_resource = FALSE;
	int result = EXIT_FAILURE;
	GOptionEntry entries[] = {
		{ "workspace", 'w', 0, G_OPTION_ARG_STRING, &workspace,
		  "Target workspace ID or unique prefix", "ID" },
		{ "session-file", 'f', 0, G_OPTION_ARG_FILENAME, &session,
		  "Target the workspace using this session file", "PATH" },
		{ "socket", 's', 0, G_OPTION_ARG_FILENAME, &socket_path,
		  "Connect directly (requires --workspace)", "PATH" },
		{ "working-directory", 'd', 0, G_OPTION_ARG_FILENAME, &cwd,
		  "Initial working directory", "PATH" },
		{ "group", 'g', 0, G_OPTION_ARG_STRING, &group_id,
		  "Place the page in this group", "ID" },
		{ "group-name", 0, 0, G_OPTION_ARG_STRING, &group_name,
		  "Place the page in the uniquely named group", "NAME" },
		{ "create-group", 0, 0, G_OPTION_ARG_NONE, &create_group_if_missing,
		  "Create --group-name at the workspace root when absent", NULL },
		{ "task", 't', 0, G_OPTION_ARG_STRING, &task_id,
		  "Associate the page with this task", "ID" },
		{ "columns", 'c', 0, G_OPTION_ARG_INT, &columns,
		  "Initial terminal width", "COUNT" },
		{ "rows", 'r', 0, G_OPTION_ARG_INT, &rows,
		  "Initial terminal height", "COUNT" },
		{ "reasoning", 0, 0, G_OPTION_ARG_STRING, &reasoning,
		  "Codex reasoning effort", "EFFORT" },
		{ "model", 'm', 0, G_OPTION_ARG_STRING, &model,
		  "Codex model override", "MODEL" },
		{ "resume", 0, 0, G_OPTION_ARG_STRING, &resume,
		  "Resume a Codex session by ID", "SESSION" },
		{ "title", 0, 0, G_OPTION_ARG_STRING, &title,
		  "Set and lock the Sakura page title", "TITLE" },
		{ "dynamic-title", 0, 0, G_OPTION_ARG_NONE, &dynamic_title,
		  "Set an initial page title without locking dynamic updates", NULL },
		{ "dry-run", 0, 0, G_OPTION_ARG_NONE, &dry_run,
		  "Plan manifest changes without mutating the workspace", NULL },
		{ "session-name", 0, 0, G_OPTION_ARG_STRING, &session_name,
		  "Set the Codex session name once its ID is available", "NAME" },
		{ "prompt-file", 0, 0, G_OPTION_ARG_FILENAME, &prompt_file,
		  "Send this file as the initial Codex request", "PATH" },
		{ "file", 0, 0, G_OPTION_ARG_FILENAME, &goal_file,
		  "Goal objective file", "PATH" },
		{ "print", 0, 0, G_OPTION_ARG_STRING, &print_format,
		  "Output format: id or json", "FORMAT" },
		{ "manifest", 0, 0, G_OPTION_ARG_FILENAME, &manifest,
		  "Session manifest for 'codex apply'", "PATH" },
		{ "page", 0, 0, G_OPTION_ARG_STRING, &delete_page_id,
		  "Page ID for 'delete-page'", "ID" },
		{ "terminal", 0, 0, G_OPTION_ARG_STRING, &terminal_target,
		  "Target terminal ID for 'input'", "ID" },
		{ G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &arguments,
		  NULL, "COMMAND" },
		{ NULL }
	};

	setlocale(LC_ALL, "");

	context = g_option_context_new("- control a running Sakura workspace");
	g_option_context_set_summary(context,
		"Commands:\n  list         List running workspaces\n"
		"  groups       List groups in a workspace\n"
		"  new          Open a shell page\n"
		"  codex        Open a Codex page\n"
		"  goal         Manage a Codex session goal\n"
		"  input        Send --prompt-file to --terminal\n"
		"  delete-page  Remove a page and its terminals");
	g_option_context_add_main_entries(context, entries, NULL);
	if (!g_option_context_parse(context, &argc, &argv, &error))
		goto out;
	if (arguments == NULL || g_strv_length(arguments) < 1 ||
	    g_strv_length(arguments) > 2 ||
	    (g_strv_length(arguments) == 2 &&
	     !((strcmp(arguments[0], "codex") == 0 &&
	        strcmp(arguments[1], "apply") == 0) ||
	       strcmp(arguments[0], "goal") == 0))) {
		g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "exactly one command is required: list, groups, new, codex, or delete-page");
		goto out;
	}
	command = arguments[0];
	apply = g_strv_length(arguments) == 2 && strcmp(command, "codex") == 0;
	goal = strcmp(command, "goal") == 0;
	if (goal)
		goal_action = arguments[1];
	if (goal && (goal_action == NULL ||
	    (strcmp(goal_action, "start") != 0 &&
	     strcmp(goal_action, "status") != 0 &&
	     strcmp(goal_action, "pause") != 0 &&
	     strcmp(goal_action, "resume") != 0 &&
	     strcmp(goal_action, "clear") != 0))) {
		g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "goal requires start, status, pause, resume, or clear");
		goto out;
	}
	if (strcmp(command, "list") != 0 && strcmp(command, "groups") != 0 &&
	    strcmp(command, "new") != 0 &&
	    strcmp(command, "codex") != 0 && strcmp(command, "goal") != 0 &&
	    strcmp(command, "input") != 0 &&
	    strcmp(command, "delete-page") != 0) {
		g_set_error(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		            "unknown command: %s", command);
		goto out;
	}
	if (print_format != NULL && strcmp(print_format, "id") != 0 &&
	    strcmp(print_format, "json") != 0) {
		g_set_error(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		            "unknown --print format: %s", print_format);
		goto out;
	}
	endpoints = sakura_control_workspace_discover(&error);
	if (error != NULL)
		goto out;
	if (strcmp(command, "list") == 0) {
		print_workspaces(endpoints);
		result = EXIT_SUCCESS;
		goto out;
	}
	if (columns == 0 || columns < -1 || columns > 65535 ||
	    rows == 0 || rows < -1 || rows > 65535) {
		g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "rows and columns must be between 1 and 65535");
		goto out;
	}
	if (socket_path != NULL) {
		if (workspace == NULL || workspace[0] == '\0') {
			g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			                    "--socket requires --workspace");
			goto out;
		}
		direct.workspace_id = workspace;
		direct.socket_path = socket_path;
		endpoint = &direct;
	} else if ((endpoint = select_endpoint(endpoints, workspace, session,
	                                      &error)) == NULL) {
		goto out;
	}
	codex = strcmp(command, "codex") == 0;
	connection = sakura_control_client_connect(
		endpoint->socket_path, endpoint->workspace_id, "sakura-ctl",
		SAKURA_CONTROL_CAPABILITY_WORKSPACE |
		SAKURA_CONTROL_CAPABILITY_TERMINALS |
		((codex || goal) ? SAKURA_CONTROL_CAPABILITY_CODEX : 0) |
		(model != NULL ? SAKURA_CONTROL_CAPABILITY_CODEX_MODEL : 0), &error);
	if (connection == NULL)
		goto out;
	if (strcmp(command, "groups") == 0) {
		if (!request_snapshot(connection, &snapshot, &error))
			goto out;
		for (guint i = 0; snapshot->groups != NULL && i < snapshot->groups->len; i++) {
			SakuraSessionGroupRecord *group = g_ptr_array_index(snapshot->groups, i);
			if (group == NULL)
				continue;
			if (print_format != NULL && strcmp(print_format, "json") == 0) {
				g_autofree gchar *j_id = json_escape(group->id);
				g_autofree gchar *j_name = json_escape(group->title);
				g_autofree gchar *j_directory = json_escape(group->directory);
				g_print("{\"id\":\"%s\",\"name\":\"%s\","
				        "\"directory\":\"%s\"}\n", j_id, j_name, j_directory);
			} else
				g_print("%s\t%s\t%s\n", group->id, group->title,
				        group->directory != NULL ? group->directory : "-");
		}
		result = EXIT_SUCCESS;
		goto out;
	}
	if (strcmp(command, "delete-page") == 0) {
		if (delete_page_id == NULL || delete_page_id[0] == '\0') {
			g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			                    "delete-page requires --page ID");
			goto out;
		}
		if (!sakura_control_client_delete_page(connection, delete_page_id, &error))
			goto out;
		g_print("%s\n", delete_page_id);
		result = EXIT_SUCCESS;
		goto out;
	}
	if (strcmp(command, "input") == 0) {
		g_autofree gchar *prompt = NULL;
		g_autofree gchar *submitted = NULL;
		gsize prompt_length = 0;

		if (terminal_target == NULL || terminal_target[0] == '\0' ||
		    prompt_file == NULL || prompt_file[0] == '\0') {
			g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			                    "input requires --terminal and --prompt-file");
			goto out;
		}
		if (!g_file_get_contents(prompt_file, &prompt, &prompt_length, &error))
			goto out;
		submitted = g_strconcat(prompt, "\r", NULL);
		if (!sakura_control_client_terminal_input(
			connection, terminal_target, (const guint8 *)submitted,
			strlen(submitted), &error))
			goto out;
		g_usleep(500 * 1000);
		if (!sakura_control_client_terminal_input(
			connection, terminal_target, (const guint8 *)"\r", 1, &error))
			goto out;
		result = EXIT_SUCCESS;
		goto out;
	}
	if (group_id != NULL && group_name != NULL) {
		g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "--group and --group-name are mutually exclusive");
		goto out;
	}
	if (create_group_if_missing && (group_name == NULL || group_name[0] == '\0')) {
		g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "--create-group requires --group-name");
		goto out;
	}
	if (!codex && !goal && (model != NULL || reasoning != NULL || resume != NULL ||
	                        prompt_file != NULL || session_name != NULL)) {
		g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "--model, --reasoning, --resume, --prompt-file, and --session-name are only valid with codex");
		goto out;
	}
	if (goal && (model != NULL || reasoning != NULL || resume != NULL ||
	             prompt_file != NULL)) {
		g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "Codex launch options are not valid with goal commands");
		goto out;
	}
	if (!goal && goal_file != NULL) {
		g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "--file is only valid with goal start");
		goto out;
	}
	if (goal && strcmp(goal_action, "start") == 0 &&
	    (goal_file == NULL || goal_file[0] == '\0')) {
		g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "goal start requires --file");
		goto out;
	}
	if (goal && strcmp(goal_action, "start") != 0 && goal_file != NULL) {
		g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "--file is only valid with goal start");
		goto out;
	}
	if (goal && strcmp(goal_action, "status") != 0 &&
	    (title == NULL || title[0] == '\0') &&
	    (session_name == NULL || session_name[0] == '\0')) {
		g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "goal action requires --title or --session-name");
		goto out;
	}
	if (goal_file != NULL && !g_file_test(goal_file, G_FILE_TEST_IS_REGULAR)) {
		g_set_error(&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		            "goal file was not found: %s", goal_file);
		goto out;
	}
	if (apply && (manifest == NULL || manifest[0] == '\0')) {
		g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "codex apply requires --manifest");
		goto out;
	}
	if (!apply && manifest != NULL) {
		g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "--manifest is only valid with 'codex apply'");
		goto out;
	}
	if (dry_run && !apply) {
		g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "--dry-run is only valid with 'codex apply'");
		goto out;
	}
	if (dynamic_title && (title == NULL || title[0] == '\0')) {
		g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "--dynamic-title requires --title");
		goto out;
	}
	if (prompt_file != NULL && !g_file_test(prompt_file, G_FILE_TEST_IS_REGULAR)) {
		g_set_error(&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		            "prompt file was not found: %s", prompt_file);
		goto out;
	}
	launch_cwd = cwd != NULL && cwd[0] != '\0'
		? g_canonicalize_filename(cwd, NULL) : g_get_current_dir();
	if (apply && manifest != NULL && (cwd == NULL || cwd[0] == '\0')) {
		GPtrArray *manifest_entries = parse_manifest(manifest, &error);
		if (manifest_entries == NULL)
			goto out;
		g_free(launch_cwd);
		launch_cwd = g_canonicalize_filename(
			((SakuraCtlManifestEntry *)g_ptr_array_index(
				manifest_entries, 0))->working_directory, NULL);
		g_ptr_array_unref(manifest_entries);
	}
	if (group_name != NULL) {
		if (!request_snapshot(connection, &snapshot, &error))
			goto out;
		resolved_group = find_group(snapshot, group_name, &error);
		if (error != NULL)
			goto out;
		if (resolved_group == NULL && create_group_if_missing) {
			sakura_session_snapshot_free(snapshot);
			snapshot = NULL;
			if (!create_group(connection, group_name, launch_cwd, &snapshot, &error))
				goto out;
			resolved_group = find_group(snapshot, group_name, &error);
		}
		if (resolved_group == NULL) {
			g_set_error(&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			            "group '%s' was not found; use --create-group", group_name);
			goto out;
		}
		group_id = g_strdup(resolved_group->id);
	}
	if (goal && group_id != NULL && group_id[0] != '\0') {
		gboolean id_exists = FALSE;

		if (snapshot == NULL && !request_snapshot(connection, &snapshot, &error))
			goto out;
		for (guint index = 0; snapshot->groups != NULL &&
		                     index < snapshot->groups->len; index++) {
			SakuraSessionGroupRecord *candidate = g_ptr_array_index(
				snapshot->groups, index);

			if (candidate != NULL && g_strcmp0(candidate->id, group_id) == 0) {
				id_exists = TRUE;
				break;
			}
		}
		if (!id_exists) {
			SakuraSessionGroupRecord *named = find_group(snapshot, group_id, &error);

			if (error != NULL)
				goto out;
			if (named == NULL) {
				g_set_error(&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
				            "group '%s' was not found", group_id);
				goto out;
			}
			g_free(group_id);
			group_id = g_strdup(named->id);
		}
	}
	if (goal) {
		g_autoptr(GPtrArray) matches = NULL;

		if (snapshot == NULL && !request_snapshot(connection, &snapshot, &error))
			goto out;
		matches = find_goal_sessions(snapshot, group_id, title, session_name);
		if (matches->len == 0) {
			g_set_error(&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			            "no matching Codex session was found%s%s",
			            title != NULL ? ": " : "", title != NULL ? title : "");
			goto out;
		}
		if (strcmp(goal_action, "status") != 0 && matches->len != 1) {
			g_set_error(&error, G_IO_ERROR, G_IO_ERROR_FAILED,
			            "more than one Codex session matched '%s'",
			            session_name != NULL ? session_name : title);
			goto out;
		}
		for (guint index = 0; index < matches->len; index++) {
			SakuraSessionTabRecord *tab = g_ptr_array_index(matches, index);
			SakuraSessionPageRecord *page = snapshot_page_for_tab(snapshot, tab);
			g_autofree gchar *output = run_codex_goal(
				tab->codex_session_id, goal_action,
				strcmp(goal_action, "start") == 0 ? goal_file : NULL,
				page != NULL ? page->title : title, &error);

			if (output == NULL)
				goto out;
			g_print("%s\n", output);
		}
		result = EXIT_SUCCESS;
		goto out;
	}
	if ((columns < 0 || rows < 0) && snapshot == NULL &&
	    !request_snapshot(connection, &snapshot, &error))
		goto out;
	inherit_terminal_dimensions(snapshot, &columns, &rows);
	if (apply) {
		if (group_id == NULL || group_id[0] == '\0') {
			g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			                    "codex apply requires --group or --group-name");
			goto out;
		}
		if (!apply_manifest(connection, manifest, endpoint->workspace_id,
		                    group_id, group_name, print_format, dry_run, &error))
			goto out;
		result = EXIT_SUCCESS;
		goto out;
	}
	page_id = g_uuid_string_random();
	if (codex) {
		success = sakura_control_client_create_codex_terminal_with_identity(
			connection, NULL, page_id, group_id, task_id, launch_cwd,
			(guint)columns, (guint)rows, model, reasoning, resume, session_name,
			&terminal_id, &error);
	} else {
		success = sakura_control_client_create_terminal_with_page(
			connection, NULL, page_id, group_id, task_id, launch_cwd,
			(guint)columns, (guint)rows, &terminal_id, &error);
	}
	if (!success)
		goto out;
	created_resource = TRUE;
	if (title != NULL && !rename_page(connection, page_id, title,
	                                !dynamic_title, &error))
		goto out;
	if (prompt_file != NULL) {
		g_autofree gchar *prompt = NULL;
		gsize prompt_length = 0;
		g_autofree gchar *submitted = NULL;
		if (!codex) {
			g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			                    "--prompt-file is only valid with codex");
			goto out;
		}
		if (!g_file_get_contents(prompt_file, &prompt, &prompt_length, &error))
			goto out;
		/* Codex stages pasted multiline input on the first Enter.  The second
		 * Enter must arrive as a separate input event to submit it. */
		submitted = g_strconcat(prompt, "\r", NULL);
		if (!sakura_control_client_terminal_input(
			connection, terminal_id, (const guint8 *)submitted,
			strlen(submitted), &error))
			goto out;
		g_usleep(5 * G_USEC_PER_SEC);
		if (!sakura_control_client_terminal_input(
			connection, terminal_id, (const guint8 *)"\r", 1, &error))
			goto out;
	}
	if (codex) {
		codex_session_id = resume != NULL && resume[0] != '\0'
		                 ? g_strdup(resume)
		                 : wait_for_codex_session_id(connection, terminal_id,
		                                                   &error);
		if (codex_session_id == NULL)
			goto out;
	}
	if (session_name != NULL) {
		GError *name_error = NULL;

		if (!codex) {
			g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			                    "--session-name is only valid with codex");
			goto out;
		}
		if (!set_codex_session_name(codex_session_id, session_name, &name_error)) {
			g_printerr("sakura-ctl: warning: session was created but could not "
			           "be named: %s\n", name_error != NULL
			           ? name_error->message : "unknown error");
			g_clear_error(&name_error);
		}
	}
	if (print_format == NULL || strcmp(print_format, "id") == 0)
		g_print("%s\n", terminal_id);
	else {
		g_autofree gchar *j_workspace = json_escape(endpoint->workspace_id);
		g_autofree gchar *j_group = json_escape(group_id != NULL ? group_id : "root");
		g_autofree gchar *j_page = json_escape(page_id);
		g_autofree gchar *j_terminal = json_escape(terminal_id);
		g_autofree gchar *j_session = json_escape(codex_session_id);
		g_print("{\"workspace_id\":\"%s\",\"group_id\":\"%s\","
		        "\"page_id\":\"%s\",\"terminal_id\":\"%s\","
		        "\"codex_session_id\":\"%s\"}\n",
		        j_workspace, j_group, j_page, j_terminal, j_session);
	}
	result = EXIT_SUCCESS;
out:
	if (result != EXIT_SUCCESS && created_resource && connection != NULL &&
	    page_id != NULL) {
		GError *rollback_error = NULL;

		if (!sakura_control_client_delete_page(connection, page_id,
		                                      &rollback_error)) {
			g_printerr("sakura-ctl: warning: could not roll back page %s: %s\n",
			           page_id, rollback_error != NULL
			           ? rollback_error->message : "unknown error");
			g_clear_error(&rollback_error);
		}
	}
	if (error != NULL) {
		g_printerr("sakura-ctl: %s\n", error->message);
		g_clear_error(&error);
	}
	if (connection != NULL) {
		sakura_control_client_close(connection);
		sakura_control_client_unref(connection);
	}
	g_clear_pointer(&endpoints, g_ptr_array_unref);
	sakura_session_snapshot_free(snapshot);
	g_strfreev(arguments);
	g_free(workspace); g_free(session); g_free(socket_path); g_free(cwd);
	g_free(group_id); g_free(group_name); g_free(task_id); g_free(model);
	g_free(reasoning);
	g_free(resume); g_free(title); g_free(session_name); g_free(prompt_file);
	g_free(print_format); g_free(manifest);
	g_free(goal_file);
	g_free(delete_page_id);
	if (context != NULL)
		g_option_context_free(context);
	return result;
}
