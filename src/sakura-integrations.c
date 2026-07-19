#include <libintl.h>
#include <string.h>

#include <gio/gio.h>
#include <glib/gstdio.h>

#include "sakura-private.h"

#define _(String) gettext(String)
#define GIT_ICON_NAME "git"
#define GITHUB_ICON_NAME "github"
#define SAKURA_CODEX_HELPER_PROTOCOL_VERSION "v1"
#define SAKURA_CODEX_HELPER_OVERRIDE_ENV "SAKURA_CODEX_HELPER"
#define SAKURA_CODEX_HELPER_NAME "sakura-codex-session-name"

const gchar *
sakura_tool_label(SakuraToolKind tool)
{
	switch (tool) {
		case SAKURA_TOOL_GITUI:
			return _("GitUI");
		case SAKURA_TOOL_GH_DASH:
			return _("GitHub Dashboard");
		case SAKURA_TOOL_GH_PR:
			return _("Pull request");
		case SAKURA_TOOL_GIT_COLA:
			return _("Git Cola");
		case SAKURA_TOOL_NONE:
		default:
			return _("Tool");
	}
}


const gchar *
sakura_tool_id(SakuraToolKind tool)
{
	switch (tool) {
		case SAKURA_TOOL_GITUI:
			return "gitui";
		case SAKURA_TOOL_GH_DASH:
			return "gh-dash";
		case SAKURA_TOOL_GH_PR:
			return "gh-pr";
		case SAKURA_TOOL_GIT_COLA:
			return "git-cola";
		case SAKURA_TOOL_NONE:
		default:
			return NULL;
	}
}


SakuraToolKind
sakura_tool_from_id(const gchar *id)
{
	if (g_strcmp0(id, "gitui") == 0)
		return SAKURA_TOOL_GITUI;
	if (g_strcmp0(id, "gh-dash") == 0)
		return SAKURA_TOOL_GH_DASH;
	if (g_strcmp0(id, "gh-pr") == 0)
		return SAKURA_TOOL_GH_PR;
	if (g_strcmp0(id, "git-cola") == 0)
		return SAKURA_TOOL_GIT_COLA;
	return SAKURA_TOOL_NONE;
}


const gchar *
sakura_tool_executable(SakuraToolKind tool)
{
	switch (tool) {
		case SAKURA_TOOL_GITUI:
			return "gitui";
		case SAKURA_TOOL_GH_DASH:
		case SAKURA_TOOL_GH_PR:
			return "gh";
		case SAKURA_TOOL_GIT_COLA:
			return "git-cola";
		case SAKURA_TOOL_NONE:
		default:
			return NULL;
	}
}


const gchar *
sakura_tool_icon_name(SakuraToolKind tool)
{
	switch (tool) {
		case SAKURA_TOOL_GITUI:
		case SAKURA_TOOL_GIT_COLA:
			return GIT_ICON_NAME;
		case SAKURA_TOOL_GH_DASH:
		case SAKURA_TOOL_GH_PR:
			return GITHUB_ICON_NAME;
		case SAKURA_TOOL_NONE:
		default:
			return "utilities-terminal";
	}
}


gboolean
sakura_codex_reasoning_effort_is_valid(const gchar *value)
{
	return g_strcmp0(value, "minimal") == 0 ||
	       g_strcmp0(value, "low") == 0 ||
	       g_strcmp0(value, "medium") == 0 ||
	       g_strcmp0(value, "high") == 0 ||
	       g_strcmp0(value, "xhigh") == 0;
}


const gchar *
sakura_codex_reasoning_effort_label(const gchar *value)
{
	if (g_strcmp0(value, "minimal") == 0)
		return _("Minimal");
	if (g_strcmp0(value, "low") == 0)
		return _("Fast");
	if (g_strcmp0(value, "medium") == 0)
		return _("Balanced");
	if (g_strcmp0(value, "high") == 0)
		return _("Deep");
	if (g_strcmp0(value, "xhigh") == 0)
		return _("Max");
	return _("Default");
}


gboolean
sakura_tool_requires_git_repository(SakuraToolKind tool)
{
	return tool == SAKURA_TOOL_GITUI || tool == SAKURA_TOOL_GIT_COLA;
}


gchar *
sakura_find_tool_executable(SakuraToolKind tool)
{
	const gchar *executable = sakura_tool_executable(tool);
	const gchar *cargo_home;
	gchar *path;

	if (executable == NULL)
		return NULL;

	/* Prefer PATH so distro/package-managed installations keep precedence. */
	path = g_find_program_in_path(executable);
	if (path != NULL)
		return path;

	/* Desktop launches often do not inherit the shell's PATH. Rust's default
	 * install location is ~/.cargo/bin, with CARGO_HOME as the override. */
	if (tool == SAKURA_TOOL_GITUI) {
		cargo_home = g_getenv("CARGO_HOME");
		if (cargo_home != NULL && cargo_home[0] != '\0') {
			path = g_build_filename(cargo_home, "bin", executable, NULL);
			if (g_file_test(path, G_FILE_TEST_IS_REGULAR | G_FILE_TEST_IS_EXECUTABLE))
				return path;
			g_free(path);
		}

		path = g_build_filename(g_get_home_dir(), ".cargo", "bin", executable, NULL);
		if (g_file_test(path, G_FILE_TEST_IS_REGULAR | G_FILE_TEST_IS_EXECUTABLE))
			return path;
		g_free(path);
	}

	return NULL;
}


gboolean
sakura_tool_is_available(SakuraToolKind tool)
{
	gchar *path;
	gboolean available;

#ifdef HAVE_WEBKITGTK
	/* PR tabs can be rendered directly by WebKitGTK, so the GitHub CLI is
	 * optional in this build. It remains required for the terminal fallback. */
	if (tool == SAKURA_TOOL_GH_PR)
		return TRUE;
#endif

	path = sakura_find_tool_executable(tool);
	if (path == NULL)
		return FALSE;
	g_free(path);
	if (tool != SAKURA_TOOL_GH_DASH)
		return TRUE;

	{
		gchar *argv[] = { (gchar *)"gh", (gchar *)"dash", (gchar *)"--version", NULL };
		GError *error = NULL;
		gint status = 1;

		available = g_spawn_sync(NULL, argv, NULL,
		                         G_SPAWN_SEARCH_PATH |
		                         G_SPAWN_STDOUT_TO_DEV_NULL |
		                         G_SPAWN_STDERR_TO_DEV_NULL,
		                         NULL, NULL, NULL, NULL, &status, &error) &&
		             status == 0;
		g_clear_error(&error);
	}
	return available;
}

#ifndef SAKURA_CORE_TEST
static gchar *
sakura_current_tab_cwd(void)
{
	gint page;
	SakuraTab *tab;
	gchar *cwd = NULL;

	page = sakura.notebook != NULL
	     ? gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook)) : -1;
	if (page >= 0) {
		tab = sakura_tab_at_page(page);
		cwd = sakura_get_term_cwd_osc7(tab);
		if (cwd == NULL && tab != NULL && tab->cwd != NULL)
			cwd = g_strdup(tab->cwd);
		if (cwd == NULL)
			cwd = sakura_get_term_cwd(tab);
	}
	return cwd != NULL ? cwd : g_get_current_dir();
}


static gchar *
sakura_context_cwd(void)
{
	gchar *cwd = sakura_sidebar_directory_for_node(sakura.active_group_scope);

	return cwd != NULL ? cwd : sakura_current_tab_cwd();
}


void
sakura_open_here_cb(GtkWidget *widget, void *data)
{
	SakuraOpenHereKind kind = GPOINTER_TO_INT(data);
	const gchar *configured_editor;
	const gchar *editor_candidates[] = {
		"code", "codium", "zed", "subl", "gnome-text-editor",
		"gedit", "xed", "pluma", "mousepad", "kate", NULL
	};
	gchar *cwd = NULL;
	gchar **argv = NULL;
	gchar *program = NULL;
	GError *error = NULL;
	gint argc = 0;
	gboolean directory_argument = FALSE;
	guint i;

	(void)widget;
	cwd = sakura_context_cwd();
	if (cwd == NULL || !g_file_test(cwd, G_FILE_TEST_IS_DIR)) {
		sakura_error(_("Could not determine the current directory."));
		g_free(cwd);
		return;
	}

	if (kind == SAKURA_OPEN_HERE_FILE_MANAGER) {
		program = g_find_program_in_path("gio");
		if (program != NULL) {
			argv = g_new0(gchar *, 4);
			argv[0] = program;
			argv[1] = g_strdup("open");
			argv[2] = g_strdup(cwd);
		} else {
			program = g_find_program_in_path("xdg-open");
			if (program != NULL) {
				argv = g_new0(gchar *, 3);
				argv[0] = program;
				argv[1] = g_strdup(cwd);
			}
		}
		if (argv == NULL) {
			sakura_error(_("Could not find a file manager opener (gio or xdg-open)."));
			g_free(cwd);
			return;
		}
	} else {
		configured_editor = sakura.editor_command;
		if (configured_editor == NULL || configured_editor[0] == '\0')
			configured_editor = g_getenv("SAKURA_EDITOR");

		if (configured_editor != NULL && configured_editor[0] != '\0') {
			if (!g_shell_parse_argv(configured_editor, &argc, &argv, &error) ||
			    argc == 0) {
				sakura_error(_("Could not parse the configured editor command: %s"),
				             error != NULL ? error->message : _("empty command"));
				g_clear_error(&error);
				g_strfreev(argv);
				g_free(cwd);
				return;
			}
			for (i = 0; i < (guint)argc; i++) {
				if (g_strcmp0(argv[i], "{directory}") == 0) {
					g_free(argv[i]);
					argv[i] = g_strdup(cwd);
					directory_argument = TRUE;
				}
			}
			if (!directory_argument) {
				argv = g_realloc(argv, sizeof(gchar *) * ((gsize)argc + 2));
				argv[argc] = g_strdup(cwd);
				argv[argc + 1] = NULL;
			}
		} else {
			for (i = 0; editor_candidates[i] != NULL; i++) {
				program = g_find_program_in_path(editor_candidates[i]);
				if (program != NULL)
					break;
			}
			if (program == NULL) {
				sakura_error(_("No graphical editor was found. Set editor_command in Sakura's config or SAKURA_EDITOR."));
				g_free(cwd);
				return;
			}
			argv = g_new0(gchar *, 3);
			argv[0] = program;
			argv[1] = g_strdup(cwd);
		}
	}

	if (!g_spawn_async(cwd, argv, NULL, G_SPAWN_SEARCH_PATH,
	                   NULL, NULL, NULL, &error)) {
		sakura_error(_("Could not open %s: %s"),
		             kind == SAKURA_OPEN_HERE_FILE_MANAGER
		             ? _("the file manager") : _("the editor"),
		             error != NULL ? error->message : _("unknown error"));
		g_clear_error(&error);
	}

	g_strfreev(argv);
	g_free(cwd);
}


