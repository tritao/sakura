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
	GMutex outbound_mutex;
	GCond outbound_cond;
	GQueue *outbound;
	guint outbound_messages;
	gsize outbound_bytes;
	gboolean outbound_stopping;
	GThread *writer_thread;
} SakuraAgentConnection;

typedef struct {
	SakuraAgent *agent;
	SakuraCoreTerminal *core;
	gchar *id;
	GByteArray *output_buffer;
	guint attached_clients;
	int master_fd;
	GPid pid;
	GThread *reader_thread;
	gboolean stopping;
} SakuraAgentTerminal;

typedef struct {
	pid_t child_pid;
	int pty_fd;
	SakuraCoreTerminal *core;
	SakuraCorePage *page;
	SakuraCoreGroup *previous_page_group;
	SakuraCoreTask *previous_page_task;
	gchar *previous_page_title;
	gchar *previous_page_active_terminal_id;
	gboolean previous_page_title_set_by_user;
	gboolean page_created;
	gboolean terminal_registered;
	SakuraAgentTerminal *runtime;
	gboolean runtime_registered;
	gboolean reader_started;
} SakuraTerminalCreate;

#define SAKURA_AGENT_OUTPUT_BUFFER_SIZE (64 * 1024)
#define SAKURA_AGENT_MAX_QUEUED_MESSAGES 1000
#define SAKURA_AGENT_MAX_QUEUED_BYTES (4 * 1024 * 1024)
#define SAKURA_AGENT_VERSION "0.1"
#define SAKURA_AGENT_CAPABILITIES \
	(SAKURA_CONTROL_CAPABILITY_WORKSPACE | \
	 SAKURA_CONTROL_CAPABILITY_TERMINALS | \
	 SAKURA_CONTROL_CAPABILITY_TERMINAL_ATTACH | \
	 SAKURA_CONTROL_CAPABILITY_EVENT_STREAM | \
	 SAKURA_CONTROL_CAPABILITY_TERMINAL_RESTART | \
	 SAKURA_CONTROL_CAPABILITY_GROUP_MOVE)

struct _SakuraAgent {
	GMainLoop *loop;
	SakuraCoreWorkspace *workspace;
	SakuraSessionSnapshot *session_snapshot;
	gchar *workspace_id;
	GMutex state_mutex;
	GPtrArray *subscribers; /* SakuraAgentConnection *, state_mutex protected. */
	GPtrArray *terminals; /* SakuraAgentTerminal *, owned. */
	guint64 sequence;
	guint64 workspace_revision;
	gchar *socket_path;
};


static void sakura_agent_broadcast_event(SakuraAgent *agent);
static void sakura_agent_broadcast_payload(SakuraAgent *agent,
	                                        GByteArray *payload);


static void
sakura_agent_connection_clear_queue_locked(SakuraAgentConnection *connection)
{
	while (!g_queue_is_empty(connection->outbound)) {
		GByteArray *payload = g_queue_pop_head(connection->outbound);

		g_byte_array_unref(payload);
	}
	connection->outbound_messages = 0;
	connection->outbound_bytes = 0;
}


static gboolean
sakura_agent_connection_queue(SakuraAgentConnection *connection,
	                             GByteArray *payload)
{
	gboolean queued = FALSE;

	if (connection == NULL || payload == NULL)
		return FALSE;
	g_mutex_lock(&connection->outbound_mutex);
	if (!connection->outbound_stopping &&
	    connection->outbound_messages < SAKURA_AGENT_MAX_QUEUED_MESSAGES &&
	    payload->len <= SAKURA_AGENT_MAX_QUEUED_BYTES - connection->outbound_bytes) {
		g_queue_push_tail(connection->outbound, g_byte_array_ref(payload));
		connection->outbound_messages++;
		connection->outbound_bytes += payload->len;
		queued = TRUE;
	} else if (!connection->outbound_stopping) {
		/* Drop queued data and let the writer close the connection. This keeps
		 * a client that stopped reading from consuming unbounded memory. */
		connection->outbound_stopping = TRUE;
		sakura_agent_connection_clear_queue_locked(connection);
	}
	g_cond_signal(&connection->outbound_cond);
	g_mutex_unlock(&connection->outbound_mutex);
	return queued;
}


