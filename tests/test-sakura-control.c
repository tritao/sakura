#include <signal.h>
#include <string.h>
#include <unistd.h>

#include <gio/gio.h>
#include <glib/gstdio.h>

#include "sakura-control-transport.h"
#include "sakura/control.pb-c.h"

static gchar *test_agent_workspace_id;
static void test_agent_handshake_on_connection(GSocketConnection *connection);

static SakuraSessionSnapshot *test_load_agent_session(const gchar *session_path);
static void test_save_agent_session(const gchar *session_path,
                                    const SakuraSessionSnapshot *snapshot);
static GSubprocess *test_agent_start(const gchar *socket_path,
                                     const gchar *session_path);
static void test_agent_stop(GSubprocess *process);
static void test_agent_handshake_on_connection(GSocketConnection *connection);


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
	SakuraCoreTerminal *third = sakura_core_terminal_new(
		"terminal-third", "/tmp", workspace->root_group, NULL, 80, 24);
	SakuraCoreTerminal *first = sakura_core_terminal_new(
		"terminal-first", "/tmp", workspace->root_group, NULL, 80, 24);
	SakuraCoreTerminal *second = sakura_core_terminal_new(
		"terminal-second", "/tmp", workspace->root_group, NULL, 80, 24);
	GByteArray *encoded = g_byte_array_new();
	SakuraControlResponse response = { 0 };
	SakuraSessionSnapshot *snapshot = NULL;
	guint64 sequence = 0;
	GError *error = NULL;
	third->order = 2;
	first->order = 0;
	second->order = 1;
	second->kind = SAKURA_TAB_CODEX;
	second->resume_on_start = TRUE;
	g_assert_true(sakura_core_workspace_add_terminal(workspace, third));
	g_assert_true(sakura_core_workspace_add_terminal(workspace, first));
	g_assert_true(sakura_core_workspace_add_terminal(workspace, second));

	g_assert_true(sakura_control_encode_snapshot_response("request-2", 7,
	                                                     workspace, encoded));
	g_assert_true(sakura_control_decode_response(encoded->data, encoded->len,
	                                            &response, &error));
	g_assert_no_error(error);
	g_assert_cmpstr(response.request_id, ==, "request-2");
	g_assert_true(response.has_snapshot);
	g_assert_true(sakura_control_decode_snapshot_response(
		encoded->data, encoded->len, &sequence, &snapshot, &error));
	g_assert_no_error(error);
	g_assert_cmpuint(sequence, ==, 7);
	g_assert_nonnull(snapshot);
	g_assert_nonnull(snapshot->groups);
	g_assert_cmpuint(snapshot->groups->len, ==, 0);
	g_assert_cmpuint(snapshot->tabs->len, ==, 3);
	g_assert_cmpstr(((SakuraSessionTabRecord *)g_ptr_array_index(
		snapshot->tabs, 0))->terminal_id, ==, "terminal-first");
	g_assert_cmpuint(((SakuraSessionTabRecord *)g_ptr_array_index(
		snapshot->tabs, 0))->cols, ==, 80);
	g_assert_cmpuint(((SakuraSessionTabRecord *)g_ptr_array_index(
		snapshot->tabs, 0))->rows, ==, 24);
	g_assert_cmpstr(((SakuraSessionTabRecord *)g_ptr_array_index(
		snapshot->tabs, 1))->terminal_id, ==, "terminal-second");
	g_assert_true(((SakuraSessionTabRecord *)g_ptr_array_index(
		snapshot->tabs, 1))->resume_on_start);
	g_assert_cmpstr(((SakuraSessionTabRecord *)g_ptr_array_index(
		snapshot->tabs, 2))->terminal_id, ==, "terminal-third");

	sakura_session_snapshot_free(snapshot);
	sakura_control_response_clear(&response);
	g_byte_array_unref(encoded);
	sakura_core_workspace_free(workspace);
}


static void
test_filesystem_protocol_roundtrip(void)
{
	GByteArray *encoded = g_byte_array_new();
	SakuraControlRequest request = { 0 };
	SakuraControlResponse response = { 0 };
	SakuraControlFileEntry *entry = g_new0(SakuraControlFileEntry, 1);
	GPtrArray *entries = g_ptr_array_new();
	GError *error = NULL;
	const guint8 data[] = { 'o', 'k' };

	g_assert_true(sakura_control_encode_list_files_request(
		"list-files", "task-1", "src", encoded));
	g_assert_true(sakura_control_decode_request(encoded->data, encoded->len,
	                                           &request, &error));
	g_assert_no_error(error);
	g_assert_cmpint(request.kind, ==, SAKURA_CONTROL_REQUEST_LIST_FILES);
	g_assert_cmpstr(request.worktree_id, ==, "task-1");
	g_assert_cmpstr(request.path, ==, "src");
	sakura_control_request_clear(&request);
	g_byte_array_set_size(encoded, 0);

	g_assert_true(sakura_control_encode_read_file_request(
		"read-file", "task-1", "src/main.c", 4, 128, TRUE, "v1",
		encoded));
	g_assert_true(sakura_control_decode_request(encoded->data, encoded->len,
	                                           &request, &error));
	g_assert_no_error(error);
	g_assert_cmpint(request.kind, ==, SAKURA_CONTROL_REQUEST_READ_FILE);
	g_assert_cmpuint(request.file_offset, ==, 4);
	g_assert_cmpuint(request.file_length, ==, 128);
	g_assert_true(request.has_file_length);
	g_assert_cmpstr(request.expected_file_version, ==, "v1");
	sakura_control_request_clear(&request);
	g_byte_array_set_size(encoded, 0);

	g_assert_true(sakura_control_encode_write_file_request(
		"write-file", "task-1", "src/main.c", data, sizeof(data), "v1",
		TRUE, encoded));
	g_assert_true(sakura_control_decode_request(encoded->data, encoded->len,
	                                           &request, &error));
	g_assert_no_error(error);
	g_assert_cmpint(request.kind, ==, SAKURA_CONTROL_REQUEST_WRITE_FILE);
	g_assert_cmpuint(request.file_data_length, ==, sizeof(data));
	g_assert_cmpmem(request.file_data, request.file_data_length,
	                data, sizeof(data));
	g_assert_true(request.truncate_file);
	sakura_control_request_clear(&request);
	g_byte_array_set_size(encoded, 0);

	entry->path = g_strdup("src/main.c");
	entry->name = g_strdup("main.c");
	entry->directory = FALSE;
	entry->size = sizeof(data);
	entry->modified_unix = 42;
	entry->readonly = FALSE;
	entry->version = g_strdup("v2");
	g_ptr_array_add(entries, entry);
	g_assert_true(sakura_control_encode_file_list_response(
		"list-response", "sakura://workspace/ws/task-1", "dir-v1",
		entries, encoded));
	g_ptr_array_free(entries, TRUE);
	g_free(entry->path);
	g_free(entry->name);
	g_free(entry->version);
	g_free(entry);
	entries = NULL;
	g_assert_true(sakura_control_decode_response(encoded->data, encoded->len,
	                                            &response, &error));
	g_assert_no_error(error);
	g_assert_true(response.has_file_list);
	g_assert_cmpuint(response.file_entries->len, ==, 1);
	entry = g_ptr_array_index(response.file_entries, 0);
	g_assert_cmpstr(entry->path, ==, "src/main.c");
	g_assert_cmpstr(response.file_version, ==, "dir-v1");
	sakura_control_response_clear(&response);
	g_byte_array_set_size(encoded, 0);

	g_assert_true(sakura_control_encode_file_read_response(
		"read-response", (const gchar *)data, sizeof(data), "v2", TRUE,
		encoded));
	g_assert_true(sakura_control_decode_response(encoded->data, encoded->len,
	                                            &response, &error));
	g_assert_no_error(error);
	g_assert_true(response.has_file_read);
	g_assert_cmpmem(response.file_data, response.file_data_length,
	                data, sizeof(data));
	g_assert_true(response.file_eof);
	sakura_control_response_clear(&response);
	g_byte_array_unref(encoded);
}


static void
test_terminal_output_offsets_roundtrip(void)
{
	const guint8 data[] = { 'a', 'b', 'c' };
	GByteArray *encoded = g_byte_array_new();
	guint64 sequence = 0;
	guint64 start_offset = 0;
	guint64 end_offset = 0;
	gchar *terminal_id = NULL;
	guint8 *decoded_data = NULL;
	gsize data_length = 0;
	gboolean final_chunk = FALSE;
	GError *error = NULL;

	g_assert_true(sakura_control_encode_terminal_output_event_with_offsets(
		7, "terminal-1", 42, 45, data, sizeof(data), FALSE, encoded));
	g_assert_true(sakura_control_decode_terminal_output_event_with_offsets(
		encoded->data, encoded->len, &sequence, &terminal_id, &start_offset,
		&end_offset, &decoded_data, &data_length, &final_chunk, &error));
	g_assert_no_error(error);
	g_assert_cmpuint(sequence, ==, 7);
	g_assert_cmpstr(terminal_id, ==, "terminal-1");
	g_assert_cmpuint(start_offset, ==, 42);
	g_assert_cmpuint(end_offset, ==, 45);
	g_assert_cmpuint(data_length, ==, sizeof(data));
	g_assert_cmpmem(decoded_data, data_length, data, sizeof(data));
	g_assert_false(final_chunk);

	g_free(terminal_id);
	g_free(decoded_data);
	g_byte_array_unref(encoded);
}


static void
test_structured_error_roundtrip(void)
{
	GByteArray *encoded = g_byte_array_new();
	SakuraControlResponse response = { 0 };
	SakuraControlRemoteError remote_error = { 0 };
	GError *error = NULL;

	g_assert_true(sakura_control_encode_error_response_with_revision(
		"error-request", SAKURA_CONTROL_ERROR_OUTPUT_GAP,
		"terminal output is no longer retained", 42, TRUE, encoded));
	g_assert_false(sakura_control_decode_response(
		encoded->data, encoded->len, &response, &error));
	g_assert_error(error, SAKURA_CONTROL_ERROR_DOMAIN,
	               SAKURA_CONTROL_ERROR_CODE_OUTPUT_GAP);
	g_assert_true(sakura_control_response_get_remote_error(
		&response, &remote_error));
	g_assert_cmpstr(remote_error.remote_code, ==,
	                SAKURA_CONTROL_ERROR_OUTPUT_GAP);
	g_assert_true(remote_error.retryable);
	g_assert_cmpuint(remote_error.current_revision, ==, 42);
	g_assert_cmpstr(response.error_message, ==,
	                "terminal output is no longer retained");

	g_clear_error(&error);
	sakura_control_response_clear(&response);
	g_byte_array_unref(encoded);
}


