#include "sakura-control-client.h"

#include "sakura-control-transport.h"

#include <string.h>

struct _SakuraControlClientConnection {
	GSocketConnection *connection;
	gint ref_count;
};


void
sakura_control_terminal_attachment_clear(
	SakuraControlTerminalAttachment *attachment)
{
	if (attachment == NULL)
		return;
	g_clear_pointer(&attachment->terminal_id, g_free);
	g_clear_pointer(&attachment->output, g_free);
	attachment->cols = 0;
	attachment->rows = 0;
	attachment->status = 0;
	attachment->output_length = 0;
	attachment->output_start_offset = 0;
	attachment->output_end_offset = 0;
}


void
sakura_control_client_event_clear(SakuraControlClientEvent *event)
{
	if (event == NULL)
		return;
	g_clear_pointer(&event->terminal_id, g_free);
	g_clear_pointer(&event->data, g_free);
	g_clear_pointer(&event->message, g_free);
	g_clear_pointer(&event->payload, g_free);
	memset(event, 0, sizeof(*event));
}


static gboolean
sakura_control_client_error(GError **error, GQuark domain, gint code,
                            const gchar *message)
{
	if (error != NULL)
		g_set_error_literal(error, domain, code, message);
	return FALSE;
}


SakuraControlClientConnection *
sakura_control_client_ref(SakuraControlClientConnection *connection)
{
	if (connection == NULL || connection->connection == NULL)
		return NULL;
	g_atomic_int_inc(&connection->ref_count);
	return connection;
}


void
sakura_control_client_close(SakuraControlClientConnection *connection)
{
	if (connection == NULL || connection->connection == NULL)
		return;
	g_io_stream_close(G_IO_STREAM(connection->connection), NULL, NULL);
}


void
sakura_control_client_shutdown(SakuraControlClientConnection *connection)
{
	GSocket *socket;

	if (connection == NULL || connection->connection == NULL)
		return;
	socket = g_socket_connection_get_socket(connection->connection);
	if (socket != NULL)
		g_socket_shutdown(socket, TRUE, TRUE, NULL);
	sakura_control_client_close(connection);
}


void
sakura_control_client_unref(SakuraControlClientConnection *connection)
{
	if (connection == NULL)
		return;
	if (!g_atomic_int_dec_and_test(&connection->ref_count))
		return;
	g_clear_object(&connection->connection);
	g_free(connection);
}


static gboolean
sakura_control_client_request_with_timeout(
	SakuraControlClientConnection *connection, const GByteArray *request,
	GByteArray **response, GCancellable *cancellable, guint timeout,
	GError **error)
{
	GInputStream *input;
	GOutputStream *output;
	GSocket *socket;
	guint previous_timeout;
	gboolean success;

	if (response != NULL)
		*response = NULL;
	if (connection == NULL || connection->connection == NULL ||
	    request == NULL || request->len == 0 || response == NULL) {
		return sakura_control_client_error(
			error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			"invalid control client request");
	}
	input = g_io_stream_get_input_stream(G_IO_STREAM(connection->connection));
	output = g_io_stream_get_output_stream(G_IO_STREAM(connection->connection));
	socket = g_socket_connection_get_socket(connection->connection);
	previous_timeout = socket != NULL ? g_socket_get_timeout(socket) : 0;
	if (socket != NULL)
		g_socket_set_timeout(socket, timeout);
	success = sakura_control_frame_write(output, request->data, request->len,
	                                     cancellable, error) &&
	          sakura_control_frame_read(input, response, cancellable, error);
	if (socket != NULL)
		g_socket_set_timeout(socket, previous_timeout);
	return success;
}


gboolean
sakura_control_client_request_with_cancellable(
	SakuraControlClientConnection *connection, const GByteArray *request,
	GByteArray **response, GCancellable *cancellable, GError **error)
{
	return sakura_control_client_request_with_timeout(
		connection, request, response, cancellable,
		SAKURA_CONTROL_REQUEST_TIMEOUT_SECONDS, error);
}


gboolean
sakura_control_client_request(SakuraControlClientConnection *connection,
                              const GByteArray *request,
                              GByteArray **response, GError **error)
{
	return sakura_control_client_request_with_cancellable(
		connection, request, response, NULL, error);
}


