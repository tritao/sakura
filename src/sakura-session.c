#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libintl.h>

#include <glib/gstdio.h>

#include "sakura-private.h"

#define SAKURA_SESSION_VERSION 3
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
	    sakura.session_shutting_down || sakura.session_restore_failed ||
	    sakura.sidebar_model == NULL)
		return;

	snapshot = sakura_workspace_snapshot_new();
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

static void
sakura_session_group_record_free(gpointer data)
{
	SakuraSessionGroupRecord *record = data;

	if (record == NULL)
		return;
	g_free(record->id);
	g_free(record->parent_id);
	g_free(record->title);
	g_free(record);
}


static void
sakura_session_tab_record_free(gpointer data)
{
	SakuraSessionTabRecord *record = data;

	if (record == NULL)
		return;
	g_free(record->parent_id);
	g_free(record->cwd);
	g_free(record->title);
	g_free(record->terminal_id);
	g_free(record->tool_id);
	g_free(record->tool_target);
	g_free(record->codex_session_id);
	g_free(record->codex_session_name);
	g_free(record);
}


SakuraSessionSnapshot *
sakura_session_snapshot_new(void)
{
	SakuraSessionSnapshot *snapshot = g_new0(SakuraSessionSnapshot, 1);

	snapshot->groups = g_ptr_array_new_with_free_func(sakura_session_group_record_free);
	snapshot->tabs = g_ptr_array_new_with_free_func(sakura_session_tab_record_free);
	snapshot->selected_terminal = -1;
	snapshot->active_group_id = g_strdup("root");
	snapshot->sidebar_visible = TRUE;
	snapshot->sidebar_width = 200;
	return snapshot;
}


void
sakura_session_snapshot_free(SakuraSessionSnapshot *snapshot)
{
	if (snapshot == NULL)
		return;
	g_clear_pointer(&snapshot->groups, g_ptr_array_unref);
	g_clear_pointer(&snapshot->tabs, g_ptr_array_unref);
	g_free(snapshot->selected_terminal_id);
	g_free(snapshot->active_group_id);
	g_free(snapshot);
}


static gboolean
sakura_session_error(GError **error, GKeyFileError code, gchar *message)
{
	if (error != NULL)
		g_set_error_literal(error, G_KEY_FILE_ERROR, code, message);
	g_free(message);
	return FALSE;
}


static gboolean
sakura_session_group_ids_valid(const SakuraSessionSnapshot *snapshot,
                               GError **error)
{
	GHashTable *groups;
	guint index;

	groups = g_hash_table_new(g_str_hash, g_str_equal);
	for (index = 0; index < snapshot->groups->len; index++) {
		SakuraSessionGroupRecord *group = g_ptr_array_index(snapshot->groups, index);

		if (group->id == NULL || group->id[0] == '\0' ||
		    g_strcmp0(group->id, "root") == 0 ||
		    g_hash_table_contains(groups, group->id)) {
			g_hash_table_destroy(groups);
			return sakura_session_error(error, G_KEY_FILE_ERROR_INVALID_VALUE,
		                                g_strdup_printf("invalid or duplicate group id: %s",
	                                                 group->id != NULL ? group->id : "(null)"));
		}
		g_hash_table_add(groups, group->id);
	}

	for (index = 0; index < snapshot->groups->len; index++) {
		SakuraSessionGroupRecord *group = g_ptr_array_index(snapshot->groups, index);
		GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);
		const gchar *parent = group->parent_id;

		while (parent != NULL && parent[0] != '\0' && g_strcmp0(parent, "root") != 0) {
			SakuraSessionGroupRecord *parent_group;

			if (g_hash_table_contains(seen, parent)) {
				g_hash_table_destroy(seen);
				g_hash_table_destroy(groups);
				return sakura_session_error(error, G_KEY_FILE_ERROR_INVALID_VALUE,
				                            g_strdup_printf("cycle in group parents at: %s", parent));
			}
			g_hash_table_add(seen, (gpointer)parent);
			parent_group = NULL;
			for (guint parent_index = 0; parent_index < snapshot->groups->len;
			     parent_index++) {
				SakuraSessionGroupRecord *candidate =
					g_ptr_array_index(snapshot->groups, parent_index);
				if (g_strcmp0(candidate->id, parent) == 0) {
					parent_group = candidate;
					break;
				}
			}
			if (parent_group == NULL) {
				g_hash_table_destroy(seen);
				g_hash_table_destroy(groups);
				return sakura_session_error(error, G_KEY_FILE_ERROR_INVALID_VALUE,
				                            g_strdup_printf("group points to missing parent: %s", parent));
			}
			parent = parent_group->parent_id;
		}
		g_hash_table_destroy(seen);
	}

	g_hash_table_destroy(groups);
	return TRUE;
}


