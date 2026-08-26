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
            const gchar *title, GError **error)
{
	g_autoptr(GByteArray) request = g_byte_array_new();
	g_autofree gchar *request_id = g_uuid_string_random();
	SakuraSessionSnapshot *snapshot = NULL;
	gboolean success;

	success = sakura_control_encode_rename_page_request(
		request_id, page_id, title, TRUE, request) &&
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

static SakuraSessionTabRecord *
find_manifest_match(SakuraSessionSnapshot *snapshot, const gchar *group_id,
                    const SakuraCtlManifestEntry *entry)
{
	g_autofree gchar *wanted_cwd = g_canonicalize_filename(
		entry->working_directory, NULL);

	for (guint i = 0; snapshot != NULL && snapshot->pages != NULL &&
	                 i < snapshot->pages->len; i++) {
		SakuraSessionPageRecord *page = g_ptr_array_index(snapshot->pages, i);

		if (page == NULL || g_strcmp0(page->group_id, group_id) != 0 ||
		    g_strcmp0(page->title, entry->title) != 0)
			continue;
		for (guint j = 0; snapshot->tabs != NULL && j < snapshot->tabs->len; j++) {
			SakuraSessionTabRecord *tab = g_ptr_array_index(snapshot->tabs, j);
			g_autofree gchar *actual_cwd = tab != NULL && tab->cwd != NULL
			                             ? g_canonicalize_filename(tab->cwd, NULL)
			                             : NULL;
			if (tab != NULL && tab->kind == SAKURA_TAB_CODEX &&
			    g_strcmp0(tab->page_id, page->id) == 0 &&
			    g_strcmp0(actual_cwd, wanted_cwd) == 0 &&
			    tab->runtime_status == SAKURA_TERMINAL_RUNNING &&
			    tab->codex_session_id != NULL &&
			    tab->codex_session_id[0] != '\0' &&
			    g_strcmp0(tab->codex_model, entry->model) == 0 &&
			    g_strcmp0(tab->codex_reasoning_effort, entry->reasoning) == 0)
				return tab;
		}
	}
	return NULL;
}

static SakuraSessionPageRecord *
find_manifest_page(SakuraSessionSnapshot *snapshot, const gchar *group_id,
	               const SakuraCtlManifestEntry *entry)
{
	for (guint index = 0; snapshot != NULL && snapshot->pages != NULL &&
	                     index < snapshot->pages->len; index++) {
		SakuraSessionPageRecord *page = g_ptr_array_index(snapshot->pages, index);

		if (page != NULL && g_strcmp0(page->group_id, group_id) == 0 &&
		    g_strcmp0(page->title, entry->title) == 0)
			return page;
	}
	return NULL;
}

static gboolean
apply_manifest(SakuraControlClientConnection *connection,
               const gchar *manifest_path, const gchar *workspace_id,
               const gchar *group_id, const gchar *group_name,
               const gchar *print_format, GError **error)
{
	g_autoptr(GPtrArray) entries = parse_manifest(manifest_path, error);
	g_autofree gchar *self = g_file_read_link("/proc/self/exe", error);

	if (entries == NULL || self == NULL)
		return FALSE;
	for (guint i = 0; i < entries->len; i++) {
		SakuraCtlManifestEntry *entry = g_ptr_array_index(entries, i);
		SakuraSessionSnapshot *snapshot = NULL;
		SakuraSessionTabRecord *match;
		SakuraSessionPageRecord *stale_page;
		GPtrArray *args;
		g_autofree gchar *stdout_text = NULL;
		g_autofree gchar *stderr_text = NULL;
		gint wait_status = 0;

		if (!request_snapshot(connection, &snapshot, error))
			return FALSE;
		match = find_manifest_match(snapshot, group_id, entry);
		if (match != NULL) {
			if (print_format != NULL && strcmp(print_format, "json") == 0) {
				g_autofree gchar *j_title = json_escape(entry->title);
				g_autofree gchar *j_terminal = json_escape(match->terminal_id);
				g_print("{\"status\":\"reused\",\"title\":\"%s\","
				        "\"terminal_id\":\"%s\"}\n", j_title, j_terminal);
			} else
				g_print("reused\t%s\t%s\n", entry->title, match->terminal_id);
			sakura_session_snapshot_free(snapshot);
			continue;
		}
		stale_page = find_manifest_page(snapshot, group_id, entry);
		if (stale_page != NULL &&
		    !sakura_control_client_delete_page(connection, stale_page->id, error)) {
			sakura_session_snapshot_free(snapshot);
			return FALSE;
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
		g_ptr_array_add(args, g_strdup("--working-directory"));
		g_ptr_array_add(args, g_strdup(entry->working_directory));
		if (entry->reasoning != NULL && entry->reasoning[0] != '\0') {
			g_ptr_array_add(args, g_strdup("--reasoning"));
			g_ptr_array_add(args, g_strdup(entry->reasoning));
		}
		if (entry->model != NULL && entry->model[0] != '\0') {
			g_ptr_array_add(args, g_strdup("--model"));
			g_ptr_array_add(args, g_strdup(entry->model));
		}
		if (entry->prompt_file != NULL && entry->prompt_file[0] != '\0') {
			g_ptr_array_add(args, g_strdup("--prompt-file"));
			g_ptr_array_add(args, g_strdup(entry->prompt_file));
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
	}
	(void)group_name;
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
	gchar *manifest = NULL, *delete_page_id = NULL;
	gchar **arguments = NULL;
	gint columns = 80, rows = 24;
	GOptionContext *context = NULL;
	GError *error = NULL;
	GPtrArray *endpoints = NULL;
	SakuraControlWorkspaceEndpoint direct = { 0 }, *endpoint = NULL;
	SakuraControlClientConnection *connection = NULL;
	g_autofree gchar *page_id = NULL, *terminal_id = NULL, *launch_cwd = NULL;
	g_autofree gchar *codex_session_id = NULL;
	SakuraSessionSnapshot *snapshot = NULL;
	SakuraSessionGroupRecord *resolved_group = NULL;
	const gchar *command;
	gboolean codex, success, apply = FALSE, create_group_if_missing = FALSE;
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
		{ "session-name", 0, 0, G_OPTION_ARG_STRING, &session_name,
		  "Set the Codex session name once its ID is available", "NAME" },
		{ "prompt-file", 0, 0, G_OPTION_ARG_FILENAME, &prompt_file,
		  "Send this file as the initial Codex request", "PATH" },
		{ "print", 0, 0, G_OPTION_ARG_STRING, &print_format,
		  "Output format: id or json", "FORMAT" },
		{ "manifest", 0, 0, G_OPTION_ARG_FILENAME, &manifest,
		  "Session manifest for 'codex apply'", "PATH" },
		{ "page", 0, 0, G_OPTION_ARG_STRING, &delete_page_id,
		  "Page ID for 'delete-page'", "ID" },
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
		"  delete-page  Remove a page and its terminals");
	g_option_context_add_main_entries(context, entries, NULL);
	if (!g_option_context_parse(context, &argc, &argv, &error))
		goto out;
	if (arguments == NULL || g_strv_length(arguments) < 1 ||
	    g_strv_length(arguments) > 2 ||
	    (g_strv_length(arguments) == 2 &&
	     (strcmp(arguments[0], "codex") != 0 ||
	      strcmp(arguments[1], "apply") != 0))) {
		g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "exactly one command is required: list, groups, new, codex, or delete-page");
		goto out;
	}
	command = arguments[0];
	apply = g_strv_length(arguments) == 2;
	if (strcmp(command, "list") != 0 && strcmp(command, "groups") != 0 &&
	    strcmp(command, "new") != 0 &&
	    strcmp(command, "codex") != 0 && strcmp(command, "delete-page") != 0) {
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
	if (columns <= 0 || columns > 65535 || rows <= 0 || rows > 65535) {
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
		(codex ? SAKURA_CONTROL_CAPABILITY_CODEX : 0) |
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
	if (!codex && (model != NULL || reasoning != NULL || resume != NULL || prompt_file != NULL ||
	               session_name != NULL)) {
		g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "--model, --reasoning, --resume, --prompt-file, and --session-name are only valid with codex");
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
	if (apply) {
		if (group_id == NULL || group_id[0] == '\0') {
			g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			                    "codex apply requires --group or --group-name");
			goto out;
		}
		if (!apply_manifest(connection, manifest, endpoint->workspace_id,
		                    group_id, group_name, print_format, &error))
			goto out;
		result = EXIT_SUCCESS;
		goto out;
	}
	page_id = g_uuid_string_random();
	if (codex) {
		success = sakura_control_client_create_codex_terminal_with_model(
			connection, NULL, page_id, group_id, task_id, launch_cwd,
			(guint)columns, (guint)rows, model, reasoning, resume, &terminal_id,
			&error);
	} else {
		success = sakura_control_client_create_terminal_with_page(
			connection, NULL, page_id, group_id, task_id, launch_cwd,
			(guint)columns, (guint)rows, &terminal_id, &error);
	}
	if (!success)
		goto out;
	created_resource = TRUE;
	if (title != NULL && !rename_page(connection, page_id, title, &error))
		goto out;
	if (codex) {
		codex_session_id = resume != NULL && resume[0] != '\0'
		                 ? g_strdup(resume)
		                 : wait_for_codex_session_id(connection, terminal_id,
		                                                   &error);
		if (codex_session_id == NULL)
			goto out;
	}
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
		submitted = g_strconcat(prompt, "\r", NULL);
		if (!sakura_control_client_terminal_input(
			connection, terminal_id, (const guint8 *)submitted,
			strlen(submitted), &error))
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
	g_free(delete_page_id);
	if (context != NULL)
		g_option_context_free(context);
	return result;
}
