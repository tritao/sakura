#include <string.h>

#include "sakura-control-transport.h"
#include "sakura/control.pb-c.h"


static gboolean
sakura_control_error(GError **error, const gchar *message)
{
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, message);
	return FALSE;
}


static const gchar *
sakura_control_string(const gchar *value)
{
	return value != NULL ? value : "";
}


static gboolean
sakura_control_pack_message(const ProtobufCMessage *message, GByteArray *payload)
{
	gsize size;

	if (message == NULL || payload == NULL)
		return FALSE;
	size = protobuf_c_message_get_packed_size(message);
	g_byte_array_set_size(payload, size);
	if (size != 0 && protobuf_c_message_pack(message, payload->data) != size) {
		g_byte_array_set_size(payload, 0);
		return FALSE;
	}
	return TRUE;
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
	Sakura__Control__V1__GetSnapshotRequest get_snapshot =
		SAKURA__CONTROL__V1__GET_SNAPSHOT_REQUEST__INIT;
	Sakura__Control__V1__Request request =
		SAKURA__CONTROL__V1__REQUEST__INIT;

	if (payload == NULL || request_id == NULL || request_id[0] == '\0')
		return FALSE;
	request.request_id = (gchar *)request_id;
	request.body_case = SAKURA__CONTROL__V1__REQUEST__BODY_GET_SNAPSHOT;
	request.get_snapshot = &get_snapshot;
	return sakura_control_pack_message(&request.base, payload);
}


gboolean
sakura_control_decode_request(const guint8 *payload,
	                            gsize payload_length,
	                            SakuraControlRequest *request,
	                            GError **error)
{
	Sakura__Control__V1__Request *decoded;

	if (payload == NULL || request == NULL)
		return sakura_control_error(error, "invalid control request");
	sakura_control_request_clear(request);
	decoded = sakura__control__v1__request__unpack(NULL, payload_length, payload);
	if (decoded == NULL)
		return sakura_control_error(error, "invalid control request");
	if (decoded->request_id[0] != '\0')
		request->request_id = g_strdup(decoded->request_id);
	request->get_snapshot =
		decoded->body_case == SAKURA__CONTROL__V1__REQUEST__BODY_GET_SNAPSHOT &&
		decoded->get_snapshot != NULL;
	sakura__control__v1__request__free_unpacked(decoded, NULL);
	if (request->request_id == NULL || !request->get_snapshot)
		return sakura_control_error(error, "unsupported control request");
	return TRUE;
}


static void
sakura_control_fill_group(Sakura__Control__V1__Group *message,
	                         const SakuraCoreGroup *group)
{
	sakura__control__v1__group__init(message);
	message->id = (gchar *)sakura_control_string(group->id);
	message->parent_id = (gchar *)sakura_control_string(
		group->parent != NULL ? group->parent->id : "root");
	message->title = (gchar *)sakura_control_string(group->title);
	message->directory = (gchar *)sakura_control_string(group->directory);
	message->order = group->order;
	message->archived = group->archived;
}


static void
sakura_control_fill_task(Sakura__Control__V1__Task *message,
	                        const SakuraCoreTask *task)
{
	sakura__control__v1__task__init(message);
	message->id = (gchar *)sakura_control_string(task->id);
	message->parent_id = (gchar *)sakura_control_string(
		task->parent != NULL ? task->parent->id :
		task->group != NULL ? task->group->id : "root");
	message->group_id = (gchar *)sakura_control_string(
		task->group != NULL ? task->group->id : "root");
	message->title = (gchar *)sakura_control_string(task->title);
	message->provider = (gchar *)sakura_control_string(task->provider);
	message->external_id = (gchar *)sakura_control_string(task->external_id);
	message->url = (gchar *)sakura_control_string(task->url);
	message->status = task->status;
	message->order = task->order;
	message->archived = task->archived;
}