static gboolean
sakura_session_terminal_ids_valid(const SakuraSessionSnapshot *snapshot,
                                  GError **error)
{
	GHashTable *ids = g_hash_table_new(g_str_hash, g_str_equal);
	GHashTable *groups = g_hash_table_new(g_str_hash, g_str_equal);
	guint index;

	for (index = 0; index < snapshot->groups->len; index++) {
		SakuraSessionGroupRecord *group = g_ptr_array_index(snapshot->groups, index);
		g_hash_table_add(groups, group->id);
	}
	for (index = 0; index < snapshot->tabs->len; index++) {
		SakuraSessionTabRecord *tab = g_ptr_array_index(snapshot->tabs, index);

		if (tab->parent_id != NULL && tab->parent_id[0] != '\0' &&
		    g_strcmp0(tab->parent_id, "root") != 0 &&
		    !g_hash_table_contains(groups, tab->parent_id)) {
			g_hash_table_destroy(ids);
			g_hash_table_destroy(groups);
			return sakura_session_error(error, G_KEY_FILE_ERROR_INVALID_VALUE,
		                                g_strdup_printf("terminal points to missing parent: %s",
	                                                 tab->parent_id));
		}
		if (tab->terminal_id != NULL && tab->terminal_id[0] != '\0') {
			if (g_hash_table_contains(ids, tab->terminal_id)) {
				g_hash_table_destroy(ids);
				g_hash_table_destroy(groups);
				return sakura_session_error(error, G_KEY_FILE_ERROR_INVALID_VALUE,
				                            g_strdup_printf("duplicate terminal id: %s", tab->terminal_id));
			}
			g_hash_table_add(ids, tab->terminal_id);
		}
	}

	g_hash_table_destroy(ids);
	g_hash_table_destroy(groups);
	return TRUE;
}