static gpointer
sakura_agent_connection_writer(gpointer data)
{
	SakuraAgentConnection *connection = data;
	GOutputStream *output;

	output = g_io_stream_get_output_stream(G_IO_STREAM(connection->connection));
	for (;;) {
		GByteArray *payload = NULL;
		GError *error = NULL;

		g_mutex_lock(&connection->outbound_mutex);
		while (g_queue_is_empty(connection->outbound) &&
		       !connection->outbound_stopping)
			g_cond_wait(&connection->outbound_cond,
			            &connection->outbound_mutex);
		if (connection->outbound_stopping) {
			sakura_agent_connection_clear_queue_locked(connection);
			g_mutex_unlock(&connection->outbound_mutex);
			break;
		}
		payload = g_queue_pop_head(connection->outbound);
		connection->outbound_messages--;
		connection->outbound_bytes -= payload->len;
		g_mutex_unlock(&connection->outbound_mutex);

		if (!sakura_control_frame_write(output, payload->data, payload->len,
		                               NULL, &error)) {
			g_clear_error(&error);
			g_byte_array_unref(payload);
			g_mutex_lock(&connection->outbound_mutex);
			connection->outbound_stopping = TRUE;
			sakura_agent_connection_clear_queue_locked(connection);
			g_cond_broadcast(&connection->outbound_cond);
			g_mutex_unlock(&connection->outbound_mutex);
			g_io_stream_close(G_IO_STREAM(connection->connection), NULL, NULL);
			break;
		}
		g_byte_array_unref(payload);
	}
	return NULL;
}


static void
sakura_agent_connection_stop_writer(SakuraAgentConnection *connection)
{
	if (connection == NULL)
		return;
	g_mutex_lock(&connection->outbound_mutex);
	connection->outbound_stopping = TRUE;
	sakura_agent_connection_clear_queue_locked(connection);
	g_cond_broadcast(&connection->outbound_cond);
	g_mutex_unlock(&connection->outbound_mutex);
	g_io_stream_close(G_IO_STREAM(connection->connection), NULL, NULL);
	if (connection->writer_thread != NULL) {
		g_thread_join(connection->writer_thread);
		connection->writer_thread = NULL;
	}
}


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

	/* The desktop coordinator is the sole session writer. The agent only
	 * reads this snapshot to bootstrap its runtime projection. */
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
	g_clear_pointer(&terminal->output_buffer, g_byte_array_unref);
	g_free(terminal);
}


