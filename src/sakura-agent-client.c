#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "sakura-private.h"
#include "sakura-control-client.h"
#include "sakura-control-transport.h"


static void sakura_agent_refresh_after_revision_conflict(SakuraApp *app);
static gboolean sakura_agent_apply_event_cb(gpointer data);


static gboolean
sakura_agent_error_is_revision_conflict(const GError *error)
{
	return error != NULL && error->domain == SAKURA_CONTROL_ERROR_DOMAIN &&
	       error->code == SAKURA_CONTROL_ERROR_CODE_REVISION_CONFLICT;
}


static gchar *
sakura_agent_socket_path_new(SakuraApp *app)
{
	const gchar *runtime_dir = g_get_user_runtime_dir();
	gchar *workspace_hash;
	gchar *socket_name;
	gchar *socket_path;

	if (app != NULL && app->agent_socket_path_override != NULL &&
	    app->agent_socket_path_override[0] != '\0')
		return g_strdup(app->agent_socket_path_override);

	/* The endpoint is derived from the persisted workspace identity rather
	 * than from a mutable path, so renaming or relocating a session does not
	 * create a second workspace. */
	if (app == NULL || app->workspace_id == NULL ||
	    app->workspace_id[0] == '\0')
		return NULL;
	workspace_hash = g_compute_checksum_for_string(
		G_CHECKSUM_SHA256, app->workspace_id, -1);
	socket_name = g_strdup_printf("sakura-agent-%.*s.sock", 16,
	                              workspace_hash);
	g_free(workspace_hash);
	if (runtime_dir != NULL && runtime_dir[0] != '\0')
		socket_path = g_build_filename(runtime_dir, socket_name, NULL);
	else
		socket_path = g_build_filename(g_get_user_cache_dir(), "sakura",
		                               socket_name, NULL);
	g_free(socket_name);
	return socket_path;
}


#define SAKURA_AGENT_CLIENT_NAME "sakura-gtk"
#define SAKURA_AGENT_REQUIRED_CAPABILITIES \
	(SAKURA_CONTROL_CAPABILITY_WORKSPACE | \
	 SAKURA_CONTROL_CAPABILITY_TERMINALS | \
	 SAKURA_CONTROL_CAPABILITY_TERMINAL_ATTACH | \
	 SAKURA_CONTROL_CAPABILITY_EVENT_STREAM | \
	 SAKURA_CONTROL_CAPABILITY_TERMINAL_RESTART | \
	 SAKURA_CONTROL_CAPABILITY_GROUP_MOVE | \
	 SAKURA_CONTROL_CAPABILITY_WORKSPACE_REVISIONS | \
	 SAKURA_CONTROL_CAPABILITY_STRUCTURED_ERRORS | \
	 SAKURA_CONTROL_CAPABILITY_TERMINAL_OUTPUT_OFFSETS)


static SakuraControlClientConnection *
sakura_agent_connect(const gchar *socket_path, const gchar *workspace_id,
                     GError **error)
{
	return sakura_control_client_connect(
		socket_path, workspace_id, SAKURA_AGENT_CLIENT_NAME,
		SAKURA_AGENT_REQUIRED_CAPABILITIES, error);
}


static guint64
sakura_agent_workspace_revision_get(SakuraApp *app)
{
	guint64 revision;

	if (app == NULL)
		return 0;
	if (!app->agent_revision_mutex_initialized)
		return app->agent_workspace_revision;
	g_mutex_lock(&app->agent_revision_mutex);
	revision = app->agent_workspace_revision;
	g_mutex_unlock(&app->agent_revision_mutex);
	return revision;
}


static void
sakura_agent_workspace_revision_set(SakuraApp *app, guint64 revision)
{
	if (app == NULL)
		return;
	if (!app->agent_revision_mutex_initialized) {
		app->agent_workspace_revision = revision;
		return;
	}
	g_mutex_lock(&app->agent_revision_mutex);
	app->agent_workspace_revision = revision;
	g_mutex_unlock(&app->agent_revision_mutex);
}


static void
sakura_agent_workspace_revision_init(SakuraApp *app)
{
	if (app == NULL || app->agent_revision_mutex_initialized)
		return;
	g_mutex_init(&app->agent_revision_mutex);
	g_mutex_init(&app->agent_workspace_mutation_mutex);
	app->agent_revision_mutex_initialized = TRUE;
	app->agent_workspace_mutation_mutex_initialized = TRUE;
	app->agent_workspace_revision = 0;
}


typedef struct {
	SakuraApp *app;
	guint64 sequence;
	SakuraSessionSnapshot *snapshot;
} SakuraAgentEvent;

typedef struct {
	SakuraApp *app;
	gchar *terminal_id;
	guint8 *data;
	gsize data_length;
	guint64 start_offset;
	guint64 end_offset;
	guint status;
	gchar *message;
	gboolean output;
} SakuraAgentTerminalEvent;

typedef enum {
	SAKURA_AGENT_COMMAND_INPUT,
	SAKURA_AGENT_COMMAND_RESIZE,
	SAKURA_AGENT_COMMAND_CLOSE,
	SAKURA_AGENT_COMMAND_DETACH,
	SAKURA_AGENT_COMMAND_UPDATE_PAGE,
	SAKURA_AGENT_COMMAND_DELETE_PAGE
} SakuraAgentCommandKind;

typedef struct {
	SakuraAgentCommandKind kind;
	gchar *terminal_id;
	gchar *page_id;
	gchar *group_id;
	gchar *task_id;
	gchar *title;
	gboolean title_set_by_user;
	gboolean archived;
	guint8 *input_data;
	gsize input_length;
	guint cols;
	guint rows;
} SakuraAgentCommand;

typedef struct {
	SakuraApp *app;
	SakuraAgentCommandKind kind;
	gchar *terminal_id;
	gchar *message;
} SakuraAgentCommandFailure;

static void
sakura_agent_command_free(SakuraAgentCommand *command)
{
	if (command == NULL)
		return;
	g_free(command->terminal_id);
	g_free(command->page_id);
	g_free(command->group_id);
	g_free(command->task_id);
	g_free(command->title);
	g_free(command->input_data);
	g_free(command);
}


static gboolean
sakura_agent_apply_command_failure_cb(gpointer data)
{
	SakuraAgentCommandFailure *failure = data;
	SakuraTab *tab = NULL;

	if (failure->app != NULL && !failure->app->session_shutting_down &&
	    failure->app->workspace != NULL &&
	    failure->kind != SAKURA_AGENT_COMMAND_UPDATE_PAGE &&
	    failure->kind != SAKURA_AGENT_COMMAND_DELETE_PAGE)
		tab = sakura_find_pane_by_terminal_id(failure->terminal_id);
	if (failure->kind == SAKURA_AGENT_COMMAND_UPDATE_PAGE)
		g_warning("Could not update agent page %s: %s",
		          failure->terminal_id, failure->message);
	else if (failure->kind == SAKURA_AGENT_COMMAND_DELETE_PAGE)
		g_warning("Could not delete agent page %s: %s",
		          failure->terminal_id, failure->message);
	else if (tab != NULL) {
		if (failure->kind == SAKURA_AGENT_COMMAND_CLOSE)
			g_warning("Could not close agent terminal %s: %s",
			          failure->terminal_id, failure->message);
		else if (failure->kind == SAKURA_AGENT_COMMAND_DETACH)
			g_warning("Could not detach agent terminal %s: %s",
			          failure->terminal_id, failure->message);
		else
			sakura_tab_agent_status(tab, SAKURA_TERMINAL_ERROR,
			                       failure->message);
	}
	g_free(failure->terminal_id);
	g_free(failure->message);
	g_free(failure);
	return G_SOURCE_REMOVE;
}


static void
sakura_agent_report_command_failure(SakuraApp *app,
	                                  SakuraAgentCommand *command,
	                                  const GError *error)
{
	SakuraAgentCommandFailure *failure = g_new0(SakuraAgentCommandFailure, 1);

	if (sakura_agent_error_is_revision_conflict(error))
		sakura_agent_refresh_after_revision_conflict(app);

	failure->app = app;
	failure->kind = command->kind;
	failure->terminal_id = g_strdup(
		command->kind == SAKURA_AGENT_COMMAND_UPDATE_PAGE ||
		command->kind == SAKURA_AGENT_COMMAND_DELETE_PAGE
		? command->page_id : command->terminal_id);
	failure->message = g_strdup(error != NULL ? error->message :
	                            "agent command failed");
	if (app->session_shutting_down) {
		g_free(failure->terminal_id);
		g_free(failure->message);
		g_free(failure);
		return;
	}
	g_main_context_invoke(NULL, sakura_agent_apply_command_failure_cb, failure);
}


