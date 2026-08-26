#ifndef SAKURA_CONTROL_DISCOVERY_H
#define SAKURA_CONTROL_DISCOVERY_H

#include <glib.h>

typedef struct {
	gchar *workspace_id;
	gchar *socket_path;
	gchar *session_path;
	gint64 pid;
} SakuraControlWorkspaceEndpoint;

void sakura_control_workspace_endpoint_free(
	SakuraControlWorkspaceEndpoint *endpoint);
gboolean sakura_control_workspace_publish(const gchar *workspace_id,
	const gchar *socket_path, const gchar *session_path, GError **error);
void sakura_control_workspace_unpublish(const gchar *workspace_id);
GPtrArray *sakura_control_workspace_discover(GError **error);

#endif