static void
sakura_agent_terminal_buffer_output(SakuraAgentTerminal *terminal,
	                                  const guint8 *data, gsize data_length)
{
	gsize excess;

	if (terminal == NULL || terminal->output_buffer == NULL ||
	    data == NULL || data_length == 0)
		return;
	g_byte_array_append(terminal->output_buffer, data, data_length);
	if (terminal->output_buffer->len > SAKURA_AGENT_OUTPUT_BUFFER_SIZE) {
		excess = terminal->output_buffer->len - SAKURA_AGENT_OUTPUT_BUFFER_SIZE;
		g_byte_array_remove_range(terminal->output_buffer, 0, excess);
	}
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
			/* A restart/close may have stopped this reader while read() was
			 * returning data. Do not enter the agent mutex after the owner has
			 * begun retiring this runtime. */
			if (terminal->stopping)
				break;
			GByteArray *event = g_byte_array_new();

			g_mutex_lock(&agent->state_mutex);
			if (!terminal->stopping)
				sakura_agent_terminal_buffer_output(terminal, buffer, count);
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

	if (!terminal->stopping) {
		g_mutex_lock(&agent->state_mutex);
		if (terminal->master_fd >= 0) {
			close(terminal->master_fd);
			terminal->master_fd = -1;
		}
		if (terminal->core != NULL) {
			GByteArray *event = g_byte_array_new();

			terminal->core->status = SAKURA_TERMINAL_EXITED;
			if (sakura_control_encode_terminal_status_event(
					++agent->sequence, terminal->id, terminal->core->status,
					"terminal process exited", event))
				sakura_agent_broadcast_payload(agent, event);
			g_byte_array_unref(event);
		}
		g_mutex_unlock(&agent->state_mutex);
	}
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
sakura_agent_move_group(SakuraAgent *agent,
	                      const SakuraControlRequest *request,
	                      GError **error)
{
	SakuraCoreGroup *group;
	SakuraCoreGroup *parent;
	SakuraCoreGroup *target = NULL;

	if (request->group_id == NULL || request->group_id[0] == '\0' ||
	    g_strcmp0(request->group_id, "root") == 0)
		return sakura_agent_error(error, "a non-root group is required");
	group = sakura_agent_request_group(agent, request->group_id, error);
	if (group == NULL)
		return FALSE;
	parent = sakura_agent_request_group(agent, request->parent_id, error);
	if (parent == NULL)
		return FALSE;
	if (request->target_id != NULL && request->target_id[0] != '\0') {
		if (g_strcmp0(request->target_id, "root") == 0)
			return sakura_agent_error(error, "a non-root target is required");
		target = sakura_agent_request_group(agent, request->target_id, error);
		if (target == NULL)
			return FALSE;
	}
	if (!sakura_core_workspace_move_group(agent->workspace, group, parent,
	                                      target, request->after))
		return sakura_agent_error(error, "could not move group");
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
sakura_agent_update_page(SakuraAgent *agent,
	                      const SakuraControlRequest *request,
	                      GError **error)
{
	SakuraCorePage *page;
	SakuraCoreGroup *group;
	SakuraCoreTask *task = NULL;

	if (request->page_id == NULL || request->page_id[0] == '\0')
		return sakura_agent_error(error, "page id is required");
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
			return sakura_agent_error(error,
			                          "page task belongs to another group");
	}
	page = sakura_core_workspace_find_page(agent->workspace, request->page_id);
	if (page == NULL) {
		page = sakura_core_page_new(request->page_id, group, task);
		if (!sakura_core_workspace_add_page(agent->workspace, page)) {
			sakura_core_page_free(page);
			return sakura_agent_error(error, "could not register page");
		}
	}
	page->group = task != NULL ? task->group : group;
	page->task = task;
	g_free(page->title);
	page->title = g_strdup(request->title != NULL ? request->title : "");
	page->title_set_by_user = request->title_set_by_user;
	page->archived = request->archived;
	return TRUE;
}


static gboolean
sakura_agent_delete_page(SakuraAgent *agent,
                         const SakuraControlRequest *request,
                         GError **error)
{
	SakuraCorePage *page;

	if (request->page_id == NULL || request->page_id[0] == '\0')
		return sakura_agent_error(error, "page id is required");
	page = sakura_core_workspace_find_page(agent->workspace, request->page_id);
	if (page == NULL) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		            "page %s was not found", request->page_id);
		return FALSE;
	}
	if (!sakura_core_workspace_remove_page(agent->workspace, page))
		return sakura_agent_error(error, "could not delete page");
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
sakura_agent_bind_page_terminal(SakuraAgent *agent, const gchar *page_id,
	                              const gchar *terminal_id,
	                              SakuraCoreGroup *group,
	                              SakuraCoreTask *task,
	                              const gchar *title,
	                              SakuraTerminalCreate *create,
	                              GError **error)
{
	SakuraCorePage *page;

	if (page_id == NULL || page_id[0] == '\0')
		return TRUE;
	page = sakura_core_workspace_find_page(agent->workspace, page_id);
	if (page == NULL) {
		page = sakura_core_page_new(page_id, group, task);
		if (!sakura_core_workspace_add_page(agent->workspace, page)) {
			sakura_core_page_free(page);
			return sakura_agent_error(error, "could not register page");
		}
		create->page_created = TRUE;
	} else {
		create->previous_page_group = page->group;
		create->previous_page_task = page->task;
		create->previous_page_title = g_strdup(page->title);
		create->previous_page_active_terminal_id =
			g_strdup(page->active_terminal_id);
		create->previous_page_title_set_by_user = page->title_set_by_user;
	}
	create->page = page;
	page->group = group;
	page->task = task;
	g_free(page->active_terminal_id);
	page->active_terminal_id = g_strdup(terminal_id);
	if (page->title == NULL || page->title[0] == '\0') {
		g_free(page->title);
		page->title = g_strdup(title != NULL ? title : "");
	}
	return TRUE;
}