static gboolean
sakura_agent_send_command(SakuraApp *app,
	                       SakuraControlClientConnection *connection,
	                       SakuraAgentCommand *command, GError **error)
{
	GByteArray *request = g_byte_array_new();
	GByteArray *response_payload = NULL;
	SakuraControlResponse response = { 0 };
	gchar *request_id = g_uuid_string_random();
	gboolean encoded = FALSE;
	gboolean success = FALSE;
	gboolean mutation_locked = FALSE;

	switch (command->kind) {
	case SAKURA_AGENT_COMMAND_INPUT:
		encoded = sakura_control_encode_terminal_input_request(
			request_id, command->terminal_id, command->input_data,
			command->input_length, request);
		break;
	case SAKURA_AGENT_COMMAND_RESIZE:
		encoded = sakura_control_encode_terminal_resize_request(
			request_id, command->terminal_id, command->cols, command->rows,
			request);
		break;
	case SAKURA_AGENT_COMMAND_CLOSE:
		encoded = sakura_control_encode_close_terminal_request(
			request_id, command->terminal_id, request);
		break;
	case SAKURA_AGENT_COMMAND_DETACH:
		encoded = sakura_control_encode_detach_terminal_request(
			request_id, command->terminal_id, request);
		break;
	case SAKURA_AGENT_COMMAND_UPDATE_PAGE:
		encoded = sakura_control_encode_update_page_request(
			request_id, command->page_id, command->group_id, command->task_id,
			command->title, command->title_set_by_user, command->archived,
			request);
		break;
	case SAKURA_AGENT_COMMAND_DELETE_PAGE:
		encoded = sakura_control_encode_delete_page_request(
			request_id, command->page_id, request);
		break;
	default:
		break;
	}
	if (!encoded) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "could not encode agent command");
		goto out;
	}
	if (command->kind != SAKURA_AGENT_COMMAND_INPUT &&
	    command->kind != SAKURA_AGENT_COMMAND_RESIZE &&
	    app->agent_workspace_mutation_mutex_initialized) {
		g_mutex_lock(&app->agent_workspace_mutation_mutex);
		mutation_locked = TRUE;
	}
	if (command->kind != SAKURA_AGENT_COMMAND_INPUT &&
	    command->kind != SAKURA_AGENT_COMMAND_RESIZE &&
	    !sakura_control_request_set_expected_revision(
			request, sakura_agent_workspace_revision_get(app))) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			"could not add expected workspace revision");
		goto out;
	}
	if (!sakura_control_client_request_with_cancellable(
			connection, request, &response_payload,
			app->agent_command_cancellable, error) ||
		    !sakura_control_decode_response(response_payload->data,
	                                    response_payload->len, &response,
	                                    error))
		goto out;
	if (g_strcmp0(response.request_id, request_id) != 0) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                    "agent response id did not match command");
		goto out;
	}
	if (!response.accepted && !response.has_snapshot) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                    "agent did not accept command");
		goto out;
	}
	sakura_agent_workspace_revision_set(app, response.workspace_revision);
	success = TRUE;
out:
	if (mutation_locked)
		g_mutex_unlock(&app->agent_workspace_mutation_mutex);
	if (!success && error != NULL &&
	    sakura_agent_error_is_revision_conflict(*error)) {
		/* Error responses carry the authoritative revision. Advance it here,
		 * on the command worker, before the next queued mutation is encoded.
		 * Scheduling an asynchronous snapshot refresh alone leaves the rest of
		 * the queue using the same stale revision and creates a warning storm. */
		guint64 current_revision = response.error_current_revision != 0
		                           ? response.error_current_revision
		                           : response.workspace_revision;

		if (current_revision != 0)
			sakura_agent_workspace_revision_set(app, current_revision);
	}
	g_clear_pointer(&response_payload, g_byte_array_unref);
	sakura_control_response_clear(&response);
	g_byte_array_unref(request);
	g_free(request_id);
	return success;
}


static gpointer
sakura_agent_command_thread(gpointer data)
{
	SakuraApp *app = data;
	SakuraControlClientConnection *connection = NULL;

	for (;;) {
		SakuraAgentCommand *command;
		GError *error = NULL;
		guint revision_retries = 0;
		gboolean command_sent;

		g_mutex_lock(&app->agent_command_mutex);
		while (g_queue_is_empty(app->agent_command_queue) &&
		       !app->agent_command_stopping)
			g_cond_wait(&app->agent_command_cond, &app->agent_command_mutex);
		if (app->agent_command_stopping &&
		    g_queue_is_empty(app->agent_command_queue)) {
			g_mutex_unlock(&app->agent_command_mutex);
			break;
		}
		command = g_queue_pop_head(app->agent_command_queue);
		g_mutex_unlock(&app->agent_command_mutex);

		if (connection == NULL) {
			connection = sakura_agent_connect(
				app->agent_socket_path, app->workspace_id, &error);
			if (connection != NULL) {
				g_mutex_lock(&app->agent_command_mutex);
				app->agent_command_connection =
					sakura_control_client_ref(connection);
				g_mutex_unlock(&app->agent_command_mutex);
			}
		}
		if (connection == NULL) {
			sakura_agent_report_command_failure(app, command, error);
			g_clear_error(&error);
			sakura_agent_command_free(command);
			continue;
		}
		command_sent = sakura_agent_send_command(app, connection, command, &error);
		while (!command_sent && sakura_agent_error_is_revision_conflict(error) &&
		       revision_retries++ < 3) {
			/* A conflict guarantees that this mutation was not applied. Retrying
			 * it with the revision captured from the error response is therefore
			 * safe, and preserves the order of the command queue. */
			g_clear_error(&error);
			command_sent = sakura_agent_send_command(
				app, connection, command, &error);
		}
		if (!command_sent) {
			sakura_agent_report_command_failure(app, command, error);
			g_clear_error(&error);
			g_mutex_lock(&app->agent_command_mutex);
			if (app->agent_command_connection == connection) {
				sakura_control_client_unref(app->agent_command_connection);
				app->agent_command_connection = NULL;
			}
			g_mutex_unlock(&app->agent_command_mutex);
			sakura_control_client_close(connection);
			sakura_control_client_unref(connection);
			connection = NULL;
		}
		sakura_agent_command_free(command);
	}
	if (connection != NULL) {
		g_mutex_lock(&app->agent_command_mutex);
		if (app->agent_command_connection == connection) {
			sakura_control_client_unref(app->agent_command_connection);
			app->agent_command_connection = NULL;
		}
		g_mutex_unlock(&app->agent_command_mutex);
		sakura_control_client_close(connection);
		sakura_control_client_unref(connection);
	}
	return NULL;
}


static void
sakura_agent_command_start(SakuraApp *app)
{
	if (app == NULL || app->agent_socket_path == NULL ||
	    app->agent_command_thread != NULL)
		return;
	g_mutex_init(&app->agent_command_mutex);
	g_cond_init(&app->agent_command_cond);
	app->agent_command_queue = g_queue_new();
	app->agent_command_cancellable = g_cancellable_new();
	app->agent_command_mutex_initialized = TRUE;
	app->agent_command_stopping = FALSE;
	app->agent_command_thread = g_thread_new("sakura-agent-commands",
	                                         sakura_agent_command_thread, app);
}


static gboolean
sakura_agent_enqueue_command(SakuraApp *app, SakuraAgentCommand *command,
	                            GError **error)
{
	if (app == NULL || app->agent_socket_path == NULL || command == NULL) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED,
		                    "agent is not connected");
		sakura_agent_command_free(command);
		return FALSE;
	}
	if (!app->agent_command_mutex_initialized)
		sakura_agent_command_start(app);
	g_mutex_lock(&app->agent_command_mutex);
	if (app->agent_command_stopping) {
		g_mutex_unlock(&app->agent_command_mutex);
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_CLOSED,
		                    "agent command queue is stopping");
		sakura_agent_command_free(command);
		return FALSE;
	}
	if (command->kind == SAKURA_AGENT_COMMAND_RESIZE) {
		for (GList *link = app->agent_command_queue->tail; link != NULL;
		     link = link->prev) {
			SakuraAgentCommand *pending = link->data;

			if (pending != NULL && pending->kind == command->kind &&
			    g_strcmp0(pending->terminal_id, command->terminal_id) == 0) {
				pending->cols = command->cols;
				pending->rows = command->rows;
				g_mutex_unlock(&app->agent_command_mutex);
				sakura_agent_command_free(command);
				return TRUE;
			}
		}
	}
	if (command->kind == SAKURA_AGENT_COMMAND_UPDATE_PAGE) {
		for (GList *link = app->agent_command_queue->tail; link != NULL;
		     link = link->prev) {
			SakuraAgentCommand *pending = link->data;

			if (pending != NULL &&
			    pending->kind == SAKURA_AGENT_COMMAND_UPDATE_PAGE &&
			    g_strcmp0(pending->page_id, command->page_id) == 0) {
				g_free(pending->group_id);
				pending->group_id = g_strdup(command->group_id);
				g_free(pending->task_id);
				pending->task_id = g_strdup(command->task_id);
				g_free(pending->title);
				pending->title = g_strdup(command->title);
				pending->title_set_by_user = command->title_set_by_user;
				pending->archived = command->archived;
				g_mutex_unlock(&app->agent_command_mutex);
				sakura_agent_command_free(command);
				return TRUE;
			}
		}
	}
	g_queue_push_tail(app->agent_command_queue, command);
	g_cond_signal(&app->agent_command_cond);
	g_mutex_unlock(&app->agent_command_mutex);
	return TRUE;
}


static gboolean
sakura_agent_apply_workspace_snapshot(SakuraApp *app,
                                       SakuraSessionSnapshot *snapshot)
{
	gboolean scope_needs_default = FALSE;

	if (app == NULL || snapshot == NULL || app->workspace == NULL ||
	    app->session_shutting_down)
		return FALSE;
	sakura_agent_workspace_revision_set(app, snapshot->workspace_revision);

	sakura_workspace_begin_mutation();
	/* The agent snapshot is authoritative for domain relationships. GTK pages,
	 * widgets, and layout nodes remain owned by the desktop, but their stable
	 * page identities and domain parents come from the agent. */
	sakura_workspace_model_merge_agent_snapshot(app->workspace, snapshot);
	if (snapshot->root_directory != NULL &&
	    snapshot->root_directory[0] != '\0') {
		g_free(app->workspace->root_group->directory);
		app->workspace->root_group->directory = g_strdup(
			snapshot->root_directory);
	}
	if (app->workspace->active_task != NULL &&
	    sakura_workspace_model_task_is_archived(
			app->workspace, app->workspace->active_task))
	{
		app->workspace->active_task = NULL;
		scope_needs_default = TRUE;
	}
	if (app->workspace->active_group != NULL &&
	    sakura_workspace_model_group_is_archived(
			app->workspace, app->workspace->active_group)) {
		app->workspace->active_group = app->workspace->root_group;
		app->active_group_scope = app->sidebar_root;
		if (app->sidebar_root != NULL)
			sakura_sidebar_set_scope(app->sidebar_root);
		scope_needs_default = TRUE;
	}
	if (app->workspace->active_page != NULL &&
	    app->workspace->active_page->archived) {
		app->workspace->active_page = NULL;
		app->workspace->active_tab = NULL;
		scope_needs_default = TRUE;
	}
	sakura_sidebar_rebuild_projection();
	sakura_workspace_mark_changed(SAKURA_WORKSPACE_CHANGE_STRUCTURE |
	                              SAKURA_WORKSPACE_CHANGE_METADATA);
	sakura_workspace_end_mutation();
	/* The desktop file stores presentation joins by stable ID. Refresh it after
	 * agent changes without copying the authoritative hierarchy back into it. */
	sakura_session_mark_dirty();
	if (scope_needs_default && !app->session_shutting_down)
		sakura_select_scope_default();
	return TRUE;
}


