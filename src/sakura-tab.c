#include <libintl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib/gstdio.h>

#include <gdk/gdkx.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "sakura-private.h"

#define _(String) gettext(String)

#define TAB_MAX_SIZE 40
#define TAB_MIN_SIZE 6

static void
sakura_tab_disconnect_exit_handler(SakuraTab *tab)
{
	if (tab == NULL || tab->vte == NULL || tab->exit_handler_id == 0)
		return;
	if (g_signal_handler_is_connected(G_OBJECT(tab->vte),
	                                  tab->exit_handler_id))
		g_signal_handler_disconnect(tab->vte, tab->exit_handler_id);
	tab->exit_handler_id = 0;
}

const gchar *
sakura_tab_status_label(SakuraTabStatus status)
{
	switch (status) {
		case SAKURA_TAB_STATUS_IDLE:
			return _("Idle");
		case SAKURA_TAB_STATUS_RUNNING:
			return _("Working");
		case SAKURA_TAB_STATUS_NEEDS_APPROVAL:
			return _("Needs approval");
		case SAKURA_TAB_STATUS_READY:
			return _("Ready to review");
		case SAKURA_TAB_STATUS_INTERRUPTED:
			return _("Interrupted");
		case SAKURA_TAB_STATUS_ERROR:
			return _("Error");
		case SAKURA_TAB_STATUS_NONE:
		default:
			return NULL;
	}
}

const gchar *
sakura_tab_status_color(SakuraTabStatus status)
{
	switch (status) {
		case SAKURA_TAB_STATUS_RUNNING:
			return "#5b9bd5";
		case SAKURA_TAB_STATUS_NEEDS_APPROVAL:
			return "#e5a13a";
		case SAKURA_TAB_STATUS_READY:
			return "#72b879";
		case SAKURA_TAB_STATUS_INTERRUPTED:
			return "#8c8c8c";
		case SAKURA_TAB_STATUS_ERROR:
			return "#d96c75";
		case SAKURA_TAB_STATUS_IDLE:
			return "#8c8c8c";
		case SAKURA_TAB_STATUS_NONE:
		default:
			return NULL;
	}
}

const gchar *
sakura_tab_status_symbol(SakuraTabStatus status)
{
	switch (status) {
		case SAKURA_TAB_STATUS_IDLE:
			return "•";
		case SAKURA_TAB_STATUS_NEEDS_APPROVAL:
			return "?";
		case SAKURA_TAB_STATUS_READY:
			return "✓";
		case SAKURA_TAB_STATUS_INTERRUPTED:
			return "■";
		case SAKURA_TAB_STATUS_ERROR:
			return "!";
		case SAKURA_TAB_STATUS_RUNNING:
		case SAKURA_TAB_STATUS_NONE:
		default:
			return NULL;
	}
}

gboolean
sakura_tab_is_current(SakuraTab *tab)
{
	return tab != NULL && tab->hbox != NULL && sakura.notebook != NULL &&
	       (sakura.active_tab != NULL
	        ? tab == sakura.active_tab
	        : sakura_page_for_tab(tab) ==
	          gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook)));
}


gboolean
sakura_tab_can_split(SakuraTab *tab)
{
	if (tab == NULL || tab->page == NULL || tab->layout_leaf == NULL)
		return FALSE;
	/* Shells and Codex sessions are terminal surfaces. Tool pages may be
	 * terminal-backed today, but their reuse/lookup semantics are not yet
	 * defined for splits; keep them single-pane until that policy is explicit. */
	if (tab->kind == SAKURA_TAB_TOOL)
		return FALSE;
#ifdef HAVE_WEBKITGTK
	if (tab->browser != NULL)
		return FALSE;
#endif
	return TRUE;
}


SakuraTab *
sakura_tab_new(void)
{
	return g_new0(SakuraTab, 1);
}


gboolean
sakura_terminal_id_is_valid(const gchar *terminal_id)
{
	const gchar *cursor;

	if (terminal_id == NULL || terminal_id[0] == '\0' || strlen(terminal_id) > 128)
		return FALSE;
	for (cursor = terminal_id; *cursor != '\0'; cursor++) {
		if (!g_ascii_isalnum(*cursor) && *cursor != '-' &&
		    *cursor != '_' && *cursor != '.')
			return FALSE;
	}
	return TRUE;
}


gchar *
sakura_generate_terminal_id(void)
{
	return g_strdup_printf("terminal-%d-%u", (int)getpid(), g_random_int());
}


gchar *
sakura_history_file_for_tab(const SakuraTab *tab)
{
	if (sakura.history_dir == NULL || tab == NULL || tab->terminal_id == NULL)
		return NULL;
	return g_build_filename(sakura.history_dir, tab->terminal_id, NULL);
}


void
sakura_prepare_history_file(SakuraTab *tab)
{
	gchar *history_file;
	int fd;

	history_file = sakura_history_file_for_tab(tab);
	if (history_file == NULL)
		return;

	fd = g_open(history_file, O_WRONLY | O_CREAT | O_APPEND, 0600);
	if (fd == -1) {
		g_warning("Could not create terminal history file %s: %s",
		          history_file, g_strerror(errno));
	} else {
		close(fd);
		if (chmod(history_file, 0600) != 0)
			g_warning("Could not secure terminal history file %s: %s",
			          history_file, g_strerror(errno));
	}
	g_free(history_file);
}


void
sakura_remove_history_file(SakuraTab *tab)
{
	gchar *history_file;

	if (sakura.session_shutting_down)
		return;
	history_file = sakura_history_file_for_tab(tab);
	if (history_file != NULL && g_remove(history_file) != 0 && errno != ENOENT)
		g_warning("Could not remove terminal history file %s: %s",
		          history_file, g_strerror(errno));
	g_free(history_file);
}


static gboolean
sakura_shell_is_bash(void)
{
	gchar *basename;
	gboolean is_bash;

	if (sakura.argv[0] == NULL)
		return FALSE;
	basename = g_path_get_basename(sakura.argv[0]);
	is_bash = g_strcmp0(basename, "bash") == 0;
	g_free(basename);
	return is_bash;
}


gboolean
sakura_bash_integration_enabled(void)
{
	return sakura.bash_history_rc != NULL && sakura_shell_is_bash() &&
	       g_getenv("SAKURA_DISABLE_HISTORY_INTEGRATION") == NULL;
}


void
sakura_spawn_callback(VteTerminal *vte, GPid pid, GError *error,
                      gpointer user_data)
{
	SakuraTab *tab = user_data;

	(void)vte;
	if (pid == -1) {
		g_warning("Could not spawn terminal child: %s",
		          error != NULL ? error->message : "unknown error");
		sakura_tab_set_status(tab, SAKURA_TAB_STATUS_ERROR, TRUE);
	} else if (tab != NULL) {
		tab->pid = pid;
	}
}


void
sakura_tab_spawn_shell(SakuraTab *tab, const gchar *cwd, gchar **env,
                       gboolean login_shell)
{
	gchar *bash_argv[6];
	gchar **argv = sakura.argv;

	if (sakura_bash_integration_enabled()) {
		bash_argv[0] = sakura.argv[0];
		if (login_shell) {
			/* Reproduce login-shell startup inside the generated rc file. */
			bash_argv[1] = sakura.argv[0];
			bash_argv[2] = (gchar *)"--noprofile";
			bash_argv[3] = (gchar *)"--rcfile";
			bash_argv[4] = sakura.bash_history_rc;
			bash_argv[5] = NULL;
		} else {
			bash_argv[1] = sakura.argv[1];
			bash_argv[2] = (gchar *)"--rcfile";
			bash_argv[3] = sakura.bash_history_rc;
			bash_argv[4] = NULL;
		}
		argv = bash_argv;
	}

	vte_terminal_spawn_async(VTE_TERMINAL(tab->vte), VTE_PTY_NO_HELPER, cwd,
	                         argv, env,
	                         G_SPAWN_SEARCH_PATH | G_SPAWN_FILE_AND_ARGV_ZERO,
	                         NULL, NULL, NULL, -1, NULL,
	                         sakura_spawn_callback, tab);
}


void
sakura_tab_spawn_codex(SakuraTab *tab, const gchar *cwd, gchar **env)
{
	gchar **codex_env = g_get_environ();
	gchar *reasoning_config = NULL;
	gchar *argv[10] = { (gchar *)"codex",
	                    (gchar *)"--dangerously-bypass-approvals-and-sandbox",
	                    (gchar *)"--enable", (gchar *)"hooks",
	                    NULL, NULL, NULL, NULL, NULL, NULL };
	guint next_arg = 4;

	if (sakura_codex_reasoning_effort_is_valid(tab->codex_reasoning_effort)) {
		reasoning_config = g_strdup_printf("model_reasoning_effort=%s",
		                                    tab->codex_reasoning_effort);
		argv[next_arg++] = (gchar *)"--config";
		argv[next_arg++] = reasoning_config;
	}

	if (tab->codex_session_id != NULL && tab->codex_session_id[0] != '\0') {
		argv[next_arg++] = (gchar *)"resume";
		argv[next_arg++] = tab->codex_session_id;
	}
	/* NO_COLOR is inherited from the environment Sakura was launched from.
	 * Codex is a color-capable TUI, so remove only this opt-out from its child
	 * environment while preserving all other inherited variables and Sakura's
	 * per-tab additions. */
	if (env != NULL) {
		gchar **entry;
		for (entry = env; *entry != NULL; entry++) {
			const gchar *separator = strchr(*entry, '=');
			gchar *name;

			if (separator == NULL || separator == *entry)
				continue;
			name = g_strndup(*entry, (gsize)(separator - *entry));
			codex_env = g_environ_setenv(codex_env, name, separator + 1, TRUE);
			g_free(name);
		}
	}
	codex_env = g_environ_unsetenv(codex_env, "NO_COLOR");
	vte_terminal_spawn_async(VTE_TERMINAL(tab->vte), VTE_PTY_NO_HELPER, cwd,
	                         argv, codex_env, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL,
	                         -1, NULL, sakura_spawn_callback, tab);
	g_strfreev(codex_env);
	g_free(reasoning_config);
}


void
sakura_build_command(const gchar *execute_command, gchar **xterm_args,
                     int *command_argc, gchar ***command_argv)
{
	GError *error = NULL;

	if (execute_command != NULL) {
		/* -x option: only one argument. */
		if (!g_shell_parse_argv(execute_command, command_argc, command_argv, &error)) {
			if (error != NULL)
				sakura_error("Cannot parse command line arguments: %s", error->message);
			g_clear_error(&error);
			exit(EXIT_FAILURE);
		}
		return;
	}

	/* -e option: the remaining command-line arguments. */
	if (xterm_args != NULL) {
		guint size = 0;
		guint index = 0;
		gchar **quoted_args;
		gchar *command_joined;

		while (xterm_args[size] != NULL)
			size++;
		quoted_args = g_new0(gchar *, size + 1);
		while (xterm_args[index] != NULL) {
			quoted_args[index] = g_shell_quote(xterm_args[index]);
			index++;
		}
		command_joined = g_strjoinv(" ", quoted_args);
		if (!g_shell_parse_argv(command_joined, command_argc, command_argv, &error)) {
			if (error != NULL)
				sakura_error("Cannot parse command line arguments: %s", error->message);
			g_clear_error(&error);
			g_free(command_joined);
			g_strfreev(quoted_args);
			exit(EXIT_FAILURE);
		}
		g_free(command_joined);
		g_strfreev(quoted_args);
	}
}