static void
sakura_agent_terminal_create_cleanup(SakuraAgent *agent,
	                                  SakuraTerminalCreate *create)
{
	if (create == NULL)
		return;
	if (create->runtime != NULL) {
		if (create->reader_started) {
			sakura_agent_terminal_stop(create->runtime);
			if (create->runtime->reader_thread != NULL) {
				g_thread_join(create->runtime->reader_thread);
				create->runtime->reader_thread = NULL;
			}
		}
		create->runtime->core = NULL;
		if (create->runtime_registered)
			g_ptr_array_remove(agent->terminals, create->runtime);
		else
			sakura_agent_terminal_free(create->runtime);
		create->pty_fd = -1;
		create->child_pid = 0;
		create->runtime = NULL;
	}
	if (create->page != NULL) {
		if (create->page_created) {
			sakura_core_workspace_remove_page(agent->workspace, create->page);
		} else {
			g_free(create->page->active_terminal_id);
			create->page->active_terminal_id =
				create->previous_page_active_terminal_id;
			create->previous_page_active_terminal_id = NULL;
			g_free(create->page->title);
			create->page->title = create->previous_page_title;
			create->previous_page_title = NULL;
			create->page->group = create->previous_page_group;
			create->page->task = create->previous_page_task;
			create->page->title_set_by_user =
				create->previous_page_title_set_by_user;
		}
		create->page = NULL;
	}
	if (create->terminal_registered && create->core != NULL) {
		if (!sakura_core_workspace_remove_terminal(agent->workspace,
		                                          create->core))
			sakura_core_terminal_free(create->core);
		create->core = NULL;
	}
	if (create->pty_fd >= 0) {
		close(create->pty_fd);
		create->pty_fd = -1;
	}
	if (create->child_pid > 0) {
		kill(create->child_pid, SIGHUP);
		waitpid(create->child_pid, NULL, 0);
		create->child_pid = 0;
	}
	g_clear_pointer(&create->previous_page_title, g_free);
	g_clear_pointer(&create->previous_page_active_terminal_id, g_free);
}


static void
sakura_agent_terminal_create_clear(SakuraTerminalCreate *create)
{
	if (create == NULL)
		return;
	g_clear_pointer(&create->previous_page_title, g_free);
	g_clear_pointer(&create->previous_page_active_terminal_id, g_free);
}


static gboolean
sakura_agent_create_terminal(SakuraAgent *agent,
	                           const SakuraControlRequest *request,
	                           gchar **accepted_id,
	                           GError **error)
{
	SakuraTerminalCreate create = {
		.pty_fd = -1,
	};
	SakuraCoreGroup *group;
	SakuraCoreTask *task = NULL;
	struct winsize size = { 0 };
	const gchar *cwd;
	const gchar *shell;
	gchar *owned_cwd = NULL;
	gchar *id = NULL;
	gchar *title = NULL;

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
		if (!sakura_agent_terminal_id_is_valid(request->terminal_id)) {
			sakura_agent_error(error, "terminal id is invalid");
			goto fail;
		}
		if (sakura_core_workspace_find_terminal(agent->workspace,
	                                       request->terminal_id) != NULL) {
			sakura_agent_error(error, "terminal id is already in use");
			goto fail;
		}
		id = g_strdup(request->terminal_id);
	} else {
		id = g_uuid_string_random();
	}
	create.child_pid = forkpty(&create.pty_fd, NULL, NULL, &size);
	if (create.child_pid < 0) {
		g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
		            "could not create terminal: %s", g_strerror(errno));
		create.child_pid = 0;
		goto fail;
	}
	if (create.child_pid == 0) {
		if (chdir(cwd) != 0)
			_exit(127);
		execl(shell, shell, "-i", (gchar *)NULL);
		_exit(127);
	}
	title = g_path_get_basename(cwd);
	create.core = sakura_core_terminal_new(id, cwd, group, task,
	                                      size.ws_col, size.ws_row);
	create.core->title = title;
	title = NULL;
	create.core->status = SAKURA_TERMINAL_RUNNING;
	if (!sakura_core_workspace_add_terminal(agent->workspace, create.core)) {
		sakura_agent_error(error, "could not register terminal");
		goto fail;
	}
	create.terminal_registered = TRUE;
	if (!sakura_agent_bind_page_terminal(agent, request->page_id, id, group,
	                                     task, create.core->title, &create,
	                                     error)) {
		goto fail;
	}
	create.runtime = g_new0(SakuraAgentTerminal, 1);
	create.runtime->agent = agent;
	create.runtime->core = create.core;
	create.runtime->id = g_strdup(id);
	create.runtime->output_buffer = g_byte_array_new();
	create.runtime->master_fd = create.pty_fd;
	create.runtime->pid = create.child_pid;
	g_ptr_array_add(agent->terminals, create.runtime);
	create.runtime_registered = TRUE;
	create.runtime->reader_thread = g_thread_new("sakura-terminal-reader",
	                                             sakura_agent_terminal_reader,
	                                             create.runtime);
	create.reader_started = create.runtime->reader_thread != NULL;
	if (!create.reader_started) {
		sakura_agent_error(error, "could not start terminal reader");
		goto fail;
	}
	create.core = NULL;
	create.pty_fd = -1;
	create.child_pid = 0;
	create.runtime = NULL;
	if (accepted_id != NULL)
		*accepted_id = g_strdup(id);
	g_free(id);
	g_free(title);
	g_free(owned_cwd);
	sakura_agent_terminal_create_clear(&create);
	return TRUE;