void
sakura_agent_apply_pending_snapshot(SakuraApp *app)
{
	SakuraSessionSnapshot *snapshot;

	if (app == NULL || app->session_restoring ||
	    app->agent_pending_snapshot == NULL)
		return;
	snapshot = app->agent_pending_snapshot;
	app->agent_pending_snapshot = NULL;
	if (!sakura_agent_apply_workspace_snapshot(app, snapshot))
		g_warning("Could not apply pending sakura-agent workspace snapshot");
	sakura_session_snapshot_free(snapshot);
}


static gboolean
sakura_agent_apply_event_cb(gpointer data)
{
	SakuraAgentEvent *event = data;

	if (event->app != NULL && event->app->session_restoring &&
	    event->app->workspace != NULL &&
	    !event->app->session_shutting_down) {
		/* Restoring passes sidebar node pointers through several GTK calls.
		 * Rebuilding the projection here would invalidate a restore parent
		 * halfway through the operation. Keep only the newest event and apply it
		 * once restoration has completed. */
		sakura_session_snapshot_free(event->app->agent_pending_snapshot);
		event->app->agent_pending_snapshot = event->snapshot;
		event->snapshot = NULL;
	} else if (event->app != NULL) {
		sakura_agent_apply_workspace_snapshot(event->app, event->snapshot);
	}
	sakura_session_snapshot_free(event->snapshot);
	g_free(event);
	return G_SOURCE_REMOVE;
}


static gboolean
sakura_agent_apply_terminal_event_cb(gpointer data)
{
	SakuraAgentTerminalEvent *event = data;
	SakuraTab *tab = NULL;

	if (event->app != NULL && !event->app->session_shutting_down &&
	    event->app->workspace != NULL)
		tab = sakura_find_pane_by_terminal_id(event->terminal_id);
	if (tab != NULL) {
		if (event->output)
			sakura_tab_agent_feed_output(tab, event->data, event->data_length,
			                              event->start_offset,
			                              event->end_offset);
		else
			sakura_tab_agent_status(tab, event->status, event->message);
	}
	g_free(event->terminal_id);
	g_free(event->data);
	g_free(event->message);
	g_free(event);
	return G_SOURCE_REMOVE;
}


static gboolean
sakura_agent_event_is_stopping(SakuraApp *app)
{
	gboolean stopping;

	if (app == NULL || !app->agent_event_mutex_initialized)
		return TRUE;
	g_mutex_lock(&app->agent_event_mutex);
	stopping = app->agent_event_stopping;
	g_mutex_unlock(&app->agent_event_mutex);
	return stopping;
}


static gboolean
sakura_agent_event_retry_wait(SakuraApp *app)
{
	for (guint attempt = 0; attempt < 10; attempt++) {
		if (sakura_agent_event_is_stopping(app))
			return FALSE;
		g_usleep(25 * 1000);
	}
	return !sakura_agent_event_is_stopping(app);
}


static gboolean
sakura_agent_resume_terminals_cb(gpointer data)
{
	SakuraApp *app = data;

	if (app == NULL || app->session_shutting_down || app->workspace == NULL ||
	    app->workspace->panes == NULL)
		return G_SOURCE_REMOVE;
	for (guint index = 0; index < app->workspace->panes->len; index++) {
		SakuraTab *tab = g_ptr_array_index(app->workspace->panes, index);

		if (tab == NULL || !tab->agent_backed || tab->agent_terminal_exited)
			continue;
		/* A dropped event stream does not invalidate the agent-owned PTY or
		 * the local proxy. Reattach only the event-side cursor and replay the
		 * bytes that arrived while the stream was unavailable. */
		(void)sakura_tab_resume_agent_terminal(tab);
	}
	return G_SOURCE_REMOVE;
}


static gpointer
sakura_agent_event_thread(gpointer data)
{
	SakuraApp *app = data;
	SakuraControlClientConnection *connection = NULL;
	GByteArray *payload = NULL;
	GError *error = NULL;
	gboolean connected_before = FALSE;
	gboolean resume_terminals = FALSE;

	while (!sakura_agent_event_is_stopping(app)) {
		connection = sakura_agent_connect(
			app->agent_socket_path, app->workspace_id, &error);
		if (connection == NULL) {
			if (error != NULL && !sakura_agent_event_is_stopping(app))
				g_debug("Could not connect agent event stream: %s", error->message);
			g_clear_error(&error);
			if (!sakura_agent_event_retry_wait(app))
				break;
			continue;
		}

		g_mutex_lock(&app->agent_event_mutex);
		if (app->agent_event_stopping) {
			g_mutex_unlock(&app->agent_event_mutex);
			sakura_control_client_close(connection);
			sakura_control_client_unref(connection);
			connection = NULL;
			break;
		}
		app->agent_event_connection = sakura_control_client_ref(connection);
		g_mutex_unlock(&app->agent_event_mutex);

		if (!sakura_control_client_subscribe_events(connection, 0, &error))
			goto disconnect;
		resume_terminals = connected_before;
		connected_before = TRUE;
		g_clear_pointer(&payload, g_byte_array_unref);
		while (sakura_control_client_read_frame(
			connection, &payload, NULL, &error)) {
			SakuraAgentEvent *event = g_new0(SakuraAgentEvent, 1);
			SakuraAgentTerminalEvent *terminal_event;
			guint64 terminal_sequence;
			gboolean final_chunk;

			if (!sakura_control_decode_workspace_changed_event(
				payload->data, payload->len, &event->sequence, &event->snapshot,
				&error)) {
				g_free(event);
				g_clear_error(&error);
				terminal_event = g_new0(SakuraAgentTerminalEvent, 1);
				terminal_event->app = app;
				if (sakura_control_decode_terminal_output_event_with_offsets(
					payload->data, payload->len, &terminal_sequence,
					&terminal_event->terminal_id,
					&terminal_event->start_offset, &terminal_event->end_offset,
					&terminal_event->data, &terminal_event->data_length,
					&final_chunk, &error)) {
					terminal_event->output = TRUE;
					(void)terminal_sequence;
					(void)final_chunk;
					g_main_context_invoke(NULL,
					                     sakura_agent_apply_terminal_event_cb,
					                     terminal_event);
					g_clear_pointer(&payload, g_byte_array_unref);
					continue;
				}
				g_clear_error(&error);
				g_free(terminal_event);
				terminal_event = g_new0(SakuraAgentTerminalEvent, 1);
				terminal_event->app = app;
				if (sakura_control_decode_terminal_status_event(
					payload->data, payload->len, &terminal_sequence,
					&terminal_event->terminal_id,
					&terminal_event->status, &terminal_event->message, &error)) {
					g_main_context_invoke(NULL,
					                     sakura_agent_apply_terminal_event_cb,
					                     terminal_event);
					g_clear_pointer(&payload, g_byte_array_unref);
					continue;
				}
				g_free(terminal_event->terminal_id);
				g_free(terminal_event->message);
				g_free(terminal_event);
				g_clear_pointer(&payload, g_byte_array_unref);
				break;
			}
			event->app = app;
			g_main_context_invoke(NULL, sakura_agent_apply_event_cb, event);
			if (resume_terminals) {
				g_main_context_invoke(NULL,
				                     sakura_agent_resume_terminals_cb, app);
				resume_terminals = FALSE;
			}
			g_clear_pointer(&payload, g_byte_array_unref);
		}

disconnect:
		if (error != NULL && !sakura_agent_event_is_stopping(app))
			g_debug("Agent event subscription stopped: %s", error->message);
		g_clear_error(&error);
		g_clear_pointer(&payload, g_byte_array_unref);
		g_mutex_lock(&app->agent_event_mutex);
		if (app->agent_event_connection == connection) {
			sakura_control_client_unref(app->agent_event_connection);
			app->agent_event_connection = NULL;
		}
		g_mutex_unlock(&app->agent_event_mutex);
		sakura_control_client_close(connection);
		sakura_control_client_unref(connection);
		connection = NULL;
		if (!sakura_agent_event_retry_wait(app))
			break;
	}
	g_clear_error(&error);
	g_clear_pointer(&payload, g_byte_array_unref);
	return NULL;
}


static void
sakura_agent_subscribe_start(SakuraApp *app)
{
	if (app == NULL || app->agent_socket_path == NULL ||
	    app->agent_event_thread != NULL)
		return;
	g_mutex_init(&app->agent_event_mutex);
	app->agent_event_mutex_initialized = TRUE;
	app->agent_event_stopping = FALSE;
	app->agent_event_thread = g_thread_new("sakura-agent-events",
	                                      sakura_agent_event_thread, app);
}