void
sakura_search (const char *pattern, bool reverse)
{
	GError *error=NULL;
	VteRegex *regex;
	struct sakura_tab *sk_tab;

	sk_tab = sakura.active_tab != NULL ? sakura.active_tab :
	         sakura_tab_at_page(gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook)));
	if (sk_tab == NULL)
		return;

	vte_terminal_search_set_wrap_around(VTE_TERMINAL(sk_tab->vte), TRUE);

	regex=vte_regex_new_for_search(pattern, (gssize) strlen(pattern), PCRE2_MULTILINE|PCRE2_CASELESS, &error);
	if (!regex) { /* Ubuntu-fucking-morons (17.10/18.04/18.10) package a broken VTE without PCRE2, and search fails */
		      /* For more info about their moronity please look at https://github.com/gnunn1/tilix/issues/916   */
		sakura_error(error->message);
		g_error_free(error);
	} else {
		vte_terminal_search_set_regex(VTE_TERMINAL(sk_tab->vte), regex, 0);

		if (!vte_terminal_search_find_next(VTE_TERMINAL(sk_tab->vte))) {
			vte_terminal_unselect_all(VTE_TERMINAL(sk_tab->vte));
			vte_terminal_search_find_next(VTE_TERMINAL(sk_tab->vte));
		}

		if (regex) vte_regex_unref(regex);
	}
}


void
sakura_copy ()
{
	struct sakura_tab *sk_tab;

	sk_tab = sakura.active_tab != NULL ? sakura.active_tab :
	         sakura_tab_at_page(gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook)));
	if (sk_tab == NULL)
		return;

	if (vte_terminal_get_has_selection(VTE_TERMINAL(sk_tab->vte))) {
		vte_terminal_copy_clipboard_format(VTE_TERMINAL(sk_tab->vte), VTE_FORMAT_TEXT);
	}
}


void
sakura_paste ()
{
	struct sakura_tab *sk_tab;

	sk_tab = sakura.active_tab != NULL ? sakura.active_tab :
	         sakura_tab_at_page(gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook)));
	if (sk_tab == NULL)
		return;

	vte_terminal_paste_clipboard(VTE_TERMINAL(sk_tab->vte));
}


void
sakura_paste_primary ()
{
	struct sakura_tab *sk_tab;

	sk_tab = sakura.active_tab != NULL ? sakura.active_tab :
	         sakura_tab_at_page(gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook)));
	if (sk_tab == NULL)
		return;

	vte_terminal_paste_primary(VTE_TERMINAL(sk_tab->vte));
}

gboolean
sakura_tab_start_process(SakuraTab *tab, const gchar *cwd, gchar **env,
                         SakuraTabKind kind, SakuraToolKind tool,
                         const gchar *execute_command, gchar **xterm_args,
                         gboolean allow_execute)
{
	if (kind == SAKURA_TAB_CODEX) {
		sakura_tab_spawn_codex(tab, cwd, env);
		return TRUE;
	}
	if (kind == SAKURA_TAB_TOOL) {
		sakura_tab_spawn_tool(tab, cwd, env);
		return TRUE;
	}
	if (allow_execute && (execute_command != NULL || xterm_args != NULL))
		return sakura_tab_spawn_command(tab, cwd, env, execute_command, xterm_args);
	return FALSE;
}

