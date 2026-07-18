#include "sakura-control-transport.h"
#include "sakura/control.pb-c.h"

#include <string.h>


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
	g_clear_pointer(&request->parent_id, g_free);
	g_clear_pointer(&request->title, g_free);
	g_clear_pointer(&request->directory, g_free);
	g_clear_pointer(&request->group_id, g_free);
	g_clear_pointer(&request->task_id, g_free);
	g_clear_pointer(&request->terminal_id, g_free);
	g_clear_pointer(&request->cwd, g_free);
	g_clear_pointer(&request->provider, g_free);
	g_clear_pointer(&request->external_id, g_free);
	g_clear_pointer(&request->url, g_free);
	request->kind = SAKURA_CONTROL_REQUEST_NONE;
	request->archived = FALSE;
	g_clear_pointer(&request->input_data, g_free);
	request->input_length = 0;
	request->cols = 0;
	request->rows = 0;
	request->after_sequence = 0;
}


void
sakura_control_response_clear(SakuraControlResponse *response)
{
	if (response == NULL)
		return;
	g_clear_pointer(&response->request_id, g_free);
	g_clear_pointer(&response->accepted_kind, g_free);
	g_clear_pointer(&response->accepted_id, g_free);
	g_clear_pointer(&response->attached_terminal_id, g_free);
	g_clear_pointer(&response->attached_output, g_free);
	response->has_snapshot = FALSE;
	response->accepted = FALSE;
	response->attached = FALSE;
	response->attached_cols = 0;
	response->attached_rows = 0;
	response->attached_status = 0;
	response->attached_output_length = 0;
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
sakura_control_encode_create_group_request(const gchar *request_id,
	                                         const gchar *parent_id,
	                                         const gchar *title,
	                                         const gchar *directory,
	                                         GByteArray *payload)
{
	Sakura__Control__V1__CreateGroupRequest create_group =
		SAKURA__CONTROL__V1__CREATE_GROUP_REQUEST__INIT;
	Sakura__Control__V1__Request request =
		SAKURA__CONTROL__V1__REQUEST__INIT;

	if (payload == NULL || request_id == NULL || request_id[0] == '\0')
		return FALSE;
	create_group.parent_id = (gchar *)sakura_control_string(parent_id);
	create_group.title = (gchar *)sakura_control_string(title);
	create_group.directory = (gchar *)sakura_control_string(directory);
	request.request_id = (gchar *)request_id;
	request.body_case = SAKURA__CONTROL__V1__REQUEST__BODY_CREATE_GROUP;
	request.create_group = &create_group;
	return sakura_control_pack_message(&request.base, payload);
}


gboolean
sakura_control_encode_create_task_request(const gchar *request_id,
	                                        const gchar *group_id,
	                                        const gchar *parent_id,
	                                        const gchar *title,
	                                        const gchar *provider,
	                                        const gchar *external_id,
	                                        const gchar *url,
	                                        GByteArray *payload)
{
	Sakura__Control__V1__CreateTaskRequest create_task =
		SAKURA__CONTROL__V1__CREATE_TASK_REQUEST__INIT;
	Sakura__Control__V1__Request request =
		SAKURA__CONTROL__V1__REQUEST__INIT;

	if (payload == NULL || request_id == NULL || request_id[0] == '\0')
		return FALSE;
	create_task.group_id = (gchar *)sakura_control_string(group_id);
	create_task.parent_id = (gchar *)sakura_control_string(parent_id);
	create_task.title = (gchar *)sakura_control_string(title);
	create_task.provider = (gchar *)sakura_control_string(provider);
	create_task.external_id = (gchar *)sakura_control_string(external_id);
	create_task.url = (gchar *)sakura_control_string(url);
	request.request_id = (gchar *)request_id;
	request.body_case = SAKURA__CONTROL__V1__REQUEST__BODY_CREATE_TASK;
	request.create_task = &create_task;
	return sakura_control_pack_message(&request.base, payload);
}


gboolean
sakura_control_encode_update_group_request(const gchar *request_id,
	                                           const gchar *group_id,
	                                           const gchar *title,
	                                           const gchar *directory,
	                                           GByteArray *payload)
{
	Sakura__Control__V1__UpdateGroupRequest update_group =
		SAKURA__CONTROL__V1__UPDATE_GROUP_REQUEST__INIT;
	Sakura__Control__V1__Request request =
		SAKURA__CONTROL__V1__REQUEST__INIT;

	if (payload == NULL || request_id == NULL || request_id[0] == '\0' ||
	    group_id == NULL || group_id[0] == '\0')
		return FALSE;
	update_group.group_id = (gchar *)group_id;
	update_group.title = (gchar *)sakura_control_string(title);
	update_group.directory = (gchar *)sakura_control_string(directory);
	request.request_id = (gchar *)request_id;
	request.body_case = SAKURA__CONTROL__V1__REQUEST__BODY_UPDATE_GROUP;
	request.update_group = &update_group;
	return sakura_control_pack_message(&request.base, payload);
}


gboolean
sakura_control_encode_set_group_archived_request(const gchar *request_id,
	                                                const gchar *group_id,
	                                                gboolean archived,
	                                                GByteArray *payload)
{
	Sakura__Control__V1__SetGroupArchivedRequest set_archived =
		SAKURA__CONTROL__V1__SET_GROUP_ARCHIVED_REQUEST__INIT;
	Sakura__Control__V1__Request request =
		SAKURA__CONTROL__V1__REQUEST__INIT;

	if (payload == NULL || request_id == NULL || request_id[0] == '\0' ||
	    group_id == NULL || group_id[0] == '\0')
		return FALSE;
	set_archived.group_id = (gchar *)group_id;
	set_archived.archived = archived;
	request.request_id = (gchar *)request_id;
	request.body_case =
		SAKURA__CONTROL__V1__REQUEST__BODY_SET_GROUP_ARCHIVED;
	request.set_group_archived = &set_archived;
	return sakura_control_pack_message(&request.base, payload);
}


gboolean
sakura_control_encode_delete_group_request(const gchar *request_id,
	                                           const gchar *group_id,
	                                           GByteArray *payload)
{
	Sakura__Control__V1__DeleteGroupRequest delete_group =
		SAKURA__CONTROL__V1__DELETE_GROUP_REQUEST__INIT;
	Sakura__Control__V1__Request request =
		SAKURA__CONTROL__V1__REQUEST__INIT;

	if (payload == NULL || request_id == NULL || request_id[0] == '\0' ||
	    group_id == NULL || group_id[0] == '\0')
		return FALSE;
	delete_group.group_id = (gchar *)group_id;
	request.request_id = (gchar *)request_id;
	request.body_case = SAKURA__CONTROL__V1__REQUEST__BODY_DELETE_GROUP;
	request.delete_group = &delete_group;
	return sakura_control_pack_message(&request.base, payload);
}


gboolean
sakura_control_encode_update_task_request(const gchar *request_id,
	                                          const gchar *task_id,
	                                          const gchar *title,
	                                          GByteArray *payload)
{
	Sakura__Control__V1__UpdateTaskRequest update_task =
		SAKURA__CONTROL__V1__UPDATE_TASK_REQUEST__INIT;
	Sakura__Control__V1__Request request =
		SAKURA__CONTROL__V1__REQUEST__INIT;

	if (payload == NULL || request_id == NULL || request_id[0] == '\0' ||
	    task_id == NULL || task_id[0] == '\0')
		return FALSE;
	update_task.task_id = (gchar *)task_id;
	update_task.title = (gchar *)sakura_control_string(title);
	request.request_id = (gchar *)request_id;
	request.body_case = SAKURA__CONTROL__V1__REQUEST__BODY_UPDATE_TASK;
	request.update_task = &update_task;
	return sakura_control_pack_message(&request.base, payload);
}


gboolean
sakura_control_encode_set_task_archived_request(const gchar *request_id,
	                                               const gchar *task_id,
	                                               gboolean archived,
	                                               GByteArray *payload)
{
	Sakura__Control__V1__SetTaskArchivedRequest set_archived =
		SAKURA__CONTROL__V1__SET_TASK_ARCHIVED_REQUEST__INIT;
	Sakura__Control__V1__Request request =
		SAKURA__CONTROL__V1__REQUEST__INIT;

	if (payload == NULL || request_id == NULL || request_id[0] == '\0' ||
	    task_id == NULL || task_id[0] == '\0')
		return FALSE;
	set_archived.task_id = (gchar *)task_id;
	set_archived.archived = archived;
	request.request_id = (gchar *)request_id;
	request.body_case =
		SAKURA__CONTROL__V1__REQUEST__BODY_SET_TASK_ARCHIVED;
	request.set_task_archived = &set_archived;
	return sakura_control_pack_message(&request.base, payload);
}


gboolean
sakura_control_encode_delete_task_request(const gchar *request_id,
	                                          const gchar *task_id,
	                                          GByteArray *payload)
{
	Sakura__Control__V1__DeleteTaskRequest delete_task =
		SAKURA__CONTROL__V1__DELETE_TASK_REQUEST__INIT;
	Sakura__Control__V1__Request request =
		SAKURA__CONTROL__V1__REQUEST__INIT;

	if (payload == NULL || request_id == NULL || request_id[0] == '\0' ||
	    task_id == NULL || task_id[0] == '\0')
		return FALSE;
	delete_task.task_id = (gchar *)task_id;
	request.request_id = (gchar *)request_id;
	request.body_case = SAKURA__CONTROL__V1__REQUEST__BODY_DELETE_TASK;
	request.delete_task = &delete_task;
	return sakura_control_pack_message(&request.base, payload);
}


gboolean
sakura_control_encode_create_terminal_request(const gchar *request_id,
	                                             const gchar *terminal_id,
	                                             const gchar *group_id,
	                                             const gchar *task_id,
	                                             const gchar *cwd,
	                                             guint cols, guint rows,
	                                             GByteArray *payload)
{
	Sakura__Control__V1__CreateTerminalRequest create_terminal =
		SAKURA__CONTROL__V1__CREATE_TERMINAL_REQUEST__INIT;
	Sakura__Control__V1__Request request =
		SAKURA__CONTROL__V1__REQUEST__INIT;

	if (payload == NULL || request_id == NULL || request_id[0] == '\0')
		return FALSE;
	create_terminal.terminal_id = (gchar *)sakura_control_string(terminal_id);
	create_terminal.group_id = (gchar *)sakura_control_string(group_id);
	create_terminal.task_id = (gchar *)sakura_control_string(task_id);
	create_terminal.cwd = (gchar *)sakura_control_string(cwd);
	create_terminal.cols = cols;
	create_terminal.rows = rows;
	request.request_id = (gchar *)request_id;
	request.body_case = SAKURA__CONTROL__V1__REQUEST__BODY_CREATE_TERMINAL;
	request.create_terminal = &create_terminal;
	return sakura_control_pack_message(&request.base, payload);
}


gboolean
sakura_control_encode_terminal_input_request(const gchar *request_id,
	                                            const gchar *terminal_id,
	                                            const guint8 *data,
	                                            gsize data_length,
	                                            GByteArray *payload)
{
	Sakura__Control__V1__TerminalInput terminal_input =
		SAKURA__CONTROL__V1__TERMINAL_INPUT__INIT;
	Sakura__Control__V1__Request request =
		SAKURA__CONTROL__V1__REQUEST__INIT;

	if (payload == NULL || request_id == NULL || request_id[0] == '\0' ||
	    terminal_id == NULL || terminal_id[0] == '\0' ||
	    (data == NULL && data_length != 0))
		return FALSE;
	terminal_input.terminal_id = (gchar *)terminal_id;
	terminal_input.data.data = (guint8 *)data;
	terminal_input.data.len = data_length;
	request.request_id = (gchar *)request_id;
	request.body_case = SAKURA__CONTROL__V1__REQUEST__BODY_TERMINAL_INPUT;
	request.terminal_input = &terminal_input;
	return sakura_control_pack_message(&request.base, payload);
}


gboolean
sakura_control_encode_terminal_resize_request(const gchar *request_id,
	                                             const gchar *terminal_id,
	                                             guint cols, guint rows,
	                                             GByteArray *payload)
{
	Sakura__Control__V1__TerminalResize terminal_resize =
		SAKURA__CONTROL__V1__TERMINAL_RESIZE__INIT;
	Sakura__Control__V1__Request request =
		SAKURA__CONTROL__V1__REQUEST__INIT;

	if (payload == NULL || request_id == NULL || request_id[0] == '\0' ||
	    terminal_id == NULL || terminal_id[0] == '\0')
		return FALSE;
	terminal_resize.terminal_id = (gchar *)terminal_id;
	terminal_resize.cols = cols;
	terminal_resize.rows = rows;
	request.request_id = (gchar *)request_id;
	request.body_case = SAKURA__CONTROL__V1__REQUEST__BODY_TERMINAL_RESIZE;
	request.terminal_resize = &terminal_resize;
	return sakura_control_pack_message(&request.base, payload);
}


gboolean
sakura_control_encode_close_terminal_request(const gchar *request_id,
	                                            const gchar *terminal_id,
	                                            GByteArray *payload)
{
	Sakura__Control__V1__CloseSessionRequest close_terminal =
		SAKURA__CONTROL__V1__CLOSE_SESSION_REQUEST__INIT;
	Sakura__Control__V1__Request request =
		SAKURA__CONTROL__V1__REQUEST__INIT;

	if (payload == NULL || request_id == NULL || request_id[0] == '\0' ||
	    terminal_id == NULL || terminal_id[0] == '\0')
		return FALSE;
	close_terminal.terminal_id = (gchar *)terminal_id;
	request.request_id = (gchar *)request_id;
	request.body_case = SAKURA__CONTROL__V1__REQUEST__BODY_CLOSE_SESSION;
	request.close_session = &close_terminal;
	return sakura_control_pack_message(&request.base, payload);
}


gboolean
sakura_control_encode_attach_terminal_request(const gchar *request_id,
	                                             const gchar *terminal_id,
	                                             guint cols, guint rows,
	                                             GByteArray *payload)
{
	Sakura__Control__V1__AttachTerminalRequest attach =
		SAKURA__CONTROL__V1__ATTACH_TERMINAL_REQUEST__INIT;
	Sakura__Control__V1__Request request =
		SAKURA__CONTROL__V1__REQUEST__INIT;

	if (payload == NULL || request_id == NULL || request_id[0] == '\0' ||
	    terminal_id == NULL || terminal_id[0] == '\0')
		return FALSE;
	attach.terminal_id = (gchar *)terminal_id;
	attach.cols = cols;
	attach.rows = rows;
	request.request_id = (gchar *)request_id;
	request.body_case = SAKURA__CONTROL__V1__REQUEST__BODY_ATTACH_TERMINAL;
	request.attach_terminal = &attach;
	return sakura_control_pack_message(&request.base, payload);
}


gboolean
sakura_control_encode_detach_terminal_request(const gchar *request_id,
	                                             const gchar *terminal_id,
	                                             GByteArray *payload)
{
	Sakura__Control__V1__DetachTerminalRequest detach =
		SAKURA__CONTROL__V1__DETACH_TERMINAL_REQUEST__INIT;
	Sakura__Control__V1__Request request =
		SAKURA__CONTROL__V1__REQUEST__INIT;

	if (payload == NULL || request_id == NULL || request_id[0] == '\0' ||
	    terminal_id == NULL || terminal_id[0] == '\0')
		return FALSE;
	detach.terminal_id = (gchar *)terminal_id;
	request.request_id = (gchar *)request_id;
	request.body_case = SAKURA__CONTROL__V1__REQUEST__BODY_DETACH_TERMINAL;
	request.detach_terminal = &detach;
	return sakura_control_pack_message(&request.base, payload);
}


gboolean
sakura_control_encode_subscribe_events_request(const gchar *request_id,
	                                             guint64 after_sequence,
	                                             GByteArray *payload)
{
	Sakura__Control__V1__SubscribeEventsRequest subscribe =
		SAKURA__CONTROL__V1__SUBSCRIBE_EVENTS_REQUEST__INIT;
	Sakura__Control__V1__Request request =
		SAKURA__CONTROL__V1__REQUEST__INIT;

	if (payload == NULL || request_id == NULL || request_id[0] == '\0')
		return FALSE;
	subscribe.after_sequence = after_sequence;
	request.request_id = (gchar *)request_id;
	request.body_case = SAKURA__CONTROL__V1__REQUEST__BODY_SUBSCRIBE_EVENTS;
	request.subscribe_events = &subscribe;
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
	switch (decoded->body_case) {
	case SAKURA__CONTROL__V1__REQUEST__BODY_GET_SNAPSHOT:
		if (decoded->get_snapshot != NULL)
			request->kind = SAKURA_CONTROL_REQUEST_GET_SNAPSHOT;
		break;
	case SAKURA__CONTROL__V1__REQUEST__BODY_CREATE_GROUP:
		if (decoded->create_group != NULL) {
			request->kind = SAKURA_CONTROL_REQUEST_CREATE_GROUP;
			request->parent_id = g_strdup(decoded->create_group->parent_id);
			request->title = g_strdup(decoded->create_group->title);
			request->directory = g_strdup(decoded->create_group->directory);
		}
		break;
	case SAKURA__CONTROL__V1__REQUEST__BODY_CREATE_TASK:
		if (decoded->create_task != NULL) {
			request->kind = SAKURA_CONTROL_REQUEST_CREATE_TASK;
			request->group_id = g_strdup(decoded->create_task->group_id);
			request->parent_id = g_strdup(decoded->create_task->parent_id);
			request->title = g_strdup(decoded->create_task->title);
			request->provider = g_strdup(decoded->create_task->provider);
			request->external_id = g_strdup(decoded->create_task->external_id);
			request->url = g_strdup(decoded->create_task->url);
		}
		break;
	case SAKURA__CONTROL__V1__REQUEST__BODY_UPDATE_GROUP:
		if (decoded->update_group != NULL) {
			request->kind = SAKURA_CONTROL_REQUEST_UPDATE_GROUP;
			request->group_id = g_strdup(decoded->update_group->group_id);
			request->title = g_strdup(decoded->update_group->title);
			request->directory = g_strdup(decoded->update_group->directory);
		}
		break;
	case SAKURA__CONTROL__V1__REQUEST__BODY_SET_GROUP_ARCHIVED:
		if (decoded->set_group_archived != NULL) {
			request->kind = SAKURA_CONTROL_REQUEST_SET_GROUP_ARCHIVED;
			request->group_id = g_strdup(
				decoded->set_group_archived->group_id);
			request->archived = decoded->set_group_archived->archived;
		}
		break;
	case SAKURA__CONTROL__V1__REQUEST__BODY_DELETE_GROUP:
		if (decoded->delete_group != NULL) {
			request->kind = SAKURA_CONTROL_REQUEST_DELETE_GROUP;
			request->group_id = g_strdup(decoded->delete_group->group_id);
		}
		break;
	case SAKURA__CONTROL__V1__REQUEST__BODY_UPDATE_TASK:
		if (decoded->update_task != NULL) {
			request->kind = SAKURA_CONTROL_REQUEST_UPDATE_TASK;
			request->task_id = g_strdup(decoded->update_task->task_id);
			request->title = g_strdup(decoded->update_task->title);
		}
		break;
	case SAKURA__CONTROL__V1__REQUEST__BODY_SET_TASK_ARCHIVED:
		if (decoded->set_task_archived != NULL) {
			request->kind = SAKURA_CONTROL_REQUEST_SET_TASK_ARCHIVED;
			request->task_id = g_strdup(decoded->set_task_archived->task_id);
			request->archived = decoded->set_task_archived->archived;
		}
		break;
	case SAKURA__CONTROL__V1__REQUEST__BODY_DELETE_TASK:
		if (decoded->delete_task != NULL) {
			request->kind = SAKURA_CONTROL_REQUEST_DELETE_TASK;
			request->task_id = g_strdup(decoded->delete_task->task_id);
		}
		break;
	case SAKURA__CONTROL__V1__REQUEST__BODY_CREATE_TERMINAL:
		if (decoded->create_terminal != NULL) {
			request->kind = SAKURA_CONTROL_REQUEST_CREATE_TERMINAL;
			request->terminal_id = g_strdup(decoded->create_terminal->terminal_id);
			request->group_id = g_strdup(decoded->create_terminal->group_id);
			request->task_id = g_strdup(decoded->create_terminal->task_id);
			request->cwd = g_strdup(decoded->create_terminal->cwd);
			request->cols = decoded->create_terminal->cols;
			request->rows = decoded->create_terminal->rows;
		}
		break;
	case SAKURA__CONTROL__V1__REQUEST__BODY_TERMINAL_INPUT:
		if (decoded->terminal_input != NULL) {
			request->kind = SAKURA_CONTROL_REQUEST_TERMINAL_INPUT;
			request->terminal_id = g_strdup(decoded->terminal_input->terminal_id);
			request->input_length = decoded->terminal_input->data.len;
			if (request->input_length != 0) {
				request->input_data = g_malloc(request->input_length);
				memcpy(request->input_data, decoded->terminal_input->data.data,
				       request->input_length);
			}
		}
		break;
	case SAKURA__CONTROL__V1__REQUEST__BODY_TERMINAL_RESIZE:
		if (decoded->terminal_resize != NULL) {
			request->kind = SAKURA_CONTROL_REQUEST_TERMINAL_RESIZE;
			request->terminal_id = g_strdup(decoded->terminal_resize->terminal_id);
			request->cols = decoded->terminal_resize->cols;
			request->rows = decoded->terminal_resize->rows;
		}
		break;
	case SAKURA__CONTROL__V1__REQUEST__BODY_CLOSE_SESSION:
		if (decoded->close_session != NULL) {
			request->kind = SAKURA_CONTROL_REQUEST_CLOSE_TERMINAL;
			request->terminal_id = g_strdup(decoded->close_session->terminal_id);
		}
		break;
	case SAKURA__CONTROL__V1__REQUEST__BODY_ATTACH_TERMINAL:
		if (decoded->attach_terminal != NULL) {
			request->kind = SAKURA_CONTROL_REQUEST_ATTACH_TERMINAL;
			request->terminal_id = g_strdup(
				decoded->attach_terminal->terminal_id);
			request->cols = decoded->attach_terminal->cols;
			request->rows = decoded->attach_terminal->rows;
		}
		break;
	case SAKURA__CONTROL__V1__REQUEST__BODY_DETACH_TERMINAL:
		if (decoded->detach_terminal != NULL) {
			request->kind = SAKURA_CONTROL_REQUEST_DETACH_TERMINAL;
			request->terminal_id = g_strdup(
				decoded->detach_terminal->terminal_id);
		}
		break;
	case SAKURA__CONTROL__V1__REQUEST__BODY_SUBSCRIBE_EVENTS:
		if (decoded->subscribe_events != NULL) {
			request->kind = SAKURA_CONTROL_REQUEST_SUBSCRIBE_EVENTS;
			request->after_sequence = decoded->subscribe_events->after_sequence;
		}
		break;
	default:
		break;
	}
	sakura__control__v1__request__free_unpacked(decoded, NULL);
	if (request->request_id == NULL ||
	    request->kind == SAKURA_CONTROL_REQUEST_NONE)
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


static void
sakura_control_fill_terminal(Sakura__Control__V1__Terminal *message,
	                            const SakuraCoreTerminal *terminal)
{
	sakura__control__v1__terminal__init(message);
	message->id = (gchar *)sakura_control_string(terminal->id);
	message->group_id = (gchar *)sakura_control_string(
		terminal->group != NULL ? terminal->group->id : "root");
	message->task_id = (gchar *)sakura_control_string(
		terminal->task != NULL ? terminal->task->id : NULL);
	message->cwd = (gchar *)sakura_control_string(terminal->cwd);
	message->title = (gchar *)sakura_control_string(terminal->title);
	message->cols = terminal->cols;
	message->rows = terminal->rows;
	message->status = terminal->status;
}


static void
sakura_control_fill_snapshot(
	Sakura__Control__V1__WorkspaceSnapshot *snapshot,
	const SakuraCoreWorkspace *workspace)
{
	GPtrArray *groups;
	GPtrArray *tasks;
	GPtrArray *terminals;

	sakura__control__v1__workspace_snapshot__init(snapshot);
	groups = sakura_core_workspace_ordered_groups(workspace);
	if (groups->len != 0) {
		snapshot->groups = g_new0(Sakura__Control__V1__Group *, groups->len);
		for (guint index = 0; index < groups->len; index++) {
			Sakura__Control__V1__Group *group_message = g_new0(
				Sakura__Control__V1__Group, 1);

			sakura_control_fill_group(group_message,
			                          g_ptr_array_index(groups, index));
			snapshot->groups[index] = group_message;
		}
		snapshot->n_groups = groups->len;
	}
	g_ptr_array_unref(groups);

	tasks = sakura_core_workspace_ordered_tasks(workspace);
	if (tasks->len != 0) {
		snapshot->tasks = g_new0(Sakura__Control__V1__Task *, tasks->len);
		for (guint index = 0; index < tasks->len; index++) {
			Sakura__Control__V1__Task *task_message = g_new0(
				Sakura__Control__V1__Task, 1);

			sakura_control_fill_task(task_message,
			                         g_ptr_array_index(tasks, index));
			snapshot->tasks[index] = task_message;
		}
		snapshot->n_tasks = tasks->len;
	}
	g_ptr_array_unref(tasks);

	terminals = workspace != NULL && workspace->terminals != NULL
	          ? workspace->terminals : g_ptr_array_new();
	if (terminals->len != 0) {
		snapshot->terminals = g_new0(Sakura__Control__V1__Terminal *,
		                             terminals->len);
		for (guint index = 0; index < terminals->len; index++) {
			Sakura__Control__V1__Terminal *terminal_message = g_new0(
				Sakura__Control__V1__Terminal, 1);

			sakura_control_fill_terminal(terminal_message,
			                             g_ptr_array_index(terminals, index));
			snapshot->terminals[index] = terminal_message;
		}
		snapshot->n_terminals = terminals->len;
	}
	if (terminals != workspace->terminals)
		g_ptr_array_unref(terminals);

	snapshot->active_group_id = (gchar *)sakura_control_string(
		workspace->active_group != NULL ? workspace->active_group->id : "root");
	snapshot->active_task_id = (gchar *)sakura_control_string(
		workspace->active_task != NULL ? workspace->active_task->id : NULL);
	snapshot->root_directory = (gchar *)sakura_control_string(
		workspace->root_group != NULL ? workspace->root_group->directory : NULL);
	snapshot->sequence = 0;
}


static void
sakura_control_free_snapshot_messages(
	Sakura__Control__V1__WorkspaceSnapshot *snapshot)
{
	for (gsize index = 0; index < snapshot->n_groups; index++)
		g_free(snapshot->groups[index]);
	for (gsize index = 0; index < snapshot->n_tasks; index++)
		g_free(snapshot->tasks[index]);
	for (gsize index = 0; index < snapshot->n_terminals; index++)
		g_free(snapshot->terminals[index]);
	g_free(snapshot->groups);
	g_free(snapshot->tasks);
	g_free(snapshot->terminals);
}


gboolean
sakura_control_encode_snapshot_response(const gchar *request_id,
	                                    guint64 sequence,
	                                    const SakuraCoreWorkspace *workspace,
	                                    GByteArray *payload)
{
	Sakura__Control__V1__WorkspaceSnapshot snapshot =
		SAKURA__CONTROL__V1__WORKSPACE_SNAPSHOT__INIT;
	Sakura__Control__V1__Response response =
		SAKURA__CONTROL__V1__RESPONSE__INIT;

	if (request_id == NULL || workspace == NULL || payload == NULL)
		return FALSE;
	sakura_control_fill_snapshot(&snapshot, workspace);
	snapshot.sequence = sequence;
	response.request_id = (gchar *)request_id;
	response.body_case = SAKURA__CONTROL__V1__RESPONSE__BODY_SNAPSHOT;
	response.snapshot = &snapshot;

	{
		gboolean result = sakura_control_pack_message(&response.base, payload);

		sakura_control_free_snapshot_messages(&snapshot);
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
sakura_control_encode_accepted_response(const gchar *request_id,
	                                      const gchar *kind,
	                                      const gchar *id,
	                                      GByteArray *payload)
{
	Sakura__Control__V1__EntityRef accepted =
		SAKURA__CONTROL__V1__ENTITY_REF__INIT;
	Sakura__Control__V1__Response response =
		SAKURA__CONTROL__V1__RESPONSE__INIT;

	if (request_id == NULL || payload == NULL)
		return FALSE;
	accepted.kind = (gchar *)sakura_control_string(kind);
	accepted.id = (gchar *)sakura_control_string(id);
	response.request_id = (gchar *)request_id;
	response.body_case = SAKURA__CONTROL__V1__RESPONSE__BODY_ACCEPTED;
	response.accepted = &accepted;
	return sakura_control_pack_message(&response.base, payload);
}


gboolean
sakura_control_encode_terminal_attachment_response(
	const gchar *request_id, const SakuraCoreTerminal *terminal,
	const guint8 *replay_data, gsize replay_data_length, GByteArray *payload)
{
	Sakura__Control__V1__TerminalAttachment attachment =
		SAKURA__CONTROL__V1__TERMINAL_ATTACHMENT__INIT;
	Sakura__Control__V1__Response response =
		SAKURA__CONTROL__V1__RESPONSE__INIT;

	if (request_id == NULL || terminal == NULL || payload == NULL ||
	    (replay_data == NULL && replay_data_length != 0))
		return FALSE;
	attachment.terminal_id = (gchar *)sakura_control_string(terminal->id);
	attachment.cols = terminal->cols;
	attachment.rows = terminal->rows;
	attachment.status = terminal->status;
	attachment.replay_data.data = (guint8 *)replay_data;
	attachment.replay_data.len = replay_data_length;
	response.request_id = (gchar *)request_id;
	response.body_case =
		SAKURA__CONTROL__V1__RESPONSE__BODY_TERMINAL_ATTACHMENT;
	response.terminal_attachment = &attachment;
	return sakura_control_pack_message(&response.base, payload);
}


gboolean
sakura_control_encode_workspace_changed_event(
	guint64 sequence, const SakuraCoreWorkspace *workspace, GByteArray *payload)
{
	Sakura__Control__V1__WorkspaceSnapshot snapshot =
		SAKURA__CONTROL__V1__WORKSPACE_SNAPSHOT__INIT;
	Sakura__Control__V1__WorkspaceChanged changed =
		SAKURA__CONTROL__V1__WORKSPACE_CHANGED__INIT;
	Sakura__Control__V1__Event event = SAKURA__CONTROL__V1__EVENT__INIT;

	if (workspace == NULL || payload == NULL)
		return FALSE;
	sakura_control_fill_snapshot(&snapshot, workspace);
	snapshot.sequence = sequence;
	changed.snapshot = &snapshot;
	event.sequence = sequence;
	event.body_case = SAKURA__CONTROL__V1__EVENT__BODY_WORKSPACE_CHANGED;
	event.workspace_changed = &changed;
	{
		gboolean result = sakura_control_pack_message(&event.base, payload);

		sakura_control_free_snapshot_messages(&snapshot);
		return result;
	}
}


gboolean
sakura_control_encode_terminal_output_event(guint64 sequence,
	                                          const gchar *terminal_id,
	                                          const guint8 *data,
	                                          gsize data_length,
	                                          gboolean final_chunk,
	                                          GByteArray *payload)
{
	Sakura__Control__V1__TerminalOutput output =
		SAKURA__CONTROL__V1__TERMINAL_OUTPUT__INIT;
	Sakura__Control__V1__Event event = SAKURA__CONTROL__V1__EVENT__INIT;

	if (payload == NULL || terminal_id == NULL || terminal_id[0] == '\0' ||
	    (data == NULL && data_length != 0))
		return FALSE;
	output.terminal_id = (gchar *)terminal_id;
	output.data.data = (guint8 *)data;
	output.data.len = data_length;
	output.final_chunk = final_chunk;
	event.sequence = sequence;
	event.body_case = SAKURA__CONTROL__V1__EVENT__BODY_TERMINAL_OUTPUT;
	event.terminal_output = &output;
	return sakura_control_pack_message(&event.base, payload);
}


gboolean
sakura_control_encode_terminal_status_event(guint64 sequence,
	                                          const gchar *terminal_id,
	                                          guint status,
	                                          const gchar *message,
	                                          GByteArray *payload)
{
	Sakura__Control__V1__TerminalStatusChanged changed =
		SAKURA__CONTROL__V1__TERMINAL_STATUS_CHANGED__INIT;
	Sakura__Control__V1__Event event = SAKURA__CONTROL__V1__EVENT__INIT;

	if (payload == NULL || terminal_id == NULL || terminal_id[0] == '\0')
		return FALSE;
	changed.terminal_id = (gchar *)terminal_id;
	changed.status = status;
	changed.message = (gchar *)sakura_control_string(message);
	event.sequence = sequence;
	event.body_case =
		SAKURA__CONTROL__V1__EVENT__BODY_TERMINAL_STATUS_CHANGED;
	event.terminal_status_changed = &changed;
	return sakura_control_pack_message(&event.base, payload);
}


gboolean
sakura_control_decode_response(const guint8 *payload,
	                             gsize payload_length,
	                             SakuraControlResponse *response,
	                             GError **error)
{
	Sakura__Control__V1__Response *decoded;
	gboolean has_snapshot;
	gboolean accepted;
	gboolean attached;

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
	accepted =
		decoded->body_case == SAKURA__CONTROL__V1__RESPONSE__BODY_ACCEPTED &&
		decoded->accepted != NULL;
	attached =
		decoded->body_case ==
			SAKURA__CONTROL__V1__RESPONSE__BODY_TERMINAL_ATTACHMENT &&
		decoded->terminal_attachment != NULL;
	if (accepted) {
		response->accepted_kind = g_strdup(decoded->accepted->kind);
		response->accepted_id = g_strdup(decoded->accepted->id);
	}
	if (attached) {
		Sakura__Control__V1__TerminalAttachment *attachment =
			decoded->terminal_attachment;

		response->attached_terminal_id = g_strdup(attachment->terminal_id);
		response->attached_cols = attachment->cols;
		response->attached_rows = attachment->rows;
		response->attached_status = attachment->status;
		response->attached_output_length = attachment->replay_data.len;
		if (response->attached_output_length != 0) {
			response->attached_output = g_malloc(
				response->attached_output_length);
			memcpy(response->attached_output, attachment->replay_data.data,
			       response->attached_output_length);
		}
	}
	sakura__control__v1__response__free_unpacked(decoded, NULL);
	response->has_snapshot = has_snapshot;
	response->accepted = accepted;
	response->attached = attached;
	if (response->request_id == NULL ||
	    (!response->has_snapshot && !response->accepted && !response->attached))
		return sakura_control_error(error, "unsupported control response");
	return TRUE;
}


gboolean
sakura_control_decode_workspace_changed_event(
	const guint8 *payload, gsize payload_length, guint64 *sequence,
	SakuraSessionSnapshot **snapshot, GError **error)
{
	Sakura__Control__V1__Event *decoded;
	Sakura__Control__V1__WorkspaceSnapshot *wire_snapshot;
	SakuraSessionSnapshot *decoded_snapshot;

	if (payload == NULL || sequence == NULL || snapshot == NULL)
		return sakura_control_error(error, "invalid control event");
	*snapshot = NULL;
	decoded = sakura__control__v1__event__unpack(NULL, payload_length, payload);
	if (decoded == NULL ||
	    decoded->body_case != SAKURA__CONTROL__V1__EVENT__BODY_WORKSPACE_CHANGED ||
	    decoded->workspace_changed == NULL ||
	    decoded->workspace_changed->snapshot == NULL) {
		if (decoded != NULL)
			sakura__control__v1__event__free_unpacked(decoded, NULL);
		return sakura_control_error(error, "unsupported control event");
	}
	wire_snapshot = decoded->workspace_changed->snapshot;
	decoded_snapshot = sakura_session_snapshot_new();
	g_free(decoded_snapshot->active_group_id);
	decoded_snapshot->active_group_id = g_strdup(wire_snapshot->active_group_id);
	decoded_snapshot->root_directory = g_strdup(wire_snapshot->root_directory);
	for (gsize index = 0; index < wire_snapshot->n_groups; index++) {
		Sakura__Control__V1__Group *wire_group = wire_snapshot->groups[index];
		SakuraSessionGroupRecord *group = g_new0(SakuraSessionGroupRecord, 1);

		group->id = g_strdup(wire_group->id);
		group->parent_id = g_strdup(wire_group->parent_id);
		group->title = g_strdup(wire_group->title);
		group->directory = g_strdup(wire_group->directory);
		group->order = wire_group->order;
		group->archived = wire_group->archived;
		g_ptr_array_add(decoded_snapshot->groups, group);
	}
	for (gsize index = 0; index < wire_snapshot->n_tasks; index++) {
		Sakura__Control__V1__Task *wire_task = wire_snapshot->tasks[index];
		SakuraSessionTaskRecord *task = g_new0(SakuraSessionTaskRecord, 1);

		task->id = g_strdup(wire_task->id);
		task->parent_id = g_strdup(wire_task->parent_id);
		task->group_id = g_strdup(wire_task->group_id);
		task->title = g_strdup(wire_task->title);
		task->provider = g_strdup(wire_task->provider);
		task->external_id = g_strdup(wire_task->external_id);
		task->url = g_strdup(wire_task->url);
		task->status = wire_task->status;
		task->order = wire_task->order;
		task->archived = wire_task->archived;
		g_ptr_array_add(decoded_snapshot->tasks, task);
	}
	*sequence = decoded->sequence;
	*snapshot = decoded_snapshot;
	sakura__control__v1__event__free_unpacked(decoded, NULL);
	return TRUE;
}


gboolean
sakura_control_decode_terminal_output_event(const guint8 *payload,
	                                           gsize payload_length,
	                                           guint64 *sequence,
	                                           gchar **terminal_id,
	                                           guint8 **data,
	                                           gsize *data_length,
	                                           gboolean *final_chunk,
	                                           GError **error)
{
	Sakura__Control__V1__Event *decoded;
	Sakura__Control__V1__TerminalOutput *output;

	if (payload == NULL || sequence == NULL || terminal_id == NULL ||
	    data == NULL || data_length == NULL || final_chunk == NULL)
		return sakura_control_error(error, "invalid terminal output event");
	*terminal_id = NULL;
	*data = NULL;
	*data_length = 0;
	*final_chunk = FALSE;
	decoded = sakura__control__v1__event__unpack(NULL, payload_length, payload);
	if (decoded == NULL ||
	    decoded->body_case != SAKURA__CONTROL__V1__EVENT__BODY_TERMINAL_OUTPUT ||
	    decoded->terminal_output == NULL) {
		if (decoded != NULL)
			sakura__control__v1__event__free_unpacked(decoded, NULL);
		return sakura_control_error(error, "unsupported terminal output event");
	}
	output = decoded->terminal_output;
	*sequence = decoded->sequence;
	*terminal_id = g_strdup(output->terminal_id);
	*data_length = output->data.len;
	if (*data_length != 0) {
		*data = g_malloc(*data_length);
		memcpy(*data, output->data.data, *data_length);
	}
	*final_chunk = output->final_chunk;
	sakura__control__v1__event__free_unpacked(decoded, NULL);
	return TRUE;
}


gboolean
sakura_control_decode_terminal_status_event(const guint8 *payload,
	                                           gsize payload_length,
	                                           guint64 *sequence,
	                                           gchar **terminal_id,
	                                           guint *status,
	                                           gchar **message,
	                                           GError **error)
{
	Sakura__Control__V1__Event *decoded;
	Sakura__Control__V1__TerminalStatusChanged *changed;

	if (payload == NULL || sequence == NULL || terminal_id == NULL ||
	    status == NULL || message == NULL)
		return sakura_control_error(error, "invalid terminal status event");
	*terminal_id = NULL;
	*status = 0;
	*message = NULL;
	decoded = sakura__control__v1__event__unpack(NULL, payload_length, payload);
	if (decoded == NULL ||
	    decoded->body_case !=
			SAKURA__CONTROL__V1__EVENT__BODY_TERMINAL_STATUS_CHANGED ||
	    decoded->terminal_status_changed == NULL) {
		if (decoded != NULL)
			sakura__control__v1__event__free_unpacked(decoded, NULL);
		return sakura_control_error(error, "unsupported terminal status event");
	}
	changed = decoded->terminal_status_changed;
	*sequence = decoded->sequence;
	*terminal_id = g_strdup(changed->terminal_id);
	*status = changed->status;
	*message = g_strdup(changed->message);
	sakura__control__v1__event__free_unpacked(decoded, NULL);
	return TRUE;
}