static void
sakura_agent_command_stop(SakuraApp *app)
{
	if (app == NULL || !app->agent_command_mutex_initialized)
		return;
	g_mutex_lock(&app->agent_command_mutex);
	app->agent_command_stopping = TRUE;
	g_cond_broadcast(&app->agent_command_cond);
	g_mutex_unlock(&app->agent_command_mutex);
	/* Structural ownership changes are queued to keep drag/drop responsive.
	 * Drain accepted commands before teardown. Each request has a deadline, so
	 * an unavailable agent cannot hold shutdown indefinitely. */
	if (app->agent_command_thread != NULL) {
		g_thread_join(app->agent_command_thread);
		app->agent_command_thread = NULL;
	}
	g_mutex_lock(&app->agent_command_mutex);
	if (app->agent_command_connection != NULL) {
		sakura_control_client_unref(app->agent_command_connection);
		app->agent_command_connection = NULL;
	}
	while (app->agent_command_queue != NULL &&
	       !g_queue_is_empty(app->agent_command_queue))
		sakura_agent_command_free(g_queue_pop_head(app->agent_command_queue));
	g_mutex_unlock(&app->agent_command_mutex);
	g_clear_pointer(&app->agent_command_queue, g_queue_free);
	g_clear_object(&app->agent_command_cancellable);
	g_cond_clear(&app->agent_command_cond);
	g_mutex_clear(&app->agent_command_mutex);
	app->agent_command_mutex_initialized = FALSE;
}


static void
sakura_agent_event_stop(SakuraApp *app)
{
	if (app == NULL || !app->agent_event_mutex_initialized)
		return;
	SakuraControlClientConnection *connection = NULL;

	g_mutex_lock(&app->agent_event_mutex);
	app->agent_event_stopping = TRUE;
	if (app->agent_event_connection != NULL)
		connection = sakura_control_client_ref(app->agent_event_connection);
	g_mutex_unlock(&app->agent_event_mutex);
	if (connection != NULL) {
		sakura_control_client_shutdown(connection);
		sakura_control_client_unref(connection);
	}
	if (app->agent_event_thread != NULL) {
		g_thread_join(app->agent_event_thread);
		app->agent_event_thread = NULL;
	}
	g_mutex_lock(&app->agent_event_mutex);
	if (app->agent_event_connection != NULL) {
		sakura_control_client_unref(app->agent_event_connection);
		app->agent_event_connection = NULL;
	}
	g_mutex_unlock(&app->agent_event_mutex);
	g_mutex_clear(&app->agent_event_mutex);
	app->agent_event_mutex_initialized = FALSE;
}


static gboolean
sakura_agent_request_snapshot(const gchar *socket_path,
	                           const gchar *workspace_id,
	                           SakuraSessionSnapshot **snapshot,
	                           GError **error)
{
	SakuraControlClientConnection *connection;
	GByteArray *request = NULL;
	GByteArray *response_payload = NULL;
	SakuraControlResponse response = { 0 };
	gchar *request_id = NULL;
	gboolean success = FALSE;
	SakuraSessionSnapshot *decoded_snapshot = NULL;
	guint64 sequence = 0;

	if (snapshot != NULL)
		*snapshot = NULL;

	connection = sakura_agent_connect(socket_path, workspace_id, error);
	if (connection == NULL)
		return FALSE;
	request_id = g_uuid_string_random();
	request = g_byte_array_new();
	sakura_control_encode_get_snapshot_request(request_id, request);
	if (!sakura_control_client_request(
			connection, request, &response_payload, error) ||
	    !sakura_control_decode_response(response_payload->data,
	                                    response_payload->len, &response,
	                                    error))
		goto out;
	if (g_strcmp0(response.request_id, request_id) != 0) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                    "agent response id did not match request");
		goto out;
	}
	if (!response.has_snapshot ||
	    !sakura_control_decode_snapshot_response(
			response_payload->data, response_payload->len, &sequence,
			&decoded_snapshot, error))
		goto out;
	success = TRUE;
out:
	if (success && snapshot != NULL) {
		*snapshot = decoded_snapshot;
		decoded_snapshot = NULL;
	}
	sakura_session_snapshot_free(decoded_snapshot);
	g_clear_pointer(&response_payload, g_byte_array_unref);
	g_clear_pointer(&request, g_byte_array_unref);
	sakura_control_response_clear(&response);
	g_clear_pointer(&request_id, g_free);
	sakura_control_client_close(connection);
	sakura_control_client_unref(connection);
	return success;
}


typedef struct {
	SakuraApp *app;
	gchar *socket_path;
	gchar *workspace_id;
} SakuraAgentRefreshJob;


static gpointer
sakura_agent_refresh_thread(gpointer data)
{
	SakuraAgentRefreshJob *job = data;
	SakuraSessionSnapshot *snapshot = NULL;
	GError *error = NULL;

	if (sakura_agent_request_snapshot(job->socket_path, job->workspace_id,
	                                  &snapshot, &error)) {
		SakuraAgentEvent *event = g_new0(SakuraAgentEvent, 1);

		event->app = job->app;
		event->snapshot = snapshot;
		g_main_context_invoke(NULL, sakura_agent_apply_event_cb, event);
	} else {
		g_debug("Could not refresh workspace after revision conflict: %s",
		        error != NULL ? error->message : "unknown error");
	}
	g_clear_error(&error);
	g_free(job->socket_path);
	g_free(job->workspace_id);
	g_free(job);
	return NULL;
}


static void
sakura_agent_refresh_after_revision_conflict(SakuraApp *app)
{
	SakuraAgentRefreshJob *job;

	if (app == NULL || app->session_shutting_down ||
	    app->agent_socket_path == NULL || app->workspace_id == NULL)
		return;
	job = g_new0(SakuraAgentRefreshJob, 1);
	job->app = app;
	job->socket_path = g_strdup(app->agent_socket_path);
	job->workspace_id = g_strdup(app->workspace_id);
	g_thread_unref(g_thread_new("sakura-agent-refresh",
	                            sakura_agent_refresh_thread, job));
}


static gboolean
sakura_agent_request_mutation(SakuraApp *app, const gchar *request_id,
	                            GByteArray *request, GError **error)
{
	SakuraControlClientConnection *connection;
	GByteArray *response_payload = NULL;
	SakuraControlResponse response = { 0 };
	gboolean success = FALSE;
	gboolean mutation_locked = FALSE;
	guint revision_retries = 0;

	if (app == NULL || app->agent_socket_path == NULL || request_id == NULL ||
	    request == NULL)
		return FALSE;
	connection = sakura_agent_connect(
		app->agent_socket_path, app->workspace_id, error);
	if (connection == NULL)
		return FALSE;
	if (app->agent_workspace_mutation_mutex_initialized) {
		g_mutex_lock(&app->agent_workspace_mutation_mutex);
		mutation_locked = TRUE;
	}
	for (;;) {
		if (!sakura_control_request_set_expected_revision(
				request, sakura_agent_workspace_revision_get(app))) {
			g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			                    "could not add expected workspace revision");
			goto out;
		}
		if (sakura_control_client_request_with_cancellable(
				connection, request, &response_payload,
				app->agent_command_cancellable, error) &&
		    sakura_control_decode_response(response_payload->data,
		                                    response_payload->len, &response,
		                                    error))
			break;
		if (!sakura_agent_error_is_revision_conflict(
				error != NULL ? *error : NULL) || revision_retries++ >= 3)
			goto out;
		{
			guint64 current_revision = response.error_current_revision != 0
			                           ? response.error_current_revision
			                           : response.workspace_revision;

			if (current_revision == 0)
				goto out;
			sakura_agent_workspace_revision_set(app, current_revision);
		}
		g_clear_error(error);
		g_clear_pointer(&response_payload, g_byte_array_unref);
		sakura_control_response_clear(&response);
	}
	if (g_strcmp0(response.request_id, request_id) != 0) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                    "agent response id did not match request");
		goto out;
	}
	if (!response.has_snapshot) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                    "agent mutation did not return a snapshot");
		goto out;
	}
	sakura_agent_workspace_revision_set(app, response.workspace_revision);
	success = TRUE;
out:
	if (mutation_locked)
		g_mutex_unlock(&app->agent_workspace_mutation_mutex);
	g_clear_pointer(&response_payload, g_byte_array_unref);
	sakura_control_response_clear(&response);
	sakura_control_client_close(connection);
	sakura_control_client_unref(connection);
	return success;
}


static gboolean
sakura_agent_request_encoded_mutation(SakuraApp *app, const gchar *request_id,
	                                   GByteArray *request, gboolean encoded,
	                                   const gchar *description, GError **error)
{
	if (!encoded) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		            "could not encode %s request", description);
		return FALSE;
	}
	return sakura_agent_request_mutation(app, request_id, request, error);
}


static gboolean
sakura_agent_request_accepted(SakuraApp *app, const gchar *request_id,
	                             GByteArray *request, gchar **accepted_id,
	                             GError **error)
{
	SakuraControlClientConnection *connection;
	GByteArray *response_payload = NULL;
	SakuraControlResponse response = { 0 };
	gboolean success = FALSE;
	gboolean mutation_locked = FALSE;
	guint revision_retries = 0;

	if (accepted_id != NULL)
		*accepted_id = NULL;
	if (app == NULL || app->agent_socket_path == NULL || request_id == NULL ||
	    request == NULL)
		return FALSE;
	connection = sakura_agent_connect(
		app->agent_socket_path, app->workspace_id, error);
	if (connection == NULL)
		return FALSE;
	if (app->agent_workspace_mutation_mutex_initialized) {
		g_mutex_lock(&app->agent_workspace_mutation_mutex);
		mutation_locked = TRUE;
	}
	for (;;) {
		if (!sakura_control_request_set_expected_revision(
				request, sakura_agent_workspace_revision_get(app))) {
			g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			                    "could not add expected workspace revision");
			goto out;
		}
		if (sakura_control_client_request_with_cancellable(
				connection, request, &response_payload,
				app->agent_command_cancellable, error) &&
		    sakura_control_decode_response(response_payload->data,
		                                    response_payload->len, &response,
		                                    error))
			break;
		if (!sakura_agent_error_is_revision_conflict(
				error != NULL ? *error : NULL) || revision_retries++ >= 3)
			goto out;
		/* Creation was rejected before it could have side effects. Adopt the
		 * authoritative revision while holding the mutation lock, then retry the
		 * same stable terminal ID so concurrent restore jobs cannot cascade. */
		{
			guint64 current_revision = response.error_current_revision != 0
			                           ? response.error_current_revision
			                           : response.workspace_revision;

			if (current_revision == 0)
				goto out;
			sakura_agent_workspace_revision_set(app, current_revision);
		}
		g_clear_error(error);
		g_clear_pointer(&response_payload, g_byte_array_unref);
		sakura_control_response_clear(&response);
	}
	if (g_strcmp0(response.request_id, request_id) != 0) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                    "agent response id did not match request");
		goto out;
	}
	if (!response.accepted) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                    "agent did not accept terminal request");
		goto out;
	}
	sakura_agent_workspace_revision_set(app, response.workspace_revision);
	if (accepted_id != NULL)
		*accepted_id = g_strdup(response.accepted_id);
	success = TRUE;
