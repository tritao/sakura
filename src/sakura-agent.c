#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pty.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gio/gio.h>
#include <glib-unix.h>
#include <glib/gstdio.h>

#include "sakura-control-transport.h"


typedef struct _SakuraAgent SakuraAgent;

typedef struct {
	GSocketConnection *connection;
	SakuraAgent *agent;
} SakuraAgentConnection;

typedef struct {
	SakuraAgent *agent;
	SakuraCoreTerminal *core;
	gchar *id;
	int master_fd;
	GPid pid;
	GThread *reader_thread;
	gboolean stopping;
} SakuraAgentTerminal;

struct _SakuraAgent {
	GMainLoop *loop;
	SakuraCoreWorkspace *workspace;
	SakuraSessionSnapshot *session_snapshot;
	GMutex state_mutex;
	GPtrArray *subscribers; /* GSocketConnection *, owned references. */
	GPtrArray *terminals; /* SakuraAgentTerminal *, owned. */
	guint64 sequence;
	gchar *socket_path;
	gchar *session_path;
};


static void sakura_agent_broadcast_event(SakuraAgent *agent);
static void sakura_agent_broadcast_payload(SakuraAgent *agent,
	                                        GByteArray *payload);


static gboolean
sakura_agent_error(GError **error, const gchar *message)
{
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, message);
	return FALSE;
}


static SakuraSessionSnapshot *
sakura_agent_load_session(const gchar *session_path, GError **error)
{
	SakuraSessionSnapshot *snapshot;
	GKeyFile *key_file;

	snapshot = sakura_session_snapshot_new();
	if (session_path == NULL ||
	    !g_file_test(session_path, G_FILE_TEST_IS_REGULAR))
		return snapshot;
	key_file = g_key_file_new();
	if (!g_key_file_load_from_file(key_file, session_path, 0, error) ||
	    !sakura_session_snapshot_load(key_file, snapshot, error)) {
		g_key_file_free(key_file);
		sakura_session_snapshot_free(snapshot);
		return NULL;
	}
	g_key_file_free(key_file);
	return snapshot;
}


static gboolean
sakura_agent_save_session(SakuraAgent *agent, GError **error)
{
	GKeyFile *key_file;
	gchar *data;
	gchar *directory;
	gchar *temporary_file;
	gsize data_length;

	if (agent == NULL || agent->session_snapshot == NULL ||
	    agent->session_path == NULL || agent->session_path[0] == '\0')
		return TRUE;
	key_file = g_key_file_new();
	sakura_session_snapshot_save(agent->session_snapshot, key_file);
	data = g_key_file_to_data(key_file, &data_length, error);
	g_key_file_free(key_file);
	if (data == NULL)
		return FALSE;
	directory = g_path_get_dirname(agent->session_path);
	if (g_mkdir_with_parents(directory, 0700) != 0) {
		g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
		            "Could not create session directory %s: %s", directory,
		            g_strerror(errno));
		g_free(directory);
		g_free(data);
		return FALSE;
	}
	temporary_file = g_strdup_printf("%s.tmp.%d", agent->session_path,
	                                  (int)getpid());
	if (!g_file_set_contents(temporary_file, data, data_length, error) ||
	    g_chmod(temporary_file, 0600) != 0 ||
	    g_rename(temporary_file, agent->session_path) != 0) {
		if (error == NULL || *error == NULL)
			g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
			            "Could not save session %s: %s", agent->session_path,
			            g_strerror(errno));
		g_remove(temporary_file);
		g_free(temporary_file);
		g_free(directory);
		g_free(data);
		return FALSE;
	}
	g_free(temporary_file);
	g_free(directory);
	g_free(data);
	return TRUE;
}


static guint
sakura_agent_next_group_order(const SakuraCoreWorkspace *workspace,
	                            const SakuraCoreGroup *parent)
{
	guint order = 0;

	for (guint index = 0; workspace != NULL && workspace->groups != NULL &&
	                       index < workspace->groups->len; index++) {
		SakuraCoreGroup *group = g_ptr_array_index(workspace->groups, index);

		if (group != NULL && group != workspace->root_group &&
		    group->parent == parent && group->order >= order)
			order = group->order + 1;
	}
	return order;
}


static guint
sakura_agent_next_task_order(const SakuraCoreWorkspace *workspace,
	                           const SakuraCoreGroup *group,
	                           const SakuraCoreTask *parent)
{
	guint order = 0;

	for (guint index = 0; workspace != NULL && workspace->tasks != NULL &&
	                       index < workspace->tasks->len; index++) {
		SakuraCoreTask *task = g_ptr_array_index(workspace->tasks, index);

		if (task != NULL && task->group == group && task->parent == parent &&
		    task->order >= order)
			order = task->order + 1;
	}
	return order;
}