void
sakura_tab_add_with_options (const gchar *restore_cwd,
                              struct sakura_sidebar_node *restore_parent,
                              const gchar *restore_title,
                              gboolean restore_title_set,
                              SakuraTabKind restore_kind,
                              SakuraToolKind restore_tool,
                              const gchar *restore_codex_session_id,
                              const gchar *restore_codex_session_name,
                              const gchar *restore_codex_reasoning_effort,
                              const gchar *restore_tool_target,
                              const gchar *restore_terminal_id,
                              gint restore_colorset,
                              const SakuraTabLaunchConfig *launch_config)
{
	struct sakura_tab *sk_tab;
	SakuraPage *tab_page;
	GtkWidget *tab_title_hbox; GtkWidget *close_button; /* We could put them inside struct sakura_tab, but it is not necessary */
	GtkWidget *event_box;
	gint index, page, npages, target_page_index = -1;
	gchar *cwd = NULL; gchar *group_cwd = NULL; gchar *default_label_text = NULL;
	struct sakura_sidebar_node *sidebar_parent;
	const SakuraTabLaunchConfig default_config = {
		.execute_command = NULL,
		.xterm_args = NULL,
		.login_shell = FALSE,
		.hold = FALSE,
		.execute_on_existing_tabs = FALSE,
		.suppress_current_cwd_fallback = FALSE,
		.target_page = NULL,
		.target_layout = NULL,
		.target_ratio = 0.5,
		.split_direction = SAKURA_SPLIT_RIGHT
	};
	const SakuraTabLaunchConfig *config = launch_config != NULL ? launch_config : &default_config;
	gboolean split_into_page = config->target_page != NULL;
	gboolean hold_option = config->hold;

	sk_tab = sakura_tab_new();
	tab_page = config->target_page;
	if (split_into_page) {
		/* Resolve the notebook position before changing the layout. A split
		 * target that is no longer attached must not leave a new model leaf
		 * behind when creation is rejected. */
		if (tab_page == NULL || tab_page->active_tab == NULL ||
		    tab_page->active_tab->layout_leaf == NULL ||
		    (target_page_index = sakura_page_for_tab(tab_page->active_tab)) < 0) {
			sakura_tab_free(sk_tab);
			sakura_error("Cannot find the split notebook page");
			return;
		}
	}
	if (!split_into_page) {
		tab_page = sakura_page_new(NULL);
		tab_page->container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		if (sakura_layout_leaf_new(tab_page, sk_tab) == NULL) {
			sakura_page_free(tab_page);
			sakura_tab_free(sk_tab);
			sakura_error("Cannot create a layout leaf");
			exit(EXIT_FAILURE);
		}
	}
	sk_tab->terminal_id = sakura_terminal_id_is_valid(restore_terminal_id)
	                    ? g_strdup(restore_terminal_id)
	                    : sakura_generate_terminal_id();
	sk_tab->kind = restore_kind;
	sk_tab->tool = restore_kind == SAKURA_TAB_TOOL ? restore_tool : SAKURA_TOOL_NONE;
	sk_tab->hold = hold_option;
	sk_tab->tool_target = restore_kind == SAKURA_TAB_TOOL
	                   ? g_strdup(restore_tool_target) : NULL;
	/* A restored Codex tab has no persisted status yet. Start from a known
	 * neutral state until the tracking hook reports its current state. */
	sk_tab->status = restore_kind == SAKURA_TAB_CODEX
	               ? SAKURA_TAB_STATUS_IDLE : SAKURA_TAB_STATUS_NONE;
	sk_tab->codex_session_id = g_strdup(restore_codex_session_id);
	sk_tab->codex_session_name = g_strdup(restore_codex_session_name);
	sk_tab->codex_reasoning_effort = sakura_codex_reasoning_effort_is_valid(
		restore_codex_reasoning_effort) ? g_strdup(restore_codex_reasoning_effort) : NULL;
	if (sk_tab->codex_session_name == NULL &&
	    restore_kind == SAKURA_TAB_CODEX &&
	    restore_codex_session_id != NULL &&
	    !sakura_codex_session_id_is_uuid(restore_codex_session_id))
		sk_tab->codex_session_name = g_strdup(restore_codex_session_id);
	sk_tab->codex_tracking_token = g_strdup_printf("%d-%u", (int)getpid(),
	                                              g_random_int());
	sidebar_parent = restore_parent != NULL ? restore_parent :
	                 sakura_sidebar_default_parent();

	/* Tab widgets and the VTE are created together so the tab module owns the
	 * complete terminal surface. */
	sakura_tab_create_widgets(sk_tab);
	if (!split_into_page) {
		/* The first leaf is created before its GTK surface exists. Complete the
		 * model/widget link now so later split operations can replace this leaf
		 * with a GtkPaned subtree. */
		if (sk_tab->layout_leaf != NULL)
			sk_tab->layout_leaf->widget = sk_tab->hbox;
		gtk_box_pack_start(GTK_BOX(tab_page->container), sk_tab->hbox, TRUE, TRUE, 0);
	} else if ((config->target_layout == NULL &&
	          (tab_page->active_tab == NULL ||
	           tab_page->active_tab->layout_leaf == NULL)) ||
	         !sakura_layout_split_node_widgets(
	             config->target_layout != NULL ? config->target_layout
	                                            : tab_page->active_tab->layout_leaf,
	             config->split_direction, sk_tab)) {
		sakura_tab_free(sk_tab);
			sakura_error("Cannot split the current terminal pane");
			return;
	}
	if (split_into_page && sk_tab->layout_leaf != NULL &&
	    sk_tab->layout_leaf->parent != NULL && config->target_ratio > 0.0 &&
	    config->target_ratio <= 1.0)
		sakura_layout_set_ratio(sk_tab->layout_leaf->parent, config->target_ratio);
	tab_title_hbox = sk_tab->tab_title_hbox;
	event_box = sk_tab->tab_event_box;
	close_button = sk_tab->tab_close_button;

	sk_tab->colorset = restore_colorset >= 0 && restore_colorset < NUM_COLORSETS
	                 ? restore_colorset
	                 : CLAMP(sakura.last_colorset - 1, 0, NUM_COLORSETS - 1);

	/* -1 if there is no pages yet */
	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));

	/* Use the restored directory when reopening a workspace. Otherwise use the
	 * configured group directory, then the previous terminal (if there is one)
	 * cwd and colorset. */
	if (restore_cwd == NULL || restore_cwd[0] == '\0')
		group_cwd = sakura_sidebar_directory_for_node(sidebar_parent);
	if (restore_cwd != NULL && restore_cwd[0] != '\0') {
		cwd = g_strdup(restore_cwd);
	} else if (group_cwd != NULL) {
		cwd = g_strdup(group_cwd);
	} else if (page >= 0 && !config->suppress_current_cwd_fallback) {
		struct sakura_tab *prev_term;
		prev_term = sakura_tab_at_page(page);
		/* If OSC7 method doesn't work, use the old one as fallback */
		if ((cwd = sakura_get_term_cwd_osc7(prev_term)) == NULL) {
			cwd = sakura_get_term_cwd(prev_term);
		}

		sk_tab->colorset = prev_term->colorset;
	}

	if (!cwd)
		cwd = g_get_current_dir();
	g_free(group_cwd);

	if (!split_into_page) {
		if (!sakura.new_tab_after_current) {
			if ((index=gtk_notebook_append_page(GTK_NOTEBOOK(sakura.notebook), tab_page->container, tab_title_hbox))==-1) {
				sakura_error("Cannot create a new tab");
				exit(1);
			}
		} else {
			if ((index=gtk_notebook_insert_page(GTK_NOTEBOOK(sakura.notebook), tab_page->container, tab_title_hbox, page+1))==-1) {
				sakura_error("Cannot create a new tab");
				exit(1);
			}
		}
		gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(sakura.notebook), tab_page->container, TRUE);
	} else {
		/* A split adds a pane inside the current notebook page; it must not
		 * create another notebook tab. */
		index = target_page_index;
	}

	if (!split_into_page) {
		if (sakura.tabs != NULL)
			g_ptr_array_insert(sakura.tabs, index, sk_tab);
		if (sakura.pages != NULL)
			g_ptr_array_insert(sakura.pages, index, tab_page);
		tab_page->tab_bar_tab = sk_tab;
	} else {
		tab_page->active_tab = sk_tab;
	}
	if (sakura.panes != NULL)
		g_ptr_array_add(sakura.panes, sk_tab);
	if (gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook)) == index)
		sakura.active_tab = sk_tab;
	if (gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook)) == index)
		sakura.active_page = tab_page;
	if (split_into_page) {
		sakura.active_tab = sk_tab;
		sakura.active_page = tab_page;
	}

	/* vte signals */
	g_signal_connect(G_OBJECT(sk_tab->vte), "bell", G_CALLBACK(sakura_beep_cb), NULL);
	g_signal_connect(G_OBJECT(sk_tab->vte), "increase-font-size", G_CALLBACK(sakura_increase_font_cb), NULL);
	g_signal_connect(G_OBJECT(sk_tab->vte), "decrease-font-size", G_CALLBACK(sakura_decrease_font_cb), NULL);
	sk_tab->exit_handler_id = g_signal_connect(G_OBJECT(sk_tab->vte), "child-exited", G_CALLBACK(sakura_child_exited_cb), NULL);
	g_signal_connect(G_OBJECT(sk_tab->vte), "eof", G_CALLBACK(sakura_eof_cb), NULL);
	g_signal_connect(G_OBJECT(sk_tab->vte), "window-title-changed", G_CALLBACK(sakura_tab_title_changed_cb), NULL);
	g_signal_connect(G_OBJECT(sk_tab->vte), "button-press-event", G_CALLBACK(sakura_term_buttonpressed_cb), sakura.menu);
	g_signal_connect_swapped(G_OBJECT(sk_tab->vte), "button-release-event", G_CALLBACK(sakura_term_buttonreleased_cb), sakura.menu);
	g_signal_connect(G_OBJECT(sk_tab->vte), "focus-in-event", G_CALLBACK(sakura_pane_focus_in_cb), sk_tab);
	g_signal_connect(G_OBJECT(sk_tab->vte), "key-press-event", G_CALLBACK(sakura_tab_keypress_cb), sk_tab);

	/* Label & button signals */
	/* We need the hbox to know which label/button was clicked */
	g_signal_connect(G_OBJECT(event_box), "button_press_event", G_CALLBACK(sakura_label_clicked_cb), sk_tab);
	if (sakura.show_closebutton) {
		g_signal_connect(G_OBJECT(close_button), "clicked", G_CALLBACK(sakura_closebutton_clicked_cb), sk_tab);
	}
	sakura_prepare_history_file(sk_tab);

	/* Terminal-specific environment is assembled by the tab module. */
	gchar **command_env = sakura_tab_build_environment(sk_tab, config->login_shell);

	/******* First tab **********/
	npages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));
	if (!split_into_page && npages == 1) {
		gtk_notebook_set_show_border(GTK_NOTEBOOK(sakura.notebook), FALSE);

		sakura_set_font();
		sakura_set_colors();
		/* Set size before showing the widgets but after setting the font */
		sakura_set_size();

		/* Notebook signals. Per notebook signals only need to be defined once, so we put them here */
		g_signal_connect(sakura.notebook, "scroll-event", G_CALLBACK(sakura_notebook_scroll_cb), NULL);
		g_signal_connect(G_OBJECT(sakura.notebook), "switch-page", G_CALLBACK(sakura_switch_page_cb), NULL);
		g_signal_connect(G_OBJECT(sakura.notebook), "page-reordered",
		                 G_CALLBACK(sakura_notebook_page_reordered_cb), NULL);
		g_signal_connect(G_OBJECT(sakura.notebook), "page-removed", G_CALLBACK(sakura_page_removed_cb), NULL);
		g_signal_connect(G_OBJECT(sakura.notebook), "focus-in-event", G_CALLBACK(sakura_notebook_focus_cb), NULL);

		gtk_widget_show_all(sakura.notebook);
		if (!sakura.show_scrollbar) {
			gtk_widget_hide(sk_tab->scrollbar);
		}

		gtk_widget_show(sakura.main_window);

		sakura_set_colors();
#ifdef GDK_WINDOWING_X11
		/* Set WINDOWID env variable */
		GdkDisplay *display = gdk_display_get_default();

		if (GDK_IS_X11_DISPLAY (display)) {
			GdkWindow *gwin = gtk_widget_get_window (sakura.main_window);
			if (gwin != NULL) {
				guint winid = gdk_x11_window_get_xid (gwin);
				gchar *winidstr = g_strdup_printf ("%d", winid);
				g_setenv ("WINDOWID", winidstr, FALSE);
				g_free (winidstr);
			}
		}
#endif

		gboolean child_started = sakura_tab_start_process(
			sk_tab, cwd, command_env, restore_kind, restore_tool,
			config->execute_command, config->xterm_args, TRUE);

		/* Fork shell if there is no execute option or if the command is not valid */
		if (restore_kind != SAKURA_TAB_CODEX && restore_kind != SAKURA_TAB_TOOL &&
		    !child_started) {
			if (hold_option == TRUE) {
				sakura_error("Hold option given without any command");
				hold_option = FALSE;
				sk_tab->hold = FALSE;
			}
			sakura_tab_spawn_shell(sk_tab, cwd, command_env, config->login_shell);
		}

	/********** Not the first tab ************/
	} else {
		sakura_set_font();
		sakura_set_colors();
		/* A GtkNotebook will not switch to a hidden child. New standalone
		 * pages are inserted after the notebook was initially shown, so make
		 * the page container itself visible, not only its terminal child. */
		gtk_widget_show_all(split_into_page ? sk_tab->hbox : tab_page->container);
		if (!sakura.show_scrollbar) {
			gtk_widget_hide(sk_tab->scrollbar);
		}

		if (npages == 2 && sakura.show_tab_bar != SHOW_TAB_BAR_NEVER) {
			sakura_tab_bar_refresh();
			sakura_set_size();
		}
		/* Call set_current page after showing the widget: gtk ignores this
		 * function in the window is not visible *sigh*. Gtk documentation
		 * says this is for "historical" reasons. Me arse */
		gtk_notebook_set_current_page(GTK_NOTEBOOK(sakura.notebook), index);

		gboolean child_started = sakura_tab_start_process(
			sk_tab, cwd, command_env, restore_kind, restore_tool,
			config->execute_command, config->xterm_args, config->execute_on_existing_tabs);

		/* Fork shell if there is no execute option or if the command is not valid */
		if (restore_kind != SAKURA_TAB_CODEX && restore_kind != SAKURA_TAB_TOOL &&
		    !child_started) {
			if (hold_option == TRUE) {
				sakura_error("Hold option given without any command");
				hold_option = FALSE;
				sk_tab->hold = FALSE;
			}
			sakura_tab_spawn_shell(sk_tab, cwd, command_env, config->login_shell);
		}
	}

	g_free(sk_tab->cwd);
	sk_tab->cwd = g_strdup(cwd);
	sakura_update_tab_metadata(sk_tab,
	                           vte_terminal_get_window_title(VTE_TERMINAL(sk_tab->vte)));
	g_free(cwd);

	/* Applying the restored title or tab title pattern from config
	 * (https://answers.launchpad.net/sakura/+question/267951) */
	if (restore_title_set) {
		default_label_text = (gchar *)restore_title;
		sk_tab->label_set_byuser = true;
	} else if (sakura.tab_default_title != NULL) {
		default_label_text = sakura.tab_default_title;
		sk_tab->label_set_byuser = true;
	} else if (restore_kind == SAKURA_TAB_TOOL) {
		default_label_text = (gchar *)sakura_tool_label(restore_tool);
		sk_tab->label_set_byuser = false;
	} else {
		sk_tab->label_set_byuser=false;
	}

	/* Set the default title text (NULL is valid) */
	page = sakura_page_for_tab(sk_tab);
	if (!split_into_page)
		sakura_set_tab_label_text(default_label_text, page);
	else
		gtk_label_set_text(GTK_LABEL(sk_tab->label), _("Terminal"));
	sakura_sidebar_add_terminal(sk_tab, sidebar_parent);
	if (sk_tab->kind == SAKURA_TAB_CODEX)
		sakura_codex_sync_name(sk_tab);
	sakura_session_mark_dirty();
	g_strfreev(command_env);

	sakura_tab_configure_terminal(sk_tab);
	if (!sakura.session_restoring)
		sakura_sidebar_select_created_tab(sk_tab);

}
void
sakura_tab_delete_pane(SakuraTab *tab)
{
	SakuraPage *page;
	gint page_index;
	gboolean was_active;
	gboolean was_representative;

	if (tab == NULL || tab->page == NULL || tab->layout_leaf == NULL)
		return;
	page = tab->page;
	if (page->panes == NULL || page->panes->len <= 1) {
		page_index = sakura_page_for_tab(tab);
		if (page_index >= 0)
			sakura_tab_delete_page(page_index);
		return;
	}
	if (sakura.workspace_mutating)
		return;
	sakura.workspace_mutating = TRUE;
	/* Removing the leaf can destroy its VTE. Disconnect while the widget is
	 * still alive, before GTK reparents/removes the containing paned widget. */
	sakura_tab_disconnect_exit_handler(tab);
	if (!sakura_layout_remove_leaf_widgets(tab->layout_leaf))
	{
		sakura.workspace_mutating = FALSE;
		return;
	}

	was_active = sakura.active_tab == tab;
	was_representative = page->tab_bar_tab == tab;
	sakura_sidebar_remove_tab(tab);
	sakura_remove_history_file(tab);
	if (sakura.panes != NULL)
		g_ptr_array_remove_fast(sakura.panes, tab);
	sakura_layout_remove_leaf(tab->layout_leaf);
	if (was_representative) {
		page->tab_bar_tab = page->active_tab;
		sakura_notebook_sync_page_order();
	}
	if (was_active)
		sakura.active_tab = page->active_tab;
	if (sakura.active_tab == NULL)
		sakura.active_tab = page->active_tab;
	sakura.active_page = page;
	sakura_tab_free(tab);
	sakura_tab_bar_refresh();
	sakura_sidebar_update_attention_count();
	if (sakura.active_tab != NULL)
		sakura_select_tab(sakura.active_tab, TRUE);
	sakura_set_size();
	sakura_session_mark_dirty();
	sakura_session_flush();
	sakura.workspace_mutating = FALSE;
}