out:
	if (mutation_locked)
		g_mutex_unlock(&app->agent_workspace_mutation_mutex);
	if (!success && error != NULL &&
	    sakura_agent_error_is_revision_conflict(*error))
		sakura_agent_refresh_after_revision_conflict(app);
	g_clear_pointer(&response_payload, g_byte_array_unref);
	sakura_control_response_clear(&response);
	sakura_control_client_close(connection);
	sakura_control_client_unref(connection);
	return success;
}


static gboolean
sakura_agent_request_attached(SakuraApp *app, const gchar *request_id,
	                            GByteArray *request, guint8 **replay_data,
	                            gsize *replay_length,
	                            guint64 *replay_start_offset,
	                            guint64 *replay_end_offset, guint *cols,
	                            guint *rows, guint *status, GError **error)
{
	SakuraControlClientConnection *connection;
	GByteArray *response_payload = NULL;
	SakuraControlResponse response = { 0 };
	gboolean success = FALSE;
	gboolean mutation_locked = FALSE;
	guint revision_retries = 0;

	if (replay_data != NULL)
		*replay_data = NULL;
	if (replay_length != NULL)
		*replay_length = 0;
	if (replay_start_offset != NULL)
		*replay_start_offset = 0;
	if (replay_end_offset != NULL)
		*replay_end_offset = 0;
	if (cols != NULL)
		*cols = 0;
	if (rows != NULL)
		*rows = 0;
	if (status != NULL)
		*status = 0;
	if (app == NULL || app->agent_socket_path == NULL || request_id == NULL ||
	    request == NULL)
		return FALSE;
	connection = sakura_agent_connect(
		app->agent_socket_path, app->workspace_id, error);
	if (connection == NULL)
		return FALSE;
	if (app->agent_workspace_mutation_mutex_initialized) {
		g_mutex_lock(&app->agent_workspace_mutation_mutex);
		mutation_locked = TRUE;
	}
	for (;;) {
		if (!sakura_control_request_set_expected_revision(
				request, sakura_agent_workspace_revision_get(app))) {
			g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			                    "could not add expected workspace revision");
			goto out;
		}
		if (sakura_control_client_request_with_cancellable(
				connection, request, &response_payload,
				app->agent_command_cancellable, error) &&
		    sakura_control_decode_response(response_payload->data,
		                                    response_payload->len, &response,
		                                    error))
			break;
		if (!sakura_agent_error_is_revision_conflict(
				error != NULL ? *error : NULL) || revision_retries++ >= 3)
			goto out;
		{
			guint64 current_revision = response.error_current_revision != 0
			                           ? response.error_current_revision
			                           : response.workspace_revision;

			if (current_revision == 0)
				goto out;
			sakura_agent_workspace_revision_set(app, current_revision);
		}
		g_clear_error(error);
		g_clear_pointer(&response_payload, g_byte_array_unref);
		sakura_control_response_clear(&response);
	}
	if (g_strcmp0(response.request_id, request_id) != 0) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
	                    "agent response id did not match attach request");
		goto out;
	}
	if (!response.attached) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                    "agent did not attach terminal");
		goto out;
	}
	sakura_agent_workspace_revision_set(app, response.workspace_revision);
	if (replay_data != NULL) {
		*replay_data = response.attached_output;
		response.attached_output = NULL;
	}
	if (replay_length != NULL)
		*replay_length = response.attached_output_length;
	if (replay_start_offset != NULL)
		*replay_start_offset = response.attached_output_start_offset;
	if (replay_end_offset != NULL)
		*replay_end_offset = response.attached_output_end_offset;
	if (cols != NULL)
		*cols = response.attached_cols;
	if (rows != NULL)
		*rows = response.attached_rows;
	if (status != NULL)
		*status = response.attached_status;
	success = TRUE;
out:
	if (mutation_locked)
		g_mutex_unlock(&app->agent_workspace_mutation_mutex);
	g_clear_pointer(&response_payload, g_byte_array_unref);
	sakura_control_response_clear(&response);
	sakura_control_client_close(connection);
	sakura_control_client_unref(connection);
	return success;
}


gboolean
sakura_agent_create_group(SakuraApp *app, const gchar *parent_id,
	                       const gchar *title, const gchar *directory,
	                       GError **error)
{
	GByteArray *request;
	gchar *request_id;
	gboolean result;

	if (app == NULL || app->agent_socket_path == NULL)
		return FALSE;
	request_id = g_uuid_string_random();
	request = g_byte_array_new();
	result = sakura_agent_request_encoded_mutation(
		app, request_id, request,
		sakura_control_encode_create_group_request(request_id, parent_id, title,
		                                           directory, request),
		"create group", error);
	g_byte_array_unref(request);
	g_free(request_id);
	return result;
}


gboolean
sakura_agent_update_group(SakuraApp *app, const gchar *group_id,
	                       const gchar *title, const gchar *directory,
	                       GError **error)
{
	GByteArray *request;
	gchar *request_id;
	gboolean result;

	if (app == NULL || app->agent_socket_path == NULL)
		return FALSE;
	request_id = g_uuid_string_random();
	request = g_byte_array_new();
	result = sakura_agent_request_encoded_mutation(
		app, request_id, request,
		sakura_control_encode_update_group_request(request_id, group_id, title,
		                                          directory, request),
		"update group", error);
	g_byte_array_unref(request);
	g_free(request_id);
	return result;
}


gboolean
sakura_agent_move_group(SakuraApp *app, const gchar *group_id,
	                      const gchar *parent_id, const gchar *target_id,
	                      gboolean after, GError **error)
{
	GByteArray *request;
	gchar *request_id;
	gboolean result;

	if (app == NULL || app->agent_socket_path == NULL)
		return FALSE;
	request_id = g_uuid_string_random();
	request = g_byte_array_new();
	result = sakura_agent_request_encoded_mutation(
		app, request_id, request,
		sakura_control_encode_move_group_request(request_id, group_id, parent_id,
		                                         target_id, after, request),
		"move group", error);
	g_byte_array_unref(request);
	g_free(request_id);
	return result;
}


gboolean
sakura_agent_set_group_archived(SakuraApp *app, const gchar *group_id,
	                              gboolean archived, GError **error)
{
	GByteArray *request;
	gchar *request_id;
	gboolean result;

	if (app == NULL || app->agent_socket_path == NULL)
		return FALSE;
	request_id = g_uuid_string_random();
	request = g_byte_array_new();
	result = sakura_agent_request_encoded_mutation(
		app, request_id, request,
		sakura_control_encode_set_group_archived_request(request_id, group_id,
		                                               archived, request),
		"set group archive state", error);
	g_byte_array_unref(request);
	g_free(request_id);
	return result;
}


gboolean
sakura_agent_delete_group(SakuraApp *app, const gchar *group_id,
	                       GError **error)
{
	GByteArray *request;
	gchar *request_id;
	gboolean result;

	if (app == NULL || app->agent_socket_path == NULL)
		return FALSE;
	request_id = g_uuid_string_random();
	request = g_byte_array_new();
	result = sakura_agent_request_encoded_mutation(
		app, request_id, request,
		sakura_control_encode_delete_group_request(request_id, group_id, request),
		"delete group", error);
	g_byte_array_unref(request);
	g_free(request_id);
	return result;
}


gboolean
sakura_agent_create_task(SakuraApp *app, const gchar *group_id,
	                      const gchar *parent_id, const gchar *title,
	                      const gchar *provider, const gchar *external_id,
	                      const gchar *url, GError **error)
{
	GByteArray *request;
	gchar *request_id;
	gboolean result;

	if (app == NULL || app->agent_socket_path == NULL)
		return FALSE;
	request_id = g_uuid_string_random();
	request = g_byte_array_new();
	result = sakura_agent_request_encoded_mutation(
		app, request_id, request,
		sakura_control_encode_create_task_request(request_id, group_id, parent_id,
		                                          title, provider, external_id,
		                                          url, request),
		"create task", error);
	g_byte_array_unref(request);
	g_free(request_id);
	return result;
}


gboolean
sakura_agent_update_task(SakuraApp *app, const gchar *task_id,
	                       const gchar *title, GError **error)
{
	GByteArray *request;
	gchar *request_id;
	gboolean result;

	if (app == NULL || app->agent_socket_path == NULL)
		return FALSE;
	request_id = g_uuid_string_random();
	request = g_byte_array_new();
	result = sakura_agent_request_encoded_mutation(
		app, request_id, request,
		sakura_control_encode_update_task_request(request_id, task_id, title,
		                                         request),
		"update task", error);
	g_byte_array_unref(request);
	g_free(request_id);
	return result;
}