GtkWidget *
sakura_open_here_menu_new(void)
{
	GtkWidget *menu, *item;

	menu = gtk_menu_new();
	item = gtk_menu_item_new_with_label(_("File Manager"));
	g_signal_connect(item, "activate", G_CALLBACK(sakura_open_here_cb),
	                 GINT_TO_POINTER(SAKURA_OPEN_HERE_FILE_MANAGER));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	item = gtk_menu_item_new_with_label(_("Editor"));
	g_signal_connect(item, "activate", G_CALLBACK(sakura_open_here_cb),
	                 GINT_TO_POINTER(SAKURA_OPEN_HERE_EDITOR));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	return menu;
}


void
sakura_new_tool_cb(GtkWidget *widget, void *data)
{
	SakuraToolKind tool = GPOINTER_TO_INT(data);
	gchar *cwd = NULL, *tool_cwd = NULL;
	gchar *standard_output = NULL, *standard_error = NULL;
	gchar *git_argv[6];
	GError *error = NULL;
	gint status = 0;
	SakuraTab *existing;

	(void)widget;
	if (tool == SAKURA_TOOL_NONE)
		return;
	if (!sakura_tool_is_available(tool)) {
		if (tool == SAKURA_TOOL_GH_DASH)
			sakura_error(_("GitHub Dashboard is not installed. Run: gh extension install dlvhdr/gh-dash"));
		else if (tool == SAKURA_TOOL_GIT_COLA)
			sakura_error(_("Git Cola is not installed. Install the git-cola package and try again."));
		else
			sakura_error(_("GitUI is not installed. Install it and try again."));
		return;
	}

	cwd = sakura_context_cwd();
	tool_cwd = cwd;
	if (sakura_tool_requires_git_repository(tool)) {
		git_argv[0] = (gchar *)"git";
		git_argv[1] = (gchar *)"-C";
		git_argv[2] = cwd;
		git_argv[3] = (gchar *)"rev-parse";
		git_argv[4] = (gchar *)"--show-toplevel";
		git_argv[5] = NULL;
		if (!g_spawn_sync(NULL, git_argv, NULL, G_SPAWN_SEARCH_PATH,
		                  NULL, NULL, &standard_output, &standard_error,
		                  &status, &error) || status != 0) {
			sakura_error(_("The current directory is not inside a Git repository."));
			g_clear_error(&error);
			g_free(standard_output);
			g_free(standard_error);
			g_free(cwd);
			return;
		}

		if (standard_output == NULL) {
			sakura_error(_("Could not determine the Git repository root."));
			g_free(standard_error);
			g_free(cwd);
			return;
		}
		g_strstrip(standard_output);
		if (standard_output[0] == '\0' ||
		    !g_file_test(standard_output, G_FILE_TEST_IS_DIR)) {
			sakura_error(_("Could not determine the Git repository root."));
			g_free(standard_output);
			g_free(standard_error);
			g_free(cwd);
			return;
		}
		tool_cwd = g_strdup(standard_output);
	}

	existing = sakura_find_tool_tab(tool, tool_cwd);
	if (existing != NULL) {
		sakura_select_tab(existing, TRUE);
		sakura_tab_clear_attention(existing);
		if (tool_cwd != cwd)
			g_free(tool_cwd);
		g_free(standard_output);
		g_free(standard_error);
		g_free(cwd);
		return;
	}

	sakura_session_accept_changes();
	sakura_add_tab_with_options(tool_cwd, NULL, NULL, FALSE,
	                            SAKURA_TAB_TOOL, tool, NULL, NULL, NULL, NULL, NULL, -1);

	if (tool_cwd != cwd)
		g_free(tool_cwd);
	g_free(standard_output);
	g_free(standard_error);
	g_free(cwd);
}


static gboolean
sakura_pull_request_url_is_valid(const gchar *url)
{
	const gchar *host_start, *host_end, *path, *pull, *number;
	gsize host_length;

	if (url == NULL || !g_str_has_prefix(url, "https://"))
		return FALSE;
	for (const gchar *cursor = url; *cursor != '\0'; cursor++) {
		if (g_ascii_isspace(*cursor))
			return FALSE;
	}

	host_start = url + strlen("https://");
	host_end = strpbrk(host_start, "/?#");
	if (host_end == NULL || host_end == host_start)
		return FALSE;
	host_length = host_end - host_start;
	if (!((host_length == strlen("github.com") &&
	       g_ascii_strncasecmp(host_start, "github.com", host_length) == 0) ||
	      (host_length == strlen("www.github.com") &&
	       g_ascii_strncasecmp(host_start, "www.github.com", host_length) == 0)))
		return FALSE;

	if (*host_end != '/')
		return FALSE;
	path = host_end;
	pull = strstr(path, "/pull/");
	if (pull == NULL || pull == path)
		return FALSE;

	number = pull + strlen("/pull/");
	if (!g_ascii_isdigit(*number))
		return FALSE;
	while (g_ascii_isdigit(*number))
		number++;

	return *number == '\0' || *number == '/' || *number == '?' || *number == '#';
}


void
sakura_open_pr_cb(GtkWidget *widget, void *data)
{
	GtkWidget *dialog, *entry;
	SakuraTab *existing;
	gchar *url, *cwd;

	(void)data;
	dialog = gtk_dialog_new_with_buttons(
		_("Open pull request"), GTK_WINDOW(sakura.main_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR,
		_("_Cancel"), GTK_RESPONSE_CANCEL,
		_("_Open"), GTK_RESPONSE_ACCEPT,
		NULL);
	entry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(entry),
	                              _("https://github.com/owner/repo/pull/123"));
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
	                   entry, FALSE, FALSE, 12);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
	gtk_widget_show_all(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));

	if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT) {
		gtk_widget_destroy(dialog);
		return;
	}

	url = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
	g_strstrip(url);
	gtk_widget_destroy(dialog);

	if (!sakura_pull_request_url_is_valid(url)) {
		sakura_error(_("Enter a valid HTTPS GitHub pull request URL."));
		g_free(url);
		return;
	}
	if (!sakura_tool_is_available(SAKURA_TOOL_GH_PR)) {
		sakura_error(_("GitHub CLI is not installed. Install gh and try again."));
		g_free(url);
		return;
	}

	existing = sakura_find_tool_target_tab(SAKURA_TOOL_GH_PR, url);
	if (existing != NULL) {
		sakura_select_tab(existing, TRUE);
		sakura_tab_clear_attention(existing);
		g_free(url);
		return;
	}

	cwd = sakura_context_cwd();
	sakura_session_accept_changes();
	sakura_add_tab_with_options(cwd, NULL, NULL, FALSE,
	                            SAKURA_TAB_TOOL, SAKURA_TOOL_GH_PR,
	                            NULL, NULL, NULL, url, NULL, -1);
	g_free(cwd);
	g_free(url);
}


void
sakura_copy_url_cb(GtkWidget *widget, void *data)
{
	GtkClipboard *clipboard;

	(void)widget;
	(void)data;
	clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
	gtk_clipboard_set_text(clipboard, sakura.current_match, -1);
}


void
sakura_copy_pr_url_cb(GtkWidget *widget, void *data)
{
	SakuraTab *tab = data;
	GtkClipboard *clipboard;

	(void)widget;
	if (tab == NULL || tab->kind != SAKURA_TAB_TOOL ||
	    tab->tool != SAKURA_TOOL_GH_PR || tab->tool_target == NULL ||
	    tab->tool_target[0] == '\0')
		return;

	clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
	gtk_clipboard_set_text(clipboard, tab->tool_target, -1);
}


