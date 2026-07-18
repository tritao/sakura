#include <libintl.h>

#include "sakura-private.h"

#define _(String) gettext(String)


static void
sakura_startup_options_clear(SakuraStartupOptions *options)
{
	if (options == NULL)
		return;
	g_clear_pointer(&options->codex_session, g_free);
	options->new_session = FALSE;
	options->new_window = FALSE;
	options->ntabs = 0;
	options->fullscreen = FALSE;
}


static void
sakura_startup_set_status(const gchar *status)
{
	if (sakura.startup.status_label == NULL)
		return;
	gtk_label_set_text(GTK_LABEL(sakura.startup.status_label),
	                   status != NULL ? status : "");
}


static void
sakura_startup_maybe_finish_loading(SakuraApp *app)
{
	if (app == NULL || app->startup.phase != SAKURA_STARTUP_READY ||
	    app->startup.pending_terminal_starts != 0)
		return;
	if (app->startup.spinner != NULL) {
		gtk_spinner_stop(GTK_SPINNER(app->startup.spinner));
		gtk_widget_hide(app->startup.spinner);
	}
	if (app->startup.overlay != NULL)
		gtk_widget_hide(app->startup.overlay);
}


void
sakura_startup_terminal_start_pending(SakuraApp *app, gboolean pending)
{
	if (app == NULL)
		return;
	if (pending)
		app->startup.pending_terminal_starts++;
	else if (app->startup.pending_terminal_starts > 0)
		app->startup.pending_terminal_starts--;
	sakura_startup_maybe_finish_loading(app);
}


static void
sakura_startup_finish(void)
{
	sakura.session_restoring = FALSE;
	sakura_agent_apply_pending_snapshot(&sakura);
	sakura.session_ready = TRUE;
	if (!sakura.startup.options.new_window &&
	    !sakura.startup.preserve_failed_session) {
		sakura_session_mark_dirty();
		sakura_session_flush();
	}
	if (!sakura.startup.options.fullscreen)
		gtk_window_maximize(GTK_WINDOW(sakura.main_window));
	sakura.first_run = FALSE;
	sakura_sanitize_working_directory();
	sakura.startup.phase = SAKURA_STARTUP_READY;
	sakura_startup_maybe_finish_loading(&sakura);
	sakura.startup.restore_source_id = 0;
	if (sakura.startup.finished_callback != NULL)
		sakura.startup.finished_callback(sakura.startup.finished_data);
}


static void
sakura_startup_restore_complete(gboolean success, gpointer data)
{
	(void)data;
	if (!success) {
		if (sakura.sessionfile != NULL &&
		    g_file_test(sakura.sessionfile, G_FILE_TEST_IS_REGULAR)) {
			sakura.startup.preserve_failed_session = TRUE;
			sakura.session_restore_failed = TRUE;
			g_debug("Could not restore the saved session; preserving %s",
			          sakura.sessionfile);
		}
		for (guint index = 0; index < sakura.startup.options.ntabs; index++)
			sakura_add_tab();
	}
	sakura_startup_set_status(_("Starting saved terminal sessions…"));
	sakura_startup_finish();
}


static gboolean
sakura_startup_restore_workspace_cb(gpointer data)
{
	(void)data;
	if (sakura.session_shutting_down) {
		sakura.startup.restore_source_id = 0;
		return G_SOURCE_REMOVE;
	}

	sakura.startup.phase = SAKURA_STARTUP_RESTORING;
	sakura_startup_set_status(_("Restoring saved workspace…"));
	if (sakura.startup.options.codex_session != NULL) {
		sakura_add_tab_with_options(NULL, NULL, NULL, FALSE,
		                            SAKURA_TAB_CODEX, SAKURA_TOOL_NONE,
		                            sakura.startup.options.codex_session,
		                            NULL, NULL, NULL, NULL, -1);
		sakura_startup_restore_complete(TRUE, NULL);
		return G_SOURCE_REMOVE;
	}
	if (sakura.startup.options.new_session ||
	    sakura.startup.options.new_window ||
	    sakura.startup.options.ntabs > 1) {
		for (guint index = 0; index < sakura.startup.options.ntabs; index++)
			sakura_add_tab();
		sakura_startup_restore_complete(TRUE, NULL);
		return G_SOURCE_REMOVE;
	}
	if (!sakura_workspace_restore_snapshot_async(
			sakura.session_snapshot, sakura_startup_restore_complete, NULL))
		sakura_startup_restore_complete(FALSE, NULL);
	sakura.startup.restore_source_id = 0;
	return G_SOURCE_REMOVE;
}


