#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>

#include <libintl.h>

#include <glib/gstdio.h>

#include "sakura-private.h"

#define _(String) gettext(String)


#ifndef SAKURA_CORE_TEST
static gboolean
sakura_session_save_timeout_cb(gpointer data)
{
	SakuraApp *app = data != NULL ? data : &sakura;

	app->session_save_source_id = 0;
	sakura_session_flush();
	return G_SOURCE_REMOVE;
}


void
sakura_session_accept_changes(void)
{
	/* A failed restore is protected until the user explicitly changes the
	 * workspace. Automatic metadata updates must not overwrite the preserved
	 * session while the fallback workspace is still on screen. */
	sakura.session_restore_failed = FALSE;
}


gboolean
sakura_session_confirm_new_instance(SakuraApp *app)
{
	GtkWidget *dialog;
	gint response;

	(void)app;
	dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
	                                GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
	                                "%s", _("This Sakura session is already open."));
	gtk_window_set_title(GTK_WINDOW(dialog), _("Sakura session already open"));
	gtk_message_dialog_format_secondary_text(
		GTK_MESSAGE_DIALOG(dialog),
		_("Choose Exit to keep using the existing session, or launch a new persistent instance with its own saved workspace."));
	gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Exit"), GTK_RESPONSE_CANCEL);
	gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Launch new instance"), GTK_RESPONSE_ACCEPT);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
	response = gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);

	return response == GTK_RESPONSE_ACCEPT;
}


gboolean
sakura_session_start_new_instance(SakuraApp *app)
{
	gchar *base_sessionfile;
	gint64 stamp;
	guint attempt;

	if (app == NULL || app->sessionfile == NULL)
		return FALSE;
	base_sessionfile = g_strdup(app->sessionfile);
	stamp = g_get_real_time();
	for (attempt = 0; attempt < 100; attempt++) {
		gchar *candidate;
		int lock_result;

		candidate = g_strdup_printf("%s.instance-%" G_GINT64_FORMAT "-%u",
		                            base_sessionfile, stamp, attempt);
		if (g_file_test(candidate, G_FILE_TEST_EXISTS)) {
			g_free(candidate);
			continue;
		}

		lock_result = sakura_session_lock_acquire(app, candidate);
		if (lock_result == 1) {
			g_free(app->sessionfile);
			app->sessionfile = candidate;
			g_free(base_sessionfile);
			return TRUE;
		}
		g_free(candidate);
		if (lock_result < 0)
			break;
	}

	g_free(base_sessionfile);
	return FALSE;
}


void
sakura_session_mark_dirty(void)
{
	sakura.session_dirty = TRUE;
	if (sakura.sessionfile == NULL || sakura.session_new_window ||
	    !sakura.session_ready || sakura.session_restoring || sakura.dont_save ||
	    sakura.session_restore_failed || sakura.session_shutting_down)
		return;

	if (sakura.session_save_source_id == 0)
		sakura.session_save_source_id = g_timeout_add(500,
	                                               sakura_session_save_timeout_cb,
	                                               &sakura);
}


void
sakura_session_flush(void)
{
	SakuraSessionSnapshot *snapshot;
	gboolean saved;

	if (sakura.session_save_source_id != 0) {
		g_source_remove(sakura.session_save_source_id);
		sakura.session_save_source_id = 0;
	}
	if (!sakura.session_dirty)
		return;
	if (sakura.sessionfile == NULL || sakura.session_new_window || sakura.dont_save ||
	    sakura.session_shutting_down || sakura.session_restoring ||
	    sakura.session_restore_failed ||
	    sakura.sidebar_model == NULL)
		return;

	snapshot = sakura_workspace_model_snapshot_new(
		sakura.workspace, sakura.sidebar_visible,
		sakura.sidebar_paned != NULL
		? gtk_paned_get_position(GTK_PANED(sakura.sidebar_paned))
		: sakura.sidebar_width);
	if (snapshot != NULL) {
		g_free(snapshot->workspace_id);
		snapshot->workspace_id = g_strdup(sakura.workspace_id);
		snapshot->show_archived = sakura.show_archived;
		sakura_sidebar_capture_expansion(snapshot);
	}
	saved = sakura_session_write_snapshot(&sakura, snapshot);
	sakura_session_snapshot_free(snapshot);
	if (saved)
		sakura.session_dirty = FALSE;
}
#endif