static SakuraCoreGroup *
sakura_agent_request_group(SakuraAgent *agent, const gchar *group_id,
	                         GError **error)
{
	if (group_id == NULL || group_id[0] == '\0' ||
	    g_strcmp0(group_id, "root") == 0)
		return agent->workspace->root_group;
	if (sakura_core_workspace_find_group(agent->workspace, group_id) == NULL) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		            "group %s was not found", group_id);
		return NULL;
	}
	return sakura_core_workspace_find_group(agent->workspace, group_id);
}


static SakuraAgentTerminal *
sakura_agent_find_terminal(SakuraAgent *agent, const gchar *terminal_id,
	                         GError **error)
{
	if (agent == NULL || terminal_id == NULL || terminal_id[0] == '\0') {
		sakura_agent_error(error, "terminal id is required");
		return NULL;
	}
	for (guint index = 0; index < agent->terminals->len; index++) {
		SakuraAgentTerminal *terminal = g_ptr_array_index(agent->terminals, index);

		if (terminal != NULL && g_strcmp0(terminal->id, terminal_id) == 0)
			return terminal;
	}
	g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
	            "terminal %s was not found", terminal_id);
	return NULL;
}


static gboolean
sakura_agent_terminal_id_is_valid(const gchar *terminal_id)
{
	const gchar *cursor;

	if (terminal_id == NULL || terminal_id[0] == '\0' ||
	    strlen(terminal_id) > 128)
		return FALSE;
	for (cursor = terminal_id; *cursor != '\0'; cursor++) {
		if (!g_ascii_isalnum(*cursor) && *cursor != '-' &&
		    *cursor != '_' && *cursor != '.')
			return FALSE;
	}
	return TRUE;
}


static void
sakura_agent_terminal_stop(SakuraAgentTerminal *terminal)
{
	if (terminal == NULL || terminal->stopping)
		return;
	terminal->stopping = TRUE;
	if (terminal->pid > 0)
		kill(terminal->pid, SIGHUP);
	if (terminal->master_fd >= 0) {
		close(terminal->master_fd);
		terminal->master_fd = -1;
	}
}


static void
sakura_agent_terminal_free(SakuraAgentTerminal *terminal)
{
	if (terminal == NULL)
		return;
	sakura_agent_terminal_stop(terminal);
	if (terminal->reader_thread != NULL) {
		g_thread_join(terminal->reader_thread);
		terminal->reader_thread = NULL;
	}
	if (terminal->pid > 0) {
		waitpid(terminal->pid, NULL, 0);
		terminal->pid = 0;
	}
	g_free(terminal->id);
	g_free(terminal);
}


static gpointer
sakura_agent_terminal_reader(gpointer data)
{
	SakuraAgentTerminal *terminal = data;
	SakuraAgent *agent = terminal->agent;
	guint8 buffer[4096];

	for (;;) {
		ssize_t count = read(terminal->master_fd, buffer, sizeof(buffer));

		if (count > 0) {
			GByteArray *event = g_byte_array_new();

			g_mutex_lock(&agent->state_mutex);
			if (!terminal->stopping &&
			    sakura_control_encode_terminal_output_event(
				    ++agent->sequence, terminal->id, buffer, count, FALSE,
				    event))
				sakura_agent_broadcast_payload(agent, event);
			g_mutex_unlock(&agent->state_mutex);
			g_byte_array_unref(event);
			continue;
		}
		if (count < 0 && (errno == EINTR || errno == EAGAIN))
			continue;
		break;
	}

	g_mutex_lock(&agent->state_mutex);
	if (terminal->master_fd >= 0) {
		close(terminal->master_fd);
		terminal->master_fd = -1;
	}
	if (!terminal->stopping && terminal->core != NULL) {
		GByteArray *event = g_byte_array_new();

		terminal->core->status = SAKURA_TERMINAL_EXITED;
		if (sakura_control_encode_terminal_status_event(
				++agent->sequence, terminal->id, terminal->core->status,
				"terminal process exited", event))
			sakura_agent_broadcast_payload(agent, event);
		g_byte_array_unref(event);
	}
	g_mutex_unlock(&agent->state_mutex);
	if (terminal->pid > 0) {
		waitpid(terminal->pid, NULL, 0);
		terminal->pid = 0;
	}
	return NULL;
}


static gboolean
sakura_agent_update_group(SakuraAgent *agent,
	                       const SakuraControlRequest *request,
	                       GError **error)
{
	SakuraCoreGroup *group;

	if (request->group_id == NULL || request->group_id[0] == '\0' ||
	    g_strcmp0(request->group_id, "root") == 0)
		return sakura_agent_error(error, "a non-root group is required");
	group = sakura_agent_request_group(agent, request->group_id, error);
	if (group == NULL)
		return FALSE;
	if (request->title == NULL || request->title[0] == '\0')
		return sakura_agent_error(error, "group title is required");
	g_free(group->title);
	group->title = g_strdup(request->title);
	g_free(group->directory);
	group->directory = g_strdup(request->directory != NULL &&
	                            request->directory[0] != '\0'
	                            ? request->directory : NULL);
	return TRUE;
}