static gboolean
sakura_session_snapshot_load_into(GKeyFile *key_file,
                                   SakuraSessionSnapshot *snapshot,
                                   GError **error)
{
	gint version, group_count, terminal_count;
	gint index;

	if (key_file == NULL || snapshot == NULL)
		return sakura_session_error(error, G_KEY_FILE_ERROR_INVALID_VALUE,
		                            g_strdup("session snapshot arguments are invalid"));

	version = g_key_file_get_integer(key_file, "Session", "version", error);
	if (error != NULL && *error != NULL)
		return FALSE;
	if (version < 1 || version > SAKURA_SESSION_VERSION)
		return sakura_session_error(error, G_KEY_FILE_ERROR_INVALID_VALUE,
		                            g_strdup_printf("unsupported session version: %d", version));

	group_count = g_key_file_get_integer(key_file, "Session", "group_count", error);
	if (error != NULL && *error != NULL)
		return FALSE;
	terminal_count = g_key_file_get_integer(key_file, "Session", "terminal_count", error);
	if (error != NULL && *error != NULL)
		return FALSE;
	if (group_count < 0 || terminal_count < 0)
		return sakura_session_error(error, G_KEY_FILE_ERROR_INVALID_VALUE,
		                            g_strdup("session counts cannot be negative"));

	if (g_key_file_has_key(key_file, "Session", "selected_terminal", NULL))
		snapshot->selected_terminal = g_key_file_get_integer(key_file, "Session",
	                                                     "selected_terminal", NULL);
	g_free(snapshot->selected_terminal_id);
	snapshot->selected_terminal_id = g_key_file_get_string(key_file, "Session",
	                                                       "selected_terminal_id", NULL);
	g_free(snapshot->active_group_id);
	snapshot->active_group_id = g_key_file_get_string(key_file, "Session",
	                                                  "active_group_id", NULL);
	if (snapshot->active_group_id == NULL)
		snapshot->active_group_id = g_strdup("root");
	if (g_key_file_has_key(key_file, "Session", "sidebar_visible", NULL))
		snapshot->sidebar_visible = g_key_file_get_boolean(key_file, "Session",
	                                                   "sidebar_visible", NULL);
	if (g_key_file_has_key(key_file, "Session", "sidebar_width", NULL))
		snapshot->sidebar_width = g_key_file_get_integer(key_file, "Session",
                                                 "sidebar_width", NULL);

	g_ptr_array_set_size(snapshot->groups, 0);
	g_ptr_array_set_size(snapshot->tabs, 0);
	for (index = 0; index < group_count; index++) {
		gchar *section = g_strdup_printf("Group%d", index);
		SakuraSessionGroupRecord *group = g_new0(SakuraSessionGroupRecord, 1);
		group->id = g_key_file_get_string(key_file, section, "id", NULL);
		group->parent_id = g_key_file_get_string(key_file, section, "parent", NULL);
		group->title = g_key_file_get_string(key_file, section, "title", NULL);
		if (group->parent_id == NULL)
			group->parent_id = g_strdup("root");
		if (group->title == NULL)
			group->title = g_strdup("");
		g_ptr_array_add(snapshot->groups, group);
		g_free(section);
	}

	for (index = 0; index < terminal_count; index++) {
		gchar *section = g_strdup_printf("Terminal%d", index);
		SakuraSessionTabRecord *tab = g_new0(SakuraSessionTabRecord, 1);
		gchar *kind = g_key_file_get_string(key_file, section, "kind", NULL);

		tab->parent_id = g_key_file_get_string(key_file, section, "parent", NULL);
		tab->cwd = g_key_file_get_string(key_file, section, "cwd", NULL);
		tab->title = g_key_file_get_string(key_file, section, "title", NULL);
		tab->terminal_id = g_key_file_get_string(key_file, section, "terminal_id", NULL);
		tab->tool_id = g_key_file_get_string(key_file, section, "tool", NULL);
		tab->tool_target = g_key_file_get_string(key_file, section, "tool_target", NULL);
		tab->codex_session_id = g_key_file_get_string(key_file, section,
	                                                "codex_session_id", NULL);
		tab->codex_session_name = g_key_file_get_string(key_file, section,
	                                                  "codex_session_name", NULL);
		tab->title_set_by_user = g_key_file_get_boolean(key_file, section,
	                                                 "title_set_by_user", NULL);
		tab->kind = g_strcmp0(kind, "codex") == 0
		          ? SAKURA_TAB_CODEX
		          : g_strcmp0(kind, "tool") == 0 || g_strcmp0(kind, "gitui") == 0
		          ? SAKURA_TAB_TOOL : SAKURA_TAB_SHELL;
		if (g_strcmp0(kind, "gitui") == 0 && tab->tool_id == NULL)
			tab->tool_id = g_strdup("gitui");
		if (tab->parent_id == NULL)
			tab->parent_id = g_strdup("root");
		g_ptr_array_add(snapshot->tabs, tab);
		g_free(kind);
		g_free(section);
	}

	return sakura_session_group_ids_valid(snapshot, error) &&
	       sakura_session_terminal_ids_valid(snapshot, error);
}


