#ifndef SAKURA_CONTROL_CLIENT_H
#define SAKURA_CONTROL_CLIENT_H

#include <gio/gio.h>

#define SAKURA_CONTROL_CLIENT_API_VERSION 1
#define SAKURA_CONTROL_PROTOCOL_VERSION 2
#define SAKURA_CONTROL_MAX_FRAME (1024 * 1024)
#define SAKURA_CONTROL_CAPABILITY_WORKSPACE G_GUINT64_CONSTANT(1)
#define SAKURA_CONTROL_CAPABILITY_TERMINALS G_GUINT64_CONSTANT(2)
#define SAKURA_CONTROL_CAPABILITY_TERMINAL_ATTACH G_GUINT64_CONSTANT(4)
#define SAKURA_CONTROL_CAPABILITY_EVENT_STREAM G_GUINT64_CONSTANT(8)
#define SAKURA_CONTROL_CAPABILITY_TERMINAL_RESTART G_GUINT64_CONSTANT(16)
#define SAKURA_CONTROL_CAPABILITY_GROUP_MOVE G_GUINT64_CONSTANT(32)
#define SAKURA_CONTROL_CAPABILITY_WORKSPACE_REVISIONS G_GUINT64_CONSTANT(64)
#define SAKURA_CONTROL_CAPABILITY_STRUCTURED_ERRORS G_GUINT64_CONSTANT(128)
#define SAKURA_CONTROL_CAPABILITY_TERMINAL_OUTPUT_OFFSETS G_GUINT64_CONSTANT(256)
#define SAKURA_CONTROL_CONNECT_TIMEOUT_SECONDS 2
#define SAKURA_CONTROL_HANDSHAKE_TIMEOUT_SECONDS 2
#define SAKURA_CONTROL_REQUEST_TIMEOUT_SECONDS 5

#define SAKURA_CONTROL_ERROR_INVALID_ARGUMENT "INVALID_ARGUMENT"
#define SAKURA_CONTROL_ERROR_NOT_FOUND "NOT_FOUND"
#define SAKURA_CONTROL_ERROR_ALREADY_EXISTS "ALREADY_EXISTS"
#define SAKURA_CONTROL_ERROR_REVISION_CONFLICT "REVISION_CONFLICT"
#define SAKURA_CONTROL_ERROR_INVALID_STATE "INVALID_STATE"
#define SAKURA_CONTROL_ERROR_UNSUPPORTED "UNSUPPORTED"
#define SAKURA_CONTROL_ERROR_UNAUTHORIZED "UNAUTHORIZED"
#define SAKURA_CONTROL_ERROR_TIMEOUT "TIMEOUT"
#define SAKURA_CONTROL_ERROR_INTERNAL "INTERNAL_ERROR"
#define SAKURA_CONTROL_ERROR_OUTPUT_GAP "OUTPUT_GAP"

typedef enum {
	SAKURA_CONTROL_ERROR_CODE_FAILED,
	SAKURA_CONTROL_ERROR_CODE_INVALID_ARGUMENT,
	SAKURA_CONTROL_ERROR_CODE_NOT_FOUND,
	SAKURA_CONTROL_ERROR_CODE_ALREADY_EXISTS,
	SAKURA_CONTROL_ERROR_CODE_REVISION_CONFLICT,
	SAKURA_CONTROL_ERROR_CODE_INVALID_STATE,
	SAKURA_CONTROL_ERROR_CODE_UNSUPPORTED,
	SAKURA_CONTROL_ERROR_CODE_UNAUTHORIZED,
	SAKURA_CONTROL_ERROR_CODE_TIMEOUT,
	SAKURA_CONTROL_ERROR_CODE_INTERNAL,
	SAKURA_CONTROL_ERROR_CODE_OUTPUT_GAP
} SakuraControlError;

#define SAKURA_CONTROL_ERROR_DOMAIN (sakura_control_error_quark())
#define SAKURA_CONTROL_ERROR SAKURA_CONTROL_ERROR_DOMAIN

GQuark sakura_control_error_quark(void);

typedef struct _SakuraControlClientConnection SakuraControlClientConnection;