static gboolean
sakura_agent_set_group_archived(SakuraAgent *agent,
	                              const SakuraControlRequest *request,
	                              GError **error)
{
	SakuraCoreGroup *group;

	if (request->group_id == NULL || request->group_id[0] == '\0' ||
	    g_strcmp0(request->group_id, "root") == 0)
		return sakura_agent_error(error, "a non-root group is required");
	group = sakura_agent_request_group(agent, request->group_id, error);
	if (group == NULL)
		return FALSE;
	sakura_core_workspace_set_group_archived(agent->workspace, group,
	                                          request->archived);
	return TRUE;
}


static gboolean
sakura_agent_delete_group(SakuraAgent *agent,
	                       const SakuraControlRequest *request,
	                       GError **error)
{
	SakuraCoreGroup *group;

	if (request->group_id == NULL || request->group_id[0] == '\0' ||
	    g_strcmp0(request->group_id, "root") == 0)
		return sakura_agent_error(error, "a non-root group is required");
	group = sakura_agent_request_group(agent, request->group_id, error);
	if (group == NULL)
		return FALSE;
	if (!group->archived)
		return sakura_agent_error(error, "only archived groups can be deleted");
	if (!sakura_core_workspace_can_remove_group(agent->workspace, group))
		return sakura_agent_error(error,
		                          "group must be empty before deletion");
	if (!sakura_core_workspace_remove_group(agent->workspace, group))
		return sakura_agent_error(error, "could not delete group");
	return TRUE;
}


static gboolean
sakura_agent_create_group(SakuraAgent *agent,
	                        const SakuraControlRequest *request,
	                        GError **error)
{
	SakuraCoreGroup *parent;
	SakuraCoreGroup *group;
	gchar *id;

	if (request->title == NULL || request->title[0] == '\0')
		return sakura_agent_error(error, "group title is required");
	parent = sakura_agent_request_group(agent, request->parent_id, error);
	if (parent == NULL)
		return FALSE;
	id = g_uuid_string_random();
	group = sakura_core_group_new(id, request->title, parent);
	group->directory = g_strdup(request->directory != NULL &&
	                            request->directory[0] != '\0'
	                            ? request->directory : NULL);
	group->order = sakura_agent_next_group_order(agent->workspace, parent);
	g_free(id);
	if (!sakura_core_workspace_add_group(agent->workspace, group)) {
		sakura_core_group_free(group);
		return sakura_agent_error(error, "could not create group");
	}
	return TRUE;
}


static gboolean
sakura_agent_create_task(SakuraAgent *agent,
	                       const SakuraControlRequest *request,
	                       GError **error)
{
	SakuraCoreGroup *group;
	SakuraCoreTask *parent = NULL;
	SakuraCoreTask *task;
	gchar *id;

	if (request->title == NULL || request->title[0] == '\0')
		return sakura_agent_error(error, "task title is required");
	if (request->parent_id != NULL && request->parent_id[0] != '\0' &&
	    g_strcmp0(request->parent_id, "root") != 0) {
		parent = sakura_core_workspace_find_task(agent->workspace,
		                                         request->parent_id);
		if (parent == NULL) {
			g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			            "parent task %s was not found", request->parent_id);
			return FALSE;
		}
	}
	group = sakura_agent_request_group(agent, request->group_id, error);
	if (group == NULL)
		return FALSE;
	if (parent != NULL && parent->group != group)
		return sakura_agent_error(error, "task parent belongs to another group");
	if (parent != NULL)
		group = parent->group;
	id = g_uuid_string_random();
	task = sakura_core_task_new(id, request->title, group, parent);
	g_free(id);
	if (request->provider != NULL && request->provider[0] != '\0') {
		g_free(task->provider);
		task->provider = g_strdup(request->provider);
	}
	task->external_id = g_strdup(request->external_id != NULL &&
	                             request->external_id[0] != '\0'
	                             ? request->external_id : NULL);
	task->url = g_strdup(request->url != NULL && request->url[0] != '\0'
	                     ? request->url : NULL);
	task->order = sakura_agent_next_task_order(agent->workspace, group, parent);
	if (!sakura_core_workspace_add_task(agent->workspace, task)) {
		sakura_core_task_free(task);
		return sakura_agent_error(error, "could not create task");
	}
	return TRUE;
}