int
sakura_session_lock_acquire(SakuraApp *app, const gchar *sessionfile)
{
	gchar *lock_path;
	int fd;

	if (app == NULL || sessionfile == NULL || sessionfile[0] == '\0')
		return -1;

	lock_path = g_strdup_printf("%s.lock", sessionfile);
	fd = g_open(lock_path, O_CREAT | O_RDWR, 0600);
	if (fd < 0) {
		g_warning("Could not open session lock %s: %s", lock_path, g_strerror(errno));
		g_free(lock_path);
		return -1;
	}
	if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
		g_warning("Could not mark session lock close-on-exec: %s", g_strerror(errno));
		close(fd);
		g_free(lock_path);
		return -1;
	}

	if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
		app->session_lock_fd = fd;
		app->session_lock_path = lock_path;
		return 1;
	}

	if (errno == EWOULDBLOCK || errno == EAGAIN) {
		close(fd);
		g_free(lock_path);
		return 0;
	}

	g_warning("Could not lock session %s: %s", sessionfile, g_strerror(errno));
	close(fd);
	g_free(lock_path);
	return -1;
}


gboolean
sakura_session_backup_existing(const gchar *sessionfile)
{
	gchar *backup_file;
	gchar *contents = NULL;
	gsize length = 0;
	GError *error = NULL;

	if (sessionfile == NULL ||
	    !g_file_test(sessionfile, G_FILE_TEST_IS_REGULAR))
		return TRUE;

	if (!g_file_get_contents(sessionfile, &contents, &length, &error)) {
		g_warning("Could not back up session file: %s",
		          error != NULL ? error->message : "unknown error");
		g_clear_error(&error);
		return FALSE;
	}

	backup_file = g_strdup_printf("%s.bak", sessionfile);
	if (!g_file_set_contents(backup_file, contents, length, &error)) {
		g_warning("Could not back up session file: %s",
		          error != NULL ? error->message : "unknown error");
		g_clear_error(&error);
		g_free(backup_file);
		g_free(contents);
		return FALSE;
	}
	if (chmod(backup_file, 0600) != 0) {
		g_warning("Could not secure session backup: %s", g_strerror(errno));
		g_free(backup_file);
		g_free(contents);
		return FALSE;
	}

	g_free(backup_file);
	g_free(contents);
	return TRUE;
}


gboolean
sakura_session_write_snapshot(SakuraApp *app,
                              const SakuraSessionSnapshot *snapshot)
{
	GKeyFile *key_file;
	GError *error = NULL;
	gchar *data = NULL;
	gchar *temporary_file = NULL;
	gsize data_length = 0;
	gboolean saved = FALSE;

	if (app == NULL || snapshot == NULL || app->sessionfile == NULL)
		return FALSE;

	key_file = g_key_file_new();
	sakura_session_snapshot_save(snapshot, key_file);
	data = g_key_file_to_data(key_file, &data_length, &error);
	if (data == NULL) {
		g_warning("Could not serialize session: %s",
		          error != NULL ? error->message : "unknown error");
		g_clear_error(&error);
		g_key_file_free(key_file);
		return FALSE;
	}

	temporary_file = g_strdup_printf("%s.tmp.%d", app->sessionfile, (int)getpid());
	if (!g_file_set_contents(temporary_file, data, data_length, &error) ||
	    chmod(temporary_file, 0600) != 0 ||
	    !sakura_session_backup_existing(app->sessionfile) ||
	    g_rename(temporary_file, app->sessionfile) != 0) {
		g_warning("Could not save session: %s",
		          error != NULL ? error->message : g_strerror(errno));
		g_clear_error(&error);
		g_remove(temporary_file);
	} else {
		saved = TRUE;
	}

	g_free(temporary_file);
	g_free(data);
	g_key_file_free(key_file);
	return saved;
}