static gboolean
sakura_launch_uri(const gchar *uri)
{
	const gchar *browser;
	GError *error = NULL;

	if (uri == NULL || uri[0] == '\0')
		return FALSE;

	browser = g_getenv("BROWSER");
	if (browser != NULL && browser[0] != '\0') {
		gint argc = 0;
		gchar **argv = NULL;

		if (!g_shell_parse_argv(browser, &argc, &argv, &error)) {
			sakura_error(_("Could not parse BROWSER: %s"), error->message);
			g_clear_error(&error);
			return FALSE;
		}
		argv = g_realloc(argv, sizeof(*argv) * (argc + 2));
		argv[argc] = g_strdup(uri);
		argv[argc + 1] = NULL;
		if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
		                   NULL, NULL, NULL, &error)) {
			sakura_error(_("Could not open link with %s: %s"),
			             argv[0], error->message);
			g_clear_error(&error);
			g_strfreev(argv);
			return FALSE;
		}
		g_strfreev(argv);
		return TRUE;
	}

	if (!g_app_info_launch_default_for_uri(uri, NULL, &error)) {
		sakura_error(_("Could not open link: %s"), error->message);
		g_clear_error(&error);
		return FALSE;
	}
	return TRUE;
}


void
sakura_open_url_cb(GtkWidget *widget, void *data)
{
	(void)widget;
	(void)data;
	sakura_launch_uri(sakura.current_match);
}


void
sakura_open_mail_cb(GtkWidget *widget, void *data)
{
	GError *error = NULL;
	gchar *program;

	(void)widget;
	(void)data;
	program = g_find_program_in_path("xdg-email");
	if (program != NULL) {
		gchar *argv[] = { program, sakura.current_match, NULL };
		if (!g_spawn_async(".", argv, NULL, G_SPAWN_SEARCH_PATH,
		                   NULL, NULL, NULL, &error)) {
			sakura_error("Couldn't exec \"%s %s\": %s", program,
			             sakura.current_match,
			             error != NULL ? error->message : "unknown error");
			g_clear_error(&error);
		}
		g_free(program);
	}
}


#ifdef HAVE_WEBKITGTK
static void
sakura_browser_update_navigation(SakuraTab *tab)
{
	WebKitWebView *view;

	if (tab == NULL || tab->browser == NULL)
		return;
	view = WEBKIT_WEB_VIEW(tab->browser);
	gtk_widget_set_sensitive(tab->browser_back, webkit_web_view_can_go_back(view));
	gtk_widget_set_sensitive(tab->browser_forward, webkit_web_view_can_go_forward(view));
}

static void
sakura_browser_back_cb(GtkWidget *widget, void *data)
{
	SakuraTab *tab = data;

	(void)widget;
	if (tab != NULL && tab->browser != NULL &&
	    webkit_web_view_can_go_back(WEBKIT_WEB_VIEW(tab->browser)))
		webkit_web_view_go_back(WEBKIT_WEB_VIEW(tab->browser));
}

static void
sakura_browser_forward_cb(GtkWidget *widget, void *data)
{
	SakuraTab *tab = data;

	(void)widget;
	if (tab != NULL && tab->browser != NULL &&
	    webkit_web_view_can_go_forward(WEBKIT_WEB_VIEW(tab->browser)))
		webkit_web_view_go_forward(WEBKIT_WEB_VIEW(tab->browser));
}

static void
sakura_browser_reload_cb(GtkWidget *widget, void *data)
{
	SakuraTab *tab = data;

	(void)widget;
	if (tab != NULL && tab->browser != NULL)
		webkit_web_view_reload(WEBKIT_WEB_VIEW(tab->browser));
}

static void
sakura_browser_external_cb(GtkWidget *widget, void *data)
{
	SakuraTab *tab = data;
	const gchar *uri;

	(void)widget;
	if (tab == NULL || tab->browser == NULL)
		return;
	uri = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(tab->browser));
	if (uri != NULL)
		sakura_launch_uri(uri);
}

static void
sakura_browser_navigation_changed_cb(GObject *object, GParamSpec *pspec,
                                     void *data)
{
	(void)object;
	(void)pspec;
	sakura_browser_update_navigation(data);
}
#endif
#endif

#ifndef SAKURA_CORE_TEST
enum sakura_codex_session_query_kind {
	SAKURA_CODEX_SESSION_QUERY_NAME,
	SAKURA_CODEX_SESSION_QUERY_INFO,
	SAKURA_CODEX_SESSION_QUERY_SET_NAME
};

struct sakura_codex_name_query {
	enum sakura_codex_session_query_kind kind;
	gchar *tracking_token;
	gchar *session_id;
	gchar *fallback_cwd;
	gchar *new_name;
	gboolean resume_cwd;
	gchar *request_line;
	guint request_id;
	gsize request_offset;
};

static void sakura_codex_name_helper_dispatch(void);



SakuraTab *
sakura_find_codex_tab_by_tracking_token (const gchar *tracking_token)
{
	gint page, pages;

	if (tracking_token == NULL || sakura.notebook == NULL)
		return NULL;

	pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));
	for (page = 0; page < pages; page++) {
		struct sakura_tab *sk_tab = sakura_tab_at_page(page);
		if (g_strcmp0(sk_tab->codex_tracking_token, tracking_token) == 0)
			return sk_tab;
	}
	return NULL;
}


static gboolean
sakura_codex_name_retry_cb (gpointer data)
{
	const gchar *tracking_token = data;
	struct sakura_tab *sk_tab;

	sk_tab = sakura_find_codex_tab_by_tracking_token(tracking_token);
	if (sk_tab != NULL) {
		sk_tab->codex_name_retry_source_id = 0;
		sakura_codex_sync_name(sk_tab);
	}

	return G_SOURCE_REMOVE;
}


static void
sakura_codex_schedule_name_retry (struct sakura_tab *sk_tab)
{
	if (sk_tab == NULL || sk_tab->codex_tracking_token == NULL ||
	    sk_tab->codex_name_retry_source_id != 0 ||
	    sk_tab->codex_name_retry_count >= 3 || sakura.session_shutting_down)
		return;

	sk_tab->codex_name_retry_count++;
	sk_tab->codex_name_retry_source_id = g_timeout_add_full(
		G_PRIORITY_DEFAULT, 1000, sakura_codex_name_retry_cb,
		g_strdup(sk_tab->codex_tracking_token), g_free);
}


static gboolean
sakura_codex_helper_is_usable(const gchar *path)
{
	return path != NULL && path[0] != '\0' &&
	       g_file_test(path, G_FILE_TEST_IS_REGULAR | G_FILE_TEST_IS_EXECUTABLE);
}


static gchar *
sakura_codex_helper_in_directory(const gchar *directory)
{
	gchar *helper;

	if (directory == NULL || directory[0] == '\0')
		return NULL;
	helper = g_build_filename(directory, SAKURA_CODEX_HELPER_NAME, NULL);
	if (sakura_codex_helper_is_usable(helper))
		return helper;
	g_free(helper);
	return NULL;
}


static gchar *
sakura_codex_helper_beside_executable(void)
{
	gchar *executable, *directory, *helper;

	executable = g_file_read_link("/proc/self/exe", NULL);
	if (executable == NULL || executable[0] == '\0') {
		g_free(executable);
		return NULL;
	}
	directory = g_path_get_dirname(executable);
	helper = sakura_codex_helper_in_directory(directory);
	g_free(directory);
	g_free(executable);
	return helper;
}


gchar *
sakura_find_codex_name_helper (void)
{
	const gchar *override;
	gchar *helper;

	override = g_getenv(SAKURA_CODEX_HELPER_OVERRIDE_ENV);
	if (override != NULL && override[0] != '\0') {
		helper = strchr(override, G_DIR_SEPARATOR) != NULL
		       ? g_strdup(override) : g_find_program_in_path(override);
		if (sakura_codex_helper_is_usable(helper))
			return helper;
		g_warning("Configured %s is not an executable helper: %s",
		          SAKURA_CODEX_HELPER_OVERRIDE_ENV, override);
		g_free(helper);
		return NULL;
	}

	helper = sakura_codex_helper_beside_executable();
	if (helper != NULL)
		return helper;

	helper = sakura_codex_helper_in_directory(SAKURA_SOURCE_SCRIPT_DIR);
	if (helper != NULL)
		return helper;

	helper = sakura_codex_helper_in_directory(SAKURA_INSTALL_BINDIR);
	if (helper != NULL)
		return helper;

	return NULL;
}


static void
sakura_codex_name_query_complete(struct sakura_codex_name_query *query,
	                               const gchar *name,
	                               const gchar *error_message);