static gboolean
sakura_agent_update_task(SakuraAgent *agent,
	                      const SakuraControlRequest *request,
	                      GError **error)
{
	SakuraCoreTask *task;

	if (request->task_id == NULL || request->task_id[0] == '\0')
		return sakura_agent_error(error, "task id is required");
	task = sakura_core_workspace_find_task(agent->workspace, request->task_id);
	if (task == NULL) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		            "task %s was not found", request->task_id);
		return FALSE;
	}
	if (request->title == NULL || request->title[0] == '\0')
		return sakura_agent_error(error, "task title is required");
	g_free(task->title);
	task->title = g_strdup(request->title);
	return TRUE;
}


static gboolean
sakura_agent_set_task_archived(SakuraAgent *agent,
	                             const SakuraControlRequest *request,
	                             GError **error)
{
	SakuraCoreTask *task;

	if (request->task_id == NULL || request->task_id[0] == '\0')
		return sakura_agent_error(error, "task id is required");
	task = sakura_core_workspace_find_task(agent->workspace, request->task_id);
	if (task == NULL) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		            "task %s was not found", request->task_id);
		return FALSE;
	}
	sakura_core_workspace_set_task_archived(agent->workspace, task,
	                                         request->archived);
	return TRUE;
}


static gboolean
sakura_agent_delete_task(SakuraAgent *agent,
	                      const SakuraControlRequest *request,
	                      GError **error)
{
	SakuraCoreTask *task;

	if (request->task_id == NULL || request->task_id[0] == '\0')
		return sakura_agent_error(error, "task id is required");
	task = sakura_core_workspace_find_task(agent->workspace, request->task_id);
	if (task == NULL) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		            "task %s was not found", request->task_id);
		return FALSE;
	}
	if (!task->archived)
		return sakura_agent_error(error, "only archived tasks can be deleted");
	if (!sakura_core_workspace_can_remove_task(agent->workspace, task))
		return sakura_agent_error(error,
		                          "task must have no child tasks before deletion");
	if (!sakura_core_workspace_remove_task(agent->workspace, task))
		return sakura_agent_error(error, "could not delete task");
	return TRUE;
}


static gboolean
sakura_agent_create_terminal(SakuraAgent *agent,
	                           const SakuraControlRequest *request,
	                           gchar **accepted_id,
	                           GError **error)
{
	SakuraCoreGroup *group;
	SakuraCoreTask *task = NULL;
	SakuraCoreTerminal *core = NULL;
	SakuraAgentTerminal *terminal = NULL;
	struct winsize size = { 0 };
	const gchar *cwd;
	const gchar *shell;
	gchar *owned_cwd = NULL;
	gchar *id = NULL;
	gchar *title = NULL;
	int master_fd = -1;
	pid_t pid;

	group = sakura_agent_request_group(agent, request->group_id, error);
	if (group == NULL)
		return FALSE;
	if (request->task_id != NULL && request->task_id[0] != '\0' &&
	    g_strcmp0(request->task_id, "root") != 0) {
		task = sakura_core_workspace_find_task(agent->workspace,
		                                       request->task_id);
		if (task == NULL) {
			g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			            "task %s was not found", request->task_id);
			return FALSE;
		}
		if (task->group != group)
			return sakura_agent_error(error, "terminal task belongs to another group");
	}
	cwd = request->cwd != NULL && request->cwd[0] != '\0'
	    ? request->cwd
	    : agent->workspace->root_group->directory;
	if (cwd == NULL || cwd[0] == '\0') {
		owned_cwd = g_get_current_dir();
		cwd = owned_cwd;
	}
	if (!g_file_test(cwd, G_FILE_TEST_IS_DIR)) {
		g_free(owned_cwd);
		return sakura_agent_error(error, "terminal cwd is not a directory");
	}
	size.ws_col = request->cols != 0 ? request->cols : 80;
	size.ws_row = request->rows != 0 ? request->rows : 24;
	shell = g_getenv("SHELL");
	if (shell == NULL || shell[0] == '\0')
		shell = "/bin/sh";
	if (request->terminal_id != NULL && request->terminal_id[0] != '\0') {
		if (!sakura_agent_terminal_id_is_valid(request->terminal_id))
			return sakura_agent_error(error, "terminal id is invalid");
		if (sakura_core_workspace_find_terminal(agent->workspace,
	                                       request->terminal_id) != NULL)
			return sakura_agent_error(error, "terminal id is already in use");
		id = g_strdup(request->terminal_id);
	} else {
		id = g_uuid_string_random();
	}
	pid = forkpty(&master_fd, NULL, NULL, &size);
	if (pid < 0) {
		g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
		            "could not create terminal: %s", g_strerror(errno));
		g_free(id);
		g_free(owned_cwd);
		return FALSE;
	}
	if (pid == 0) {
		if (chdir(cwd) != 0)
			_exit(127);
		execl(shell, shell, "-i", (gchar *)NULL);
		_exit(127);
	}
	title = g_path_get_basename(cwd);
	core = sakura_core_terminal_new(id, cwd, group, task,
	                               size.ws_col, size.ws_row);
	core->title = title;
	title = NULL;
	core->status = SAKURA_TERMINAL_RUNNING;
	if (!sakura_core_workspace_add_terminal(agent->workspace, core)) {
		kill(pid, SIGHUP);
		close(master_fd);
		waitpid(pid, NULL, 0);
		sakura_core_terminal_free(core);
		g_free(id);
		g_free(owned_cwd);
		return sakura_agent_error(error, "could not register terminal");
	}
	terminal = g_new0(SakuraAgentTerminal, 1);
	terminal->agent = agent;
	terminal->core = core;
	terminal->id = g_strdup(id);
	terminal->master_fd = master_fd;
	terminal->pid = pid;
	g_ptr_array_add(agent->terminals, terminal);
	terminal->reader_thread = g_thread_new("sakura-terminal-reader",
	                                       sakura_agent_terminal_reader,
	                                       terminal);
	if (accepted_id != NULL)
		*accepted_id = g_strdup(id);
	g_free(id);
	g_free(title);
	g_free(owned_cwd);
	return TRUE;
}


