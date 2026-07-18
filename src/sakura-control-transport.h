#ifndef SAKURA_CONTROL_TRANSPORT_H
#define SAKURA_CONTROL_TRANSPORT_H

#include <gio/gio.h>

#include "sakura-core.h"

/* The protobuf payload is framed separately so the same generated messages
 * can later be carried by an HTTP or WebSocket adapter. */
#define SAKURA_CONTROL_PROTOCOL_VERSION 1
#define SAKURA_CONTROL_MAX_FRAME (1024 * 1024)

typedef struct {
	gchar *request_id;
	gboolean get_snapshot;
} SakuraControlRequest;

typedef struct {
	gchar *request_id;
	gboolean has_snapshot;
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
gboolean sakura_control_decode_request(const guint8 *payload,
	                                     gsize payload_length,
	                                     SakuraControlRequest *request,
	                                     GError **error);
gboolean sakura_control_encode_snapshot_response(
	const gchar *request_id, const SakuraCoreWorkspace *workspace,
	GByteArray *payload);
gboolean sakura_control_encode_error_response(const gchar *request_id,
	                                            const gchar *code,
	                                            const gchar *message,
	                                            GByteArray *payload);
gboolean sakura_control_decode_response(const guint8 *payload,
	                                      gsize payload_length,
	                                      SakuraControlResponse *response,
	                                      GError **error);

#endif /* SAKURA_CONTROL_TRANSPORT_H */
