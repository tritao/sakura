#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <gio/gio.h>
#include <glib-unix.h>

#include "sakura-control-transport.h"


typedef struct {
	GSocketConnection *connection;
	SakuraCoreWorkspace *workspace;
} SakuraAgentConnection;

typedef struct {
	GMainLoop *loop;
	SakuraCoreWorkspace *workspace;
	gchar *socket_path;
} SakuraAgent;


static gpointer
sakura_agent_connection_thread(gpointer data)
{
	SakuraAgentConnection *request = data;
	GInputStream *input;
	GOutputStream *output;
	GByteArray *payload = NULL;
	GByteArray *response = NULL;
	SakuraControlRequest decoded = { 0 };
	GError *error = NULL;

	input = g_io_stream_get_input_stream(G_IO_STREAM(request->connection));
	output = g_io_stream_get_output_stream(G_IO_STREAM(request->connection));
	if (sakura_control_frame_read(input, &payload, NULL, &error) &&
	    sakura_control_decode_request(payload->data, payload->len, &decoded,
	                                   &error)) {
		response = g_byte_array_new();
		if (decoded.get_snapshot)
			sakura_control_encode_snapshot_response(decoded.request_id,
			                                       request->workspace, response);
	} else {
		const gchar *message = error != NULL ? error->message : "invalid request";

		response = g_byte_array_new();
		sakura_control_encode_error_response(
			decoded.request_id != NULL ? decoded.request_id : "unknown",
			"invalid_request", message, response);
	}
	if (response != NULL && response->len != 0)
		sakura_control_frame_write(output, response->data, response->len,
		                           NULL, NULL);
	g_clear_error(&error);
	g_clear_pointer(&payload, g_byte_array_unref);
	g_clear_pointer(&response, g_byte_array_unref);
	sakura_control_request_clear(&decoded);
	g_io_stream_close(G_IO_STREAM(request->connection), NULL, NULL);
	g_object_unref(request->connection);
	g_free(request);
	return NULL;
}


static gboolean
sakura_agent_incoming_cb(GSocketService *service,
	                     GSocketConnection *connection,
	                     GObject *source_object,
	                     gpointer user_data)
{
	SakuraAgent *agent = user_data;
	SakuraAgentConnection *request;

	(void)service;
	(void)source_object;
	request = g_new0(SakuraAgentConnection, 1);
	request->connection = g_object_ref(connection);
	request->workspace = agent->workspace;
	g_thread_unref(g_thread_new("sakura-agent-client",
	                            sakura_agent_connection_thread, request));
	return TRUE;
}


static gboolean
sakura_agent_quit_cb(gpointer data)
{
	GMainLoop *loop = data;

	g_main_loop_quit(loop);
	return G_SOURCE_REMOVE;
}


static gchar *
sakura_agent_default_socket_path(void)
{
	const gchar *runtime_dir = g_get_user_runtime_dir();

	if (runtime_dir != NULL && runtime_dir[0] != '\0')
		return g_build_filename(runtime_dir, "sakura-agent.sock", NULL);
	return g_build_filename(g_get_user_cache_dir(), "sakura", "agent.sock",
	                        NULL);
}


static gboolean
sakura_agent_prepare_socket_path(const gchar *socket_path, GError **error)
{
	gchar *directory;
	gboolean directory_exists;

	directory = g_path_get_dirname(socket_path);
	directory_exists = g_file_test(directory, G_FILE_TEST_IS_DIR);
	if (g_mkdir_with_parents(directory, 0700) != 0) {
		g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
		            "Could not create agent socket directory %s: %s", directory,
		            g_strerror(errno));
		g_free(directory);
		return FALSE;
	}
	if (!directory_exists && chmod(directory, 0700) != 0) {
		g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
		            "Could not secure agent socket directory %s: %s", directory,
		            g_strerror(errno));
		g_free(directory);
		return FALSE;
	}
	g_free(directory);
	/* The client probes an existing endpoint before starting us. At this
	 * point an existing path is therefore stale or owned by a process that
	 * disappeared between the probe and bind. */
	g_remove(socket_path);
	return TRUE;
}


int
main(int argc, char **argv)
{
	GSocketService *service;
	GSocketAddress *address;
	GMainLoop *loop;
	SakuraCoreWorkspace *workspace;
	SakuraCoreGroup *root;
	SakuraAgent agent = { 0 };
	GError *error = NULL;
	gchar *socket_path = NULL;
	GOptionContext *context;
	GOptionEntry entries[] = {
		{ "socket", 's', 0, G_OPTION_ARG_STRING, &socket_path,
		  "Unix socket path", "PATH" },
		{ NULL }
	};

	context = g_option_context_new("- Sakura local control agent");
	g_option_context_add_main_entries(context, entries, NULL);
	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		g_printerr("%s\n", error->message);
		g_clear_error(&error);
		g_option_context_free(context);
		return EXIT_FAILURE;
	}
	g_option_context_free(context);
	if (socket_path == NULL)
		socket_path = sakura_agent_default_socket_path();
	if (!sakura_agent_prepare_socket_path(socket_path, &error)) {
		g_printerr("%s\n", error->message);
		g_clear_error(&error);
		g_free(socket_path);
		return EXIT_FAILURE;
	}

	signal(SIGPIPE, SIG_IGN);
	workspace = sakura_core_workspace_new();
	root = sakura_core_group_new("root", "All terminals", NULL);
	if (!sakura_core_workspace_set_root(workspace, root)) {
		g_printerr("Could not initialize agent workspace\n");
		sakura_core_group_free(root);
		sakura_core_workspace_free(workspace);
		g_free(socket_path);
		return EXIT_FAILURE;
	}
	workspace->active_group = workspace->root_group;

	service = g_socket_service_new();
	address = g_unix_socket_address_new(socket_path);
	if (!g_socket_listener_add_address(G_SOCKET_LISTENER(service), address,
	                                   G_SOCKET_TYPE_STREAM,
	                                   G_SOCKET_PROTOCOL_DEFAULT, NULL, NULL,
	                                   &error)) {
		g_printerr("Could not bind agent socket %s: %s\n", socket_path,
		            error->message);
		g_clear_error(&error);
		g_object_unref(address);
		g_object_unref(service);
		sakura_core_workspace_free(workspace);
		g_remove(socket_path);
		g_free(socket_path);
		return EXIT_FAILURE;
	}
	g_object_unref(address);
	if (chmod(socket_path, 0600) != 0)
		g_printerr("Could not secure agent socket %s: %s\n", socket_path,
		            g_strerror(errno));

	loop = g_main_loop_new(NULL, FALSE);
	agent.loop = loop;
	agent.workspace = workspace;
	agent.socket_path = socket_path;
	g_signal_connect(service, "incoming", G_CALLBACK(sakura_agent_incoming_cb),
	                 &agent);
	g_unix_signal_add(SIGINT, sakura_agent_quit_cb, loop);
	g_unix_signal_add(SIGTERM, sakura_agent_quit_cb, loop);
	g_socket_service_start(service);
	g_main_loop_run(loop);
	g_socket_service_stop(service);
	g_main_loop_unref(loop);
	g_object_unref(service);
	sakura_core_workspace_free(workspace);
	g_remove(socket_path);
	g_free(socket_path);
	return EXIT_SUCCESS;
}