gboolean
sakura_tab_delete_page(gint page)
{
	struct sakura_tab *sk_tab;
	SakuraPage *tab_page;
	GPtrArray *page_panes;
	gboolean removed_active;
	guint index;

	/* GTK's notebook API expects a non-negative page number. The shutdown
	 * path historically used -1 as shorthand for the last tab, which could
	 * leave us disconnecting handlers from an already-destroyed VTE widget. */
	if (page < 0)
		page = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook)) - 1;
	if (page < 0)
		return FALSE;
	sk_tab = sakura_tab_at_page(page);
	if (sk_tab == NULL)
		return FALSE;
	if (sakura.workspace_mutating)
		return FALSE;
	sakura.workspace_mutating = TRUE;
	tab_page = sk_tab->page;
	page_panes = tab_page != NULL && tab_page->panes != NULL
	           ? g_ptr_array_ref(tab_page->panes) : NULL;
	/* Capture this before GTK detaches the page. Removing the current page can
	 * synchronously emit switch-page and temporarily point active_page at a
	 * physical notebook neighbor. */
	removed_active = tab_page != NULL &&
	               (sakura.active_page == tab_page ||
	                gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook)) == page);
	if (page_panes != NULL) {
		for (guint index = 0; index < page_panes->len; index++)
			sakura_tab_disconnect_exit_handler(g_ptr_array_index(page_panes, index));
	}
	if (tab_page != NULL && !sakura_notebook_detach_page(tab_page)) {
		g_clear_pointer(&page_panes, g_ptr_array_unref);
		sakura.workspace_mutating = FALSE;
		return FALSE;
	}
	if (removed_active)
		sakura.active_tab = NULL;

	/* Do the first tab checks BEFORE deleting the tab, to ensure correct
	 * sizes are calculated when the tab is deleted */
	if (page_panes != NULL) {
		for (index = 0; index < page_panes->len; index++) {
			SakuraTab *pane = g_ptr_array_index(page_panes, index);
			if (pane == NULL)
				continue;
			sakura_sidebar_remove_tab(pane);
			sakura_remove_history_file(pane);
			if (sakura.panes != NULL)
				g_ptr_array_remove_fast(sakura.panes, pane);
		}
	}
	if (sakura.active_page == tab_page)
		sakura.active_page = NULL;
	if (tab_page != NULL) {
		sakura_sidebar_remove_page(tab_page);
		sakura_page_free(tab_page);
	}
	if (page_panes != NULL) {
		for (index = 0; index < page_panes->len; index++)
			sakura_tab_free(g_ptr_array_index(page_panes, index));
		g_ptr_array_unref(page_panes);
	} else {
		sakura_tab_free(sk_tab);
	}
	sakura_tab_bar_refresh();
	if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook)) > 0)
		sakura_set_size();
	sakura_sidebar_update_attention_count();

	/* Commit the deletion before selecting a replacement. This lets the normal
	 * notebook callback run for the replacement page and keeps the fallback
	 * scoped to the group/task that owned the deleted page. */
	sakura.workspace_mutating = FALSE;
	if (removed_active)
		sakura_select_scope_default();
	else
		sakura_tab_bar_refresh();
	sakura_session_mark_dirty();
	sakura_session_flush();
	return TRUE;
}


void
sakura_tab_create_widgets(SakuraTab *tab)
{
	GtkWidget *tab_label_box;
	GtkWidget *image;

	if (tab == NULL)
		return;

	tab->label = gtk_label_new(NULL);
	gtk_label_set_ellipsize(GTK_LABEL(tab->label), PANGO_ELLIPSIZE_END);
	tab->spinner = gtk_spinner_new();
	gtk_widget_set_no_show_all(tab->spinner, TRUE);
	gtk_widget_set_size_request(tab->spinner, 16, 16);
	gtk_widget_set_valign(tab->spinner, GTK_ALIGN_CENTER);
	gtk_widget_set_tooltip_text(tab->spinner, _("Working"));

	tab->tab_title_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	gtk_widget_set_hexpand(tab->tab_title_hbox, TRUE);
	tab->tab_event_box = gtk_event_box_new();
	tab_label_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	gtk_box_pack_start(GTK_BOX(tab_label_box), tab->spinner, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(tab_label_box), tab->label, TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(tab->tab_event_box), tab_label_box);
	gtk_widget_set_events(tab->tab_event_box, GDK_BUTTON_PRESS_MASK);
	gtk_box_pack_start(GTK_BOX(tab->tab_title_hbox), tab->tab_event_box,
	                   TRUE, TRUE, 0);

	if (sakura.show_closebutton) {
		tab->tab_close_button = gtk_button_new();
		gtk_widget_add_events(tab->tab_close_button, GDK_SCROLL_MASK);
		gtk_widget_set_name(tab->tab_close_button, "closebutton");
		gtk_button_set_relief(GTK_BUTTON(tab->tab_close_button), GTK_RELIEF_NONE);
		image = gtk_image_new_from_icon_name("window-close", GTK_ICON_SIZE_MENU);
		gtk_container_add(GTK_CONTAINER(tab->tab_close_button), image);
		gtk_box_pack_start(GTK_BOX(tab->tab_title_hbox), tab->tab_close_button,
		                   FALSE, FALSE, 0);
	}

	gtk_widget_show_all(tab->tab_title_hbox);

	tab->vte = vte_terminal_new();
	/* The widget is owned by the GTK hierarchy; keep this model pointer weak so
	 * teardown cannot leave a dangling GObject pointer behind. */
	g_object_add_weak_pointer(G_OBJECT(tab->vte), (gpointer *)&tab->vte);
	tab->scrollbar = gtk_scrollbar_new(
		GTK_ORIENTATION_VERTICAL,
		gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(tab->vte)));
	tab->hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(tab->hbox), tab->vte, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(tab->hbox), tab->scrollbar, FALSE, FALSE, 0);
}


void
sakura_tab_configure_terminal(SakuraTab *tab)
{
	if (tab == NULL || tab->vte == NULL)
		return;

	vte_terminal_set_scrollback_lines(VTE_TERMINAL(tab->vte), sakura.scroll_lines);
	if (sakura.http_vteregexp != NULL)
		vte_terminal_match_add_regex(VTE_TERMINAL(tab->vte),
		                             sakura.http_vteregexp, PCRE2_CASELESS);
	if (sakura.mail_vteregexp != NULL)
		vte_terminal_match_add_regex(VTE_TERMINAL(tab->vte),
		                             sakura.mail_vteregexp, PCRE2_CASELESS);
	vte_terminal_set_mouse_autohide(VTE_TERMINAL(tab->vte), TRUE);
	vte_terminal_set_backspace_binding(VTE_TERMINAL(tab->vte), VTE_ERASE_ASCII_DELETE);
	vte_terminal_set_word_char_exceptions(VTE_TERMINAL(tab->vte), sakura.word_chars);
	vte_terminal_set_audible_bell(VTE_TERMINAL(tab->vte), sakura.audible_bell ? TRUE : FALSE);
	vte_terminal_set_cursor_blink_mode(
		VTE_TERMINAL(tab->vte),
		sakura.blinking_cursor ? VTE_CURSOR_BLINK_ON : VTE_CURSOR_BLINK_OFF);
	vte_terminal_set_cursor_shape(VTE_TERMINAL(tab->vte), sakura.cursor_type);
}


gchar **
sakura_tab_build_environment(SakuraTab *tab, gboolean login_shell)
{
	gchar **environment;
	guint length = 0;

	environment = g_new0(gchar *, 8);
	environment[length++] = g_strdup_printf("TERM=%s",
	                                       sakura.term != NULL
	                                       ? sakura.term : "xterm-256color");
	if (sakura.history_dir != NULL) {
		gchar *history_file = sakura_history_file_for_tab(tab);
		if (history_file != NULL) {
			environment[length++] = g_strdup_printf("HISTFILE=%s", history_file);
			environment[length++] = g_strdup_printf("SAKURA_HISTORY_FILE=%s",
		                                           history_file);
			g_free(history_file);
		}
	}
	if (sakura_bash_integration_enabled() && login_shell)
		environment[length++] = g_strdup("SAKURA_BASH_LOGIN=1");
	if (sakura.codex_tracking_dir != NULL) {
		environment[length++] = g_strdup_printf("SAKURA_CODEX_TRACKING_DIR=%s",
		                                           sakura.codex_tracking_dir);
		environment[length++] = g_strdup_printf("SAKURA_CODEX_TAB_TOKEN=%s",
		                                           tab->codex_tracking_token);
	}
	return environment;
}


gboolean
sakura_tab_spawn_command(SakuraTab *tab, const gchar *cwd, gchar **env,
                         const gchar *execute_command, gchar **xterm_args)
{
	int command_argc = 0;
	gchar **command_argv = NULL;
	gchar *path;

	sakura_build_command(execute_command, xterm_args,
	                     &command_argc, &command_argv);
	if (command_argc == 0 || command_argv == NULL)
		return FALSE;

	path = g_find_program_in_path(command_argv[0]);
	if (path == NULL) {
		sakura_error("%s command not found", command_argv[0]);
		g_strfreev(command_argv);
		return FALSE;
	}

	vte_terminal_spawn_async(VTE_TERMINAL(tab->vte), VTE_PTY_NO_HELPER, NULL,
	                         command_argv, env, G_SPAWN_SEARCH_PATH,
	                         NULL, NULL, NULL, -1, NULL,
	                         sakura_spawn_callback, tab);
	g_free(path);
	g_strfreev(command_argv);
	return TRUE;
}


SakuraTab *
sakura_tab_for_vte(VteTerminal *vte)
{
	guint page;

	if (vte == NULL || sakura.panes == NULL)
		return NULL;

	for (page = 0; page < sakura.panes->len; page++) {
		SakuraTab *tab = g_ptr_array_index(sakura.panes, page);
		if (tab != NULL && VTE_TERMINAL(tab->vte) == vte)
			return tab;
	}
	return NULL;
}


