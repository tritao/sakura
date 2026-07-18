#include <gio/gio.h>

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
	g_assert_true(request.get_snapshot);

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

	g_assert_true(sakura_control_encode_snapshot_response("request-2", workspace,
	                                                     encoded));
	g_assert_true(sakura_control_decode_response(encoded->data, encoded->len,
	                                            &response, &error));
	g_assert_no_error(error);
	g_assert_cmpstr(response.request_id, ==, "request-2");
	g_assert_true(response.has_snapshot);

	sakura_control_response_clear(&response);
	g_byte_array_unref(encoded);
	sakura_core_workspace_free(workspace);
}


int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/control/request-frame-roundtrip",
	                test_request_frame_roundtrip);
	g_test_add_func("/control/snapshot-response-roundtrip",
	                test_snapshot_response_roundtrip);
	return g_test_run();
}