gboolean
sakura_agent_move_task(SakuraApp *app, const gchar *task_id,
	                    const gchar *group_id, const gchar *parent_id,
	                    const gchar *target_id, gboolean after,
	                    GError **error)
{
	GByteArray *request;
	gchar *request_id;
	gboolean result;

	if (app == NULL || app->agent_socket_path == NULL)
		return FALSE;
	request_id = g_uuid_string_random();
	request = g_byte_array_new();
	result = sakura_agent_request_encoded_mutation(
		app, request_id, request,
		sakura_control_encode_move_task_request(
			request_id, task_id, group_id, parent_id, target_id, after, request),
		"move task", error);
	g_byte_array_unref(request);
	g_free(request_id);
	return result;
}


gboolean
sakura_agent_update_page(SakuraApp *app, const gchar *page_id,
	                       const gchar *group_id, const gchar *task_id,
	                       const gchar *title, gboolean title_set_by_user,
	                       gboolean archived, GError **error)
{
	GByteArray *request;
	gchar *request_id;
	gboolean result;

	if (app == NULL || app->agent_socket_path == NULL ||
	    page_id == NULL || page_id[0] == '\0') {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "page id is required");
		return FALSE;
	}
	request_id = g_uuid_string_random();
	request = g_byte_array_new();
	result = sakura_agent_request_encoded_mutation(
		app, request_id, request,
		sakura_control_encode_update_page_request(
			request_id, page_id, group_id, task_id, title,
			title_set_by_user, archived, request),
		"update page", error);
	g_byte_array_unref(request);
	g_free(request_id);
	return result;
}


gboolean
sakura_agent_move_page(SakuraApp *app, const gchar *page_id,
                       const gchar *group_id, const gchar *task_id,
                       GError **error)
{
	GByteArray *request;
	gchar *request_id;
	gboolean result;

	if (app == NULL || app->agent_socket_path == NULL || page_id == NULL ||
	    page_id[0] == '\0' || group_id == NULL || group_id[0] == '\0')
		return FALSE;
	request_id = g_uuid_string_random();
	request = g_byte_array_new();
	result = sakura_agent_request_encoded_mutation(
		app, request_id, request,
		sakura_control_encode_move_page_request(
			request_id, page_id, group_id, task_id, request),
		"move page", error);
	g_byte_array_unref(request);
	g_free(request_id);
	return result;
}


gboolean
sakura_agent_rename_page(SakuraApp *app, const gchar *page_id,
                         const gchar *title, gboolean title_set_by_user,
                         GError **error)
{
	GByteArray *request = g_byte_array_new();
	gchar *request_id = g_uuid_string_random();
	gboolean result = sakura_agent_request_encoded_mutation(
		app, request_id, request,
		sakura_control_encode_rename_page_request(
			request_id, page_id, title, title_set_by_user, request),
		"rename page", error);

	g_byte_array_unref(request);
	g_free(request_id);
	return result;
}


gboolean
sakura_agent_set_page_archived(SakuraApp *app, const gchar *page_id,
                               gboolean archived, GError **error)
{
	GByteArray *request = g_byte_array_new();
	gchar *request_id = g_uuid_string_random();
	gboolean result = sakura_agent_request_encoded_mutation(
		app, request_id, request,
		sakura_control_encode_set_page_archived_request(
			request_id, page_id, archived, request),
		"set page archived", error);

	g_byte_array_unref(request);
	g_free(request_id);
	return result;
}


gboolean
sakura_agent_set_task_status(SakuraApp *app, const gchar *task_id,
	                          SakuraTaskStatus status, GError **error)
{
	GByteArray *request = g_byte_array_new();
	gchar *request_id = g_uuid_string_random();
	gboolean result = sakura_agent_request_encoded_mutation(
		app, request_id, request,
		sakura_control_encode_set_task_status_request(
			request_id, task_id, status, request),
		"set task status", error);

	g_byte_array_unref(request);
	g_free(request_id);
	return result;
}


gboolean
sakura_agent_delete_page(SakuraApp *app, const gchar *page_id,
	                       GError **error)
{
	SakuraAgentCommand *command;

	if (page_id == NULL || page_id[0] == '\0') {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "page id is required");
		return FALSE;
	}
	command = g_new0(SakuraAgentCommand, 1);
	command->kind = SAKURA_AGENT_COMMAND_DELETE_PAGE;
	command->page_id = g_strdup(page_id);
	return sakura_agent_enqueue_command(app, command, error);
}


gboolean
sakura_agent_set_task_archived(SakuraApp *app, const gchar *task_id,
	                             gboolean archived, GError **error)
{
	GByteArray *request;
	gchar *request_id;
	gboolean result;

	if (app == NULL || app->agent_socket_path == NULL)
		return FALSE;
	request_id = g_uuid_string_random();
	request = g_byte_array_new();
	result = sakura_agent_request_encoded_mutation(
		app, request_id, request,
		sakura_control_encode_set_task_archived_request(request_id, task_id,
	                                                archived, request),
		"set task archive state", error);
	g_byte_array_unref(request);
	g_free(request_id);
	return result;
}


gboolean
sakura_agent_delete_task(SakuraApp *app, const gchar *task_id,
	                      GError **error)
{
	GByteArray *request;
	gchar *request_id;
	gboolean result;

	if (app == NULL || app->agent_socket_path == NULL)
		return FALSE;
	request_id = g_uuid_string_random();
	request = g_byte_array_new();
	result = sakura_agent_request_encoded_mutation(
		app, request_id, request,
		sakura_control_encode_delete_task_request(request_id, task_id, request),
		"delete task", error);
	g_byte_array_unref(request);
	g_free(request_id);
	return result;
}


gboolean
sakura_agent_create_terminal(SakuraApp *app, const gchar *requested_terminal_id,
	                           const gchar *page_id, const gchar *group_id,
	                           const gchar *task_id,
	                           const gchar *cwd, guint cols, guint rows,
	                           gchar **created_terminal_id,
	                           GError **error)
{
	GByteArray *request;
	gchar *request_id;
	gboolean result;

	if (created_terminal_id != NULL)
		*created_terminal_id = NULL;
	if (app == NULL || app->agent_socket_path == NULL)
		return FALSE;
	request_id = g_uuid_string_random();
	request = g_byte_array_new();
	if (!sakura_control_encode_create_terminal_request_with_page(
			request_id, requested_terminal_id, page_id, group_id, task_id, cwd,
			cols, rows, request)) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "could not encode create terminal request");
		result = FALSE;
	} else {
		result = sakura_agent_request_accepted(app, request_id, request,
		                                      created_terminal_id, error);
	}
	g_byte_array_unref(request);
	g_free(request_id);
	return result;
}


gboolean
sakura_agent_restart_terminal(SakuraApp *app, const gchar *terminal_id,
	                            const gchar *page_id, const gchar *group_id,
	                            const gchar *task_id,
	                            const gchar *cwd, guint cols, guint rows,
	                            SakuraTabKind kind,
	                            const gchar *resume_session_id,
	                            const gchar *reasoning_effort,
	                            const gchar *tracking_token,
	                            GError **error)
{
	GByteArray *request;
	gchar *request_id;
	gchar *accepted_id = NULL;
	gboolean result;

	if (app == NULL || app->agent_socket_path == NULL ||
	    terminal_id == NULL || terminal_id[0] == '\0')
		return FALSE;
	request_id = g_uuid_string_random();
	request = g_byte_array_new();
	if (!sakura_control_encode_restart_terminal_request_with_spec(
			request_id, terminal_id, page_id, group_id, task_id, cwd, cols, rows,
			kind, resume_session_id, reasoning_effort, tracking_token,
			request)) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "could not encode restart terminal request");
		result = FALSE;
	} else {
		result = sakura_agent_request_accepted(app, request_id, request,
		                                      &accepted_id, error);
	}
	g_byte_array_unref(request);
	g_free(request_id);
	g_free(accepted_id);
	return result;
}


gboolean
sakura_agent_attach_terminal(SakuraApp *app, const gchar *terminal_id,
	                           guint cols, guint rows, guint8 **replay_data,
	                           gsize *replay_length, guint *attached_cols,
	                           guint *attached_rows, guint *status,
	                           GError **error)
{
	GByteArray *request;
	gchar *request_id;
	gboolean encoded;
	gboolean result;

	request_id = g_uuid_string_random();
	request = g_byte_array_new();
	encoded = sakura_control_encode_attach_terminal_request(
		request_id, terminal_id, cols, rows, request);
	if (!encoded) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "could not encode attach terminal request");
		result = FALSE;
	} else {
		result = sakura_agent_request_attached(
			app, request_id, request, replay_data, replay_length,
			NULL, NULL,
			attached_cols, attached_rows, status, error);
	}
	g_byte_array_unref(request);
	g_free(request_id);
	return result;
}


gboolean
sakura_agent_attach_terminal_after_offset(
	SakuraApp *app, const gchar *terminal_id, guint cols, guint rows,
	guint64 after_output_offset, guint8 **replay_data, gsize *replay_length,
	guint64 *replay_start_offset, guint64 *replay_end_offset,
	guint *attached_cols, guint *attached_rows, guint *status, GError **error)
{
	GByteArray *request;
	gchar *request_id;
	gboolean encoded;
	gboolean result;

	request_id = g_uuid_string_random();
	request = g_byte_array_new();
	encoded = sakura_control_encode_attach_terminal_request_after_offset(
		request_id, terminal_id, cols, rows, after_output_offset, request);
	if (!encoded) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "could not encode attach terminal request");
		result = FALSE;
	} else {
		result = sakura_agent_request_attached(
			app, request_id, request, replay_data, replay_length,
			replay_start_offset, replay_end_offset, attached_cols,
			attached_rows, status, error);
	}
	g_byte_array_unref(request);
	g_free(request_id);
	return result;
}