SakuraTab *
sakura_find_pane_by_terminal_id(const gchar *terminal_id)
{
	guint index;

	if (terminal_id == NULL || terminal_id[0] == '\0' || sakura.panes == NULL)
		return NULL;
	for (index = 0; index < sakura.panes->len; index++) {
		SakuraTab *tab = g_ptr_array_index(sakura.panes, index);
		if (tab != NULL && g_strcmp0(tab->terminal_id, terminal_id) == 0)
			return tab;
	}
	return NULL;
}


void
sakura_set_window_title(const gchar *title)
{
	if (sakura.main_window != NULL)
		gtk_window_set_title(GTK_WINDOW(sakura.main_window), title != NULL ? title : "");
	if (sakura.header_bar != NULL)
		gtk_header_bar_set_title(GTK_HEADER_BAR(sakura.header_bar), title != NULL ? title : "");
}


void
sakura_set_tab_label_text(const gchar *title, gint page)
{
	SakuraTab *tab;
	gchar *label_text;

	tab = sakura_tab_at_page(page);
	if (tab == NULL || tab->label == NULL)
		return;

	if (title != NULL && title[0] != '\0') {
		label_text = g_strndup(title, TAB_MAX_SIZE);
		while (strlen(label_text) < TAB_MIN_SIZE) {
			gchar *old_text = label_text;
			label_text = g_strconcat(label_text, " ", NULL);
			g_free(old_text);
		}
	} else {
		label_text = g_strdup_printf(_("Terminal %d"), page);
	}

	gtk_label_set_text(GTK_LABEL(tab->label), label_text);
	if (tab->page != NULL && tab->page->tab_bar_tab == tab) {
		g_free(tab->page->title);
		tab->page->title = g_strdup(label_text);
		tab->page->title_set_by_user = tab->label_set_byuser;
	}
	g_free(label_text);
	sakura_sidebar_update_tab(tab);
}


gchar *
sakura_get_term_cwd(SakuraTab *tab)
{
	char *cwd = NULL;

	if (tab != NULL && tab->pid >= 0) {
		char *file, *buf;
		struct stat sb;
		ssize_t length;

		file = g_strdup_printf("/proc/%d/cwd", tab->pid);
		if (g_stat(file, &sb) == -1) {
			g_free(file);
			return NULL;
		}

		buf = g_malloc((gsize)sb.st_size + 1);
		length = readlink(file, buf, (size_t)sb.st_size + 1);
		if (length > 0 && buf[0] == '/') {
			buf[length] = '\0';
			cwd = g_strdup(buf);
		}

		g_free(buf);
		g_free(file);
	}

	return cwd;
}


gchar *
sakura_get_term_cwd_osc7(SakuraTab *tab)
{
	gchar *cwd = NULL;
	gchar *osc7_hostname = NULL;
	const gchar *osc7_uri;
	const gchar *hostname;

	if (tab == NULL || tab->vte == NULL)
		return NULL;

	osc7_uri = vte_terminal_get_current_directory_uri(VTE_TERMINAL(tab->vte));
	if (osc7_uri == NULL)
		return NULL;

	cwd = g_filename_from_uri(osc7_uri, &osc7_hostname, NULL);
	hostname = g_get_host_name();
	if (osc7_hostname == NULL ||
	    g_ascii_strcasecmp(osc7_hostname, hostname) != 0 ||
	    g_ascii_strcasecmp(osc7_hostname, "localhost") == 0) {
		g_free(cwd);
		cwd = NULL;
	}
	g_free(osc7_hostname);
	return cwd;
}


gboolean
sakura_update_tab_cwd(SakuraTab *tab)
{
	const gchar *directory_uri;
	gchar *cwd;

	if (tab == NULL || tab->vte == NULL)
		return FALSE;

	directory_uri = vte_terminal_get_current_directory_uri(VTE_TERMINAL(tab->vte));
	cwd = directory_uri != NULL ? g_filename_from_uri(directory_uri, NULL, NULL) : NULL;
	if (cwd == NULL || cwd[0] != '/') {
		g_free(cwd);
		cwd = sakura_get_term_cwd(tab);
	}

	if (cwd == NULL || g_strcmp0(tab->cwd, cwd) == 0) {
		g_free(cwd);
		return FALSE;
	}

	g_free(tab->cwd);
	tab->cwd = cwd;
	return TRUE;
}


void
sakura_update_tab_metadata(SakuraTab *tab, const gchar *raw_title)
{
	const gchar *directory_uri;
	const gchar *local_host;
	gchar *uri_host = NULL;
	gchar *uri_cwd = NULL;

	if (tab == NULL || tab->vte == NULL)
		return;

	g_free(tab->raw_title);
	tab->raw_title = g_strdup(raw_title != NULL ? raw_title : "");
	g_free(tab->host);
	tab->host = NULL;

	directory_uri = vte_terminal_get_current_directory_uri(VTE_TERMINAL(tab->vte));
	if (directory_uri != NULL)
		uri_cwd = g_filename_from_uri(directory_uri, &uri_host, NULL);

	local_host = g_get_host_name();
	if (uri_host != NULL && uri_host[0] != '\0' &&
	    g_ascii_strcasecmp(uri_host, "localhost") != 0 &&
	    g_ascii_strcasecmp(uri_host, local_host) != 0)
		tab->host = g_strdup(uri_host);

	sakura_update_tab_cwd(tab);

	g_free(uri_cwd);
	g_free(uri_host);
	sakura_sidebar_update_tab(tab);
}


void
sakura_set_text_selection_mode(SakuraTab *tab, gboolean enabled)
{
	if (tab == NULL || tab->vte == NULL || tab->text_selection_mode == enabled)
		return;

	tab->text_selection_mode = enabled;
	/* VTE normally forwards mouse events to applications which enable mouse
	 * reporting. During selection mode, keep the child alive but let VTE own
	 * the pointer events so drag selection works in interactive TUIs. */
	vte_terminal_set_input_enabled(VTE_TERMINAL(tab->vte), !enabled);
	if (enabled)
		gtk_widget_grab_focus(tab->vte);
}


void
sakura_setname_entry_changed_cb (GtkWidget *widget, void *data)
{
	GtkDialog *dialog = GTK_DIALOG(data);
	const gchar *text;

	if (dialog == NULL || widget == NULL)
		return;
	text = gtk_entry_get_text(GTK_ENTRY(widget));
	gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_ACCEPT,
	                                  text != NULL && text[0] != '\0');
}


void
sakura_copy_cb (GtkWidget *widget, void *data)
{
	(void)widget;
	(void)data;
	sakura_copy();
}


void
sakura_paste_cb (GtkWidget *widget, void *data)
{
	(void)widget;
	(void)data;
	sakura_paste();
}


void
sakura_select_text_cb (GtkWidget *widget, void *data)
{
	SakuraTab *tab;
	gint page;

	(void)data;
	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	if (page < 0)
		return;
	tab = sakura_tab_at_page(page);
	sakura_set_text_selection_mode(
		tab, gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget)));
}


void
sakura_search_dialog (void)
{
	GtkWidget *dialog, *header, *entry, *label, *box;
	gint response;

	dialog = gtk_dialog_new_with_buttons(
		_("Search"), GTK_WINDOW(sakura.main_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR,
		_("_Cancel"), GTK_RESPONSE_CANCEL,
		_("_Apply"), GTK_RESPONSE_ACCEPT, NULL);
	header = gtk_dialog_get_header_bar(GTK_DIALOG(dialog));
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), FALSE);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

	entry = gtk_entry_new();
	label = gtk_label_new(_("Search"));
	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 12);
	gtk_box_pack_start(GTK_BOX(box), entry, TRUE, TRUE, 12);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
	                   box, FALSE, FALSE, 12);
	g_signal_connect(entry, "changed", G_CALLBACK(sakura_setname_entry_changed_cb),
	                 dialog);
	gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT, FALSE);
	gtk_widget_show_all(box);

	response = gtk_dialog_run(GTK_DIALOG(dialog));
	if (response == GTK_RESPONSE_ACCEPT)
		sakura_search(gtk_entry_get_text(GTK_ENTRY(entry)), FALSE);
	gtk_widget_destroy(dialog);
}


void
sakura_set_name_dialog_cb (GtkWidget *widget, void *data)
{
	GtkWidget *dialog, *header, *entry, *label, *box;
	SakuraTab *tab = data;
	gint page;
	const gchar *text;
	gint response;

	(void)widget;
	page = tab != NULL
	     ? sakura_page_for_tab(tab)
	     : gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	if (page < 0)
		return;
	if (tab == NULL)
		tab = sakura_tab_at_page(page);
	if (tab == NULL)
		return;

	dialog = gtk_dialog_new_with_buttons(
		_("Set tab name"), GTK_WINDOW(sakura.main_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR,
		_("_Cancel"), GTK_RESPONSE_CANCEL,
		_("_Apply"), GTK_RESPONSE_ACCEPT, NULL);
	header = gtk_dialog_get_header_bar(GTK_DIALOG(dialog));
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), FALSE);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	entry = gtk_entry_new();
	label = gtk_label_new(_("New text"));
	text = gtk_label_get_text(GTK_LABEL(tab->label));
	if (text != NULL)
		gtk_entry_set_text(GTK_ENTRY(entry), text);
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 12);
	gtk_box_pack_start(GTK_BOX(box), entry, TRUE, TRUE, 12);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
	                   box, FALSE, FALSE, 12);
	g_signal_connect(entry, "changed", G_CALLBACK(sakura_setname_entry_changed_cb),
	                 dialog);
	gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT, FALSE);
	gtk_widget_show_all(box);

	response = gtk_dialog_run(GTK_DIALOG(dialog));
	if (response == GTK_RESPONSE_ACCEPT) {
		sakura_session_accept_changes();
		sakura_set_tab_label_text(gtk_entry_get_text(GTK_ENTRY(entry)), page);
		sakura_set_window_title(gtk_entry_get_text(GTK_ENTRY(entry)));
		tab->label_set_byuser = TRUE;
		sakura_sidebar_update_tab(tab);
		sakura.main_title = NULL;
	}
	gtk_widget_destroy(dialog);
}


gboolean
sakura_tab_keypress_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	SakuraTab *tab = data;

	/* VTE handles some combinations such as Alt+Arrow itself, preventing the
	 * event from bubbling up to the window. Give Sakura's application-level
	 * shortcuts first refusal while the terminal has keyboard focus. */
	if (sakura_key_press_cb(sakura.main_window, event, NULL))
		return TRUE;

	(void)widget;
	/* Ctrl-C is the conventional terminal interrupt. Mark an active Codex
	 * turn as interrupted immediately; the next hook event can replace this
	 * optimistic state with the authoritative state from Codex. */
	if (tab != NULL && tab->kind == SAKURA_TAB_CODEX &&
	    tab->status == SAKURA_TAB_STATUS_RUNNING &&
	    event->keyval == GDK_KEY_c &&
	    (event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) == GDK_CONTROL_MASK) {
		tab->codex_interrupt_requested = TRUE;
		sakura_tab_set_status(tab, SAKURA_TAB_STATUS_INTERRUPTED, FALSE);
	}

	if (tab != NULL && tab->text_selection_mode &&
	    event->keyval == GDK_KEY_Escape) {
		sakura_set_text_selection_mode(tab, FALSE);
		return TRUE;
	}
	return FALSE;
}