static void
sakura_codex_session_info_complete(struct sakura_codex_name_query *query,
	                                  const gchar *name, const gchar *cwd,
	                                  const gchar *error_message)
{
	struct sakura_tab *sk_tab;
	gchar *clean_cwd = NULL;

	sk_tab = sakura_find_codex_tab_by_tracking_token(query->tracking_token);
	if (sk_tab == NULL)
		return;
	if (sk_tab->kind != SAKURA_TAB_CODEX || sakura.session_shutting_down) {
		if (query->resume_cwd) {
			sk_tab->codex_resume_cwd_query_active = FALSE;
			sk_tab->codex_session_query_active = FALSE;
		}
		return;
	}
	if (g_strcmp0(sk_tab->codex_session_id, query->session_id) != 0) {
		if (query->resume_cwd) {
			sk_tab->codex_resume_cwd_query_active = FALSE;
			sk_tab->codex_session_query_active = FALSE;
			sakura_codex_sync_name(sk_tab);
		}
		else if (!sk_tab->codex_resume_cwd_query_active) {
			sk_tab->codex_session_query_active = FALSE;
			sakura_codex_sync_name(sk_tab);
		}
		return;
	}

	if (error_message != NULL) {
		if (!query->resume_cwd) {
			sakura_codex_name_query_complete(query, NULL, error_message);
			return;
		}
		sk_tab->codex_resume_cwd_query_active = FALSE;
		sk_tab->codex_session_query_active = FALSE;
		sk_tab->codex_resume_cwd_lookup_done = TRUE;
		g_debug("Could not read Codex session information: %s", error_message);
		sakura_tab_resume_codex_with_cwd(sk_tab, query->fallback_cwd);
		sakura_sidebar_update_tab(sk_tab);
		sakura_codex_sync_name(sk_tab);
		return;
	}

	sakura_codex_name_query_complete(query, name, NULL);
	if (g_strcmp0(sk_tab->codex_session_id, query->session_id) != 0)
		return;
	if (!query->resume_cwd)
		return;

	sk_tab->codex_resume_cwd_query_active = FALSE;
	sk_tab->codex_session_query_active = FALSE;
	sk_tab->codex_resume_cwd_lookup_done = TRUE;
	if (cwd != NULL) {
		clean_cwd = g_strdup(cwd);
		g_strstrip(clean_cwd);
		if (!g_path_is_absolute(clean_cwd) ||
		    !g_file_test(clean_cwd, G_FILE_TEST_IS_DIR)) {
			g_free(clean_cwd);
			clean_cwd = NULL;
		}
	}
	if (clean_cwd != NULL) {
		if (g_strcmp0(sk_tab->cwd, clean_cwd) != 0) {
			g_free(sk_tab->cwd);
			sk_tab->cwd = g_strdup(clean_cwd);
			sakura_session_mark_dirty();
		}
		sk_tab->codex_resume_cwd = clean_cwd;
		clean_cwd = NULL;
	} else {
		g_debug("Starting Codex resume from Sakura's saved working directory");
	}

	sakura_tab_resume_codex_with_cwd(sk_tab, query->fallback_cwd);
	sakura_sidebar_update_tab(sk_tab);
	g_free(clean_cwd);
}


void
sakura_codex_resolve_resume_cwd_async(SakuraTab *tab, const gchar *fallback_cwd)
{
	struct sakura_codex_name_query *query;

	if (tab == NULL || tab->kind != SAKURA_TAB_CODEX ||
	    tab->codex_session_id == NULL || tab->codex_session_id[0] == '\0' ||
	    tab->codex_tracking_token == NULL || sakura.session_shutting_down)
		return;

	if (sakura.codex_name_query_queue == NULL)
		sakura.codex_name_query_queue = g_queue_new();
	query = g_new0(struct sakura_codex_name_query, 1);
	query->kind = SAKURA_CODEX_SESSION_QUERY_INFO;
	query->resume_cwd = TRUE;
	query->tracking_token = g_strdup(tab->codex_tracking_token);
	query->session_id = g_strdup(tab->codex_session_id);
	query->fallback_cwd = g_strdup(fallback_cwd);
	tab->codex_session_query_active = TRUE;
	g_queue_push_tail(sakura.codex_name_query_queue, query);
	sakura_codex_name_helper_dispatch();
}


static void
sakura_codex_name_query_free (struct sakura_codex_name_query *query)
{
	g_free(query->tracking_token);
	g_free(query->session_id);
	g_free(query->fallback_cwd);
	g_free(query->new_name);
	g_free(query->request_line);
	g_free(query);
}


static void
sakura_codex_name_query_complete (struct sakura_codex_name_query *query,
	                               const gchar *name,
	                               const gchar *error_message)
{
	struct sakura_tab *sk_tab;
	gchar *clean_name = NULL;
	gboolean stale_query = FALSE;

	sk_tab = sakura_find_codex_tab_by_tracking_token(query->tracking_token);
	if (sk_tab == NULL)
		return;

	if (!sk_tab->codex_resume_cwd_query_active)
		sk_tab->codex_session_query_active = FALSE;
	if (sakura.session_shutting_down)
		return;

	if (sk_tab->kind == SAKURA_TAB_CODEX &&
	    g_strcmp0(sk_tab->codex_session_id, query->session_id) == 0) {
		if (error_message != NULL) {
			g_debug("Could not read Codex session name: %s", error_message);
			sakura_codex_schedule_name_retry(sk_tab);
			return;
		}

		clean_name = g_strdup(name != NULL ? name : "");
		g_strstrip(clean_name);
		if (clean_name[0] != '\0')
			sk_tab->codex_name_retry_count = 0;
		if (g_strcmp0(sk_tab->codex_session_name, clean_name) != 0) {
			g_free(sk_tab->codex_session_name);
			sk_tab->codex_session_name = clean_name[0] != '\0' ? g_strdup(clean_name) : NULL;
			if (!sk_tab->label_set_byuser)
				sakura_sidebar_update_tab(sk_tab);
			sakura_session_mark_dirty();
		}
		if (clean_name[0] == '\0')
			sakura_codex_schedule_name_retry(sk_tab);
		g_free(clean_name);
		return;
	}

	stale_query = sk_tab->kind == SAKURA_TAB_CODEX &&
	              g_strcmp0(sk_tab->codex_session_id, query->session_id) != 0;
	if (stale_query)
		sakura_codex_sync_name(sk_tab);
}


static void
sakura_codex_name_helper_clear (gboolean terminate)
{
	if (sakura.codex_name_helper_process != NULL && terminate)
		g_subprocess_force_exit(sakura.codex_name_helper_process);
	g_clear_object(&sakura.codex_name_helper_input);
	g_clear_object(&sakura.codex_name_helper_output);
	g_clear_object(&sakura.codex_name_helper_process);
}


static void
sakura_codex_set_name_query_complete(struct sakura_codex_name_query *query,
                                      const gchar *error_message)
{
	struct sakura_tab *sk_tab;

	sk_tab = sakura_find_codex_tab_by_tracking_token(query->tracking_token);
	if (sk_tab == NULL)
		return;
	sk_tab->codex_session_query_active = FALSE;
	if (sakura.session_shutting_down)
		return;
	if (error_message != NULL) {
		sakura_error(_("Could not rename Codex session: %s"), error_message);
		return;
	}
	if (sk_tab->kind == SAKURA_TAB_CODEX &&
	    g_strcmp0(sk_tab->codex_session_id, query->session_id) == 0) {
		g_free(sk_tab->codex_session_name);
		sk_tab->codex_session_name = g_strdup(query->new_name);
		if (!sk_tab->label_set_byuser)
			sakura_sidebar_update_tab(sk_tab);
		sakura_session_mark_dirty();
	}
}


static void
sakura_codex_name_helper_wait_done (GObject *source_object,
	                                  GAsyncResult *result, gpointer data)
{
	GSubprocess *process = G_SUBPROCESS(source_object);
	GError *error = NULL;

	(void)data;
	if (!g_subprocess_wait_finish(process, result, &error))
		g_clear_error(&error);

	/* The helper may have been replaced while the old process was being
	 * reaped. Never clear the state belonging to the replacement process. */
	if (sakura.codex_name_helper_process == process) {
		g_clear_object(&sakura.codex_name_helper_input);
		g_clear_object(&sakura.codex_name_helper_output);
		g_clear_object(&sakura.codex_name_helper_process);
	}
}


static gboolean
sakura_codex_name_helper_start (GError **error)
{
	GSubprocess *process;
	GInputStream *stdout_pipe;
	gchar *helper;
	const gchar *argv[3];

	if (sakura.codex_name_helper_process != NULL)
		return TRUE;

	helper = sakura_find_codex_name_helper();
	if (helper == NULL) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		                    "sakura-codex-session-name was not found");
		return FALSE;
	}
	argv[0] = helper;
	argv[1] = "--server";
	argv[2] = NULL;
	process = g_subprocess_newv(argv,
	                            G_SUBPROCESS_FLAGS_STDIN_PIPE |
	                            G_SUBPROCESS_FLAGS_STDOUT_PIPE |
	                            G_SUBPROCESS_FLAGS_STDERR_SILENCE,
	                            error);
	g_free(helper);
	if (process == NULL)
		return FALSE;

	stdout_pipe = g_subprocess_get_stdout_pipe(process);
	sakura.codex_name_helper_process = process;
	sakura.codex_name_helper_input =
		g_object_ref(g_subprocess_get_stdin_pipe(process));
	sakura.codex_name_helper_output = g_data_input_stream_new(stdout_pipe);
	g_subprocess_wait_async(process, NULL, sakura_codex_name_helper_wait_done, NULL);
	return TRUE;
}


static void
sakura_codex_name_helper_request_failed (struct sakura_codex_name_query *query,
	                                       const gchar *error_message)
{
	if (sakura.codex_name_query_in_flight != query)
		return;
	sakura.codex_name_query_in_flight = NULL;
	sakura_codex_name_helper_clear(TRUE);
	if (query->kind == SAKURA_CODEX_SESSION_QUERY_INFO)
		sakura_codex_session_info_complete(query, NULL, NULL, error_message);
	else if (query->kind == SAKURA_CODEX_SESSION_QUERY_SET_NAME)
		sakura_codex_set_name_query_complete(query, error_message);
	else
		sakura_codex_name_query_complete(query, NULL, error_message);
	sakura_codex_name_query_free(query);
	sakura_codex_name_helper_dispatch();
}


