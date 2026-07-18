#ifndef SAKURA_CONTROL_TRANSPORT_H
#define SAKURA_CONTROL_TRANSPORT_H

#include <gio/gio.h>

#include "sakura-core.h"

/* The protobuf payload is framed separately so the same generated messages
 * can later be carried by an HTTP or WebSocket adapter. */
#define SAKURA_CONTROL_PROTOCOL_VERSION 1
#define SAKURA_CONTROL_MAX_FRAME (1024 * 1024)
#define SAKURA_CONTROL_CAPABILITY_WORKSPACE G_GUINT64_CONSTANT(1)
#define SAKURA_CONTROL_CAPABILITY_TERMINALS G_GUINT64_CONSTANT(2)
#define SAKURA_CONTROL_CAPABILITY_TERMINAL_ATTACH G_GUINT64_CONSTANT(4)
#define SAKURA_CONTROL_CAPABILITY_EVENT_STREAM G_GUINT64_CONSTANT(8)

typedef enum {
	SAKURA_CONTROL_REQUEST_NONE,
	SAKURA_CONTROL_REQUEST_GET_SNAPSHOT,
	SAKURA_CONTROL_REQUEST_CREATE_GROUP,
	SAKURA_CONTROL_REQUEST_CREATE_TASK,
	SAKURA_CONTROL_REQUEST_UPDATE_GROUP,
	SAKURA_CONTROL_REQUEST_SET_GROUP_ARCHIVED,
	SAKURA_CONTROL_REQUEST_DELETE_GROUP,
	SAKURA_CONTROL_REQUEST_UPDATE_TASK,
	SAKURA_CONTROL_REQUEST_SET_TASK_ARCHIVED,
	SAKURA_CONTROL_REQUEST_DELETE_TASK,
	SAKURA_CONTROL_REQUEST_CREATE_TERMINAL,
	SAKURA_CONTROL_REQUEST_TERMINAL_INPUT,
	SAKURA_CONTROL_REQUEST_TERMINAL_RESIZE,
	SAKURA_CONTROL_REQUEST_CLOSE_TERMINAL,
	SAKURA_CONTROL_REQUEST_SUBSCRIBE_EVENTS,
	SAKURA_CONTROL_REQUEST_ATTACH_TERMINAL,
	SAKURA_CONTROL_REQUEST_DETACH_TERMINAL,
	SAKURA_CONTROL_REQUEST_HELLO
} SakuraControlRequestKind;

typedef struct {
	gchar *request_id;
	SakuraControlRequestKind kind;
	gchar *parent_id;
	gchar *title;
	gchar *directory;
	gchar *group_id;
	gchar *task_id;
	gchar *terminal_id;
	gchar *cwd;
	gchar *provider;
	gchar *external_id;
	gchar *url;
	gchar *client_name;
	gboolean archived;
	guint protocol_version;
	guint8 *input_data;
	gsize input_length;
	guint cols;
	guint rows;
	guint64 after_sequence;
} SakuraControlRequest;

typedef struct {
	gchar *request_id;
	gboolean has_snapshot;
	gboolean accepted;
	gchar *accepted_kind;
	gchar *accepted_id;
	gboolean hello;
	guint hello_protocol_version;
	gchar *agent_version;
	guint64 capabilities;
	gboolean attached;
	gchar *attached_terminal_id;
	guint attached_cols;
	guint attached_rows;
	guint attached_status;
	guint8 *attached_output;
	gsize attached_output_length;
} SakuraControlResponse;

void sakura_control_request_clear(SakuraControlRequest *request);
void sakura_control_response_clear(SakuraControlResponse *response);

gboolean sakura_control_frame_read(GInputStream *input,
	                                  GByteArray **payload,
	                                  GCancellable *cancellable,
	                                  GError **error);
gboolean sakura_control_frame_write(GOutputStream *output,
	                                   const guint8 *payload,
	                                   gsize payload_length,
	                                   GCancellable *cancellable,
	                                   GError **error);

gboolean sakura_control_encode_get_snapshot_request(const gchar *request_id,
	                                                  GByteArray *payload);
gboolean sakura_control_encode_hello_request(const gchar *request_id,
	                                           guint protocol_version,
	                                           const gchar *client_name,
	                                           GByteArray *payload);
gboolean sakura_control_encode_create_group_request(
	const gchar *request_id, const gchar *parent_id, const gchar *title,
	const gchar *directory, GByteArray *payload);
gboolean sakura_control_encode_create_task_request(
	const gchar *request_id, const gchar *group_id, const gchar *parent_id,
	const gchar *title, const gchar *provider, const gchar *external_id,
	const gchar *url, GByteArray *payload);
