#include "sakura-control-client.h"

#include "sakura-control-transport.h"

struct _SakuraControlClientConnection {
	GSocketConnection *connection;
	gint ref_count;
};


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


gboolean
sakura_control_client_request(SakuraControlClientConnection *connection,
                              const GByteArray *request,
                              GByteArray **response, GError **error)
{
	GInputStream *input;
	GOutputStream *output;

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
	return sakura_control_frame_write(output, request->data, request->len,
	                                  NULL, error) &&
	       sakura_control_frame_read(input, response, NULL, error);
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
	    !sakura_control_client_request(connection, request, &response_payload,
	                                   error) ||
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
