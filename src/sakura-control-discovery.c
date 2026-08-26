#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib/gstdio.h>

#include "sakura-control-client.h"
#include "sakura-control-discovery.h"

static gchar *
sakura_control_workspace_directory(void)
{
	const gchar *runtime_dir = g_get_user_runtime_dir();

	if (runtime_dir != NULL && runtime_dir[0] != '\0')
		return g_build_filename(runtime_dir, "sakura", "workspaces", NULL);
	return g_build_filename(g_get_user_cache_dir(), "sakura", "workspaces", NULL);
}

static gchar *
sakura_control_workspace_descriptor_path(const gchar *workspace_id)
{
	g_autofree gchar *directory = sakura_control_workspace_directory();
	g_autofree gchar *hash = NULL;
	g_autofree gchar *name = NULL;

	if (workspace_id == NULL || workspace_id[0] == '\0')
		return NULL;
	hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, workspace_id, -1);
	name = g_strdup_printf("%s.ini", hash);
	return g_build_filename(directory, name, NULL);
}

void
sakura_control_workspace_endpoint_free(SakuraControlWorkspaceEndpoint *endpoint)
{
	if (endpoint == NULL)
		return;
	g_free(endpoint->workspace_id);
	g_free(endpoint->socket_path);
	g_free(endpoint->session_path);
	g_free(endpoint);
}

gboolean
sakura_control_workspace_publish(const gchar *workspace_id,
	                              const gchar *socket_path,
	                              const gchar *session_path, GError **error)
{
	g_autofree gchar *directory = NULL;
	g_autofree gchar *descriptor_path = NULL;
	g_autofree gchar *temporary_path = NULL;
	g_autofree gchar *data = NULL;
	GKeyFile *key_file;
	gsize data_length = 0;
	gboolean success = FALSE;

	if (workspace_id == NULL || workspace_id[0] == '\0' ||
	    socket_path == NULL || socket_path[0] == '\0') {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "workspace identity and socket path are required");
		return FALSE;
	}
	directory = sakura_control_workspace_directory();
	if (g_mkdir_with_parents(directory, 0700) != 0 ||
	    chmod(directory, 0700) != 0) {
		g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
		            "could not prepare workspace discovery directory: %s",
		            g_strerror(errno));
		return FALSE;
	}
	descriptor_path = sakura_control_workspace_descriptor_path(workspace_id);
	temporary_path = g_strdup_printf("%s.tmp.%d", descriptor_path, (int)getpid());
	key_file = g_key_file_new();
	g_key_file_set_string(key_file, "Workspace", "id", workspace_id);
	g_key_file_set_string(key_file, "Workspace", "socket", socket_path);
	if (session_path != NULL && session_path[0] != '\0')
		g_key_file_set_string(key_file, "Workspace", "session", session_path);
	g_key_file_set_int64(key_file, "Workspace", "pid", (gint64)getpid());
	data = g_key_file_to_data(key_file, &data_length, error);
	g_key_file_free(key_file);
	if (data == NULL)
		return FALSE;
	if (!g_file_set_contents(temporary_path, data, data_length, error))
		goto out;
	if (chmod(temporary_path, 0600) != 0 ||
	    g_rename(temporary_path, descriptor_path) != 0) {
		g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
		            "could not publish workspace endpoint: %s", g_strerror(errno));
		goto out;
	}
	success = TRUE;
out:
	if (!success)
		g_remove(temporary_path);
	return success;
}

void
sakura_control_workspace_unpublish(const gchar *workspace_id)
{
	g_autofree gchar *descriptor_path =
		sakura_control_workspace_descriptor_path(workspace_id);

	if (descriptor_path != NULL)
		g_remove(descriptor_path);
}

static SakuraControlWorkspaceEndpoint *
sakura_control_workspace_endpoint_load(const gchar *path)
{
	SakuraControlWorkspaceEndpoint *endpoint = NULL;
	GKeyFile *key_file;
	GError *error = NULL;
	struct stat descriptor_stat;

	if (g_lstat(path, &descriptor_stat) != 0 ||
	    !S_ISREG(descriptor_stat.st_mode) || descriptor_stat.st_uid != getuid() ||
	    (descriptor_stat.st_mode & 0077) != 0)
		return NULL;
	key_file = g_key_file_new();
	if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, &error)) {
		g_clear_error(&error);
		g_key_file_free(key_file);
		return NULL;
	}
	endpoint = g_new0(SakuraControlWorkspaceEndpoint, 1);
	endpoint->workspace_id = g_key_file_get_string(key_file, "Workspace", "id", NULL);
	endpoint->socket_path = g_key_file_get_string(key_file, "Workspace", "socket", NULL);
	endpoint->session_path = g_key_file_get_string(key_file, "Workspace", "session", NULL);
	endpoint->pid = g_key_file_get_int64(key_file, "Workspace", "pid", NULL);
	g_key_file_free(key_file);
	if (endpoint->workspace_id == NULL || endpoint->workspace_id[0] == '\0' ||
	    endpoint->socket_path == NULL || endpoint->socket_path[0] == '\0' ||
	    endpoint->pid <= 0 ||
	    (kill((pid_t)endpoint->pid, 0) != 0 && errno != EPERM) ||
	    !sakura_control_validate_local_endpoint(endpoint->socket_path, NULL)) {
		sakura_control_workspace_endpoint_free(endpoint);
		return NULL;
	}
	return endpoint;
}

GPtrArray *
sakura_control_workspace_discover(GError **error)
{
	g_autofree gchar *directory = sakura_control_workspace_directory();
	GPtrArray *endpoints = g_ptr_array_new_with_free_func(
		(GDestroyNotify)sakura_control_workspace_endpoint_free);
	GDir *dir;
	const gchar *name;

	dir = g_dir_open(directory, 0, error);
	if (dir == NULL) {
		if (error != NULL && *error != NULL &&
		    (*error)->domain == G_FILE_ERROR &&
		    (*error)->code == G_FILE_ERROR_NOENT)
			g_clear_error(error);
		return endpoints;
	}
	while ((name = g_dir_read_name(dir)) != NULL) {
		g_autofree gchar *path = NULL;
		SakuraControlWorkspaceEndpoint *endpoint;

		if (!g_str_has_suffix(name, ".ini"))
			continue;
		path = g_build_filename(directory, name, NULL);
		endpoint = sakura_control_workspace_endpoint_load(path);
		if (endpoint != NULL)
			g_ptr_array_add(endpoints, endpoint);
	}
	g_dir_close(dir);
	return endpoints;
}
