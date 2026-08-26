#include <stdlib.h>
#include <string.h>

#include "sakura-control-client.h"
#include "sakura-control-discovery.h"

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
			g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
			                    "more than one workspace matched; use --workspace with a unique ID prefix");
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
	gchar *cwd = NULL, *group_id = NULL, *task_id = NULL;
	gchar *reasoning = NULL, *resume = NULL, **arguments = NULL;
	gint columns = 80, rows = 24;
	GOptionContext *context = NULL;
	GError *error = NULL;
	GPtrArray *endpoints = NULL;
	SakuraControlWorkspaceEndpoint direct = { 0 }, *endpoint = NULL;
	SakuraControlClientConnection *connection = NULL;
	g_autofree gchar *page_id = NULL, *terminal_id = NULL, *launch_cwd = NULL;
	const gchar *command;
	gboolean codex, success;
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
		{ "task", 't', 0, G_OPTION_ARG_STRING, &task_id,
		  "Associate the page with this task", "ID" },
		{ "columns", 'c', 0, G_OPTION_ARG_INT, &columns,
		  "Initial terminal width", "COUNT" },
		{ "rows", 'r', 0, G_OPTION_ARG_INT, &rows,
		  "Initial terminal height", "COUNT" },
		{ "reasoning", 0, 0, G_OPTION_ARG_STRING, &reasoning,
		  "Codex reasoning effort", "EFFORT" },
		{ "resume", 0, 0, G_OPTION_ARG_STRING, &resume,
		  "Resume a Codex session by ID", "SESSION" },
		{ G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &arguments,
		  NULL, "COMMAND" },
		{ NULL }
	};

	context = g_option_context_new("- control a running Sakura workspace");
	g_option_context_set_summary(context,
		"Commands:\n  list   List running workspaces\n  new    Open a shell page\n  codex  Open a Codex page");
	g_option_context_add_main_entries(context, entries, NULL);
	if (!g_option_context_parse(context, &argc, &argv, &error))
		goto out;
	if (arguments == NULL || arguments[0] == NULL || arguments[1] != NULL) {
		g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "exactly one command is required: list, new, or codex");
		goto out;
	}
	command = arguments[0];
	if (strcmp(command, "list") != 0 && strcmp(command, "new") != 0 &&
	    strcmp(command, "codex") != 0) {
		g_set_error(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		            "unknown command: %s", command);
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
		SAKURA_CONTROL_CAPABILITY_TERMINALS |
		(codex ? SAKURA_CONTROL_CAPABILITY_CODEX : 0), &error);
	if (connection == NULL)
		goto out;
	launch_cwd = cwd != NULL && cwd[0] != '\0'
		? g_canonicalize_filename(cwd, NULL) : g_get_current_dir();
	page_id = g_uuid_string_random();
	if (codex) {
		success = sakura_control_client_create_codex_terminal(
			connection, NULL, page_id, group_id, task_id, launch_cwd,
			(guint)columns, (guint)rows, reasoning, resume, &terminal_id, &error);
	} else {
		if (reasoning != NULL || resume != NULL) {
			g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			                    "--reasoning and --resume are only valid with codex");
			goto out;
		}
		success = sakura_control_client_create_terminal_with_page(
			connection, NULL, page_id, group_id, task_id, launch_cwd,
			(guint)columns, (guint)rows, &terminal_id, &error);
	}
	if (!success)
		goto out;
	g_print("%s\n", terminal_id);
	result = EXIT_SUCCESS;
out:
	if (error != NULL) {
		g_printerr("sakura-ctl: %s\n", error->message);
		g_clear_error(&error);
	}
	if (connection != NULL) {
		sakura_control_client_close(connection);
		sakura_control_client_unref(connection);
	}
	g_clear_pointer(&endpoints, g_ptr_array_unref);
	g_strfreev(arguments);
	g_free(workspace); g_free(session); g_free(socket_path); g_free(cwd);
	g_free(group_id); g_free(task_id); g_free(reasoning); g_free(resume);
	if (context != NULL)
		g_option_context_free(context);
	return result;
}