gboolean
sakura_control_client_read_frame(SakuraControlClientConnection *connection,
                                 GByteArray **payload,
                                 GCancellable *cancellable, GError **error)
{
	GInputStream *input;

	if (payload != NULL)
		*payload = NULL;
	if (connection == NULL || connection->connection == NULL ||
	    payload == NULL) {
		return sakura_control_client_error(
			error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			"invalid control client connection");
	}
	input = g_io_stream_get_input_stream(G_IO_STREAM(connection->connection));
	return sakura_control_frame_read(input, payload, cancellable, error);
}


gboolean
sakura_control_client_subscribe_events(
	SakuraControlClientConnection *connection, guint64 after_sequence,
	GError **error)
{
	GByteArray *request = g_byte_array_new();
	GByteArray *response_payload = NULL;
	SakuraControlResponse response = { 0 };
	gchar *request_id = g_uuid_string_random();
	gboolean success = FALSE;

	if (!sakura_control_encode_subscribe_events_request(
		    request_id, after_sequence, request) ||
	    !sakura_control_client_request(connection, request, &response_payload,
	                                   error) ||
	    !sakura_control_decode_response(response_payload->data,
	                                    response_payload->len, &response,
	                                    error))
		goto out;
	if (g_strcmp0(response.request_id, request_id) != 0) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                    "agent response id did not match subscription request");
		goto out;
	}
	if (!response.accepted) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                    "agent did not accept event subscription");
		goto out;
	}
	success = TRUE;
out:
	g_clear_pointer(&response_payload, g_byte_array_unref);
	sakura_control_response_clear(&response);
	g_byte_array_unref(request);
	g_free(request_id);
	return success;
}


static gboolean
sakura_control_client_call(SakuraControlClientConnection *connection,
	                           const gchar *request_id,
	                           GByteArray *request,
	                           SakuraControlResponse *response,
	                           GError **error)
{
	GByteArray *response_payload = NULL;
	gboolean success = FALSE;

	if (!sakura_control_client_request(connection, request, &response_payload,
	                                   error) ||
	    !sakura_control_decode_response(response_payload->data,
	                                    response_payload->len, response,
	                                    error))
		goto out;
	if (g_strcmp0(response->request_id, request_id) != 0) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                    "agent response id did not match request");
		goto out;
	}
	success = TRUE;
out:
	g_clear_pointer(&response_payload, g_byte_array_unref);
	return success;
}


static gboolean
sakura_control_client_require_accepted(const SakuraControlResponse *response,
	                                      GError **error)
{
	if (response != NULL && response->accepted)
		return TRUE;
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
	                    "Sakura agent rejected the request");
	return FALSE;
}


static gboolean
sakura_control_client_request_terminal(
	SakuraControlClientConnection *connection, const gchar *request_id,
	GByteArray *request,
	SakuraControlResponse *response, GError **error)
{
	if (!sakura_control_client_call(connection, request_id, request, response,
	                                error))
		return FALSE;
	return sakura_control_client_require_accepted(response, error);
}


gboolean
sakura_control_client_create_terminal(
	SakuraControlClientConnection *connection, const gchar *terminal_id,
	const gchar *group_id, const gchar *task_id, const gchar *cwd, guint cols,
	guint rows, gchar **created_terminal_id, GError **error)
{
	GByteArray *request = g_byte_array_new();
	SakuraControlResponse response = { 0 };
	gchar *request_id = g_uuid_string_random();
	gboolean success = FALSE;

	if (created_terminal_id != NULL)
		*created_terminal_id = NULL;
	if (!sakura_control_encode_create_terminal_request(
		    request_id, terminal_id, group_id, task_id, cwd, cols, rows,
		    request) ||
	    !sakura_control_client_request_terminal(connection, request_id, request,
	                                            &response,
	                                            error))
		goto out;
	if (created_terminal_id == NULL || response.accepted_id == NULL ||
	    response.accepted_id[0] == '\0') {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                    "Sakura agent did not return a terminal id");
		goto out;
	}
	*created_terminal_id = g_strdup(response.accepted_id);
	success = TRUE;
out:
	if (!success && created_terminal_id != NULL)
		g_clear_pointer(created_terminal_id, g_free);
	sakura_control_response_clear(&response);
	g_byte_array_unref(request);
	g_free(request_id);
	return success;
}