void
sakura_closebutton_clicked_cb (GtkWidget *widget, void *data)
{
	SakuraTab *tab = data;
	gint page;

	(void)widget;
	page = sakura_page_for_tab(tab);
	if (page >= 0)
		sakura_close_tab(page);
}


gboolean
sakura_label_clicked_cb (GtkWidget *widget, GdkEventButton *button_event,
                         void *data)
{
	SakuraTab *tab = data;
	gint page;

	(void)widget;
	page = sakura_page_for_tab(tab);
	if (button_event == NULL || button_event->type != GDK_BUTTON_PRESS ||
	    page < 0 || tab == NULL)
		return FALSE;
	if (button_event->button == 1) {
		gtk_widget_grab_focus(tab->vte);
		return FALSE;
	}
	if (button_event->button == 3)
		return FALSE;
	sakura_close_tab(page);
	return TRUE;
}


gboolean
sakura_term_buttonreleased_cb (GtkWidget *widget,
                               GdkEventButton *button_event,
                               gpointer user_data)
{
	(void)widget;
	(void)user_data;
	if (button_event == NULL || button_event->type != GDK_BUTTON_RELEASE)
		return FALSE;
	if (sakura.copy_on_select && button_event->button == 1)
		sakura_copy();
	return FALSE;
}


gboolean
sakura_term_buttonpressed_cb (GtkWidget *widget,
                              GdkEventButton *button_event,
                              gpointer user_data)
{
	SakuraTab *tab;
	gint tag;

	if (button_event == NULL || button_event->type != GDK_BUTTON_PRESS)
		return FALSE;
	tab = sakura_tab_for_vte(VTE_TERMINAL(widget));
	if (tab == NULL)
		return FALSE;
	sakura.active_tab = tab;
	sakura.active_page = tab->page;
	if (tab->page != NULL)
		tab->page->active_tab = tab;
	if (tab->page != NULL && tab->page->panes != NULL && tab->page->panes->len <= 1)
		sakura_sidebar_queue_select_node(tab->page->sidebar_node);
	else
		sakura_sidebar_queue_select_node(tab->sidebar_node);
	sakura_sidebar_update_page(tab->page);

	sakura.current_match = vte_terminal_match_check_event(
		VTE_TERMINAL(tab->vte), (GdkEvent *)button_event, &tag);
	if (button_event->button == 1 &&
	    (((button_event->state & GDK_CONTROL_MASK) == GDK_CONTROL_MASK) ||
	     ((button_event->state & sakura.open_url_accelerator) ==
	      sakura.open_url_accelerator)) && sakura.current_match) {
		sakura_open_url_cb(NULL, NULL);
		return TRUE;
	}

	if (sakura.copy_on_select && button_event->button == sakura.paste_button) {
		sakura_paste_primary();
		return TRUE;
	}

	if (button_event->button == sakura.menu_button) {
		GtkMenu *menu = GTK_MENU(user_data);
		if (sakura.pane_menu != NULL)
			/* Refresh pane action sensitivity before the parent menu opens. */
			gtk_widget_set_sensitive(sakura.pane_menu, TRUE);
		if (sakura.pane_split_right != NULL)
			gtk_widget_set_sensitive(sakura.pane_split_right, sakura_tab_can_split(tab));
		if (sakura.pane_split_down != NULL)
			gtk_widget_set_sensitive(sakura.pane_split_down, sakura_tab_can_split(tab));
		if (sakura.pane_layout_menu != NULL)
			gtk_widget_set_sensitive(sakura.pane_layout_menu,
			                         tab->page != NULL && tab->page->panes != NULL &&
			                         tab->page->panes->len == 1 &&
			                         sakura_tab_can_split(tab));

		if (sakura.item_select_text != NULL) {
			gtk_check_menu_item_set_active(
				GTK_CHECK_MENU_ITEM(sakura.item_select_text),
				tab->text_selection_mode);
			gtk_widget_set_sensitive(sakura.item_select_text, TRUE);
		}
		if (sakura.current_match) {
			gchar *matches = NULL;
			if (vte_terminal_event_check_regex_simple(
					VTE_TERMINAL(tab->vte), (GdkEvent *)button_event,
					&sakura.mail_vteregexp, 1, 0, &matches)) {
				gtk_widget_show(sakura.item_open_mail);
				gtk_widget_hide(sakura.item_open_link);
			} else {
				gtk_widget_show(sakura.item_open_link);
				gtk_widget_hide(sakura.item_open_mail);
			}
			gtk_widget_show(sakura.item_copy_link);
			gtk_widget_show(sakura.open_link_separator);
			g_free(matches);
		} else {
			gtk_widget_hide(sakura.item_open_mail);
			gtk_widget_hide(sakura.item_open_link);
			gtk_widget_hide(sakura.item_copy_link);
			gtk_widget_hide(sakura.open_link_separator);
		}
		gtk_menu_popup_at_pointer(menu, (GdkEvent *)button_event);
		return TRUE;
	}

	return FALSE;
}


void
sakura_beep_cb (GtkWidget *widget, void *data)
{
	SakuraTab *tab = sakura_tab_for_vte(VTE_TERMINAL(widget));

	(void)data;
	if (tab != NULL)
		sakura_tab_set_status(tab, SAKURA_TAB_STATUS_READY, TRUE);
	gtk_window_set_urgency_hint(GTK_WINDOW(sakura.main_window), FALSE);
	if (!gtk_window_is_active(GTK_WINDOW(sakura.main_window)) && sakura.urgent_bell)
		gtk_window_set_urgency_hint(GTK_WINDOW(sakura.main_window), TRUE);
}


void
sakura_tab_move_relative(gint direction)
{
	gint page;
	gint pages;
	GtkWidget *child;

	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));
	if (page < 0 || page >= pages)
		return;
	child = gtk_notebook_get_nth_page(GTK_NOTEBOOK(sakura.notebook), page);
	if (direction > 0) {
		if (page < pages - 1)
			gtk_notebook_reorder_child(GTK_NOTEBOOK(sakura.notebook), child, page + 1);
	} else if (page > 0) {
		gtk_notebook_reorder_child(GTK_NOTEBOOK(sakura.notebook), child, page - 1);
	}
	sakura_tab_bar_refresh();
}


static gboolean
sakura_page_has_running_process(SakuraPage *page)
{
	guint index;

	if (page == NULL || page->panes == NULL)
		return FALSE;
	for (index = 0; index < page->panes->len; index++) {
		SakuraTab *pane = g_ptr_array_index(page->panes, index);
		VtePty *pty;
		pid_t pgid;

		if (pane == NULL || pane->vte == NULL)
			continue;
		pty = vte_terminal_get_pty(VTE_TERMINAL(pane->vte));
		pgid = pty != NULL ? tcgetpgrp(vte_pty_get_fd(pty)) : -1;
		if (pgid != -1 && pgid != pane->pid)
			return TRUE;
	}
	return FALSE;
}


gboolean
sakura_close_tab(gint page)
{
	gint pages;
	gint response;
	SakuraTab *tab;
	SakuraPage *tab_page;
	GtkWidget *dialog;
	gboolean deleted = FALSE;

	pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));
	tab = sakura_tab_at_page(page);
	if (tab == NULL)
		return FALSE;
	tab_page = tab->page;
	if (sakura_page_has_running_process(tab_page) && !sakura.less_questions) {
		dialog = gtk_message_dialog_new(
			GTK_WINDOW(sakura.main_window), GTK_DIALOG_MODAL,
			GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
			_("There is a running process in this terminal.\n\nDo you really want to close it?"));
		response = gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
		if (response == GTK_RESPONSE_YES)
			deleted = sakura_tab_delete_page(page);
	} else {
		deleted = sakura_tab_delete_page(page);
	}

	if (deleted && pages == 1) {
		sakura_session_accept_changes();
		sakura_config_done();
		sakura_destroy();
	} else if (deleted) {
		sakura_session_accept_changes();
	}
	return deleted;
}


void
sakura_child_exited_cb (GtkWidget *widget, void *data)
{
	gint page, pages;
	SakuraTab *tab;

	(void)data;
	if (sakura.workspace_mutating)
		return;
	tab = sakura_tab_for_vte(VTE_TERMINAL(widget));
	page = sakura_page_for_tab(tab);
	pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));
	if (tab == NULL || page < 0)
		return;

	/* Only write configuration to disk if this was the last tab. */
	if (pages == 1)
		sakura_config_done();
	if (tab->hold) {
		sakura_tab_set_status(tab, SAKURA_TAB_STATUS_READY, TRUE);
		return;
	}

	/* The child is automatically reaped because we did not request a child
	 * watch with G_SPAWN_DO_NOT_REAP_CHILD. */
	g_spawn_close_pid(tab->pid);
	if (tab->page != NULL && tab->page->panes != NULL &&
	    tab->page->panes->len > 1)
		sakura_tab_delete_pane(tab);
	else
		sakura_tab_delete_page(page);
	if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook)) == 0)
		sakura_destroy();
}


void
sakura_eof_cb (GtkWidget *widget, void *data)
{
	(void)widget;
	(void)data;
}


gboolean
sakura_notebook_focus_cb (GtkWindow *window, GdkEvent *event, void *data)
{
	SakuraTab *tab;
	gint page;

	(void)window;
	(void)data;
	if (event == NULL || event->type != GDK_FOCUS_CHANGE)
		return FALSE;

	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	tab = sakura_tab_at_page(page);
	if (tab == NULL)
		return FALSE;

	/* When clicking a tab label, the notebook may receive focus instead of the
	 * terminal. Return focus to the active content surface. */
#ifdef HAVE_WEBKITGTK
	if (tab->browser != NULL) {
		gtk_widget_grab_focus(tab->browser);
		return FALSE;
	}
#endif
	gtk_widget_grab_focus(tab->vte);
	return FALSE;
}


gboolean
sakura_pane_focus_in_cb(GtkWidget *widget, GdkEventFocus *event, gpointer data)
{
	SakuraTab *tab = data != NULL ? data : sakura_tab_for_vte(VTE_TERMINAL(widget));

	(void)event;
	if (tab == NULL || tab->page == NULL)
		return FALSE;
	sakura.active_tab = tab;
	sakura.active_page = tab->page;
	tab->page->active_tab = tab;
	sakura_tab_clear_attention(tab);
	if (tab->page->panes != NULL && tab->page->panes->len <= 1)
		sakura_sidebar_queue_select_node(tab->page->sidebar_node);
	else
		sakura_sidebar_queue_select_node(tab->sidebar_node);
	sakura_sidebar_update_page(tab->page);
	sakura_tab_bar_refresh();
	return FALSE;
}