static gboolean
sakura_agent_terminal_input(SakuraAgent *agent,
	                          const SakuraControlRequest *request,
	                          GError **error)
{
	SakuraAgentTerminal *terminal;
	gsize offset = 0;

	terminal = sakura_agent_find_terminal(agent, request->terminal_id, error);
	if (terminal == NULL)
		return FALSE;
	if (terminal->stopping || terminal->master_fd < 0)
		return sakura_agent_error(error, "terminal is not running");
	while (offset < request->input_length) {
		ssize_t count = write(terminal->master_fd,
		                      request->input_data + offset,
		                      request->input_length - offset);

		if (count < 0 && errno == EINTR)
			continue;
		if (count <= 0) {
			g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
			            "could not write terminal input: %s", g_strerror(errno));
			return FALSE;
		}
		offset += count;
	}
	return TRUE;
}


static gboolean
sakura_agent_terminal_resize(SakuraAgent *agent,
	                           const SakuraControlRequest *request,
	                           GError **error)
{
	SakuraAgentTerminal *terminal;
	struct winsize size = { 0 };

	if (request->cols == 0 || request->rows == 0)
		return sakura_agent_error(error, "terminal dimensions are required");
	terminal = sakura_agent_find_terminal(agent, request->terminal_id, error);
	if (terminal == NULL)
		return FALSE;
	if (terminal->stopping || terminal->master_fd < 0)
		return sakura_agent_error(error, "terminal is not running");
	size.ws_col = request->cols;
	size.ws_row = request->rows;
	if (ioctl(terminal->master_fd, TIOCSWINSZ, &size) != 0) {
		g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
		            "could not resize terminal: %s", g_strerror(errno));
		return FALSE;
	}
	if (terminal->core != NULL) {
		terminal->core->cols = request->cols;
		terminal->core->rows = request->rows;
	}
	if (terminal->pid > 0)
		kill(terminal->pid, SIGWINCH);
	return TRUE;
}


static gboolean
sakura_agent_close_terminal(SakuraAgent *agent,
	                          const SakuraControlRequest *request,
	                          GError **error)
{
	SakuraAgentTerminal *terminal;

	terminal = sakura_agent_find_terminal(agent, request->terminal_id, error);
	if (terminal == NULL)
		return FALSE;
	sakura_agent_terminal_stop(terminal);
	if (terminal->core != NULL) {
		if (!sakura_core_workspace_remove_terminal(agent->workspace,
		                                           terminal->core))
			return sakura_agent_error(error, "could not remove terminal");
		terminal->core = NULL;
	}
	return TRUE;
}