typedef struct {
	gchar *terminal_id;
	guint cols;
	guint rows;
	guint status;
	guint8 *output;
	gsize output_length;
	guint64 output_start_offset;
	guint64 output_end_offset;
} SakuraControlTerminalAttachment;

typedef enum {
	SAKURA_CONTROL_CLIENT_EVENT_UNKNOWN,
	SAKURA_CONTROL_CLIENT_EVENT_TERMINAL_OUTPUT,
	SAKURA_CONTROL_CLIENT_EVENT_TERMINAL_STATUS
} SakuraControlClientEventType;

typedef struct {
	SakuraControlClientEventType type;
	guint64 sequence;
	gchar *terminal_id;
	guint8 *data;
	gsize data_length;
	guint64 start_offset;
	guint64 end_offset;
	gboolean final_chunk;
	guint status;
	gchar *message;
	guint8 *payload;
	gsize payload_length;
} SakuraControlClientEvent;

void sakura_control_terminal_attachment_clear(
	SakuraControlTerminalAttachment *attachment);
void sakura_control_client_event_clear(SakuraControlClientEvent *event);
gboolean sakura_control_validate_local_endpoint(const gchar *socket_path,
	GError **error);

/*
 * Opens a local agent connection and completes the protocol handshake.
 * The returned connection is safe to reference from another thread so that
 * one thread can stop a reader while the owning thread is shutting down.
 */
SakuraControlClientConnection *sakura_control_client_connect(
	const gchar *socket_path, const gchar *workspace_id,
	const gchar *client_name, guint64 required_capabilities, GError **error);
SakuraControlClientConnection *sakura_control_client_ref(
	SakuraControlClientConnection *connection);
void sakura_control_client_close(SakuraControlClientConnection *connection);
void sakura_control_client_shutdown(SakuraControlClientConnection *connection);
void sakura_control_client_unref(SakuraControlClientConnection *connection);

/*
 * Sends one already-encoded request and returns its serialized response
 * payload. Request/response encoding remains in
 * sakura-control-transport so clients can choose the messages they need.
 */
gboolean sakura_control_client_request(
	SakuraControlClientConnection *connection, const GByteArray *request,
	GByteArray **response, GError **error);
gboolean sakura_control_client_request_with_cancellable(
	SakuraControlClientConnection *connection, const GByteArray *request,
	GByteArray **response, GCancellable *cancellable, GError **error);

gboolean sakura_control_client_subscribe_events(
	SakuraControlClientConnection *connection, guint64 after_sequence,
	GError **error);
gboolean sakura_control_client_read_frame(
	SakuraControlClientConnection *connection, GByteArray **payload,
	GCancellable *cancellable, GError **error);

/* Terminal operations are the stable high-level API. Callers do not need
 * protocol or generated protobuf headers to use the client library. */
gboolean sakura_control_client_create_terminal(
	SakuraControlClientConnection *connection, const gchar *terminal_id,
	const gchar *group_id, const gchar *task_id, const gchar *cwd, guint cols,
	guint rows, gchar **created_terminal_id, GError **error);
gboolean sakura_control_client_attach_terminal(
	SakuraControlClientConnection *connection, const gchar *terminal_id,
	guint cols, guint rows, SakuraControlTerminalAttachment *attachment,
	GError **error);
gboolean sakura_control_client_attach_terminal_after_offset(
	SakuraControlClientConnection *connection, const gchar *terminal_id,
	guint cols, guint rows, guint64 after_output_offset,
	SakuraControlTerminalAttachment *attachment, GError **error);
gboolean sakura_control_client_terminal_input(
	SakuraControlClientConnection *connection, const gchar *terminal_id,
	const guint8 *data, gsize data_length, GError **error);
gboolean sakura_control_client_terminal_resize(
	SakuraControlClientConnection *connection, const gchar *terminal_id,
	guint cols, guint rows, GError **error);
gboolean sakura_control_client_detach_terminal(
	SakuraControlClientConnection *connection, const gchar *terminal_id,
	GError **error);
gboolean sakura_control_client_read_event(
	SakuraControlClientConnection *connection, SakuraControlClientEvent *event,
	GCancellable *cancellable, GError **error);

#endif /* SAKURA_CONTROL_CLIENT_H */