static gchar *
sakura_codex_decode_value(const gchar *encoded)
{
	guchar *bytes;
	gchar *value;
	gsize length = 0;

	if (encoded == NULL)
		return NULL;
	bytes = g_base64_decode(encoded, &length);
	if (length == 0) {
		g_free(bytes);
		return g_strdup("");
	}
	if (bytes == NULL)
		return NULL;
	value = g_strndup((gchar *)bytes, length);
	g_free(bytes);
	return value;
}


static void
sakura_codex_name_helper_read_done (GObject *source_object,
	                                  GAsyncResult *result,
	                                  gpointer data)
{
	GDataInputStream *stream = G_DATA_INPUT_STREAM(source_object);
	struct sakura_codex_name_query *query = data;
	GError *error = NULL;
	gchar *line = NULL;
	gchar **fields = NULL;
	gchar *decoded = NULL;
	gchar *decoded_cwd = NULL;
	const gchar *status_field;
	const gchar *payload_field;
	gchar *end = NULL;
	guint64 request_id;
	gboolean valid_response = FALSE;

	line = g_data_input_stream_read_line_finish(stream, result, NULL, &error);
	if (error != NULL) {
		sakura_codex_name_helper_request_failed(query, error->message);
		g_error_free(error);
		return;
	}
	if (line == NULL) {
		sakura_codex_name_helper_request_failed(query, "Codex session name helper exited");
		return;
	}

	fields = g_strsplit(line, "\t", 6);
	request_id = fields[0] != NULL ? g_ascii_strtoull(fields[0], &end, 10) : 0;
	status_field = fields[1];
	payload_field = fields[2];
	if (query->kind != SAKURA_CODEX_SESSION_QUERY_NAME) {
		valid_response = fields[1] != NULL &&
		                g_strcmp0(fields[1], SAKURA_CODEX_HELPER_PROTOCOL_VERSION) == 0 &&
		                fields[2] != NULL && fields[3] != NULL;
		status_field = fields[2];
		payload_field = fields[3];
	}
	valid_response = valid_response ||
	                (query->kind == SAKURA_CODEX_SESSION_QUERY_NAME &&
	                 fields[0] != NULL && fields[1] != NULL && fields[2] != NULL);
	valid_response = valid_response && end != fields[0] && *end == '\0' &&
	                request_id == query->request_id;
	if (!valid_response) {
		if (query->kind != SAKURA_CODEX_SESSION_QUERY_NAME)
			sakura_codex_name_helper_request_failed(
				query, "incompatible Codex session helper (expected protocol v1)");
		else
			sakura_codex_name_helper_request_failed(
				query, "invalid response from Codex session name helper");
		g_strfreev(fields);
		g_free(line);
		return;
	}

	decoded = sakura_codex_decode_value(payload_field);
	if (decoded == NULL) {
		sakura_codex_name_helper_request_failed(query, "invalid encoded response from Codex session name helper");
		g_strfreev(fields);
		g_free(line);
		return;
	}
	if (query->kind == SAKURA_CODEX_SESSION_QUERY_INFO &&
	    g_strcmp0(status_field, "ok") == 0) {
		if (fields[4] == NULL) {
			sakura_codex_name_helper_request_failed(
				query, "invalid session information response");
			g_free(decoded);
			g_strfreev(fields);
			g_free(line);
			return;
		}
		decoded_cwd = sakura_codex_decode_value(fields[4]);
		if (decoded_cwd == NULL) {
			sakura_codex_name_helper_request_failed(
				query, "invalid session working directory response");
			g_free(decoded);
			g_strfreev(fields);
			g_free(line);
			return;
		}
	}

	sakura.codex_name_query_in_flight = NULL;
	if (query->kind == SAKURA_CODEX_SESSION_QUERY_INFO) {
		if (g_strcmp0(status_field, "ok") == 0)
			sakura_codex_session_info_complete(query, decoded, decoded_cwd, NULL);
		else
			sakura_codex_session_info_complete(query, NULL, NULL, decoded);
	} else if (query->kind == SAKURA_CODEX_SESSION_QUERY_SET_NAME) {
		if (g_strcmp0(status_field, "ok") == 0)
			sakura_codex_set_name_query_complete(query, NULL);
		else
			sakura_codex_set_name_query_complete(query, decoded);
	} else if (g_strcmp0(status_field, "ok") == 0) {
		sakura_codex_name_query_complete(query, decoded, NULL);
	} else {
		sakura_codex_name_query_complete(query, NULL, decoded);
	}
	sakura_codex_name_query_free(query);
	sakura_codex_name_helper_dispatch();

	g_free(decoded);
	g_free(decoded_cwd);
	g_strfreev(fields);
	g_free(line);
}


static void
sakura_codex_name_helper_write_done (GObject *source_object,
	                                   GAsyncResult *result,
	                                   gpointer data)
{
	GOutputStream *stream = G_OUTPUT_STREAM(source_object);
	struct sakura_codex_name_query *query = data;
	GError *error = NULL;
	gssize bytes_written;

	bytes_written = g_output_stream_write_finish(stream, result, &error);
	if (bytes_written <= 0) {
		if (error == NULL)
			g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_FAILED,
			                    "Codex session name helper closed its input");
		sakura_codex_name_helper_request_failed(query, error->message);
		g_error_free(error);
		return;
	}
	query->request_offset += (gsize)bytes_written;
	if (query->request_offset < strlen(query->request_line)) {
		g_output_stream_write_async(stream,
		                            query->request_line + query->request_offset,
		                            strlen(query->request_line) - query->request_offset,
		                            G_PRIORITY_DEFAULT, NULL,
		                            sakura_codex_name_helper_write_done, query);
		return;
	}
	if (sakura.codex_name_query_in_flight != query ||
	    sakura.codex_name_helper_output == NULL || sakura.session_shutting_down)
		return;
	g_data_input_stream_read_line_async(sakura.codex_name_helper_output,
	                                    G_PRIORITY_DEFAULT, NULL,
	                                    sakura_codex_name_helper_read_done, query);
}


static void
sakura_codex_name_helper_dispatch (void)
{
	struct sakura_codex_name_query *query;
	GError *error = NULL;
	gchar *encoded_session_id;
	gchar *encoded_name;

	if (sakura.session_shutting_down || sakura.codex_name_query_in_flight != NULL ||
	    sakura.codex_name_query_queue == NULL ||
	    g_queue_is_empty(sakura.codex_name_query_queue))
		return;

	query = g_queue_pop_head(sakura.codex_name_query_queue);
	if (!sakura_codex_name_helper_start(&error)) {
		g_debug("Could not start Codex session name helper: %s",
		    error != NULL ? error->message : "unknown error");
		if (query->kind == SAKURA_CODEX_SESSION_QUERY_INFO)
			sakura_codex_session_info_complete(
				query, NULL, NULL,
				error != NULL ? error->message : "helper start failed");
		else if (query->kind == SAKURA_CODEX_SESSION_QUERY_SET_NAME)
			sakura_codex_set_name_query_complete(
				query, error != NULL ? error->message : "helper start failed");
		else
			sakura_codex_name_query_complete(
				query, NULL,
				error != NULL ? error->message : "helper start failed");
		sakura_codex_name_query_free(query);
		g_clear_error(&error);
		sakura_codex_name_helper_dispatch();
		return;
	}

	if (++sakura.codex_name_helper_request_id == 0)
		sakura.codex_name_helper_request_id = 1;
	query->request_id = sakura.codex_name_helper_request_id;
	encoded_session_id = g_base64_encode((const guchar *)query->session_id,
	                                     strlen(query->session_id));
	if (query->kind == SAKURA_CODEX_SESSION_QUERY_INFO) {
		query->request_line = g_strdup_printf("%u\t%s\tinfo\t%s\n",
		                                      query->request_id,
		                                      SAKURA_CODEX_HELPER_PROTOCOL_VERSION,
		                                      encoded_session_id);
	} else if (query->kind == SAKURA_CODEX_SESSION_QUERY_SET_NAME) {
		encoded_name = g_base64_encode((const guchar *)query->new_name,
		                               strlen(query->new_name));
		query->request_line = g_strdup_printf("%u\t%s\tset-name\t%s\t%s\n",
		                                      query->request_id,
		                                      SAKURA_CODEX_HELPER_PROTOCOL_VERSION,
		                                      encoded_session_id, encoded_name);
		g_free(encoded_name);
	} else {
		query->request_line = g_strdup_printf("%u\t%s\n", query->request_id,
		                                      encoded_session_id);
	}
	query->request_offset = 0;
	g_free(encoded_session_id);
	sakura.codex_name_query_in_flight = query;
	g_output_stream_write_async(sakura.codex_name_helper_input,
	                            query->request_line, strlen(query->request_line),
	                            G_PRIORITY_DEFAULT, NULL,
	                            sakura_codex_name_helper_write_done, query);
}


void
sakura_codex_name_helper_shutdown (void)
{
	struct sakura_codex_name_query *query;

	sakura_codex_name_helper_clear(TRUE);
	query = sakura.codex_name_query_in_flight;
	sakura.codex_name_query_in_flight = NULL;
	if (query != NULL)
		sakura_codex_name_query_free(query);
	if (sakura.codex_name_query_queue != NULL) {
		while ((query = g_queue_pop_head(sakura.codex_name_query_queue)) != NULL)
			sakura_codex_name_query_free(query);
		g_queue_free(sakura.codex_name_query_queue);
		sakura.codex_name_query_queue = NULL;
	}
}