fail:
	sakura_agent_terminal_create_cleanup(agent, &create);
	sakura_core_terminal_free(create.core);
	g_free(id);
	g_free(title);
	g_free(owned_cwd);
	sakura_agent_terminal_create_clear(&create);
	return FALSE;
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
sakura_agent_attach_terminal(SakuraAgent *agent,
	                           const SakuraControlRequest *request,
	                           const gchar *request_id,
	                           GByteArray *response, gboolean *workspace_changed,
	                           GError **error)
{
	SakuraAgentTerminal *terminal;
	struct winsize size = { 0 };

	if (workspace_changed != NULL)
		*workspace_changed = FALSE;

	terminal = sakura_agent_find_terminal(agent, request->terminal_id, error);
	if (terminal == NULL)
		return FALSE;
	if (terminal->core == NULL)
		return sakura_agent_error(error, "terminal is closed");
	if (request->cols != 0 && request->rows != 0) {
		size.ws_col = request->cols;
		size.ws_row = request->rows;
		if (terminal->master_fd >= 0 &&
		    ioctl(terminal->master_fd, TIOCSWINSZ, &size) != 0) {
			g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
			            "could not resize attached terminal: %s",
			            g_strerror(errno));
			return FALSE;
		}
		terminal->core->cols = request->cols;
		terminal->core->rows = request->rows;
		if (terminal->pid > 0)
			kill(terminal->pid, SIGWINCH);
	}
	terminal->attached_clients++;
	if (!sakura_control_encode_terminal_attachment_response(
			request_id, terminal->core,
			terminal->output_buffer != NULL ? terminal->output_buffer->data : NULL,
			terminal->output_buffer != NULL ? terminal->output_buffer->len : 0,
			response)) {
		terminal->attached_clients--;
		return sakura_agent_error(error, "could not encode terminal attachment");
	}
	return TRUE;
}


static gboolean
sakura_agent_detach_terminal(SakuraAgent *agent,
	                           const SakuraControlRequest *request,
	                           GError **error)
{
	SakuraAgentTerminal *terminal;

	terminal = sakura_agent_find_terminal(agent, request->terminal_id, error);
	if (terminal == NULL)
		return FALSE;
	if (terminal->core == NULL)
		return sakura_agent_error(error, "terminal is closed");
	if (terminal->attached_clients > 0)
		terminal->attached_clients--;
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
	for (guint index = 0; agent->workspace->pages != NULL &&
	                     index < agent->workspace->pages->len; index++) {
		SakuraCorePage *page = g_ptr_array_index(agent->workspace->pages, index);

		if (page != NULL && g_strcmp0(page->active_terminal_id,
	                              terminal->id) == 0)
			g_clear_pointer(&page->active_terminal_id, g_free);
	}
	return TRUE;
}