static gboolean
sakura_agent_apply_request(SakuraAgent *agent,
	                         const SakuraControlRequest *request,
	                         gchar **accepted_id,
	                         GError **error)
{
	gboolean changed = FALSE;

	switch (request->kind) {
	case SAKURA_CONTROL_REQUEST_CREATE_GROUP:
		changed = sakura_agent_create_group(agent, request, error);
		break;
	case SAKURA_CONTROL_REQUEST_CREATE_TASK:
		changed = sakura_agent_create_task(agent, request, error);
		break;
	case SAKURA_CONTROL_REQUEST_UPDATE_GROUP:
		changed = sakura_agent_update_group(agent, request, error);
		break;
	case SAKURA_CONTROL_REQUEST_SET_GROUP_ARCHIVED:
		changed = sakura_agent_set_group_archived(agent, request, error);
		break;
	case SAKURA_CONTROL_REQUEST_DELETE_GROUP:
		changed = sakura_agent_delete_group(agent, request, error);
		break;
	case SAKURA_CONTROL_REQUEST_UPDATE_TASK:
		changed = sakura_agent_update_task(agent, request, error);
		break;
	case SAKURA_CONTROL_REQUEST_SET_TASK_ARCHIVED:
		changed = sakura_agent_set_task_archived(agent, request, error);
		break;
	case SAKURA_CONTROL_REQUEST_DELETE_TASK:
		changed = sakura_agent_delete_task(agent, request, error);
		break;
	case SAKURA_CONTROL_REQUEST_CREATE_TERMINAL:
		changed = sakura_agent_create_terminal(agent, request, accepted_id,
		                                       error);
		break;
	case SAKURA_CONTROL_REQUEST_TERMINAL_INPUT:
		changed = sakura_agent_terminal_input(agent, request, error);
		break;
	case SAKURA_CONTROL_REQUEST_TERMINAL_RESIZE:
		changed = sakura_agent_terminal_resize(agent, request, error);
		break;
	case SAKURA_CONTROL_REQUEST_CLOSE_TERMINAL:
		changed = sakura_agent_close_terminal(agent, request, error);
		break;
	default:
		return TRUE;
	}
	if (!changed)
		return FALSE;
	if (!sakura_core_workspace_sync_snapshot(agent->workspace,
	                                         agent->session_snapshot))
		return sakura_agent_error(error, "could not update session snapshot");
	return sakura_agent_save_session(agent, error);
}


static gboolean
sakura_agent_send_payload(GSocketConnection *connection,
	                         GByteArray *payload, GError **error)
{
	return sakura_control_frame_write(
		g_io_stream_get_output_stream(G_IO_STREAM(connection)), payload->data,
		payload->len, NULL, error);
}


static void
sakura_agent_broadcast_payload(SakuraAgent *agent, GByteArray *payload)
{
	for (guint index = 0; index < agent->subscribers->len;) {
		GSocketConnection *connection = g_ptr_array_index(
			agent->subscribers, index);
		GError *error = NULL;

		if (sakura_agent_send_payload(connection, payload, &error)) {
			index++;
			continue;
		}
		g_clear_error(&error);
		g_ptr_array_remove_index(agent->subscribers, index);
	}
}


static void
sakura_agent_broadcast_event(SakuraAgent *agent)
{
	GByteArray *event = g_byte_array_new();

	if (sakura_control_encode_workspace_changed_event(
			agent->sequence, agent->workspace, event))
		sakura_agent_broadcast_payload(agent, event);
	g_byte_array_unref(event);
}


static void
sakura_agent_remove_subscriber(SakuraAgent *agent, GSocketConnection *connection)
{
	for (guint index = 0; index < agent->subscribers->len; index++) {
		if (g_ptr_array_index(agent->subscribers, index) == connection) {
			g_ptr_array_remove_index(agent->subscribers, index);
			break;
		}
	}
}