void
sakura_codex_sync_name(SakuraTab *tab)
{
	struct sakura_codex_name_query *query;

	if (tab == NULL || tab->kind != SAKURA_TAB_CODEX ||
	    tab->codex_session_id == NULL || tab->codex_session_id[0] == '\0' ||
	    tab->codex_session_query_active || sakura.session_shutting_down)
		return;

	if (sakura.codex_name_query_queue == NULL)
		sakura.codex_name_query_queue = g_queue_new();
	query = g_new0(struct sakura_codex_name_query, 1);
	query->kind = SAKURA_CODEX_SESSION_QUERY_NAME;
	query->tracking_token = g_strdup(tab->codex_tracking_token);
	query->session_id = g_strdup(tab->codex_session_id);
	tab->codex_session_query_active = TRUE;
	g_queue_push_tail(sakura.codex_name_query_queue, query);
	sakura_codex_name_helper_dispatch();
}

void
sakura_codex_set_name_async (struct sakura_tab *sk_tab, const gchar *name)
{
	struct sakura_codex_name_query *query;

	if (sk_tab == NULL || name == NULL || name[0] == '\0' ||
	    sk_tab->kind != SAKURA_TAB_CODEX ||
	    sk_tab->codex_session_id == NULL ||
	    sk_tab->codex_session_id[0] == '\0' || sakura.session_shutting_down)
		return;

	if (sakura.codex_name_query_queue == NULL)
		sakura.codex_name_query_queue = g_queue_new();
	query = g_new0(struct sakura_codex_name_query, 1);
	query->kind = SAKURA_CODEX_SESSION_QUERY_SET_NAME;
	query->tracking_token = g_strdup(sk_tab->codex_tracking_token);
	query->session_id = g_strdup(sk_tab->codex_session_id);
	query->new_name = g_strdup(name);
	sk_tab->codex_session_query_active = TRUE;
	g_queue_push_tail(sakura.codex_name_query_queue, query);
	sakura_codex_name_helper_dispatch();
}


static gboolean
sakura_process_has_environment (GPid pid, const gchar *name, const gchar *value)
{
	gchar *environment, *needle, *path;
	gsize length, needle_length, offset;
	gboolean found = FALSE;

	if (pid <= 0 || name == NULL || value == NULL)
		return FALSE;

	path = g_strdup_printf("/proc/%d/environ", (gint)pid);
	if (!g_file_get_contents(path, &environment, &length, NULL)) {
		g_free(path);
		return FALSE;
	}
	g_free(path);

	needle = g_strdup_printf("%s=%s", name, value);
	needle_length = strlen(needle);
	for (offset = 0; offset + needle_length <= length; offset++) {
		if ((offset == 0 || environment[offset - 1] == '\0') &&
		    memcmp(environment + offset, needle, needle_length) == 0 &&
		    (offset + needle_length == length ||
		     environment[offset + needle_length] == '\0')) {
			found = TRUE;
			break;
		}
	}

	g_free(needle);
	g_free(environment);
	return found;
}


static gchar *
sakura_codex_hooks_path (void)
{
	const gchar *codex_home = g_getenv("CODEX_HOME");

	if (codex_home == NULL || codex_home[0] == '\0')
		return g_build_filename(g_get_home_dir(), ".codex", "hooks.json", NULL);

	return g_build_filename(codex_home, "hooks.json", NULL);
}


SakuraCodexTrackingState
sakura_codex_tracking_state (void)
{
	static const gchar *required_events[] = {
		"SessionStart",
		"UserPromptSubmit",
		"PreToolUse",
		"PermissionRequest",
		"Stop"
	};
	gchar *hook_path;
	gchar *hook_config = NULL;
	gsize hook_config_length = 0;
	guint found_events = 0;
	guint i;
	gboolean has_marker;

	hook_path = sakura_codex_hooks_path();
	if (!g_file_get_contents(hook_path, &hook_config, &hook_config_length, NULL)) {
		g_free(hook_path);
		return SAKURA_CODEX_TRACKING_MISSING;
	}

	has_marker = g_strstr_len(hook_config, (gssize)hook_config_length,
	                          "sakura-codex-session-hook") != NULL;
	if (has_marker) {
		for (i = 0; i < G_N_ELEMENTS(required_events); i++) {
			gchar *event_key = g_strdup_printf("\"%s\"", required_events[i]);

			if (g_strstr_len(hook_config, (gssize)hook_config_length, event_key) != NULL)
				found_events++;
			g_free(event_key);
		}
	}

	g_free(hook_config);
	g_free(hook_path);

	if (!has_marker)
		return SAKURA_CODEX_TRACKING_MISSING;
	if (found_events == G_N_ELEMENTS(required_events))
		return SAKURA_CODEX_TRACKING_ENABLED;
	return SAKURA_CODEX_TRACKING_PARTIAL;
}


void
sakura_codex_tracking_status_cb (GtkWidget *widget, void *data)
{
	GtkWidget *message;
	struct sakura_tab *sk_tab = NULL;
	gchar *text;
	gint page;
	gboolean environment_ok = FALSE;
	SakuraCodexTrackingState tracking_state;
	const gchar *hook_label;

	(void)widget;
	(void)data;

	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	if (page >= 0) {
		sk_tab = sakura_tab_at_page(page);
		if (sk_tab != NULL)
			environment_ok = sakura_process_has_environment(
				sk_tab->pid, "SAKURA_CODEX_TAB_TOKEN", sk_tab->codex_tracking_token);
	}

	tracking_state = sakura_codex_tracking_state();
	hook_label = tracking_state == SAKURA_CODEX_TRACKING_ENABLED ? _("enabled") :
	             tracking_state == SAKURA_CODEX_TRACKING_PARTIAL ? _("incomplete") :
	             _("missing");
	text = g_strdup_printf(
		_("Codex tracking status:\n\n"
		"Hook entry: %s\n"
		"Tracking directory: %s\n"
		"Current tab environment: %s\n"
		"Current tab kind: %s\n"
		"Current Codex session: %s\n"
		"Current Codex name: %s"),
		hook_label,
		sakura.codex_tracking_dir != NULL ? sakura.codex_tracking_dir : _("disabled"),
		sk_tab != NULL && environment_ok ? _("present") : _("missing (reopen this tab)"),
		sk_tab != NULL && sk_tab->kind == SAKURA_TAB_CODEX ? _("Codex") : _("shell"),
		sk_tab != NULL && sk_tab->codex_session_id != NULL &&
		sk_tab->codex_session_id[0] != '\0' ? sk_tab->codex_session_id : _("not received"),
		sk_tab != NULL && sk_tab->codex_session_name != NULL &&
		sk_tab->codex_session_name[0] != '\0' ? sk_tab->codex_session_name : _("not available"));

	message = gtk_message_dialog_new(GTK_WINDOW(sakura.main_window),
	                                GTK_DIALOG_DESTROY_WITH_PARENT,
	                                GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
	                                "%s", text);
	gtk_dialog_run(GTK_DIALOG(message));
	gtk_widget_destroy(message);
	g_free(text);
}


void
sakura_new_codex_cb (GtkWidget *widget, void *data)
{
	const gchar *reasoning_effort = widget != NULL
	                              ? g_object_get_data(G_OBJECT(widget),
	                                                  SAKURA_CODEX_REASONING_EFFORT_DATA_KEY)
	                              : NULL;
	SakuraSidebarNode *parent = data;
	sakura_session_accept_changes();
	sakura_add_tab_with_options(NULL, parent, NULL, FALSE, SAKURA_TAB_CODEX,
	                            SAKURA_TOOL_NONE, NULL, NULL, reasoning_effort,
	                            NULL, NULL, -1);
}


void
sakura_resume_codex_cb (GtkWidget *widget, void *data)
{
	GtkWidget *dialog, *entry;
	const gchar *session;
	SakuraSidebarNode *parent = data;

	(void)widget;
	dialog = gtk_dialog_new_with_buttons(
		_("Resume session"), GTK_WINDOW(sakura.main_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR,
		_("_Cancel"), GTK_RESPONSE_CANCEL,
		_("_Resume"), GTK_RESPONSE_ACCEPT, NULL);
	entry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(entry), _("Session ID or name"));
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
	                   entry, FALSE, FALSE, 12);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
	gtk_widget_show_all(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		session = gtk_entry_get_text(GTK_ENTRY(entry));
		if (session != NULL && session[0] != '\0') {
			sakura_session_accept_changes();
			sakura_add_tab_with_options(NULL, parent, NULL, FALSE,
			                            SAKURA_TAB_CODEX, SAKURA_TOOL_NONE,
			                            session, NULL, NULL, NULL, NULL, -1);
		}
	}
	gtk_widget_destroy(dialog);
}


