#include <signal.h>

#include <gio/gio.h>
#include <glib/gstdio.h>

#include "sakura-control-transport.h"


static SakuraCoreWorkspace *
test_workspace_new(void)
{
	SakuraCoreWorkspace *workspace = sakura_core_workspace_new();
	SakuraCoreGroup *root = sakura_core_group_new("root", "All terminals", NULL);

	g_assert_true(sakura_core_workspace_set_root(workspace, root));
	workspace->active_group = root;
	return workspace;
}


static void
test_request_frame_roundtrip(void)
{
	GByteArray *encoded = g_byte_array_new();
	GMemoryOutputStream *memory_output;
	GMemoryInputStream *memory_input;
	GByteArray *decoded_frame = NULL;
	SakuraControlRequest request = { 0 };
	GError *error = NULL;
	const guint8 *wire_data;
	gsize wire_length;

	g_assert_true(sakura_control_encode_get_snapshot_request("request-1",
	                                                         encoded));
	memory_output = G_MEMORY_OUTPUT_STREAM(g_memory_output_stream_new_resizable());
	g_assert_true(sakura_control_frame_write(
		G_OUTPUT_STREAM(memory_output), encoded->data, encoded->len, NULL, &error));
	g_assert_no_error(error);
	wire_data = g_memory_output_stream_get_data(memory_output);
	wire_length = g_memory_output_stream_get_data_size(memory_output);
	memory_input = G_MEMORY_INPUT_STREAM(g_memory_input_stream_new_from_data(
		wire_data, wire_length, NULL));
	g_assert_true(sakura_control_frame_read(G_INPUT_STREAM(memory_input),
	                                       &decoded_frame, NULL, &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_decode_request(decoded_frame->data,
	                                           decoded_frame->len, &request,
	                                           &error));
	g_assert_no_error(error);
	g_assert_cmpstr(request.request_id, ==, "request-1");
	g_assert_cmpint(request.kind, ==, SAKURA_CONTROL_REQUEST_GET_SNAPSHOT);

	sakura_control_request_clear(&request);
	g_clear_pointer(&decoded_frame, g_byte_array_unref);
	g_object_unref(memory_input);
	g_object_unref(memory_output);
	g_byte_array_unref(encoded);
}


static void
test_snapshot_response_roundtrip(void)
{
	SakuraCoreWorkspace *workspace = test_workspace_new();
	GByteArray *encoded = g_byte_array_new();
	SakuraControlResponse response = { 0 };
	GError *error = NULL;

	g_assert_true(sakura_control_encode_snapshot_response("request-2", 0,
	                                                     workspace, encoded));
	g_assert_true(sakura_control_decode_response(encoded->data, encoded->len,
	                                            &response, &error));
	g_assert_no_error(error);
	g_assert_cmpstr(response.request_id, ==, "request-2");
	g_assert_true(response.has_snapshot);

	sakura_control_response_clear(&response);
	g_byte_array_unref(encoded);
	sakura_core_workspace_free(workspace);
}


static void
test_mutation_request_roundtrip(void)
{
	GByteArray *encoded = g_byte_array_new();
	SakuraControlRequest request = { 0 };
	GError *error = NULL;

	g_assert_true(sakura_control_encode_create_group_request(
		"group-request", "root", "Projects", "/tmp/projects", encoded));
	g_assert_true(sakura_control_decode_request(encoded->data, encoded->len,
	                                           &request, &error));
	g_assert_no_error(error);
	g_assert_cmpint(request.kind, ==, SAKURA_CONTROL_REQUEST_CREATE_GROUP);
	g_assert_cmpstr(request.request_id, ==, "group-request");
	g_assert_cmpstr(request.parent_id, ==, "root");
	g_assert_cmpstr(request.title, ==, "Projects");
	g_assert_cmpstr(request.directory, ==, "/tmp/projects");
	sakura_control_request_clear(&request);
	g_byte_array_set_size(encoded, 0);

	g_assert_true(sakura_control_encode_create_task_request(
		"task-request", "group-1", "", "Build", "local", "42", "",
		encoded));
	g_assert_true(sakura_control_decode_request(encoded->data, encoded->len,
	                                           &request, &error));
	g_assert_no_error(error);
	g_assert_cmpint(request.kind, ==, SAKURA_CONTROL_REQUEST_CREATE_TASK);
	g_assert_cmpstr(request.request_id, ==, "task-request");
	g_assert_cmpstr(request.group_id, ==, "group-1");
	g_assert_cmpstr(request.title, ==, "Build");
	g_assert_cmpstr(request.provider, ==, "local");
	g_assert_cmpstr(request.external_id, ==, "42");
	sakura_control_request_clear(&request);
	g_byte_array_unref(encoded);
}


static GSocketConnection *
test_agent_connect(const gchar *socket_path, GError **error)
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


static GSocketConnection *
test_agent_connect_wait(const gchar *socket_path)
{
	GSocketConnection *connection = NULL;

	for (guint attempt = 0; attempt < 100 && connection == NULL; attempt++) {
		GError *error = NULL;

		connection = test_agent_connect(socket_path, &error);
		if (connection == NULL)
			g_clear_error(&error);
		if (connection == NULL)
			g_usleep(10 * 1000);
	}
	g_assert_nonnull(connection);
	return connection;
}


static GSubprocess *
test_agent_start(const gchar *socket_path, const gchar *session_path)
{
	GSubprocess *process;
	GError *error = NULL;
	GSocketConnection *connection;

#ifdef SAKURA_AGENT_BUILD_PATH
	process = g_subprocess_new(
		G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
		&error, SAKURA_AGENT_BUILD_PATH, "--socket", socket_path,
		"--session", session_path, NULL);
#else
	process = g_subprocess_new(
		G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
		&error, "sakura-agent", "--socket", socket_path, "--session",
		session_path, NULL);
#endif
	g_assert_no_error(error);
	g_assert_nonnull(process);
	connection = test_agent_connect_wait(socket_path);
	g_io_stream_close(G_IO_STREAM(connection), NULL, NULL);
	g_object_unref(connection);
	return process;
}


static void
test_agent_stop(GSubprocess *process)
{
	GError *error = NULL;

	g_subprocess_send_signal(process, SIGTERM);
	g_assert_true(g_subprocess_wait(process, NULL, &error));
	g_assert_no_error(error);
	g_object_unref(process);
}


static void
test_agent_call(const gchar *socket_path, const gchar *request_id,
	              GByteArray *request_payload, SakuraControlResponse *response)
{
	GSocketConnection *connection;
	GInputStream *input;
	GOutputStream *output;
	GByteArray *response_payload = NULL;
	GError *error = NULL;

	connection = test_agent_connect_wait(socket_path);
	input = g_io_stream_get_input_stream(G_IO_STREAM(connection));
	output = g_io_stream_get_output_stream(G_IO_STREAM(connection));
	g_assert_true(sakura_control_frame_write(output, request_payload->data,
	                                         request_payload->len, NULL,
	                                         &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_frame_read(input, &response_payload, NULL,
	                                        &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_decode_response(response_payload->data,
	                                             response_payload->len, response,
	                                             &error));
	g_assert_no_error(error);
	g_assert_cmpstr(response->request_id, ==, request_id);
	g_assert_true(response->has_snapshot);
	g_clear_pointer(&response_payload, g_byte_array_unref);
	g_io_stream_close(G_IO_STREAM(connection), NULL, NULL);
	g_object_unref(connection);
}


static SakuraSessionSnapshot *
test_load_agent_session(const gchar *session_path)
{
	GKeyFile *key_file;
	SakuraSessionSnapshot *snapshot;
	GError *error = NULL;

	key_file = g_key_file_new();
	g_assert_true(g_key_file_load_from_file(key_file, session_path, 0, &error));
	g_assert_no_error(error);
	snapshot = sakura_session_snapshot_new();
	g_assert_true(sakura_session_snapshot_load(key_file, snapshot, &error));
	g_assert_no_error(error);
	g_key_file_free(key_file);
	return snapshot;
}


static void
test_agent_create_and_reload(void)
{
	GSubprocess *process;
	GByteArray *request = g_byte_array_new();
	SakuraControlResponse response = { 0 };
	SakuraSessionSnapshot *snapshot;
	SakuraSessionGroupRecord *group;
	SakuraSessionTaskRecord *task;
	GError *error = NULL;
	GSocketConnection *subscriber;
	GInputStream *subscriber_input;
	GOutputStream *subscriber_output;
	GByteArray *event_payload = NULL;
	SakuraSessionSnapshot *event_snapshot = NULL;
	guint64 event_sequence;
	gchar *directory;
	gchar *socket_path;
	gchar *session_path;
	gchar *group_id;

	directory = g_dir_make_tmp("sakura-agent-test-XXXXXX", &error);
	g_assert_no_error(error);
	g_assert_nonnull(directory);
	socket_path = g_build_filename(directory, "agent.sock", NULL);
	session_path = g_build_filename(directory, "workspace.session", NULL);
	process = test_agent_start(socket_path, session_path);

	g_assert_true(sakura_control_encode_create_group_request(
		"create-group", "root", "Projects", "/tmp/projects", request));
	test_agent_call(socket_path, "create-group", request, &response);
	sakura_control_response_clear(&response);

	snapshot = test_load_agent_session(session_path);
	g_assert_cmpuint(snapshot->groups->len, ==, 1);
	group = g_ptr_array_index(snapshot->groups, 0);
	g_assert_cmpstr(group->title, ==, "Projects");
	group_id = g_strdup(group->id);
	sakura_session_snapshot_free(snapshot);
	g_byte_array_set_size(request, 0);

	g_assert_true(sakura_control_encode_create_task_request(
		"create-task", group_id, "", "Build", "local", "42", "", request));
	test_agent_call(socket_path, "create-task", request, &response);
	sakura_control_response_clear(&response);

	snapshot = test_load_agent_session(session_path);
	g_assert_cmpuint(snapshot->groups->len, ==, 1);
	g_assert_cmpuint(snapshot->tasks->len, ==, 1);
	task = g_ptr_array_index(snapshot->tasks, 0);
	g_assert_cmpstr(task->title, ==, "Build");
	g_assert_cmpstr(task->group_id, ==, group_id);
	sakura_session_snapshot_free(snapshot);
	test_agent_stop(process);

	process = test_agent_start(socket_path, session_path);
	g_byte_array_set_size(request, 0);
	g_assert_true(sakura_control_encode_get_snapshot_request("reload", request));
	test_agent_call(socket_path, "reload", request, &response);
	sakura_control_response_clear(&response);

	subscriber = test_agent_connect_wait(socket_path);
	subscriber_input = g_io_stream_get_input_stream(G_IO_STREAM(subscriber));
	subscriber_output = g_io_stream_get_output_stream(G_IO_STREAM(subscriber));
	g_byte_array_set_size(request, 0);
	g_assert_true(sakura_control_encode_subscribe_events_request(
		"subscribe", 0, request));
	g_assert_true(sakura_control_frame_write(subscriber_output, request->data,
	                                         request->len, NULL, &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_frame_read(subscriber_input, &event_payload,
	                                        NULL, &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_decode_response(event_payload->data,
	                                             event_payload->len, &response,
	                                             &error));
	g_assert_no_error(error);
	g_assert_true(response.accepted);
	sakura_control_response_clear(&response);
	g_clear_pointer(&event_payload, g_byte_array_unref);
	g_assert_true(sakura_control_frame_read(subscriber_input, &event_payload,
	                                        NULL, &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_decode_workspace_changed_event(
		event_payload->data, event_payload->len, &event_sequence,
		&event_snapshot, &error));
	g_assert_no_error(error);
	g_assert_cmpuint(event_sequence, ==, 0);
	g_assert_cmpuint(event_snapshot->groups->len, ==, 1);
	g_assert_cmpuint(event_snapshot->tasks->len, ==, 1);
	sakura_session_snapshot_free(event_snapshot);
	event_snapshot = NULL;
	g_clear_pointer(&event_payload, g_byte_array_unref);

	g_byte_array_set_size(request, 0);
	g_assert_true(sakura_control_encode_create_group_request(
		"second-group", "root", "Archive", "", request));
	test_agent_call(socket_path, "second-group", request, &response);
	sakura_control_response_clear(&response);
	g_assert_true(sakura_control_frame_read(subscriber_input, &event_payload,
	                                        NULL, &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_decode_workspace_changed_event(
		event_payload->data, event_payload->len, &event_sequence,
		&event_snapshot, &error));
	g_assert_no_error(error);
	g_assert_cmpuint(event_sequence, ==, 1);
	g_assert_cmpuint(event_snapshot->groups->len, ==, 2);
	sakura_session_snapshot_free(event_snapshot);
	g_clear_pointer(&event_payload, g_byte_array_unref);
	g_io_stream_close(G_IO_STREAM(subscriber), NULL, NULL);
	g_object_unref(subscriber);
	test_agent_stop(process);

	g_byte_array_unref(request);
	g_free(group_id);
	g_remove(session_path);
	g_remove(socket_path);
	g_rmdir(directory);
	g_free(session_path);
	g_free(socket_path);
	g_free(directory);
}


int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/control/request-frame-roundtrip",
	                test_request_frame_roundtrip);
	g_test_add_func("/control/snapshot-response-roundtrip",
	                test_snapshot_response_roundtrip);
	g_test_add_func("/control/mutation-request-roundtrip",
	                test_mutation_request_roundtrip);
	g_test_add_func("/control/agent-create-and-reload",
	                test_agent_create_and_reload);
	return g_test_run();
}