gboolean
sakura_control_encode_snapshot_response(const gchar *request_id,
	                                    const SakuraCoreWorkspace *workspace,
	                                    GByteArray *payload)
{
	Sakura__Control__V1__WorkspaceSnapshot snapshot =
		SAKURA__CONTROL__V1__WORKSPACE_SNAPSHOT__INIT;
	Sakura__Control__V1__Response response =
		SAKURA__CONTROL__V1__RESPONSE__INIT;
	GPtrArray *groups;
	GPtrArray *tasks;

	if (request_id == NULL || workspace == NULL || payload == NULL)
		return FALSE;
	groups = sakura_core_workspace_ordered_groups(workspace);
	if (groups->len != 0) {
		snapshot.groups = g_new0(Sakura__Control__V1__Group *, groups->len);
		for (guint index = 0; index < groups->len; index++) {
			Sakura__Control__V1__Group *group_message = g_new0(
				Sakura__Control__V1__Group, 1);

			sakura_control_fill_group(group_message,
			                          g_ptr_array_index(groups, index));
			snapshot.groups[index] = group_message;
		}
		snapshot.n_groups = groups->len;
	}
	g_ptr_array_unref(groups);

	tasks = sakura_core_workspace_ordered_tasks(workspace);
	if (tasks->len != 0) {
		snapshot.tasks = g_new0(Sakura__Control__V1__Task *, tasks->len);
		for (guint index = 0; index < tasks->len; index++) {
			Sakura__Control__V1__Task *task_message = g_new0(
				Sakura__Control__V1__Task, 1);

			sakura_control_fill_task(task_message,
			                         g_ptr_array_index(tasks, index));
			snapshot.tasks[index] = task_message;
		}
		snapshot.n_tasks = tasks->len;
	}
	g_ptr_array_unref(tasks);

	snapshot.active_group_id = (gchar *)sakura_control_string(
		workspace->active_group != NULL ? workspace->active_group->id : "root");
	snapshot.active_task_id = (gchar *)sakura_control_string(
		workspace->active_task != NULL ? workspace->active_task->id : NULL);
	snapshot.root_directory = (gchar *)sakura_control_string(
		workspace->root_group != NULL ? workspace->root_group->directory : NULL);
	response.request_id = (gchar *)request_id;
	response.body_case = SAKURA__CONTROL__V1__RESPONSE__BODY_SNAPSHOT;
	response.snapshot = &snapshot;

	{
		gboolean result = sakura_control_pack_message(&response.base, payload);

		for (gsize index = 0; index < snapshot.n_groups; index++)
			g_free(snapshot.groups[index]);
		for (gsize index = 0; index < snapshot.n_tasks; index++)
			g_free(snapshot.tasks[index]);
		g_free(snapshot.groups);
		g_free(snapshot.tasks);
		return result;
	}
}


gboolean
sakura_control_encode_error_response(const gchar *request_id,
	                                   const gchar *code,
	                                   const gchar *message,
	                                   GByteArray *payload)
{
	Sakura__Control__V1__Error error_message =
		SAKURA__CONTROL__V1__ERROR__INIT;
	Sakura__Control__V1__Response response =
		SAKURA__CONTROL__V1__RESPONSE__INIT;

	if (request_id == NULL || payload == NULL)
		return FALSE;
	error_message.code = (gchar *)sakura_control_string(code);
	error_message.message = (gchar *)sakura_control_string(message);
	response.request_id = (gchar *)request_id;
	response.body_case = SAKURA__CONTROL__V1__RESPONSE__BODY_ERROR;
	response.error = &error_message;
	return sakura_control_pack_message(&response.base, payload);
}


gboolean
sakura_control_decode_response(const guint8 *payload,
	                             gsize payload_length,
	                             SakuraControlResponse *response,
	                             GError **error)
{
	Sakura__Control__V1__Response *decoded;
	gboolean has_snapshot;

	if (payload == NULL || response == NULL)
		return sakura_control_error(error, "invalid control response");
	sakura_control_response_clear(response);
	decoded = sakura__control__v1__response__unpack(NULL, payload_length, payload);
	if (decoded == NULL)
		return sakura_control_error(error, "invalid control response");
	if (decoded->request_id[0] != '\0')
		response->request_id = g_strdup(decoded->request_id);
	if (decoded->body_case == SAKURA__CONTROL__V1__RESPONSE__BODY_ERROR &&
	    decoded->error != NULL) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		            "agent returned %s: %s",
		            sakura_control_string(decoded->error->code),
		            sakura_control_string(decoded->error->message));
		sakura__control__v1__response__free_unpacked(decoded, NULL);
		return FALSE;
	}
	has_snapshot =
		decoded->body_case == SAKURA__CONTROL__V1__RESPONSE__BODY_SNAPSHOT &&
		decoded->snapshot != NULL;
	sakura__control__v1__response__free_unpacked(decoded, NULL);
	response->has_snapshot = has_snapshot;
	if (response->request_id == NULL || !response->has_snapshot)
		return sakura_control_error(error, "unsupported control response");
	return TRUE;
}