gboolean
sakura_agent_attach_terminal_from_oldest(
	SakuraApp *app, const gchar *terminal_id, guint cols, guint rows,
	guint8 **replay_data, gsize *replay_length, guint64 *replay_start_offset,
	guint64 *replay_end_offset, guint *attached_cols, guint *attached_rows,
	guint *status, GError **error)
{
	GByteArray *request;
	gchar *request_id;
	gboolean encoded;
	gboolean result;

	request_id = g_uuid_string_random();
	request = g_byte_array_new();
	encoded = sakura_control_encode_attach_terminal_request(
		request_id, terminal_id, cols, rows, request);
	if (!encoded) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "could not encode attach terminal request");
		result = FALSE;
	} else {
		result = sakura_agent_request_attached(
			app, request_id, request, replay_data, replay_length,
			replay_start_offset, replay_end_offset, attached_cols,
			attached_rows, status, error);
	}
	g_byte_array_unref(request);
	g_free(request_id);
	return result;
}


typedef struct {
	SakuraApp *app;
	gchar *terminal_id;
	gchar *page_id;
	gchar *group_id;
	gchar *task_id;
	gchar *cwd;
	guint cols;
	guint rows;
	SakuraTabKind kind;
	gchar *resume_session_id;
	gchar *reasoning_effort;
	gchar *tracking_token;
	SakuraAgentTerminalStartCallback callback;
	gpointer callback_data;
} SakuraAgentTerminalStartJob;

typedef struct {
	SakuraAgentTerminalStartResult *result;
	SakuraAgentTerminalStartCallback callback;
	gpointer callback_data;
} SakuraAgentTerminalStartCompletion;


void
sakura_agent_terminal_start_result_free(SakuraAgentTerminalStartResult *result)
{
	if (result == NULL)
		return;
	g_free(result->requested_terminal_id);
	g_free(result->created_terminal_id);
	g_free(result->replay_data);
	g_clear_error(&result->error);
	g_free(result);
}


static void
sakura_agent_terminal_start_job_free(SakuraAgentTerminalStartJob *job)
{
	if (job == NULL)
		return;
	g_free(job->terminal_id);
	g_free(job->page_id);
	g_free(job->group_id);
	g_free(job->task_id);
	g_free(job->cwd);
	g_free(job->resume_session_id);
	g_free(job->reasoning_effort);
	g_free(job->tracking_token);
	g_free(job);
}


static void
sakura_agent_terminal_start_cleanup_remote(
	SakuraAgentTerminalStartResult *result)
{
	const gchar *terminal_id;

	if (result == NULL || result->app == NULL || result->error != NULL)
		return;
	terminal_id = result->created_terminal_id != NULL
	            ? result->created_terminal_id : result->requested_terminal_id;
	if (terminal_id == NULL || terminal_id[0] == '\0')
		return;
	if (result->attached)
		sakura_agent_detach_terminal(result->app, terminal_id, NULL);
	else
		sakura_agent_close_terminal(result->app, terminal_id, NULL);
}


static gboolean
sakura_agent_terminal_start_deliver(gpointer data)
{
	SakuraAgentTerminalStartCompletion *completion = data;

	if (completion != NULL && completion->callback != NULL &&
	    completion->result != NULL && completion->result->app != NULL &&
	    !completion->result->app->session_shutting_down)
		completion->callback(completion->result, completion->callback_data);
	else if (completion != NULL)
		sakura_agent_terminal_start_cleanup_remote(completion->result);
	return G_SOURCE_REMOVE;
}


static void
sakura_agent_terminal_start_completion_free(
	SakuraAgentTerminalStartCompletion *completion)
{
	if (completion == NULL)
		return;
	sakura_agent_terminal_start_result_free(completion->result);
	g_free(completion);
}


static void
sakura_agent_terminal_start_worker(gpointer data, gpointer user_data)
{
	SakuraAgentTerminalStartJob *job = data;
	SakuraAgentTerminalStartResult *result;
	GError *error = NULL;
	SakuraAgentTerminalStartCompletion *completion;

	(void)user_data;
	result = g_new0(SakuraAgentTerminalStartResult, 1);
	result->app = job->app;
	result->requested_terminal_id = g_strdup(job->terminal_id);
	result->attached = sakura_agent_attach_terminal_from_oldest(
		job->app, job->terminal_id, job->cols, job->rows,
		&result->replay_data, &result->replay_length,
		&result->replay_start_offset, &result->replay_end_offset,
		&result->attached_cols, &result->attached_rows,
		&result->attached_status, &error);
	if (!result->attached) {
		g_clear_error(&error);
		if (!sakura_agent_restart_terminal(
				job->app, job->terminal_id, job->page_id, job->group_id,
				job->task_id, job->cwd, job->cols, job->rows, job->kind,
				job->resume_session_id, job->reasoning_effort,
				job->tracking_token, &error))
			result->error = g_steal_pointer(&error);
		else
			result->created_terminal_id = g_strdup(job->terminal_id);
	}
	g_clear_error(&error);

	if (job->app->session_shutting_down) {
		sakura_agent_terminal_start_cleanup_remote(result);
		sakura_agent_terminal_start_result_free(result);
		sakura_agent_terminal_start_job_free(job);
		return;
	}
	completion = g_new0(SakuraAgentTerminalStartCompletion, 1);
	completion->result = result;
	completion->callback = job->callback;
	completion->callback_data = job->callback_data;
	g_main_context_invoke_full(NULL, G_PRIORITY_DEFAULT,
	                           sakura_agent_terminal_start_deliver,
	                           completion,
	                           (GDestroyNotify)sakura_agent_terminal_start_completion_free);
	sakura_agent_terminal_start_job_free(job);
}


gboolean
sakura_agent_start_terminal_async(
	SakuraApp *app, const gchar *terminal_id, const gchar *page_id,
	const gchar *group_id,
	const gchar *task_id, const gchar *cwd, guint cols, guint rows,
	SakuraTabKind kind, const gchar *resume_session_id,
	const gchar *reasoning_effort, const gchar *tracking_token,
	SakuraAgentTerminalStartCallback callback, gpointer data, GError **error)
{
	SakuraAgentTerminalStartJob *job;

	if (app == NULL || app->agent_socket_path == NULL ||
	    terminal_id == NULL || terminal_id[0] == '\0' || callback == NULL) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "invalid asynchronous terminal start request");
		return FALSE;
	}
	if (app->agent_terminal_start_pool == NULL) {
		app->agent_terminal_start_stopping = FALSE;
		app->agent_terminal_start_pool = g_thread_pool_new(
			sakura_agent_terminal_start_worker, NULL, 4, FALSE, error);
		if (app->agent_terminal_start_pool == NULL)
			return FALSE;
	}
	if (app->agent_terminal_start_stopping) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_CLOSED,
		                    "agent terminal start pool is stopping");
		return FALSE;
	}
	job = g_new0(SakuraAgentTerminalStartJob, 1);
	job->app = app;
	job->terminal_id = g_strdup(terminal_id);
	job->page_id = g_strdup(page_id);
	job->group_id = g_strdup(group_id != NULL ? group_id : "root");
	job->task_id = g_strdup(task_id != NULL ? task_id : "root");
	job->cwd = g_strdup(cwd);
	job->cols = cols;
	job->rows = rows;
	job->kind = kind;
	job->resume_session_id = g_strdup(resume_session_id);
	job->reasoning_effort = g_strdup(reasoning_effort);
	job->tracking_token = g_strdup(tracking_token);
	job->callback = callback;
	job->callback_data = data;
	if (!g_thread_pool_push(app->agent_terminal_start_pool, job, error)) {
		sakura_agent_terminal_start_job_free(job);
		return FALSE;
	}
	return TRUE;
}


gboolean
sakura_agent_terminal_input(SakuraApp *app, const gchar *terminal_id,
	                          const guint8 *data, gsize data_length,
	                          GError **error)
{
	SakuraAgentCommand *command;

	if (terminal_id == NULL || terminal_id[0] == '\0' ||
	    (data == NULL && data_length != 0)) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "invalid terminal input command");
		return FALSE;
	}
	command = g_new0(SakuraAgentCommand, 1);
	command->kind = SAKURA_AGENT_COMMAND_INPUT;
	command->terminal_id = g_strdup(terminal_id);
	command->input_length = data_length;
	if (data_length != 0) {
		command->input_data = g_malloc(data_length);
		memcpy(command->input_data, data, data_length);
	}
	return sakura_agent_enqueue_command(app, command, error);
}


gboolean
sakura_agent_terminal_resize(SakuraApp *app, const gchar *terminal_id,
	                           guint cols, guint rows, GError **error)
{
	SakuraAgentCommand *command;

	if (terminal_id == NULL || terminal_id[0] == '\0' || cols == 0 || rows == 0) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "invalid terminal resize command");
		return FALSE;
	}
	command = g_new0(SakuraAgentCommand, 1);
	command->kind = SAKURA_AGENT_COMMAND_RESIZE;
	command->terminal_id = g_strdup(terminal_id);
	command->cols = cols;
	command->rows = rows;
	return sakura_agent_enqueue_command(app, command, error);
}


gboolean
sakura_agent_close_terminal(SakuraApp *app, const gchar *terminal_id,
	                          GError **error)
{
	SakuraAgentCommand *command;

	if (terminal_id == NULL || terminal_id[0] == '\0') {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "invalid terminal close command");
		return FALSE;
	}
	command = g_new0(SakuraAgentCommand, 1);
	command->kind = SAKURA_AGENT_COMMAND_CLOSE;
	command->terminal_id = g_strdup(terminal_id);
	return sakura_agent_enqueue_command(app, command, error);
}