void
sakura_rename_codex_session_cb (GtkWidget *widget, void *data)
{
	GtkWidget *dialog, *entry;
	SakuraTab *tab = data;
	gint page;
	gint response;
	const gchar *current_name;
	gchar *name;

	(void)widget;
	page = tab != NULL
	     ? sakura_page_for_tab(tab)
	     : gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	if (page < 0)
		return;
	if (tab == NULL)
		tab = sakura_tab_at_page(page);
	if (tab == NULL || tab->kind != SAKURA_TAB_CODEX ||
	    tab->codex_session_id == NULL || tab->codex_session_id[0] == '\0') {
		sakura_error(_("The current tab is not attached to a Codex session."));
		return;
	}

	dialog = gtk_dialog_new_with_buttons(
		_("Rename Codex session"), GTK_WINDOW(sakura.main_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR,
		_("_Cancel"), GTK_RESPONSE_CANCEL,
		_("_Rename"), GTK_RESPONSE_ACCEPT, NULL);
	entry = gtk_entry_new();
	current_name = tab->codex_session_name != NULL &&
	               tab->codex_session_name[0] != '\0'
	               ? tab->codex_session_name : "";
	gtk_entry_set_text(GTK_ENTRY(entry), current_name);
	gtk_entry_set_placeholder_text(GTK_ENTRY(entry), _("Session name"));
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
	                   entry, FALSE, FALSE, 12);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
	gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT,
	                                  current_name[0] != '\0');
	g_signal_connect(entry, "changed", G_CALLBACK(sakura_setname_entry_changed_cb),
	                 dialog);
	gtk_widget_show_all(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));
	gtk_widget_grab_focus(entry);
	gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);

	response = gtk_dialog_run(GTK_DIALOG(dialog));
	if (response == GTK_RESPONSE_ACCEPT) {
		name = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
		g_strstrip(name);
		if (name[0] != '\0') {
			sakura_session_accept_changes();
			sakura_codex_set_name_async(tab, name);
		}
		g_free(name);
	}
	gtk_widget_destroy(dialog);
}


void
sakura_attach_codex_cb (GtkWidget *widget, void *data)
{
	GtkWidget *dialog, *entry;
	gint page;
	SakuraTab *tab;
	gchar *session;

	(void)widget;
	(void)data;
	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	if (page < 0)
		return;
	tab = sakura_tab_at_page(page);
	if (tab == NULL)
		return;

	dialog = gtk_dialog_new_with_buttons(_("Attach Codex session"),
	                                     GTK_WINDOW(sakura.main_window),
	                                     GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR,
	                                     _("_Cancel"), GTK_RESPONSE_CANCEL,
	                                     _("_Attach"), GTK_RESPONSE_ACCEPT,
	                                     NULL);
	entry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(entry), _("Session ID or name"));
	if (tab->codex_session_id != NULL)
		gtk_entry_set_text(GTK_ENTRY(entry), tab->codex_session_id);
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
	                   entry, FALSE, FALSE, 12);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
	gtk_widget_show_all(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		session = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
		g_strstrip(session);
		if (session[0] != '\0') {
			sakura_session_accept_changes();
			g_free(tab->codex_session_id);
			tab->codex_session_id = session;
			g_clear_pointer(&tab->codex_resume_cwd, g_free);
			tab->codex_resume_cwd_query_active = FALSE;
			tab->codex_resume_cwd_lookup_done = FALSE;
			tab->codex_session_query_active = FALSE;
			g_free(tab->codex_session_name);
			tab->codex_session_name = sakura_codex_session_id_is_uuid(session)
			                         ? NULL : g_strdup(session);
			tab->kind = SAKURA_TAB_CODEX;
			sakura_sidebar_update_tab(tab);
			sakura_codex_sync_name(tab);
			sakura_session_mark_dirty();
			sakura_session_flush();
		} else {
			g_free(session);
		}
	}
	gtk_widget_destroy(dialog);
}


void
sakura_refresh_codex_name_cb (GtkWidget *widget, void *data)
{
	gint page;
	SakuraTab *tab;

	(void)widget;
	(void)data;
	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	if (page < 0)
		return;
	tab = sakura_tab_at_page(page);
	sakura_codex_sync_name(tab);
}


void
sakura_install_codex_hook_cb (GtkWidget *widget, void *data)
{
	gchar *hook, *standard_error = NULL;
	GError *error = NULL;
	gint status;

	(void)data;
	hook = g_find_program_in_path("sakura-codex-session-hook");
	if (hook == NULL) {
		sakura_error(_("The Codex hook is not installed. Run scripts/sakura-codex-session-hook --install from the Sakura source tree first."));
		return;
	}
	g_free(hook);

	if (!g_spawn_command_line_sync("sakura-codex-session-hook --install",
	                               NULL, &standard_error, &status, &error) || status != 0) {
		sakura_error(_("Could not install the Codex session hook: %s"),
		             error != NULL ? error->message :
		             (standard_error != NULL ? standard_error : _("unknown error")));
		g_clear_error(&error);
		g_free(standard_error);
		return;
	}
	g_free(standard_error);
	sakura_codex_tracking_menu_update(widget);

	{
		GtkWidget *message = gtk_message_dialog_new(GTK_WINDOW(sakura.main_window),
		                                           GTK_DIALOG_DESTROY_WITH_PARENT,
		                                           GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
		                                           _("Codex session tracking is enabled."));
		gtk_dialog_run(GTK_DIALOG(message));
		gtk_widget_destroy(message);
	}
}


void
sakura_tab_spawn_tool(SakuraTab *tab, const gchar *cwd, gchar **env)
{
	gchar *argv[] = { NULL, NULL, NULL, NULL, NULL, NULL };
	gchar *executable;
	const gchar *pr_command =
		"gh pr view \"$1\"; "
		"printf '\\n[Pull request view finished. Press Ctrl-D to close.]\\n'; "
		"exec \"${SHELL:-sh}\"";

#ifdef HAVE_WEBKITGTK
	if (tab->tool == SAKURA_TOOL_GH_PR) {
		GtkWidget *browser_box, *toolbar, *reload_button, *external_button;
		WebKitWebView *view;

		if (tab->tool_target == NULL || tab->tool_target[0] == '\0') {
			sakura_tab_set_status(tab, SAKURA_TAB_STATUS_ERROR, TRUE);
			return;
		}
		browser_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
		tab->browser_back = gtk_button_new_from_icon_name("go-previous-symbolic",
		                                                  GTK_ICON_SIZE_MENU);
		gtk_button_set_relief(GTK_BUTTON(tab->browser_back), GTK_RELIEF_NONE);
		gtk_widget_set_tooltip_text(tab->browser_back, _("Back"));
		tab->browser_forward = gtk_button_new_from_icon_name("go-next-symbolic",
		                                                     GTK_ICON_SIZE_MENU);
		gtk_button_set_relief(GTK_BUTTON(tab->browser_forward), GTK_RELIEF_NONE);
		gtk_widget_set_tooltip_text(tab->browser_forward, _("Forward"));
		reload_button = gtk_button_new_from_icon_name("view-refresh-symbolic",
		                                              GTK_ICON_SIZE_MENU);
		gtk_button_set_relief(GTK_BUTTON(reload_button), GTK_RELIEF_NONE);
		gtk_widget_set_tooltip_text(reload_button, _("Reload"));
		external_button = gtk_button_new_from_icon_name("external-link-symbolic",
		                                                GTK_ICON_SIZE_MENU);
		gtk_button_set_relief(GTK_BUTTON(external_button), GTK_RELIEF_NONE);
		gtk_widget_set_tooltip_text(external_button, _("Open in external browser"));
		gtk_box_pack_start(GTK_BOX(toolbar), tab->browser_back, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(toolbar), tab->browser_forward, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(toolbar), reload_button, FALSE, FALSE, 0);
		gtk_box_pack_end(GTK_BOX(toolbar), external_button, FALSE, FALSE, 0);

		view = WEBKIT_WEB_VIEW(webkit_web_view_new());
		tab->browser = GTK_WIDGET(view);
		gtk_widget_set_can_focus(tab->browser, TRUE);
		g_signal_connect(tab->browser, "focus-in-event",
		                 G_CALLBACK(sakura_pane_focus_in_cb), tab);
		gtk_widget_hide(tab->vte);
		gtk_widget_hide(tab->scrollbar);
		gtk_box_pack_start(GTK_BOX(browser_box), toolbar, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(browser_box), tab->browser, TRUE, TRUE, 0);
		gtk_box_pack_start(GTK_BOX(tab->hbox), browser_box, TRUE, TRUE, 0);
		g_signal_connect(tab->browser_back, "clicked", G_CALLBACK(sakura_browser_back_cb), tab);
		g_signal_connect(tab->browser_forward, "clicked", G_CALLBACK(sakura_browser_forward_cb), tab);
		g_signal_connect(reload_button, "clicked", G_CALLBACK(sakura_browser_reload_cb), tab);
		g_signal_connect(external_button, "clicked", G_CALLBACK(sakura_browser_external_cb), tab);
		g_signal_connect(view, "notify::can-go-back",
		                 G_CALLBACK(sakura_browser_navigation_changed_cb), tab);
		g_signal_connect(view, "notify::can-go-forward",
		                 G_CALLBACK(sakura_browser_navigation_changed_cb), tab);
		gtk_widget_show_all(browser_box);
		sakura_browser_update_navigation(tab);
		webkit_web_view_load_uri(view, tab->tool_target);
		return;
	}
#endif

	if (tab->tool == SAKURA_TOOL_GH_PR)
		executable = g_find_program_in_path("sh");
	else
		executable = sakura_find_tool_executable(tab->tool);
	if (executable == NULL) {
		sakura_tab_set_status(tab, SAKURA_TAB_STATUS_ERROR, TRUE);
		return;
	}
	argv[0] = executable;
	if (tab->tool == SAKURA_TOOL_GH_DASH) {
		argv[1] = (gchar *)"dash";
	} else if (tab->tool == SAKURA_TOOL_GH_PR) {
		if (tab->tool_target == NULL || tab->tool_target[0] == '\0') {
			g_free(executable);
			sakura_tab_set_status(tab, SAKURA_TAB_STATUS_ERROR, TRUE);
			return;
		}
		argv[1] = (gchar *)"-c";
		argv[2] = (gchar *)pr_command;
		argv[3] = (gchar *)"sakura-gh-pr";
		argv[4] = tab->tool_target;
	}
	vte_terminal_spawn_async(VTE_TERMINAL(tab->vte), VTE_PTY_NO_HELPER, cwd,
	                         argv, env, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL,
	                         -1, NULL, sakura_spawn_callback, NULL);
	g_free(executable);
}