void
sakura_tab_title_changed_cb(GtkWidget *widget, void *data)
{
	SakuraTab *tab;
	const gchar *title;
	gint page;

	(void)data;
	tab = sakura_tab_for_vte(VTE_TERMINAL(widget));
	page = sakura_page_for_tab(tab);
	if (tab == NULL || page < 0)
		return;

	title = vte_terminal_get_window_title(VTE_TERMINAL(tab->vte));
	sakura_update_tab_metadata(tab, title);

	/* User-set values override terminal-provided titles. */
	if (!tab->label_set_byuser && tab->kind != SAKURA_TAB_TOOL) {
		sakura_set_tab_label_text(title, page);
		if (!sakura.main_title && tab == sakura.active_tab)
			sakura_set_window_title(title);
	}
}


void
sakura_tab_free(SakuraTab *tab)
{
	if (tab == NULL)
		return;
	if (tab->codex_name_retry_source_id != 0) {
		g_source_remove(tab->codex_name_retry_source_id);
		tab->codex_name_retry_source_id = 0;
	}
	if (tab->vte != NULL)
		g_object_remove_weak_pointer(G_OBJECT(tab->vte),
		                             (gpointer *)&tab->vte);

	g_free(tab->cwd);
	g_free(tab->host);
	g_free(tab->raw_title);
	g_free(tab->terminal_id);
	g_free(tab->tool_target);
	g_free(tab->codex_session_id);
	g_free(tab->codex_session_name);
	g_free(tab->codex_reasoning_effort);
	g_free(tab->codex_tracking_token);
	g_free(tab);
}

void
sakura_tab_set_status(SakuraTab *tab, SakuraTabStatus status, gboolean attention)
{
	gboolean event_attention = attention;
	gboolean visible_attention = attention;

	if (tab == NULL)
		return;

	/* A state change in the visible, focused terminal is already visible to the
	 * user. Keep the state marker, but don't turn it into an unread alert. */
	if (visible_attention && sakura.main_window != NULL &&
	    sakura_tab_is_current(tab) &&
	    gtk_window_is_active(GTK_WINDOW(sakura.main_window)))
		visible_attention = FALSE;

	if (tab->status == status && tab->attention == visible_attention)
		return;
	if (event_attention && (tab->status != status || !tab->attention))
		tab->attention_timestamp = g_get_real_time();

	tab->status = status;
	tab->attention = visible_attention;
	if (tab->spinner != NULL) {
		if (status == SAKURA_TAB_STATUS_RUNNING) {
			gtk_widget_show(tab->spinner);
			gtk_spinner_start(GTK_SPINNER(tab->spinner));
		} else {
			gtk_spinner_stop(GTK_SPINNER(tab->spinner));
			gtk_widget_hide(tab->spinner);
		}
	}
	sakura_sidebar_update_tab(tab);
	sakura_sidebar_update_attention_count();

	if (visible_attention && sakura.main_window != NULL &&
	    !gtk_window_is_active(GTK_WINDOW(sakura.main_window)) && sakura.urgent_bell)
		gtk_window_set_urgency_hint(GTK_WINDOW(sakura.main_window), TRUE);
}

void
sakura_tab_clear_attention(SakuraTab *tab)
{
	if (tab == NULL || !tab->attention)
		return;

	tab->attention = FALSE;
	sakura_sidebar_update_tab(tab);
	sakura_sidebar_update_attention_count();
	sakura_session_mark_dirty();
}


void
sakura_tab_restore_state(SakuraTab *tab, SakuraTabStatus status,
                         gboolean attention, gint64 attention_timestamp)
{
	if (tab == NULL)
		return;
	if (status <= SAKURA_TAB_STATUS_NONE || status > SAKURA_TAB_STATUS_ERROR)
		status = tab->status;

	tab->status = status;
	tab->attention = attention;
	tab->attention_timestamp = attention_timestamp > 0 ? attention_timestamp : 0;
	if (tab->spinner != NULL) {
		if (status == SAKURA_TAB_STATUS_RUNNING) {
			gtk_widget_show(tab->spinner);
			gtk_spinner_start(GTK_SPINNER(tab->spinner));
		} else {
			gtk_spinner_stop(GTK_SPINNER(tab->spinner));
			gtk_widget_hide(tab->spinner);
		}
	}
	sakura_sidebar_update_tab(tab);
	sakura_sidebar_update_attention_count();
}


void
sakura_tab_bar_refresh(void)
{
	guint visible_count, order = 0;
	gint page, pages, current_page;
	gboolean show_tabs;
	const gchar *scope_title;
	gchar *scope_label;

	if (sakura.tab_bar == NULL || sakura.notebook == NULL)
		return;
	/* Changing the notebook page emits switch-page synchronously. During
	 * startup GTK can still report no current page while emitting that signal,
	 * so let the outer refresh finish instead of recursively refreshing until
	 * the stack overflows. */
	if (sakura.tab_bar_refreshing)
		return;
	sakura.tab_bar_refreshing = TRUE;

	visible_count = sakura_tab_bar_visible_count();
	scope_title = sakura.active_task != NULL && sakura.active_task->title != NULL
	            ? sakura.active_task->title
	            : sakura.active_group_scope != NULL &&
	              sakura.active_group_scope->title != NULL
	            ? sakura.active_group_scope->title
	            : _("All terminals");
	scope_label = g_strdup_printf(_("%s (%u)"), scope_title, visible_count);
	gtk_label_set_text(GTK_LABEL(sakura.tab_bar_scope_label), scope_label);
	g_free(scope_label);

	pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));
	current_page = sakura.active_tab != NULL
	             ? sakura_page_for_tab(sakura.active_tab)
	             : -1;

	for (page = 0; page < pages; page++) {
		SakuraTab *tab = sakura_tab_at_page(page);
		gboolean visible = sakura_tab_is_in_active_scope(tab);

		if (tab == NULL)
			continue;
		sakura_tab_bar_update_tab(tab);
		if (tab->tab_item != NULL) {
			gtk_widget_set_visible(tab->tab_item, visible);
			gtk_box_reorder_child(GTK_BOX(sakura.tab_bar), tab->tab_item, order++);
			GtkStyleContext *style_context =
				gtk_widget_get_style_context(tab->tab_item);
			if (visible && page == current_page)
				gtk_style_context_add_class(style_context, "selected");
			else
				gtk_style_context_remove_class(style_context, "selected");
		}
	}

	show_tabs = visible_count > 0 &&
	            (sakura.show_tab_bar == SHOW_TAB_BAR_ALWAYS ||
	             (sakura.show_tab_bar == SHOW_TAB_BAR_MULTIPLE && visible_count > 1));
	gtk_widget_set_visible(sakura.tab_bar_scrolled, show_tabs);
	gtk_widget_set_visible(sakura.tab_bar_shell, show_tabs || visible_count == 0);
	gtk_widget_set_visible(sakura.notebook, visible_count > 0);
	gtk_widget_set_visible(sakura.tab_bar_empty, visible_count == 0);

	if (sakura.tab_bar_new_button != NULL)
		gtk_widget_set_visible(sakura.tab_bar_new_button, show_tabs);
	sakura.tab_bar_refreshing = FALSE;
}

guint
sakura_tab_bar_visible_count(void)
{
	guint count = 0;
	guint page;

	if (sakura.tabs == NULL)
		return 0;

	for (page = 0; page < sakura.tabs->len; page++) {
		SakuraTab *tab = sakura_tab_at_page((gint)page);
		if (sakura_tab_is_in_active_scope(tab))
			count++;
	}
	return count;
}

gint
sakura_tab_bar_nth_visible_page(guint visible_index)
{
	guint visible = 0;
	guint page;

	if (sakura.tabs == NULL)
		return -1;

	for (page = 0; page < sakura.tabs->len; page++) {
		SakuraTab *tab = sakura_tab_at_page((gint)page);
		if (!sakura_tab_is_in_active_scope(tab))
			continue;
		if (visible++ == visible_index)
			return (gint)page;
	}
	return -1;
}

gboolean
sakura_tab_bar_select_relative(gint direction)
{
	guint count, visible_index = 0;
	gint page, current_page;

	count = sakura_tab_bar_visible_count();
	if (count == 0 || sakura.notebook == NULL)
		return FALSE;

	current_page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	for (page = 0; page < current_page; page++) {
		SakuraTab *tab = sakura_tab_at_page(page);
		if (sakura_tab_is_in_active_scope(tab))
			visible_index++;
	}
	if (current_page < 0 || !sakura_tab_is_in_active_scope(
			current_page >= 0 ? sakura_tab_at_page(current_page) : NULL))
		visible_index = direction < 0 ? count - 1 : 0;
	else if (direction < 0)
		visible_index = visible_index == 0 ? count - 1 : visible_index - 1;
	else
		visible_index = (visible_index + 1) % count;

	page = sakura_tab_bar_nth_visible_page(visible_index);
	if (page < 0)
		return FALSE;
	sakura_select_tab(sakura_tab_at_page(page), FALSE);
	return TRUE;
}

static void
sakura_tab_button_clicked_cb(GtkWidget *widget, void *data)
{
	SakuraTab *tab = data;

	(void)widget;
	if (tab == NULL || tab->hbox == NULL)
		return;
	sakura_select_tab(tab, TRUE);
}

static void
sakura_tab_button_close_cb(GtkWidget *widget, void *data)
{
	SakuraTab *tab = data;
	gint page;

	(void)widget;
	if (tab == NULL || tab->hbox == NULL)
		return;
	page = sakura_page_for_tab(tab);
	if (page >= 0)
		sakura_close_tab(page);
}

void
sakura_tab_bar_update_tab(SakuraTab *tab)
{
	const gchar *status_color, *status_symbol;
	const gchar *tooltip;
	GtkWidget *status_slot;

	if (tab == NULL || tab->tab_button == NULL)
		return;

	if (tab->sidebar_node != NULL && tab->sidebar_node->title != NULL)
		gtk_label_set_text(GTK_LABEL(tab->tab_button_label), tab->sidebar_node->title);
	else
		gtk_label_set_text(GTK_LABEL(tab->tab_button_label), _("Terminal"));
	status_slot = gtk_widget_get_parent(tab->tab_button_status);

	tooltip = tab->sidebar_node != NULL && tab->sidebar_node->tooltip != NULL
	        ? tab->sidebar_node->tooltip
	        : gtk_label_get_text(GTK_LABEL(tab->tab_button_label));
	gtk_widget_set_tooltip_text(tab->tab_button, tooltip);

	status_color = sakura_tab_status_color(tab->status);
	status_symbol = sakura_tab_status_symbol(tab->status);
	if (tab->status == SAKURA_TAB_STATUS_RUNNING) {
		gtk_widget_show(status_slot);
		gtk_widget_hide(tab->tab_button_status);
		gtk_widget_show(tab->tab_button_spinner);
		gtk_spinner_start(GTK_SPINNER(tab->tab_button_spinner));
	} else {
		gtk_spinner_stop(GTK_SPINNER(tab->tab_button_spinner));
		gtk_widget_hide(tab->tab_button_spinner);
		if (status_color != NULL && status_symbol != NULL) {
			gchar *status_markup = g_strdup_printf("<span foreground=\"%s\">%s</span>",
			                                       status_color, status_symbol);
			gtk_label_set_markup(GTK_LABEL(tab->tab_button_status), status_markup);
			g_free(status_markup);
			gtk_widget_show(status_slot);
			gtk_widget_show(tab->tab_button_status);
		} else {
			gtk_widget_hide(status_slot);
			gtk_widget_hide(tab->tab_button_status);
		}
	}
}