gboolean
sakura_session_load_file(SakuraApp *app, gboolean restore_session)
{
	GError *error = NULL;
	SakuraSessionSnapshot *snapshot;

	if (app == NULL || app->sessionfile == NULL)
		return FALSE;

	g_clear_pointer(&app->session_cfg, g_key_file_free);
	app->session_cfg = g_key_file_new();
	if (!g_key_file_load_from_file(app->session_cfg, app->sessionfile, 0, &error)) {
		g_clear_pointer(&app->session_cfg, g_key_file_free);
		g_clear_error(&error);
		return FALSE;
	}
	if (!restore_session)
		return TRUE;

	snapshot = sakura_session_snapshot_new();
	if (!sakura_session_snapshot_load(app->session_cfg, snapshot, &error)) {
		g_warning("Could not parse saved session: %s",
		          error != NULL ? error->message : "unknown error");
		g_clear_error(&error);
		sakura_session_snapshot_free(snapshot);
		return FALSE;
	}

	app->sidebar_visible = snapshot->sidebar_visible;
	app->show_archived = snapshot->show_archived;
	g_free(app->workspace_id);
	app->workspace_id = g_strdup(snapshot->workspace_id);
	if (snapshot->sidebar_width >= 160 && snapshot->sidebar_width <= 500)
		app->sidebar_width = snapshot->sidebar_width;
	sakura_session_snapshot_free(app->session_snapshot);
	app->session_snapshot = snapshot;
	return TRUE;
}


void
sakura_session_prepare_bash_integration(SakuraApp *app)
{
	static const gchar bash_rc[] =
		"if [[ -z \"${SAKURA_HISTORY_PROMPT_ACTIVE:-}\" ]]; then\n"
		"    SAKURA_HISTORY_PROMPT_ACTIVE=1\n"
		"    if [[ -n \"${SAKURA_BASH_LOGIN:-}\" ]]; then\n"
		"        [[ -r /etc/profile ]] && . /etc/profile\n"
		"        if [[ -n \"${HOME:-}\" && -r \"$HOME/.bash_profile\" ]]; then\n"
		"            . \"$HOME/.bash_profile\"\n"
		"        elif [[ -n \"${HOME:-}\" && -r \"$HOME/.bash_login\" ]]; then\n"
		"            . \"$HOME/.bash_login\"\n"
		"        elif [[ -n \"${HOME:-}\" && -r \"$HOME/.profile\" ]]; then\n"
		"            . \"$HOME/.profile\"\n"
		"        fi\n"
		"    elif [[ -n \"${HOME:-}\" && -r \"$HOME/.bashrc\" ]]; then\n"
		"        . \"$HOME/.bashrc\"\n"
		"    fi\n"
		"    if [[ -n \"${SAKURA_HISTORY_FILE:-}\" && "
		"-z \"${SAKURA_DISABLE_HISTORY_INTEGRATION:-}\" ]]; then\n"
		"        HISTFILE=\"$SAKURA_HISTORY_FILE\"\n"
		"        shopt -s histappend\n"
		"        if [[ $(declare -p PROMPT_COMMAND 2>/dev/null) == "
		"\"declare -a\"* ]]; then\n"
		"            PROMPT_COMMAND=(history -a history -n \"${PROMPT_COMMAND[@]}\")\n"
		"        else\n"
		"            PROMPT_COMMAND=\"history -a; history -n${PROMPT_COMMAND:+;$PROMPT_COMMAND}\"\n"
		"        fi\n"
		"    fi\n"
		"fi\n";
	GError *error = NULL;

	if (app == NULL || app->history_dir == NULL)
		return;

	app->bash_history_rc = g_build_filename(app->history_dir, "sakura-bashrc", NULL);
	if (!g_file_set_contents(app->bash_history_rc, bash_rc, -1, &error) ||
	    chmod(app->bash_history_rc, 0600) != 0) {
		g_warning("Could not prepare Bash history integration: %s",
		          error != NULL ? error->message : g_strerror(errno));
		g_clear_error(&error);
		g_clear_pointer(&app->bash_history_rc, g_free);
	}
}