gboolean
sakura_control_client_attach_terminal(
	SakuraControlClientConnection *connection, const gchar *terminal_id,
	guint cols, guint rows, SakuraControlTerminalAttachment *attachment,
	GError **error)
{
	GByteArray *request = g_byte_array_new();
	SakuraControlResponse response = { 0 };
	gchar *request_id = g_uuid_string_random();
	gboolean success = FALSE;

	if (attachment != NULL)
		sakura_control_terminal_attachment_clear(attachment);
	if (!sakura_control_encode_attach_terminal_request(
		    request_id, terminal_id, cols, rows, request) ||
	    !sakura_control_client_call(connection, request_id, request, &response,
	                                error))
		goto out;
	if (!response.attached) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                    "Sakura agent did not attach the terminal");
		goto out;
	}
	if (attachment == NULL) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "terminal attachment output is required");
		goto out;
	}
	attachment->terminal_id = g_strdup(response.attached_terminal_id);
	attachment->cols = response.attached_cols;
	attachment->rows = response.attached_rows;
	attachment->status = response.attached_status;
	attachment->output_length = response.attached_output_length;
	attachment->output_start_offset = response.attached_output_start_offset;
	attachment->output_end_offset = response.attached_output_end_offset;
	if (attachment->output_length != 0) {
		attachment->output = g_malloc(attachment->output_length);
		memcpy(attachment->output, response.attached_output,
		       attachment->output_length);
	}
	success = TRUE;
out:
	if (!success && attachment != NULL)
		sakura_control_terminal_attachment_clear(attachment);
	sakura_control_response_clear(&response);
	g_byte_array_unref(request);
	g_free(request_id);
	return success;
}


gboolean
sakura_control_client_terminal_input(
	SakuraControlClientConnection *connection, const gchar *terminal_id,
	const guint8 *data, gsize data_length, GError **error)
{
	GByteArray *request = g_byte_array_new();
	SakuraControlResponse response = { 0 };
	gchar *request_id = g_uuid_string_random();
	gboolean success;

	success = sakura_control_encode_terminal_input_request(
		request_id, terminal_id, data, data_length, request) &&
		sakura_control_client_request_terminal(connection, request_id, request,
		                                       &response, error);
	sakura_control_response_clear(&response);
	g_byte_array_unref(request);
	g_free(request_id);
	return success;
}


gboolean
sakura_control_client_terminal_resize(
	SakuraControlClientConnection *connection, const gchar *terminal_id,
	guint cols, guint rows, GError **error)
{
	GByteArray *request = g_byte_array_new();
	SakuraControlResponse response = { 0 };
	gchar *request_id = g_uuid_string_random();
	gboolean success;

	success = sakura_control_encode_terminal_resize_request(
		request_id, terminal_id, cols, rows, request) &&
		sakura_control_client_request_terminal(connection, request_id, request,
		                                       &response, error);
	sakura_control_response_clear(&response);
	g_byte_array_unref(request);
	g_free(request_id);
	return success;
}


gboolean
sakura_control_client_detach_terminal(
	SakuraControlClientConnection *connection, const gchar *terminal_id,
	GError **error)
{
	GByteArray *request = g_byte_array_new();
	SakuraControlResponse response = { 0 };
	gchar *request_id = g_uuid_string_random();
	gboolean success;

	success = sakura_control_encode_detach_terminal_request(
		request_id, terminal_id, request) &&
		sakura_control_client_request_terminal(connection, request_id, request,
		                                       &response, error);
	sakura_control_response_clear(&response);
	g_byte_array_unref(request);
	g_free(request_id);
	return success;
}


