#include <signal.h>
#include <string.h>

#include <gio/gio.h>
#include <glib/gstdio.h>

#include "sakura-control-transport.h"
#include "sakura/control.pb-c.h"


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

	g_assert_true(sakura_control_encode_update_group_request(
		"update-group", "group-1", "Renamed", "/tmp/renamed", encoded));
	g_assert_true(sakura_control_decode_request(encoded->data, encoded->len,
	                                           &request, &error));
	g_assert_no_error(error);
	g_assert_cmpint(request.kind, ==, SAKURA_CONTROL_REQUEST_UPDATE_GROUP);
	g_assert_cmpstr(request.group_id, ==, "group-1");
	g_assert_cmpstr(request.title, ==, "Renamed");
	g_assert_cmpstr(request.directory, ==, "/tmp/renamed");
	sakura_control_request_clear(&request);
	g_byte_array_set_size(encoded, 0);

	g_assert_true(sakura_control_encode_set_group_archived_request(
		"archive-group", "group-1", TRUE, encoded));
	g_assert_true(sakura_control_decode_request(encoded->data, encoded->len,
	                                           &request, &error));
	g_assert_no_error(error);
	g_assert_cmpint(request.kind, ==,
	               SAKURA_CONTROL_REQUEST_SET_GROUP_ARCHIVED);
	g_assert_cmpstr(request.group_id, ==, "group-1");
	g_assert_true(request.archived);
	sakura_control_request_clear(&request);
	g_byte_array_set_size(encoded, 0);

	g_assert_true(sakura_control_encode_delete_group_request(
		"delete-group", "group-1", encoded));
	g_assert_true(sakura_control_decode_request(encoded->data, encoded->len,
	                                           &request, &error));
	g_assert_no_error(error);
	g_assert_cmpint(request.kind, ==, SAKURA_CONTROL_REQUEST_DELETE_GROUP);
	g_assert_cmpstr(request.group_id, ==, "group-1");
	sakura_control_request_clear(&request);
	g_byte_array_set_size(encoded, 0);

	g_assert_true(sakura_control_encode_update_task_request(
		"update-task", "task-1", "Renamed task", encoded));
	g_assert_true(sakura_control_decode_request(encoded->data, encoded->len,
	                                           &request, &error));
	g_assert_no_error(error);
	g_assert_cmpint(request.kind, ==, SAKURA_CONTROL_REQUEST_UPDATE_TASK);
	g_assert_cmpstr(request.task_id, ==, "task-1");
	g_assert_cmpstr(request.title, ==, "Renamed task");
	sakura_control_request_clear(&request);
	g_byte_array_set_size(encoded, 0);

	g_assert_true(sakura_control_encode_set_task_archived_request(
		"archive-task", "task-1", TRUE, encoded));
	g_assert_true(sakura_control_decode_request(encoded->data, encoded->len,
	                                           &request, &error));
	g_assert_no_error(error);
	g_assert_cmpint(request.kind, ==,
	               SAKURA_CONTROL_REQUEST_SET_TASK_ARCHIVED);
	g_assert_cmpstr(request.task_id, ==, "task-1");
	g_assert_true(request.archived);
	sakura_control_request_clear(&request);
	g_byte_array_set_size(encoded, 0);

	g_assert_true(sakura_control_encode_delete_task_request(
		"delete-task", "task-1", encoded));
	g_assert_true(sakura_control_decode_request(encoded->data, encoded->len,
	                                           &request, &error));
	g_assert_no_error(error);
	g_assert_cmpint(request.kind, ==, SAKURA_CONTROL_REQUEST_DELETE_TASK);
	g_assert_cmpstr(request.task_id, ==, "task-1");
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
	g_byte_array_set_size(encoded, 0);

	g_assert_true(sakura_control_encode_create_terminal_request(
		"create-terminal", "terminal-1", "group-1", "task-1", "/tmp", 100, 40,
		encoded));
	g_assert_true(sakura_control_decode_request(encoded->data, encoded->len,
	                                           &request, &error));
	g_assert_no_error(error);
	g_assert_cmpint(request.kind, ==,
	               SAKURA_CONTROL_REQUEST_CREATE_TERMINAL);
	g_assert_cmpstr(request.terminal_id, ==, "terminal-1");
	g_assert_cmpstr(request.group_id, ==, "group-1");
	g_assert_cmpstr(request.task_id, ==, "task-1");
	g_assert_cmpstr(request.cwd, ==, "/tmp");
	g_assert_cmpuint(request.cols, ==, 100);
	g_assert_cmpuint(request.rows, ==, 40);
	sakura_control_request_clear(&request);
	g_byte_array_set_size(encoded, 0);

	{
		const guint8 input[] = "echo terminal";

		g_assert_true(sakura_control_encode_terminal_input_request(
			"terminal-input", "terminal-1", input, sizeof(input) - 1,
			encoded));
		g_assert_true(sakura_control_decode_request(encoded->data, encoded->len,
		                                           &request, &error));
		g_assert_no_error(error);
		g_assert_cmpint(request.kind, ==,
		               SAKURA_CONTROL_REQUEST_TERMINAL_INPUT);
		g_assert_cmpstr(request.terminal_id, ==, "terminal-1");
		g_assert_cmpuint(request.input_length, ==, sizeof(input) - 1);
		g_assert_true(memcmp(request.input_data, input, sizeof(input) - 1) == 0);
	}
	sakura_control_request_clear(&request);
	g_byte_array_set_size(encoded, 0);

	g_assert_true(sakura_control_encode_terminal_resize_request(
		"terminal-resize", "terminal-1", 120, 50, encoded));
	g_assert_true(sakura_control_decode_request(encoded->data, encoded->len,
	                                           &request, &error));
	g_assert_no_error(error);
	g_assert_cmpint(request.kind, ==,
	               SAKURA_CONTROL_REQUEST_TERMINAL_RESIZE);
	g_assert_cmpstr(request.terminal_id, ==, "terminal-1");
	g_assert_cmpuint(request.cols, ==, 120);
	g_assert_cmpuint(request.rows, ==, 50);
	sakura_control_request_clear(&request);
	g_byte_array_set_size(encoded, 0);

	g_assert_true(sakura_control_encode_close_terminal_request(
		"close-terminal", "terminal-1", encoded));
	g_assert_true(sakura_control_decode_request(encoded->data, encoded->len,
	                                           &request, &error));
	g_assert_no_error(error);
	g_assert_cmpint(request.kind, ==,
	               SAKURA_CONTROL_REQUEST_CLOSE_TERMINAL);
	g_assert_cmpstr(request.terminal_id, ==, "terminal-1");
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
test_agent_call_on_connection(GSocketConnection *connection,
	                            const gchar *request_id,
	                            GByteArray *request_payload,
	                            SakuraControlResponse *response)
{
	GInputStream *input;
	GOutputStream *output;
	GByteArray *response_payload = NULL;
	GError *error = NULL;

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
	g_assert_true(response->has_snapshot || response->accepted);
	g_clear_pointer(&response_payload, g_byte_array_unref);
}