static gboolean
sakura_startup_start_agent_cb(gpointer data)
{
	(void)data;
	if (sakura.session_shutting_down) {
		sakura.startup.restore_source_id = 0;
		return G_SOURCE_REMOVE;
	}
	sakura_startup_set_status(_("Starting workspace agent…"));
	if (!sakura_agent_start(&sakura))
		g_debug("Local sakura-agent is unavailable; continuing with the desktop model");
	sakura.startup.restore_source_id = g_timeout_add(
		50, sakura_startup_restore_workspace_cb, NULL);
	return G_SOURCE_REMOVE;
}


static gboolean
sakura_startup_restore_cb(gpointer data)
{
	(void)data;
	if (sakura.session_shutting_down) {
		sakura.startup.restore_source_id = 0;
		return G_SOURCE_REMOVE;
	}
	sakura_startup_set_status(_("Starting workspace agent…"));
	sakura.startup.restore_source_id = g_timeout_add(
		50, sakura_startup_start_agent_cb, NULL);
	return G_SOURCE_REMOVE;
}


void
sakura_startup_init_ui(void)
{
	GtkWidget *window_overlay;
	GtkWidget *status_frame;
	GtkWidget *status_box;
	GtkWidget *status_label;

	window_overlay = gtk_overlay_new();
	gtk_container_add(GTK_CONTAINER(window_overlay), sakura.sidebar_paned);
	status_frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(status_frame), GTK_SHADOW_ETCHED_IN);
	status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_container_set_border_width(GTK_CONTAINER(status_box), 14);
	gtk_container_add(GTK_CONTAINER(status_frame), status_box);
	gtk_widget_set_halign(status_frame, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(status_frame, GTK_ALIGN_CENTER);
	gtk_overlay_add_overlay(GTK_OVERLAY(window_overlay), status_frame);
	status_label = gtk_label_new(_("Preparing saved workspace…"));
	gtk_box_pack_start(GTK_BOX(status_box), status_label, FALSE, FALSE, 0);
	sakura.startup.spinner = gtk_spinner_new();
	gtk_box_pack_start(GTK_BOX(status_box), sakura.startup.spinner,
	                   FALSE, FALSE, 0);
	sakura.startup.status_label = status_label;
	sakura.startup.overlay = status_frame;
	sakura.startup.phase = SAKURA_STARTUP_IDLE;
	gtk_widget_show_all(window_overlay);
	gtk_spinner_start(GTK_SPINNER(sakura.startup.spinner));
	gtk_container_add(GTK_CONTAINER(sakura.main_window), window_overlay);
}


void
sakura_startup_begin(const SakuraStartupOptions *options,
                     SakuraStartupFinishedCallback callback,
                     gpointer data)
{
	if (options == NULL || sakura.startup.phase != SAKURA_STARTUP_IDLE)
		return;
	sakura.startup.options = *options;
	sakura.startup.options.codex_session = g_strdup(options->codex_session);
	sakura.startup.finished_callback = callback;
	sakura.startup.finished_data = data;
	sakura.startup.preserve_failed_session = FALSE;
	sakura.startup.pending_terminal_starts = 0;
	sakura.startup.phase = SAKURA_STARTUP_SCHEDULED;
	sakura.session_restoring = TRUE;
	gtk_widget_show(sakura.main_window);
	/* Let GTK paint the loading surface before starting agent/session work. */
	sakura.startup.restore_source_id = g_timeout_add(
		50, sakura_startup_restore_cb, NULL);
}


void
sakura_startup_stop(void)
{
	if (sakura.startup.restore_source_id != 0) {
		g_source_remove(sakura.startup.restore_source_id);
		sakura.startup.restore_source_id = 0;
	}
	sakura_workspace_restore_snapshot_async_cancel();
	sakura_startup_options_clear(&sakura.startup.options);
	sakura.startup.finished_callback = NULL;
	sakura.startup.finished_data = NULL;
	sakura.startup.phase = SAKURA_STARTUP_IDLE;
}
