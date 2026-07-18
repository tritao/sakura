#include <string.h>

#include "sakura-control.h"


static gboolean
sakura_control_error(GError **error, const gchar *message)
{
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, message);
	return FALSE;
}


static void
sakura_control_append_varint(GByteArray *buffer, guint64 value)
{
	while (value >= 0x80) {
		guint8 byte = (guint8)(value | 0x80);
		g_byte_array_append(buffer, &byte, 1);
		value >>= 7;
	}
	{
		guint8 byte = (guint8)value;
		g_byte_array_append(buffer, &byte, 1);
	}
}


static void
sakura_control_append_key(GByteArray *buffer, guint field, guint wire_type)
{
	sakura_control_append_varint(buffer, ((guint64)field << 3) | wire_type);
}


static void
sakura_control_append_bytes(GByteArray *buffer,
	                           guint field,
	                           const guint8 *data,
	                           gsize length)
{
	if (data == NULL && length != 0)
		return;
	sakura_control_append_key(buffer, field, 2);
	sakura_control_append_varint(buffer, length);
	if (length != 0)
		g_byte_array_append(buffer, data, length);
}


static void
sakura_control_append_string(GByteArray *buffer,
	                            guint field,
	                            const gchar *value)
{
	if (value != NULL)
		sakura_control_append_bytes(buffer, field, (const guint8 *)value,
		                            strlen(value));
}


static void
sakura_control_append_uint(GByteArray *buffer, guint field, guint64 value)
{
	sakura_control_append_key(buffer, field, 0);
	sakura_control_append_varint(buffer, value);
}


static gboolean
sakura_control_read_varint(const guint8 *data,
	                          gsize length,
	                          gsize *offset,
	                          guint64 *value)
{
	guint64 result = 0;
	guint shift = 0;

	while (*offset < length && shift < 64) {
		guint8 byte = data[(*offset)++];

		if (shift == 63 && byte > 1)
			return FALSE;
		result |= ((guint64)(byte & 0x7f)) << shift;
		if ((byte & 0x80) == 0) {
			*value = result;
			return TRUE;
		}
		shift += 7;
	}
	return FALSE;
}


static gboolean
sakura_control_read_bytes(const guint8 *data,
	                         gsize length,
	                         gsize *offset,
	                         const guint8 **value,
	                         gsize *value_length)
{
	guint64 encoded_length;

	if (!sakura_control_read_varint(data, length, offset, &encoded_length) ||
	    encoded_length > G_MAXSIZE - *offset ||
	    *offset + (gsize)encoded_length > length)
		return FALSE;
	*value = data + *offset;
	*value_length = (gsize)encoded_length;
	*offset += (gsize)encoded_length;
	return TRUE;
}


static gboolean
sakura_control_skip_field(const guint8 *data,
	                         gsize length,
	                         gsize *offset,
	                         guint wire_type)
{
	guint64 value;
	const guint8 *bytes;
	gsize bytes_length;

	switch (wire_type) {
	case 0:
		return sakura_control_read_varint(data, length, offset, &value);
	case 1:
		if (length - *offset < 8)
			return FALSE;
		*offset += 8;
		return TRUE;
	case 2:
		return sakura_control_read_bytes(data, length, offset, &bytes,
		                                 &bytes_length);
	case 5:
		if (length - *offset < 4)
			return FALSE;
		*offset += 4;
		return TRUE;
	default:
		return FALSE;
	}
}


static gboolean
sakura_control_read_string(const guint8 *data,
	                          gsize length,
	                          gsize *offset,
	                          gchar **value)
{
	const guint8 *bytes;
	gsize bytes_length;

	if (!sakura_control_read_bytes(data, length, offset, &bytes,
	                               &bytes_length) ||
	    !g_utf8_validate((const gchar *)bytes, bytes_length, NULL))
		return FALSE;
	*value = g_strndup((const gchar *)bytes, bytes_length);
	return TRUE;
}


static gboolean
sakura_control_read_message(const guint8 *data,
	                           gsize length,
	                           gsize *offset,
	                           const guint8 **message,
	                           gsize *message_length)
{
	return sakura_control_read_bytes(data, length, offset, message,
	                                 message_length);
}


void
sakura_control_request_clear(SakuraControlRequest *request)
{
	if (request == NULL)
		return;
	g_clear_pointer(&request->request_id, g_free);
	request->get_snapshot = FALSE;
}


void
sakura_control_response_clear(SakuraControlResponse *response)
{
	if (response == NULL)
		return;
	g_clear_pointer(&response->request_id, g_free);
	response->has_snapshot = FALSE;
}