gboolean
sakura_agent_detach_terminal(SakuraApp *app, const gchar *terminal_id,
	                           GError **error)
{
	SakuraAgentCommand *command;

	if (terminal_id == NULL || terminal_id[0] == '\0') {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "invalid terminal detach command");
		return FALSE;
	}
	command = g_new0(SakuraAgentCommand, 1);
	command->kind = SAKURA_AGENT_COMMAND_DETACH;
	command->terminal_id = g_strdup(terminal_id);
	return sakura_agent_enqueue_command(app, command, error);
}


static gchar *
sakura_agent_binary_path(void)
{
	gchar *path = NULL;

#ifdef SAKURA_AGENT_BUILD_PATH
	if (g_file_test(SAKURA_AGENT_BUILD_PATH, G_FILE_TEST_IS_EXECUTABLE))
		return g_strdup(SAKURA_AGENT_BUILD_PATH);
#endif
	path = g_find_program_in_path("sakura-agent");
	return path;
}


static gboolean sakura_agent_restart_cb(gpointer data);


static void
sakura_agent_mark_terminals_lost(SakuraApp *app)
{
	if (app == NULL || app->workspace == NULL || app->workspace->panes == NULL)
		return;
	for (guint index = 0; index < app->workspace->panes->len; index++) {
		SakuraTab *tab = g_ptr_array_index(app->workspace->panes, index);

		if (tab == NULL || !tab->agent_backed)
			continue;
		sakura_tab_agent_status(tab, SAKURA_TERMINAL_EXITED,
		                       "embedded agent exited");
		tab->agent_backed = FALSE;
		tab->agent_terminal_lost = TRUE;
		sakura_tab_set_status(tab, SAKURA_TAB_STATUS_ERROR, TRUE);
	}
}


static void
sakura_agent_recover_terminals(SakuraApp *app)
{
	if (app == NULL || app->workspace == NULL || app->workspace->panes == NULL)
		return;
	for (guint index = 0; index < app->workspace->panes->len; index++) {
		SakuraTab *tab = g_ptr_array_index(app->workspace->panes, index);

		if (tab == NULL || !tab->agent_terminal_lost)
			continue;
		(void)sakura_tab_restart_agent_terminal(tab);
	}
}


static GSubprocess *
sakura_agent_spawn_process(SakuraApp *app, const gchar *socket_path,
	                         GError **error)
{
	GSubprocess *process;
	gchar *binary;
	gchar *workspace_path = NULL;

	binary = sakura_agent_binary_path();
	if (binary == NULL) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		                    "sakura-agent was not found");
		return NULL;
	}
	if (app->sessionfile != NULL && app->sessionfile[0] != '\0') {
		workspace_path = g_strdup_printf("%s.workspace", app->sessionfile);
		process = g_subprocess_new(
			G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
			G_SUBPROCESS_FLAGS_STDERR_SILENCE,
			error, binary, "--socket", socket_path, "--workspace-id",
			app->workspace_id, "--session", app->sessionfile,
			"--workspace-file", workspace_path, NULL);
	} else
		process = g_subprocess_new(
			G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
			G_SUBPROCESS_FLAGS_STDERR_SILENCE,
			error, binary, "--socket", socket_path, "--workspace-id",
			app->workspace_id, NULL);
	g_free(binary);
	g_free(workspace_path);
	return process;
}


static gboolean
sakura_agent_wait_ready(const gchar *socket_path, const gchar *workspace_id,
	                     SakuraSessionSnapshot **snapshot,
	                     GError **error)
{
	gint64 deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;

	while (g_get_monotonic_time() < deadline) {
		if (sakura_agent_request_snapshot(socket_path, workspace_id, snapshot,
		                                  error))
			return TRUE;
		g_clear_error(error);
		g_usleep(MIN((gint64)10 * 1000,
		             deadline - g_get_monotonic_time()));
	}
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
	                    "sakura-agent did not become ready");
	return FALSE;
}


static void
sakura_agent_process_wait_done(GObject *source_object, GAsyncResult *result,
	                              gpointer data)
{
	SakuraApp *app = data;
	GSubprocess *process = G_SUBPROCESS(source_object);
	GError *error = NULL;

	if (!g_subprocess_wait_finish(process, result, &error))
		g_clear_error(&error);
	if (app == NULL || app->agent_process != process ||
	    app->agent_supervisor_stopping || app->session_shutting_down)
		return;
	g_clear_object(&app->agent_process);
	g_warning("Embedded sakura-agent exited; restarting it");
	sakura_agent_mark_terminals_lost(app);
	sakura_agent_command_stop(app);
	sakura_agent_event_stop(app);
	if (app->agent_restart_source_id == 0)
		app->agent_restart_source_id = g_timeout_add(
			250, sakura_agent_restart_cb, app);
}


static gboolean
sakura_agent_launch_owned(SakuraApp *app, const gchar *socket_path,
	                         SakuraSessionSnapshot **snapshot,
	                         GError **error)
{
	GSubprocess *process;

	process = sakura_agent_spawn_process(app, socket_path, error);
	if (process == NULL)
		return FALSE;
	if (!sakura_agent_wait_ready(socket_path, app->workspace_id, snapshot,
	                             error)) {
		g_subprocess_force_exit(process);
		g_subprocess_wait(process, NULL, NULL);
		g_object_unref(process);
		return FALSE;
	}
	app->agent_process = process;
	g_subprocess_wait_async(process, NULL, sakura_agent_process_wait_done, app);
	return TRUE;
}


static gboolean
sakura_agent_restart_cb(gpointer data)
{
	SakuraApp *app = data;
	GError *error = NULL;
	SakuraSessionSnapshot *agent_snapshot = NULL;

	if (app == NULL || app->session_shutting_down ||
	    app->agent_supervisor_stopping || app->agent_socket_path == NULL) {
		if (app != NULL)
			app->agent_restart_source_id = 0;
		return G_SOURCE_REMOVE;
	}
	app->agent_restart_source_id = 0;
	if (!sakura_agent_launch_owned(app, app->agent_socket_path,
	                               &agent_snapshot, &error)) {
		g_warning("Could not restart embedded sakura-agent: %s",
		          error != NULL ? error->message : "unknown error");
		g_clear_error(&error);
		app->agent_restart_source_id = g_timeout_add(
			1000, sakura_agent_restart_cb, app);
		return G_SOURCE_REMOVE;
	}
	sakura_session_snapshot_free(app->agent_pending_snapshot);
	app->agent_pending_snapshot = agent_snapshot;
	agent_snapshot = NULL;
	sakura_agent_apply_pending_snapshot(app);
	sakura_agent_subscribe_start(app);
	sakura_agent_command_start(app);
	sakura_agent_recover_terminals(app);
	return G_SOURCE_REMOVE;
}


gboolean
sakura_agent_start(SakuraApp *app)
{
	GError *error = NULL;
	gchar *socket_path;
	SakuraSessionSnapshot *agent_snapshot = NULL;

	if (app == NULL)
		return FALSE;
	sakura_agent_workspace_revision_init(app);
	if (app->workspace_id == NULL || app->workspace_id[0] == '\0')
		app->workspace_id = g_uuid_string_random();
	app->agent_supervisor_stopping = FALSE;
	if (app->agent_socket_path != NULL) {
		sakura_agent_command_start(app);
		return TRUE;
	}
	socket_path = sakura_agent_socket_path_new(app);
	if (socket_path == NULL) {
		g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                    "workspace identity is required for agent startup");
		g_warning("Could not start sakura-agent: %s", error->message);
		g_clear_error(&error);
		return FALSE;
	}
	if (sakura_agent_request_snapshot(socket_path, app->workspace_id,
	                                  &agent_snapshot, &error)) {
		app->agent_socket_path = socket_path;
		sakura_session_snapshot_free(app->agent_pending_snapshot);
		app->agent_pending_snapshot = agent_snapshot;
		agent_snapshot = NULL;
		sakura_agent_apply_pending_snapshot(app);
		sakura_agent_subscribe_start(app);
		sakura_agent_command_start(app);
		return TRUE;
	}
	g_clear_error(&error);

	if (!sakura_agent_launch_owned(app, socket_path, &agent_snapshot, &error)) {
		g_warning("Could not start sakura-agent: %s",
		          error != NULL ? error->message : "unknown error");
		g_clear_error(&error);
		g_free(socket_path);
		return FALSE;
	}
	app->agent_socket_path = socket_path;
	sakura_session_snapshot_free(app->agent_pending_snapshot);
	app->agent_pending_snapshot = agent_snapshot;
	agent_snapshot = NULL;
	sakura_agent_apply_pending_snapshot(app);
	sakura_agent_subscribe_start(app);
	sakura_agent_command_start(app);
	return TRUE;
}


void
sakura_agent_stop(SakuraApp *app)
{
	if (app == NULL)
		return;
	app->agent_supervisor_stopping = TRUE;
	if (app->agent_restart_source_id != 0) {
		g_source_remove(app->agent_restart_source_id);
		app->agent_restart_source_id = 0;
	}
	if (app->agent_terminal_start_pool != NULL) {
		app->agent_terminal_start_stopping = TRUE;
		g_thread_pool_free(app->agent_terminal_start_pool, FALSE, TRUE);
		app->agent_terminal_start_pool = NULL;
	}
	sakura_agent_command_stop(app);
	sakura_agent_event_stop(app);
	if (app->agent_process != NULL) {
		GError *error = NULL;

		g_subprocess_force_exit(app->agent_process);
		if (!g_subprocess_wait(app->agent_process, NULL, &error))
			g_clear_error(&error);
		g_clear_object(&app->agent_process);
	}
	g_clear_pointer(&app->agent_socket_path, g_free);
	if (app->agent_revision_mutex_initialized) {
		g_mutex_clear(&app->agent_revision_mutex);
		app->agent_revision_mutex_initialized = FALSE;
	}
	if (app->agent_workspace_mutation_mutex_initialized) {
		g_mutex_clear(&app->agent_workspace_mutation_mutex);
		app->agent_workspace_mutation_mutex_initialized = FALSE;
	}
}
