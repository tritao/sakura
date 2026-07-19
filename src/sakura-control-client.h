#ifndef SAKURA_CONTROL_CLIENT_H
#define SAKURA_CONTROL_CLIENT_H

#include <gio/gio.h>

typedef struct _SakuraControlClientConnection SakuraControlClientConnection;

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

gboolean sakura_control_client_subscribe_events(
	SakuraControlClientConnection *connection, guint64 after_sequence,
	GError **error);
gboolean sakura_control_client_read_frame(
	SakuraControlClientConnection *connection, GByteArray **payload,
	GCancellable *cancellable, GError **error);

#endif /* SAKURA_CONTROL_CLIENT_H */