static void
test_agent_call(const gchar *socket_path, const gchar *request_id,
	              GByteArray *request_payload, SakuraControlResponse *response)
{
	GSocketConnection *connection = test_agent_connect_wait(socket_path);

	test_agent_call_on_connection(connection, request_id, request_payload,
	                              response);
	g_io_stream_close(G_IO_STREAM(connection), NULL, NULL);
	g_object_unref(connection);
}


static void
test_agent_set_event_timeout(GSocketConnection *connection)
{
	g_socket_set_timeout(g_socket_connection_get_socket(connection), 10);
}


static gboolean
test_agent_read_workspace_until(GInputStream *input, gsize terminal_count,
	                              const gchar *terminal_id,
	                              guint expected_cols, guint expected_rows)
{
	for (guint attempt = 0; attempt < 100; attempt++) {
		GByteArray *payload = NULL;
		Sakura__Control__V1__Event *event;
		GError *error = NULL;

		if (!sakura_control_frame_read(input, &payload, NULL, &error)) {
			g_clear_error(&error);
			return FALSE;
		}
		event = sakura__control__v1__event__unpack(NULL, payload->len,
		                                           payload->data);
		g_byte_array_unref(payload);
		if (event == NULL)
			continue;
		if (event->body_case ==
				SAKURA__CONTROL__V1__EVENT__BODY_WORKSPACE_CHANGED &&
		    event->workspace_changed != NULL &&
		    event->workspace_changed->snapshot != NULL &&
		    event->workspace_changed->snapshot->n_terminals == terminal_count) {
			Sakura__Control__V1__WorkspaceSnapshot *snapshot =
				event->workspace_changed->snapshot;
			gboolean found = terminal_id == NULL;

			for (gsize index = 0; index < snapshot->n_terminals; index++) {
				Sakura__Control__V1__Terminal *terminal =
					snapshot->terminals[index];

				if (terminal_id != NULL &&
				    g_strcmp0(terminal->id, terminal_id) == 0) {
					found = TRUE;
					if (expected_cols != 0)
						g_assert_cmpuint(terminal->cols, ==, expected_cols);
					if (expected_rows != 0)
						g_assert_cmpuint(terminal->rows, ==, expected_rows);
				}
			}
			if (found) {
				sakura__control__v1__event__free_unpacked(event, NULL);
				return TRUE;
			}
		}
		sakura__control__v1__event__free_unpacked(event, NULL);
	}
	return FALSE;
}