gboolean sakura_control_encode_update_group_request(
	const gchar *request_id, const gchar *group_id, const gchar *title,
	const gchar *directory, GByteArray *payload);
gboolean sakura_control_encode_set_group_archived_request(
	const gchar *request_id, const gchar *group_id, gboolean archived,
	GByteArray *payload);
gboolean sakura_control_encode_delete_group_request(
	const gchar *request_id, const gchar *group_id, GByteArray *payload);
gboolean sakura_control_encode_update_task_request(
	const gchar *request_id, const gchar *task_id, const gchar *title,
	GByteArray *payload);
gboolean sakura_control_encode_set_task_archived_request(
	const gchar *request_id, const gchar *task_id, gboolean archived,
	GByteArray *payload);
gboolean sakura_control_encode_delete_task_request(
	const gchar *request_id, const gchar *task_id, GByteArray *payload);
gboolean sakura_control_encode_create_terminal_request(
	const gchar *request_id, const gchar *terminal_id, const gchar *group_id,
	const gchar *task_id, const gchar *cwd, guint cols, guint rows,
	GByteArray *payload);
gboolean sakura_control_encode_terminal_input_request(
	const gchar *request_id, const gchar *terminal_id, const guint8 *data,
	gsize data_length, GByteArray *payload);
gboolean sakura_control_encode_terminal_resize_request(
	const gchar *request_id, const gchar *terminal_id, guint cols, guint rows,
	GByteArray *payload);
gboolean sakura_control_encode_close_terminal_request(
	const gchar *request_id, const gchar *terminal_id, GByteArray *payload);
gboolean sakura_control_encode_attach_terminal_request(
	const gchar *request_id, const gchar *terminal_id, guint cols, guint rows,
	GByteArray *payload);
gboolean sakura_control_encode_detach_terminal_request(
	const gchar *request_id, const gchar *terminal_id, GByteArray *payload);
gboolean sakura_control_encode_subscribe_events_request(
	const gchar *request_id, guint64 after_sequence, GByteArray *payload);
gboolean sakura_control_decode_request(const guint8 *payload,
	                                     gsize payload_length,
	                                     SakuraControlRequest *request,
	                                     GError **error);
gboolean sakura_control_encode_snapshot_response(
	const gchar *request_id, guint64 sequence,
	const SakuraCoreWorkspace *workspace,
	GByteArray *payload);
gboolean sakura_control_encode_error_response(const gchar *request_id,
	                                            const gchar *code,
	                                            const gchar *message,
	                                            GByteArray *payload);
gboolean sakura_control_encode_accepted_response(const gchar *request_id,
	                                               const gchar *kind,
	                                               const gchar *id,
	                                               GByteArray *payload);
gboolean sakura_control_encode_hello_response(const gchar *request_id,
	                                            guint protocol_version,
	                                            const gchar *agent_version,
	                                            guint64 capabilities,
	                                            GByteArray *payload);
gboolean sakura_control_encode_terminal_attachment_response(
	const gchar *request_id, const SakuraCoreTerminal *terminal,
	const guint8 *replay_data, gsize replay_data_length, GByteArray *payload);
gboolean sakura_control_encode_workspace_changed_event(
	guint64 sequence, const SakuraCoreWorkspace *workspace, GByteArray *payload);
gboolean sakura_control_encode_terminal_output_event(
	guint64 sequence, const gchar *terminal_id, const guint8 *data,
	gsize data_length, gboolean final_chunk, GByteArray *payload);
gboolean sakura_control_encode_terminal_status_event(
	guint64 sequence, const gchar *terminal_id, guint status,
	const gchar *message, GByteArray *payload);
gboolean sakura_control_decode_response(const guint8 *payload,
	                                      gsize payload_length,
	                                      SakuraControlResponse *response,
	                                      GError **error);
gboolean sakura_control_decode_workspace_changed_event(
	const guint8 *payload, gsize payload_length, guint64 *sequence,
	SakuraSessionSnapshot **snapshot, GError **error);
gboolean sakura_control_decode_terminal_output_event(
	const guint8 *payload, gsize payload_length, guint64 *sequence,
	gchar **terminal_id, guint8 **data, gsize *data_length,
	gboolean *final_chunk, GError **error);
gboolean sakura_control_decode_terminal_status_event(
	const guint8 *payload, gsize payload_length, guint64 *sequence,
	gchar **terminal_id, guint *status, gchar **message, GError **error);

#endif /* SAKURA_CONTROL_TRANSPORT_H */