static gboolean
sakura_codex_status_from_state(const gchar *state, SakuraTabStatus *status,
                                gboolean *attention)
{
	if (g_strcmp0(state, "idle") == 0) {
		*status = SAKURA_TAB_STATUS_IDLE;
		*attention = FALSE;
	} else if (g_strcmp0(state, "running") == 0) {
		*status = SAKURA_TAB_STATUS_RUNNING;
		*attention = FALSE;
	} else if (g_strcmp0(state, "needs-approval") == 0) {
		*status = SAKURA_TAB_STATUS_NEEDS_APPROVAL;
		*attention = TRUE;
	} else if (g_strcmp0(state, "ready") == 0) {
		*status = SAKURA_TAB_STATUS_READY;
		*attention = TRUE;
	} else if (g_strcmp0(state, "interrupted") == 0 ||
	           g_strcmp0(state, "cancelled") == 0 ||
	           g_strcmp0(state, "canceled") == 0) {
		*status = SAKURA_TAB_STATUS_INTERRUPTED;
		*attention = FALSE;
	} else if (g_strcmp0(state, "error") == 0) {
		*status = SAKURA_TAB_STATUS_ERROR;
		*attention = TRUE;
	} else {
		return FALSE;
	}
	return TRUE;
}


gboolean
sakura_codex_interrupt_matches_event(const SakuraTab *tab,
                                      const gchar *event_name,
                                      const gchar *turn_id)
{
	if (tab == NULL || !tab->codex_interrupt_requested ||
	    g_strcmp0(event_name, "UserPromptSubmit") == 0)
		return FALSE;
	if (turn_id != NULL && turn_id[0] != '\0' &&
	    tab->codex_interrupt_turn_id != NULL)
		return g_strcmp0(turn_id, tab->codex_interrupt_turn_id) == 0;
	/* Older hook payloads may not carry turn_id. Hold the local interrupt
	 * until a new user prompt gives us an unambiguous new turn. */
	return turn_id == NULL || turn_id[0] == '\0';
}


gboolean
sakura_codex_tracking_poll_cb(gpointer data)
{
	GDir *dir;
	const gchar *filename;
	gboolean changed = FALSE;

	(void)data;
	if (sakura.codex_tracking_dir == NULL)
		return G_SOURCE_CONTINUE;
	dir = g_dir_open(sakura.codex_tracking_dir, 0, NULL);
	if (dir == NULL)
		return G_SOURCE_CONTINUE;

	while ((filename = g_dir_read_name(dir)) != NULL) {
		gchar *path, *contents, *session_id = NULL, *state = NULL;
		gchar *event_name = NULL, *turn_id = NULL;
		GKeyFile *tracking;
		gsize length;
		guint page;
		SakuraTab *matched_tab = NULL;
		SakuraTabStatus status;
		gboolean attention;

		if (filename[0] == '.')
			continue;
		path = g_build_filename(sakura.codex_tracking_dir, filename, NULL);
		if (!g_file_get_contents(path, &contents, &length, NULL)) {
			g_free(path);
			continue;
		}
		g_strstrip(contents);
		if (contents[0] == '\0') {
			g_free(contents);
			g_free(path);
			continue;
		}

		tracking = g_key_file_new();
		if (g_key_file_load_from_data(tracking, contents, length,
		                              G_KEY_FILE_NONE, NULL)) {
			session_id = g_key_file_get_string(tracking, "tracking", "session_id", NULL);
			state = g_key_file_get_string(tracking, "tracking", "state", NULL);
			event_name = g_key_file_get_string(tracking, "tracking", "event", NULL);
			turn_id = g_key_file_get_string(tracking, "tracking", "turn_id", NULL);
		} else {
			session_id = g_strdup(contents);
		}
		g_key_file_free(tracking);
		if (session_id == NULL || session_id[0] == '\0') {
			g_free(session_id);
			g_free(state);
			g_free(event_name);
			g_free(turn_id);
			g_free(contents);
			g_free(path);
			continue;
		}

		if (sakura.workspace->tabs != NULL) {
			for (page = 0; page < sakura.workspace->tabs->len; page++) {
				SakuraTab *tab = sakura_tab_at_page((gint)page);
				if (tab == NULL || tab->codex_tracking_token == NULL ||
				    g_strcmp0(tab->codex_tracking_token, filename) != 0)
					continue;
				if (tab->kind != SAKURA_TAB_CODEX) {
					tab->kind = SAKURA_TAB_CODEX;
					sakura_sidebar_update_tab(tab);
					changed = TRUE;
				}
				if (g_strcmp0(tab->codex_session_id, session_id) != 0) {
					g_free(tab->codex_session_id);
					tab->codex_session_id = g_strdup(session_id);
					g_clear_pointer(&tab->codex_resume_cwd, g_free);
					tab->codex_resume_cwd_query_active = FALSE;
					tab->codex_resume_cwd_lookup_done = FALSE;
					tab->codex_session_query_active = FALSE;
					changed = TRUE;
				}
				if (state != NULL && sakura_codex_status_from_state(state, &status, &attention)) {
					gboolean preserve_interrupt;
					gboolean preserve_restored_state =
						tab->attention_restore_pending &&
						g_strcmp0(event_name, "SessionStart") == 0;

					if (turn_id != NULL && turn_id[0] != '\0') {
						g_free(tab->codex_turn_id);
						tab->codex_turn_id = g_strdup(turn_id);
					}
					preserve_interrupt = sakura_codex_interrupt_matches_event(
						tab, event_name, turn_id);
					if (preserve_restored_state) {
						/* Resuming a saved Codex session emits an initial idle
						 * SessionStart event. It describes process startup, not an
						 * acknowledgement of the saved ready/attention state. Keep
						 * the restored marker until a real turn event arrives. */
					} else if (status == SAKURA_TAB_STATUS_RUNNING && preserve_interrupt) {
						/* A delayed running event from the interrupted turn must
						 * not restart the activity spinner. */
					} else {
						if (status == SAKURA_TAB_STATUS_READY && preserve_interrupt) {
							status = SAKURA_TAB_STATUS_INTERRUPTED;
							attention = FALSE;
						}
						if (tab->codex_interrupt_requested) {
							tab->codex_interrupt_requested = FALSE;
							g_clear_pointer(&tab->codex_interrupt_turn_id, g_free);
						}
						sakura_tab_set_status(tab, status, attention);
					}
				} else if (state == NULL && tab->status == SAKURA_TAB_STATUS_NONE) {
					sakura_tab_set_status(tab, SAKURA_TAB_STATUS_IDLE, FALSE);
				}
				matched_tab = tab;
				g_remove(path);
				break;
			}
		}
		if (matched_tab != NULL)
			sakura_codex_sync_name(matched_tab);
		g_free(contents);
		g_free(session_id);
		g_free(state);
		g_free(event_name);
		g_free(turn_id);
		g_free(path);
	}
	g_dir_close(dir);
	if (changed)
		sakura_session_mark_dirty();
	return G_SOURCE_CONTINUE;
}


gboolean
sakura_codex_session_id_is_uuid(const gchar *value)
{
	gsize index;

	if (value == NULL || strlen(value) != 36)
		return FALSE;
	for (index = 0; index < 36; index++) {
		if (index == 8 || index == 13 || index == 18 || index == 23) {
			if (value[index] != '-')
				return FALSE;
		} else if (!g_ascii_isxdigit(value[index])) {
			return FALSE;
		}
	}
	return TRUE;
}


SakuraTab *
sakura_find_tool_tab(SakuraToolKind tool, const gchar *cwd)
{
	guint page;

	if (tool == SAKURA_TOOL_NONE || sakura.workspace->tabs == NULL)
		return NULL;

	for (page = 0; page < sakura.workspace->tabs->len; page++) {
		SakuraTab *tab = sakura_tab_at_page((gint)page);

		if (tab == NULL || tab->kind != SAKURA_TAB_TOOL || tab->tool != tool)
			continue;
		/* The dashboard is global; repository tools get one instance per repository. */
		if (sakura_tool_requires_git_repository(tool) &&
		    g_strcmp0(tab->cwd, cwd) != 0)
			continue;
		return tab;
	}
	return NULL;
}

SakuraTab *
sakura_find_tool_target_tab(SakuraToolKind tool, const gchar *target)
{
	guint page;

	if (tool == SAKURA_TOOL_NONE || target == NULL || sakura.workspace->tabs == NULL)
		return NULL;

	for (page = 0; page < sakura.workspace->tabs->len; page++) {
		SakuraTab *tab = sakura_tab_at_page((gint)page);

		if (tab != NULL && tab->kind == SAKURA_TAB_TOOL &&
		    tab->tool == tool && g_strcmp0(tab->tool_target, target) == 0)
			return tab;
	}
	return NULL;
}
#endif