static gpointer
sakura_agent_connection_thread(gpointer data)
{
	SakuraAgentConnection *request = data;
	GInputStream *input;
	GOutputStream *output;
	GByteArray *payload = NULL;
	GByteArray *response = NULL;
	GByteArray *initial_event = NULL;
	SakuraControlRequest decoded = { 0 };
	GError *error = NULL;
	const gchar *request_id = "unknown";
	gchar *accepted_id = NULL;
	gboolean subscribed = FALSE;

	input = g_io_stream_get_input_stream(G_IO_STREAM(request->connection));
	output = g_io_stream_get_output_stream(G_IO_STREAM(request->connection));
	for (;;) {
		payload = NULL;
		response = g_byte_array_new();
		initial_event = NULL;
		decoded = (SakuraControlRequest){ 0 };
		error = NULL;
		request_id = "unknown";
		accepted_id = NULL;
		subscribed = FALSE;
		if (!sakura_control_frame_read(input, &payload, NULL, &error))
			break;
		g_mutex_lock(&request->agent->state_mutex);
		if (sakura_control_decode_request(payload->data, payload->len, &decoded,
	                                   &error)) {
		request_id = decoded.request_id != NULL ? decoded.request_id : "unknown";
		if (decoded.kind == SAKURA_CONTROL_REQUEST_SUBSCRIBE_EVENTS) {
			g_ptr_array_add(request->agent->subscribers,
			                 g_object_ref(request->connection));
			sakura_control_encode_accepted_response(request_id,
			                                        "workspace_events", NULL,
			                                        response);
			initial_event = g_byte_array_new();
			if (!sakura_control_encode_workspace_changed_event(
				request->agent->sequence, request->agent->workspace,
				initial_event))
				g_clear_pointer(&initial_event, g_byte_array_unref);
			subscribed = TRUE;
		} else if (decoded.kind == SAKURA_CONTROL_REQUEST_GET_SNAPSHOT) {
			sakura_control_encode_snapshot_response(request_id,
			                                       request->agent->sequence,
			                                       request->agent->workspace,
			                                       response);
		} else if (sakura_agent_apply_request(request->agent, &decoded,
		                                      &accepted_id, &error)) {
			request->agent->sequence++;
			if (decoded.kind == SAKURA_CONTROL_REQUEST_CREATE_TERMINAL)
				sakura_control_encode_accepted_response(
					request_id, "terminal", accepted_id, response);
			else if (decoded.kind == SAKURA_CONTROL_REQUEST_TERMINAL_INPUT)
				sakura_control_encode_accepted_response(
					request_id, "terminal_input", decoded.terminal_id, response);
			else
				sakura_control_encode_snapshot_response(
					request_id, request->agent->sequence,
					request->agent->workspace, response);
			sakura_agent_broadcast_event(request->agent);
		}
	}
		if (response->len == 0) {
		const gchar *message = error != NULL ? error->message : "invalid request";

		sakura_control_encode_error_response(request_id, "invalid_request",
		                                    message, response);
	}
		if (response != NULL && response->len != 0)
			sakura_control_frame_write(output, response->data, response->len,
		                           NULL, NULL);
		if (subscribed && initial_event != NULL)
			sakura_control_frame_write(output, initial_event->data, initial_event->len,
		                           NULL, NULL);
		g_mutex_unlock(&request->agent->state_mutex);
		if (subscribed) {
			while (sakura_control_frame_read(input, &payload, NULL, &error))
				g_clear_pointer(&payload, g_byte_array_unref);
			g_clear_error(&error);
			g_mutex_lock(&request->agent->state_mutex);
			sakura_agent_remove_subscriber(request->agent, request->connection);
			g_mutex_unlock(&request->agent->state_mutex);
			g_clear_pointer(&payload, g_byte_array_unref);
			g_clear_pointer(&response, g_byte_array_unref);
			g_clear_pointer(&initial_event, g_byte_array_unref);
			sakura_control_request_clear(&decoded);
			g_free(accepted_id);
			break;
		}
		g_clear_error(&error);
		g_clear_pointer(&payload, g_byte_array_unref);
		g_clear_pointer(&response, g_byte_array_unref);
		g_clear_pointer(&initial_event, g_byte_array_unref);
		sakura_control_request_clear(&decoded);
		g_free(accepted_id);
	}
	g_clear_error(&error);
	g_clear_pointer(&payload, g_byte_array_unref);
	g_clear_pointer(&response, g_byte_array_unref);
	g_clear_pointer(&initial_event, g_byte_array_unref);
	sakura_control_request_clear(&decoded);
	g_free(accepted_id);
	g_io_stream_close(G_IO_STREAM(request->connection), NULL, NULL);
	g_object_unref(request->connection);
	g_free(request);
	return NULL;
}


static gboolean
sakura_agent_incoming_cb(GSocketService *service,
	                     GSocketConnection *connection,
	                     GObject *source_object,
	                     gpointer user_data)
{
	SakuraAgent *agent = user_data;
	SakuraAgentConnection *request;

	(void)service;
	(void)source_object;
	request = g_new0(SakuraAgentConnection, 1);
	request->connection = g_object_ref(connection);
	request->agent = agent;
	g_thread_unref(g_thread_new("sakura-agent-client",
	                            sakura_agent_connection_thread, request));
	return TRUE;
}


static gboolean
sakura_agent_quit_cb(gpointer data)
{
	GMainLoop *loop = data;

	g_main_loop_quit(loop);
	return G_SOURCE_REMOVE;
}


static gchar *
sakura_agent_default_socket_path(void)
{
	const gchar *runtime_dir = g_get_user_runtime_dir();

	if (runtime_dir != NULL && runtime_dir[0] != '\0')
		return g_build_filename(runtime_dir, "sakura-agent.sock", NULL);
	return g_build_filename(g_get_user_cache_dir(), "sakura", "agent.sock",
	                        NULL);
}


static gboolean
sakura_agent_prepare_socket_path(const gchar *socket_path, GError **error)
{
	gchar *directory;
	gboolean directory_exists;

	directory = g_path_get_dirname(socket_path);
	directory_exists = g_file_test(directory, G_FILE_TEST_IS_DIR);
	if (g_mkdir_with_parents(directory, 0700) != 0) {
		g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
		            "Could not create agent socket directory %s: %s", directory,
		            g_strerror(errno));
		g_free(directory);
		return FALSE;
	}
	if (!directory_exists && chmod(directory, 0700) != 0) {
		g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
		            "Could not secure agent socket directory %s: %s", directory,
		            g_strerror(errno));
		g_free(directory);
		return FALSE;
	}
	g_free(directory);
	/* The client probes an existing endpoint before starting us. At this
	 * point an existing path is therefore stale or owned by a process that
	 * disappeared between the probe and bind. */
	g_remove(socket_path);
	return TRUE;
}