static gboolean
sakura_tab_button_button_press_cb(GtkWidget *widget, GdkEventButton *event,
                                  void *data)
{
	SakuraTab *tab = data;
	GtkWidget *menu, *item;

	(void)widget;
	if (event->button != GDK_BUTTON_SECONDARY || tab == NULL)
		return FALSE;

	menu = gtk_menu_new();
	if (tab->kind == SAKURA_TAB_TOOL && tab->tool == SAKURA_TOOL_GH_PR) {
		item = gtk_menu_item_new_with_label(_("Copy pull request URL"));
		gtk_widget_set_sensitive(item,
		                         tab->tool_target != NULL && tab->tool_target[0] != '\0');
		g_signal_connect(item, "activate", G_CALLBACK(sakura_copy_pr_url_cb), tab);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	}
	item = gtk_menu_item_new_with_label(_("Rename terminal..."));
	g_signal_connect(item, "activate", G_CALLBACK(sakura_set_name_dialog_cb), tab);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	item = gtk_menu_item_new_with_label(_("Close terminal"));
	g_signal_connect(item, "activate", G_CALLBACK(sakura_tab_button_close_cb), tab);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	gtk_widget_show_all(menu);
	gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
	return TRUE;
}

void
sakura_tab_bar_add_tab(SakuraTab *tab)
{
	GtkWidget *button_box, *button, *status_slot, *status, *spinner, *label;
	GtkWidget *close_button, *image, *tool_icon = NULL;

	if (sakura.tab_bar == NULL || tab == NULL)
		return;
	if (tab->page != NULL && tab->page->tab_bar_tab != tab)
		return;

	button = gtk_button_new();
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_widget_set_can_focus(button, TRUE);
	button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
	status_slot = gtk_overlay_new();
	gtk_widget_set_size_request(status_slot, 16, 16);
	status = gtk_label_new(NULL);
	spinner = gtk_spinner_new();
	gtk_widget_set_size_request(status, 16, 16);
	gtk_widget_set_size_request(spinner, 16, 16);
	gtk_widget_set_no_show_all(spinner, TRUE);
	gtk_container_add(GTK_CONTAINER(status_slot), status);
	gtk_overlay_add_overlay(GTK_OVERLAY(status_slot), spinner);
	if (tab->kind == SAKURA_TAB_TOOL) {
		tool_icon = gtk_image_new_from_icon_name(sakura_tool_icon_name(tab->tool),
		                                         GTK_ICON_SIZE_MENU);
		gtk_box_pack_start(GTK_BOX(button_box), tool_icon, FALSE, FALSE, 0);
	}
	label = gtk_label_new(_("Terminal"));
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
	gtk_box_pack_start(GTK_BOX(button_box), label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(button_box), status_slot, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(button), button_box);

	tab->tab_item = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	tab->tab_button = button;
	tab->tab_button_icon = tool_icon;
	tab->tab_button_label = label;
	tab->tab_button_status = status;
	tab->tab_button_spinner = spinner;
	gtk_style_context_add_class(gtk_widget_get_style_context(tab->tab_item),
	                            "sakura-tab");
	gtk_box_pack_start(GTK_BOX(tab->tab_item), button, FALSE, FALSE, 0);

	if (sakura.show_closebutton) {
		close_button = gtk_button_new();
		gtk_button_set_relief(GTK_BUTTON(close_button), GTK_RELIEF_NONE);
		gtk_widget_set_name(close_button, "closebutton");
		gtk_style_context_add_class(gtk_widget_get_style_context(close_button),
		                            "sakura-tab-close");
		gtk_widget_set_tooltip_text(close_button, _("Close terminal"));
		image = gtk_image_new_from_icon_name("window-close", GTK_ICON_SIZE_MENU);
		gtk_container_add(GTK_CONTAINER(close_button), image);
		gtk_box_pack_start(GTK_BOX(tab->tab_item), close_button, FALSE, FALSE, 0);
		tab->tab_button_close = close_button;
		g_signal_connect(close_button, "clicked", G_CALLBACK(sakura_tab_button_close_cb), tab);
	}

	gtk_box_pack_start(GTK_BOX(sakura.tab_bar), tab->tab_item, FALSE, FALSE, 0);
	g_signal_connect(button, "clicked", G_CALLBACK(sakura_tab_button_clicked_cb), tab);
	gtk_widget_add_events(button, GDK_BUTTON_PRESS_MASK);
	g_signal_connect(button, "button-press-event",
	                 G_CALLBACK(sakura_tab_button_button_press_cb), tab);
	gtk_widget_show_all(tab->tab_item);
	gtk_widget_hide(status_slot);
	gtk_widget_hide(status);
	gtk_widget_hide(spinner);
	sakura_tab_bar_update_tab(tab);
}

void
sakura_tab_bar_remove_tab(SakuraTab *tab)
{
	if (tab == NULL || tab->tab_item == NULL || sakura.tab_bar == NULL)
		return;
	gtk_container_remove(GTK_CONTAINER(sakura.tab_bar), tab->tab_item);
	tab->tab_item = NULL;
	tab->tab_button = NULL;
	tab->tab_button_icon = NULL;
	tab->tab_button_label = NULL;
	tab->tab_button_status = NULL;
	tab->tab_button_spinner = NULL;
	tab->tab_button_close = NULL;
}

SakuraPage *
sakura_page_at_page(gint page)
{
	GtkWidget *child;
	guint index;

	if (sakura.notebook == NULL || sakura.pages == NULL || page < 0)
		return NULL;
	child = gtk_notebook_get_nth_page(GTK_NOTEBOOK(sakura.notebook), page);
	if (child == NULL)
		return NULL;
	for (index = 0; index < sakura.pages->len; index++) {
		SakuraPage *candidate = g_ptr_array_index(sakura.pages, index);
		if (candidate != NULL && candidate->container == child)
			return candidate;
	}
	return NULL;
}

SakuraTab *
sakura_tab_at_page(gint page)
{
	SakuraPage *notebook_page = sakura_page_at_page(page);

	if (notebook_page != NULL)
		return notebook_page->tab_bar_tab != NULL
		     ? notebook_page->tab_bar_tab : notebook_page->active_tab;
	if (sakura.tabs == NULL || page < 0 || (guint)page >= sakura.tabs->len)
		return NULL;
	return g_ptr_array_index(sakura.tabs, page);
}

gboolean
sakura_notebook_sync_page_order(void)
{
	GPtrArray *ordered_pages, *ordered_tabs;
	gint count, index;

	if (sakura.notebook == NULL || sakura.pages == NULL || sakura.tabs == NULL)
		return FALSE;
	count = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));
	if (count < 0 || (guint)count != sakura.pages->len ||
	    (guint)count != sakura.tabs->len)
		return FALSE;

	ordered_pages = g_ptr_array_sized_new((guint)count);
	ordered_tabs = g_ptr_array_sized_new((guint)count);
	for (index = 0; index < count; index++) {
		SakuraPage *page = sakura_page_at_page(index);
		if (page == NULL || page->tab_bar_tab == NULL) {
			g_ptr_array_unref(ordered_pages);
			g_ptr_array_unref(ordered_tabs);
			return FALSE;
		}
		g_ptr_array_add(ordered_pages, page);
		g_ptr_array_add(ordered_tabs, page->tab_bar_tab);
	}

	g_ptr_array_set_size(sakura.pages, 0);
	g_ptr_array_set_size(sakura.tabs, 0);
	for (index = 0; index < count; index++) {
		g_ptr_array_add(sakura.pages, g_ptr_array_index(ordered_pages, index));
		g_ptr_array_add(sakura.tabs, g_ptr_array_index(ordered_tabs, index));
	}
	g_ptr_array_unref(ordered_pages);
	g_ptr_array_unref(ordered_tabs);
	return TRUE;
}

gboolean
sakura_notebook_detach_page(SakuraPage *page)
{
	gint notebook_page;

	if (page == NULL || page->container == NULL || sakura.notebook == NULL)
		return FALSE;
	notebook_page = gtk_notebook_page_num(GTK_NOTEBOOK(sakura.notebook),
	                                     page->container);
	if (notebook_page < 0)
		return FALSE;
	if (sakura.tabs != NULL)
		g_ptr_array_remove(sakura.tabs, page->tab_bar_tab);
	if (sakura.pages != NULL)
		g_ptr_array_remove(sakura.pages, page);
	gtk_notebook_remove_page(GTK_NOTEBOOK(sakura.notebook), notebook_page);
	return TRUE;
}

gint
sakura_page_for_tab(SakuraTab *tab)
{
	gint notebook_page;
	guint index;

	if (tab == NULL)
		return -1;
	/* GTK's child order is the authority for what set_current_page() will
	 * display. The cached pages array can temporarily differ during restore or
	 * notebook reordering, so never use its index to drive visible selection. */
	if (tab->page != NULL && tab->page->container != NULL &&
	    sakura.notebook != NULL) {
		notebook_page = gtk_notebook_page_num(GTK_NOTEBOOK(sakura.notebook),
		                                      tab->page->container);
		if (notebook_page >= 0)
			return notebook_page;
	}
	if (tab->page != NULL && sakura.pages != NULL) {
		for (index = 0; index < sakura.pages->len; index++) {
			SakuraPage *page = g_ptr_array_index(sakura.pages, index);
			if (page == tab->page)
				return (gint)index;
		}
	}
	if (sakura.tabs == NULL)
		return -1;
	for (index = 0; index < sakura.tabs->len; index++) {
		if (g_ptr_array_index(sakura.tabs, index) == tab)
			return (gint)index;
	}
	return -1;
}

gint
sakura_find_tab_by_terminal_id(const gchar *terminal_id)
{
	SakuraTab *tab;

	tab = sakura_find_pane_by_terminal_id(terminal_id);
	return tab != NULL ? sakura_page_for_tab(tab) : -1;
}

void
sakura_notebook_page_reordered_cb(GtkNotebook *notebook, GtkWidget *child,
                                  guint page_num, void *data)
{
	(void)notebook;
	(void)child;
	(void)page_num;
	(void)data;
	if (sakura_notebook_sync_page_order())
		sakura_session_mark_dirty();
}
