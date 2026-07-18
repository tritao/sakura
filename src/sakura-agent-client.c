#include <errno.h>
#include <unistd.h>

#include "sakura-private.h"
#include "sakura-control-transport.h"


static gchar *
sakura_agent_socket_path_new(void)
{
	const gchar *runtime_dir = g_get_user_runtime_dir();

	if (runtime_dir != NULL && runtime_dir[0] != '\0')
		return g_build_filename(runtime_dir, "sakura-agent.sock", NULL);
	return g_build_filename(g_get_user_cache_dir(), "sakura", "agent.sock",
	                        NULL);
}


static GSocketConnection *
sakura_agent_connect(const gchar *socket_path, GError **error)
{
	GSocketClient *client;
	GSocketAddress *address;
	GSocketConnection *connection;

	client = g_socket_client_new();
	address = g_unix_socket_address_new(socket_path);
	connection = g_socket_client_connect(client,
	                                     G_SOCKET_CONNECTABLE(address),
	                                     NULL, error);
	g_object_unref(address);
	g_object_unref(client);
	return connection;
}


static gboolean
sakura_agent_request_snapshot(const gchar *socket_path, GError **error)
{
	GSocketConnection *connection;
	GInputStream *input;
	GOutputStream *output;
	GByteArray *request;
	GByteArray *response_payload = NULL;
	SakuraControlResponse response = { 0 };
	gchar *request_id;
	gboolean success = FALSE;

	connection = sakura_agent_connect(socket_path, error);
	if (connection == NULL)
		return FALSE;
	request_id = g_uuid_string_random();
	request = g_byte_array_new();
	sakura_control_encode_get_snapshot_request(request_id, request);
	input = g_io_stream_get_input_stream(G_IO_STREAM(connection));
	output = g_io_stream_get_output_stream(G_IO_STREAM(connection));
	if (!sakura_control_frame_write(output, request->data, request->len, NULL,
	                               error) ||
	    !sakura_control_frame_read(input, &response_payload, NULL, error) ||
	    !sakura_control_decode_response(response_payload->data,
	                                    response_payload->len, &response,
	                                    error))
		goto out;
	if (g_strcmp0(response.request_id, request_id) != 0) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                    "agent response id did not match request");
		goto out;
	}
	success = TRUE;
out:
	g_clear_pointer(&response_payload, g_byte_array_unref);
	g_byte_array_unref(request);
	sakura_control_response_clear(&response);
	g_free(request_id);
	g_io_stream_close(G_IO_STREAM(connection), NULL, NULL);
	g_object_unref(connection);
	return success;
}


static gchar *
sakura_agent_binary_path(void)
{
	gchar *path = NULL;

#ifdef SAKURA_AGENT_BUILD_PATH
	if (g_file_test(SAKURA_AGENT_BUILD_PATH, G_FILE_TEST_IS_EXECUTABLE))
		return g_strdup(SAKURA_AGENT_BUILD_PATH);
#endif
	path = g_find_program_in_path("sakura-agent");
	return path;
}


gboolean
sakura_agent_start(SakuraApp *app)
{
	GError *error = NULL;
	gchar *socket_path;
	gchar *binary;
	GSubprocess *process;

	if (app == NULL)
		return FALSE;
	if (app->agent_socket_path != NULL)
		return TRUE;
	socket_path = sakura_agent_socket_path_new();
	if (sakura_agent_request_snapshot(socket_path, &error)) {
		app->agent_socket_path = socket_path;
		return TRUE;
	}
	g_clear_error(&error);

	binary = sakura_agent_binary_path();
	if (binary == NULL) {
		g_warning("Could not find sakura-agent; continuing without the local agent");
		g_free(socket_path);
		return FALSE;
	}
	process = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
	                           G_SUBPROCESS_FLAGS_STDERR_SILENCE,
	                           &error, binary, "--socket", socket_path, NULL);
	g_free(binary);
	if (process == NULL) {
		g_warning("Could not start sakura-agent: %s",
		          error != NULL ? error->message : "unknown error");
		g_clear_error(&error);
		g_free(socket_path);
		return FALSE;
	}

	/* Startup is intentionally synchronous and short: the UI must not restore
	 * a workspace until the agent endpoint has completed its handshake. */
	for (guint attempt = 0; attempt < 50; attempt++) {
		if (sakura_agent_request_snapshot(socket_path, &error)) {
			app->agent_process = process;
			app->agent_socket_path = socket_path;
			return TRUE;
		}
		g_clear_error(&error);
		g_usleep(10 * 1000);
	}
	g_warning("sakura-agent did not become ready");
	g_subprocess_force_exit(process);
	g_subprocess_wait(process, NULL, NULL);
	g_object_unref(process);
	g_free(socket_path);
	return FALSE;
}


void
sakura_agent_stop(SakuraApp *app)
{
	if (app == NULL)
		return;
	/* Desktop mode currently supervises the child through GSubprocess. A future
	 * systemd user-service mode can omit this cleanup and let the same endpoint
	 * outlive the GTK process. */
	g_clear_object(&app->agent_process);
	g_clear_pointer(&app->agent_socket_path, g_free);
}