static gboolean
sakura_agent_restart_terminal(SakuraAgent *agent,
	                             const SakuraControlRequest *request,
	                             gchar **accepted_id,
	                             SakuraAgentTerminal **retired_terminal,
	                             GError **error)
{
	SakuraAgentTerminal *existing = NULL;
	guint existing_index = 0;

	if (retired_terminal != NULL)
		*retired_terminal = NULL;
	if (request->terminal_id == NULL || request->terminal_id[0] == '\0' ||
	    !sakura_agent_terminal_id_is_valid(request->terminal_id))
		return sakura_agent_error(error, "terminal id is invalid");

	/* Restart is an idempotent replacement operation. This matters both after
	 * an agent process restart and when a caller retries a request after losing
	 * its response. Retire any runtime with the logical ID before creating the
	 * replacement. The retired object is handed to the connection thread and
	 * joined after the workspace mutex is released. */
	for (guint index = 0; index < agent->terminals->len; index++) {
		SakuraAgentTerminal *candidate = g_ptr_array_index(agent->terminals, index);

		if (candidate != NULL &&
		    g_strcmp0(candidate->id, request->terminal_id) == 0) {
			existing = candidate;
			existing_index = index;
			break;
		}
	}
	if (existing != NULL) {
		sakura_agent_terminal_stop(existing);
		if (existing->core != NULL) {
			if (!sakura_core_workspace_remove_terminal(agent->workspace,
			                                           existing->core))
				return sakura_agent_error(error,
				                         "could not retire terminal runtime");
			existing->core = NULL;
		}
		if (retired_terminal != NULL)
			*retired_terminal = g_ptr_array_steal_index(agent->terminals,
			                                           existing_index);
	}
	return sakura_agent_create_terminal(agent, request, accepted_id, error);
}


static gboolean
sakura_agent_apply_request(SakuraAgent *agent,
	                         const SakuraControlRequest *request,
	                         gchar **accepted_id,
	                         SakuraAgentTerminal **retired_terminal,
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
	case SAKURA_CONTROL_REQUEST_MOVE_GROUP:
		changed = sakura_agent_move_group(agent, request, error);
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
	case SAKURA_CONTROL_REQUEST_UPDATE_PAGE:
		changed = sakura_agent_update_page(agent, request, error);
		break;
	case SAKURA_CONTROL_REQUEST_DELETE_PAGE:
		changed = sakura_agent_delete_page(agent, request, error);
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
	case SAKURA_CONTROL_REQUEST_RESTART_TERMINAL:
		changed = sakura_agent_restart_terminal(agent, request, accepted_id,
		                                        retired_terminal, error);
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
	return TRUE;
}


static void
sakura_agent_broadcast_payload(SakuraAgent *agent, GByteArray *payload)
{
	for (guint index = 0; index < agent->subscribers->len; index++) {
		SakuraAgentConnection *connection = g_ptr_array_index(
			agent->subscribers, index);

		/* This only touches the bounded in-memory queue. The writer thread is
		 * the sole owner of socket output. */
		sakura_agent_connection_queue(connection, payload);
	}
}


static void
sakura_agent_broadcast_event(SakuraAgent *agent)
{
	GByteArray *event = g_byte_array_new();

	if (sakura_control_encode_workspace_changed_event_with_revision(
			agent->sequence, agent->workspace_revision, agent->workspace, event))
		sakura_agent_broadcast_payload(agent, event);
	g_byte_array_unref(event);
}


static gboolean
sakura_agent_request_changes_workspace(SakuraControlRequestKind kind)
{
	switch (kind) {
	case SAKURA_CONTROL_REQUEST_CREATE_GROUP:
	case SAKURA_CONTROL_REQUEST_UPDATE_GROUP:
	case SAKURA_CONTROL_REQUEST_MOVE_GROUP:
	case SAKURA_CONTROL_REQUEST_SET_GROUP_ARCHIVED:
	case SAKURA_CONTROL_REQUEST_DELETE_GROUP:
	case SAKURA_CONTROL_REQUEST_CREATE_TASK:
	case SAKURA_CONTROL_REQUEST_UPDATE_TASK:
	case SAKURA_CONTROL_REQUEST_SET_TASK_ARCHIVED:
	case SAKURA_CONTROL_REQUEST_DELETE_TASK:
	case SAKURA_CONTROL_REQUEST_UPDATE_PAGE:
	case SAKURA_CONTROL_REQUEST_DELETE_PAGE:
	case SAKURA_CONTROL_REQUEST_CREATE_TERMINAL:
	case SAKURA_CONTROL_REQUEST_CLOSE_TERMINAL:
	case SAKURA_CONTROL_REQUEST_RESTART_TERMINAL:
		return TRUE;
	default:
		return FALSE;
	}
}