static gboolean
test_agent_read_output_until(GInputStream *input, const gchar *terminal_id,
	                           const gchar *needle)
{
	for (guint attempt = 0; attempt < 100; attempt++) {
		GByteArray *payload = NULL;
		guint64 sequence;
		gchar *event_terminal_id = NULL;
		guint8 *data = NULL;
		gsize data_length = 0;
		gboolean final_chunk = FALSE;
		GError *error = NULL;

		if (!sakura_control_frame_read(input, &payload, NULL, &error)) {
			g_clear_error(&error);
			return FALSE;
		}
		if (sakura_control_decode_terminal_output_event(
				payload->data, payload->len, &sequence, &event_terminal_id,
				&data, &data_length, &final_chunk, &error)) {
			if (data != NULL &&
			    g_strcmp0(event_terminal_id, terminal_id) == 0 &&
			    g_strstr_len((const gchar *)data, data_length, needle) != NULL) {
				g_free(event_terminal_id);
				g_free(data);
				g_byte_array_unref(payload);
				return TRUE;
			}
			g_free(event_terminal_id);
			g_free(data);
		} else {
			g_clear_error(&error);
		}
		g_byte_array_unref(payload);
	}
	return FALSE;
}


static gboolean
test_agent_read_status_until(GInputStream *input, const gchar *terminal_id,
	                           guint expected_status)
{
	for (guint attempt = 0; attempt < 100; attempt++) {
		GByteArray *payload = NULL;
		guint64 sequence;
		gchar *event_terminal_id = NULL;
		gchar *message = NULL;
		guint status = 0;
		GError *error = NULL;

		if (!sakura_control_frame_read(input, &payload, NULL, &error)) {
			g_clear_error(&error);
			return FALSE;
		}
		if (sakura_control_decode_terminal_status_event(
				payload->data, payload->len, &sequence, &event_terminal_id,
				&status, &message, &error)) {
			if (g_strcmp0(event_terminal_id, terminal_id) == 0 &&
			    status == expected_status) {
				g_free(event_terminal_id);
				g_free(message);
				g_byte_array_unref(payload);
				return TRUE;
			}
			g_free(event_terminal_id);
			g_free(message);
		} else {
			g_clear_error(&error);
		}
		g_byte_array_unref(payload);
	}
	return FALSE;
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
	gchar *second_group_id = NULL;
	gchar *task_id = NULL;

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
	task_id = g_strdup(task->id);
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
	event_snapshot = NULL;
	g_clear_pointer(&event_payload, g_byte_array_unref);

	snapshot = test_load_agent_session(session_path);
	for (guint index = 0; index < snapshot->groups->len; index++) {
		group = g_ptr_array_index(snapshot->groups, index);
		if (g_strcmp0(group->title, "Archive") == 0)
			second_group_id = g_strdup(group->id);
	}
	g_assert_nonnull(second_group_id);
	sakura_session_snapshot_free(snapshot);

	g_byte_array_set_size(request, 0);
	g_assert_true(sakura_control_encode_update_group_request(
		"update-group", second_group_id, "Renamed", "/tmp/renamed", request));
	test_agent_call(socket_path, "update-group", request, &response);
	sakura_control_response_clear(&response);
	g_assert_true(sakura_control_frame_read(subscriber_input, &event_payload,
	                                        NULL, &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_decode_workspace_changed_event(
		event_payload->data, event_payload->len, &event_sequence,
		&event_snapshot, &error));
	g_assert_no_error(error);
	g_assert_cmpuint(event_sequence, ==, 2);
	g_assert_cmpuint(event_snapshot->groups->len, ==, 2);
	for (guint index = 0; index < event_snapshot->groups->len; index++) {
		group = g_ptr_array_index(event_snapshot->groups, index);
		if (g_strcmp0(group->id, second_group_id) == 0) {
			g_assert_cmpstr(group->title, ==, "Renamed");
			g_assert_cmpstr(group->directory, ==, "/tmp/renamed");
		}
	}
	sakura_session_snapshot_free(event_snapshot);
	event_snapshot = NULL;
	g_clear_pointer(&event_payload, g_byte_array_unref);

	g_byte_array_set_size(request, 0);
	g_assert_true(sakura_control_encode_set_group_archived_request(
		"archive-group", second_group_id, TRUE, request));
	test_agent_call(socket_path, "archive-group", request, &response);
	sakura_control_response_clear(&response);
	g_assert_true(sakura_control_frame_read(subscriber_input, &event_payload,
	                                        NULL, &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_decode_workspace_changed_event(
		event_payload->data, event_payload->len, &event_sequence,
		&event_snapshot, &error));
	g_assert_no_error(error);
	g_assert_cmpuint(event_sequence, ==, 3);
	for (guint index = 0; index < event_snapshot->groups->len; index++) {
		group = g_ptr_array_index(event_snapshot->groups, index);
		if (g_strcmp0(group->id, second_group_id) == 0)
			g_assert_true(group->archived);
	}
	sakura_session_snapshot_free(event_snapshot);
	event_snapshot = NULL;
	g_clear_pointer(&event_payload, g_byte_array_unref);

	g_byte_array_set_size(request, 0);
	g_assert_true(sakura_control_encode_delete_group_request(
		"delete-group", second_group_id, request));
	test_agent_call(socket_path, "delete-group", request, &response);
	sakura_control_response_clear(&response);
	g_assert_true(sakura_control_frame_read(subscriber_input, &event_payload,
	                                        NULL, &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_decode_workspace_changed_event(
		event_payload->data, event_payload->len, &event_sequence,
		&event_snapshot, &error));
	g_assert_no_error(error);
	g_assert_cmpuint(event_sequence, ==, 4);
	g_assert_cmpuint(event_snapshot->groups->len, ==, 1);
	sakura_session_snapshot_free(event_snapshot);
	event_snapshot = NULL;
	g_clear_pointer(&event_payload, g_byte_array_unref);

	g_byte_array_set_size(request, 0);
	g_assert_true(sakura_control_encode_update_task_request(
		"update-task", task_id, "Renamed build", request));
	test_agent_call(socket_path, "update-task", request, &response);
	sakura_control_response_clear(&response);
	g_assert_true(sakura_control_frame_read(subscriber_input, &event_payload,
	                                        NULL, &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_decode_workspace_changed_event(
		event_payload->data, event_payload->len, &event_sequence,
		&event_snapshot, &error));
	g_assert_no_error(error);
	g_assert_cmpuint(event_sequence, ==, 5);
	g_assert_cmpuint(event_snapshot->tasks->len, ==, 1);
	task = g_ptr_array_index(event_snapshot->tasks, 0);
	g_assert_cmpstr(task->id, ==, task_id);
	g_assert_cmpstr(task->title, ==, "Renamed build");
	sakura_session_snapshot_free(event_snapshot);
	event_snapshot = NULL;
	g_clear_pointer(&event_payload, g_byte_array_unref);

	g_byte_array_set_size(request, 0);
	g_assert_true(sakura_control_encode_set_task_archived_request(
		"archive-task", task_id, TRUE, request));
	test_agent_call(socket_path, "archive-task", request, &response);
	sakura_control_response_clear(&response);
	g_assert_true(sakura_control_frame_read(subscriber_input, &event_payload,
	                                        NULL, &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_decode_workspace_changed_event(
		event_payload->data, event_payload->len, &event_sequence,
		&event_snapshot, &error));
	g_assert_no_error(error);
	g_assert_cmpuint(event_sequence, ==, 6);
	task = g_ptr_array_index(event_snapshot->tasks, 0);
	g_assert_true(task->archived);
	sakura_session_snapshot_free(event_snapshot);
	event_snapshot = NULL;
	g_clear_pointer(&event_payload, g_byte_array_unref);

	g_byte_array_set_size(request, 0);
	g_assert_true(sakura_control_encode_delete_task_request(
		"delete-task", task_id, request));
	test_agent_call(socket_path, "delete-task", request, &response);
	sakura_control_response_clear(&response);
	g_assert_true(sakura_control_frame_read(subscriber_input, &event_payload,
	                                        NULL, &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_decode_workspace_changed_event(
		event_payload->data, event_payload->len, &event_sequence,
		&event_snapshot, &error));
	g_assert_no_error(error);
	g_assert_cmpuint(event_sequence, ==, 7);
	g_assert_cmpuint(event_snapshot->tasks->len, ==, 0);
	sakura_session_snapshot_free(event_snapshot);
	event_snapshot = NULL;
	g_clear_pointer(&event_payload, g_byte_array_unref);
	g_io_stream_close(G_IO_STREAM(subscriber), NULL, NULL);
	g_object_unref(subscriber);
	test_agent_stop(process);

	g_byte_array_unref(request);
	g_free(group_id);
	g_free(second_group_id);
	g_free(task_id);
	g_remove(session_path);
	g_remove(socket_path);
	g_rmdir(directory);
	g_free(session_path);
	g_free(socket_path);
	g_free(directory);
}


static void
test_agent_terminal_lifecycle(void)
{
	GSubprocess *process;
	GByteArray *request = g_byte_array_new();
	SakuraControlResponse response = { 0 };
	GSocketConnection *command_connection;
	GSocketConnection *subscriber;
	GInputStream *subscriber_input;
	GOutputStream *subscriber_output;
	GByteArray *event_payload = NULL;
	GError *error = NULL;
	gchar *directory;
	gchar *socket_path;
	gchar *session_path;
	gchar *terminal_id = NULL;

	directory = g_dir_make_tmp("sakura-agent-terminal-XXXXXX", &error);
	g_assert_no_error(error);
	g_assert_nonnull(directory);
	socket_path = g_build_filename(directory, "agent.sock", NULL);
	session_path = g_build_filename(directory, "workspace.session", NULL);
	process = test_agent_start(socket_path, session_path);
	command_connection = test_agent_connect_wait(socket_path);

	subscriber = test_agent_connect_wait(socket_path);
	test_agent_set_event_timeout(subscriber);
	subscriber_input = g_io_stream_get_input_stream(G_IO_STREAM(subscriber));
	subscriber_output = g_io_stream_get_output_stream(G_IO_STREAM(subscriber));
	g_assert_true(sakura_control_encode_subscribe_events_request(
		"subscribe-terminal", 0, request));
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
	g_assert_cmpstr(response.accepted_kind, ==, "workspace_events");
	sakura_control_response_clear(&response);
	g_clear_pointer(&event_payload, g_byte_array_unref);
	g_assert_true(test_agent_read_workspace_until(subscriber_input, 0, NULL,
	                                              0, 0));

	g_byte_array_set_size(request, 0);
	g_assert_true(sakura_control_encode_create_terminal_request(
		"create-terminal", "terminal-agent-1", "root", "root", "/tmp", 100, 40,
		request));
	test_agent_call_on_connection(command_connection, "create-terminal", request,
	                              &response);
	g_assert_true(response.accepted);
	g_assert_cmpstr(response.accepted_kind, ==, "terminal");
	g_assert_nonnull(response.accepted_id);
	g_assert_cmpstr(response.accepted_id, ==, "terminal-agent-1");
	terminal_id = g_strdup(response.accepted_id);
	sakura_control_response_clear(&response);
	g_assert_true(test_agent_read_workspace_until(subscriber_input, 1,
	                                              terminal_id, 100, 40));

	g_byte_array_set_size(request, 0);
	{
		const gchar input[] = "printf 'sakura-terminal-test\n'\n";

		g_assert_true(sakura_control_encode_terminal_input_request(
			"terminal-input", terminal_id, (const guint8 *)input,
			sizeof(input) - 1, request));
	}
	test_agent_call_on_connection(command_connection, "terminal-input", request,
	                              &response);
	g_assert_true(response.accepted);
	g_assert_cmpstr(response.accepted_kind, ==, "terminal_input");
	sakura_control_response_clear(&response);
	g_assert_true(test_agent_read_output_until(
		subscriber_input, terminal_id, "sakura-terminal-test"));

	g_byte_array_set_size(request, 0);
	g_assert_true(sakura_control_encode_terminal_resize_request(
		"terminal-resize", terminal_id, 120, 50, request));
	test_agent_call_on_connection(command_connection, "terminal-resize", request,
	                              &response);
	g_assert_true(response.has_snapshot);
	sakura_control_response_clear(&response);
	g_assert_true(test_agent_read_workspace_until(subscriber_input, 1,
	                                              terminal_id, 120, 50));

	g_byte_array_set_size(request, 0);
	{
		const gchar input[] = "exit\n";

		g_assert_true(sakura_control_encode_terminal_input_request(
			"terminal-exit", terminal_id, (const guint8 *)input,
			sizeof(input) - 1, request));
	}
	test_agent_call_on_connection(command_connection, "terminal-exit", request,
	                              &response);
	g_assert_true(response.accepted);
	sakura_control_response_clear(&response);
	g_assert_true(test_agent_read_status_until(
		subscriber_input, terminal_id, SAKURA_TERMINAL_EXITED));

	g_byte_array_set_size(request, 0);
	g_assert_true(sakura_control_encode_close_terminal_request(
		"close-terminal", terminal_id, request));
	test_agent_call_on_connection(command_connection, "close-terminal", request,
	                              &response);
	g_assert_true(response.has_snapshot);
	sakura_control_response_clear(&response);
	g_assert_true(test_agent_read_workspace_until(subscriber_input, 0, NULL,
	                                              0, 0));

	g_io_stream_close(G_IO_STREAM(subscriber), NULL, NULL);
	g_object_unref(subscriber);
	g_io_stream_close(G_IO_STREAM(command_connection), NULL, NULL);
	g_object_unref(command_connection);
	test_agent_stop(process);
	g_byte_array_unref(request);
	g_free(terminal_id);
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
	g_test_add_func("/control/agent-terminal-lifecycle",
	                test_agent_terminal_lifecycle);
	return g_test_run();
}