gboolean
sakura_session_snapshot_load(GKeyFile *key_file,
                              SakuraSessionSnapshot *snapshot,
                              GError **error)
{
	SakuraSessionSnapshot *parsed;
	SakuraSessionSnapshot previous;

	if (snapshot == NULL)
		return sakura_session_error(error, G_KEY_FILE_ERROR_INVALID_VALUE,
		                            g_strdup("session snapshot arguments are invalid"));

	parsed = sakura_session_snapshot_new();
	if (!sakura_session_snapshot_load_into(key_file, parsed, error)) {
		sakura_session_snapshot_free(parsed);
		return FALSE;
	}

	/* Commit atomically at the record level. A malformed or truncated session
	 * must never partially replace a snapshot that the caller may still use. */
	previous = *snapshot;
	*snapshot = *parsed;
	*parsed = previous;
	sakura_session_snapshot_free(parsed);
	return TRUE;
}


void
sakura_session_snapshot_save(const SakuraSessionSnapshot *snapshot,
                             GKeyFile *key_file)
{
	guint index;

	g_key_file_set_integer(key_file, "Session", "version", SAKURA_SESSION_VERSION);
	g_key_file_set_integer(key_file, "Session", "group_count", snapshot->groups->len);
	g_key_file_set_integer(key_file, "Session", "terminal_count", snapshot->tabs->len);
	g_key_file_set_integer(key_file, "Session", "selected_terminal",
	                      snapshot->selected_terminal);
	if (snapshot->selected_terminal_id != NULL)
		g_key_file_set_string(key_file, "Session", "selected_terminal_id",
		                      snapshot->selected_terminal_id);
	g_key_file_set_string(key_file, "Session", "active_group_id",
	                      snapshot->active_group_id != NULL ? snapshot->active_group_id : "root");
	g_key_file_set_boolean(key_file, "Session", "sidebar_visible", snapshot->sidebar_visible);
	g_key_file_set_integer(key_file, "Session", "sidebar_width", snapshot->sidebar_width);

	for (index = 0; index < snapshot->groups->len; index++) {
		SakuraSessionGroupRecord *group = g_ptr_array_index(snapshot->groups, index);
		gchar *section = g_strdup_printf("Group%u", index);
		g_key_file_set_string(key_file, section, "id", group->id != NULL ? group->id : "");
		g_key_file_set_string(key_file, section, "parent",
		                      group->parent_id != NULL ? group->parent_id : "root");
		g_key_file_set_string(key_file, section, "title", group->title != NULL ? group->title : "");
		g_free(section);
	}

	for (index = 0; index < snapshot->tabs->len; index++) {
		SakuraSessionTabRecord *tab = g_ptr_array_index(snapshot->tabs, index);
		gchar *section = g_strdup_printf("Terminal%u", index);
		const gchar *kind = tab->kind == SAKURA_TAB_CODEX ? "codex" :
		                    tab->kind == SAKURA_TAB_TOOL ? "tool" : "shell";

		g_key_file_set_string(key_file, section, "parent",
		                      tab->parent_id != NULL ? tab->parent_id : "root");
		g_key_file_set_string(key_file, section, "cwd", tab->cwd != NULL ? tab->cwd : "");
		g_key_file_set_string(key_file, section, "terminal_id",
		                      tab->terminal_id != NULL ? tab->terminal_id : "");
		g_key_file_set_string(key_file, section, "kind", kind);
		if (tab->tool_id != NULL)
			g_key_file_set_string(key_file, section, "tool", tab->tool_id);
		if (tab->tool_target != NULL && tab->tool_target[0] != '\0')
			g_key_file_set_string(key_file, section, "tool_target", tab->tool_target);
		if (tab->codex_session_id != NULL && tab->codex_session_id[0] != '\0')
			g_key_file_set_string(key_file, section, "codex_session_id", tab->codex_session_id);
		if (tab->codex_session_name != NULL && tab->codex_session_name[0] != '\0')
			g_key_file_set_string(key_file, section, "codex_session_name", tab->codex_session_name);
		g_key_file_set_boolean(key_file, section, "title_set_by_user", tab->title_set_by_user);
		if (tab->title_set_by_user)
			g_key_file_set_string(key_file, section, "title", tab->title != NULL ? tab->title : "");
		g_free(section);
	}
}