int
main(int argc, char **argv)
{
	GSocketService *service;
	GSocketAddress *address;
	GMainLoop *loop;
	SakuraCoreWorkspace *workspace;
	SakuraSessionSnapshot *session_snapshot;
	SakuraAgent agent = { 0 };
	GError *error = NULL;
	gchar *socket_path = NULL;
	gchar *session_path = NULL;
	GOptionContext *context;
	GOptionEntry entries[] = {
		{ "socket", 's', 0, G_OPTION_ARG_STRING, &socket_path,
		  "Unix socket path", "PATH" },
		{ "session", 'f', 0, G_OPTION_ARG_STRING, &session_path,
		  "Session file path", "PATH" },
		{ NULL }
	};

	context = g_option_context_new("- Sakura local control agent");
	g_option_context_add_main_entries(context, entries, NULL);
	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		g_printerr("%s\n", error->message);
		g_clear_error(&error);
		g_option_context_free(context);
		return EXIT_FAILURE;
	}
	g_option_context_free(context);
	if (socket_path == NULL)
		socket_path = sakura_agent_default_socket_path();
	if (!sakura_agent_prepare_socket_path(socket_path, &error)) {
		g_printerr("%s\n", error->message);
		g_clear_error(&error);
		g_free(socket_path);
		return EXIT_FAILURE;
	}

	signal(SIGPIPE, SIG_IGN);
	session_snapshot = sakura_agent_load_session(session_path, &error);
	if (session_snapshot == NULL) {
		g_printerr("Could not load agent session: %s\n",
		           error != NULL ? error->message : "unknown error");
		g_clear_error(&error);
		g_free(socket_path);
		g_free(session_path);
		return EXIT_FAILURE;
	}
	workspace = sakura_core_workspace_from_snapshot(session_snapshot, &error);
	if (workspace == NULL) {
		g_printerr("Could not initialize agent workspace: %s\n",
		           error != NULL ? error->message : "unknown error");
		g_clear_error(&error);
		sakura_session_snapshot_free(session_snapshot);
		g_free(socket_path);
		g_free(session_path);
		return EXIT_FAILURE;
	}

	service = g_socket_service_new();
	address = g_unix_socket_address_new(socket_path);
	if (!g_socket_listener_add_address(G_SOCKET_LISTENER(service), address,
	                                   G_SOCKET_TYPE_STREAM,
	                                   G_SOCKET_PROTOCOL_DEFAULT, NULL, NULL,
	                                   &error)) {
		g_printerr("Could not bind agent socket %s: %s\n", socket_path,
		            error->message);
		g_clear_error(&error);
		g_object_unref(address);
		g_object_unref(service);
		sakura_core_workspace_free(workspace);
		sakura_session_snapshot_free(session_snapshot);
		g_remove(socket_path);
		g_free(socket_path);
		g_free(session_path);
		return EXIT_FAILURE;
	}
	g_object_unref(address);
	if (chmod(socket_path, 0600) != 0)
		g_printerr("Could not secure agent socket %s: %s\n", socket_path,
		            g_strerror(errno));

	loop = g_main_loop_new(NULL, FALSE);
	agent.loop = loop;
	agent.workspace = workspace;
	agent.session_snapshot = session_snapshot;
	agent.subscribers = g_ptr_array_new_with_free_func(g_object_unref);
	agent.terminals = g_ptr_array_new_with_free_func(
		(GDestroyNotify)sakura_agent_terminal_free);
	agent.socket_path = socket_path;
	agent.session_path = session_path;
	g_mutex_init(&agent.state_mutex);
	g_signal_connect(service, "incoming", G_CALLBACK(sakura_agent_incoming_cb),
	                 &agent);
	g_unix_signal_add(SIGINT, sakura_agent_quit_cb, loop);
	g_unix_signal_add(SIGTERM, sakura_agent_quit_cb, loop);
	g_socket_service_start(service);
	g_main_loop_run(loop);
	g_socket_service_stop(service);
	g_main_loop_unref(loop);
	g_object_unref(service);
	g_mutex_clear(&agent.state_mutex);
	g_clear_pointer(&agent.subscribers, g_ptr_array_unref);
	g_clear_pointer(&agent.terminals, g_ptr_array_unref);
	sakura_core_workspace_free(workspace);
	sakura_session_snapshot_free(session_snapshot);
	g_remove(socket_path);
	g_free(socket_path);
	g_free(session_path);
	return EXIT_SUCCESS;
}