gboolean
sakura_control_client_read_event(SakuraControlClientConnection *connection,
	                                SakuraControlClientEvent *event,
	                                GCancellable *cancellable, GError **error)
{
	GByteArray *payload = NULL;
	GError *decode_error = NULL;

	if (event == NULL) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "event output is required");
		return FALSE;
	}
	sakura_control_client_event_clear(event);
	if (!sakura_control_client_read_frame(connection, &payload, cancellable,
	                                      error))
		return FALSE;
	if (sakura_control_decode_terminal_output_event_with_offsets(
		    payload->data, payload->len, &event->sequence, &event->terminal_id,
		    &event->start_offset, &event->end_offset, &event->data,
		    &event->data_length, &event->final_chunk,
		    &decode_error)) {
		event->type = SAKURA_CONTROL_CLIENT_EVENT_TERMINAL_OUTPUT;
		g_byte_array_unref(payload);
		return TRUE;
	}
	sakura_control_client_event_clear(event);
	g_clear_error(&decode_error);
	if (sakura_control_decode_terminal_status_event(
		    payload->data, payload->len, &event->sequence, &event->terminal_id,
		    &event->status, &event->message, &decode_error)) {
		event->type = SAKURA_CONTROL_CLIENT_EVENT_TERMINAL_STATUS;
		g_byte_array_unref(payload);
		return TRUE;
	}
	sakura_control_client_event_clear(event);
	g_clear_error(&decode_error);
	event->type = SAKURA_CONTROL_CLIENT_EVENT_UNKNOWN;
	event->payload_length = payload->len;
	event->payload = g_byte_array_free(payload, FALSE);
	return TRUE;
}


SakuraControlClientConnection *
sakura_control_client_connect(const gchar *socket_path,
                              const gchar *workspace_id,
                              const gchar *client_name,
                              guint64 required_capabilities, GError **error)
{
	GSocketClient *client;
	GSocketAddress *address;
	GSocketConnection *socket_connection;
	SakuraControlClientConnection *connection;
	GByteArray *request = NULL;
	GByteArray *response_payload = NULL;
	SakuraControlResponse response = { 0 };
	gchar *request_id = NULL;

	if (socket_path == NULL || socket_path[0] == '\0' ||
	    workspace_id == NULL || workspace_id[0] == '\0' ||
	    client_name == NULL || client_name[0] == '\0') {
		sakura_control_client_error(
			error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
			"socket path, workspace identity, and client name are required");
		return NULL;
	}
	client = g_socket_client_new();
	g_socket_client_set_timeout(client, SAKURA_CONTROL_CONNECT_TIMEOUT_SECONDS);
	address = g_unix_socket_address_new(socket_path);
	socket_connection = g_socket_client_connect(
		client, G_SOCKET_CONNECTABLE(address), NULL, error);
	g_object_unref(address);
	g_object_unref(client);
	if (socket_connection == NULL)
		return NULL;
	connection = g_new0(SakuraControlClientConnection, 1);
	connection->connection = socket_connection;
	connection->ref_count = 1;

	request = g_byte_array_new();
	request_id = g_uuid_string_random();
	if (!sakura_control_encode_hello_request(
		    request_id, SAKURA_CONTROL_PROTOCOL_VERSION, client_name,
		    workspace_id, request) ||
	    !sakura_control_client_request_with_timeout(
			connection, request, &response_payload, NULL,
			SAKURA_CONTROL_HANDSHAKE_TIMEOUT_SECONDS, error) ||
	    !sakura_control_decode_response(response_payload->data,
	                                    response_payload->len, &response,
	                                    error))
		goto fail;
	if (g_strcmp0(response.request_id, request_id) != 0) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                    "agent hello response id did not match request");
		goto fail;
	}
	if (!response.hello) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                    "agent did not return a hello response");
		goto fail;
	}
	if (g_strcmp0(response.workspace_id, workspace_id) != 0) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
		                    "agent workspace identity did not match");
		goto fail;
	}
	if (response.hello_protocol_version != SAKURA_CONTROL_PROTOCOL_VERSION) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		            "unsupported agent protocol version %u",
		            response.hello_protocol_version);
		goto fail;
	}
	if ((response.capabilities & required_capabilities) !=
	    required_capabilities) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		                    "agent does not support required capabilities");
		goto fail;
	}
	g_clear_pointer(&response_payload, g_byte_array_unref);
	sakura_control_response_clear(&response);
	g_byte_array_unref(request);
	g_free(request_id);
	return connection;

fail:
	sakura_control_client_close(connection);
	sakura_control_client_unref(connection);
	g_clear_pointer(&response_payload, g_byte_array_unref);
	sakura_control_response_clear(&response);
	g_clear_pointer(&request, g_byte_array_unref);
	g_free(request_id);
	return NULL;
}