static void
sakura_agent_remove_subscriber(SakuraAgent *agent,
	                             SakuraAgentConnection *connection)
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
	GByteArray *payload = NULL;
	GByteArray *response = NULL;
	GByteArray *initial_event = NULL;
	SakuraControlRequest decoded = { 0 };
	GError *error = NULL;
	const gchar *request_id = "unknown";
	gchar *accepted_id = NULL;
	SakuraAgentTerminal *retired_terminal = NULL;
	gboolean subscribed = FALSE;
	gboolean handshaken = FALSE;
	const gchar *error_code = "invalid_request";

	input = g_io_stream_get_input_stream(G_IO_STREAM(request->connection));
	for (;;) {
		payload = NULL;
		response = g_byte_array_new();
		initial_event = NULL;
		decoded = (SakuraControlRequest){ 0 };
		error = NULL;
		request_id = "unknown";
		error_code = "invalid_request";
		accepted_id = NULL;
		retired_terminal = NULL;
		subscribed = FALSE;
		if (!sakura_control_frame_read(input, &payload, NULL, &error))
			break;
		g_mutex_lock(&request->agent->state_mutex);
		if (sakura_control_decode_request(payload->data, payload->len, &decoded,
	                                   &error)) {
		request_id = decoded.request_id != NULL ? decoded.request_id : "unknown";
		if (decoded.kind == SAKURA_CONTROL_REQUEST_HELLO) {
			if (decoded.protocol_version != SAKURA_CONTROL_PROTOCOL_VERSION) {
				g_set_error(&error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
				            "unsupported control protocol version %u",
				            decoded.protocol_version);
			} else if (decoded.workspace_id == NULL || decoded.workspace_id[0] == '\0' ||
			           g_strcmp0(decoded.workspace_id, request->agent->workspace_id) != 0) {
				g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
				                    "control workspace identity did not match");
			} else if (sakura_control_encode_hello_response(
					request_id, SAKURA_CONTROL_PROTOCOL_VERSION,
					SAKURA_AGENT_VERSION, SAKURA_AGENT_CAPABILITIES,
					request->agent->workspace_id, response)) {
				handshaken = TRUE;
			}
		} else if (!handshaken) {
			g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
			                    "control hello is required before requests");
		} else if (sakura_agent_request_changes_workspace(decoded.kind) &&
		           decoded.has_expected_revision &&
		           decoded.expected_revision != request->agent->workspace_revision) {
			g_set_error(&error, G_IO_ERROR, G_IO_ERROR_FAILED,
			            "expected revision %"G_GUINT64_FORMAT
			            ", current revision %"G_GUINT64_FORMAT,
			            decoded.expected_revision,
			            request->agent->workspace_revision);
			error_code = "REVISION_CONFLICT";
		} else if (decoded.kind == SAKURA_CONTROL_REQUEST_SUBSCRIBE_EVENTS) {
			g_ptr_array_add(request->agent->subscribers, request);
			sakura_control_encode_accepted_response_with_revision(request_id,
			                                        "workspace_events", NULL,
			                                        request->agent->workspace_revision,
			                                        response);
			initial_event = g_byte_array_new();
			if (!sakura_control_encode_workspace_changed_event_with_revision(
				request->agent->sequence, request->agent->workspace_revision,
				request->agent->workspace,
				initial_event))
				g_clear_pointer(&initial_event, g_byte_array_unref);
			subscribed = TRUE;
		} else if (decoded.kind == SAKURA_CONTROL_REQUEST_GET_SNAPSHOT) {
			sakura_control_encode_snapshot_response_with_revision(request_id,
		                                       request->agent->sequence,
		                                       request->agent->workspace_revision,
		                                       request->agent->workspace,
		                                       response);
		} else if (decoded.kind == SAKURA_CONTROL_REQUEST_ATTACH_TERMINAL) {
			gboolean workspace_changed = FALSE;

			if (sakura_agent_attach_terminal(
					request->agent, &decoded, request_id, response,
					&workspace_changed, &error) && workspace_changed) {
				request->agent->sequence++;
				sakura_agent_broadcast_event(request->agent);
			}
		} else if (decoded.kind == SAKURA_CONTROL_REQUEST_DETACH_TERMINAL) {
			if (sakura_agent_detach_terminal(request->agent, &decoded, &error))
				sakura_control_encode_accepted_response_with_revision(
					request_id, "terminal_detached", decoded.terminal_id,
					request->agent->workspace_revision,
					response);
		} else if (sakura_agent_apply_request(request->agent, &decoded,
		                                      &accepted_id, &retired_terminal,
		                                      &error)) {
			if (sakura_agent_request_changes_workspace(decoded.kind))
				request->agent->workspace_revision++;
			request->agent->sequence++;
			if (decoded.kind == SAKURA_CONTROL_REQUEST_CREATE_TERMINAL ||
			    decoded.kind == SAKURA_CONTROL_REQUEST_RESTART_TERMINAL)
				sakura_control_encode_accepted_response_with_revision(
					request_id, "terminal", accepted_id,
					request->agent->workspace_revision, response);
			else if (decoded.kind == SAKURA_CONTROL_REQUEST_TERMINAL_INPUT)
				sakura_control_encode_accepted_response_with_revision(
					request_id, "terminal_input", decoded.terminal_id,
					request->agent->workspace_revision, response);
			else
				sakura_control_encode_snapshot_response_with_revision(
					request_id, request->agent->sequence,
					request->agent->workspace_revision,
					request->agent->workspace, response);
			sakura_agent_broadcast_event(request->agent);
		}
		}
		if (response->len == 0) {
			const gchar *message = error != NULL ? error->message : "invalid request";

			sakura_control_encode_error_response_with_revision(
				request_id, error_code, message,
				request->agent->workspace_revision,
				g_strcmp0(error_code, "REVISION_CONFLICT") == 0, response);
		}
		if (response != NULL && response->len != 0)
			sakura_agent_connection_queue(request, response);
		if (subscribed && initial_event != NULL)
			sakura_agent_connection_queue(request, initial_event);
		g_mutex_unlock(&request->agent->state_mutex);
		if (retired_terminal != NULL) {
			sakura_agent_terminal_free(retired_terminal);
			retired_terminal = NULL;
		}
		if (subscribed) {
			while (sakura_control_frame_read(input, &payload, NULL, &error))
				g_clear_pointer(&payload, g_byte_array_unref);
			g_clear_error(&error);
			g_mutex_lock(&request->agent->state_mutex);
			sakura_agent_remove_subscriber(request->agent, request);
			g_mutex_unlock(&request->agent->state_mutex);
			subscribed = FALSE;
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
	if (subscribed) {
		g_mutex_lock(&request->agent->state_mutex);
		sakura_agent_remove_subscriber(request->agent, request);
		g_mutex_unlock(&request->agent->state_mutex);
	}
	sakura_agent_connection_stop_writer(request);
	g_object_unref(request->connection);
	g_queue_free(request->outbound);
	g_mutex_clear(&request->outbound_mutex);
	g_cond_clear(&request->outbound_cond);
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
	request->outbound = g_queue_new();
	g_mutex_init(&request->outbound_mutex);
	g_cond_init(&request->outbound_cond);
	request->writer_thread = g_thread_new("sakura-agent-writer",
	                                      sakura_agent_connection_writer,
	                                      request);
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
	gchar *workspace_id = NULL;
	GOptionContext *context;
	GOptionEntry entries[] = {
		{ "socket", 's', 0, G_OPTION_ARG_STRING, &socket_path,
		  "Unix socket path", "PATH" },
		{ "session", 'f', 0, G_OPTION_ARG_STRING, &session_path,
		  "Session file path", "PATH" },
		{ "workspace-id", 0, 0, G_OPTION_ARG_STRING, &workspace_id,
		  "Workspace identity", "ID" },
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
		g_free(workspace_id);
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
		g_free(workspace_id);
		return EXIT_FAILURE;
	}
	if (workspace_id != NULL && workspace_id[0] != '\0') {
		g_free(session_snapshot->workspace_id);
		session_snapshot->workspace_id = g_strdup(workspace_id);
	}
	workspace = sakura_core_workspace_from_snapshot(session_snapshot, &error);
	if (workspace == NULL) {
		g_printerr("Could not initialize agent workspace: %s\n",
		           error != NULL ? error->message : "unknown error");
		g_clear_error(&error);
		sakura_session_snapshot_free(session_snapshot);
		g_free(socket_path);
		g_free(session_path);
		g_free(workspace_id);
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
		g_free(workspace_id);
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
	agent.workspace_id = g_strdup(session_snapshot->workspace_id);
	agent.subscribers = g_ptr_array_new();
	agent.terminals = g_ptr_array_new_with_free_func(
		(GDestroyNotify)sakura_agent_terminal_free);
	agent.socket_path = socket_path;
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
	g_free(agent.workspace_id);
	g_remove(socket_path);
	g_free(socket_path);
	g_free(session_path);
	g_free(workspace_id);
	return EXIT_SUCCESS;
}