gboolean
sakura_control_frame_read(GInputStream *input,
	                         GByteArray **payload,
	                         GCancellable *cancellable,
	                         GError **error)
{
	guint8 header[4];
	gsize bytes_read = 0;
	guint32 frame_length;

	if (input == NULL || payload == NULL)
		return sakura_control_error(error, "invalid control stream");
	*payload = NULL;
	if (!g_input_stream_read_all(input, header, sizeof(header), &bytes_read,
	                             cancellable, error))
		return FALSE;
	if (bytes_read == 0)
		return sakura_control_error(error, "control stream closed");
	if (bytes_read != sizeof(header))
		return sakura_control_error(error, "truncated control frame header");
	frame_length = ((guint32)header[0] << 24) | ((guint32)header[1] << 16) |
	               ((guint32)header[2] << 8) | header[3];
	if (frame_length == 0 || frame_length > SAKURA_CONTROL_MAX_FRAME)
		return sakura_control_error(error, "invalid control frame length");
	*payload = g_byte_array_sized_new(frame_length);
	g_byte_array_set_size(*payload, frame_length);
	if (!g_input_stream_read_all(input, (*payload)->data, frame_length,
	                             &bytes_read, cancellable, error) ||
	    bytes_read != frame_length) {
		g_clear_pointer(payload, g_byte_array_unref);
		if (error == NULL || *error == NULL)
			sakura_control_error(error, "truncated control frame");
		return FALSE;
	}
	return TRUE;
}


gboolean
sakura_control_frame_write(GOutputStream *output,
	                          const guint8 *payload,
	                          gsize payload_length,
	                          GCancellable *cancellable,
	                          GError **error)
{
	guint8 header[4];
	gsize bytes_written;

	if (output == NULL || payload == NULL || payload_length == 0 ||
	    payload_length > SAKURA_CONTROL_MAX_FRAME)
		return sakura_control_error(error, "invalid control frame");
	header[0] = (guint8)(payload_length >> 24);
	header[1] = (guint8)(payload_length >> 16);
	header[2] = (guint8)(payload_length >> 8);
	header[3] = (guint8)payload_length;
	if (!g_output_stream_write_all(output, header, sizeof(header),
	                               &bytes_written, cancellable, error) ||
	    bytes_written != sizeof(header) ||
	    !g_output_stream_write_all(output, payload, payload_length,
	                               &bytes_written, cancellable, error) ||
	    bytes_written != payload_length)
		return FALSE;
	return g_output_stream_flush(output, cancellable, error);
}


gboolean
sakura_control_encode_get_snapshot_request(const gchar *request_id,
	                                         GByteArray *payload)
{
	if (payload == NULL || request_id == NULL || request_id[0] == '\0')
		return FALSE;
	sakura_control_append_string(payload, 1, request_id);
	/* GetSnapshotRequest is an empty protobuf message. */
	sakura_control_append_key(payload, 2, 2);
	sakura_control_append_varint(payload, 0);
	return TRUE;
}


gboolean
sakura_control_decode_request(const guint8 *payload,
	                            gsize payload_length,
	                            SakuraControlRequest *request,
	                            GError **error)
{
	gsize offset = 0;

	if (payload == NULL || request == NULL) {
		sakura_control_error(error, "invalid control request");
		return FALSE;
	}
	sakura_control_request_clear(request);
	while (offset < payload_length) {
		guint64 key;
		guint field;
		guint wire_type;

		if (!sakura_control_read_varint(payload, payload_length, &offset, &key) ||
		    key > G_MAXUINT || key < 8)
			return sakura_control_error(error, "invalid control request field");
		field = (guint)(key >> 3);
		wire_type = (guint)(key & 7);
		if (field == 1 && wire_type == 2) {
			gchar *request_id = NULL;

			if (!sakura_control_read_string(payload, payload_length, &offset,
			                               &request_id))
				return sakura_control_error(error, "invalid request id");
			g_free(request->request_id);
			request->request_id = request_id;
		} else if (field == 2 && wire_type == 2) {
			const guint8 *message;
			gsize message_length;

			if (!sakura_control_read_message(payload, payload_length, &offset,
			                                &message, &message_length))
				return sakura_control_error(error, "invalid snapshot request");
			(void)message;
			(void)message_length;
			request->get_snapshot = TRUE;
		} else if (!sakura_control_skip_field(payload, payload_length, &offset,
		                                      wire_type)) {
			return sakura_control_error(error, "invalid control request field");
		}
	}
	if (request->request_id == NULL || !request->get_snapshot)
		return sakura_control_error(error, "unsupported control request");
	return TRUE;
}


static void
sakura_control_encode_group(GByteArray *message, const SakuraCoreGroup *group)
{
	sakura_control_append_string(message, 1, group->id);
	sakura_control_append_string(message, 2,
	                            group->parent != NULL ? group->parent->id : "root");
	sakura_control_append_string(message, 3, group->title);
	sakura_control_append_string(message, 4, group->directory);
	sakura_control_append_uint(message, 5, group->order);
	if (group->archived)
		sakura_control_append_uint(message, 6, TRUE);
}