static void
test_local_endpoint_validation(void)
{
	GError *error = NULL;
	gchar *directory;
	gchar *regular_path;
	gchar *symlink_path;

	directory = g_dir_make_tmp("sakura-control-endpoint-XXXXXX", &error);
	g_assert_no_error(error);
	regular_path = g_build_filename(directory, "endpoint", NULL);
	symlink_path = g_build_filename(directory, "endpoint-link", NULL);
	g_assert_true(g_file_set_contents(regular_path, "not a socket", -1, &error));
	g_assert_no_error(error);
	g_assert_false(sakura_control_validate_local_endpoint(regular_path, &error));
	g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL);
	g_clear_error(&error);
	g_assert_cmpint(symlink(regular_path, symlink_path), ==, 0);
	g_assert_false(sakura_control_validate_local_endpoint(symlink_path, &error));
	g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL);
	g_clear_error(&error);

	g_remove(symlink_path);
	g_remove(regular_path);
	g_rmdir(directory);
	g_free(symlink_path);
	g_free(regular_path);
	g_free(directory);
}


static void
test_client_request_cancellation(void)
{
	GSubprocess *process;
	SakuraControlClientConnection *client;
	GCancellable *cancellable;
	GByteArray *request = g_byte_array_new();
	GByteArray *response = NULL;
	GError *error = NULL;
	gchar *directory;
	gchar *socket_path;
	gchar *session_path;

	directory = g_dir_make_tmp("sakura-control-cancel-XXXXXX", &error);
	g_assert_no_error(error);
	socket_path = g_build_filename(directory, "agent.sock", NULL);
	session_path = g_build_filename(directory, "workspace.session", NULL);
	process = test_agent_start(socket_path, session_path);
	client = sakura_control_client_connect(
		socket_path, test_agent_workspace_id, "sakura-test", 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(client);
	g_assert_cmpstr(sakura_control_client_agent_version(client), ==,
	                SAKURA_AGENT_BUILD_ID);
	cancellable = g_cancellable_new();
	g_cancellable_cancel(cancellable);
	g_assert_true(sakura_control_encode_get_snapshot_request(
		"cancelled-request", request));
	g_assert_false(sakura_control_client_request_with_cancellable(
		client, request, &response, cancellable, &error));
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
	g_clear_error(&error);

	g_clear_pointer(&response, g_byte_array_unref);
	g_object_unref(cancellable);
	sakura_control_client_close(client);
	sakura_control_client_unref(client);
	test_agent_stop(process);
	g_byte_array_unref(request);
	g_remove(session_path);
	g_remove(socket_path);
	g_rmdir(directory);
	g_free(session_path);
	g_free(socket_path);
	g_free(directory);
}


static void
test_hello_roundtrip(void)
{
	GByteArray *request_payload = g_byte_array_new();
	GByteArray *response_payload = g_byte_array_new();
	SakuraControlRequest request = { 0 };
	SakuraControlResponse response = { 0 };
	GError *error = NULL;

	g_assert_true(sakura_control_encode_hello_request(
		"hello-request", SAKURA_CONTROL_PROTOCOL_VERSION, "test-client",
		"workspace-test",
		request_payload));
	g_assert_true(sakura_control_decode_request(
		request_payload->data, request_payload->len, &request, &error));
	g_assert_no_error(error);
	g_assert_cmpint(request.kind, ==, SAKURA_CONTROL_REQUEST_HELLO);
	g_assert_cmpuint(request.protocol_version, ==,
	                SAKURA_CONTROL_PROTOCOL_VERSION);
	g_assert_cmpstr(request.client_name, ==, "test-client");
	g_assert_cmpstr(request.workspace_id, ==, "workspace-test");
	sakura_control_request_clear(&request);

	g_assert_true(sakura_control_encode_hello_response(
		"hello-request", SAKURA_CONTROL_PROTOCOL_VERSION, "0.1",
		SAKURA_CONTROL_CAPABILITY_WORKSPACE |
		SAKURA_CONTROL_CAPABILITY_TERMINALS, "workspace-test", response_payload));
	g_assert_true(sakura_control_decode_response(
		response_payload->data, response_payload->len, &response, &error));
	g_assert_no_error(error);
	g_assert_true(response.hello);
	g_assert_cmpstr(response.request_id, ==, "hello-request");
	g_assert_cmpuint(response.hello_protocol_version, ==,
	                SAKURA_CONTROL_PROTOCOL_VERSION);
	g_assert_cmpstr(response.agent_version, ==, "0.1");
	g_assert_cmpstr(response.workspace_id, ==, "workspace-test");
	g_assert_cmpuint(response.capabilities,
	                ==,
	                SAKURA_CONTROL_CAPABILITY_WORKSPACE |
	                SAKURA_CONTROL_CAPABILITY_TERMINALS);

	sakura_control_response_clear(&response);
	g_byte_array_unref(response_payload);
	g_byte_array_unref(request_payload);
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
	g_assert_true(sakura_control_request_set_expected_revision(encoded, 12));
	sakura_control_request_clear(&request);
	g_assert_true(sakura_control_decode_request(encoded->data, encoded->len,
	                                           &request, &error));
	g_assert_no_error(error);
	g_assert_true(request.has_expected_revision);
	g_assert_cmpuint(request.expected_revision, ==, 12);
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

	g_assert_true(sakura_control_encode_move_group_request(
		"move-group", "group-1", "group-2", "group-3", TRUE, encoded));
	g_assert_true(sakura_control_decode_request(encoded->data, encoded->len,
	                                           &request, &error));
	g_assert_no_error(error);
	g_assert_cmpint(request.kind, ==, SAKURA_CONTROL_REQUEST_MOVE_GROUP);
	g_assert_cmpstr(request.group_id, ==, "group-1");
	g_assert_cmpstr(request.parent_id, ==, "group-2");
	g_assert_cmpstr(request.target_id, ==, "group-3");
	g_assert_true(request.after);
	sakura_control_request_clear(&request);
	g_byte_array_set_size(encoded, 0);

	g_assert_true(sakura_control_encode_move_task_request(
		"move-task", "task-1", "group-2", "task-parent", "task-2",
		TRUE, encoded));
	g_assert_true(sakura_control_decode_request(encoded->data, encoded->len,
	                                           &request, &error));
	g_assert_no_error(error);
	g_assert_cmpint(request.kind, ==, SAKURA_CONTROL_REQUEST_MOVE_TASK);
	g_assert_cmpstr(request.task_id, ==, "task-1");
	g_assert_cmpstr(request.group_id, ==, "group-2");
	g_assert_cmpstr(request.parent_id, ==, "task-parent");
	g_assert_cmpstr(request.target_id, ==, "task-2");
	g_assert_true(request.after);
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

	g_assert_true(sakura_control_encode_update_page_request(
		"update-page", "page-1", "group-1", "task-1", "Renamed page",
		TRUE, TRUE, encoded));
	g_assert_true(sakura_control_decode_request(encoded->data, encoded->len,
	                                           &request, &error));
	g_assert_no_error(error);
	g_assert_cmpint(request.kind, ==, SAKURA_CONTROL_REQUEST_UPDATE_PAGE);
	g_assert_cmpstr(request.page_id, ==, "page-1");
	g_assert_cmpstr(request.group_id, ==, "group-1");
	g_assert_cmpstr(request.task_id, ==, "task-1");
	g_assert_cmpstr(request.title, ==, "Renamed page");
	g_assert_true(request.title_set_by_user);
	g_assert_true(request.archived);
	sakura_control_request_clear(&request);
	g_byte_array_set_size(encoded, 0);

	g_assert_true(sakura_control_encode_move_page_request(
		"move-page", "page-1", "group-2", "task-2", encoded));
	g_assert_true(sakura_control_decode_request(encoded->data, encoded->len,
	                                           &request, &error));
	g_assert_no_error(error);
	g_assert_cmpint(request.kind, ==, SAKURA_CONTROL_REQUEST_MOVE_PAGE);
	g_assert_cmpstr(request.page_id, ==, "page-1");
	g_assert_cmpstr(request.group_id, ==, "group-2");
	g_assert_cmpstr(request.task_id, ==, "task-2");
	sakura_control_request_clear(&request);
	g_byte_array_set_size(encoded, 0);

	g_assert_true(sakura_control_encode_rename_page_request(
		"rename-page", "page-1", "New page", TRUE, encoded));
	g_assert_true(sakura_control_decode_request(encoded->data, encoded->len,
	                                           &request, &error));
	g_assert_no_error(error);
	g_assert_cmpint(request.kind, ==, SAKURA_CONTROL_REQUEST_RENAME_PAGE);
	g_assert_cmpstr(request.page_id, ==, "page-1");
	g_assert_cmpstr(request.title, ==, "New page");
	g_assert_true(request.title_set_by_user);
	sakura_control_request_clear(&request);
	g_byte_array_set_size(encoded, 0);

	g_assert_true(sakura_control_encode_set_page_archived_request(
		"archive-page", "page-1", TRUE, encoded));
	g_assert_true(sakura_control_decode_request(encoded->data, encoded->len,
	                                           &request, &error));
	g_assert_no_error(error);
	g_assert_cmpint(request.kind, ==, SAKURA_CONTROL_REQUEST_SET_PAGE_ARCHIVED);
	g_assert_cmpstr(request.page_id, ==, "page-1");
	g_assert_true(request.archived);
	sakura_control_request_clear(&request);
	g_byte_array_set_size(encoded, 0);

	g_assert_true(sakura_control_encode_delete_page_request(
		"delete-page", "page-1", encoded));
	g_assert_true(sakura_control_decode_request(encoded->data, encoded->len,
	                                           &request, &error));
	g_assert_no_error(error);
	g_assert_cmpint(request.kind, ==, SAKURA_CONTROL_REQUEST_DELETE_PAGE);
	g_assert_cmpstr(request.page_id, ==, "page-1");
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

	g_assert_true(sakura_control_encode_create_terminal_request_with_order(
		"create-terminal", "terminal-1", "page-1", "group-1", "task-1",
		"/tmp", 100, 40, 7, TRUE,
		encoded));
	g_assert_true(sakura_control_decode_request(encoded->data, encoded->len,
	                                           &request, &error));
	g_assert_no_error(error);
	g_assert_cmpint(request.kind, ==,
	               SAKURA_CONTROL_REQUEST_CREATE_TERMINAL);
	g_assert_cmpstr(request.terminal_id, ==, "terminal-1");
	g_assert_cmpstr(request.page_id, ==, "page-1");
	g_assert_cmpstr(request.group_id, ==, "group-1");
	g_assert_cmpstr(request.task_id, ==, "task-1");
	g_assert_cmpstr(request.cwd, ==, "/tmp");
	g_assert_cmpuint(request.cols, ==, 100);
	g_assert_cmpuint(request.rows, ==, 40);
	g_assert_true(request.has_order);
	g_assert_cmpuint(request.order, ==, 7);
	sakura_control_request_clear(&request);
	g_byte_array_set_size(encoded, 0);

	g_assert_true(sakura_control_encode_create_codex_request_with_model(
		"create-codex", "codex-1", "page-1", "group-1", "task-1",
		"/tmp", 120, 50, "gpt-5.6-luna", "high", "resume-1", encoded));
	g_assert_true(sakura_control_decode_request(encoded->data, encoded->len,
	                                           &request, &error));
	g_assert_no_error(error);
	g_assert_cmpint(request.kind, ==, SAKURA_CONTROL_REQUEST_CREATE_CODEX);
	g_assert_cmpstr(request.terminal_id, ==, "codex-1");
	g_assert_cmpstr(request.page_id, ==, "page-1");
	g_assert_cmpstr(request.group_id, ==, "group-1");
	g_assert_cmpstr(request.task_id, ==, "task-1");
	g_assert_cmpstr(request.cwd, ==, "/tmp");
	g_assert_cmpstr(request.model, ==, "gpt-5.6-luna");
	g_assert_cmpstr(request.reasoning_effort, ==, "high");
	g_assert_cmpstr(request.resume_session_id, ==, "resume-1");
	g_assert_cmpuint(request.cols, ==, 120);
	g_assert_cmpuint(request.rows, ==, 50);
	sakura_control_request_clear(&request);
	g_byte_array_set_size(encoded, 0);

	g_assert_true(sakura_control_encode_restart_terminal_request_with_model_and_order(
		"restart-terminal", "terminal-1", "page-1", "group-1", "task-1",
		"/tmp", 120, 50, SAKURA_TAB_CODEX, "resume-restart",
		"gpt-5.6-luna", "high",
		"tracking-restart", 9, TRUE, encoded));
	g_assert_true(sakura_control_decode_request(encoded->data, encoded->len,
	                                           &request, &error));
	g_assert_no_error(error);
	g_assert_cmpint(request.kind, ==,
	               SAKURA_CONTROL_REQUEST_RESTART_TERMINAL);
	g_assert_cmpstr(request.terminal_id, ==, "terminal-1");
	g_assert_cmpstr(request.page_id, ==, "page-1");
	g_assert_cmpstr(request.group_id, ==, "group-1");
	g_assert_cmpstr(request.task_id, ==, "task-1");
	g_assert_cmpstr(request.cwd, ==, "/tmp");
	g_assert_cmpuint(request.cols, ==, 120);
	g_assert_cmpuint(request.rows, ==, 50);
	g_assert_cmpuint(request.terminal_kind, ==, SAKURA_TAB_CODEX);
	g_assert_cmpstr(request.resume_session_id, ==, "resume-restart");
	g_assert_cmpstr(request.model, ==, "gpt-5.6-luna");
	g_assert_cmpstr(request.reasoning_effort, ==, "high");
	g_assert_cmpstr(request.tracking_token, ==, "tracking-restart");
	g_assert_true(request.has_order);
	g_assert_cmpuint(request.order, ==, 9);
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
test_agent_start_with_workspace(const gchar *socket_path,
	                            const gchar *session_path,
	                            const gchar *workspace_path)
{
	GSubprocess *process;
	GError *error = NULL;
	GSocketConnection *connection;
	SakuraSessionSnapshot *snapshot;

	if (!g_file_test(session_path, G_FILE_TEST_IS_REGULAR)) {
		snapshot = sakura_session_snapshot_new();
		test_save_agent_session(session_path, snapshot);
		sakura_session_snapshot_free(snapshot);
	}
	snapshot = test_load_agent_session(session_path);
	g_free(test_agent_workspace_id);
	test_agent_workspace_id = g_strdup(snapshot->workspace_id);
	sakura_session_snapshot_free(snapshot);

#ifdef SAKURA_AGENT_BUILD_PATH
	if (workspace_path != NULL)
		process = g_subprocess_new(
			G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
			&error, SAKURA_AGENT_BUILD_PATH, "--socket", socket_path,
			"--workspace-id", test_agent_workspace_id, "--session", session_path,
			"--workspace-file", workspace_path, NULL);
	else
		process = g_subprocess_new(
			G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
			&error, SAKURA_AGENT_BUILD_PATH, "--socket", socket_path,
			"--workspace-id", test_agent_workspace_id, "--session", session_path, NULL);
#else
	if (workspace_path != NULL)
		process = g_subprocess_new(
			G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
			&error, "sakura-agent", "--socket", socket_path,
			"--workspace-id", test_agent_workspace_id, "--session", session_path,
			"--workspace-file", workspace_path, NULL);
	else
		process = g_subprocess_new(
			G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
			&error, "sakura-agent", "--socket", socket_path,
			"--workspace-id", test_agent_workspace_id, "--session", session_path,
			NULL);
#endif
	g_assert_no_error(error);
	g_assert_nonnull(process);
	connection = test_agent_connect_wait(socket_path);
	test_agent_handshake_on_connection(connection);
	g_io_stream_close(G_IO_STREAM(connection), NULL, NULL);
	g_object_unref(connection);
	return process;
}


static GSubprocess *
test_agent_start(const gchar *socket_path, const gchar *session_path)
{
	return test_agent_start_with_workspace(socket_path, session_path, NULL);
}


static void
test_agent_stop(GSubprocess *process)
{
	GError *error = NULL;

	g_subprocess_send_signal(process, SIGTERM);
	g_assert_true(g_subprocess_wait(process, NULL, &error));
	g_assert_no_error(error);
	g_assert_true(g_subprocess_get_successful(process));
	g_object_unref(process);
}


static void
test_agent_shutdown_with_active_subscriber(void)
{
	g_autofree gchar *directory = NULL;
	g_autofree gchar *socket_path = NULL;
	g_autofree gchar *session_path = NULL;
	GSubprocess *process;
	GSocketConnection *subscriber;
	GByteArray *request = g_byte_array_new();
	GByteArray *response = NULL;
	GError *error = NULL;

	directory = g_dir_make_tmp("sakura-agent-shutdown-XXXXXX", &error);
	g_assert_no_error(error);
	g_assert_nonnull(directory);
	socket_path = g_build_filename(directory, "agent.sock", NULL);
	session_path = g_build_filename(directory, "session", NULL);
	process = test_agent_start(socket_path, session_path);
	subscriber = test_agent_connect_wait(socket_path);
	test_agent_handshake_on_connection(subscriber);
	g_assert_true(sakura_control_encode_subscribe_events_request(
		"shutdown-subscribe", 0, request));
	g_assert_true(sakura_control_frame_write(
		g_io_stream_get_output_stream(G_IO_STREAM(subscriber)), request->data,
		request->len, NULL, &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_frame_read(
		g_io_stream_get_input_stream(G_IO_STREAM(subscriber)), &response, NULL,
		&error));
	g_assert_no_error(error);
	g_clear_pointer(&response, g_byte_array_unref);
	g_assert_true(sakura_control_frame_read(
		g_io_stream_get_input_stream(G_IO_STREAM(subscriber)), &response, NULL,
		&error));
	g_assert_no_error(error);
	g_clear_pointer(&response, g_byte_array_unref);

	test_agent_stop(process);
	g_io_stream_close(G_IO_STREAM(subscriber), NULL, NULL);
	g_object_unref(subscriber);
	g_byte_array_unref(request);
	g_remove(session_path);
	g_remove(socket_path);
	g_rmdir(directory);
}


static void
test_agent_workspace_migration_and_recovery(void)
{
	g_autofree gchar *directory = NULL;
	g_autofree gchar *socket_path = NULL;
	g_autofree gchar *session_path = NULL;
	g_autofree gchar *workspace_path = NULL;
	g_autofree gchar *backup_path = NULL;
	SakuraSessionSnapshot *legacy;
	SakuraSessionSnapshot *loaded;
	SakuraSessionGroupRecord *group;
	GSubprocess *process;
	GError *error = NULL;

	directory = g_dir_make_tmp("sakura-agent-recovery-XXXXXX", &error);
	g_assert_no_error(error);
	socket_path = g_build_filename(directory, "agent.sock", NULL);
	session_path = g_build_filename(directory, "desktop.session", NULL);
	workspace_path = g_build_filename(directory, "agent.workspace", NULL);
	backup_path = g_strdup_printf("%s.bak", workspace_path);
	legacy = sakura_session_snapshot_new();
	group = g_new0(SakuraSessionGroupRecord, 1);
	group->id = g_strdup("group-migrated");
	group->parent_id = g_strdup("root");
	group->title = g_strdup("Migrated workspace");
	g_ptr_array_add(legacy->groups, group);
	test_save_agent_session(session_path, legacy);
	sakura_session_snapshot_free(legacy);

	/* Missing agent state migrates once from the legacy desktop snapshot. */
	process = test_agent_start_with_workspace(
		socket_path, session_path, workspace_path);
	test_agent_stop(process);
	g_assert_true(g_file_test(workspace_path, G_FILE_TEST_IS_REGULAR));
	loaded = test_load_agent_session(workspace_path);
	g_assert_cmpuint(loaded->groups->len, ==, 1);
	group = g_ptr_array_index(loaded->groups, 0);
	g_assert_cmpstr(group->id, ==, "group-migrated");
	sakura_session_snapshot_free(loaded);

	/* A second atomic write retains a known-good recovery snapshot. */
	process = test_agent_start_with_workspace(
		socket_path, session_path, workspace_path);
	test_agent_stop(process);
	g_assert_true(g_file_test(backup_path, G_FILE_TEST_IS_REGULAR));

	/* A truncated primary recovers from the agent backup, not desktop state. */
	g_assert_true(g_file_set_contents(workspace_path, "[broken", -1, &error));
	g_assert_no_error(error);
	process = test_agent_start_with_workspace(
		socket_path, session_path, workspace_path);
	test_agent_stop(process);
	loaded = test_load_agent_session(workspace_path);
	g_assert_cmpuint(loaded->groups->len, ==, 1);
	group = g_ptr_array_index(loaded->groups, 0);
	g_assert_cmpstr(group->id, ==, "group-migrated");
	sakura_session_snapshot_free(loaded);
	loaded = test_load_agent_session(backup_path);
	g_assert_cmpuint(loaded->groups->len, ==, 1);
	sakura_session_snapshot_free(loaded);

	/* With no agent state at all, the legacy migration remains available. */
	g_remove(workspace_path);
	g_remove(backup_path);
	process = test_agent_start_with_workspace(
		socket_path, session_path, workspace_path);
	test_agent_stop(process);
	loaded = test_load_agent_session(workspace_path);
	g_assert_cmpuint(loaded->groups->len, ==, 1);
	sakura_session_snapshot_free(loaded);

	g_remove(backup_path);
	g_remove(workspace_path);
	g_remove(session_path);
	g_remove(socket_path);
	g_rmdir(directory);
}


static void
test_agent_handshake_on_connection(GSocketConnection *connection)
{
	GInputStream *input;
	GOutputStream *output;
	GError *error = NULL;
	GByteArray *hello_payload = g_byte_array_new();
	GByteArray *hello_response_payload = NULL;
	SakuraControlResponse hello_response = { 0 };

	input = g_io_stream_get_input_stream(G_IO_STREAM(connection));
	output = g_io_stream_get_output_stream(G_IO_STREAM(connection));
	g_assert_true(sakura_control_encode_hello_request(
		"test-hello", SAKURA_CONTROL_PROTOCOL_VERSION, "sakura-test",
		test_agent_workspace_id,
		hello_payload));
	g_assert_true(sakura_control_frame_write(output, hello_payload->data,
	                                         hello_payload->len, NULL, &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_frame_read(input, &hello_response_payload, NULL,
	                                        &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_decode_response(
		hello_response_payload->data, hello_response_payload->len,
		&hello_response, &error));
	g_assert_no_error(error);
	g_assert_true(hello_response.hello);
	g_assert_cmpstr(hello_response.workspace_id, ==, test_agent_workspace_id);
	sakura_control_response_clear(&hello_response);
	g_clear_pointer(&hello_response_payload, g_byte_array_unref);
	g_byte_array_unref(hello_payload);
}


static void
test_agent_rejects_wrong_workspace(void)
{
	GSubprocess *process;
	GSocketConnection *connection;
	GByteArray *hello_payload = g_byte_array_new();
	GByteArray *response_payload = NULL;
	SakuraControlResponse response = { 0 };
	GError *error = NULL;
	gchar *directory;
	gchar *socket_path;
	gchar *session_path;

	directory = g_dir_make_tmp("sakura-agent-identity-XXXXXX", &error);
	g_assert_no_error(error);
	socket_path = g_build_filename(directory, "agent.sock", NULL);
	session_path = g_build_filename(directory, "workspace.session", NULL);
	process = test_agent_start(socket_path, session_path);
	connection = test_agent_connect_wait(socket_path);
	g_assert_true(sakura_control_encode_hello_request(
		"wrong-hello", SAKURA_CONTROL_PROTOCOL_VERSION, "sakura-test",
		"wrong-workspace", hello_payload));
	g_assert_true(sakura_control_frame_write(
		g_io_stream_get_output_stream(G_IO_STREAM(connection)),
		hello_payload->data, hello_payload->len, NULL, &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_frame_read(
		g_io_stream_get_input_stream(G_IO_STREAM(connection)),
		&response_payload, NULL, &error));
	g_assert_no_error(error);
	g_assert_false(sakura_control_decode_response(
		response_payload->data, response_payload->len, &response, &error));
	g_assert_error(error, SAKURA_CONTROL_ERROR_DOMAIN,
	               SAKURA_CONTROL_ERROR_CODE_UNAUTHORIZED);
	g_assert_true(response.has_error);
	g_assert_cmpstr(response.error_code, ==, SAKURA_CONTROL_ERROR_UNAUTHORIZED);
	g_clear_error(&error);
	sakura_control_response_clear(&response);
	g_clear_pointer(&response_payload, g_byte_array_unref);
	g_byte_array_unref(hello_payload);
	g_io_stream_close(G_IO_STREAM(connection), NULL, NULL);
	g_object_unref(connection);
	test_agent_stop(process);
	g_remove(session_path);
	g_remove(socket_path);
	g_rmdir(directory);
	g_free(session_path);
	g_free(socket_path);
	g_free(directory);
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
	test_agent_handshake_on_connection(connection);

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
	g_assert_true(response->has_snapshot || response->accepted ||
	              response->attached || response->has_file_list ||
	              response->has_file_read || response->has_file_write);
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
test_agent_rejects_stale_revision(void)
{
	GSubprocess *process;
	GSocketConnection *connection;
	GByteArray *request = g_byte_array_new();
	GByteArray *response_payload = NULL;
	SakuraControlResponse response = { 0 };
	GError *error = NULL;
	gchar *directory;
	gchar *socket_path;
	gchar *session_path;

	directory = g_dir_make_tmp("sakura-agent-revision-XXXXXX", &error);
	g_assert_no_error(error);
	socket_path = g_build_filename(directory, "agent.sock", NULL);
	session_path = g_build_filename(directory, "workspace.session", NULL);
	process = test_agent_start(socket_path, session_path);

	g_assert_true(sakura_control_encode_create_group_request(
		"revision-current", "root", "Current", "", request));
	g_assert_true(sakura_control_request_set_expected_revision(request, 0));
	test_agent_call(socket_path, "revision-current", request, &response);
	g_assert_true(response.has_snapshot);
	g_assert_cmpuint(response.workspace_revision, ==, 1);
	sakura_control_response_clear(&response);
	g_byte_array_set_size(request, 0);

	g_assert_true(sakura_control_encode_create_group_request(
		"revision-stale", "root", "Stale", "", request));
	g_assert_true(sakura_control_request_set_expected_revision(request, 0));
	connection = test_agent_connect_wait(socket_path);
	test_agent_handshake_on_connection(connection);
	g_assert_true(sakura_control_frame_write(
		g_io_stream_get_output_stream(G_IO_STREAM(connection)), request->data,
		request->len, NULL, &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_frame_read(
		g_io_stream_get_input_stream(G_IO_STREAM(connection)),
		&response_payload, NULL, &error));
	g_assert_no_error(error);
	g_assert_false(sakura_control_decode_response(
		response_payload->data, response_payload->len, &response, &error));
	g_assert_error(error, SAKURA_CONTROL_ERROR_DOMAIN,
	               SAKURA_CONTROL_ERROR_CODE_REVISION_CONFLICT);
	g_assert_true(response.has_error);
	g_assert_cmpstr(response.error_code, ==,
	                SAKURA_CONTROL_ERROR_REVISION_CONFLICT);
	g_assert_cmpuint(response.error_current_revision, ==, 1);
	g_assert_true(response.error_retryable);
	g_clear_error(&error);
	sakura_control_response_clear(&response);
	g_clear_pointer(&response_payload, g_byte_array_unref);
	g_io_stream_close(G_IO_STREAM(connection), NULL, NULL);
	g_object_unref(connection);
	test_agent_stop(process);
	g_byte_array_unref(request);
	g_remove(session_path);
	g_remove(socket_path);
	g_rmdir(directory);
	g_free(session_path);
	g_free(socket_path);
	g_free(directory);
}


static void
test_agent_filesystem_roundtrip(void)
{
	GSubprocess *process;
	GSocketConnection *connection;
	GByteArray *request = g_byte_array_new();
	GByteArray *response_payload = NULL;
	SakuraControlResponse response = { 0 };
	SakuraSessionSnapshot *snapshot;
	SakuraControlFileEntry *entry;
	GError *error = NULL;
	gchar *directory;
	gchar *socket_path;
	gchar *session_path;
	gchar *file_path;
	gchar *version = NULL;

	directory = g_dir_make_tmp("sakura-agent-files-XXXXXX", &error);
	g_assert_no_error(error);
	socket_path = g_build_filename(directory, "agent.sock", NULL);
	session_path = g_build_filename(directory, "workspace.session", NULL);
	file_path = g_build_filename(directory, "remote.txt", NULL);
	snapshot = sakura_session_snapshot_new();
	g_free(snapshot->root_directory);
	snapshot->root_directory = g_strdup(directory);
	test_save_agent_session(session_path, snapshot);
	sakura_session_snapshot_free(snapshot);
	g_assert_true(g_file_set_contents(file_path, "hello", -1, &error));
	g_assert_no_error(error);
	process = test_agent_start(socket_path, session_path);

	g_assert_true(sakura_control_encode_list_files_request(
		"files-list", "root", "", request));
	test_agent_call(socket_path, "files-list", request, &response);
	g_assert_true(response.has_file_list);
	g_assert_nonnull(response.file_entries);
	g_assert_cmpuint(response.file_entries->len, >=, 1);
	entry = NULL;
	for (guint index = 0; index < response.file_entries->len; index++) {
		SakuraControlFileEntry *candidate = g_ptr_array_index(
			response.file_entries, index);
		if (g_strcmp0(candidate->name, "remote.txt") == 0) {
			entry = candidate;
			break;
		}
	}
	g_assert_nonnull(entry);
	g_assert_cmpstr(entry->name, ==, "remote.txt");
	sakura_control_response_clear(&response);
	g_byte_array_set_size(request, 0);

	g_assert_true(sakura_control_encode_read_file_request(
		"files-read", "root", "remote.txt", 0, 256, TRUE, NULL, request));
	test_agent_call(socket_path, "files-read", request, &response);
	g_assert_true(response.has_file_read);
	g_assert_cmpmem(response.file_data, response.file_data_length,
	                "hello", 5);
	g_assert_true(response.file_eof);
	version = g_strdup(response.file_version);
	sakura_control_response_clear(&response);
	g_byte_array_set_size(request, 0);

	g_assert_true(sakura_control_encode_write_file_request(
		"files-write", "root", "remote.txt", (const guint8 *)"hello world",
		11, version, TRUE, request));
	test_agent_call(socket_path, "files-write", request, &response);
	g_assert_true(response.has_file_write);
	g_assert_cmpstr(response.file_version, !=, version);
	sakura_control_response_clear(&response);
	g_byte_array_set_size(request, 0);

	g_assert_true(sakura_control_encode_read_file_request(
		"files-stale-read", "root", "remote.txt", 0, 256, TRUE, version,
		request));
	connection = test_agent_connect_wait(socket_path);
	test_agent_handshake_on_connection(connection);
	g_assert_true(sakura_control_frame_write(
		g_io_stream_get_output_stream(G_IO_STREAM(connection)), request->data,
		request->len, NULL, &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_frame_read(
		g_io_stream_get_input_stream(G_IO_STREAM(connection)), &response_payload,
		NULL, &error));
	g_assert_no_error(error);
	g_assert_false(sakura_control_decode_response(
		response_payload->data, response_payload->len, &response, &error));
	g_assert_error(error, SAKURA_CONTROL_ERROR_DOMAIN,
	               SAKURA_CONTROL_ERROR_CODE_REVISION_CONFLICT);
	g_clear_error(&error);
	sakura_control_response_clear(&response);
	g_clear_pointer(&response_payload, g_byte_array_unref);
	g_io_stream_close(G_IO_STREAM(connection), NULL, NULL);
	g_object_unref(connection);
	g_byte_array_set_size(request, 0);

	g_assert_true(sakura_control_encode_list_files_request(
		"files-escape", "root", "../", request));
	connection = test_agent_connect_wait(socket_path);
	test_agent_handshake_on_connection(connection);
	g_assert_true(sakura_control_frame_write(
		g_io_stream_get_output_stream(G_IO_STREAM(connection)), request->data,
		request->len, NULL, &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_frame_read(
		g_io_stream_get_input_stream(G_IO_STREAM(connection)), &response_payload,
		NULL, &error));
	g_assert_no_error(error);
	g_assert_false(sakura_control_decode_response(
		response_payload->data, response_payload->len, &response, &error));
	g_assert_error(error, SAKURA_CONTROL_ERROR_DOMAIN,
	               SAKURA_CONTROL_ERROR_CODE_UNAUTHORIZED);
	g_clear_error(&error);
	sakura_control_response_clear(&response);
	g_clear_pointer(&response_payload, g_byte_array_unref);
	g_io_stream_close(G_IO_STREAM(connection), NULL, NULL);
	g_object_unref(connection);

	g_byte_array_unref(request);
	g_free(version);
	test_agent_stop(process);
	g_remove(file_path);
	g_remove(session_path);
	g_remove(socket_path);
	g_rmdir(directory);
	g_free(file_path);
	g_free(session_path);
	g_free(socket_path);
	g_free(directory);
}


static void
test_agent_terminal_create_rollback(void)
{
	GSubprocess *process;
	GSocketConnection *connection;
	GByteArray *request = g_byte_array_new();
	GByteArray *response_payload = NULL;
	SakuraControlResponse response = { 0 };
	SakuraSessionSnapshot *snapshot = NULL;
	GError *error = NULL;
	gchar *directory;
	gchar *socket_path;
	gchar *session_path;
	guint64 sequence = 0;

	directory = g_dir_make_tmp("sakura-agent-terminal-rollback-XXXXXX", &error);
	g_assert_no_error(error);
	socket_path = g_build_filename(directory, "agent.sock", NULL);
	session_path = g_build_filename(directory, "workspace.session", NULL);
	process = test_agent_start(socket_path, session_path);
	connection = test_agent_connect_wait(socket_path);
	test_agent_handshake_on_connection(connection);

	g_assert_true(sakura_control_encode_create_terminal_request_with_page(
		"rollback-create", "rollback-terminal", "rollback-page", "root", "root",
		"/path/that/does/not/exist", 80, 24, request));
	g_assert_true(sakura_control_frame_write(
		g_io_stream_get_output_stream(G_IO_STREAM(connection)), request->data,
		request->len, NULL, &error));
	g_assert_true(sakura_control_frame_read(
		g_io_stream_get_input_stream(G_IO_STREAM(connection)),
		&response_payload, NULL, &error));
	g_assert_false(sakura_control_decode_response(
		response_payload->data, response_payload->len, &response, &error));
	g_assert_error(error, SAKURA_CONTROL_ERROR_DOMAIN,
	               SAKURA_CONTROL_ERROR_CODE_INVALID_ARGUMENT);
	g_clear_error(&error);
	sakura_control_response_clear(&response);
	g_clear_pointer(&response_payload, g_byte_array_unref);

	g_byte_array_set_size(request, 0);
	g_assert_true(sakura_control_encode_get_snapshot_request(
		"rollback-snapshot", request));
	g_assert_true(sakura_control_frame_write(
		g_io_stream_get_output_stream(G_IO_STREAM(connection)), request->data,
		request->len, NULL, &error));
	g_assert_true(sakura_control_frame_read(
		g_io_stream_get_input_stream(G_IO_STREAM(connection)),
		&response_payload, NULL, &error));
	g_assert_true(sakura_control_decode_response(
		response_payload->data, response_payload->len, &response, &error));
	g_assert_true(response.has_snapshot);
	g_assert_true(sakura_control_decode_snapshot_response(
		response_payload->data, response_payload->len, &sequence, &snapshot,
		&error));
	g_assert_cmpuint(snapshot->tabs->len, ==, 0);
	g_assert_cmpuint(snapshot->pages->len, ==, 0);

	sakura_session_snapshot_free(snapshot);
	sakura_control_response_clear(&response);
	g_clear_pointer(&response_payload, g_byte_array_unref);
	g_io_stream_close(G_IO_STREAM(connection), NULL, NULL);
	g_object_unref(connection);
	test_agent_stop(process);
	g_byte_array_unref(request);
	g_remove(session_path);
	g_remove(socket_path);
	g_rmdir(directory);
	g_free(session_path);
	g_free(socket_path);
	g_free(directory);
}


static void
test_agent_slow_subscriber_disconnects(void)
{
	GSubprocess *process;
	GSocketConnection *command_connection;
	GSocketConnection *subscriber;
	GByteArray *request = g_byte_array_new();
	GByteArray *response_payload = NULL;
	SakuraControlResponse response = { 0 };
	GError *error = NULL;
	gchar *directory;
	gchar *socket_path;
	gchar *session_path;
	gboolean subscriber_closed = FALSE;

	directory = g_dir_make_tmp("sakura-agent-slow-subscriber-XXXXXX", &error);
	g_assert_no_error(error);
	socket_path = g_build_filename(directory, "agent.sock", NULL);
	session_path = g_build_filename(directory, "workspace.session", NULL);
	process = test_agent_start(socket_path, session_path);

	/* Complete the subscription handshake, then deliberately leave its event
	 * stream unread while mutations fill the bounded outbound queue. */
	subscriber = test_agent_connect_wait(socket_path);
	test_agent_handshake_on_connection(subscriber);
	g_assert_true(sakura_control_encode_subscribe_events_request(
		"slow-subscribe", 0, request));
	g_assert_true(sakura_control_frame_write(
		g_io_stream_get_output_stream(G_IO_STREAM(subscriber)), request->data,
		request->len, NULL, &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_frame_read(
		g_io_stream_get_input_stream(G_IO_STREAM(subscriber)),
		&response_payload, NULL, &error));
	g_assert_true(sakura_control_decode_response(
		response_payload->data, response_payload->len, &response, &error));
	g_assert_true(response.accepted);
	sakura_control_response_clear(&response);
	g_clear_pointer(&response_payload, g_byte_array_unref);

	command_connection = test_agent_connect_wait(socket_path);
	test_agent_handshake_on_connection(command_connection);
	for (guint index = 0; index < 800; index++) {
		gchar *request_id = g_strdup_printf("slow-group-%u", index);

		g_byte_array_set_size(request, 0);
		g_assert_true(sakura_control_encode_create_group_request(
			request_id, "root", request_id, "", request));
		g_assert_true(sakura_control_frame_write(
			g_io_stream_get_output_stream(G_IO_STREAM(command_connection)),
			request->data, request->len, NULL, &error));
		g_assert_true(sakura_control_frame_read(
			g_io_stream_get_input_stream(G_IO_STREAM(command_connection)),
			&response_payload, NULL, &error));
		g_assert_true(sakura_control_decode_response(
			response_payload->data, response_payload->len, &response, &error));
		g_assert_true(response.has_snapshot);
		sakura_control_response_clear(&response);
		g_clear_pointer(&response_payload, g_byte_array_unref);
		g_free(request_id);
	}

	/* The reader must wake once overflow shuts down both socket directions;
	 * otherwise the subscriber thread remains registered indefinitely. */
	g_socket_set_timeout(g_socket_connection_get_socket(subscriber), 1);
	for (guint attempt = 0; attempt < 1200 && !subscriber_closed; attempt++) {
		if (!sakura_control_frame_read(
				g_io_stream_get_input_stream(G_IO_STREAM(subscriber)),
				&response_payload, NULL, &error)) {
			subscriber_closed = TRUE;
			g_clear_error(&error);
		} else {
			g_clear_pointer(&response_payload, g_byte_array_unref);
		}
	}
	g_assert_true(subscriber_closed);

	g_io_stream_close(G_IO_STREAM(command_connection), NULL, NULL);
	g_object_unref(command_connection);
	g_io_stream_close(G_IO_STREAM(subscriber), NULL, NULL);
	g_object_unref(subscriber);
	test_agent_stop(process);
	g_byte_array_unref(request);
	g_remove(session_path);
	g_remove(socket_path);
	g_rmdir(directory);
	g_free(session_path);
	g_free(socket_path);
	g_free(directory);
}


static void
test_agent_set_event_timeout(GSocketConnection *connection)
{
	g_socket_set_timeout(g_socket_connection_get_socket(connection), 10);
}


static gboolean
test_agent_read_workspace_until(GInputStream *input, gsize terminal_count,
	                              const gchar *terminal_id,
	                              guint expected_cols, guint expected_rows,
	                              const gchar *expected_page_id)
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
			if (expected_page_id != NULL) {
				gboolean page_found = FALSE;

				for (gsize index = 0; index < snapshot->n_pages; index++) {
					if (g_strcmp0(snapshot->pages[index]->id,
					             expected_page_id) == 0) {
						page_found = TRUE;
						break;
					}
				}
				found = found && page_found;
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
test_agent_read_page_state_until(GInputStream *input, const gchar *page_id,
                                 const gchar *title, gboolean title_set_by_user,
                                 gboolean archived)
{
	for (guint attempt = 0; attempt < 100; attempt++) {
		GByteArray *payload = NULL;
		SakuraSessionSnapshot *snapshot = NULL;
		guint64 sequence;
		GError *error = NULL;

		if (!sakura_control_frame_read(input, &payload, NULL, &error)) {
			g_clear_error(&error);
			return FALSE;
		}
		if (sakura_control_decode_workspace_changed_event(
				payload->data, payload->len, &sequence, &snapshot, &error)) {
			for (guint index = 0; snapshot->pages != NULL &&
			                    index < snapshot->pages->len; index++) {
				SakuraSessionPageRecord *page =
					g_ptr_array_index(snapshot->pages, index);

				if (page != NULL && g_strcmp0(page->id, page_id) == 0 &&
				    g_strcmp0(page->title, title) == 0 &&
				    page->title_set_by_user == title_set_by_user &&
				    page->archived == archived) {
					sakura_session_snapshot_free(snapshot);
					g_byte_array_unref(payload);
					return TRUE;
				}
			}
		}
		g_clear_error(&error);
		sakura_session_snapshot_free(snapshot);
		g_byte_array_unref(payload);
	}
	return FALSE;
}


static gboolean
test_agent_read_page_count_until(GInputStream *input, gsize page_count)
{
	for (guint attempt = 0; attempt < 100; attempt++) {
		GByteArray *payload = NULL;
		SakuraSessionSnapshot *snapshot = NULL;
		guint64 sequence;
		GError *error = NULL;

		if (!sakura_control_frame_read(input, &payload, NULL, &error)) {
			g_clear_error(&error);
			return FALSE;
		}
		if (sakura_control_decode_workspace_changed_event(
				payload->data, payload->len, &sequence, &snapshot, &error) &&
		    snapshot->pages != NULL && snapshot->pages->len == page_count) {
			sakura_session_snapshot_free(snapshot);
			g_byte_array_unref(payload);
			return TRUE;
		}
		g_clear_error(&error);
		sakura_session_snapshot_free(snapshot);
		g_byte_array_unref(payload);
	}
	return FALSE;
}


static gboolean
test_byte_array_contains_string(const GByteArray *bytes, const gchar *needle)
{
	gsize needle_length;

	if (bytes == NULL || needle == NULL)
		return FALSE;
	needle_length = strlen(needle);
	if (needle_length == 0)
		return TRUE;
	if (bytes->len < needle_length)
		return FALSE;
	for (gsize index = 0; index + needle_length <= bytes->len; index++) {
		if (memcmp(bytes->data + index, needle, needle_length) == 0)
			return TRUE;
	}
	return FALSE;
}


static gboolean
test_agent_read_output_until(GInputStream *input, const gchar *terminal_id,
	                           const gchar *needle)
{
	GByteArray *collected = g_byte_array_new();
	gboolean found = FALSE;

	for (guint attempt = 0; attempt < 3000; attempt++) {
		GByteArray *payload = NULL;
		guint64 sequence;
		gchar *event_terminal_id = NULL;
		guint8 *data = NULL;
		gsize data_length = 0;
		gboolean final_chunk = FALSE;
		GError *error = NULL;

		if (!sakura_control_frame_read(input, &payload, NULL, &error)) {
			g_clear_error(&error);
			break;
		}
		if (sakura_control_decode_terminal_output_event(
			payload->data, payload->len, &sequence, &event_terminal_id,
			&data, &data_length, &final_chunk, &error)) {
			if (data != NULL && g_strcmp0(event_terminal_id, terminal_id) == 0) {
				g_byte_array_append(collected, data, data_length);
				if (test_byte_array_contains_string(collected, needle)) {
					found = TRUE;
					g_free(event_terminal_id);
					g_free(data);
					g_byte_array_unref(payload);
					break;
				}
			}
			g_free(event_terminal_id);
			g_free(data);
		} else {
			g_clear_error(&error);
		}
		g_byte_array_unref(payload);
	}
	g_byte_array_unref(collected);
	return found;
}


static gboolean
test_agent_read_output_end_until(GInputStream *input, const gchar *terminal_id,
	                               const gchar *needle, guint64 *end_offset)
{
	GByteArray *collected = g_byte_array_new();
	gboolean found = FALSE;

	if (end_offset != NULL)
		*end_offset = 0;
	for (guint attempt = 0; attempt < 3000; attempt++) {
		GByteArray *payload = NULL;
		guint64 sequence = 0;
		guint64 start_offset = 0;
		guint64 event_end_offset = 0;
		gchar *event_terminal_id = NULL;
		guint8 *data = NULL;
		gsize data_length = 0;
		gboolean final_chunk = FALSE;
		GError *error = NULL;

		if (!sakura_control_frame_read(input, &payload, NULL, &error)) {
			g_clear_error(&error);
			break;
		}
		if (sakura_control_decode_terminal_output_event_with_offsets(
				payload->data, payload->len, &sequence, &event_terminal_id,
				&start_offset, &event_end_offset, &data, &data_length,
				&final_chunk, &error)) {
			if (g_strcmp0(event_terminal_id, terminal_id) == 0) {
				g_byte_array_append(collected, data, data_length);
				if (test_byte_array_contains_string(collected, needle)) {
					if (end_offset != NULL)
						*end_offset = event_end_offset;
					found = TRUE;
				}
			}
		} else {
			g_clear_error(&error);
		}
		g_free(event_terminal_id);
		g_free(data);
		g_byte_array_unref(payload);
		if (found)
			break;
	}
	g_byte_array_unref(collected);
	return found;
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
test_save_agent_session(const gchar *session_path,
                        const SakuraSessionSnapshot *snapshot)
{
	GKeyFile *key_file;
	gchar *data;
	gsize data_length;
	GError *error = NULL;

	key_file = g_key_file_new();
	sakura_session_snapshot_save(snapshot, key_file);
	data = g_key_file_to_data(key_file, &data_length, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);
	g_assert_true(g_file_set_contents(session_path, data, data_length, &error));
	g_assert_no_error(error);
	g_free(data);
	g_key_file_free(key_file);
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
	SakuraSessionPageRecord *page_record;
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
	gchar *workspace_path;
	gchar *group_id;
	gchar *second_group_id = NULL;
	gchar *task_id = NULL;

	directory = g_dir_make_tmp("sakura-agent-test-XXXXXX", &error);
	g_assert_no_error(error);
	g_assert_nonnull(directory);
	socket_path = g_build_filename(directory, "agent.sock", NULL);
	session_path = g_build_filename(directory, "workspace.session", NULL);
	workspace_path = g_build_filename(directory, "workspace.state", NULL);
	process = test_agent_start_with_workspace(socket_path, session_path,
	                                          workspace_path);
	subscriber = test_agent_connect_wait(socket_path);
	subscriber_input = g_io_stream_get_input_stream(G_IO_STREAM(subscriber));
	subscriber_output = g_io_stream_get_output_stream(G_IO_STREAM(subscriber));
	test_agent_handshake_on_connection(subscriber);
	test_agent_set_event_timeout(subscriber);
	g_assert_true(sakura_control_encode_subscribe_events_request(
		"initial-subscribe", 0, request));
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
	g_assert_cmpuint(event_snapshot->groups->len, ==, 0);
	sakura_session_snapshot_free(event_snapshot);
	event_snapshot = NULL;
	g_clear_pointer(&event_payload, g_byte_array_unref);
	g_byte_array_set_size(request, 0);

	g_assert_true(sakura_control_encode_create_group_request(
		"create-group", "root", "Projects", "/tmp/projects", request));
	test_agent_call(socket_path, "create-group", request, &response);
	sakura_control_response_clear(&response);
	g_assert_true(sakura_control_frame_read(subscriber_input, &event_payload,
	                                        NULL, &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_decode_workspace_changed_event(
		event_payload->data, event_payload->len, &event_sequence,
		&event_snapshot, &error));
	g_assert_no_error(error);
	g_assert_cmpuint(event_sequence, ==, 1);
	g_assert_cmpuint(event_snapshot->groups->len, ==, 1);
	snapshot = test_load_agent_session(workspace_path);
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
	g_clear_pointer(&event_payload, g_byte_array_unref);
	sakura_session_snapshot_free(event_snapshot);
	event_snapshot = NULL;
	g_assert_true(sakura_control_frame_read(subscriber_input, &event_payload,
	                                        NULL, &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_decode_workspace_changed_event(
		event_payload->data, event_payload->len, &event_sequence,
		&event_snapshot, &error));
	g_assert_no_error(error);
	g_assert_cmpuint(event_sequence, ==, 2);
	g_assert_cmpuint(event_snapshot->tasks->len, ==, 1);
	snapshot = test_load_agent_session(workspace_path);
	g_assert_cmpuint(snapshot->groups->len, ==, 1);
	g_assert_cmpuint(snapshot->tasks->len, ==, 1);
	task = g_ptr_array_index(snapshot->tasks, 0);
	g_assert_cmpstr(task->title, ==, "Build");
	g_assert_cmpstr(task->group_id, ==, group_id);
	task_id = g_strdup(task->id);
	sakura_session_snapshot_free(snapshot);
	g_byte_array_set_size(request, 0);
	g_assert_true(sakura_control_encode_update_page_request(
		"create-page", "page-restart", group_id, task_id, "Restored page",
		TRUE, FALSE, request));
	test_agent_call(socket_path, "create-page", request, &response);
	sakura_control_response_clear(&response);
	g_clear_pointer(&event_payload, g_byte_array_unref);
	sakura_session_snapshot_free(event_snapshot);
	event_snapshot = NULL;
	g_assert_true(sakura_control_frame_read(subscriber_input, &event_payload,
	                                        NULL, &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_decode_workspace_changed_event(
		event_payload->data, event_payload->len, &event_sequence,
		&event_snapshot, &error));
	g_assert_no_error(error);
	g_assert_cmpuint(event_sequence, ==, 3);
	g_assert_cmpuint(event_snapshot->pages->len, ==, 1);
	page_record = g_ptr_array_index(event_snapshot->pages, 0);
	g_assert_cmpstr(page_record->id, ==, "page-restart");
	g_assert_cmpstr(page_record->group_id, ==, group_id);
	g_assert_cmpstr(page_record->task_id, ==, task_id);
	g_assert_cmpstr(page_record->title, ==, "Restored page");

	/* Re-sending identical page metadata must not advance the workspace or
	 * enqueue another full snapshot for subscribers. */
	g_byte_array_set_size(request, 0);
	g_assert_true(sakura_control_encode_update_page_request(
		"unchanged-page", "page-restart", group_id, task_id, "Restored page",
		TRUE, FALSE, request));
	test_agent_call(socket_path, "unchanged-page", request, &response);
	g_assert_cmpuint(response.workspace_revision, ==, 3);
	sakura_control_response_clear(&response);

	g_byte_array_set_size(request, 0);
	g_assert_true(sakura_control_encode_set_group_archived_request(
		"archive-projects", group_id, TRUE, request));
	test_agent_call(socket_path, "archive-projects", request, &response);
	sakura_control_response_clear(&response);
	g_clear_pointer(&event_payload, g_byte_array_unref);
	sakura_session_snapshot_free(event_snapshot);
	event_snapshot = NULL;
	g_assert_true(sakura_control_frame_read(subscriber_input, &event_payload,
	                                        NULL, &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_decode_workspace_changed_event(
		event_payload->data, event_payload->len, &event_sequence,
		&event_snapshot, &error));
	g_assert_no_error(error);
	g_assert_cmpuint(event_sequence, ==, 4);
	group = g_ptr_array_index(event_snapshot->groups, 0);
	g_assert_true(group->archived);
	page_record = g_ptr_array_index(event_snapshot->pages, 0);
	g_assert_cmpstr(page_record->group_id, ==, group_id);
	g_assert_cmpstr(page_record->task_id, ==, task_id);
	g_clear_pointer(&event_payload, g_byte_array_unref);
	sakura_session_snapshot_free(event_snapshot);
	event_snapshot = NULL;
	g_io_stream_close(G_IO_STREAM(subscriber), NULL, NULL);
	g_object_unref(subscriber);
	test_agent_stop(process);

	process = test_agent_start_with_workspace(socket_path, session_path,
	                                          workspace_path);
	g_byte_array_set_size(request, 0);
	g_assert_true(sakura_control_encode_get_snapshot_request("reload", request));
	test_agent_call(socket_path, "reload", request, &response);
	sakura_control_response_clear(&response);

	subscriber = test_agent_connect_wait(socket_path);
	subscriber_input = g_io_stream_get_input_stream(G_IO_STREAM(subscriber));
	subscriber_output = g_io_stream_get_output_stream(G_IO_STREAM(subscriber));
	test_agent_handshake_on_connection(subscriber);
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
	g_assert_cmpuint(event_snapshot->pages->len, ==, 1);
	group = g_ptr_array_index(event_snapshot->groups, 0);
	g_assert_true(group->archived);
	page_record = g_ptr_array_index(event_snapshot->pages, 0);
	g_assert_cmpstr(page_record->id, ==, "page-restart");
	g_assert_cmpstr(page_record->group_id, ==, group_id);
	g_assert_cmpstr(page_record->task_id, ==, task_id);
	g_assert_cmpstr(page_record->title, ==, "Restored page");
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

	snapshot = test_load_agent_session(workspace_path);
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
	g_assert_true(sakura_control_encode_delete_page_request(
		"delete-page", "page-restart", request));
	test_agent_call(socket_path, "delete-page", request, &response);
	sakura_control_response_clear(&response);
	g_assert_true(sakura_control_frame_read(subscriber_input, &event_payload,
	                                        NULL, &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_decode_workspace_changed_event(
		event_payload->data, event_payload->len, &event_sequence,
		&event_snapshot, &error));
	g_assert_no_error(error);
	g_assert_cmpuint(event_sequence, ==, 7);
	g_assert_cmpuint(event_snapshot->pages->len, ==, 0);
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
	g_assert_cmpuint(event_sequence, ==, 8);
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
	g_remove(workspace_path);
	g_remove(session_path);
	g_remove(socket_path);
	g_rmdir(directory);
	g_free(session_path);
	g_free(workspace_path);
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
	guint64 attached_output_end = 0;

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
	test_agent_handshake_on_connection(subscriber);
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
	                                              0, 0, NULL));

	g_byte_array_set_size(request, 0);
	g_assert_true(sakura_control_encode_create_terminal_request_with_page(
		"create-terminal", "terminal-agent-1", "page-agent-1", "root", "root",
		"/tmp", 100, 40, request));
	test_agent_call_on_connection(command_connection, "create-terminal", request,
	                              &response);
	g_assert_true(response.accepted);
	g_assert_cmpstr(response.accepted_kind, ==, "terminal");
	g_assert_nonnull(response.accepted_id);
	g_assert_cmpstr(response.accepted_id, ==, "terminal-agent-1");
	terminal_id = g_strdup(response.accepted_id);
	sakura_control_response_clear(&response);
	g_assert_true(test_agent_read_workspace_until(subscriber_input, 1,
	                                              terminal_id, 100, 40,
	                                              "page-agent-1"));

	/* A reconnect that presents a future offset must get the same typed gap
	 * failure as one that has fallen off the retained ring. */
	g_byte_array_set_size(request, 0);
	g_assert_true(sakura_control_encode_attach_terminal_request_after_offset(
		"terminal-future-offset", terminal_id, 100, 40, G_MAXUINT64, request));
	g_assert_true(sakura_control_frame_write(
		g_io_stream_get_output_stream(G_IO_STREAM(command_connection)),
		request->data, request->len, NULL, &error));
	g_assert_true(sakura_control_frame_read(
		g_io_stream_get_input_stream(G_IO_STREAM(command_connection)),
		&event_payload, NULL, &error));
	g_assert_false(sakura_control_decode_response(
		event_payload->data, event_payload->len, &response, &error));
	g_assert_error(error, SAKURA_CONTROL_ERROR_DOMAIN,
	               SAKURA_CONTROL_ERROR_CODE_OUTPUT_GAP);
	g_assert_cmpstr(response.error_code, ==, SAKURA_CONTROL_ERROR_OUTPUT_GAP);
	g_clear_error(&error);
	sakura_control_response_clear(&response);
	g_clear_pointer(&event_payload, g_byte_array_unref);

	g_byte_array_set_size(request, 0);
	g_assert_true(sakura_control_encode_restart_terminal_request_with_page(
		"terminal-restart", terminal_id, "page-agent-1", "root", "root",
		directory, 120, 50, request));
	test_agent_call_on_connection(command_connection, "terminal-restart", request,
	                              &response);
	g_assert_true(response.accepted);
	g_assert_cmpstr(response.accepted_kind, ==, "terminal");
	g_assert_cmpstr(response.accepted_id, ==, terminal_id);
	sakura_control_response_clear(&response);
	g_assert_true(test_agent_read_workspace_until(subscriber_input, 1,
	                                              terminal_id, 120, 50,
	                                              "page-agent-1"));

	g_byte_array_set_size(request, 0);
	g_assert_true(sakura_control_encode_rename_page_request(
		"page-rename", "page-agent-1", "Agent page", TRUE, request));
	test_agent_call_on_connection(command_connection, "page-rename", request,
	                              &response);
	g_assert_true(response.has_snapshot);
	sakura_control_response_clear(&response);
	g_assert_true(test_agent_read_page_state_until(
		subscriber_input, "page-agent-1", "Agent page", TRUE, FALSE));

	g_byte_array_set_size(request, 0);
	g_assert_true(sakura_control_encode_set_page_archived_request(
		"page-archive", "page-agent-1", TRUE, request));
	test_agent_call_on_connection(command_connection, "page-archive", request,
	                              &response);
	g_assert_true(response.has_snapshot);
	sakura_control_response_clear(&response);
	g_assert_true(test_agent_read_page_state_until(
		subscriber_input, "page-agent-1", "Agent page", TRUE, TRUE));

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
	g_assert_true(sakura_control_encode_detach_terminal_request(
		"terminal-detach", terminal_id, request));
	test_agent_call_on_connection(command_connection, "terminal-detach", request,
	                              &response);
	g_assert_true(response.accepted);
	g_assert_cmpstr(response.accepted_kind, ==, "terminal_detached");
	g_assert_cmpstr(response.accepted_id, ==, terminal_id);
	sakura_control_response_clear(&response);

	g_byte_array_set_size(request, 0);
	g_assert_true(sakura_control_encode_attach_terminal_request(
		"terminal-attach", terminal_id, 120, 50, request));
	test_agent_call_on_connection(command_connection, "terminal-attach", request,
	                              &response);
	g_assert_true(response.attached);
	g_assert_cmpstr(response.attached_terminal_id, ==, terminal_id);
	g_assert_cmpuint(response.attached_cols, ==, 120);
	g_assert_cmpuint(response.attached_rows, ==, 50);
	g_assert_cmpuint(response.attached_status, ==, SAKURA_TERMINAL_RUNNING);
	g_assert_nonnull(response.attached_output);
	g_assert_cmpuint(response.attached_output_length, >, 0);
	g_assert_nonnull(g_strstr_len(
		(const gchar *)response.attached_output,
		response.attached_output_length, "sakura-terminal-test"));
	attached_output_end = response.attached_output_end_offset;
	sakura_control_response_clear(&response);

	/* A reconnect can request only the output it has not rendered yet. */
	g_byte_array_set_size(request, 0);
	g_assert_true(sakura_control_encode_attach_terminal_request_after_offset(
		"terminal-attach-resume", terminal_id, 120, 50,
		attached_output_end - 5, request));
	test_agent_call_on_connection(command_connection, "terminal-attach-resume",
	                              request, &response);
	g_assert_true(response.attached);
	g_assert_cmpuint(response.attached_output_start_offset, ==,
	                attached_output_end - 5);
	g_assert_cmpuint(response.attached_output_end_offset, ==,
	                attached_output_end);
	g_assert_cmpuint(response.attached_output_length, ==, 5);
	sakura_control_response_clear(&response);

	g_byte_array_set_size(request, 0);
	g_assert_true(sakura_control_encode_terminal_resize_request(
		"terminal-resize", terminal_id, 120, 50, request));
	test_agent_call_on_connection(command_connection, "terminal-resize", request,
	                              &response);
	g_assert_true(response.accepted);
	g_assert_cmpstr(response.accepted_kind, ==, "terminal_resize");
	sakura_control_response_clear(&response);
	/* Resizing is terminal runtime metadata, not a workspace event. There
	 * should be no snapshot queued for the subscriber as a result. */
	g_socket_set_timeout(g_socket_connection_get_socket(subscriber), 1);
	if (sakura_control_frame_read(subscriber_input, &event_payload, NULL,
	                              &error)) {
		SakuraSessionSnapshot *resize_snapshot = NULL;
		guint64 resize_sequence = 0;

		g_clear_error(&error);
		g_assert_false(sakura_control_decode_workspace_changed_event(
			event_payload->data, event_payload->len, &resize_sequence,
			&resize_snapshot, &error));
		g_clear_error(&error);
		sakura_session_snapshot_free(resize_snapshot);
	} else {
		g_clear_error(&error);
	}
	g_clear_error(&error);
	g_clear_pointer(&event_payload, g_byte_array_unref);
	g_socket_set_timeout(g_socket_connection_get_socket(subscriber), 10);

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
	                                              0, 0, NULL));

	g_byte_array_set_size(request, 0);
	g_assert_true(sakura_control_encode_delete_page_request(
		"delete-page", "page-agent-1", request));
	test_agent_call_on_connection(command_connection, "delete-page", request,
	                              &response);
	g_assert_true(response.has_snapshot);
	sakura_control_response_clear(&response);
	g_assert_true(test_agent_read_page_count_until(subscriber_input, 0));

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


static void
test_agent_subscribe_connection(GSocketConnection *connection,
	                              const gchar *request_id)
{
	GByteArray *request = g_byte_array_new();
	GByteArray *response_payload = NULL;
	SakuraControlResponse response = { 0 };
	GError *error = NULL;

	test_agent_set_event_timeout(connection);
	test_agent_handshake_on_connection(connection);
	g_assert_true(sakura_control_encode_subscribe_events_request(
		request_id, 0, request));
	g_assert_true(sakura_control_frame_write(
		g_io_stream_get_output_stream(G_IO_STREAM(connection)), request->data,
		request->len, NULL, &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_frame_read(
		g_io_stream_get_input_stream(G_IO_STREAM(connection)),
		&response_payload, NULL, &error));
	g_assert_no_error(error);
	g_assert_true(sakura_control_decode_response(
		response_payload->data, response_payload->len, &response, &error));
	g_assert_no_error(error);
	g_assert_true(response.accepted);
	g_assert_cmpstr(response.accepted_kind, ==, "workspace_events");
	sakura_control_response_clear(&response);
	g_byte_array_unref(response_payload);
	g_byte_array_unref(request);
}


static void
test_agent_terminal_event_resume(void)
{
	GSubprocess *process;
	GSocketConnection *subscriber;
	GByteArray *request = g_byte_array_new();
	SakuraControlResponse response = { 0 };
	GError *error = NULL;
	gchar *directory;
	gchar *socket_path;
	gchar *session_path;
	gchar *terminal_id = NULL;
	guint64 checkpoint_offset = 0;
	guint64 resume_end_offset = 0;

	directory = g_dir_make_tmp("sakura-agent-event-resume-XXXXXX", &error);
	g_assert_no_error(error);
	socket_path = g_build_filename(directory, "agent.sock", NULL);
	session_path = g_build_filename(directory, "workspace.session", NULL);
	process = test_agent_start(socket_path, session_path);

	subscriber = test_agent_connect_wait(socket_path);
	test_agent_subscribe_connection(subscriber, "resume-subscribe-1");
	g_assert_true(test_agent_read_workspace_until(
		g_io_stream_get_input_stream(G_IO_STREAM(subscriber)), 0, NULL, 0, 0,
		NULL));

	g_assert_true(sakura_control_encode_create_terminal_request_with_page(
		"resume-create", "terminal-event-resume", "page-event-resume", "root",
		"root", "/tmp", 100, 40, request));
	test_agent_call(socket_path, "resume-create", request, &response);
	g_assert_true(response.accepted);
	terminal_id = g_strdup(response.accepted_id);
	sakura_control_response_clear(&response);
	g_assert_true(test_agent_read_workspace_until(
		g_io_stream_get_input_stream(G_IO_STREAM(subscriber)), 1, terminal_id,
		100, 40, "page-event-resume"));

	g_byte_array_set_size(request, 0);
	{
		const gchar input[] = "printf 'event-resume-before\n'\n";

		g_assert_true(sakura_control_encode_terminal_input_request(
			"resume-input-before", terminal_id, (const guint8 *)input,
			sizeof(input) - 1, request));
	}
	test_agent_call(socket_path, "resume-input-before", request, &response);
	g_assert_true(response.accepted);
	sakura_control_response_clear(&response);
	g_assert_true(test_agent_read_output_end_until(
		g_io_stream_get_input_stream(G_IO_STREAM(subscriber)), terminal_id,
		"event-resume-before", &checkpoint_offset));

	/* The terminal process remains alive while only the event stream is
	 * disconnected. Output generated during that interval must be recovered
	 * by the next attach rather than lost with the socket. */
	g_io_stream_close(G_IO_STREAM(subscriber), NULL, NULL);
	g_object_unref(subscriber);
	subscriber = NULL;
	g_byte_array_set_size(request, 0);
	{
		const gchar input[] = "printf 'event-resume-during-gap\n'\n";

		g_assert_true(sakura_control_encode_terminal_input_request(
			"resume-input-gap", terminal_id, (const guint8 *)input,
			sizeof(input) - 1, request));
	}
	test_agent_call(socket_path, "resume-input-gap", request, &response);
	g_assert_true(response.accepted);
	sakura_control_response_clear(&response);

	subscriber = test_agent_connect_wait(socket_path);
	test_agent_subscribe_connection(subscriber, "resume-subscribe-2");
	g_assert_true(test_agent_read_workspace_until(
		g_io_stream_get_input_stream(G_IO_STREAM(subscriber)), 1, terminal_id,
		100, 40, "page-event-resume"));
	g_byte_array_set_size(request, 0);
	g_assert_true(sakura_control_encode_attach_terminal_request_after_offset(
		"resume-attach", terminal_id, 100, 40, checkpoint_offset, request));
	test_agent_call(socket_path, "resume-attach", request, &response);
	g_assert_true(response.attached);
	g_assert_cmpuint(response.attached_output_start_offset, ==,
	                checkpoint_offset);
	g_assert_nonnull(g_strstr_len(
		(const gchar *)response.attached_output, response.attached_output_length,
		"event-resume-during-gap"));
	resume_end_offset = response.attached_output_end_offset;
	sakura_control_response_clear(&response);

	g_byte_array_set_size(request, 0);
	{
		const gchar input[] = "printf 'event-resume-live\n'\n";

		g_assert_true(sakura_control_encode_terminal_input_request(
			"resume-input-live", terminal_id, (const guint8 *)input,
			sizeof(input) - 1, request));
	}
	test_agent_call(socket_path, "resume-input-live", request, &response);
	g_assert_true(response.accepted);
	sakura_control_response_clear(&response);
	g_assert_true(test_agent_read_output_end_until(
		g_io_stream_get_input_stream(G_IO_STREAM(subscriber)), terminal_id,
		"event-resume-live", &resume_end_offset));
	g_assert_cmpuint(resume_end_offset, >, checkpoint_offset);

	/* Force the retained ring past the old cursor and verify that the typed
	 * output-gap error is still the recovery signal for a reconnect. */
	g_io_stream_close(G_IO_STREAM(subscriber), NULL, NULL);
	g_object_unref(subscriber);
	subscriber = NULL;
	g_byte_array_set_size(request, 0);
	{
		const gchar input[] = "yes x | head -c 1100000\n";

		g_assert_true(sakura_control_encode_terminal_input_request(
			"resume-input-overflow", terminal_id, (const guint8 *)input,
			sizeof(input) - 1, request));
	}
	test_agent_call(socket_path, "resume-input-overflow", request, &response);
	g_assert_true(response.accepted);
	sakura_control_response_clear(&response);

	{
		gboolean output_gap = FALSE;

		for (guint attempt = 0; attempt < 300 && !output_gap; attempt++) {
			GSocketConnection *connection;
			GByteArray *response_payload = NULL;
			gchar *request_id = g_strdup_printf("resume-gap-%u", attempt);

			g_byte_array_set_size(request, 0);
			g_assert_true(sakura_control_encode_attach_terminal_request_after_offset(
				request_id, terminal_id, 100, 40, checkpoint_offset, request));
			connection = test_agent_connect_wait(socket_path);
			test_agent_handshake_on_connection(connection);
			g_assert_true(sakura_control_frame_write(
				g_io_stream_get_output_stream(G_IO_STREAM(connection)), request->data,
				request->len, NULL, &error));
			g_assert_no_error(error);
			g_assert_true(sakura_control_frame_read(
				g_io_stream_get_input_stream(G_IO_STREAM(connection)),
				&response_payload, NULL, &error));
			g_assert_no_error(error);
			if (!sakura_control_decode_response(
					response_payload->data, response_payload->len, &response,
					&error)) {
				if (error != NULL &&
				    error->domain == SAKURA_CONTROL_ERROR_DOMAIN &&
				    error->code == SAKURA_CONTROL_ERROR_CODE_OUTPUT_GAP)
					output_gap = TRUE;
				g_clear_error(&error);
				sakura_control_response_clear(&response);
			} else {
				sakura_control_response_clear(&response);
			}
			g_byte_array_unref(response_payload);
			g_io_stream_close(G_IO_STREAM(connection), NULL, NULL);
			g_object_unref(connection);
			g_free(request_id);
			if (!output_gap)
				g_usleep(10 * 1000);
		}
		g_assert_true(output_gap);
	}

	g_byte_array_unref(request);
	g_free(terminal_id);
	test_agent_stop(process);
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
	int result;
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/control/request-frame-roundtrip",
	                test_request_frame_roundtrip);
	g_test_add_func("/control/snapshot-response-roundtrip",
	                test_snapshot_response_roundtrip);
	g_test_add_func("/control/filesystem-protocol-roundtrip",
	                test_filesystem_protocol_roundtrip);
	g_test_add_func("/control/hello-roundtrip",
	                test_hello_roundtrip);
	g_test_add_func("/control/terminal-output-offsets-roundtrip",
	                test_terminal_output_offsets_roundtrip);
	g_test_add_func("/control/structured-error-roundtrip",
	                test_structured_error_roundtrip);
	g_test_add_func("/control/local-endpoint-validation",
	                test_local_endpoint_validation);
	g_test_add_func("/control/client-request-cancellation",
	                test_client_request_cancellation);
	g_test_add_func("/control/mutation-request-roundtrip",
	                test_mutation_request_roundtrip);
	g_test_add_func("/control/agent-create-and-reload",
	                test_agent_create_and_reload);
	g_test_add_func("/control/agent-rejects-wrong-workspace",
	                test_agent_rejects_wrong_workspace);
	g_test_add_func("/control/agent-rejects-stale-revision",
	                test_agent_rejects_stale_revision);
	g_test_add_func("/control/agent-filesystem-roundtrip",
	                test_agent_filesystem_roundtrip);
	g_test_add_func("/control/agent-terminal-create-rollback",
	                test_agent_terminal_create_rollback);
	g_test_add_func("/control/agent-slow-subscriber-disconnects",
	                test_agent_slow_subscriber_disconnects);
	g_test_add_func("/control/agent-shutdown-with-active-subscriber",
	                test_agent_shutdown_with_active_subscriber);
	g_test_add_func("/control/agent-workspace-migration-and-recovery",
	                test_agent_workspace_migration_and_recovery);
	g_test_add_func("/control/agent-terminal-lifecycle",
	                test_agent_terminal_lifecycle);
	g_test_add_func("/control/agent-terminal-event-resume",
	                test_agent_terminal_event_resume);
	result = g_test_run();
	g_clear_pointer(&test_agent_workspace_id, g_free);
	return result;
}