static void
sakura_control_encode_task(GByteArray *message, const SakuraCoreTask *task)
{
	sakura_control_append_string(message, 1, task->id);
	sakura_control_append_string(message, 2,
	                            task->parent != NULL ? task->parent->id :
	                            task->group != NULL ? task->group->id : "root");
	sakura_control_append_string(message, 3,
	                            task->group != NULL ? task->group->id : "root");
	sakura_control_append_string(message, 4, task->title);
	sakura_control_append_string(message, 5, task->provider);
	sakura_control_append_string(message, 6, task->external_id);
	sakura_control_append_string(message, 7, task->url);
	sakura_control_append_uint(message, 8, task->status);
	sakura_control_append_uint(message, 9, task->order);
	if (task->archived)
		sakura_control_append_uint(message, 10, TRUE);
}


gboolean
sakura_control_encode_snapshot_response(const gchar *request_id,
	                                    const SakuraCoreWorkspace *workspace,
	                                    GByteArray *payload)
{
	GByteArray *snapshot;
	GPtrArray *groups;
	GPtrArray *tasks;

	if (request_id == NULL || workspace == NULL || payload == NULL)
		return FALSE;
	snapshot = g_byte_array_new();
	groups = sakura_core_workspace_ordered_groups(workspace);
	for (guint index = 0; index < groups->len; index++) {
		GByteArray *group = g_byte_array_new();

		sakura_control_encode_group(group, g_ptr_array_index(groups, index));
		sakura_control_append_bytes(snapshot, 1, group->data, group->len);
		g_byte_array_unref(group);
	}
	g_ptr_array_unref(groups);
	tasks = sakura_core_workspace_ordered_tasks(workspace);
	for (guint index = 0; index < tasks->len; index++) {
		GByteArray *task = g_byte_array_new();

		sakura_control_encode_task(task, g_ptr_array_index(tasks, index));
		sakura_control_append_bytes(snapshot, 2, task->data, task->len);
		g_byte_array_unref(task);
	}
	g_ptr_array_unref(tasks);
	sakura_control_append_string(snapshot, 3,
	                            workspace->active_group != NULL
	                            ? workspace->active_group->id : "root");
	sakura_control_append_string(snapshot, 4,
	                            workspace->active_task != NULL
	                            ? workspace->active_task->id : NULL);
	if (workspace->root_group != NULL)
		sakura_control_append_string(snapshot, 5,
		                            workspace->root_group->directory);
	sakura_control_append_uint(snapshot, 6, 0);

	sakura_control_append_string(payload, 1, request_id);
	sakura_control_append_bytes(payload, 2, snapshot->data, snapshot->len);
	g_byte_array_unref(snapshot);
	return TRUE;
}


gboolean
sakura_control_encode_error_response(const gchar *request_id,
	                                   const gchar *code,
	                                   const gchar *message,
	                                   GByteArray *payload)
{
	GByteArray *error_message;

	if (request_id == NULL || payload == NULL)
		return FALSE;
	error_message = g_byte_array_new();
	sakura_control_append_string(error_message, 1, code);
	sakura_control_append_string(error_message, 2, message);
	sakura_control_append_string(payload, 1, request_id);
	sakura_control_append_bytes(payload, 4, error_message->data,
	                            error_message->len);
	g_byte_array_unref(error_message);
	return TRUE;
}


gboolean
sakura_control_decode_response(const guint8 *payload,
	                             gsize payload_length,
	                             SakuraControlResponse *response,
	                             GError **error)
{
	gsize offset = 0;

	if (payload == NULL || response == NULL) {
		sakura_control_error(error, "invalid control response");
		return FALSE;
	}
	sakura_control_response_clear(response);
	while (offset < payload_length) {
		guint64 key;
		guint field;
		guint wire_type;

		if (!sakura_control_read_varint(payload, payload_length, &offset, &key) ||
		    key > G_MAXUINT || key < 8)
			return sakura_control_error(error, "invalid control response field");
		field = (guint)(key >> 3);
		wire_type = (guint)(key & 7);
		if (field == 1 && wire_type == 2) {
			gchar *request_id = NULL;

			if (!sakura_control_read_string(payload, payload_length, &offset,
			                               &request_id))
				return sakura_control_error(error, "invalid response id");
			g_free(response->request_id);
			response->request_id = request_id;
		} else if (field == 2 && wire_type == 2) {
			const guint8 *message;
			gsize message_length;

			if (!sakura_control_read_message(payload, payload_length, &offset,
			                                &message, &message_length))
				return sakura_control_error(error, "invalid snapshot response");
			(void)message;
			(void)message_length;
			response->has_snapshot = TRUE;
		} else if (field == 4 && wire_type == 2) {
			const guint8 *message;
			gsize message_length;

			if (!sakura_control_read_message(payload, payload_length, &offset,
			                                &message, &message_length))
				return sakura_control_error(error, "invalid error response");
			(void)message;
			(void)message_length;
			return sakura_control_error(error, "agent returned an error");
		} else if (!sakura_control_skip_field(payload, payload_length, &offset,
		                                      wire_type)) {
			return sakura_control_error(error, "invalid control response field");
		}
	}
	if (response->request_id == NULL || !response->has_snapshot)
		return sakura_control_error(error, "unsupported control response");
	return TRUE;
}
