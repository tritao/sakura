/*******************************************************************************
 *  Filename: sakura.c
 *  Description: VTE-based terminal emulator
 *
 *           Copyright (C) 2006-2021  David Gómez <david@pleyades.net>
 *           Copyright (C) 2008       Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <wchar.h>
#include <math.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <locale.h>
#include <libintl.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gdesktopappinfo.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <pango/pango.h>
#include <vte/vte.h>

#define _(String) gettext(String)
#define N_(String) (String)
#define GETTEXT_PACKAGE "sakura"

#define SAY(format,...) do {\
	if (strcmp("Debug", BUILDTYPE)==0) {\
	    fprintf(stderr, "[%d] ", getpid());\
	    fprintf(stderr, "[%s] ", __FUNCTION__);\
	    if (format) fprintf(stderr, format, ##__VA_ARGS__);\
	    fputc('\n', stderr);\
		fflush(stderr);\
	}\
} while (0)

#define PALETTE_SIZE 16

/* 16 color palettes in GdkRGBA format (red, green, blue, alpha) */

const GdkRGBA gruvbox_palette[PALETTE_SIZE] = {
	{0.156863, 0.156863, 0.156863, 1.000000},
	{0.800000, 0.141176, 0.113725, 1.000000},
	{0.596078, 0.592157, 0.101961, 1.000000},
	{0.843137, 0.600000, 0.129412, 1.000000},
	{0.270588, 0.521569, 0.533333, 1.000000},
	{0.694118, 0.384314, 0.525490, 1.000000},
	{0.407843, 0.615686, 0.415686, 1.000000},
	{0.658824, 0.600000, 0.517647, 1.000000},
	{0.572549, 0.513725, 0.454902, 1.000000},
	{0.984314, 0.286275, 0.203922, 1.000000},
	{0.721569, 0.733333, 0.149020, 1.000000},
	{0.980392, 0.741176, 0.184314, 1.000000},
	{0.513725, 0.647059, 0.596078, 1.000000},
	{0.827451, 0.525490, 0.607843, 1.000000},
	{0.556863, 0.752941, 0.486275, 1.000000},
	{0.921569, 0.858824, 0.698039, 1.000000}
};

const GdkRGBA tango_palette[PALETTE_SIZE] = {
	{0.133333, 0.133333, 0.149020, 1},
	{0.8,      0,        0,        1},
	{0.305882, 0.603922, 0.023529, 1},
	{0.768627, 0.627451, 0,        1},
	{0.203922, 0.396078, 0.643137, 1},
	{0.458824, 0.313725, 0.482353, 1},
	{0.0235294,0.596078, 0.603922, 1},
	{0.827451, 0.843137, 0.811765, 1},
	{0.333333, 0.341176, 0.32549,  1},
	{0.937255, 0.160784, 0.160784, 1},
	{0.541176, 0.886275, 0.203922, 1},
	{0.988235, 0.913725, 0.309804, 1},
	{0.447059, 0.623529, 0.811765, 1},
	{0.678431, 0.498039, 0.658824, 1},
	{0.203922, 0.886275, 0.886275, 1},
	{0.933333, 0.933333, 0.92549,  1}
};

const GdkRGBA linux_palette[PALETTE_SIZE] = {
	{0,        0,        0,        1},
	{0.666667, 0,        0,        1},
	{0,        0.666667, 0,        1},
	{0.666667, 0.333333, 0,        1},
	{0,        0,        0.666667, 1},
	{0.666667, 0,        0.666667, 1},
	{0,        0.666667, 0.666667, 1},
	{0.666667, 0.666667, 0.666667, 1},
	{0.333333, 0.333333, 0.333333, 1},
	{1,        0.333333, 0.333333, 1},
	{0.333333, 1,        0.333333, 1},
	{1,        1,        0.333333, 1},
	{0.333333, 0.333333, 1,        1},
	{1,        0.333333, 1,        1},
	{0.333333, 1,        1,        1},
	{1,        1,        1,        1}
};

const GdkRGBA solarized_palette[PALETTE_SIZE] = {
	{0.027451, 0.211765, 0.258824, 1}, // 0 base02
	{0.862745, 0.196078, 0.184314, 1}, // 1 red
	{0.521569, 0.600000, 0.000000, 1}, // 2 green
	{0.709804, 0.537255, 0.000000, 1}, // 3 yellow
	{0.149020, 0.545098, 0.823529, 1}, // 4 blue
	{0.827451, 0.211765, 0.509804, 1}, // 5 magenta
	{0.164706, 0.631373, 0.596078, 1}, // 6 cyan
	{0.933333, 0.909804, 0.835294, 1}, // 7 base2
	{0.000000, 0.168627, 0.211765, 1}, // 8 base03 (bg)
	{0.796078, 0.294118, 0.086275, 1}, // 9 orange
	{0.345098, 0.431373, 0.458824, 1}, // 10 base01
	{0.396078, 0.482353, 0.513725, 1}, // 11 base00
	{0.513725, 0.580392, 0.588235, 1}, // 12 base0 (fg)
	{0.423529, 0.443137, 0.768627, 1}, // 13 violet
	{0.576471, 0.631373, 0.631373, 1}, // 14 base1
	{0.992157, 0.964706, 0.890196, 1}  // 15 base3
};

const GdkRGBA nord_palette[PALETTE_SIZE] = {
	{0.0,        0.0,        0.0234375,  1.0},
	{0.74609375, 0.37890625, 0.4140625,  1.0},
	{0.63671875, 0.7421875,  0.546875,   1.0},
	{0.91796875, 0.79296875, 0.54296875, 1.0},
	{0.50390625, 0.62890625, 0.75390625, 1.0},
	{0.703125,   0.5546875,  0.67578125, 1.0},
	{0.53125,    0.75,       0.8125,     1.0},
	{0.89453125, 0.91015625, 0.9375,     1.0},
	{0.296875,   0.3359375,  0.4140625,  1.0},
	{0.74609375, 0.37890625, 0.4140625,  1.0},
	{0.63671875, 0.7421875,  0.546875,   1.0},
	{0.91796875, 0.79296875, 0.54296875, 1.0},
	{0.50390625, 0.62890625, 0.75390625, 1.0},
	{0.703125,   0.5546875,  0.67578125, 1.0},
	{0.55859375, 0.734375,   0.73046875, 1.0},
	{0.921875,   0.93359375, 0.953125,   1.0}
};


const GdkRGBA xterm_palette[PALETTE_SIZE] = {
	{0,        0,        0,        1},
	{0.803922, 0,        0,        1},
	{0,        0.803922, 0,        1},
	{0.803922, 0.803922, 0,        1},
	{0.117647, 0.564706, 1,        1},
	{0.803922, 0,        0.803922, 1},
	{0,        0.803922, 0.803922, 1},
	{0.898039, 0.898039, 0.898039, 1},
	{0.298039, 0.298039, 0.298039, 1},
	{1,        0,        0,        1},
	{0,        1,        0,        1},
	{1,        1,        0,        1},
	{0.27451,  0.509804, 0.705882, 1},
	{1,        0,        1,        1},
	{0,        1,        1,        1},
	{1,        1,        1,        1}
};

const GdkRGBA rxvt_palette[PALETTE_SIZE] = {
	{0,        0,        0,        1},
	{0.803921, 0,        0,        1},
	{0,        0.803921, 0,        1},
	{0.803921, 0.803921, 0,        1},
	{0,        0,        0.803921, 1},
	{0.803921, 0,        0.803921, 1},
	{0,        0.803921, 0.803921, 1},
	{0.980392, 0.921568, 0.843137, 1},
	{0.250980, 0.250980, 0.250980, 1},
	{1,        0,        0,        1},
	{0,        1,        0,        1},
	{1,        1,        0,        1},
	{0,        0,        1,        1},
	{1,        0,        1,        1},
	{0,        1,        1,        1},
	{1,        1,        1,        1}
};

const GdkRGBA hybrid_palette[PALETTE_SIZE] = {
	{0.1568627450980392  , 0.16470588235294117 , 0.1803921568627451  , 1} ,
	{0.6470588235294118  , 0.25882352941176473 , 0.25882352941176473 , 1} ,
	{0.5490196078431373  , 0.5803921568627451  , 0.25098039215686274 , 1} ,
	{0.8705882352941177  , 0.5764705882352941  , 0.37254901960784315 , 1} ,
	{0.37254901960784315 , 0.5058823529411764  , 0.615686274509804   , 1} ,
	{0.5215686274509804  , 0.403921568627451   , 0.5607843137254902  , 1} ,
	{0.3686274509803922  , 0.5529411764705883  , 0.5294117647058824  , 1} ,
	{0.4392156862745098  , 0.47058823529411764 , 0.5019607843137255  , 1} ,
	{0.21568627450980393 , 0.23137254901960785 , 0.2549019607843137  , 1} ,
	{0.8                 , 0.4                 , 0.4                 , 1} ,
	{0.7098039215686275  , 0.7411764705882353  , 0.40784313725490196 , 1} ,
	{0.9411764705882353  , 0.7764705882352941  , 0.4549019607843137  , 1} ,
	{0.5058823529411764  , 0.6352941176470588  , 0.7450980392156863  , 1} ,
	{0.6980392156862745  , 0.5803921568627451  , 0.7333333333333333  , 1} ,
	{0.5411764705882353  , 0.7450980392156863  , 0.7176470588235294  , 1} ,
	{0.7725490196078432  , 0.7843137254901961  , 0.7764705882352941  , 1}
};

const char *palettes_names[]= {"Solarized", "Tango", "Gruvbox", "Nord", "Xterm", "Linux", "Rxvt", "Hybrid", "GNOME Terminal", NULL};
const GdkRGBA *palettes[] = {solarized_palette, tango_palette, gruvbox_palette, nord_palette, xterm_palette, linux_palette, rxvt_palette, hybrid_palette, NULL};
#define DEFAULT_PALETTE 1 /* Tango palette */
#define SYSTEM_PALETTE_INDEX 8

/* Defaults matching the active GNOME Terminal profile on Ubuntu. */
#define DEFAULT_FOREGROUND_COLOR "rgb(211,215,207)" /* #D3D7CF */
#define DEFAULT_BACKGROUND_COLOR "rgb(34,34,38)"    /* #222226 */
#define DEFAULT_CURSOR_COLOR "rgb(211,215,207)"     /* #D3D7CF */

/* Color schemes (fg&bg) for sakura. Each colorset can use a different scheme */
struct scheme {
	gchar *name;
	GdkRGBA bg;
	GdkRGBA fg;
};

#define NUM_SCHEMES 5
#define DEFAULT_SCHEME 1
struct scheme predefined_schemes[NUM_SCHEMES] = {
	{"Custom", {0, 0, 0, 1}, {1, 1, 1, 1}}, /* Custom values are ignored, we use the ones chosen by the user */
	{"White on black", {0, 0, 0, 1}, {1, 1, 1, 1}},
	{"Green on black", {0, 0, 0, 1}, {0.4, 1, 0, 1}},
	{"Solarized dark", {0.000000, 0.168627, 0.211765, 1}, {0.513725, 0.580392, 0.588235, 1}},
	{"Solarized light", {0.992157, 0.964706, 0.890196, 1}, {0.396078, 0.482353, 0.513725, 1}}
};

/* Keep the client-side title bar compact, like GNOME Terminal's title bar. */
#define SAKURA_CSS "\
#sakura headerbar {\
	min-height: 24px;\
	padding: 0 2px;\
}\
#sakura headerbar button {\
	min-width: 22px;\
	min-height: 22px;\
	padding: 0;\
}\
#sakura headerbar label {\
	padding: 0;\
}\
#terminal-sidebar > dndtarget:drop(active) {\
	background-color: alpha(@theme_selected_bg_color, 0.14);\
	border: 2px solid alpha(@theme_selected_bg_color, 0.72);\
	border-radius: 4px;\
	box-shadow: none;\
}\
#terminal-sidebar > dndtarget:drop(active).before,\
#terminal-sidebar > dndtarget:drop(active).after {\
	background-color: transparent;\
	border: 0;\
	border-radius: 0;\
	box-shadow: none;\
}\
#terminal-sidebar > dndtarget:drop(active).before {\
	border-top: 2px solid alpha(@theme_selected_bg_color, 0.85);\
}\
#terminal-sidebar > dndtarget:drop(active).after {\
	border-bottom: 2px solid alpha(@theme_selected_bg_color, 0.85);\
}"

enum {
	SAKURA_SIDEBAR_COLUMN_TITLE,
	SAKURA_SIDEBAR_COLUMN_SUBTITLE,
	SAKURA_SIDEBAR_COLUMN_MARKUP,
	SAKURA_SIDEBAR_COLUMN_ICON,
	SAKURA_SIDEBAR_COLUMN_TOOLTIP,
	SAKURA_SIDEBAR_COLUMN_NODE,
	SAKURA_SIDEBAR_N_COLUMNS
};

#define DEFAULT_SIDEBAR_WIDTH 200

#define FADE_WINDOW_CSS "\
window#fade_window {\
	background-color: black;\
} "

#define FADE_WINDOW_OPACITY 0.5

#define NUM_COLORSETS 6
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>



/* Tab bar visibility */
typedef enum {
	SHOW_TAB_BAR_ALWAYS,
	SHOW_TAB_BAR_MULTIPLE,
	SHOW_TAB_BAR_NEVER
} ShowTabBar;


/* Global sakura data */
static struct {
	GtkWidget *main_window;
	GtkWidget *header_bar;
	GtkWidget *sidebar_paned;
	GtkWidget *sidebar;
	GtkWidget *sidebar_tree;
	GtkTreeStore *sidebar_model;
	GtkTreeSelection *sidebar_selection;
	struct sakura_sidebar_node *sidebar_root;
	GList *sidebar_groups;
	guint sidebar_next_group_id;
	gboolean sidebar_syncing;
	gboolean sidebar_visible;
	gint sidebar_width;
	GtkWidget *notebook;
	GtkWidget *menu;
	GtkWidget *fade_window;  /* Window used for fading effect */
	PangoFontDescription *font;
	gdouble line_height; /* Font line height */
	GdkRGBA forecolors[NUM_COLORSETS];
	GdkRGBA backcolors[NUM_COLORSETS];
	GdkRGBA curscolors[NUM_COLORSETS];
	guint schemes[NUM_COLORSETS];  /* Selected color scheme for each colorset */
	const GdkRGBA *palette;
	guint palette_idx;
	GdkRGBA system_foreground;
	GdkRGBA system_background;
	GdkRGBA system_cursor;
	GdkRGBA system_palette[PALETTE_SIZE];
	bool have_system_colors;
	bool system_bold_is_bright;
	gint last_colorset;
	char *current_match;
	guint width;
	guint height;
	glong columns;
	glong rows;
	gint scroll_lines;
	VteCursorShape cursor_type;
	ShowTabBar show_tab_bar;         /* Show the tab bar: always, multiple, never */
	bool show_scrollbar;
	bool show_closebutton;
	bool new_tab_after_current;
	bool tabs_on_bottom;
	bool less_questions;
        bool copy_on_select;
	bool urgent_bell;
	bool audible_bell;
	bool blinking_cursor;
	bool fullscreen;
	bool config_modified;            /* Configuration has been modified */
	bool externally_modified;        /* Configuration file has been modified by another process */
	bool resized;
	bool disable_numbered_tabswitch; /* For disabling direct tabswitching key */
	bool use_fading;                 /* Fade the window when the focus change */
	bool scrollable_tabs;
	bool bold_is_bright;             /* Show bold characters as bright */
	bool dont_save;                  /* Don't save config file */
	bool first_run;                  /* To only execute commands first time sakura is launched */
	bool session_restoring;
	bool session_ready;
	bool session_shutting_down;
	guint session_save_source_id;
	guint codex_tracking_source_id;
	GtkWidget *item_copy_link;       /* We include here only the items which need to be hidden */
	GtkWidget *item_open_link;
	GtkWidget *item_open_mail;
	GtkWidget *open_link_separator;
	GKeyFile *cfg;
	GKeyFile *session_cfg;
	char *configfile;
	char *sessionfile;
	char *codex_tracking_dir;
	char *history_dir;
	char *icon;
	char *shell_path;
	char *main_title;		/* Main window static title from user input */
	char *term;
	gchar *tab_default_title;
	gint add_tab_accelerator;
	gint del_tab_accelerator;
	gint switch_tab_accelerator;
	gint move_tab_accelerator;
	gint copy_accelerator;
	gint scrollbar_accelerator;
	gint open_url_accelerator;
	gint font_size_accelerator;
	gint set_tab_name_accelerator;
	gint search_accelerator;
	gint set_colorset_accelerator;
	gint new_window_accelerator;
	gint add_tab_key;
	gint del_tab_key;
	gint prev_tab_key;
	gint next_tab_key;
	gint copy_key;
	gint paste_key;
	gint scrollbar_key;
	gint set_tab_name_key;
	gint search_key;
	gint fullscreen_key;
	gint increase_font_size_key;
	gint decrease_font_size_key;
	gint set_colorset_keys[NUM_COLORSETS];
	gint paste_button;
	gint menu_button;
	gint new_window_key;
	int orig_argc; /* Used for new windows */
	char** orig_argv; /* Used for new windows */
	VteRegex *http_vteregexp, *mail_vteregexp;
	char *word_chars;                /* Exceptions for word selection */
	char *argv[3];
} sakura;

/* Data associated to each sakura tab */
struct sakura_tab;

typedef enum {
	SAKURA_TAB_SHELL,
	SAKURA_TAB_CODEX
} SakuraTabKind;

typedef enum {
	SAKURA_SIDEBAR_GROUP,
	SAKURA_SIDEBAR_TERMINAL
} SakuraSidebarNodeType;

struct sakura_sidebar_node {
	SakuraSidebarNodeType type;
	gchar *id;
	gchar *title;
	gchar *subtitle;
	gchar *tooltip;
	struct sakura_sidebar_node *parent;
	struct sakura_tab *tab;
	GtkTreeRowReference *row;
};

struct sakura_tab {
	GtkWidget *hbox;
	GtkWidget *label;
	GtkWidget *vte;      /* Reference to VTE terminal */
	GtkWidget *scrollbar;
	GtkBorder padding;   /* inner-property data */
	bool label_set_byuser;
	int colorset;
	GPid pid;           /* pid of the forked process */
	gulong exit_handler_id;
	gchar *cwd;
	gchar *host;
	gchar *raw_title;
	gchar *terminal_id;
	SakuraTabKind kind;
	gchar *codex_session_id;
	gchar *codex_session_name;
	gchar *codex_tracking_token;
	gboolean codex_name_query_active;
	struct sakura_sidebar_node *sidebar_node;
};

#define ICON_FILE "terminal-tango.svg"
#define CODEX_ICON_NAME "sakura-codex"
#define CODEX_HOOK_MARKER "sakura-codex-session-hook"
#define SCROLL_LINES 4096
#define DEFAULT_SCROLL_LINES 4096
#define HTTP_REGEXP "(ftp|http)s?://[^ \t\n\b]+[^.,!? \t\n\b()<>{}«»„“”‚‘’\\[\\]\'\"]"
#define MAIL_REGEXP "[^ \t\n\b()<>{}«»„“”‚‘’\\[\\]\'\"][^ \t\n\b]*@([^ \t\n\b()<>{}«»„“”‚‘’\\[\\]\'\"]+\\.)+([a-zA-Z]{2,})"
#define DEFAULT_CONFIGFILE "sakura.conf"
#define DEFAULT_COLUMNS 80
#define DEFAULT_ROWS 24
#define DEFAULT_MIN_WIDTH_CHARS 20
#define DEFAULT_MIN_HEIGHT_CHARS 1
#define DEFAULT_FONT "Monospace 10"
#define DEFAULT_LINE_HEIGHT 1.0
#define FONT_MINIMAL_SIZE (PANGO_SCALE*6)
#define DEFAULT_WORD_CHARS "-,./?%&#_~:"
#define TAB_MAX_SIZE 40
#define TAB_MIN_SIZE 6
#define FORWARD 1
#define BACKWARDS 2
#define DEFAULT_ADD_TAB_ACCELERATOR  (GDK_CONTROL_MASK|GDK_SHIFT_MASK)
#define DEFAULT_DEL_TAB_ACCELERATOR  (GDK_CONTROL_MASK|GDK_SHIFT_MASK)
#define DEFAULT_SWITCH_TAB_ACCELERATOR  (GDK_MOD1_MASK)
#define DEFAULT_MOVE_TAB_ACCELERATOR (GDK_MOD1_MASK|GDK_SHIFT_MASK)
#define DEFAULT_COPY_ACCELERATOR  (GDK_CONTROL_MASK|GDK_SHIFT_MASK)
#define DEFAULT_SCROLLBAR_ACCELERATOR  (GDK_CONTROL_MASK|GDK_SHIFT_MASK)
#define DEFAULT_OPEN_URL_ACCELERATOR GDK_CONTROL_MASK
#define DEFAULT_FONT_SIZE_ACCELERATOR (GDK_CONTROL_MASK)
#define DEFAULT_SET_TAB_NAME_ACCELERATOR (GDK_CONTROL_MASK|GDK_SHIFT_MASK)
#define DEFAULT_SEARCH_ACCELERATOR (GDK_CONTROL_MASK|GDK_SHIFT_MASK)
#define DEFAULT_SELECT_COLORSET_ACCELERATOR (GDK_CONTROL_MASK|GDK_SHIFT_MASK)
#define DEFAULT_NEW_WINDOW_ACCELERATOR (GDK_CONTROL_MASK|GDK_SHIFT_MASK)
#define DEFAULT_ADD_TAB_KEY  GDK_KEY_T
#define DEFAULT_DEL_TAB_KEY  GDK_KEY_W
#define DEFAULT_PREV_TAB_KEY  GDK_KEY_Left
#define DEFAULT_NEXT_TAB_KEY  GDK_KEY_Right
#define DEFAULT_COPY_KEY  GDK_KEY_C
#define DEFAULT_PASTE_KEY  GDK_KEY_V
#define DEFAULT_SCROLLBAR_KEY  GDK_KEY_S
#define DEFAULT_SET_TAB_NAME_KEY  GDK_KEY_N
#define DEFAULT_SEARCH_KEY  GDK_KEY_F
#define DEFAULT_FULLSCREEN_KEY  GDK_KEY_F11
#define DEFAULT_INCREASE_FONT_SIZE_KEY GDK_KEY_plus
#define DEFAULT_DECREASE_FONT_SIZE_KEY GDK_KEY_minus
#define DEFAULT_NEW_WINDOW_KEY GDK_KEY_O
#define DEFAULT_SCROLLABLE_TABS TRUE
#define DEFAULT_PASTE_BUTTON 2
#define DEFAULT_MENU_BUTTON 3

/* make this an array instead of #defines to get a compile time
 * error instead of a runtime if NUM_COLORSETS changes */
static int cs_keys[NUM_COLORSETS] =
		{GDK_KEY_F1, GDK_KEY_F2, GDK_KEY_F3, GDK_KEY_F4, GDK_KEY_F5, GDK_KEY_F6};

#define ERROR_BUFFER_LENGTH 256
const char cfg_group[] = "sakura";

/* Get a set sakura tab data from/to our GObject (notebook) */
static GQuark term_data_id = 0;
#define  sakura_get_sktab( sakura, page_idx )  \
    (struct sakura_tab*)g_object_get_qdata(  \
            G_OBJECT( gtk_notebook_get_nth_page( (GtkNotebook*)sakura.notebook, page_idx ) ), term_data_id);

#define  sakura_set_sktab( sakura, page_idx, sk_tab )  \
    g_object_set_qdata_full( \
            G_OBJECT( gtk_notebook_get_nth_page( (GtkNotebook*)sakura.notebook, page_idx) ), \
            term_data_id, sk_tab, (GDestroyNotify)g_free);

/* Configuration macros */
#define  sakura_set_config_integer(key, value) do {\
	g_key_file_set_integer(sakura.cfg, cfg_group, key, value);\
	sakura.config_modified=TRUE;\
	} while(0);

#define  sakura_set_config_string(key, value) do {\
	g_key_file_set_value(sakura.cfg, cfg_group, key, value);\
	sakura.config_modified=TRUE;\
	} while(0);

#define  sakura_set_config_boolean(key, value) do {\
	g_key_file_set_boolean(sakura.cfg, cfg_group, key, value);\
	sakura.config_modified=TRUE;\
	} while(0);

#define  sakura_set_config_double(key, value) do {\
	g_key_file_set_double(sakura.cfg, cfg_group, key, value);\
	sakura.config_modified=TRUE;\
	} while(0);


/* Spawn callback */
void sakura_spawn_callback (VteTerminal *, GPid, GError *, gpointer);
/* VTE callbacks */
static gboolean sakura_term_buttonpressed_cb (GtkWidget *, GdkEventButton *, gpointer);
static gboolean sakura_term_buttonreleased_cb (GtkWidget *, GdkEventButton *, gpointer);
static void     sakura_beep_cb (GtkWidget *, void *);
static void     sakura_increase_font_cb (GtkWidget *, void *);
static void     sakura_decrease_font_cb (GtkWidget *, void *);
static void     sakura_child_exited_cb (GtkWidget *, void *);
static void     sakura_eof_cb (GtkWidget *, void *);
static void     sakura_title_changed_cb (GtkWidget *, void *);
static gboolean sakura_delete_event_cb (GtkWidget *, void *);
static void     sakura_destroy_window_cb (GtkWidget *, void *);
/* Main window callbacks */
static gboolean sakura_key_press_cb (GtkWidget *, GdkEventKey *, gpointer);
static gboolean sakura_resized_window_cb (GtkWidget *, GdkEventConfigure *, void *);
static gboolean sakura_focus_in_cb (GtkWidget *, GdkEvent *, void *);
static gboolean sakura_focus_out_cb (GtkWidget *, GdkEvent *, void *);
static void     sakura_conf_changed_cb (GtkWidget *, void *);
static void     sakura_show_event_cb (GtkWidget *, gpointer);
/* Notebook, notebook labels and notebook buttons callbacks */
static void     sakura_switch_page_cb (GtkWidget *, GtkWidget *, guint, void *);
static void     sakura_page_removed_cb (GtkWidget *, void *);
static gboolean sakura_notebook_scroll_cb (GtkWidget *, GdkEventScroll *);
static gboolean sakura_label_clicked_cb (GtkWidget *, GdkEventButton *, void *);
static gboolean sakura_notebook_focus_cb (GtkWindow *, GdkEvent *, void *);
static void     sakura_closebutton_clicked_cb (GtkWidget *, void *);
/* Menuitem callbacks */
static void     sakura_font_dialog_cb (GtkWidget *, void *);
static void     sakura_set_name_dialog_cb (GtkWidget *, void *);
static void     sakura_color_dialog_cb (GtkWidget *, void *);
//static void     sakura_set_title_dialog (GtkWidget *, void *);
static void     sakura_new_tab_cb (GtkWidget *, void *);
static void     sakura_new_codex_cb (GtkWidget *, void *);
static void     sakura_resume_codex_cb (GtkWidget *, void *);
static void     sakura_install_codex_hook_cb (GtkWidget *, void *);
static void     sakura_close_tab_cb (GtkWidget *, void *);
static void     sakura_fullscreen_cb (GtkWidget *, void *);
static void     sakura_open_url_cb (GtkWidget *, void *);
static void     sakura_open_mail_cb (GtkWidget *, void *);
static void     sakura_copy_url_cb (GtkWidget *, void *);
static void     sakura_copy_cb (GtkWidget *, void *);
static void     sakura_paste_cb (GtkWidget *, void *);
static void     sakura_show_tab_bar_cb (GtkWidget *, void *);
static void     sakura_tabs_on_bottom_cb (GtkWidget *, void *);
static void     sakura_less_questions_cb (GtkWidget *, void *);
static void     sakura_copy_on_select_cb (GtkWidget *, void *);
static void     sakura_new_tab_after_current_cb (GtkWidget *, void *);
static void     sakura_show_scrollbar_cb (GtkWidget *, void *);
static void     sakura_disable_numbered_tabswitch_cb (GtkWidget *, void *);
//static void     sakura_use_fading_cb (GtkWidget *, void *);
static void     sakura_setname_entry_changed_cb (GtkWidget *, void *);
static void     sakura_set_cursor_cb (GtkWidget *, void *);
static void     sakura_blinking_cursor_cb (GtkWidget *, void *);
static void     sakura_audible_bell_cb (GtkWidget *, void *);
static void     sakura_urgent_bell_cb (GtkWidget *, void *);
static void     sakura_sidebar_init (void);
static void     sakura_sidebar_select_tab (struct sakura_tab *);
static void     sakura_sidebar_update_tab (struct sakura_tab *);
static void     sakura_sidebar_remove_tab (struct sakura_tab *);
static void     sakura_focus_tab (struct sakura_tab *);
static void     sakura_sidebar_save_groups (void);
static void     sakura_sidebar_model_reordered_cb (GtkTreeModel *, GtkTreePath *, GtkTreeIter *, gint *, void *);
static void     sakura_sidebar_new_group_cb (GtkWidget *, void *);
static void     sakura_sidebar_rename_group_cb (GtkWidget *, void *);
static void     sakura_sidebar_delete_group_cb (GtkWidget *, void *);
static void     sakura_sidebar_toggle_cb (GtkWidget *, void *);
static void     sakura_sidebar_selection_changed_cb (GtkTreeSelection *, void *);
static gboolean sakura_sidebar_button_press_cb (GtkWidget *, GdkEventButton *, void *);
static void     sakura_sidebar_paned_position_cb (GObject *, GParamSpec *, void *);
static void     sakura_session_load (void);
static void     sakura_session_save (void);
static void     sakura_session_schedule_save (void);
static gboolean sakura_session_restore (void);
static gboolean sakura_codex_tracking_poll_cb (gpointer);
static void     sakura_register_codex_icon (void);
static gboolean sakura_codex_session_id_is_uuid (const gchar *);
static void     sakura_codex_sync_name (struct sakura_tab *);
static void     sakura_refresh_codex_name_cb (GtkWidget *, void *);
static void     sakura_attach_codex_cb (GtkWidget *, void *);
static void     sakura_codex_tracking_status_cb (GtkWidget *, void *);

/* Misc */
static void     sakura_error (const char *, ...);
static void     sakura_build_command (int *, char ***);
static char *   sakura_get_term_cwd (struct sakura_tab *);
static char *   sakura_get_term_cwd_osc7 (struct sakura_tab *);
static void     sakura_update_tab_metadata (struct sakura_tab *, const gchar *);
static guint    sakura_tokeycode (guint key);
static void     sakura_set_keybind (const gchar *, guint);
static guint    sakura_get_keybind (const gchar *);
static void     sakura_sanitize_working_directory (void);

/* Functions */
static void     sakura_init ();
static void     sakura_init_popup ();
static void     sakura_add_tab ();
static void     sakura_spawn_codex (struct sakura_tab *, const gchar *, gchar **);
static void     sakura_add_tab_with_options (const gchar *, struct sakura_sidebar_node *,
                                             const gchar *, gboolean, SakuraTabKind,
                                             const gchar *, const gchar *, const gchar *);
static void     sakura_del_tab (gint);
static void     sakura_close_tab (gint); /* Save config, del tab and destroy sakura */
static void     sakura_destroy ();
static void     sakura_move_tab (gint);
static gint     sakura_find_tab (VteTerminal *);
static void     sakura_set_font ();
static void     sakura_set_tab_label_text (const gchar *, gint);
static void     sakura_set_window_title (const gchar *);
static gboolean sakura_prefers_dark_theme (void);
static void     sakura_set_dark_theme_environment (void);
static gchar *  sakura_get_default_font (void);
static gboolean sakura_load_gnome_terminal_colors (void);
static void     sakura_set_size (void);
static void     sakura_config_done ();
static void     sakura_set_colorset (int);
static void     sakura_set_colors (void);
static void     sakura_search_dialog (void);
static void     sakura_search (const char *, bool);
static void     sakura_copy (void);
static void     sakura_paste (void);
static void     sakura_paste_primary (void);
static void     sakura_show_scrollbar (void);
static void     sakura_new_window (void);


/* Globals for command line parameters */
static const char *option_font;
static const char *option_workdir;
static const char *option_execute;
static const char *option_title;
static gchar **option_xterm_args;
static gboolean option_xterm_execute=FALSE;
static gboolean option_version=FALSE;
static gint option_ntabs=1;
static gint option_login = FALSE;
static const char *option_icon;
static int option_rows, option_columns;
static gboolean option_hold=FALSE;
static char *option_config_file;
static gboolean option_fullscreen;
static gboolean option_maximize;
static gint option_colorset;
static gboolean option_new_session;
static gboolean option_new_window;
static gchar *option_codex_session;


static GOptionEntry entries[] = {
	{ "version", 'v', 0, G_OPTION_ARG_NONE, &option_version, N_("Print version number"), NULL },
	{ "title", 't', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &option_title, N_("Set window title"), NULL},
	{ "font", 'f', 0, G_OPTION_ARG_STRING, &option_font, N_("Select initial terminal font"), NULL },
	{ "ntabs", 'n', 0, G_OPTION_ARG_INT, &option_ntabs, N_("Select initial number of tabs"), NULL },
	{ "working-directory", 'd', 0, G_OPTION_ARG_STRING, &option_workdir, N_("Set working directory"), NULL },
	{ "execute", 'x', 0, G_OPTION_ARG_STRING, &option_execute, N_("Execute command"), NULL },
	{ "xterm-execute", 'e', 0, G_OPTION_ARG_NONE, &option_xterm_execute, N_("Execute command (last option in the command line)"), NULL },
	{ G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &option_xterm_args, NULL, NULL },
	{ "login", 'l', 0, G_OPTION_ARG_NONE, &option_login, N_("Login shell"), NULL },
	{ "icon", 'i', 0, G_OPTION_ARG_STRING, &option_icon, N_("Set window icon"), NULL },
	{ "columns", 'c', 0, G_OPTION_ARG_INT, &option_columns, N_("Set columns number"), NULL },
	{ "rows", 'r', 0, G_OPTION_ARG_INT, &option_rows, N_("Set rows number"), NULL },
	{ "hold", 'h', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE, &option_hold, N_("Hold window after execute command"), NULL },
	{ "maximize", 'm', 0, G_OPTION_ARG_NONE, &option_maximize, N_("Maximize window"), NULL },
	{ "fullscreen", 's', 0, G_OPTION_ARG_NONE, &option_fullscreen, N_("Fullscreen mode"), NULL },
	{ "config-file", 0, 0, G_OPTION_ARG_FILENAME, &option_config_file, N_("Use alternate configuration file"), NULL },
	{ "new-session", 0, 0, G_OPTION_ARG_NONE, &option_new_session, N_("Start a new workspace instead of restoring the previous one"), NULL },
	{ "new-window", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE, &option_new_window, NULL, NULL },
	{ "codex-session", 0, 0, G_OPTION_ARG_STRING, &option_codex_session, N_("Open a Codex session by ID or name"), "SESSION" },
	{ "colorset", 0, 0, G_OPTION_ARG_INT, &option_colorset, N_("Select initial colorset"), NULL },
	{ NULL }
};


/*************************/
/* Main window callbacks */
/*************************/

static gboolean
sakura_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	gint page, npages;
	guint topage = 0;

	if (event->type != GDK_KEY_PRESS) return FALSE;

	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	npages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));

	/* Use keycodes instead of keyvals. With keyvals, key bindings work only in US/ISO8859-1 and similar locales */
	guint keycode = event->hardware_keycode;

	/* Get the GDK accel mask to compare with our accelerators */
	GdkModifierType accel_mask = gtk_accelerator_get_default_mod_mask();

	/* Add/delete tab keybinding pressed */
	if ((event->state & accel_mask) == sakura.add_tab_accelerator && keycode == sakura_tokeycode(sakura.add_tab_key)) {
		sakura_add_tab();
		return TRUE;
	} else if ((event->state & accel_mask) == sakura.del_tab_accelerator && keycode == sakura_tokeycode(sakura.del_tab_key)) {
		/* Delete current tab */
		sakura_close_tab(page);
		return TRUE;
	}

	/* New window keybinding pressed */
	if ( (event->state & accel_mask) == sakura.new_window_accelerator &&
			keycode == sakura_tokeycode(sakura.new_window_key)) {
		sakura_new_window();
		return TRUE;
	}

	/* Switch tab keybinding pressed (numbers or next/prev) */
	if ((event->state & accel_mask) == sakura.switch_tab_accelerator) {
	/* If we use accel_mask, GDK_MOD4_MASK (windows key) it's not detected... */
        //if ((event->state & sakura.switch_tab_accelerator) == sakura.switch_tab_accelerator) {
		/* Just propagate the event if there is only one tab */
		if (npages >= 2) {
			if ((keycode >= sakura_tokeycode(GDK_KEY_1)) && (keycode <= sakura_tokeycode( GDK_KEY_9))) {

				/* User has explicitly disabled this branch, make sure to propagate the event */
				if (sakura.disable_numbered_tabswitch) return FALSE;

				if      (sakura_tokeycode(GDK_KEY_1) == keycode) topage = 0;
				else if (sakura_tokeycode(GDK_KEY_2) == keycode) topage = 1;
				else if (sakura_tokeycode(GDK_KEY_3) == keycode) topage = 2;
				else if (sakura_tokeycode(GDK_KEY_4) == keycode) topage = 3;
				else if (sakura_tokeycode(GDK_KEY_5) == keycode) topage = 4;
				else if (sakura_tokeycode(GDK_KEY_6) == keycode) topage = 5;
				else if (sakura_tokeycode(GDK_KEY_7) == keycode) topage = 6;
				else if (sakura_tokeycode(GDK_KEY_8) == keycode) topage = 7;
				else if (sakura_tokeycode(GDK_KEY_9) == keycode) topage = 8;
				if (topage <= npages)
					gtk_notebook_set_current_page(GTK_NOTEBOOK(sakura.notebook), topage);
				return TRUE;
			} else if (keycode == sakura_tokeycode(sakura.prev_tab_key)) {
				if (gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook))==0) {
					gtk_notebook_set_current_page(GTK_NOTEBOOK(sakura.notebook), npages-1);
				} else {
					gtk_notebook_prev_page(GTK_NOTEBOOK(sakura.notebook));
				}
				return TRUE;
			} else if (keycode == sakura_tokeycode(sakura.next_tab_key)) {
				if (gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook)) == (npages-1)) {
					gtk_notebook_set_current_page(GTK_NOTEBOOK(sakura.notebook), 0);
				} else {
					gtk_notebook_next_page(GTK_NOTEBOOK(sakura.notebook));
				}
				return TRUE;
			}
		}
	}

	/* Move tab keybinding pressed */
	if ((event->state & accel_mask) == sakura.move_tab_accelerator) {
		if (keycode == sakura_tokeycode(sakura.prev_tab_key)) {
			sakura_move_tab(BACKWARDS);
			return TRUE;
		} else if (keycode == sakura_tokeycode(sakura.next_tab_key)) {
			sakura_move_tab(FORWARD);
			return TRUE;
		}
	}

	/* Copy/paste keybinding pressed */
	if ((event->state & accel_mask) == sakura.copy_accelerator) {
		if (keycode == sakura_tokeycode(sakura.copy_key)) {
			sakura_copy();
			return TRUE;
		} else if (keycode == sakura_tokeycode(sakura.paste_key)) {
			sakura_paste();
			return TRUE;
		}
	}

	/* Show scrollbar keybinding pressed */
	if ((event->state & accel_mask) == sakura.scrollbar_accelerator) {
		if (keycode == sakura_tokeycode(sakura.scrollbar_key)) {
			sakura_show_scrollbar();
			return TRUE;
		}
	}

	/* Set tab name keybinding pressed */
	if ((event->state & accel_mask) == sakura.set_tab_name_accelerator) {
		if (keycode == sakura_tokeycode(sakura.set_tab_name_key)) {
			sakura_set_name_dialog_cb(NULL, NULL);
			return TRUE;
		}
	}

	/* Search keybinding pressed */
	if ((event->state & accel_mask) == sakura.search_accelerator) {
		if (keycode == sakura_tokeycode(sakura.search_key)) {
			sakura_search_dialog();
			return TRUE;
		}
	}

	/* Increase/decrease font size keybinding pressed */
	if ((event->state & accel_mask) == sakura.font_size_accelerator) {
		if (keycode == sakura_tokeycode(sakura.increase_font_size_key)) {
			sakura_increase_font_cb(NULL, NULL);
			return TRUE;
		} else if (keycode == sakura_tokeycode(sakura.decrease_font_size_key)) {
			sakura_decrease_font_cb(NULL, NULL);
			return TRUE;
		}
	}

	/* F11 (fullscreen) pressed */
	if (keycode == sakura_tokeycode(sakura.fullscreen_key)) {
		sakura_fullscreen_cb(NULL, NULL);
		return TRUE;
	}

	/* Change in colorset */
	if ((event->state & accel_mask) == sakura.set_colorset_accelerator) {
		int i;
		for (i=0; i<NUM_COLORSETS; i++) {
			if (keycode == sakura_tokeycode(sakura.set_colorset_keys[i])) {
				sakura_set_colorset(i);
				return TRUE;
			}
		}
	}
	return FALSE;
}


static gboolean
sakura_resized_window_cb (GtkWidget *widget, GdkEventConfigure *event, void *data)
{
	if (event->width != sakura.width || event->height != sakura.height) {
		//SAY("Configure event received. Current w %d h %d ConfigureEvent w %d h %d",
		//sakura.width, sakura.height, event->width, event->height);
		gtk_widget_hide(sakura.fade_window);
		sakura.resized = TRUE;
	}

	return FALSE;
}

/* Use focus-in-event to unmap the fade window */
static gboolean
sakura_focus_in_cb (GtkWidget *widget, GdkEvent *event, void *data)
{
	if (event->type != GDK_FOCUS_CHANGE) return FALSE;
	//if (!sakura.use_fading) return FALSE;

	/* Got the focus, hide the fade */
	//gtk_widget_hide(sakura.fade_window);

	/* Reset urgency hint */
	gtk_window_set_urgency_hint(GTK_WINDOW(sakura.main_window), FALSE);

	return FALSE;
}


/* Use focus-out-event to map the fade window */
static gboolean
sakura_focus_out_cb (GtkWidget *widget, GdkEvent *event, void *data)
{
	gint ax, ay, mx, my, x, y;

	if (event->type != GDK_FOCUS_CHANGE) return FALSE;
	if (!sakura.use_fading) return FALSE;

	/* No fade when the menu is displayed */
	if (gtk_widget_is_visible(sakura.menu)) return FALSE;

	/* Give the right size and position to the fade_window to cover all the main window */
	gtk_widget_translate_coordinates(sakura.notebook, sakura.main_window, 0, 0, &ax, &ay);
	gtk_window_get_position(GTK_WINDOW(sakura.main_window), &mx, &my);
	gint titlebar_height = ay-my;
	gtk_window_move(GTK_WINDOW(sakura.fade_window), mx, my+titlebar_height);
	//SAY("FADE ax %d ay %d x %d y %d titlebar_h %d", ax, ay, mx, my, titlebar_height);

	/* Same size as main window */
	gtk_window_get_size(GTK_WINDOW(sakura.main_window), &x, &y);
	gtk_window_resize(GTK_WINDOW(sakura.fade_window), x, y);

	//gtk_widget_show_all(sakura.fade_window);

	return FALSE;
}


static void
sakura_show_event_cb (GtkWidget *widget, gpointer data)
{
	/* Set size when the window is first shown */
	sakura_set_size();
}


/* Callback called when sakura configuration file is modified by an external process */
static void
sakura_conf_changed_cb (GtkWidget *widget, void *data)
{
	sakura.externally_modified = true;
}



/**********************/
/* Notebook callbacks */
/**********************/


/* Handler for notebook scroll-event - switches tabs by scroll direction */
static gboolean
sakura_notebook_scroll_cb (GtkWidget *widget, GdkEventScroll *event)
{
	/* This callback cause undesirable scroll (when the mouse is over the vte window) when using
	 * input methods like hime. Disable it by now */

	/*
	gint page, npages;

	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	npages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));

	switch (event->direction) {
		case GDK_SCROLL_DOWN:
			gtk_notebook_set_current_page(GTK_NOTEBOOK(sakura.notebook), --page >= 0 ? page : npages - 1);
			break;
		case GDK_SCROLL_UP:
			gtk_notebook_set_current_page(GTK_NOTEBOOK(sakura.notebook), ++page < npages ? page : 0);
			break;
		case GDK_SCROLL_LEFT:
		case GDK_SCROLL_RIGHT:
		case GDK_SCROLL_SMOOTH:
			break;
	}
	*/

	return FALSE;
}


/* Callback called when the user switches tabs or closes a tab (but not when a tab is added) */
static void
sakura_switch_page_cb (GtkWidget *widget, GtkWidget *widget_page, guint page_num, void *data)
{
	struct sakura_tab *sk_tab;

	/* Don't use gtk_notebook_get_current_page in the callbacks, it returns the previous page */

	sk_tab = sakura_get_sktab(sakura, page_num);
	sakura_sidebar_select_tab(sk_tab);
	sakura_codex_sync_name(sk_tab);

	/* Update the window title when a new tab is selected, but don't when an user title has been set */
	//if (!sakura.tab_default_title && !sakura.main_title)
	if (!sakura.main_title) {
		if (g_strcmp0(gtk_label_get_text(GTK_LABEL(sk_tab->label)),"")!=0) {
			sakura_set_window_title(gtk_label_get_text(GTK_LABEL(sk_tab->label)));
		}
	}

}


static void
sakura_page_removed_cb (GtkWidget *widget, void *data)
{
	if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook))==1) {
		/* If the first tab is disabled, window size changes and we need to recalculate its size */
		sakura_set_size();
	}
}


/* Callback for focus-in-event to the notebook widget */
static gboolean
sakura_notebook_focus_cb (GtkWindow *window, GdkEvent *event, void *data)
{
	struct sakura_tab *sk_tab; gint page;

	if (event->type != GDK_FOCUS_CHANGE) return FALSE;

	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	sk_tab = sakura_get_sktab(sakura, page);

	/* When clicking several times in the label, terminal loses its focus.
	 * So, when the notebook got the focus, make sure the terminal HAS te focus */
	gtk_widget_grab_focus(sk_tab->vte);

	return FALSE;
}


static gboolean
sakura_sidebar_get_iter (struct sakura_sidebar_node *node, GtkTreeIter *iter)
{
	GtkTreePath *path;
	gboolean valid;

	if (node == NULL || node->row == NULL || sakura.sidebar_model == NULL)
		return FALSE;

	path = gtk_tree_row_reference_get_path(node->row);
	if (path == NULL)
		return FALSE;

	valid = gtk_tree_model_get_iter(GTK_TREE_MODEL(sakura.sidebar_model), iter, path);
	gtk_tree_path_free(path);
	return valid;
}


static void
sakura_sidebar_free_node (struct sakura_sidebar_node *node)
{
	if (node == NULL)
		return;

	if (node->row != NULL)
		gtk_tree_row_reference_free(node->row);
	g_free(node->id);
	g_free(node->title);
	g_free(node->subtitle);
	g_free(node->tooltip);
	g_free(node);
}


static void
sakura_sidebar_set_node_row (struct sakura_sidebar_node *node, GtkTreeIter *iter)
{
	const gchar *icon_name;
	GtkIconTheme *icon_theme;
	gchar *escaped_title, *escaped_subtitle, *markup;

	icon_name = "utilities-terminal";
	if (node->type == SAKURA_SIDEBAR_GROUP) {
		icon_name = "folder";
	} else if (node->tab != NULL && node->tab->kind == SAKURA_TAB_CODEX) {
		/* Keep a stock-terminal fallback for uninstalled/source-tree runs. */
		icon_theme = gtk_icon_theme_get_default();
		if (icon_theme != NULL && gtk_icon_theme_has_icon(icon_theme, CODEX_ICON_NAME))
			icon_name = CODEX_ICON_NAME;
	}
	escaped_title = g_markup_escape_text(node->title != NULL ? node->title : "", -1);
	escaped_subtitle = g_markup_escape_text(node->subtitle != NULL ? node->subtitle : "", -1);
	if (node->subtitle != NULL && node->subtitle[0] != '\0')
		markup = g_strdup_printf("%s\n<small>%s</small>", escaped_title, escaped_subtitle);
	else
		markup = g_strdup(escaped_title);

	gtk_tree_store_set(sakura.sidebar_model, iter,
	                   SAKURA_SIDEBAR_COLUMN_TITLE, node->title,
	                   SAKURA_SIDEBAR_COLUMN_SUBTITLE, node->subtitle,
	                   SAKURA_SIDEBAR_COLUMN_MARKUP, markup,
	                   SAKURA_SIDEBAR_COLUMN_ICON, icon_name,
	                   SAKURA_SIDEBAR_COLUMN_TOOLTIP,
	                   node->tooltip != NULL ? node->tooltip : node->title,
	                   SAKURA_SIDEBAR_COLUMN_NODE, node,
	                   -1);

	g_free(escaped_title);
	g_free(escaped_subtitle);
	g_free(markup);
}


static void
sakura_register_codex_icon (void)
{
	GtkIconTheme *icon_theme;
	gchar *icon_path;

	icon_theme = gtk_icon_theme_get_default();
	if (icon_theme == NULL)
		return;

	/* The development tree mirrors the hicolor layout, so GTK can resolve
	 * the same icon name without requiring a package installation. */
	icon_path = g_build_filename(SAKURA_SOURCE_ICON_DIR, "hicolor", "scalable",
	                             "apps", CODEX_ICON_NAME ".svg", NULL);
	if (g_file_test(icon_path, G_FILE_TEST_IS_REGULAR))
		gtk_icon_theme_prepend_search_path(icon_theme, SAKURA_SOURCE_ICON_DIR);
	g_free(icon_path);
}


static void
sakura_sidebar_insert_node (struct sakura_sidebar_node *node)
{
	GtkTreeIter iter, parent_iter;
	GtkTreeIter *parent = NULL;
	GtkTreePath *path;

	if (node->parent != NULL && sakura_sidebar_get_iter(node->parent, &parent_iter))
		parent = &parent_iter;

	gtk_tree_store_append(sakura.sidebar_model, &iter, parent);
	sakura_sidebar_set_node_row(node, &iter);

	path = gtk_tree_model_get_path(GTK_TREE_MODEL(sakura.sidebar_model), &iter);
	node->row = gtk_tree_row_reference_new(GTK_TREE_MODEL(sakura.sidebar_model), path);
	gtk_tree_path_free(path);

	if (node->parent != NULL && sakura_sidebar_get_iter(node->parent, &parent_iter)) {
		path = gtk_tree_model_get_path(GTK_TREE_MODEL(sakura.sidebar_model), &parent_iter);
		gtk_tree_view_expand_row(GTK_TREE_VIEW(sakura.sidebar_tree), path, FALSE);
		gtk_tree_path_free(path);
	}
}


static struct sakura_sidebar_node *
sakura_sidebar_selected_node (void)
{
	GtkTreeIter iter;
	GtkTreeModel *model;
	struct sakura_sidebar_node *node = NULL;

	if (sakura.sidebar_selection == NULL ||
	    !gtk_tree_selection_get_selected(sakura.sidebar_selection, &model, &iter))
		return NULL;

	gtk_tree_model_get(model, &iter, SAKURA_SIDEBAR_COLUMN_NODE, &node, -1);
	return node;
}


static struct sakura_sidebar_node *
sakura_sidebar_selected_group (void)
{
	struct sakura_sidebar_node *node = sakura_sidebar_selected_node();

	if (node == NULL)
		return sakura.sidebar_root;
	if (node->type == SAKURA_SIDEBAR_GROUP)
		return node;
	if (node->parent != NULL)
		return node->parent;
	return sakura.sidebar_root;
}


static void
sakura_sidebar_add_terminal (struct sakura_tab *sk_tab, struct sakura_sidebar_node *parent)
{
	struct sakura_sidebar_node *node;

	node = g_new0(struct sakura_sidebar_node, 1);
	node->type = SAKURA_SIDEBAR_TERMINAL;
	node->title = g_strdup(_("Terminal"));
	node->subtitle = g_strdup("");
	node->parent = parent != NULL ? parent : sakura.sidebar_root;
	node->tab = sk_tab;
	sk_tab->sidebar_node = node;
	sakura_sidebar_insert_node(node);
	sakura_sidebar_update_tab(sk_tab);
	sakura_sidebar_select_tab(sk_tab);
}


static void
sakura_sidebar_update_tab (struct sakura_tab *sk_tab)
{
	GtkTreeIter iter;
	struct sakura_sidebar_node *node;
	gchar *title, *subtitle, *tooltip, *display_path;
	gint page;

	if (sk_tab == NULL || sk_tab->sidebar_node == NULL ||
	    !sakura_sidebar_get_iter(sk_tab->sidebar_node, &iter))
		return;

	node = sk_tab->sidebar_node;
	if (sk_tab->label_set_byuser) {
		title = g_strdup(gtk_label_get_text(GTK_LABEL(sk_tab->label)));
		g_strstrip(title);
	} else if (sk_tab->kind == SAKURA_TAB_CODEX) {
		title = sk_tab->codex_session_name != NULL &&
		         sk_tab->codex_session_name[0] != '\0'
		         ? g_strdup(sk_tab->codex_session_name)
		         : g_strdup(_("Codex"));
	} else if (sk_tab->cwd != NULL && sk_tab->cwd[0] != '\0') {
		if (g_strcmp0(sk_tab->cwd, g_get_home_dir()) == 0)
			title = g_strdup("~");
		else
			title = g_path_get_basename(sk_tab->cwd);
	} else {
		page = gtk_notebook_page_num(GTK_NOTEBOOK(sakura.notebook), sk_tab->hbox);
		title = g_strdup_printf(_("Terminal %d"), page >= 0 ? page + 1 : 1);
	}

	display_path = NULL;
	if (sk_tab->cwd != NULL && sk_tab->cwd[0] != '\0') {
		const gchar *home = g_get_home_dir();
		if (home != NULL && g_str_has_prefix(sk_tab->cwd, home) &&
		    (sk_tab->cwd[strlen(home)] == '\0' || sk_tab->cwd[strlen(home)] == '/'))
			display_path = g_strdup_printf("~%s", sk_tab->cwd + strlen(home));
		else
			display_path = g_strdup(sk_tab->cwd);
	}
	if (sk_tab->host != NULL && display_path != NULL)
		subtitle = g_strdup_printf("%s · %s", sk_tab->host, display_path);
	else if (sk_tab->host != NULL)
		subtitle = g_strdup(sk_tab->host);
	else if (display_path != NULL)
		subtitle = g_strdup(display_path);
	else
		subtitle = g_strdup("");

	if (sk_tab->raw_title != NULL && sk_tab->raw_title[0] != '\0' && subtitle[0] != '\0')
		tooltip = g_strdup_printf("%s\n%s", sk_tab->raw_title, subtitle);
	else if (sk_tab->raw_title != NULL && sk_tab->raw_title[0] != '\0')
		tooltip = g_strdup(sk_tab->raw_title);
	else
		tooltip = g_strdup(subtitle);

	g_free(node->title);
	g_free(node->subtitle);
	g_free(node->tooltip);
	node->title = title;
	node->subtitle = subtitle;
	node->tooltip = tooltip;
	sakura_sidebar_set_node_row(node, &iter);
	g_free(display_path);
	sakura_session_schedule_save();
}


static void
sakura_sidebar_select_tab (struct sakura_tab *sk_tab)
{
	GtkTreePath *path;

	if (sakura.sidebar_selection == NULL || sk_tab == NULL ||
	    sk_tab->sidebar_node == NULL || sk_tab->sidebar_node->row == NULL)
		return;

	path = gtk_tree_row_reference_get_path(sk_tab->sidebar_node->row);
	if (path == NULL)
		return;

	sakura.sidebar_syncing = TRUE;
	gtk_tree_selection_select_path(sakura.sidebar_selection, path);
	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(sakura.sidebar_tree), path, NULL, FALSE, 0, 0);
	sakura.sidebar_syncing = FALSE;
	gtk_tree_path_free(path);
}


static gboolean
sakura_focus_tab_cb (gpointer data)
{
	GtkWidget *vte = GTK_WIDGET(data);

	if (gtk_widget_get_visible(vte) && gtk_widget_get_realized(vte))
		gtk_widget_grab_focus(vte);
	g_object_unref(vte);
	return G_SOURCE_REMOVE;
}


static void
sakura_focus_tab (struct sakura_tab *sk_tab)
{
	if (sk_tab == NULL || sk_tab->vte == NULL)
		return;

	/* Let the tree view finish handling the click before moving focus to the
	 * selected terminal. Keep the widget alive until the idle callback runs. */
	g_idle_add(sakura_focus_tab_cb, g_object_ref(sk_tab->vte));
}


static void
sakura_sidebar_remove_tab (struct sakura_tab *sk_tab)
{
	GtkTreeIter iter;

	if (sk_tab == NULL || sk_tab->sidebar_node == NULL)
		return;

	if (sakura_sidebar_get_iter(sk_tab->sidebar_node, &iter))
		gtk_tree_store_remove(sakura.sidebar_model, &iter);
	sakura_sidebar_free_node(sk_tab->sidebar_node);
	sk_tab->sidebar_node = NULL;
}


static void
sakura_sidebar_selection_changed_cb (GtkTreeSelection *selection, void *data)
{
	struct sakura_sidebar_node *node;
	gint page;

	if (sakura.sidebar_syncing)
		return;

	node = sakura_sidebar_selected_node();
	if (node == NULL || node->type != SAKURA_SIDEBAR_TERMINAL || node->tab == NULL)
		return;

	page = gtk_notebook_page_num(GTK_NOTEBOOK(sakura.notebook), node->tab->hbox);
	if (page >= 0 && page != gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook))) {
		sakura.sidebar_syncing = TRUE;
		gtk_notebook_set_current_page(GTK_NOTEBOOK(sakura.notebook), page);
		sakura.sidebar_syncing = FALSE;
	}
	sakura_focus_tab(node->tab);
	sakura_session_schedule_save();
}


static gboolean
sakura_sidebar_button_press_cb (GtkWidget *widget, GdkEventButton *event, void *data)
{
	GtkTreePath *path = NULL;
	GtkTreeViewColumn *column = NULL;
	GtkWidget *menu, *item;
	struct sakura_sidebar_node *node;

	if (event->button != GDK_BUTTON_SECONDARY)
		return FALSE;

	if (GTK_IS_TREE_VIEW(widget) &&
	    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), event->x, event->y,
	                                  &path, &column, NULL, NULL)) {
		gtk_tree_selection_select_path(sakura.sidebar_selection, path);
		node = sakura_sidebar_selected_node();
	} else {
		gtk_tree_selection_unselect_all(sakura.sidebar_selection);
		node = NULL;
	}

	menu = gtk_menu_new();

	item = gtk_menu_item_new_with_label(_("New terminal"));
	g_signal_connect(item, "activate", G_CALLBACK(sakura_new_tab_cb), NULL);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	item = gtk_menu_item_new_with_label(_("New Codex session"));
	g_signal_connect(item, "activate", G_CALLBACK(sakura_new_codex_cb), NULL);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	item = gtk_menu_item_new_with_label(_("Resume Codex session..."));
	g_signal_connect(item, "activate", G_CALLBACK(sakura_resume_codex_cb), NULL);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	item = gtk_menu_item_new_with_label(_("New terminal group"));
	g_signal_connect(item, "activate", G_CALLBACK(sakura_sidebar_new_group_cb), NULL);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	if (node != NULL && node->type == SAKURA_SIDEBAR_TERMINAL) {
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
		item = gtk_menu_item_new_with_label(_("Set terminal name..."));
		g_signal_connect(item, "activate", G_CALLBACK(sakura_set_name_dialog_cb), NULL);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		item = gtk_menu_item_new_with_label(_("Close terminal"));
		g_signal_connect(item, "activate", G_CALLBACK(sakura_close_tab_cb), NULL);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	} else if (node != NULL && node != sakura.sidebar_root) {
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
		item = gtk_menu_item_new_with_label(_("Rename group..."));
		g_signal_connect(item, "activate", G_CALLBACK(sakura_sidebar_rename_group_cb), node);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		item = gtk_menu_item_new_with_label(_("Delete empty group"));
		g_signal_connect(item, "activate", G_CALLBACK(sakura_sidebar_delete_group_cb), node);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	}

	gtk_widget_show_all(menu);
	gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
	if (path != NULL)
		gtk_tree_path_free(path);

	return TRUE;
}


static void
sakura_sidebar_new_group_cb (GtkWidget *widget, void *data)
{
	GtkWidget *dialog, *entry;
	GtkTreeIter iter;
	struct sakura_sidebar_node *parent, *node;
	const gchar *title;

	parent = sakura_sidebar_selected_group();
	dialog = gtk_dialog_new_with_buttons(_("New terminal group"),
	                                     GTK_WINDOW(sakura.main_window),
	                                     GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR,
	                                     _("_Cancel"), GTK_RESPONSE_CANCEL,
	                                     _("_Create"), GTK_RESPONSE_ACCEPT,
	                                     NULL);
	entry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(entry), _("Group name"));
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
	                   entry, FALSE, FALSE, 12);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
	gtk_widget_show_all(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		title = gtk_entry_get_text(GTK_ENTRY(entry));
		if (title[0] != '\0') {
			node = g_new0(struct sakura_sidebar_node, 1);
			node->type = SAKURA_SIDEBAR_GROUP;
			node->id = g_strdup_printf("group-%u", sakura.sidebar_next_group_id++);
			node->title = g_strdup(title);
			node->parent = parent != NULL ? parent : sakura.sidebar_root;
			sakura.sidebar_groups = g_list_append(sakura.sidebar_groups, node);
			sakura_sidebar_insert_node(node);
			if (sakura_sidebar_get_iter(node, &iter)) {
				GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(sakura.sidebar_model), &iter);
				gtk_tree_view_expand_row(GTK_TREE_VIEW(sakura.sidebar_tree), path, FALSE);
				gtk_tree_selection_select_path(sakura.sidebar_selection, path);
				gtk_tree_path_free(path);
			}
			sakura_sidebar_save_groups();
		}
	}
	gtk_widget_destroy(dialog);
}


static void
sakura_sidebar_rename_group_cb (GtkWidget *widget, void *data)
{
	struct sakura_sidebar_node *node = data;
	GtkWidget *dialog, *entry;
	GtkTreeIter iter;
	const gchar *title;

	if (node == NULL || node->type != SAKURA_SIDEBAR_GROUP)
		return;

	dialog = gtk_dialog_new_with_buttons(_("Rename terminal group"),
	                                     GTK_WINDOW(sakura.main_window),
	                                     GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR,
	                                     _("_Cancel"), GTK_RESPONSE_CANCEL,
	                                     _("_Apply"), GTK_RESPONSE_ACCEPT,
	                                     NULL);
	entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), node->title);
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
	                   entry, FALSE, FALSE, 12);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
	gtk_widget_show_all(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		title = gtk_entry_get_text(GTK_ENTRY(entry));
		if (title[0] != '\0') {
			g_free(node->title);
			node->title = g_strdup(title);
			if (sakura_sidebar_get_iter(node, &iter))
				sakura_sidebar_set_node_row(node, &iter);
			sakura_sidebar_save_groups();
		}
	}
	gtk_widget_destroy(dialog);
}


static void
sakura_sidebar_delete_group_cb (GtkWidget *widget, void *data)
{
	struct sakura_sidebar_node *node = data;
	GtkTreeIter iter;

	if (node == NULL || node == sakura.sidebar_root ||
	    node->type != SAKURA_SIDEBAR_GROUP ||
	    !sakura_sidebar_get_iter(node, &iter))
		return;

	if (gtk_tree_model_iter_has_child(GTK_TREE_MODEL(sakura.sidebar_model), &iter)) {
		GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(sakura.main_window),
		                                           GTK_DIALOG_MODAL,
		                                           GTK_MESSAGE_INFO,
		                                           GTK_BUTTONS_OK,
		                                           _("Only empty terminal groups can be deleted."));
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
		return;
	}

	gtk_tree_store_remove(sakura.sidebar_model, &iter);
	sakura.sidebar_groups = g_list_remove(sakura.sidebar_groups, node);
	sakura_sidebar_free_node(node);
	sakura_sidebar_save_groups();
}


static struct sakura_sidebar_node *
sakura_sidebar_find_group_by_id (const gchar *id)
{
	GList *group;

	if (id == NULL)
		return NULL;
	for (group = sakura.sidebar_groups; group != NULL; group = group->next) {
		struct sakura_sidebar_node *node = group->data;
		if (g_strcmp0(node->id, id) == 0)
			return node;
	}
	return NULL;
}


static void
sakura_sidebar_collect_groups (GtkTreeModel *model, GtkTreeIter *parent,
                               GPtrArray *ids, GPtrArray *parents, GPtrArray *titles)
{
	GtkTreeIter iter;
	gboolean valid;

	valid = parent == NULL
		? gtk_tree_model_get_iter_first(model, &iter)
		: gtk_tree_model_iter_children(model, &iter, parent);
	while (valid) {
		struct sakura_sidebar_node *node = NULL;
		gtk_tree_model_get(model, &iter, SAKURA_SIDEBAR_COLUMN_NODE, &node, -1);
		if (node != NULL && node->type == SAKURA_SIDEBAR_GROUP) {
			if (node != sakura.sidebar_root) {
				g_ptr_array_add(ids, g_strdup(node->id));
				g_ptr_array_add(parents, g_strdup(node->parent != NULL ? node->parent->id : "root"));
				g_ptr_array_add(titles, g_strdup(node->title));
			}
			sakura_sidebar_collect_groups(model, &iter, ids, parents, titles);
		}
		valid = gtk_tree_model_iter_next(model, &iter);
	}
}


static void
sakura_sidebar_save_groups (void)
{
	GPtrArray *ids, *parents, *titles;

	ids = g_ptr_array_new_with_free_func(g_free);
	parents = g_ptr_array_new_with_free_func(g_free);
	titles = g_ptr_array_new_with_free_func(g_free);
	sakura_sidebar_collect_groups(GTK_TREE_MODEL(sakura.sidebar_model), NULL,
	                              ids, parents, titles);

	if (ids->len == 0) {
		g_key_file_remove_key(sakura.cfg, cfg_group, "sidebar_group_ids", NULL);
		g_key_file_remove_key(sakura.cfg, cfg_group, "sidebar_group_parents", NULL);
		g_key_file_remove_key(sakura.cfg, cfg_group, "sidebar_group_titles", NULL);
	} else {
		g_key_file_set_string_list(sakura.cfg, cfg_group, "sidebar_group_ids",
		                           (const gchar * const *)ids->pdata, ids->len);
		g_key_file_set_string_list(sakura.cfg, cfg_group, "sidebar_group_parents",
		                           (const gchar * const *)parents->pdata, parents->len);
		g_key_file_set_string_list(sakura.cfg, cfg_group, "sidebar_group_titles",
		                           (const gchar * const *)titles->pdata, titles->len);
	}
	sakura.config_modified = TRUE;
	sakura_session_schedule_save();
	g_ptr_array_free(ids, TRUE);
	g_ptr_array_free(parents, TRUE);
	g_ptr_array_free(titles, TRUE);
}


static void
sakura_sidebar_collect_terminals (GtkTreeModel *model, GtkTreeIter *parent,
                                  GPtrArray *terminals)
{
	GtkTreeIter iter;
	gboolean valid;

	valid = parent == NULL
		? gtk_tree_model_get_iter_first(model, &iter)
		: gtk_tree_model_iter_children(model, &iter, parent);
	while (valid) {
		struct sakura_sidebar_node *node = NULL;
		gtk_tree_model_get(model, &iter, SAKURA_SIDEBAR_COLUMN_NODE, &node, -1);
		if (node != NULL && node->type == SAKURA_SIDEBAR_TERMINAL)
			g_ptr_array_add(terminals, node);
		else if (node != NULL && node->type == SAKURA_SIDEBAR_GROUP)
			sakura_sidebar_collect_terminals(model, &iter, terminals);
		valid = gtk_tree_model_iter_next(model, &iter);
	}
}


static gboolean
sakura_session_save_timeout_cb (gpointer data)
{
	sakura.session_save_source_id = 0;
	sakura_session_save();
	return G_SOURCE_REMOVE;
}


static gboolean
sakura_terminal_id_is_valid (const gchar *terminal_id)
{
	const gchar *cursor;

	if (terminal_id == NULL || terminal_id[0] == '\0' || strlen(terminal_id) > 128)
		return FALSE;
	for (cursor = terminal_id; *cursor != '\0'; cursor++) {
		if (!g_ascii_isalnum(*cursor) && *cursor != '-' && *cursor != '_' && *cursor != '.')
			return FALSE;
	}
	return TRUE;
}


static gchar *
sakura_generate_terminal_id (void)
{
	return g_strdup_printf("terminal-%d-%u", (int)getpid(), g_random_int());
}


static gchar *
sakura_history_file_for_tab (const struct sakura_tab *sk_tab)
{
	if (sakura.history_dir == NULL || sk_tab == NULL || sk_tab->terminal_id == NULL)
		return NULL;
	return g_build_filename(sakura.history_dir, sk_tab->terminal_id, NULL);
}


static void
sakura_prepare_history_file (struct sakura_tab *sk_tab)
{
	gchar *history_file;
	int fd;

	history_file = sakura_history_file_for_tab(sk_tab);
	if (history_file == NULL)
		return;

	fd = g_open(history_file, O_WRONLY | O_CREAT | O_APPEND, 0600);
	if (fd == -1) {
		SAY("Could not create terminal history file %s: %s", history_file, g_strerror(errno));
	} else {
		close(fd);
		if (chmod(history_file, 0600) != 0)
			SAY("Could not secure terminal history file %s: %s", history_file, g_strerror(errno));
	}
	g_free(history_file);
}


static void
sakura_remove_history_file (struct sakura_tab *sk_tab)
{
	gchar *history_file;

	if (sakura.session_shutting_down)
		return;
	history_file = sakura_history_file_for_tab(sk_tab);
	if (history_file != NULL && g_remove(history_file) != 0 && errno != ENOENT)
		SAY("Could not remove terminal history file %s: %s", history_file, g_strerror(errno));
	g_free(history_file);
}


static gboolean
sakura_session_version_supported (void)
{
	gint version;

	if (sakura.session_cfg == NULL ||
	    !g_key_file_has_key(sakura.session_cfg, "Session", "version", NULL))
		return FALSE;

	version = g_key_file_get_integer(sakura.session_cfg, "Session", "version", NULL);
	return version == 1 || version == 2 || version == 3;
}


static void
sakura_session_schedule_save (void)
{
	if (sakura.sessionfile == NULL || option_new_window ||
	    !sakura.session_ready || sakura.session_restoring || sakura.dont_save)
		return;

	if (sakura.session_save_source_id == 0)
		sakura.session_save_source_id = g_timeout_add(500, sakura_session_save_timeout_cb, NULL);
}


static void
sakura_session_save (void)
{
	GKeyFile *session;
	GPtrArray *group_ids, *group_parents, *group_titles, *terminals;
	GError *error = NULL;
	gchar *data, *temporary_file;
	gsize data_length;
	gint selected_page, selected_terminal = -1;
	guint i;

	if (sakura.sessionfile == NULL || option_new_window || sakura.dont_save || sakura.session_shutting_down ||
	    sakura.sidebar_model == NULL)
		return;

	if (sakura.session_save_source_id != 0) {
		g_source_remove(sakura.session_save_source_id);
		sakura.session_save_source_id = 0;
	}
	session = g_key_file_new();
	group_ids = g_ptr_array_new_with_free_func(g_free);
	group_parents = g_ptr_array_new_with_free_func(g_free);
	group_titles = g_ptr_array_new_with_free_func(g_free);
	terminals = g_ptr_array_new();
	sakura_sidebar_collect_groups(GTK_TREE_MODEL(sakura.sidebar_model), NULL,
	                              group_ids, group_parents, group_titles);
	sakura_sidebar_collect_terminals(GTK_TREE_MODEL(sakura.sidebar_model), NULL, terminals);

	selected_page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	if (selected_page >= 0) {
		struct sakura_tab *selected_tab = sakura_get_sktab(sakura, selected_page);
		for (i = 0; i < terminals->len; i++) {
			struct sakura_sidebar_node *node = g_ptr_array_index(terminals, i);
			if (node->tab == selected_tab) {
				selected_terminal = (gint)i;
				break;
			}
		}
	}

	g_key_file_set_integer(session, "Session", "version", 3);
	g_key_file_set_integer(session, "Session", "group_count", group_ids->len);
	g_key_file_set_integer(session, "Session", "terminal_count", terminals->len);
	g_key_file_set_integer(session, "Session", "selected_terminal", selected_terminal);
	g_key_file_set_boolean(session, "Session", "sidebar_visible", sakura.sidebar_visible);
	g_key_file_set_integer(session, "Session", "sidebar_width",
	                       sakura.sidebar_paned != NULL
	                       ? gtk_paned_get_position(GTK_PANED(sakura.sidebar_paned))
	                       : sakura.sidebar_width);

	for (i = 0; i < group_ids->len; i++) {
		gchar *section = g_strdup_printf("Group%u", i);
		g_key_file_set_string(session, section, "id", g_ptr_array_index(group_ids, i));
		g_key_file_set_string(session, section, "parent", g_ptr_array_index(group_parents, i));
		g_key_file_set_string(session, section, "title", g_ptr_array_index(group_titles, i));
		g_free(section);
	}

	for (i = 0; i < terminals->len; i++) {
		struct sakura_sidebar_node *node = g_ptr_array_index(terminals, i);
		struct sakura_tab *tab = node->tab;
		gchar *section = g_strdup_printf("Terminal%u", i);
		const gchar *title = gtk_label_get_text(GTK_LABEL(tab->label));

		g_key_file_set_string(session, section, "parent",
		                      node->parent != NULL ? node->parent->id : "root");
		g_key_file_set_string(session, section, "cwd", tab->cwd != NULL ? tab->cwd : "");
		g_key_file_set_string(session, section, "terminal_id",
		                      tab->terminal_id != NULL ? tab->terminal_id : "");
		g_key_file_set_string(session, section, "kind",
		                      tab->kind == SAKURA_TAB_CODEX ? "codex" : "shell");
		if (tab->kind == SAKURA_TAB_CODEX && tab->codex_session_id != NULL &&
		    tab->codex_session_id[0] != '\0')
			g_key_file_set_string(session, section, "codex_session_id", tab->codex_session_id);
		if (tab->kind == SAKURA_TAB_CODEX && tab->codex_session_name != NULL &&
		    tab->codex_session_name[0] != '\0')
			g_key_file_set_string(session, section, "codex_session_name", tab->codex_session_name);
		g_key_file_set_boolean(session, section, "title_set_by_user", tab->label_set_byuser);
		if (tab->label_set_byuser)
			g_key_file_set_string(session, section, "title", title != NULL ? title : "");
		g_free(section);
	}

	data = g_key_file_to_data(session, &data_length, &error);
	if (data == NULL) {
		SAY("Could not serialize session: %s", error->message);
		g_error_free(error);
	} else {
		temporary_file = g_strdup_printf("%s.tmp.%d", sakura.sessionfile, (int)getpid());
		if (!g_file_set_contents(temporary_file, data, data_length, &error) ||
		    chmod(temporary_file, 0600) != 0 ||
		    g_rename(temporary_file, sakura.sessionfile) != 0) {
			SAY("Could not save session: %s", error != NULL ? error->message : g_strerror(errno));
			if (error != NULL)
				g_error_free(error);
			g_remove(temporary_file);
		}
		g_free(temporary_file);
		g_free(data);
	}

	g_key_file_free(session);
	g_ptr_array_free(group_ids, TRUE);
	g_ptr_array_free(group_parents, TRUE);
	g_ptr_array_free(group_titles, TRUE);
	g_ptr_array_free(terminals, TRUE);
}


static void
sakura_session_load (void)
{
	GError *error = NULL;

	if (sakura.sessionfile == NULL)
		return;

	sakura.session_cfg = g_key_file_new();
	if (!g_key_file_load_from_file(sakura.session_cfg, sakura.sessionfile, 0, &error)) {
		g_key_file_free(sakura.session_cfg);
		sakura.session_cfg = NULL;
		if (error != NULL)
			g_error_free(error);
	} else if (!option_new_session && !option_new_window &&
	           sakura_session_version_supported()) {
		if (g_key_file_has_key(sakura.session_cfg, "Session", "sidebar_visible", NULL))
			sakura.sidebar_visible = g_key_file_get_boolean(sakura.session_cfg, "Session", "sidebar_visible", NULL);
		if (g_key_file_has_key(sakura.session_cfg, "Session", "sidebar_width", NULL)) {
			gint sidebar_width = g_key_file_get_integer(sakura.session_cfg, "Session", "sidebar_width", NULL);
			if (sidebar_width >= 160 && sidebar_width <= 500)
				sakura.sidebar_width = sidebar_width;
		}
	}
}


static gboolean
sakura_session_restore (void)
{
	gint terminal_count, selected_terminal, restored = 0;
	guint i;

	if (sakura.session_cfg == NULL ||
	    !sakura_session_version_supported() ||
	    !g_key_file_has_key(sakura.session_cfg, "Session", "terminal_count", NULL))
		return FALSE;

	terminal_count = g_key_file_get_integer(sakura.session_cfg, "Session", "terminal_count", NULL);
	if (terminal_count <= 0)
		return FALSE;

	selected_terminal = g_key_file_get_integer(sakura.session_cfg, "Session", "selected_terminal", NULL);
	for (i = 0; i < (guint)terminal_count; i++) {
		gchar *section = g_strdup_printf("Terminal%u", i);
		gchar *parent_id = g_key_file_get_string(sakura.session_cfg, section, "parent", NULL);
		gchar *cwd = g_key_file_get_string(sakura.session_cfg, section, "cwd", NULL);
		gchar *title = g_key_file_get_string(sakura.session_cfg, section, "title", NULL);
		gchar *terminal_id = g_key_file_get_string(sakura.session_cfg, section, "terminal_id", NULL);
		gchar *kind = g_key_file_get_string(sakura.session_cfg, section, "kind", NULL);
		gchar *codex_session_id = g_key_file_get_string(sakura.session_cfg, section,
		                                                  "codex_session_id", NULL);
		gchar *codex_session_name = g_key_file_get_string(sakura.session_cfg, section,
		                                                    "codex_session_name", NULL);
		gboolean title_set = g_key_file_get_boolean(sakura.session_cfg, section,
		                                             "title_set_by_user", NULL);
		struct sakura_sidebar_node *parent = sakura_sidebar_find_group_by_id(parent_id);
		SakuraTabKind tab_kind = g_strcmp0(kind, "codex") == 0 &&
		                         codex_session_id != NULL && codex_session_id[0] != '\0'
		                         ? SAKURA_TAB_CODEX : SAKURA_TAB_SHELL;
		title_set = title_set && title != NULL && title[0] != '\0';

		if (cwd != NULL && (cwd[0] == '\0' || !g_file_test(cwd, G_FILE_TEST_IS_DIR))) {
			g_free(cwd);
			cwd = NULL;
		}
		sakura_add_tab_with_options(cwd, parent, title_set ? title : NULL, title_set,
		                            tab_kind, tab_kind == SAKURA_TAB_CODEX ? codex_session_id : NULL,
		                            tab_kind == SAKURA_TAB_CODEX ? codex_session_name : NULL,
		                            sakura_terminal_id_is_valid(terminal_id) ? terminal_id : NULL);
		if (selected_terminal == (gint)i)
			selected_terminal = restored;
		restored++;

		g_free(section);
		g_free(parent_id);
		g_free(cwd);
		g_free(title);
		g_free(terminal_id);
		g_free(kind);
		g_free(codex_session_id);
		g_free(codex_session_name);
	}

	if (restored > 0 && selected_terminal >= 0 && selected_terminal < restored)
		gtk_notebook_set_current_page(GTK_NOTEBOOK(sakura.notebook), selected_terminal);
	return restored > 0;
}


static gboolean
sakura_codex_tracking_poll_cb (gpointer data)
{
	GDir *dir;
	const gchar *filename;
	GError *error = NULL;
	gboolean changed = FALSE;

	if (sakura.codex_tracking_dir == NULL)
		return G_SOURCE_CONTINUE;

	dir = g_dir_open(sakura.codex_tracking_dir, 0, NULL);
	if (dir == NULL)
		return G_SOURCE_CONTINUE;

	while ((filename = g_dir_read_name(dir)) != NULL) {
		gchar *path, *contents;
		gsize length;
		gint page, pages;
		struct sakura_tab *matched_tab = NULL;

		if (filename[0] == '.')
			continue;

		path = g_build_filename(sakura.codex_tracking_dir, filename, NULL);
		contents = NULL;
		if (!g_file_get_contents(path, &contents, &length, &error)) {
			if (error != NULL)
				g_clear_error(&error);
			g_free(path);
			continue;
		}
		g_strstrip(contents);

		if (contents[0] == '\0') {
			g_free(contents);
			g_free(path);
			continue;
		}

		pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));
		for (page = 0; page < pages; page++) {
			struct sakura_tab *sk_tab = sakura_get_sktab(sakura, page);

			if (sk_tab->codex_tracking_token == NULL ||
			    g_strcmp0(sk_tab->codex_tracking_token, filename) != 0)
				continue;

			if (sk_tab->kind != SAKURA_TAB_CODEX) {
				sk_tab->kind = SAKURA_TAB_CODEX;
				sakura_sidebar_update_tab(sk_tab);
				changed = TRUE;
			}
			if (g_strcmp0(sk_tab->codex_session_id, contents) != 0) {
				g_free(sk_tab->codex_session_id);
				sk_tab->codex_session_id = g_strdup(contents);
				changed = TRUE;
			}
			matched_tab = sk_tab;
			g_remove(path);
			break;
		}
		if (matched_tab != NULL)
			sakura_codex_sync_name(matched_tab);

		g_free(contents);
		g_free(path);
	}
	g_dir_close(dir);

	if (changed)
		sakura_session_schedule_save();

	return G_SOURCE_CONTINUE;
}


static void
sakura_sidebar_model_reordered_cb (GtkTreeModel *model, GtkTreePath *path,
                                   GtkTreeIter *iter, gint *new_order, void *data)
{
	sakura_sidebar_save_groups();
	sakura_session_schedule_save();
}


static void
sakura_sidebar_toggle_cb (GtkWidget *widget, void *data)
{
	sakura.sidebar_visible = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget));
	if (sakura.sidebar != NULL)
		gtk_widget_set_visible(sakura.sidebar, sakura.sidebar_visible);
	sakura_set_config_boolean("sidebar_visible", sakura.sidebar_visible);
	sakura_session_schedule_save();
}


static void
sakura_sidebar_paned_position_cb (GObject *object, GParamSpec *pspec, void *data)
{
	if (sakura.sidebar_paned != NULL && sakura.sidebar_visible) {
		sakura.sidebar_width = gtk_paned_get_position(GTK_PANED(sakura.sidebar_paned));
		sakura_set_config_integer("sidebar_width", sakura.sidebar_width);
	}
	sakura_session_schedule_save();
}


static void
sakura_sidebar_init (void)
{
	GtkWidget *sidebar_box, *toolbar, *title, *new_terminal, *new_group;
	GtkWidget *scrolled;
	GtkCellRenderer *icon_renderer, *text_renderer;
	GtkTreeViewColumn *column;
	gchar **group_ids, **group_parents, **group_titles;
	gsize n_ids = 0, n_parents = 0, n_titles = 0;
	gsize i, n_groups;
	gboolean session_has_groups;

	sakura.sidebar_model = gtk_tree_store_new(SAKURA_SIDEBAR_N_COLUMNS,
	                                         G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
	                                         G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER);
	sakura.sidebar_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(sakura.sidebar_model));
	sakura.sidebar_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(sakura.sidebar_tree));
	gtk_tree_selection_set_mode(sakura.sidebar_selection, GTK_SELECTION_SINGLE);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(sakura.sidebar_tree), FALSE);
	gtk_tree_view_set_enable_tree_lines(GTK_TREE_VIEW(sakura.sidebar_tree), TRUE);
	gtk_tree_view_set_activate_on_single_click(GTK_TREE_VIEW(sakura.sidebar_tree), TRUE);
	gtk_tree_view_set_reorderable(GTK_TREE_VIEW(sakura.sidebar_tree), TRUE);
	gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(sakura.sidebar_tree), SAKURA_SIDEBAR_COLUMN_TOOLTIP);
	gtk_widget_set_name(sakura.sidebar_tree, "terminal-sidebar");

	icon_renderer = gtk_cell_renderer_pixbuf_new();
	text_renderer = gtk_cell_renderer_text_new();
	g_object_set(text_renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_spacing(column, 6);
	gtk_tree_view_column_pack_start(column, icon_renderer, FALSE);
	gtk_tree_view_column_add_attribute(column, icon_renderer, "icon-name", SAKURA_SIDEBAR_COLUMN_ICON);
	gtk_tree_view_column_pack_start(column, text_renderer, TRUE);
	gtk_tree_view_column_add_attribute(column, text_renderer, "markup", SAKURA_SIDEBAR_COLUMN_MARKUP);
	gtk_tree_view_append_column(GTK_TREE_VIEW(sakura.sidebar_tree), column);

	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scrolled), sakura.sidebar_tree);

	toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	title = gtk_label_new(_("Terminals"));
	gtk_label_set_xalign(GTK_LABEL(title), 0.0);
	gtk_widget_set_margin_start(title, 6);
	gtk_widget_set_margin_end(title, 6);
	gtk_box_pack_start(GTK_BOX(toolbar), title, TRUE, TRUE, 0);
	new_terminal = gtk_button_new_from_icon_name("utilities-terminal", GTK_ICON_SIZE_MENU);
	gtk_button_set_relief(GTK_BUTTON(new_terminal), GTK_RELIEF_NONE);
	gtk_widget_set_tooltip_text(new_terminal, _("New terminal"));
	gtk_box_pack_start(GTK_BOX(toolbar), new_terminal, FALSE, FALSE, 0);
	new_group = gtk_button_new_from_icon_name("folder-new", GTK_ICON_SIZE_MENU);
	gtk_button_set_relief(GTK_BUTTON(new_group), GTK_RELIEF_NONE);
	gtk_widget_set_tooltip_text(new_group, _("New terminal group"));
	gtk_box_pack_start(GTK_BOX(toolbar), new_group, FALSE, FALSE, 0);

	sidebar_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(sidebar_box), toolbar, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(sidebar_box), scrolled, TRUE, TRUE, 0);
	sakura.sidebar = sidebar_box;

	sakura.sidebar_root = g_new0(struct sakura_sidebar_node, 1);
	sakura.sidebar_root->type = SAKURA_SIDEBAR_GROUP;
	sakura.sidebar_root->id = g_strdup("root");
	sakura.sidebar_root->title = g_strdup(_("All terminals"));
	sakura.sidebar_next_group_id = 1;
	sakura.sidebar_groups = g_list_append(sakura.sidebar_groups, sakura.sidebar_root);
	sakura_sidebar_insert_node(sakura.sidebar_root);

	session_has_groups = !option_new_session && !option_new_window &&
		sakura.session_cfg != NULL &&
		sakura_session_version_supported() &&
		g_key_file_has_key(sakura.session_cfg, "Session", "group_count", NULL);
	if (session_has_groups) {
		n_groups = g_key_file_get_integer(sakura.session_cfg, "Session", "group_count", NULL);
		group_ids = g_new0(gchar *, n_groups + 1);
		group_parents = g_new0(gchar *, n_groups + 1);
		group_titles = g_new0(gchar *, n_groups + 1);
		for (i = 0; i < n_groups; i++) {
			gchar *section = g_strdup_printf("Group%u", (guint)i);
			group_ids[i] = g_key_file_get_string(sakura.session_cfg, section, "id", NULL);
			group_parents[i] = g_key_file_get_string(sakura.session_cfg, section, "parent", NULL);
			group_titles[i] = g_key_file_get_string(sakura.session_cfg, section, "title", NULL);
			g_free(section);
		}
	} else {
		group_ids = g_key_file_get_string_list(sakura.cfg, cfg_group, "sidebar_group_ids", &n_ids, NULL);
		group_parents = g_key_file_get_string_list(sakura.cfg, cfg_group, "sidebar_group_parents", &n_parents, NULL);
		group_titles = g_key_file_get_string_list(sakura.cfg, cfg_group, "sidebar_group_titles", &n_titles, NULL);
		n_groups = MIN(n_ids, MIN(n_parents, n_titles));
	}
	for (i = 0; i < n_groups; i++) {
		struct sakura_sidebar_node *node, *parent;
		gchar *end = NULL;
		guint id_number;

		parent = sakura_sidebar_find_group_by_id(group_parents[i]);
		if (parent == NULL)
			parent = sakura.sidebar_root;
		node = g_new0(struct sakura_sidebar_node, 1);
		node->type = SAKURA_SIDEBAR_GROUP;
		node->id = g_strdup(group_ids[i]);
		node->title = g_strdup(group_titles[i]);
		node->parent = parent;
		sakura.sidebar_groups = g_list_append(sakura.sidebar_groups, node);
		sakura_sidebar_insert_node(node);

		if (g_str_has_prefix(node->id, "group-")) {
			id_number = (guint)g_ascii_strtoull(node->id + strlen("group-"), &end, 10);
			if (end != node->id + strlen("group-") && id_number >= sakura.sidebar_next_group_id)
				sakura.sidebar_next_group_id = id_number + 1;
		}
	}
	g_strfreev(group_ids);
	g_strfreev(group_parents);
	g_strfreev(group_titles);

	gtk_tree_view_expand_all(GTK_TREE_VIEW(sakura.sidebar_tree));

	g_signal_connect(new_terminal, "clicked", G_CALLBACK(sakura_new_tab_cb), NULL);
	g_signal_connect(new_group, "clicked", G_CALLBACK(sakura_sidebar_new_group_cb), NULL);
	g_signal_connect(sakura.sidebar_selection, "changed",
	                 G_CALLBACK(sakura_sidebar_selection_changed_cb), NULL);
	g_signal_connect(sakura.sidebar_model, "rows-reordered",
	                 G_CALLBACK(sakura_sidebar_model_reordered_cb), NULL);
	g_signal_connect(sakura.sidebar_tree, "button-press-event",
	                 G_CALLBACK(sakura_sidebar_button_press_cb), NULL);
	gtk_widget_add_events(sakura.sidebar, GDK_BUTTON_PRESS_MASK);
	g_signal_connect(sakura.sidebar, "button-press-event",
	                 G_CALLBACK(sakura_sidebar_button_press_cb), NULL);

	sakura.sidebar_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_pack1(GTK_PANED(sakura.sidebar_paned), sakura.sidebar, FALSE, FALSE);
	gtk_paned_pack2(GTK_PANED(sakura.sidebar_paned), sakura.notebook, TRUE, FALSE);
	gtk_paned_set_position(GTK_PANED(sakura.sidebar_paned), sakura.sidebar_width);
	g_signal_connect(sakura.sidebar_paned, "notify::position",
	                 G_CALLBACK(sakura_sidebar_paned_position_cb), NULL);
	gtk_widget_show_all(sakura.sidebar_paned);
	if (!sakura.sidebar_visible)
		gtk_widget_hide(sakura.sidebar);
}


static void
sakura_set_window_title (const gchar *title)
{
	gtk_window_set_title(GTK_WINDOW(sakura.main_window), title);
	if (sakura.header_bar != NULL) {
		gtk_header_bar_set_title(GTK_HEADER_BAR(sakura.header_bar), title);
	}
}


/* Callback for clicking in the tabs close buttons */
static void
sakura_closebutton_clicked_cb (GtkWidget *widget, void *data)
{
	GtkWidget *hbox = (GtkWidget *)data;
	gint page;

	page = gtk_notebook_page_num(GTK_NOTEBOOK(sakura.notebook), hbox);

	sakura_close_tab(page);
}


/* Callback for clicking in the tabs labels */
static gboolean
sakura_label_clicked_cb (GtkWidget *widget, GdkEventButton *button_event, void *data)
{
	GtkWidget *hbox = (GtkWidget *)data;
	struct sakura_tab *sk_tab;
	gint page;

	page = gtk_notebook_page_num(GTK_NOTEBOOK(sakura.notebook), hbox);
	sk_tab = sakura_get_sktab(sakura, page);

	/* Not interested in non button press events */
	if (button_event->type != GDK_BUTTON_PRESS)
		return FALSE;

	/* Left button click. We HAVE to propagate the event, or things like tab moving won't work */
	if (button_event->button == 1) {
		gtk_widget_grab_focus(sk_tab->vte);
		return FALSE;
	}

	/* Ignore right click and propagate the event */
	if (button_event->button == 3)
		return FALSE;

	/* The middle button was clicked, so close the tab */
	sakura_close_tab(page);

	return TRUE;
}


/*****************/
/* VTE callbacks */
/*****************/


/* Callback for button release on the vte terminal. Used for copy-on-selection to clipboard */
static gboolean
sakura_term_buttonreleased_cb (GtkWidget *widget, GdkEventButton *button_event, gpointer user_data)
{

	if (button_event->type != GDK_BUTTON_RELEASE)
		return FALSE;

	if (sakura.copy_on_select)
		if (button_event->button == 1)
			sakura_copy();

	return FALSE;
}


static gboolean
sakura_term_buttonpressed_cb (GtkWidget *widget, GdkEventButton *button_event, gpointer user_data)
{
	struct sakura_tab *sk_tab;
	gint page, tag;

	if (button_event->type != GDK_BUTTON_PRESS)
		return FALSE;

	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	sk_tab = sakura_get_sktab(sakura, page);

	/* Find out if cursor it's over a matched expression...*/
	sakura.current_match = vte_terminal_match_check_event(VTE_TERMINAL(sk_tab->vte), (GdkEvent *) button_event, &tag);

	/* Left button with Ctrl or the configured accelerator: open the URL if any */
	if (button_event->button == 1 &&
	    (((button_event->state & GDK_CONTROL_MASK) == GDK_CONTROL_MASK) ||
	     ((button_event->state & sakura.open_url_accelerator) == sakura.open_url_accelerator)) &&
	    sakura.current_match) {

		sakura_open_url_cb(NULL, NULL);

		return TRUE;
	}

	/* Paste when paste button is pressed */
	if (sakura.copy_on_select) {
		if (button_event->button == sakura.paste_button) {
			sakura_paste_primary(); /* This is the expected X11 behaviour, to copy the PRIMARY clipboard with the middle click. 
						   TODO: Maybe add an option to use the secondary one? */

			/* Do not propagate. vte has his own copy-on-select and we'll end with duplicates pastes */
			return TRUE;
		}
	}

	/* Show the popup menu when menu button is pressed */
	if (button_event->button == sakura.menu_button) {
		GtkMenu *menu;

		menu = GTK_MENU (user_data);

		if (sakura.current_match) {
			/* Show the extra options in the menu */

			char *matches;
			/* Is it a mail address? */
			if (vte_terminal_event_check_regex_simple(VTE_TERMINAL(sk_tab->vte), (GdkEvent *) button_event,
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
			/* Hide all the options */
			gtk_widget_hide(sakura.item_open_mail);
			gtk_widget_hide(sakura.item_open_link);
			gtk_widget_hide(sakura.item_copy_link);
			gtk_widget_hide(sakura.open_link_separator);
		}

		gtk_menu_popup_at_pointer(menu, (GdkEvent *) button_event);

		return TRUE;
	}

	return FALSE;
}


static void
sakura_beep_cb (GtkWidget *widget, void *data)
{
	/* Remove the urgency hint. This is necessary to signal the window manager  */
	/* that a new urgent event happened when the urgent hint is set after this. */
	/* TODO: this is already set in focus_in, so DO we really need it here? */
	gtk_window_set_urgency_hint(GTK_WINDOW(sakura.main_window), FALSE);

	/* If the window is active(focused), ignore and don't set the urgency hint */
	if (!gtk_window_is_active(GTK_WINDOW(sakura.main_window))) {
		if (sakura.urgent_bell) {
			gtk_window_set_urgency_hint(GTK_WINDOW(sakura.main_window), TRUE);
	}
	}

}


static void
sakura_increase_font_cb (GtkWidget *widget, void *data)
{
	gint new_size;

	/* Increment font size one unit */
	new_size = pango_font_description_get_size(sakura.font)+PANGO_SCALE;

	pango_font_description_set_size(sakura.font, new_size);
	sakura_set_font();
	sakura_set_size();
	sakura_set_config_string("font", pango_font_description_to_string(sakura.font));
}


static void
sakura_decrease_font_cb (GtkWidget *widget, void *data)
{
	gint new_size;

	/* Decrement font size one unit */
	new_size = pango_font_description_get_size(sakura.font)-PANGO_SCALE;

	/* Set a minimal size */
	if (new_size >= FONT_MINIMAL_SIZE) {
		pango_font_description_set_size(sakura.font, new_size);
		sakura_set_font();
		sakura_set_size();
		sakura_set_config_string("font", pango_font_description_to_string(sakura.font));
	}
}


static void
sakura_child_exited_cb (GtkWidget *widget, void *data)
{
	gint page, npages;
	struct sakura_tab *sk_tab;

	page = gtk_notebook_page_num(GTK_NOTEBOOK(sakura.notebook),
				gtk_widget_get_parent(widget));
	npages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));
	sk_tab = sakura_get_sktab(sakura, page);

	/* Only write configuration to disk if it's the last tab */
	if (npages==1) {
		sakura_config_done();
	}

	if (option_hold==TRUE) {
		SAY("hold option has been activated");
		return;
	}

	/* Child should be automatically reaped because we don't use G_SPAWN_DO_NOT_REAP_CHILD flag */
	g_spawn_close_pid(sk_tab->pid);

	sakura_del_tab(page);

	npages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));
	if (npages == 0)
		sakura_destroy();
}


static void
sakura_eof_cb (GtkWidget *widget, void *data)
{
	SAY("Got EOF signal");
}

/* This handler is called when vte window title changes (i.e.: cwd changes),
 * and it is used to change window and notebook pages titles */
static void
sakura_title_changed_cb (GtkWidget *widget, void *data)
{
	struct sakura_tab *sk_tab;
	const char *tabtitle;
	gint modified_page;
	VteTerminal *vte_term=(VteTerminal *)widget;

	modified_page = sakura_find_tab(vte_term);
	sk_tab = sakura_get_sktab(sakura, modified_page);

	tabtitle = vte_terminal_get_window_title(VTE_TERMINAL(sk_tab->vte));
	sakura_update_tab_metadata(sk_tab, tabtitle);

	/* User set values overrides any other one */
	if (!sk_tab->label_set_byuser) {
		sakura_set_tab_label_text(tabtitle, modified_page);
		if (!sakura.main_title) sakura_set_window_title(tabtitle);
	}

}


static gboolean
sakura_delete_event_cb (GtkWidget *widget, void *data)
{
	struct sakura_tab *sk_tab;
	GtkWidget *dialog;
	gint response;
	gint npages;
	gint i;
	pid_t pgid;

	if (!sakura.less_questions) {
		npages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));

		/* Check for each tab if there are running processes. Use tcgetpgrp to compare to the shell PGID */
		for (i=0; i < npages; i++) {

			sk_tab = sakura_get_sktab(sakura, i);
			pgid = tcgetpgrp(vte_pty_get_fd(vte_terminal_get_pty(VTE_TERMINAL(sk_tab->vte))));

			/* If running processes are found, we ask one time and exit */
			if ( (pgid != -1) && (pgid != sk_tab->pid)) {
				dialog=gtk_message_dialog_new(GTK_WINDOW(sakura.main_window), GTK_DIALOG_MODAL,
											  GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
											  _("There are running processes.\n\nDo you really want to close Sakura?"));

				response=gtk_dialog_run(GTK_DIALOG(dialog));
				gtk_widget_destroy(dialog);

				if (response==GTK_RESPONSE_YES) {
					sakura_session_save();
					sakura_config_done();
					return FALSE;
				} else {
					return TRUE;
				}
			}

		}
	}

	sakura_session_save();
	sakura_config_done();
	return FALSE;
}


static void
sakura_destroy_window_cb (GtkWidget *widget, void *data)
{
	sakura_destroy();
}


/**********************/
/* Menuitem callbacks */
/**********************/


static void
sakura_font_dialog_cb (GtkWidget *widget, void *data)
{
	GtkWidget *font_dialog;
	gint response;

	font_dialog = gtk_font_chooser_dialog_new(_("Select font"), GTK_WINDOW(sakura.main_window));
	gtk_font_chooser_set_font_desc(GTK_FONT_CHOOSER(font_dialog), sakura.font);

	response = gtk_dialog_run(GTK_DIALOG(font_dialog));

	if (response == GTK_RESPONSE_OK) {
		pango_font_description_free(sakura.font);
		sakura.font = gtk_font_chooser_get_font_desc(GTK_FONT_CHOOSER(font_dialog));
		sakura_set_font();
		sakura_set_size();
		sakura_set_config_string("font", pango_font_description_to_string(sakura.font));
	}

	gtk_widget_destroy(font_dialog);
}


static void
sakura_set_name_dialog_cb (GtkWidget *widget, void *data)
{
	GtkWidget *input_dialog, *input_header;
	GtkWidget *entry, *label;
	GtkWidget *name_hbox; /* We need this for correct spacing */
	gint response;
	gint page;
	struct sakura_tab *sk_tab;
	const gchar *text;

	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	sk_tab = sakura_get_sktab(sakura, page);

	input_dialog=gtk_dialog_new_with_buttons(_("Set tab name"),
	                                         GTK_WINDOW(sakura.main_window),
                                                 GTK_DIALOG_MODAL|GTK_DIALOG_USE_HEADER_BAR,
	                                         _("_Cancel"), GTK_RESPONSE_CANCEL,
	                                         _("_Apply"), GTK_RESPONSE_ACCEPT,
	                                         NULL);

	/* Configure the new gtk header bar*/
	input_header = gtk_dialog_get_header_bar(GTK_DIALOG(input_dialog));
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(input_header), FALSE);

	gtk_dialog_set_default_response(GTK_DIALOG(input_dialog), GTK_RESPONSE_ACCEPT);

	/* Create dialog contents */
	name_hbox=gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	entry=gtk_entry_new();
	label=gtk_label_new(_("New text"));
	/* Set tab label as entry default text (when first tab is not displayed, get_tab_label_text
	   returns a null value, so check accordingly */
	/* FIXME: Check why is returning NULL */
	text = gtk_notebook_get_tab_label_text(GTK_NOTEBOOK(sakura.notebook), sk_tab->hbox);
	if (text) {
		SAY("TEXT %s", text);
		gtk_entry_set_text(GTK_ENTRY(entry), text);
	}
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_box_pack_start(GTK_BOX(name_hbox), label, TRUE, TRUE, 12);
	gtk_box_pack_start(GTK_BOX(name_hbox), entry, TRUE, TRUE, 12);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(input_dialog))), name_hbox, FALSE, FALSE, 12);

	/* Disable accept button until some text is entered */
	g_signal_connect(G_OBJECT(entry), "changed", G_CALLBACK(sakura_setname_entry_changed_cb), input_dialog);
	gtk_dialog_set_response_sensitive(GTK_DIALOG(input_dialog), GTK_RESPONSE_ACCEPT, FALSE);

	gtk_widget_show_all(name_hbox);

	response = gtk_dialog_run(GTK_DIALOG(input_dialog));

	if (response == GTK_RESPONSE_ACCEPT) {
		sakura_set_tab_label_text(gtk_entry_get_text(GTK_ENTRY(entry)), page);
		sakura_set_window_title(gtk_entry_get_text(GTK_ENTRY(entry)));
		sk_tab->label_set_byuser=true; 
		sakura_sidebar_update_tab(sk_tab);
		sakura.main_title=NULL; /* Ignore the user-set window title if the user names the tab */
	}

	gtk_widget_destroy(input_dialog);
}



/* Callback for the color dialog signals. Used to UPDATE the contents of that dialog (passed as 'data') */
static void
sakura_color_dialog_changed_cb ( GtkWidget *widget, void *data)
{
	GtkDialog *dialog = (GtkDialog*) data;
	GtkColorButton *fore_button = g_object_get_data (G_OBJECT(dialog), "fore_button");
	GtkColorButton *back_button = g_object_get_data (G_OBJECT(dialog), "back_button");
	GtkColorButton *curs_button = g_object_get_data (G_OBJECT(dialog), "curs_button");
	GdkRGBA *forecolors = g_object_get_data (G_OBJECT(dialog), "fore");
	GdkRGBA *backcolors = g_object_get_data (G_OBJECT(dialog), "back");
	GdkRGBA *curscolors = g_object_get_data (G_OBJECT(dialog), "curs");
	GtkComboBox *cs_combo = g_object_get_data (G_OBJECT(dialog), "cs_combo");
	GtkComboBox *scheme_combo = g_object_get_data (G_OBJECT(dialog), "scheme_combo");
	GtkSpinButton *opacity_spin = g_object_get_data (G_OBJECT(dialog), "opacity_spin");
	GtkCheckButton *bib_checkbutton = g_object_get_data (G_OBJECT(dialog), "bib_checkbutton");

	gint current_cs = gtk_combo_box_get_active(cs_combo);

	/* If we come here as a result of a change in the active colorset, load the new colorset to the buttons.
	 * Else, the color buttons or opacity spin have gotten a new value, store that. */
	if ((GtkWidget *)cs_combo == widget ) {
		/* Spin opacity is a percentage, convert it*/
		gint new_opacity = (int) (backcolors[current_cs].alpha*100);
		gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(fore_button), &forecolors[current_cs]);
		gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(back_button), &backcolors[current_cs]);
		gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(curs_button), &curscolors[current_cs]);
		gtk_spin_button_set_value(opacity_spin, new_opacity);
		gtk_combo_box_set_active(GTK_COMBO_BOX(scheme_combo), sakura.schemes[current_cs]);
	} else if ((GtkWidget *)scheme_combo == widget) {
		/* Scheme has changed, update the buttons. No cursor and no alpha */
		int selected_scheme = gtk_combo_box_get_active(GTK_COMBO_BOX(scheme_combo));
		if (selected_scheme != 0) {
			float old_alpha = backcolors[current_cs].alpha; /* Keep the previous alpha */
			gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(fore_button), &predefined_schemes[selected_scheme].fg);
			gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(back_button), &predefined_schemes[selected_scheme].bg);
			forecolors[current_cs] = predefined_schemes[selected_scheme].fg;
			backcolors[current_cs] = predefined_schemes[selected_scheme].bg;
			backcolors[current_cs].alpha = old_alpha;
			sakura.schemes[current_cs] = selected_scheme;
		} /* else Custom, do nothing */
	} else if ((GtkWidget *)bib_checkbutton == widget) {
		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(bib_checkbutton))) {
			sakura.bold_is_bright = true;
		}
		else {
			sakura.bold_is_bright = false;
		}
	} else {
		gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(fore_button), &forecolors[current_cs]);
		gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(back_button), &backcolors[current_cs]);
		gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(curs_button), &curscolors[current_cs]);
		gtk_spin_button_update(opacity_spin);
		backcolors[current_cs].alpha = gtk_spin_button_get_value(opacity_spin)/100;
		/* User changed colors. Set custom scheme */
		sakura.schemes[current_cs] = 0;
		gtk_combo_box_set_active(GTK_COMBO_BOX(scheme_combo), sakura.schemes[current_cs]);
	}

}


/* Dialog to select foreground, background and cursors colors, transparency and palette */
static void
sakura_color_dialog_cb (GtkWidget *widget, void *data)
{
	GtkWidget *color_dialog; GtkWidget *color_header;
	GtkWidget *cs_label, *scheme_label, *fore_label, *back_label, *curs_label, *opacity_label, *palette_label;
	GtkWidget *cs_combo, *scheme_combo, *fore_button, *back_button, *curs_button, *palette_combo, *opacity_spin;
	GtkWidget *cs_hbox, *scheme_hbox, *fore_hbox, *back_hbox, *curs_hbox, *opacity_hbox, *palette_hbox, *bib_hbox;
	GtkWidget *bib_checkbutton;
	GdkRGBA temp_fore[NUM_COLORSETS]; GdkRGBA temp_back[NUM_COLORSETS];	GdkRGBA temp_curs[NUM_COLORSETS];
	GtkAdjustment *spin_adj;
	struct sakura_tab *sk_tab;
	gint response;
	gint page, i;


	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	sk_tab = sakura_get_sktab(sakura, page);

	color_dialog = gtk_dialog_new_with_buttons(_("Select colors"), GTK_WINDOW(sakura.main_window),
	                                           GTK_DIALOG_MODAL|GTK_DIALOG_USE_HEADER_BAR,
	                                           _("_Cancel"), GTK_RESPONSE_CANCEL, _("_Select"), GTK_RESPONSE_ACCEPT, NULL);

	/* Configure the new gtk header bar */
	color_header = gtk_dialog_get_header_bar(GTK_DIALOG(color_dialog));
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(color_header), FALSE);
	gtk_dialog_set_default_response(GTK_DIALOG(color_dialog), GTK_RESPONSE_ACCEPT);

	/* Add the combobox to select the current colorset */
	cs_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	cs_label = gtk_label_new(_("Colorset"));
	cs_combo = gtk_combo_box_text_new();
	gchar combo_text[3];
	for (i=0; i < NUM_COLORSETS; i++) {
		g_snprintf(combo_text, 2, "%d", i+1);
		gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(cs_combo), NULL, combo_text);
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(cs_combo), sk_tab->colorset);

	/* Add the scheme combobox */
	scheme_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	scheme_label = gtk_label_new(_("Color scheme"));
	scheme_combo = gtk_combo_box_text_new();
	for (i=0; i < NUM_SCHEMES; i++) {
		gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(scheme_combo), NULL, predefined_schemes[i].name);
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(scheme_combo), sakura.schemes[sk_tab->colorset]);

	/* Foreground and background and cursor color buttons */
	fore_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	back_hbox = gtk_box_new(FALSE, 12);
	curs_hbox = gtk_box_new(FALSE, 12);
	fore_label = gtk_label_new(_("Foreground color"));
	back_label = gtk_label_new(_("Background color"));
	curs_label = gtk_label_new(_("Cursor color"));
	fore_button = gtk_color_button_new_with_rgba(&sakura.forecolors[sk_tab->colorset]);
	back_button = gtk_color_button_new_with_rgba(&sakura.backcolors[sk_tab->colorset]);
	curs_button = gtk_color_button_new_with_rgba(&sakura.curscolors[sk_tab->colorset]);

	/* Opacity control */
	opacity_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	spin_adj = gtk_adjustment_new ((sakura.backcolors[sk_tab->colorset].alpha)*100, 0.0, 100.0, 1.0, 5.0, 0);
	opacity_spin = gtk_spin_button_new(GTK_ADJUSTMENT(spin_adj), 1.0, 0);
	opacity_label = gtk_label_new(_("Opacity level (%)"));

	/* Palette combobox */
	palette_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	palette_label = gtk_label_new(_("Palette"));
	palette_combo = gtk_combo_box_text_new();
	for (i=0; palettes_names[i] != NULL; i++) {
		if (i == SYSTEM_PALETTE_INDEX && !sakura.have_system_colors) {
			continue;
		}
		gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(palette_combo), NULL, palettes_names[i]);
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(palette_combo), sakura.palette_idx);

	/* Bold is bright checkbutton */
	bib_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	bib_checkbutton = gtk_check_button_new_with_label(_("Use bright colors for bold text"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(bib_checkbutton), sakura.bold_is_bright);

	gtk_box_pack_start(GTK_BOX(cs_hbox), cs_label, FALSE, FALSE, 12);
	gtk_box_pack_end(GTK_BOX(cs_hbox), cs_combo, FALSE, FALSE, 12);
	gtk_box_pack_start(GTK_BOX(scheme_hbox), scheme_label, FALSE, FALSE, 12);
	gtk_box_pack_end(GTK_BOX(scheme_hbox), scheme_combo, FALSE, FALSE, 12);
	gtk_box_pack_start(GTK_BOX(fore_hbox), fore_label, FALSE, FALSE, 12);
	gtk_box_pack_end(GTK_BOX(fore_hbox), fore_button, FALSE, FALSE, 12);
	gtk_box_pack_start(GTK_BOX(back_hbox), back_label, FALSE, FALSE, 12);
	gtk_box_pack_end(GTK_BOX(back_hbox), back_button, FALSE, FALSE, 12);
	gtk_box_pack_start(GTK_BOX(curs_hbox), curs_label, FALSE, FALSE, 12);
	gtk_box_pack_end(GTK_BOX(curs_hbox), curs_button, FALSE, FALSE, 12);
	gtk_box_pack_start(GTK_BOX(opacity_hbox), opacity_label, FALSE, FALSE, 12);
	gtk_box_pack_end(GTK_BOX(opacity_hbox), opacity_spin, FALSE, FALSE, 12);
	gtk_box_pack_start(GTK_BOX(palette_hbox), palette_label, FALSE, FALSE, 12);
	gtk_box_pack_end(GTK_BOX(palette_hbox), palette_combo, FALSE, FALSE, 12);
	gtk_box_pack_start(GTK_BOX(bib_hbox), bib_checkbutton, FALSE, FALSE, 12);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(color_dialog))), cs_hbox, FALSE, FALSE, 6);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(color_dialog))), scheme_hbox, FALSE, FALSE, 6);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(color_dialog))), fore_hbox, FALSE, FALSE, 6);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(color_dialog))), back_hbox, FALSE, FALSE, 6);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(color_dialog))), curs_hbox, FALSE, FALSE, 6);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(color_dialog))), opacity_hbox, FALSE, FALSE, 6);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(color_dialog))), palette_hbox, FALSE, FALSE, 6);
	gtk_box_pack_end(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(color_dialog))), bib_hbox, FALSE, FALSE, 6);

	gtk_widget_show_all(gtk_dialog_get_content_area(GTK_DIALOG(color_dialog)));

	/* When the user switches the colorset, callback needs access to these selector widgets */
	g_object_set_data(G_OBJECT(color_dialog), "cs_combo", cs_combo);
	g_object_set_data(G_OBJECT(color_dialog), "scheme_combo", scheme_combo);
	g_object_set_data(G_OBJECT(color_dialog), "fore_button", fore_button);
	g_object_set_data(G_OBJECT(color_dialog), "back_button", back_button);
	g_object_set_data(G_OBJECT(color_dialog), "curs_button", curs_button);
	g_object_set_data(G_OBJECT(color_dialog), "opacity_spin", opacity_spin);
	g_object_set_data(G_OBJECT(color_dialog), "fore", temp_fore);
	g_object_set_data(G_OBJECT(color_dialog), "back", temp_back);
	g_object_set_data(G_OBJECT(color_dialog), "curs", temp_curs);
	g_object_set_data(G_OBJECT(color_dialog), "bib_checkbutton", bib_checkbutton);

	g_signal_connect(G_OBJECT(cs_combo), "changed", G_CALLBACK(sakura_color_dialog_changed_cb), color_dialog);
	g_signal_connect(G_OBJECT(scheme_combo), "changed", G_CALLBACK(sakura_color_dialog_changed_cb), color_dialog);
	g_signal_connect(G_OBJECT(fore_button), "color-set", G_CALLBACK(sakura_color_dialog_changed_cb), color_dialog);
	g_signal_connect(G_OBJECT(back_button), "color-set", G_CALLBACK(sakura_color_dialog_changed_cb), color_dialog);
	g_signal_connect(G_OBJECT(curs_button), "color-set", G_CALLBACK(sakura_color_dialog_changed_cb), color_dialog);
	g_signal_connect(G_OBJECT(opacity_spin), "changed", G_CALLBACK(sakura_color_dialog_changed_cb), color_dialog);
	g_signal_connect(G_OBJECT(bib_checkbutton), "toggled", G_CALLBACK(sakura_color_dialog_changed_cb), color_dialog);

	for (i=0; i<NUM_COLORSETS; i++) {
		temp_fore[i] = sakura.forecolors[i];
		temp_back[i] = sakura.backcolors[i];
		temp_curs[i] = sakura.curscolors[i];
	}

	response = gtk_dialog_run(GTK_DIALOG(color_dialog));

	if (response==GTK_RESPONSE_ACCEPT) {
		/* Save all colorsets to both the global struct and configuration.*/
		for (i=0; i<NUM_COLORSETS; i++) {
			char name[20];
			gchar *cfgtmp;

			sakura.forecolors[i] = temp_fore[i];
			sakura.backcolors[i] = temp_back[i];
			sakura.curscolors[i] = temp_curs[i];

			sprintf(name, "colorset%d_fore", i+1);
			cfgtmp = gdk_rgba_to_string(&sakura.forecolors[i]);
			sakura_set_config_string(name, cfgtmp);
			g_free(cfgtmp);

			sprintf(name, "colorset%d_back", i+1);
			cfgtmp = gdk_rgba_to_string(&sakura.backcolors[i]);
			sakura_set_config_string(name, cfgtmp);
			g_free(cfgtmp);

			sprintf(name, "colorset%d_curs", i+1);
			cfgtmp = gdk_rgba_to_string(&sakura.curscolors[i]);
			sakura_set_config_string(name, cfgtmp);
			g_free(cfgtmp);

			sprintf(name, "colorset%d_scheme", i+1);
			sakura_set_config_integer(name, sakura.schemes[i]);
		}

		/* Set the current tab's colorset to the last selected one in the dialog.
		 * This is probably what the new user expects, and the experienced user hopefully will not mind. */
		sk_tab->colorset = gtk_combo_box_get_active(GTK_COMBO_BOX(cs_combo));
		sakura_set_config_integer("last_colorset", sk_tab->colorset+1);

		/* Set the selected palette */
		guint palette_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(palette_combo));
		if (palette_idx == SYSTEM_PALETTE_INDEX && sakura.have_system_colors) {
			sakura.palette = sakura.system_palette;
		} else if (palette_idx < SYSTEM_PALETTE_INDEX) {
			sakura.palette = palettes[palette_idx];
		} else {
			palette_idx = DEFAULT_PALETTE;
			sakura.palette = palettes[DEFAULT_PALETTE];
		}
		sakura.palette_idx = palette_idx;
		sakura_set_config_integer("palette", sakura.palette_idx);

		/* Set bold is bright option */
		sakura_set_config_boolean("bold_is_bright", sakura.bold_is_bright);

		/* Apply the new colorsets to all tabs */
		sakura_set_colors();
	}

	gtk_widget_destroy(color_dialog);
}


#if 0
static void
sakura_set_title_dialog (GtkWidget *widget, void *data)
{
	GtkWidget *title_dialog, *title_header;
	GtkWidget *entry, *label;
	GtkWidget *title_hbox;
	gint response;

	title_dialog=gtk_dialog_new_with_buttons(_("Set window title"),
	                                         GTK_WINDOW(sakura.main_window),
	                                         GTK_DIALOG_MODAL|GTK_DIALOG_USE_HEADER_BAR,
	                                         _("_Cancel"), GTK_RESPONSE_CANCEL,
	                                         _("_Apply"), GTK_RESPONSE_ACCEPT,
	                                          NULL);

	/* Configure the new gtk header bar*/
	title_header=gtk_dialog_get_header_bar(GTK_DIALOG(title_dialog));
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(title_header), FALSE);
	gtk_dialog_set_default_response(GTK_DIALOG(title_dialog), GTK_RESPONSE_ACCEPT);

	entry=gtk_entry_new();
	label=gtk_label_new(_("New window title"));
	title_hbox=gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	/* Set window label as entry default text */
	gtk_entry_set_text(GTK_ENTRY(entry), gtk_window_get_title(GTK_WINDOW(sakura.main_window)));
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_box_pack_start(GTK_BOX(title_hbox), label, TRUE, TRUE, 12);
	gtk_box_pack_start(GTK_BOX(title_hbox), entry, TRUE, TRUE, 12);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(title_dialog))), title_hbox, FALSE, FALSE, 12);

	/* Disable accept button until some text is entered */
	g_signal_connect(G_OBJECT(entry), "changed", G_CALLBACK(sakura_setname_entry_changed), title_dialog);
	gtk_dialog_set_response_sensitive(GTK_DIALOG(title_dialog), GTK_RESPONSE_ACCEPT, FALSE);

	gtk_widget_show_all(title_hbox);

	response=gtk_dialog_run(GTK_DIALOG(title_dialog));
	if (response==GTK_RESPONSE_ACCEPT) {
		/* Bug #257391 shadow reaches here too... */
		sakura_set_window_title(gtk_entry_get_text(GTK_ENTRY(entry)));
	}
	gtk_widget_destroy(title_dialog);
}
#endif


static void
sakura_copy_url_cb (GtkWidget *widget, void *data)
{
	GtkClipboard* clip;

	clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
	gtk_clipboard_set_text(clip, sakura.current_match, -1 );
	//clip = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
	//gtk_clipboard_set_text(clip, sakura.current_match, -1 );

}


static void
sakura_open_url_cb (GtkWidget *widget, void *data)
{
	GError *error=NULL;
	gchar *browser=NULL;

	SAY("Opening %s", sakura.current_match);

	browser = g_strdup(g_getenv("BROWSER"));

	if (!browser) {
		if ( !(browser = g_find_program_in_path("xdg-open")) ) {
			sakura_error("Browser not found");
		}
	}

	if (browser) {
		gchar * argv[] = {browser, sakura.current_match, NULL};
		if (!g_spawn_async(".", argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {
			sakura_error("Couldn't exec \"%s %s\": %s", browser, sakura.current_match, error->message);
			g_error_free(error);
		}

		g_free(browser);
	}
}


static void
sakura_open_mail_cb (GtkWidget *widget, void *data)
{
	GError *error = NULL;
	gchar *program = NULL;

	if ( (program = g_find_program_in_path("xdg-email")) ) {
		gchar * argv[] = { program, sakura.current_match, NULL };
		if (!g_spawn_async(".", argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {
			sakura_error("Couldn't exec \"%s %s\": %s", program, sakura.current_match, error->message);
		}
		g_free(program);
	}
}


static void
sakura_show_tab_bar_cb (GtkWidget *widget, void *data)
{
	char *setting_string = (char *)data;
	char *config_string;
	gboolean show_tabs;

	if (strcmp(setting_string, "always")==0) {
		sakura.show_tab_bar = SHOW_TAB_BAR_ALWAYS;
		config_string = "always";
		show_tabs = TRUE;
	} else if (strcmp(setting_string, "multiple")==0) {
		sakura.show_tab_bar = SHOW_TAB_BAR_MULTIPLE;
		config_string = "multiple";
		show_tabs = (gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook)) != 1);
	} else if (strcmp(setting_string, "never")==0) {
		sakura.show_tab_bar = SHOW_TAB_BAR_NEVER;
		config_string = "never";
		show_tabs = FALSE;
	}

	sakura_set_config_string("show_tab_bar", config_string);
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(sakura.notebook), show_tabs);

	sakura_set_size();
}


static void
sakura_tabs_on_bottom_cb (GtkWidget *widget, void *data)
{

	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
		gtk_notebook_set_tab_pos(GTK_NOTEBOOK(sakura.notebook), GTK_POS_BOTTOM);
		sakura_set_config_boolean("tabs_on_bottom", TRUE);
	} else {
		gtk_notebook_set_tab_pos(GTK_NOTEBOOK(sakura.notebook), GTK_POS_TOP);
		sakura_set_config_boolean("tabs_on_bottom", FALSE);
	}
}


static void
sakura_less_questions_cb (GtkWidget *widget, void *data)
{

	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
		sakura.less_questions = TRUE;
		sakura_set_config_boolean("less_questions", TRUE);
	} else {
		sakura.less_questions = FALSE;
		sakura_set_config_boolean("less_questions", FALSE);
	}
}


static void
sakura_copy_on_select_cb (GtkWidget *widget, void *data)
{
        sakura.copy_on_select = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget));
        if (sakura.copy_on_select) {
                sakura_set_config_boolean("copy_on_select", TRUE);
        } else {
                sakura_set_config_boolean("copy_on_select", FALSE);
        }
}


static void
sakura_new_tab_after_current_cb (GtkWidget *widget, void *data)
{
	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
		sakura.new_tab_after_current=TRUE;
		sakura_set_config_boolean("new_tab_after_current", TRUE);
	} else {
		sakura.new_tab_after_current=FALSE;
		sakura_set_config_boolean("new_tab_after_current", FALSE);
	}
}


static void
sakura_show_scrollbar_cb (GtkWidget *widget, void *data)
{
	sakura_show_scrollbar();
}


static void
sakura_urgent_bell_cb (GtkWidget *widget, void *data)
{
	sakura.urgent_bell = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget));
	if (sakura.urgent_bell) {
		sakura_set_config_string("urgent_bell", "Yes");
	} else {
		sakura_set_config_string("urgent_bell", "No");
	}
}


static void
sakura_audible_bell_cb (GtkWidget *widget, void *data)
{
	gint page;
	struct sakura_tab *sk_tab;

	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	sk_tab = sakura_get_sktab(sakura, page);

	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
		vte_terminal_set_audible_bell (VTE_TERMINAL(sk_tab->vte), TRUE);
		sakura_set_config_string("audible_bell", "Yes");
	} else {
		vte_terminal_set_audible_bell (VTE_TERMINAL(sk_tab->vte), FALSE);
		sakura_set_config_string("audible_bell", "No");
	}
}


static void
sakura_blinking_cursor_cb (GtkWidget *widget, void *data)
{
	gint page;
	struct sakura_tab *sk_tab;

	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	sk_tab = sakura_get_sktab(sakura, page);

	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
		vte_terminal_set_cursor_blink_mode (VTE_TERMINAL(sk_tab->vte), VTE_CURSOR_BLINK_ON);
		sakura_set_config_string("blinking_cursor", "Yes");
	} else {
		vte_terminal_set_cursor_blink_mode (VTE_TERMINAL(sk_tab->vte), VTE_CURSOR_BLINK_OFF);
		sakura_set_config_string("blinking_cursor", "No");
	}
}



static void
sakura_set_cursor_cb (GtkWidget *widget, void *data)
{
	struct sakura_tab *sk_tab;
	int n_pages, i;

	char *cursor_string = (char *)data;
	n_pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));

	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {

		if (strcmp(cursor_string, "block")==0) {
			sakura.cursor_type=VTE_CURSOR_SHAPE_BLOCK;
		} else if (strcmp(cursor_string, "underline")==0) {
			sakura.cursor_type=VTE_CURSOR_SHAPE_UNDERLINE;
		} else if (strcmp(cursor_string, "ibeam")==0) {
			sakura.cursor_type=VTE_CURSOR_SHAPE_IBEAM;
		}

		for (i = (n_pages - 1); i >= 0; i--) {
			sk_tab = sakura_get_sktab(sakura, i);
			vte_terminal_set_cursor_shape(VTE_TERMINAL(sk_tab->vte), sakura.cursor_type);
		}

		sakura_set_config_integer("cursor_type", sakura.cursor_type);
	}
}


static void
sakura_setname_entry_changed_cb (GtkWidget *widget, void *data)
{
	GtkDialog *title_dialog=(GtkDialog *)data;

	if (strcmp(gtk_entry_get_text(GTK_ENTRY(widget)), "")==0) {
		gtk_dialog_set_response_sensitive(GTK_DIALOG(title_dialog), GTK_RESPONSE_ACCEPT, FALSE);
	} else {
		gtk_dialog_set_response_sensitive(GTK_DIALOG(title_dialog), GTK_RESPONSE_ACCEPT, TRUE);
	}
}


/* Parameters are never used */
static void
sakura_copy_cb (GtkWidget *widget, void *data)
{
	sakura_copy();
}


/* Parameters are never used */
static void
sakura_paste_cb (GtkWidget *widget, void *data)
{
	sakura_paste();
}


static void
sakura_new_tab_cb (GtkWidget *widget, void *data)
{
	sakura_add_tab();
}


static void
sakura_new_codex_cb (GtkWidget *widget, void *data)
{
	sakura_add_tab_with_options(NULL, NULL, NULL, FALSE, SAKURA_TAB_CODEX, NULL, NULL, NULL);
}


static void
sakura_resume_codex_cb (GtkWidget *widget, void *data)
{
	GtkWidget *dialog, *entry;
	const gchar *session;

	dialog = gtk_dialog_new_with_buttons(_("Resume Codex session"),
	                                     GTK_WINDOW(sakura.main_window),
	                                     GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR,
	                                     _("_Cancel"), GTK_RESPONSE_CANCEL,
	                                     _("_Resume"), GTK_RESPONSE_ACCEPT,
	                                     NULL);
	entry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(entry), _("Session ID or name"));
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
	                   entry, FALSE, FALSE, 12);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
	gtk_widget_show_all(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		session = gtk_entry_get_text(GTK_ENTRY(entry));
		if (session[0] != '\0')
			sakura_add_tab_with_options(NULL, NULL, NULL, FALSE,
			                            SAKURA_TAB_CODEX, session, NULL, NULL);
	}
	gtk_widget_destroy(dialog);
}


static void
sakura_attach_codex_cb (GtkWidget *widget, void *data)
{
	GtkWidget *dialog, *entry;
	gint page;
	struct sakura_tab *sk_tab;
	gchar *session;

	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	if (page < 0)
		return;
	sk_tab = sakura_get_sktab(sakura, page);

	dialog = gtk_dialog_new_with_buttons(_("Attach Codex session"),
	                                     GTK_WINDOW(sakura.main_window),
	                                     GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR,
	                                     _("_Cancel"), GTK_RESPONSE_CANCEL,
	                                     _("_Attach"), GTK_RESPONSE_ACCEPT,
	                                     NULL);
	entry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(entry), _("Session ID or name"));
	if (sk_tab->codex_session_id != NULL)
		gtk_entry_set_text(GTK_ENTRY(entry), sk_tab->codex_session_id);
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
	                   entry, FALSE, FALSE, 12);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
	gtk_widget_show_all(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		session = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
		g_strstrip(session);
		if (session[0] != '\0') {
			g_free(sk_tab->codex_session_id);
			sk_tab->codex_session_id = session;
			g_free(sk_tab->codex_session_name);
			sk_tab->codex_session_name = sakura_codex_session_id_is_uuid(session)
			                             ? NULL : g_strdup(session);
			sk_tab->kind = SAKURA_TAB_CODEX;
			sakura_sidebar_update_tab(sk_tab);
			sakura_codex_sync_name(sk_tab);
			sakura_session_save();
		} else {
			g_free(session);
		}
	}
	gtk_widget_destroy(dialog);
}


static void
sakura_refresh_codex_name_cb (GtkWidget *widget, void *data)
{
	gint page;
	struct sakura_tab *sk_tab;

	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	if (page < 0)
		return;
	sk_tab = sakura_get_sktab(sakura, page);
	sakura_codex_sync_name(sk_tab);
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


static void
sakura_codex_tracking_status_cb (GtkWidget *widget, void *data)
{
	GtkWidget *message;
	struct sakura_tab *sk_tab = NULL;
	gchar *hook_path, *hook_config, *text;
	gsize hook_config_length;
	gint page;
	gboolean environment_ok = FALSE;
	gboolean hook_configured = FALSE;

	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	if (page >= 0) {
		sk_tab = sakura_get_sktab(sakura, page);
		environment_ok = sakura_process_has_environment(
			sk_tab->pid, "SAKURA_CODEX_TAB_TOKEN", sk_tab->codex_tracking_token);
	}

	hook_path = g_build_filename(g_get_home_dir(), ".codex", "hooks.json", NULL);
	hook_config = NULL;
	if (g_file_get_contents(hook_path, &hook_config, &hook_config_length, NULL))
		hook_configured = strstr(hook_config, CODEX_HOOK_MARKER) != NULL;
	text = g_strdup_printf(
		_("Codex tracking status:\n\n"
		"Hook entry: %s\n"
		"Tracking directory: %s\n"
		"Current tab environment: %s\n"
		"Current tab kind: %s\n"
		"Current Codex session: %s\n"
		"Current Codex name: %s"),
		hook_configured ? _("present") : _("missing"),
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
	g_free(hook_config);
	g_free(hook_path);
}


static void
sakura_install_codex_hook_cb (GtkWidget *widget, void *data)
{
	gchar *hook, *standard_error = NULL;
	GError *error = NULL;
	gint status;

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
		if (error != NULL)
			g_error_free(error);
		g_free(standard_error);
		return;
	}
	g_free(standard_error);

	{
		GtkWidget *message = gtk_message_dialog_new(GTK_WINDOW(sakura.main_window),
		                                           GTK_DIALOG_DESTROY_WITH_PARENT,
		                                           GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
		                                           _("Codex session tracking is enabled."));
		gtk_dialog_run(GTK_DIALOG(message));
		gtk_widget_destroy(message);
	}
}


static void
sakura_close_tab_cb (GtkWidget *widget, void *data)
{
	gint page;

	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));

	sakura_close_tab(page);
}



static void
sakura_fullscreen_cb (GtkWidget *widget, void *data)
{
	if (!sakura.fullscreen) {
		sakura.fullscreen = TRUE;
		gtk_window_fullscreen(GTK_WINDOW(sakura.main_window));
	} else {
		sakura.fullscreen = FALSE;
		gtk_window_unfullscreen(GTK_WINDOW(sakura.main_window));
	}
}


static void
sakura_disable_numbered_tabswitch_cb (GtkWidget *widget, void *data)
{
	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
		sakura.disable_numbered_tabswitch = true;
		sakura_set_config_boolean("disable_numbered_tabswitch", TRUE);
	} else {
		sakura.disable_numbered_tabswitch = false;
		sakura_set_config_boolean("disable_numbered_tabswitch", FALSE);
	}
}


#if 0
static void
sakura_use_fading_cb (GtkWidget *widget, void *data)
{
	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
		sakura.use_fading = true;
		sakura_set_config_boolean("use_fading", TRUE);
	} else {
		sakura.use_fading = false;
		sakura_set_config_boolean("use_fading", FALSE);
	}
}
#endif


/**************************/
/******* Functions ********/
/**************************/

static gboolean
sakura_prefers_dark_theme (void)
{
	GSettingsSchemaSource *source;
	GSettingsSchema *schema;
	GSettings *settings;
	gchar *theme_variant;
	gboolean prefers_dark = FALSE;

	source = g_settings_schema_source_get_default();
	if (source == NULL) {
		return FALSE;
	}

	/* GNOME Terminal has its own theme variant setting. Use it when the
	 * schema is available so sakura follows the terminal's appearance. */
	schema = g_settings_schema_source_lookup(source,
	                                         "org.gnome.Terminal.Legacy.Settings",
	                                         TRUE);
	if (schema != NULL) {
		settings = g_settings_new_full(schema, NULL, NULL);
		theme_variant = g_settings_get_string(settings, "theme-variant");
		prefers_dark = g_strcmp0(theme_variant, "dark") == 0;
		g_free(theme_variant);
		g_object_unref(settings);
		g_settings_schema_unref(schema);
		return prefers_dark;
	}

	/* Fall back to the desktop-wide preference on systems without GNOME
	 * Terminal's settings schema. */
	schema = g_settings_schema_source_lookup(source,
	                                         "org.gnome.desktop.interface",
	                                         TRUE);
	if (schema != NULL) {
		settings = g_settings_new_full(schema, NULL, NULL);
		theme_variant = g_settings_get_string(settings, "color-scheme");
		prefers_dark = g_strcmp0(theme_variant, "prefer-dark") == 0;
		g_free(theme_variant);
		g_object_unref(settings);
		g_settings_schema_unref(schema);
	}

	return prefers_dark;
}

static void
sakura_set_dark_theme_environment (void)
{
	GSettingsSchemaSource *source;
	GSettingsSchema *schema;
	GSettings *settings;
	gchar *theme_name;
	gchar *gtk_theme;

	if (!sakura_prefers_dark_theme() || g_getenv("GTK_THEME") != NULL) {
		return;
	}

	source = g_settings_schema_source_get_default();
	if (source == NULL) {
		return;
	}

	schema = g_settings_schema_source_lookup(source,
	                                         "org.gnome.desktop.interface",
	                                         TRUE);
	if (schema == NULL) {
		return;
	}

	settings = g_settings_new_full(schema, NULL, NULL);
	theme_name = g_settings_get_string(settings, "gtk-theme");
	if (theme_name != NULL && theme_name[0] != '\0') {
		gtk_theme = g_strdup_printf("%s:dark", theme_name);
		g_setenv("GTK_THEME", gtk_theme, FALSE);
		g_free(gtk_theme);
	}
	g_free(theme_name);
	g_object_unref(settings);
	g_settings_schema_unref(schema);
}

static gchar *
sakura_get_default_font (void)
{
	GSettingsSchemaSource *source;
	GSettingsSchema *schema;
	GSettings *settings;
	gchar *font_name;

	source = g_settings_schema_source_get_default();
	if (source == NULL) {
		return g_strdup(DEFAULT_FONT);
	}

	schema = g_settings_schema_source_lookup(source,
	                                         "org.gnome.desktop.interface",
	                                         TRUE);
	if (schema == NULL) {
		return g_strdup(DEFAULT_FONT);
	}

	settings = g_settings_new_full(schema, NULL, NULL);
	font_name = g_settings_get_string(settings, "monospace-font-name");
	g_object_unref(settings);
	g_settings_schema_unref(schema);

	if (font_name == NULL || font_name[0] == '\0') {
		g_free(font_name);
		return g_strdup(DEFAULT_FONT);
	}

	return font_name;
}

static gboolean
sakura_load_gnome_terminal_colors (void)
{
	GSettingsSchemaSource *source;
	GSettingsSchema *profiles_schema;
	GSettingsSchema *profile_schema;
	GSettings *profiles;
	GSettings *profile;
	GtkStyleContext *style_context;
	GdkRGBA theme_background;
	gchar *uuid;
	gchar *profile_path;
	gchar *value;
	gchar **palette;
	gboolean use_theme_colors;
	gboolean cursor_colors_set;
	gboolean valid = TRUE;
	guint i;

	source = g_settings_schema_source_get_default();
	if (source == NULL) {
		return FALSE;
	}

	profiles_schema = g_settings_schema_source_lookup(source,
	                                                   "org.gnome.Terminal.ProfilesList",
	                                                   TRUE);
	profile_schema = g_settings_schema_source_lookup(source,
	                                                  "org.gnome.Terminal.Legacy.Profile",
	                                                  TRUE);
	if (profiles_schema == NULL || profile_schema == NULL) {
		if (profiles_schema != NULL) {
			g_settings_schema_unref(profiles_schema);
		}
		if (profile_schema != NULL) {
			g_settings_schema_unref(profile_schema);
		}
		return FALSE;
	}

	profiles = g_settings_new_full(profiles_schema, NULL, NULL);
	uuid = g_settings_get_string(profiles, "default");
	if (uuid == NULL || uuid[0] == '\0') {
		g_free(uuid);
		g_object_unref(profiles);
		g_settings_schema_unref(profiles_schema);
		g_settings_schema_unref(profile_schema);
		return FALSE;
	}

	profile_path = g_strdup_printf("/org/gnome/terminal/legacy/profiles:/:%s/", uuid);
	profile = g_settings_new_full(profile_schema, NULL, profile_path);

	value = g_settings_get_string(profile, "foreground-color");
	valid = value != NULL && gdk_rgba_parse(&sakura.system_foreground, value);
	g_free(value);

	use_theme_colors = g_settings_get_boolean(profile, "use-theme-colors");
	value = g_settings_get_string(profile, "background-color");
	if (use_theme_colors) {
		style_context = gtk_style_context_new();
		gtk_style_context_set_screen(style_context, gdk_screen_get_default());
		if (gtk_style_context_lookup_color(style_context, "wm_bg", &theme_background)) {
			sakura.system_background = theme_background;
		} else {
			valid = value != NULL && gdk_rgba_parse(&sakura.system_background, value) && valid;
		}
		g_object_unref(style_context);
	} else {
		valid = value != NULL && gdk_rgba_parse(&sakura.system_background, value) && valid;
	}
	g_free(value);

	palette = g_settings_get_strv(profile, "palette");
	if (palette == NULL || g_strv_length(palette) < PALETTE_SIZE) {
		valid = FALSE;
	} else {
		for (i = 0; i < PALETTE_SIZE; i++) {
			if (!gdk_rgba_parse(&sakura.system_palette[i], palette[i])) {
				valid = FALSE;
				break;
			}
		}
	}
	g_strfreev(palette);

	cursor_colors_set = g_settings_get_boolean(profile, "cursor-colors-set");
	if (cursor_colors_set) {
		value = g_settings_get_string(profile, "cursor-background-color");
		valid = value != NULL && gdk_rgba_parse(&sakura.system_cursor, value) && valid;
		g_free(value);
	} else {
		sakura.system_cursor = sakura.system_foreground;
	}
	sakura.system_bold_is_bright = g_settings_get_boolean(profile, "bold-is-bright");

	g_free(profile_path);
	g_free(uuid);
	g_object_unref(profile);
	g_object_unref(profiles);
	g_settings_schema_unref(profile_schema);
	g_settings_schema_unref(profiles_schema);

	return valid;
}

static void
sakura_init()
{
	GError *gerror=NULL;
	char* configdir = NULL;
	int i;

	term_data_id = g_quark_from_static_string("sakura_term");

	/*** Config file initialization ***/

	sakura.cfg = g_key_file_new();
	sakura.config_modified=false;

	configdir = g_build_filename( g_get_user_config_dir(), "sakura", NULL);
	if (!g_file_test(g_get_user_config_dir(), G_FILE_TEST_EXISTS))
		g_mkdir(g_get_user_config_dir(), 0755 );
	if (!g_file_test(configdir, G_FILE_TEST_EXISTS))
		g_mkdir( configdir, 0755);
	if (option_config_file) { /* Don't force the config path for user conf file */
		sakura.configfile = option_config_file;
	} else {
		sakura.configfile = g_build_filename(configdir, DEFAULT_CONFIGFILE, NULL);
	}
	g_free(configdir);

	/* Open config file */
	if (!g_key_file_load_from_file(sakura.cfg, sakura.configfile, 0, &gerror)) {
		/* If there's no file, ignore the error. A new one is created */
		if (gerror->code==G_KEY_FILE_ERROR_UNKNOWN_ENCODING || gerror->code==G_KEY_FILE_ERROR_INVALID_VALUE) {
			g_error_free(gerror);
			fprintf(stderr, "Not valid config file format\n");
			exit(EXIT_FAILURE);
		}
	}

	/* Add GFile monitor to control file external changes */
	GFile *cfgfile = g_file_new_for_path(sakura.configfile);
	GFileMonitor *mon_cfgfile = g_file_monitor_file (cfgfile, 0, NULL, NULL);
	g_signal_connect(G_OBJECT(mon_cfgfile), "changed", G_CALLBACK(sakura_conf_changed_cb), NULL);

	gchar *cfgtmp = NULL;

	/* We can safely ignore errors from g_key_file_get_value(), since if the
	 * call to g_key_file_has_key() was successful, the key IS there. From the
	 * glib docs I don't know if we can ignore errors from g_key_file_has_key,
	 * too. I think we can: the only possible error is that the config file
	 * doesn't exist, but we have just read it!
	 */

	sakura.have_system_colors = sakura_load_gnome_terminal_colors();

	for (i=0; i<NUM_COLORSETS; i++) {
		char temp_name[20];

		sprintf(temp_name, "colorset%d_fore", i+1);
		if (g_key_file_has_key(sakura.cfg, cfg_group, temp_name, NULL)) {
			cfgtmp = g_key_file_get_value(sakura.cfg, cfg_group, temp_name, NULL);
			gdk_rgba_parse(&sakura.forecolors[i], cfgtmp);
			g_free(cfgtmp);
		} else if (sakura.have_system_colors) {
			sakura.forecolors[i] = sakura.system_foreground;
		} else {
			sakura_set_config_string(temp_name, DEFAULT_FOREGROUND_COLOR);
			gdk_rgba_parse(&sakura.forecolors[i], DEFAULT_FOREGROUND_COLOR);
		}

		sprintf(temp_name, "colorset%d_back", i+1);
		if (g_key_file_has_key(sakura.cfg, cfg_group, temp_name, NULL)) {
			cfgtmp = g_key_file_get_value(sakura.cfg, cfg_group, temp_name, NULL);
			gdk_rgba_parse(&sakura.backcolors[i], cfgtmp);
			g_free(cfgtmp);
		} else if (sakura.have_system_colors) {
			sakura.backcolors[i] = sakura.system_background;
		} else {
			sakura_set_config_string(temp_name, DEFAULT_BACKGROUND_COLOR);
			gdk_rgba_parse(&sakura.backcolors[i], DEFAULT_BACKGROUND_COLOR);
		}

		sprintf(temp_name, "colorset%d_curs", i+1);
		if (g_key_file_has_key(sakura.cfg, cfg_group, temp_name, NULL)) {
			cfgtmp = g_key_file_get_value(sakura.cfg, cfg_group, temp_name, NULL);
			gdk_rgba_parse(&sakura.curscolors[i], cfgtmp);
			g_free(cfgtmp);
		} else if (sakura.have_system_colors) {
			sakura.curscolors[i] = sakura.system_cursor;
		} else {
			sakura_set_config_string(temp_name, DEFAULT_CURSOR_COLOR);
			gdk_rgba_parse(&sakura.curscolors[i], DEFAULT_CURSOR_COLOR);
		}

		sprintf(temp_name, "colorset%d_scheme", i+1);
		if (!g_key_file_has_key(sakura.cfg, cfg_group, temp_name, NULL)) {
			sakura_set_config_integer(temp_name, DEFAULT_SCHEME);
		}
		sakura.schemes[i] = g_key_file_get_integer(sakura.cfg, cfg_group, temp_name, NULL);

		sprintf(temp_name, "colorset%d_key", i+1);
		if (!g_key_file_has_key(sakura.cfg, cfg_group, temp_name, NULL)) {
			sakura_set_keybind(temp_name, cs_keys[i]);
		}
		sakura.set_colorset_keys[i]= sakura_get_keybind(temp_name);
	}

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "last_colorset", NULL)) {
		sakura_set_config_integer("last_colorset", 1);
	}
	sakura.last_colorset = g_key_file_get_integer(sakura.cfg, cfg_group, "last_colorset", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "bold_is_bright", NULL)) {
		if (sakura.have_system_colors) {
			sakura.bold_is_bright = sakura.system_bold_is_bright;
		} else {
			sakura_set_config_boolean("bold_is_bright", TRUE);
			sakura.bold_is_bright = TRUE;
		}
	} else {
		sakura.bold_is_bright = g_key_file_get_boolean(sakura.cfg, cfg_group, "bold_is_bright", NULL);
	}

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "scroll_lines", NULL)) {
		g_key_file_set_integer(sakura.cfg, cfg_group, "scroll_lines", DEFAULT_SCROLL_LINES);
	}
	sakura.scroll_lines = g_key_file_get_integer(sakura.cfg, cfg_group, "scroll_lines", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "line_height", NULL)) {
		sakura_set_config_double("line_height", DEFAULT_LINE_HEIGHT);
	}
	sakura.line_height = g_key_file_get_double(sakura.cfg, cfg_group, "line_height", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "font", NULL)) {
		gchar *default_font = sakura_get_default_font();
		sakura_set_config_string("font", default_font);
		g_free(default_font);
	}
	cfgtmp = g_key_file_get_value(sakura.cfg, cfg_group, "font", NULL);
	sakura.font = pango_font_description_from_string(cfgtmp);
	g_free(cfgtmp);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "show_tab_bar", NULL)) {
		/* legacy option "show_always_first_tab" now sets "show_tab_bar = always | multiple" */
		if (g_key_file_has_key(sakura.cfg, cfg_group, "show_always_first_tab", NULL)) {
			cfgtmp = g_key_file_get_value(sakura.cfg, cfg_group, "show_always_first_tab", NULL);
			sakura_set_config_string("show_tab_bar", (strcmp(cfgtmp, "Yes")==0) ? "always" : "multiple");
			g_free(cfgtmp);
		} else {
			sakura_set_config_string("show_tab_bar", "multiple");
		}
	}
	cfgtmp = g_key_file_get_value(sakura.cfg, cfg_group, "show_tab_bar", NULL);
	if (strcmp(cfgtmp, "always")==0) {
		sakura.show_tab_bar = SHOW_TAB_BAR_ALWAYS;
	} else if (strcmp(cfgtmp, "multiple")==0) {
		sakura.show_tab_bar = SHOW_TAB_BAR_MULTIPLE;
	} else if (strcmp(cfgtmp, "never")==0) {
		sakura.show_tab_bar = SHOW_TAB_BAR_NEVER;
	} else {
		fprintf(stderr, "Invalid configuration value: show_tab_bar=%s (valid values: always|multiple|never)\n", cfgtmp);
		sakura.show_tab_bar = SHOW_TAB_BAR_MULTIPLE;
	}
	g_free(cfgtmp);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "sidebar_visible", NULL)) {
		sakura_set_config_boolean("sidebar_visible", TRUE);
	}
	sakura.sidebar_visible = g_key_file_get_boolean(sakura.cfg, cfg_group, "sidebar_visible", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "sidebar_width", NULL)) {
		sakura_set_config_integer("sidebar_width", DEFAULT_SIDEBAR_WIDTH);
	}
	sakura.sidebar_width = g_key_file_get_integer(sakura.cfg, cfg_group, "sidebar_width", NULL);
	if (sakura.sidebar_width < 160 || sakura.sidebar_width > 500)
		sakura.sidebar_width = DEFAULT_SIDEBAR_WIDTH;

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "scrollbar", NULL)) {
		sakura_set_config_boolean("scrollbar", FALSE);
	}
	sakura.show_scrollbar = g_key_file_get_boolean(sakura.cfg, cfg_group, "scrollbar", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "closebutton", NULL)) {
		sakura_set_config_boolean("closebutton", TRUE);
	}
	sakura.show_closebutton = g_key_file_get_boolean(sakura.cfg, cfg_group, "closebutton", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "new_tab_after_current", NULL)) {
		sakura_set_config_boolean("new_tab_after_current", TRUE);
	}
	sakura.new_tab_after_current = g_key_file_get_boolean(sakura.cfg, cfg_group, "new_tab_after_current", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "tabs_on_bottom", NULL)) {
		sakura_set_config_boolean("tabs_on_bottom", FALSE);
	}
	sakura.tabs_on_bottom = g_key_file_get_boolean(sakura.cfg, cfg_group, "tabs_on_bottom", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "less_questions", NULL)) {
		sakura_set_config_boolean("less_questions", FALSE);
	}
	sakura.less_questions = g_key_file_get_boolean(sakura.cfg, cfg_group, "less_questions", NULL);

        if (!g_key_file_has_key(sakura.cfg, cfg_group, "copy_on_select", NULL)) {
                sakura_set_config_boolean("copy_on_select", FALSE);
        }
        sakura.copy_on_select = g_key_file_get_boolean(sakura.cfg, cfg_group, "copy_on_select", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "disable_numbered_tabswitch", NULL)) {
		sakura_set_config_boolean("disable_numbered_tabswitch", FALSE);
	}
	sakura.disable_numbered_tabswitch = g_key_file_get_boolean(sakura.cfg, cfg_group, "disable_numbered_tabswitch", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "use_fading", NULL)) {
		sakura_set_config_boolean("use_fading", FALSE);
	}
	sakura.use_fading = g_key_file_get_boolean(sakura.cfg, cfg_group, "use_fading", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "scrollable_tabs", NULL)) {
		sakura_set_config_boolean("scrollable_tabs", TRUE);
	}
	sakura.scrollable_tabs = g_key_file_get_boolean(sakura.cfg, cfg_group, "scrollable_tabs", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "urgent_bell", NULL)) {
		sakura_set_config_string("urgent_bell", "Yes");
	}
	cfgtmp = g_key_file_get_value(sakura.cfg, cfg_group, "urgent_bell", NULL);
	sakura.urgent_bell= (strcmp(cfgtmp, "Yes")==0) ? 1 : 0;
	g_free(cfgtmp);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "audible_bell", NULL)) {
		sakura_set_config_string("audible_bell", "Yes");
	}
	cfgtmp = g_key_file_get_value(sakura.cfg, cfg_group, "audible_bell", NULL);
	sakura.audible_bell= (strcmp(cfgtmp, "Yes")==0) ? 1 : 0;
	g_free(cfgtmp);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "blinking_cursor", NULL)) {
		sakura_set_config_string("blinking_cursor", "No");
	}
	cfgtmp = g_key_file_get_value(sakura.cfg, cfg_group, "blinking_cursor", NULL);
	sakura.blinking_cursor= (strcmp(cfgtmp, "Yes")==0) ? 1 : 0;
	g_free(cfgtmp);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "cursor_type", NULL)) {
		sakura_set_config_string("cursor_type", "VTE_CURSOR_SHAPE_BLOCK");
	}
	sakura.cursor_type = g_key_file_get_integer(sakura.cfg, cfg_group, "cursor_type", NULL);

	/* Only in config file */
	if (!g_key_file_has_key(sakura.cfg, cfg_group, "word_chars", NULL)) {
		sakura_set_config_string("word_chars", DEFAULT_WORD_CHARS);
	}
	sakura.word_chars = g_key_file_get_value(sakura.cfg, cfg_group, "word_chars", NULL);

	if (g_key_file_has_key(sakura.cfg, cfg_group, "palette", NULL)) {
		gerror=NULL;
		sakura.palette_idx = g_key_file_get_integer(sakura.cfg, cfg_group, "palette", &gerror);
		/* Backwards compatibility after changing (v.3.7.1) "palette" type from string to int. */
		if (gerror && gerror->code == G_KEY_FILE_ERROR_INVALID_VALUE) {
			sakura.palette_idx = DEFAULT_PALETTE;
			g_error_free(gerror);
		}
	} else if (sakura.have_system_colors) {
		sakura.palette_idx = SYSTEM_PALETTE_INDEX;
	} else {
		sakura.palette_idx = DEFAULT_PALETTE;
		sakura_set_config_integer("palette", DEFAULT_PALETTE);
	}

	if (sakura.palette_idx == SYSTEM_PALETTE_INDEX && sakura.have_system_colors) {
		sakura.palette = sakura.system_palette;
	} else if (sakura.palette_idx < SYSTEM_PALETTE_INDEX) {
		sakura.palette = palettes[sakura.palette_idx];
	} else {
		sakura.palette_idx = DEFAULT_PALETTE;
		sakura.palette = palettes[DEFAULT_PALETTE];
	}

	/* Keybindings are only in the config file */
	if (!g_key_file_has_key(sakura.cfg, cfg_group, "add_tab_accelerator", NULL)) {
		sakura_set_config_integer("add_tab_accelerator", DEFAULT_ADD_TAB_ACCELERATOR);
	}
	sakura.add_tab_accelerator = g_key_file_get_integer(sakura.cfg, cfg_group, "add_tab_accelerator", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "del_tab_accelerator", NULL)) {
		sakura_set_config_integer("del_tab_accelerator", DEFAULT_DEL_TAB_ACCELERATOR);
	}
	sakura.del_tab_accelerator = g_key_file_get_integer(sakura.cfg, cfg_group, "del_tab_accelerator", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "switch_tab_accelerator", NULL)) {
		sakura_set_config_integer("switch_tab_accelerator", DEFAULT_SWITCH_TAB_ACCELERATOR);
	}
	sakura.switch_tab_accelerator = g_key_file_get_integer(sakura.cfg, cfg_group, "switch_tab_accelerator", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "move_tab_accelerator", NULL)) {
		sakura_set_config_integer("move_tab_accelerator", DEFAULT_MOVE_TAB_ACCELERATOR);
	}
	sakura.move_tab_accelerator = g_key_file_get_integer(sakura.cfg, cfg_group, "move_tab_accelerator", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "copy_accelerator", NULL)) {
		sakura_set_config_integer("copy_accelerator", DEFAULT_COPY_ACCELERATOR);
	}
	sakura.copy_accelerator = g_key_file_get_integer(sakura.cfg, cfg_group, "copy_accelerator", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "scrollbar_accelerator", NULL)) {
		sakura_set_config_integer("scrollbar_accelerator", DEFAULT_SCROLLBAR_ACCELERATOR);
	}
	sakura.scrollbar_accelerator = g_key_file_get_integer(sakura.cfg, cfg_group, "scrollbar_accelerator", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "open_url_accelerator", NULL)) {
		sakura_set_config_integer("open_url_accelerator", DEFAULT_OPEN_URL_ACCELERATOR);
	}
	sakura.open_url_accelerator = g_key_file_get_integer(sakura.cfg, cfg_group, "open_url_accelerator", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "font_size_accelerator", NULL)) {
		sakura_set_config_integer("font_size_accelerator", DEFAULT_FONT_SIZE_ACCELERATOR);
	}
	sakura.font_size_accelerator = g_key_file_get_integer(sakura.cfg, cfg_group, "font_size_accelerator", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "set_tab_name_accelerator", NULL)) {
		sakura_set_config_integer("set_tab_name_accelerator", DEFAULT_SET_TAB_NAME_ACCELERATOR);
	}
	sakura.set_tab_name_accelerator = g_key_file_get_integer(sakura.cfg, cfg_group, "set_tab_name_accelerator", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "search_accelerator", NULL)) {
		sakura_set_config_integer("search_accelerator", DEFAULT_SEARCH_ACCELERATOR);
	}
	sakura.search_accelerator = g_key_file_get_integer(sakura.cfg, cfg_group, "search_accelerator", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "new_window_accelerator", NULL)) {
		sakura_set_config_integer("new_window_accelerator", DEFAULT_NEW_WINDOW_ACCELERATOR);
	}
	sakura.new_window_accelerator = g_key_file_get_integer(sakura.cfg, cfg_group, "new_window_accelerator", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "add_tab_key", NULL)) {
		sakura_set_keybind("add_tab_key", DEFAULT_ADD_TAB_KEY);
	}
	sakura.add_tab_key = sakura_get_keybind("add_tab_key");

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "del_tab_key", NULL)) {
		sakura_set_keybind("del_tab_key", DEFAULT_DEL_TAB_KEY);
	}
	sakura.del_tab_key = sakura_get_keybind("del_tab_key");

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "prev_tab_key", NULL)) {
		sakura_set_keybind("prev_tab_key", DEFAULT_PREV_TAB_KEY);
	}
	sakura.prev_tab_key = sakura_get_keybind("prev_tab_key");

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "next_tab_key", NULL)) {
		sakura_set_keybind("next_tab_key", DEFAULT_NEXT_TAB_KEY);
	}
	sakura.next_tab_key = sakura_get_keybind("next_tab_key");

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "copy_key", NULL)) {
		sakura_set_keybind( "copy_key", DEFAULT_COPY_KEY);
	}
	sakura.copy_key = sakura_get_keybind("copy_key");

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "paste_key", NULL)) {
		sakura_set_keybind("paste_key", DEFAULT_PASTE_KEY);
	}
	sakura.paste_key = sakura_get_keybind("paste_key");

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "scrollbar_key", NULL)) {
		sakura_set_keybind("scrollbar_key", DEFAULT_SCROLLBAR_KEY);
	}
	sakura.scrollbar_key = sakura_get_keybind("scrollbar_key");

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "set_tab_name_key", NULL)) {
		sakura_set_keybind("set_tab_name_key", DEFAULT_SET_TAB_NAME_KEY);
	}
	sakura.set_tab_name_key = sakura_get_keybind("set_tab_name_key");

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "search_key", NULL)) {
		sakura_set_keybind("search_key", DEFAULT_SEARCH_KEY);
	}
	sakura.search_key = sakura_get_keybind("search_key");

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "increase_font_size_key", NULL)) {
		sakura_set_keybind("increase_font_size_key", DEFAULT_INCREASE_FONT_SIZE_KEY);
	}
	sakura.increase_font_size_key = sakura_get_keybind("increase_font_size_key");

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "decrease_font_size_key", NULL)) {
		sakura_set_keybind("decrease_font_size_key", DEFAULT_DECREASE_FONT_SIZE_KEY);
	}
	sakura.decrease_font_size_key = sakura_get_keybind("decrease_font_size_key");

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "fullscreen_key", NULL)) {
		sakura_set_keybind("fullscreen_key", DEFAULT_FULLSCREEN_KEY);
	}
	sakura.fullscreen_key = sakura_get_keybind("fullscreen_key");

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "new_window_key", NULL)) {
		sakura_set_keybind("new_window_key", DEFAULT_NEW_WINDOW_KEY);
	}
	sakura.new_window_key = sakura_get_keybind("new_window_key");

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "set_colorset_accelerator", NULL)) {
		sakura_set_config_integer("set_colorset_accelerator", DEFAULT_SELECT_COLORSET_ACCELERATOR);
	}
	sakura.set_colorset_accelerator = g_key_file_get_integer(sakura.cfg, cfg_group, "set_colorset_accelerator", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "icon_file", NULL)) {
		sakura_set_config_string("icon_file", ICON_FILE);
	}
	sakura.icon = g_key_file_get_string(sakura.cfg, cfg_group, "icon_file", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "paste_button", NULL)) {
		sakura_set_config_integer("paste_button", DEFAULT_PASTE_BUTTON);
	}
	sakura.paste_button = g_key_file_get_integer(sakura.cfg, cfg_group, "paste_button", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "menu_button", NULL)) {
		sakura_set_config_integer("menu_button", DEFAULT_MENU_BUTTON);
	}
	sakura.menu_button = g_key_file_get_integer(sakura.cfg, cfg_group, "menu_button", NULL);

	/* NULL if not found. Don't add a new one */ /* Only in config file */
	sakura.tab_default_title = g_key_file_get_string(sakura.cfg, cfg_group, "tab_default_title", NULL);

	sakura.dont_save = g_key_file_get_boolean(sakura.cfg, cfg_group, "dont_save", NULL);

	/* Default terminal size */
	if (!g_key_file_has_key(sakura.cfg, cfg_group, "window_columns", NULL)) {
		sakura_set_config_integer("window_columns", DEFAULT_COLUMNS);
	}
	sakura.columns = g_key_file_get_integer(sakura.cfg, cfg_group, "window_columns", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "window_rows", NULL)) {
		sakura_set_config_integer("window_rows", DEFAULT_ROWS);
	}
	sakura.rows = g_key_file_get_integer(sakura.cfg, cfg_group, "window_rows", NULL);

	/* Optional only, no need to set it if not found */
	sakura.shell_path = g_key_file_get_string(sakura.cfg, cfg_group, "shell_path", NULL);
	
	/* Default terminal. Only in config file */
	sakura.term = g_key_file_get_value(sakura.cfg, cfg_group, "term", NULL);

	if (!sakura.dont_save) {
		sakura.sessionfile = g_strdup_printf("%s.session", sakura.configfile);
		sakura.codex_tracking_dir = g_strdup_printf("%s.codex", sakura.sessionfile);
		sakura.history_dir = g_strdup_printf("%s.history", sakura.sessionfile);
		if (g_mkdir_with_parents(sakura.codex_tracking_dir, 0700) != 0)
			SAY("Could not create Codex tracking directory: %s", g_strerror(errno));
		if (g_mkdir_with_parents(sakura.history_dir, 0700) != 0)
			SAY("Could not create terminal history directory: %s", g_strerror(errno));
		else if (chmod(sakura.history_dir, 0700) != 0)
			SAY("Could not secure terminal history directory: %s", g_strerror(errno));
		sakura_session_load();
		sakura.codex_tracking_source_id = g_timeout_add(500,
		                                                 sakura_codex_tracking_poll_cb,
		                                                 NULL);
	}

	/*** Sakura window initialization ***/

	/* Use the same dark/light preference as GNOME Terminal and GTK header
	 * bars for the main window and dialogs. */
	g_object_set(gtk_settings_get_default(),
	             "gtk-dialogs-use-header", TRUE,
	             "gtk-application-prefer-dark-theme", sakura_prefers_dark_theme(),
	             NULL);

	/* Create our windows */
	sakura.main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(sakura.main_window), "sakura");
	gtk_widget_set_name(sakura.main_window, "sakura");
	sakura.header_bar = gtk_header_bar_new();
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(sakura.header_bar), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(sakura.header_bar), "sakura");
	gtk_window_set_titlebar(GTK_WINDOW(sakura.main_window), sakura.header_bar);
	gtk_widget_show(sakura.header_bar);

	sakura.fade_window = gtk_window_new(GTK_WINDOW_POPUP);
	gtk_widget_set_name(sakura.fade_window, "fade_window");
	gtk_window_set_position(GTK_WINDOW(sakura.fade_window), GTK_WIN_POS_NONE);
	gtk_widget_set_opacity(sakura.fade_window, FADE_WINDOW_OPACITY);
	gtk_window_set_transient_for(GTK_WINDOW(sakura.fade_window), GTK_WINDOW(sakura.main_window));

	/* Add CSS styles for main and fade window*/
	GtkCssProvider *provider = gtk_css_provider_new();
	GdkScreen *screen = gtk_widget_get_screen(GTK_WIDGET(sakura.main_window));
	gtk_css_provider_load_from_data(provider, SAKURA_CSS, -1, NULL);
	gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER (provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(provider);

	provider = gtk_css_provider_new();
	screen = gtk_widget_get_screen(GTK_WIDGET(sakura.fade_window));
	gtk_css_provider_load_from_data(provider, FADE_WINDOW_CSS, -1, NULL);
	gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER (provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(provider);

	/* Create notebook and set style */
	sakura.notebook = gtk_notebook_new();
	gtk_notebook_set_scrollable((GtkNotebook*)sakura.notebook, sakura.scrollable_tabs);
	sakura_register_codex_icon();
	sakura_sidebar_init();

	/* Adding mask, for handle scroll events */
	gtk_widget_add_events(sakura.notebook, GDK_SCROLL_MASK);
	
	/* Figure out if we have rgba capabilities. Without this transparency won't work as expected */
	screen = gtk_widget_get_screen (GTK_WIDGET (sakura.main_window));
	GdkVisual *visual = gdk_screen_get_rgba_visual (screen);
	if (visual != NULL && gdk_screen_is_composited (screen)) {
		gtk_widget_set_visual (GTK_WIDGET (sakura.main_window), visual);
	}
	
	/*** Command line options initialization ***/

	/* Set argv for forked childs. Real argv vector starts at argv[1] because we're
	   using G_SPAWN_FILE_AND_ARGV_ZERO to be able to launch login shells */
	/* If the shell_path has been set in the config file it takes priority over the envvar */
	if (sakura.shell_path != NULL) {
		sakura.argv[0] = g_strdup(sakura.shell_path);
		sakura.argv[1] = g_strdup(sakura.shell_path);
	} else {
		sakura.argv[0] = g_strdup(g_getenv("SHELL"));
		if (option_login) {
			sakura.argv[1] = g_strdup_printf("-%s", g_getenv("SHELL"));
		} else {
			sakura.argv[1] = g_strdup(g_getenv("SHELL"));
		}
	}
	sakura.argv[2]=NULL;

	/* Add datadir path to icon name and set icon */
	gchar *icon_path; gerror=NULL;
	if (option_icon) {
		icon_path = g_strdup_printf("%s", option_icon);
	} else {
		icon_path = g_strdup_printf(DATADIR "/pixmaps/%s", sakura.icon);
	}
	gtk_window_set_icon_from_file(GTK_WINDOW(sakura.main_window), icon_path, &gerror);
	g_free(icon_path); icon_path=NULL;
	if (gerror) {
		g_error_free(gerror);
		gtk_window_set_icon_name(GTK_WINDOW(sakura.main_window), "utilities-terminal");
	}

	/* More options */
	if (option_title) {
		sakura.main_title = g_strdup_printf("%s", option_title);
		sakura_set_window_title(sakura.main_title);
	} else {
		sakura.main_title = NULL;
	}

	if (option_columns) {
		sakura.columns = option_columns;
	}

	if (option_rows) {
		sakura.rows = option_rows;
	}

	if (option_font) {
		sakura.font=pango_font_description_from_string(option_font);
	}

	if (option_colorset && option_colorset>0 && option_colorset <= NUM_COLORSETS) {
		sakura.last_colorset = option_colorset;
	}

	sakura.fullscreen = FALSE;
	if (option_fullscreen) {
		sakura_fullscreen_cb(NULL, NULL); /* FIXME: Move to sakura_set_size?? */
	}
	
	sakura.resized = FALSE;
	sakura.externally_modified = false;
	sakura.first_run=true;

	gerror = NULL;
	sakura.http_vteregexp = vte_regex_new_for_match(HTTP_REGEXP, strlen(HTTP_REGEXP), PCRE2_MULTILINE, &gerror);
	if (!sakura.http_vteregexp) {
		SAY("http_regexp: %s", gerror->message);
		g_error_free(gerror);
	}
	gerror=NULL;
	sakura.mail_vteregexp = vte_regex_new_for_match(MAIL_REGEXP, strlen(MAIL_REGEXP), PCRE2_MULTILINE, &gerror);
	if (!sakura.mail_vteregexp) {
		SAY("mail_regexp: %s", gerror->message);
		g_error_free(gerror);
	}

	gtk_container_add(GTK_CONTAINER(sakura.main_window), sakura.sidebar_paned);

	sakura_init_popup();

	g_signal_connect(G_OBJECT(sakura.main_window), "delete_event", G_CALLBACK(sakura_delete_event_cb), NULL);
	g_signal_connect(G_OBJECT(sakura.main_window), "destroy", G_CALLBACK(sakura_destroy_window_cb), NULL);
	g_signal_connect(G_OBJECT(sakura.main_window), "key-press-event", G_CALLBACK(sakura_key_press_cb), NULL);
	g_signal_connect(G_OBJECT(sakura.main_window), "configure-event", G_CALLBACK(sakura_resized_window_cb), NULL);
	g_signal_connect(G_OBJECT(sakura.main_window), "focus-out-event", G_CALLBACK(sakura_focus_out_cb), NULL);
	g_signal_connect(G_OBJECT(sakura.main_window), "focus-in-event", G_CALLBACK(sakura_focus_in_cb), NULL);
	g_signal_connect(G_OBJECT(sakura.main_window), "show", G_CALLBACK(sakura_show_event_cb), NULL);
}


static void
sakura_init_popup()
{
	GtkWidget *item_new_tab, *item_set_name, *item_close_tab, *item_copy,
	          *item_paste, *item_fullscreen, *item_select_font, *item_select_colors,
	          *item_show_tab_bar, *item_sidebar,
	          *item_show_tab_bar_always, *item_show_tab_bar_multiple, *item_show_tab_bar_never,
	          *item_toggle_scrollbar, *item_options,
	          *item_urgent_bell, *item_audible_bell, *item_blinking_cursor,
	          *item_cursor, *item_cursor_block, *item_cursor_underline, *item_cursor_ibeam,
		  *item_tabs_on_bottom, *item_less_questions, *item_copy_on_select,
	          *item_disable_numbered_tabswitch, *item_new_tab_after_current; // *item_use_fading;
	GtkWidget *options_menu, *show_tab_bar_menu, *cursor_menu;
	GtkWidget *codex_menu, *item_codex, *item_new_codex, *item_resume_codex,
	          *item_attach_codex, *item_refresh_codex_name, *item_codex_status,
	          *item_install_codex;

	sakura.item_open_mail = gtk_menu_item_new_with_label(_("Open mail"));
	sakura.item_open_link = gtk_menu_item_new_with_label(_("Open link"));
	sakura.item_copy_link = gtk_menu_item_new_with_label(_("Copy link"));
	item_new_tab = gtk_menu_item_new_with_label(_("New tab"));
	item_codex = gtk_menu_item_new_with_label(_("Codex"));
	item_new_codex = gtk_menu_item_new_with_label(_("New Codex session"));
	item_resume_codex = gtk_menu_item_new_with_label(_("Resume Codex session..."));
	item_attach_codex = gtk_menu_item_new_with_label(_("Attach current tab..."));
	item_refresh_codex_name = gtk_menu_item_new_with_label(_("Refresh session name"));
	item_codex_status = gtk_menu_item_new_with_label(_("Check session tracking"));
	item_install_codex = gtk_menu_item_new_with_label(_("Enable Codex session tracking"));
	item_set_name = gtk_menu_item_new_with_label(_("Set tab name..."));
	item_close_tab = gtk_menu_item_new_with_label(_("Close tab"));
	item_fullscreen = gtk_menu_item_new_with_label(_("Full screen"));
	item_copy = gtk_menu_item_new_with_label(_("Copy"));
	item_paste = gtk_menu_item_new_with_label(_("Paste"));

	item_options = gtk_menu_item_new_with_label(_("Options"));

	item_select_font = gtk_menu_item_new_with_label(_("Select font..."));
	item_select_colors = gtk_menu_item_new_with_label(_("Select colors..."));
	item_sidebar = gtk_check_menu_item_new_with_label(_("Show terminal sidebar"));
	item_show_tab_bar = gtk_menu_item_new_with_label(_("Show tab bar"));
	item_show_tab_bar_always = gtk_radio_menu_item_new_with_label(NULL, _("Always"));
	item_show_tab_bar_multiple = gtk_radio_menu_item_new_with_label_from_widget(
		GTK_RADIO_MENU_ITEM(item_show_tab_bar_always), _("When there's more than one tab"));
	item_show_tab_bar_never = gtk_radio_menu_item_new_with_label_from_widget(
		GTK_RADIO_MENU_ITEM(item_show_tab_bar_always), _("Never"));
	item_tabs_on_bottom = gtk_check_menu_item_new_with_label(_("Tabs at bottom"));
	item_new_tab_after_current = gtk_check_menu_item_new_with_label(_("New tab after current tab"));
	item_toggle_scrollbar = gtk_check_menu_item_new_with_label(_("Show scrollbar"));
	item_less_questions = gtk_check_menu_item_new_with_label(_("Fewer questions at exit time"));
        item_copy_on_select = gtk_check_menu_item_new_with_label(_("Automatically copy selected text"));
	item_urgent_bell = gtk_check_menu_item_new_with_label(_("Set urgent bell"));
	item_audible_bell = gtk_check_menu_item_new_with_label(_("Set audible bell"));
	item_blinking_cursor = gtk_check_menu_item_new_with_label(_("Set blinking cursor"));
	item_disable_numbered_tabswitch = gtk_check_menu_item_new_with_label(_("Disable numbered tabswitch"));
	//item_use_fading = gtk_check_menu_item_new_with_label(_("Enable focus fade"));
	item_cursor = gtk_menu_item_new_with_label(_("Set cursor type"));
	item_cursor_block = gtk_radio_menu_item_new_with_label(NULL, _("Block"));
	item_cursor_underline = gtk_radio_menu_item_new_with_label_from_widget(GTK_RADIO_MENU_ITEM(item_cursor_block), _("Underline"));
	item_cursor_ibeam = gtk_radio_menu_item_new_with_label_from_widget(GTK_RADIO_MENU_ITEM(item_cursor_block), _("IBeam"));

	/* Show defaults in menu items */
	switch (sakura.show_tab_bar) {
		case SHOW_TAB_BAR_ALWAYS:
			gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_show_tab_bar_always), TRUE);
			break;
		case SHOW_TAB_BAR_MULTIPLE:
			gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_show_tab_bar_multiple), TRUE);
			break;
		case SHOW_TAB_BAR_NEVER:
			gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_show_tab_bar_never), TRUE);
	}

	if (sakura.new_tab_after_current) {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_new_tab_after_current), TRUE);
	} else {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_new_tab_after_current), FALSE);
	}
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_sidebar), sakura.sidebar_visible);

	if (sakura.tabs_on_bottom) {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_tabs_on_bottom), TRUE);
	} else {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_tabs_on_bottom), FALSE);
	}

	if (sakura.less_questions) {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_less_questions), TRUE);
	} else {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_less_questions), FALSE);
	}

        if (sakura.copy_on_select) {
                gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_copy_on_select), TRUE);
        }

	if (sakura.show_scrollbar) {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_toggle_scrollbar), TRUE);
	} else {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_toggle_scrollbar), FALSE);
	}

	if (sakura.disable_numbered_tabswitch) {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_disable_numbered_tabswitch), TRUE);
	} else {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_disable_numbered_tabswitch), FALSE);
	}

	//if (sakura.use_fading) {
	//	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_use_fading), TRUE);
	//} else {
	//	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_use_fading), FALSE);
	//}

	if (sakura.urgent_bell) {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_urgent_bell), TRUE);
	}

	if (sakura.audible_bell) {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_audible_bell), TRUE);
	}

	if (sakura.blinking_cursor) {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_blinking_cursor), TRUE);
	}

	switch (sakura.cursor_type) {
		case VTE_CURSOR_SHAPE_BLOCK:
			gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_cursor_block), TRUE);
			break;
		case VTE_CURSOR_SHAPE_UNDERLINE:
			gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_cursor_underline), TRUE);
			break;
		case VTE_CURSOR_SHAPE_IBEAM:
			gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item_cursor_ibeam), TRUE);
	}

	sakura.open_link_separator = gtk_separator_menu_item_new();

	sakura.menu = gtk_menu_new();

	/* Add items to popup menu */
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), sakura.item_open_mail);
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), sakura.item_open_link);
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), sakura.item_copy_link);
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), sakura.open_link_separator);
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), item_new_tab);
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), item_codex);
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), item_set_name);
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), item_close_tab);
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), gtk_separator_menu_item_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), item_fullscreen);
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), gtk_separator_menu_item_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), item_copy);
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), item_paste);
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), gtk_separator_menu_item_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), item_options);

	options_menu = gtk_menu_new();
	show_tab_bar_menu = gtk_menu_new();
	cursor_menu = gtk_menu_new();
	codex_menu = gtk_menu_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(codex_menu), item_new_codex);
	gtk_menu_shell_append(GTK_MENU_SHELL(codex_menu), item_resume_codex);
	gtk_menu_shell_append(GTK_MENU_SHELL(codex_menu), item_attach_codex);
	gtk_menu_shell_append(GTK_MENU_SHELL(codex_menu), item_refresh_codex_name);
	gtk_menu_shell_append(GTK_MENU_SHELL(codex_menu), item_codex_status);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_codex), codex_menu);

	gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), item_select_colors);
	gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), item_select_font);
	gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), gtk_separator_menu_item_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), item_show_tab_bar);
	gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), item_sidebar);
	gtk_menu_shell_append(GTK_MENU_SHELL(show_tab_bar_menu), item_show_tab_bar_always);
	gtk_menu_shell_append(GTK_MENU_SHELL(show_tab_bar_menu), item_show_tab_bar_multiple);
	gtk_menu_shell_append(GTK_MENU_SHELL(show_tab_bar_menu), item_show_tab_bar_never);
	gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), item_tabs_on_bottom);
	gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), item_new_tab_after_current);
	gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), gtk_separator_menu_item_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), item_toggle_scrollbar);
	gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), item_less_questions);
        gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), item_copy_on_select);
	gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), item_urgent_bell);
	gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), item_audible_bell);
	gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), item_disable_numbered_tabswitch);
	//gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), item_use_fading);
	gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), item_blinking_cursor);
	gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), item_cursor);
	gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), gtk_separator_menu_item_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(options_menu), item_install_codex);
	gtk_menu_shell_append(GTK_MENU_SHELL(cursor_menu), item_cursor_block);
	gtk_menu_shell_append(GTK_MENU_SHELL(cursor_menu), item_cursor_underline);
	gtk_menu_shell_append(GTK_MENU_SHELL(cursor_menu), item_cursor_ibeam);

	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_options), options_menu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_show_tab_bar), show_tab_bar_menu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_cursor), cursor_menu);

	/* ... and finally assign callbacks to menuitems */
	g_signal_connect(G_OBJECT(item_new_tab), "activate", G_CALLBACK(sakura_new_tab_cb), NULL);
	g_signal_connect(G_OBJECT(item_new_codex), "activate", G_CALLBACK(sakura_new_codex_cb), NULL);
	g_signal_connect(G_OBJECT(item_resume_codex), "activate", G_CALLBACK(sakura_resume_codex_cb), NULL);
	g_signal_connect(G_OBJECT(item_attach_codex), "activate", G_CALLBACK(sakura_attach_codex_cb), NULL);
	g_signal_connect(G_OBJECT(item_refresh_codex_name), "activate", G_CALLBACK(sakura_refresh_codex_name_cb), NULL);
	g_signal_connect(G_OBJECT(item_codex_status), "activate", G_CALLBACK(sakura_codex_tracking_status_cb), NULL);
	g_signal_connect(G_OBJECT(item_install_codex), "activate", G_CALLBACK(sakura_install_codex_hook_cb), NULL);
	g_signal_connect(G_OBJECT(item_set_name), "activate", G_CALLBACK(sakura_set_name_dialog_cb), NULL);
	g_signal_connect(G_OBJECT(item_close_tab), "activate", G_CALLBACK(sakura_close_tab_cb), NULL);
	g_signal_connect(G_OBJECT(item_select_font), "activate", G_CALLBACK(sakura_font_dialog_cb), NULL);
	g_signal_connect(G_OBJECT(item_copy), "activate", G_CALLBACK(sakura_copy_cb), NULL);
	g_signal_connect(G_OBJECT(item_paste), "activate", G_CALLBACK(sakura_paste_cb), NULL);
	g_signal_connect(G_OBJECT(item_select_colors), "activate", G_CALLBACK(sakura_color_dialog_cb), NULL);

	g_signal_connect(G_OBJECT(item_show_tab_bar_always), "activate", G_CALLBACK(sakura_show_tab_bar_cb), "always");
	g_signal_connect(G_OBJECT(item_show_tab_bar_multiple), "activate", G_CALLBACK(sakura_show_tab_bar_cb), "multiple");
	g_signal_connect(G_OBJECT(item_show_tab_bar_never), "activate", G_CALLBACK(sakura_show_tab_bar_cb), "never");
	g_signal_connect(G_OBJECT(item_sidebar), "activate", G_CALLBACK(sakura_sidebar_toggle_cb), NULL);
	g_signal_connect(G_OBJECT(item_tabs_on_bottom), "activate", G_CALLBACK(sakura_tabs_on_bottom_cb), NULL);
	g_signal_connect(G_OBJECT(item_less_questions), "activate", G_CALLBACK(sakura_less_questions_cb), NULL);
        g_signal_connect(G_OBJECT(item_copy_on_select), "activate", G_CALLBACK(sakura_copy_on_select_cb), NULL);
        g_signal_connect(G_OBJECT(item_new_tab_after_current), "activate", G_CALLBACK(sakura_new_tab_after_current_cb), NULL);
	g_signal_connect(G_OBJECT(item_toggle_scrollbar), "activate", G_CALLBACK(sakura_show_scrollbar_cb), NULL);
	g_signal_connect(G_OBJECT(item_urgent_bell), "activate", G_CALLBACK(sakura_urgent_bell_cb), NULL);
	g_signal_connect(G_OBJECT(item_audible_bell), "activate", G_CALLBACK(sakura_audible_bell_cb), NULL);
	g_signal_connect(G_OBJECT(item_blinking_cursor), "activate", G_CALLBACK(sakura_blinking_cursor_cb), NULL);
	g_signal_connect(G_OBJECT(item_disable_numbered_tabswitch), "activate", G_CALLBACK(sakura_disable_numbered_tabswitch_cb), NULL);
	//g_signal_connect(G_OBJECT(item_use_fading), "activate", G_CALLBACK(sakura_use_fading_cb), NULL);
	g_signal_connect(G_OBJECT(item_cursor_block), "activate", G_CALLBACK(sakura_set_cursor_cb), "block");
	g_signal_connect(G_OBJECT(item_cursor_underline), "activate", G_CALLBACK(sakura_set_cursor_cb), "underline");
	g_signal_connect(G_OBJECT(item_cursor_ibeam), "activate", G_CALLBACK(sakura_set_cursor_cb), "ibeam");

	g_signal_connect(G_OBJECT(sakura.item_open_mail), "activate", G_CALLBACK(sakura_open_mail_cb), NULL);
	g_signal_connect(G_OBJECT(sakura.item_open_link), "activate", G_CALLBACK(sakura_open_url_cb), NULL);
	g_signal_connect(G_OBJECT(sakura.item_copy_link), "activate", G_CALLBACK(sakura_copy_url_cb), NULL);
	g_signal_connect(G_OBJECT(item_fullscreen), "activate", G_CALLBACK(sakura_fullscreen_cb), NULL);

	gtk_widget_show_all(sakura.menu);
}


static void
sakura_destroy()
{
	GList *group;

	/* A hook can be detected just before the window closes. Process any
	 * pending tracking record and flush the debounced save before tearing down
	 * the notebook, otherwise the freshly learned Codex ID is lost. */
	if (!sakura.session_shutting_down && sakura.sessionfile != NULL &&
	    !option_new_window && !sakura.dont_save) {
		sakura_codex_tracking_poll_cb(NULL);
		if (sakura.session_ready)
			sakura_session_save();
	}
	sakura.session_shutting_down = TRUE;

	if (sakura.session_save_source_id != 0) {
		g_source_remove(sakura.session_save_source_id);
		sakura.session_save_source_id = 0;
	}
	if (sakura.codex_tracking_source_id != 0) {
		g_source_remove(sakura.codex_tracking_source_id);
		sakura.codex_tracking_source_id = 0;
	}

	/* Delete all existing tabs */
	while (gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook)) >= 1) {
		sakura_del_tab(-1);
	}
	for (group = sakura.sidebar_groups; group != NULL; group = group->next)
		sakura_sidebar_free_node(group->data);
	g_list_free(sakura.sidebar_groups);
	sakura.sidebar_groups = NULL;
	sakura.sidebar_root = NULL;

	g_key_file_free(sakura.cfg);
	if (sakura.session_cfg != NULL)
		g_key_file_free(sakura.session_cfg);

	pango_font_description_free(sakura.font);

	free(sakura.configfile);
	g_free(sakura.sessionfile);
	g_free(sakura.codex_tracking_dir);
	g_free(sakura.history_dir);

	gtk_main_quit();
}


static void
sakura_search_dialog ()
{
	GtkWidget *title_dialog, *title_header;
	GtkWidget *entry, *label;
	GtkWidget *title_hbox;
	gint response;

	title_dialog=gtk_dialog_new_with_buttons(_("Search"),
	                                         GTK_WINDOW(sakura.main_window),
	                                         GTK_DIALOG_MODAL|GTK_DIALOG_USE_HEADER_BAR,
	                                         _("_Cancel"), GTK_RESPONSE_CANCEL,
	                                         _("_Apply"), GTK_RESPONSE_ACCEPT,
	                                          NULL);

	/* Configure the new gtk header bar*/
	title_header = gtk_dialog_get_header_bar(GTK_DIALOG(title_dialog));
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(title_header), FALSE);
	gtk_dialog_set_default_response(GTK_DIALOG(title_dialog), GTK_RESPONSE_ACCEPT);

	entry = gtk_entry_new();
	label = gtk_label_new(_("Search"));
	title_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_box_pack_start(GTK_BOX(title_hbox), label, TRUE, TRUE, 12);
	gtk_box_pack_start(GTK_BOX(title_hbox), entry, TRUE, TRUE, 12);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(title_dialog))), title_hbox, FALSE, FALSE, 12);

	/* Disable accept button until some text is entered */
	g_signal_connect(G_OBJECT(entry), "changed", G_CALLBACK(sakura_setname_entry_changed_cb), title_dialog);
	gtk_dialog_set_response_sensitive(GTK_DIALOG(title_dialog), GTK_RESPONSE_ACCEPT, FALSE);

	gtk_widget_show_all(title_hbox);

	response = gtk_dialog_run(GTK_DIALOG(title_dialog));
	if (response == GTK_RESPONSE_ACCEPT) {
		sakura_search(gtk_entry_get_text(GTK_ENTRY(entry)), 0);
	}
	gtk_widget_destroy(title_dialog);
}


void
sakura_search (const char *pattern, bool reverse)
{
	GError *error=NULL;
	VteRegex *regex;
	gint page;
	struct sakura_tab *sk_tab;

	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	sk_tab = sakura_get_sktab(sakura, page);

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


static void
sakura_copy ()
{
	gint page;
	struct sakura_tab *sk_tab;

	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	sk_tab = sakura_get_sktab(sakura, page);

	if (vte_terminal_get_has_selection(VTE_TERMINAL(sk_tab->vte))) {
		vte_terminal_copy_clipboard_format(VTE_TERMINAL(sk_tab->vte), VTE_FORMAT_TEXT);
	}
}


static void
sakura_paste ()
{
	gint page;
	struct sakura_tab *sk_tab;

	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	sk_tab = sakura_get_sktab(sakura, page);

	vte_terminal_paste_clipboard(VTE_TERMINAL(sk_tab->vte));
}


static void
sakura_paste_primary ()
{
	gint page;
	struct sakura_tab *sk_tab;

	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	sk_tab = sakura_get_sktab(sakura, page);

	vte_terminal_paste_primary(VTE_TERMINAL(sk_tab->vte));
}


static void
sakura_show_scrollbar (void)
{
	gint page, n_pages;
	struct sakura_tab *sk_tab;
	int i;

	n_pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));
	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	sk_tab = sakura_get_sktab(sakura, page);

	if (!g_key_file_get_boolean(sakura.cfg, cfg_group, "scrollbar", NULL)) {
		sakura.show_scrollbar = true;
		sakura_set_config_boolean("scrollbar", TRUE);
	} else {
		sakura.show_scrollbar = false;
		sakura_set_config_boolean("scrollbar", FALSE);
	}

	/* Toggle/Untoggle the scrollbar for all tabs */
	for (i = (n_pages - 1); i >= 0; i--) {
		sk_tab = sakura_get_sktab(sakura, i);
		if (!sakura.show_scrollbar)
			gtk_widget_hide(sk_tab->scrollbar);
		else
			gtk_widget_show(sk_tab->scrollbar);
	}
	sakura_set_size();
}


static void
sakura_set_size (void)
{
	struct sakura_tab *sk_tab;
	gint pad_x, pad_y;
	gint char_width, char_height;
	guint npages;
	gint min_width, natural_width;
	gint page;


	sk_tab = sakura_get_sktab(sakura, 0);
	npages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));

	/* Mayhaps an user resize happened. Check if row and columns have changed */
	if (sakura.resized) {
		sakura.columns = vte_terminal_get_column_count(VTE_TERMINAL(sk_tab->vte));
		sakura.rows = vte_terminal_get_row_count(VTE_TERMINAL(sk_tab->vte));
		sakura.resized = FALSE;
	}

	gtk_style_context_get_padding(gtk_widget_get_style_context(sk_tab->vte),
		gtk_widget_get_state_flags(sk_tab->vte),
		&sk_tab->padding);
	pad_x = sk_tab->padding.left + sk_tab->padding.right;
	pad_y = sk_tab->padding.top + sk_tab->padding.bottom;
	//SAY("padding x %d y %d", pad_x, pad_y);
	char_width = vte_terminal_get_char_width(VTE_TERMINAL(sk_tab->vte));
	char_height = vte_terminal_get_char_height(VTE_TERMINAL(sk_tab->vte));
	char_height = (int) (sakura.line_height * char_height);

	sakura.width = pad_x + (char_width * sakura.columns);
	sakura.height = pad_y + (char_height * sakura.rows);

	if (sakura.show_tab_bar == SHOW_TAB_BAR_ALWAYS || (sakura.show_tab_bar == SHOW_TAB_BAR_MULTIPLE && npages > 1)) {

		/* TODO: Yeah i know, this is utter shit. Remove this ugly hack and set geometry hints*/
		if (!sakura.show_scrollbar)
			//sakura.height += min_height - 10;
			sakura.height += 10;
		else
			//sakura.height += min_height - 47;
			sakura.height += 47;

		sakura.width += 8;
		sakura.width += /* (hb*2)+*/ (pad_x*2);
	}

	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	sk_tab = sakura_get_sktab(sakura, page);

	gtk_widget_get_preferred_width(sk_tab->scrollbar, &min_width, &natural_width);
	//SAY("SCROLLBAR min width %d natural width %d", min_width, natural_width);
	if (sakura.show_scrollbar) {
		sakura.width += min_width;
	}

	if (sakura.sidebar_visible)
		sakura.width += sakura.sidebar_width;

	/* Maximize window at init time when command line option is used */
	if (option_maximize && sakura.first_run) {
		gtk_window_maximize(GTK_WINDOW(sakura.main_window));
		gtk_widget_show_all(GTK_WIDGET(sakura.main_window));
		return; /* No need to resize */
	}

	gtk_window_resize(GTK_WINDOW(sakura.main_window), sakura.width, sakura.height);
}


static void
sakura_set_font()
{
	gint n_pages;
	struct sakura_tab *sk_tab;
	int i;

	n_pages=gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));

	/* Set the font for all tabs */
	for (i = (n_pages - 1); i >= 0; i--) {
		sk_tab = sakura_get_sktab(sakura, i);
		vte_terminal_set_font(VTE_TERMINAL(sk_tab->vte), sakura.font);
		vte_terminal_set_cell_height_scale(VTE_TERMINAL(sk_tab->vte), sakura.line_height);
	}
}

/* Set colorset when colosert keybinding is used */
static void
sakura_set_colorset (int cs)
{
	gint page;
	struct sakura_tab *sk_tab;

	if (cs < 0 || cs >= NUM_COLORSETS)
		return;

	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	sk_tab = sakura_get_sktab(sakura, page);
	sk_tab->colorset = cs;

	sakura_set_config_integer("last_colorset", sk_tab->colorset+1);

	sakura_set_colors();
}


/* Set the terminal colors for all notebook tabs */
static void
sakura_set_colors ()
{
	int i;
	int n_pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));
	struct sakura_tab *sk_tab;

	for (i = (n_pages - 1); i >= 0; i--) {
		sk_tab = sakura_get_sktab(sakura, i);

		/* Set fore, back, cursor color and palette for the terminal's colorset */
		vte_terminal_set_colors(VTE_TERMINAL(sk_tab->vte),
		                        &sakura.forecolors[sk_tab->colorset],
		                        &sakura.backcolors[sk_tab->colorset],
		                        sakura.palette, PALETTE_SIZE);
		vte_terminal_set_color_cursor(VTE_TERMINAL(sk_tab->vte), &sakura.curscolors[sk_tab->colorset]);

		/* Use background color to make text visible when the cursor is over it */
		vte_terminal_set_color_cursor_foreground(VTE_TERMINAL(sk_tab->vte), &sakura.backcolors[sk_tab->colorset]);

		vte_terminal_set_bold_is_bright(VTE_TERMINAL(sk_tab->vte), sakura.bold_is_bright);

	}

	/* Main window opacity must be set. Otherwise vte widget will remain opaque */
	gtk_widget_set_opacity(sakura.main_window, sakura.backcolors[sk_tab->colorset].alpha);
}


static void
sakura_move_tab(gint direction)
{
	gint page, n_pages;
	GtkWidget *child;

	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	n_pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));
	child = gtk_notebook_get_nth_page(GTK_NOTEBOOK(sakura.notebook), page);

	if (direction == FORWARD) {
		if (page != n_pages-1)
			gtk_notebook_reorder_child(GTK_NOTEBOOK(sakura.notebook), child, page+1);
	} else {
		if (page != 0)
			gtk_notebook_reorder_child(GTK_NOTEBOOK(sakura.notebook), child, page-1);
	}
}


/* Find the notebook page for the vte terminal passed as a parameter */
static gint
sakura_find_tab(VteTerminal *vte_term)
{
	gint matched_page, page, n_pages;
	struct sakura_tab *sk_tab;

	n_pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));

	matched_page = -1;
	page = 0;

	do {
		sk_tab = sakura_get_sktab(sakura, page);
		if ((VteTerminal *)sk_tab->vte == vte_term) {
			matched_page=page;
		}
		page++;
	} while (page < n_pages);

	return (matched_page);
}


static void
sakura_set_tab_label_text(const gchar *title, gint page)
{
	struct sakura_tab *sk_tab;
	gchar *chopped_title;
	gchar *default_label_text;

	sk_tab = sakura_get_sktab(sakura, page);

	if ((title != NULL) && (g_strcmp0(title, "") != 0)) {
		/* Chop to max size */
		chopped_title = g_strndup(title, TAB_MAX_SIZE);
		/* Honor the minimum tab label size */
		while (strlen(chopped_title)< TAB_MIN_SIZE) {
			char *old_ptr = chopped_title;
			chopped_title = g_strconcat(chopped_title, " ", NULL);
			free(old_ptr);
		}
		gtk_label_set_text(GTK_LABEL(sk_tab->label), chopped_title);
		free(chopped_title);
	} else { /* Use the default values */
		default_label_text = g_strdup_printf(_("Terminal %d"), page);
		gtk_label_set_text(GTK_LABEL(sk_tab->label), default_label_text);
		free(default_label_text);
	}
	sakura_sidebar_update_tab(sk_tab);
}


/* Callback for vte_terminal_spawn_async */
void
sakura_spawn_callback (VteTerminal *vte, GPid pid, GError *error, gpointer user_data)
{
	struct sakura_tab *sk_tab = (struct sakura_tab *) user_data;

	if (pid == -1) { /* Fork has failed */
		SAY("Error: %s", error->message);
	} else {
		sk_tab->pid=pid;
	}
}


static void
sakura_add_tab (void)
{
	sakura_add_tab_with_options(NULL, NULL, NULL, FALSE, SAKURA_TAB_SHELL, NULL, NULL, NULL);
}


static void
sakura_spawn_codex (struct sakura_tab *sk_tab, const gchar *cwd, gchar **env)
{
	gchar *argv[6] = { (gchar *)"codex", (gchar *)"--enable", (gchar *)"hooks",
	                   NULL, NULL, NULL };

	if (sk_tab->codex_session_id != NULL && sk_tab->codex_session_id[0] != '\0') {
		argv[3] = (gchar *)"resume";
		argv[4] = sk_tab->codex_session_id;
	}
	vte_terminal_spawn_async(VTE_TERMINAL(sk_tab->vte), VTE_PTY_NO_HELPER, cwd,
	                         argv, env, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL,
	                         -1, NULL, sakura_spawn_callback, sk_tab);
}


struct sakura_codex_name_query {
	gchar *tracking_token;
	gchar *session_id;
};


static gboolean
sakura_codex_session_id_is_uuid (const gchar *value)
{
	gsize i;

	if (value == NULL || strlen(value) != 36)
		return FALSE;
	for (i = 0; i < 36; i++) {
		if (i == 8 || i == 13 || i == 18 || i == 23) {
			if (value[i] != '-')
				return FALSE;
		} else if (!g_ascii_isxdigit(value[i])) {
			return FALSE;
		}
	}
	return TRUE;
}


static struct sakura_tab *
sakura_find_codex_tab_by_tracking_token (const gchar *tracking_token)
{
	gint page, pages;

	if (tracking_token == NULL || sakura.notebook == NULL)
		return NULL;

	pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));
	for (page = 0; page < pages; page++) {
		struct sakura_tab *sk_tab = sakura_get_sktab(sakura, page);
		if (g_strcmp0(sk_tab->codex_tracking_token, tracking_token) == 0)
			return sk_tab;
	}
	return NULL;
}


static gchar *
sakura_find_codex_name_helper (void)
{
	gchar *helper;

	helper = g_find_program_in_path("sakura-codex-session-name");
	if (helper != NULL)
		return helper;

	helper = g_build_filename(SAKURA_SOURCE_SCRIPT_DIR,
	                           "sakura-codex-session-name", NULL);
	if (g_file_test(helper, G_FILE_TEST_IS_REGULAR))
		return helper;

	g_free(helper);
	return NULL;
}


static void
sakura_codex_name_query_done (GObject *source_object, GAsyncResult *result, gpointer data)
{
	GSubprocess *process = G_SUBPROCESS(source_object);
	struct sakura_codex_name_query *query = data;
	struct sakura_tab *sk_tab;
	gchar *standard_output = NULL, *standard_error = NULL, *name = NULL;
	GError *error = NULL;
	gboolean completed;

	completed = g_subprocess_communicate_utf8_finish(process, result,
	                                                 &standard_output,
	                                                 &standard_error, &error);
	sk_tab = sakura_find_codex_tab_by_tracking_token(query->tracking_token);
	if (sk_tab != NULL)
		sk_tab->codex_name_query_active = FALSE;

	if (completed && g_subprocess_get_successful(process) && sk_tab != NULL &&
	    !sakura.session_shutting_down &&
	    g_strcmp0(sk_tab->codex_session_id, query->session_id) == 0) {
		name = g_strdup(standard_output != NULL ? standard_output : "");
		g_strstrip(name);
		if (g_strcmp0(sk_tab->codex_session_name, name) != 0) {
			g_free(sk_tab->codex_session_name);
			sk_tab->codex_session_name = name[0] != '\0' ? g_strdup(name) : NULL;
			if (!sk_tab->label_set_byuser)
				sakura_sidebar_update_tab(sk_tab);
			else
				sakura_session_schedule_save();
		}
	} else if (error != NULL) {
		SAY("Could not read Codex session name: %s", error->message);
	}

	if (sk_tab != NULL && sk_tab->kind == SAKURA_TAB_CODEX &&
	    g_strcmp0(sk_tab->codex_session_id, query->session_id) != 0)
		sakura_codex_sync_name(sk_tab);

	g_clear_error(&error);
	g_free(name);
	g_free(standard_output);
	g_free(standard_error);
	g_free(query->tracking_token);
	g_free(query->session_id);
	g_free(query);
	g_object_unref(process);
}


static void
sakura_codex_sync_name (struct sakura_tab *sk_tab)
{
	GSubprocess *process;
	GError *error = NULL;
	struct sakura_codex_name_query *query;
	gchar *helper;
	const gchar *argv[4];

	if (sk_tab == NULL || sk_tab->kind != SAKURA_TAB_CODEX ||
	    sk_tab->codex_session_id == NULL || sk_tab->codex_session_id[0] == '\0' ||
	    sk_tab->codex_name_query_active || sakura.session_shutting_down)
		return;

	helper = sakura_find_codex_name_helper();
	if (helper == NULL)
		return;

	argv[0] = helper;
	argv[1] = "--name";
	argv[2] = sk_tab->codex_session_id;
	argv[3] = NULL;
	process = g_subprocess_newv(argv,
	                            G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
	                            &error);
	g_free(helper);
	if (process == NULL) {
		SAY("Could not start Codex session name helper: %s", error->message);
		g_error_free(error);
		return;
	}

	query = g_new0(struct sakura_codex_name_query, 1);
	query->tracking_token = g_strdup(sk_tab->codex_tracking_token);
	query->session_id = g_strdup(sk_tab->codex_session_id);
	sk_tab->codex_name_query_active = TRUE;
	g_subprocess_communicate_utf8_async(process, NULL, NULL,
	                                   sakura_codex_name_query_done, query);
}


static void
sakura_add_tab_with_options (const gchar *restore_cwd,
                              struct sakura_sidebar_node *restore_parent,
                              const gchar *restore_title,
                              gboolean restore_title_set,
                              SakuraTabKind restore_kind,
                              const gchar *restore_codex_session_id,
                              const gchar *restore_codex_session_name,
                              const gchar *restore_terminal_id)
{
	struct sakura_tab *sk_tab;
	GtkWidget *tab_title_hbox; GtkWidget *close_button; /* We could put them inside struct sakura_tab, but it is not necessary */
	GtkWidget *event_box;
	gint index, page, npages;
	gchar *cwd = NULL; gchar *default_label_text = NULL;
	struct sakura_sidebar_node *sidebar_parent;

	sk_tab = g_new0(struct sakura_tab, 1);
	sk_tab->terminal_id = sakura_terminal_id_is_valid(restore_terminal_id)
	                    ? g_strdup(restore_terminal_id)
	                    : sakura_generate_terminal_id();
	sk_tab->kind = restore_kind;
	sk_tab->codex_session_id = g_strdup(restore_codex_session_id);
	sk_tab->codex_session_name = g_strdup(restore_codex_session_name);
	if (sk_tab->codex_session_name == NULL &&
	    restore_kind == SAKURA_TAB_CODEX &&
	    restore_codex_session_id != NULL &&
	    !sakura_codex_session_id_is_uuid(restore_codex_session_id))
		sk_tab->codex_session_name = g_strdup(restore_codex_session_id);
	sk_tab->codex_tracking_token = g_strdup_printf("%d-%u", (int)getpid(),
	                                              g_random_int());
	sidebar_parent = restore_parent != NULL ? restore_parent : sakura_sidebar_selected_group();

	/* Create the tab label */
	sk_tab->label = gtk_label_new(NULL);
	gtk_label_set_ellipsize(GTK_LABEL(sk_tab->label), PANGO_ELLIPSIZE_END);

	/* Create hbox for our label & button */
	tab_title_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	gtk_widget_set_hexpand(tab_title_hbox, TRUE);

	/* Label widgets has no window associated, so we need an event box to catch click events */
	event_box = gtk_event_box_new();
	gtk_container_add(GTK_CONTAINER(event_box), sk_tab->label);
	gtk_widget_set_events(event_box, GDK_BUTTON_PRESS_MASK);

	/* Expand&fill the event_box to get click events all along the tab */
	gtk_box_pack_start(GTK_BOX(tab_title_hbox), event_box, TRUE, TRUE, 0);

	/* If the tab close button is enabled, create and add it to the tab */
	if (sakura.show_closebutton) {
		close_button = gtk_button_new();
		/* Adding scroll-event to button, to propagate it to notebook (fix for scroll event when pointer is above the button) */
		gtk_widget_add_events(close_button, GDK_SCROLL_MASK);

		gtk_widget_set_name(close_button, "closebutton");
		gtk_button_set_relief(GTK_BUTTON(close_button), GTK_RELIEF_NONE);

		GtkWidget *image = gtk_image_new_from_icon_name("window-close", GTK_ICON_SIZE_MENU);
		gtk_container_add (GTK_CONTAINER (close_button), image);
		gtk_box_pack_start(GTK_BOX(tab_title_hbox), close_button, FALSE, FALSE, 0);
	}

	if (sakura.tabs_on_bottom) {
		gtk_notebook_set_tab_pos(GTK_NOTEBOOK(sakura.notebook), GTK_POS_BOTTOM);
	}

	gtk_widget_show_all(tab_title_hbox);

	/* Create new vte terminal, scrollbar, and pack it */
	sk_tab->vte = vte_terminal_new();
	sk_tab->scrollbar = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(sk_tab->vte)));
	sk_tab->hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(sk_tab->hbox), sk_tab->vte, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(sk_tab->hbox), sk_tab->scrollbar, FALSE, FALSE, 0);

	sk_tab->colorset = sakura.last_colorset-1;

	/* -1 if there is no pages yet */
	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));

	/* Use the restored directory when reopening a workspace. Otherwise use the
	 * previous terminal (if there is one) cwd and colorset. */
	if (restore_cwd != NULL && restore_cwd[0] != '\0') {
		cwd = g_strdup(restore_cwd);
	} else if (page >= 0) {
		struct sakura_tab *prev_term;
		prev_term = sakura_get_sktab(sakura, page);
		/* If OSC7 method doesn't work, use the old one as fallback */
		if ((cwd = sakura_get_term_cwd_osc7(prev_term)) == NULL) {
			cwd = sakura_get_term_cwd(prev_term);
		}

		sk_tab->colorset = prev_term->colorset;
	}

	if (!cwd)
		cwd = g_get_current_dir();

	if (!sakura.new_tab_after_current) {
		if ((index=gtk_notebook_append_page(GTK_NOTEBOOK(sakura.notebook), sk_tab->hbox, tab_title_hbox))==-1) {
			sakura_error("Cannot create a new tab");
			exit(1);
		}
	} else {
		if ((index=gtk_notebook_insert_page(GTK_NOTEBOOK(sakura.notebook), sk_tab->hbox, tab_title_hbox, page+1))==-1) {
			sakura_error("Cannot create a new tab");
			exit(1);
		}
	}

	gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(sakura.notebook), sk_tab->hbox, TRUE);

	sakura_set_sktab(sakura, index, sk_tab );

	/* vte signals */
	g_signal_connect(G_OBJECT(sk_tab->vte), "bell", G_CALLBACK(sakura_beep_cb), NULL);
	g_signal_connect(G_OBJECT(sk_tab->vte), "increase-font-size", G_CALLBACK(sakura_increase_font_cb), NULL);
	g_signal_connect(G_OBJECT(sk_tab->vte), "decrease-font-size", G_CALLBACK(sakura_decrease_font_cb), NULL);
	sk_tab->exit_handler_id = g_signal_connect(G_OBJECT(sk_tab->vte), "child-exited", G_CALLBACK(sakura_child_exited_cb), NULL);
	g_signal_connect(G_OBJECT(sk_tab->vte), "eof", G_CALLBACK(sakura_eof_cb), NULL);
	g_signal_connect(G_OBJECT(sk_tab->vte), "window-title-changed", G_CALLBACK(sakura_title_changed_cb), NULL);
	g_signal_connect_after(G_OBJECT(sk_tab->vte), "button-press-event", G_CALLBACK(sakura_term_buttonpressed_cb), sakura.menu);
	g_signal_connect_swapped(G_OBJECT(sk_tab->vte), "button-release-event", G_CALLBACK(sakura_term_buttonreleased_cb), sakura.menu);

	/* Label & button signals */
	/* We need the hbox to know which label/button was clicked */
	g_signal_connect(G_OBJECT(event_box), "button_press_event", G_CALLBACK(sakura_label_clicked_cb), sk_tab->hbox);
	if (sakura.show_closebutton) {
		g_signal_connect(G_OBJECT(close_button), "clicked", G_CALLBACK(sakura_closebutton_clicked_cb), sk_tab->hbox);
	}
	sakura_prepare_history_file(sk_tab);

	/* Allow the user to use a different TERM value */
	char *command_env[6];
	guint command_env_length = 0;
	if (sakura.term != NULL) {
		command_env[command_env_length++] = g_strdup_printf ("TERM=%s", sakura.term);
	} else {
		command_env[command_env_length++] = g_strdup_printf ("TERM=xterm-256color");
	}
	if (sakura.history_dir != NULL) {
		gchar *history_file = sakura_history_file_for_tab(sk_tab);
		command_env[command_env_length++] = g_strdup_printf("HISTFILE=%s", history_file);
		command_env[command_env_length++] = g_strdup_printf("SAKURA_HISTORY_FILE=%s", history_file);
		g_free(history_file);
	}
	if (sakura.codex_tracking_dir != NULL) {
		command_env[command_env_length++] = g_strdup_printf("SAKURA_CODEX_TRACKING_DIR=%s",
		                                                     sakura.codex_tracking_dir);
		command_env[command_env_length++] = g_strdup_printf("SAKURA_CODEX_TAB_TOKEN=%s",
		                                                     sk_tab->codex_tracking_token);
	}
	command_env[command_env_length] = NULL;

	/******* First tab **********/
	npages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));
	if (npages == 1) {
		if (sakura.show_tab_bar == SHOW_TAB_BAR_ALWAYS) {
			gtk_notebook_set_show_tabs(GTK_NOTEBOOK(sakura.notebook), TRUE);
		} else {
			gtk_notebook_set_show_tabs(GTK_NOTEBOOK(sakura.notebook), FALSE);
		}

		gtk_notebook_set_show_border(GTK_NOTEBOOK(sakura.notebook), FALSE);

		/* Set geometry hints when the first tab is created */
		GdkGeometry sk_hints;

		sk_hints.base_width = vte_terminal_get_char_width(VTE_TERMINAL(sk_tab->vte));
		sk_hints.base_height = vte_terminal_get_char_height(VTE_TERMINAL(sk_tab->vte));
		sk_hints.min_width = vte_terminal_get_char_width(VTE_TERMINAL(sk_tab->vte)) * DEFAULT_MIN_WIDTH_CHARS;
		sk_hints.min_height = vte_terminal_get_char_height(VTE_TERMINAL(sk_tab->vte)) * DEFAULT_MIN_HEIGHT_CHARS;
		sk_hints.width_inc = vte_terminal_get_char_width(VTE_TERMINAL(sk_tab->vte));
		sk_hints.height_inc = vte_terminal_get_char_height(VTE_TERMINAL(sk_tab->vte));

		gtk_window_set_geometry_hints(GTK_WINDOW(sakura.main_window), GTK_WIDGET (sk_tab->vte), &sk_hints,
		                              GDK_HINT_RESIZE_INC | GDK_HINT_MIN_SIZE | GDK_HINT_BASE_SIZE);

		sakura_set_font();
		sakura_set_colors();
		/* Set size before showing the widgets but after setting the font */
		sakura_set_size();

		/* Notebook signals. Per notebook signals only need to be defined once, so we put them here */
		g_signal_connect(sakura.notebook, "scroll-event", G_CALLBACK(sakura_notebook_scroll_cb), NULL);
		g_signal_connect(G_OBJECT(sakura.notebook), "switch-page", G_CALLBACK(sakura_switch_page_cb), NULL);
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

		int command_argc = 0; char **command_argv = NULL;

		/* Execute command for the fist tab if we have one */
		if (restore_kind == SAKURA_TAB_CODEX) {
			sakura_spawn_codex(sk_tab, cwd, command_env);
			command_argc = 1;
		} else if (option_execute||option_xterm_execute) {
			char *path;

			sakura_build_command(&command_argc, &command_argv);

			/* If the command is valid, run it */
			if (command_argc > 0) {
				path = g_find_program_in_path(command_argv[0]);

				if (!path) {
					sakura_error("%s command not found", command_argv[0]);
					command_argc = 0;
				}
				vte_terminal_spawn_async(VTE_TERMINAL(sk_tab->vte), VTE_PTY_NO_HELPER, NULL, command_argv, command_env,
						       	         G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, -1, NULL, sakura_spawn_callback, sk_tab);

				free(path);
				g_strfreev(command_argv);
			}
		} 

		/* Fork shell if there is no execute option or if the command is not valid */
		if (restore_kind != SAKURA_TAB_CODEX &&
		    ((!option_execute && !option_xterm_execute) || (command_argc==0))) {
			if (option_hold == TRUE) {
				sakura_error("Hold option given without any command");
				option_hold = FALSE;
			}
			vte_terminal_spawn_async(VTE_TERMINAL(sk_tab->vte), VTE_PTY_NO_HELPER, cwd, sakura.argv, command_env,
					        G_SPAWN_SEARCH_PATH|G_SPAWN_FILE_AND_ARGV_ZERO, NULL, NULL, NULL, -1, NULL, sakura_spawn_callback, sk_tab);
		}

	/********** Not the first tab ************/
	} else {
		sakura_set_font();
		sakura_set_colors();
		gtk_widget_show_all(sk_tab->hbox);
		if (!sakura.show_scrollbar) {
			gtk_widget_hide(sk_tab->scrollbar);
		}

		if (npages == 2 && sakura.show_tab_bar != SHOW_TAB_BAR_NEVER) {
			gtk_notebook_set_show_tabs(GTK_NOTEBOOK(sakura.notebook), TRUE);
			sakura_set_size();
		}
		/* Call set_current page after showing the widget: gtk ignores this
		 * function in the window is not visible *sigh*. Gtk documentation
		 * says this is for "historical" reasons. Me arse */
		gtk_notebook_set_current_page(GTK_NOTEBOOK(sakura.notebook), index);

		int command_argc = 0; char **command_argv = NULL;

		/* Execute command (only in the first run) for additional tabs if we have one */
		if (restore_kind == SAKURA_TAB_CODEX) {
			sakura_spawn_codex(sk_tab, cwd, command_env);
			command_argc = 1;
		} else if ((option_execute||option_xterm_execute) && sakura.first_run) {
			char *path;

			sakura_build_command(&command_argc, &command_argv);

			/* If the command is valid, run it */
			if (command_argc > 0) {
				path = g_find_program_in_path(command_argv[0]);

				if (!path) {
					sakura_error("%s command not found", command_argv[0]);
					command_argc = 0;
				}
				vte_terminal_spawn_async(VTE_TERMINAL(sk_tab->vte), VTE_PTY_NO_HELPER, NULL, command_argv, command_env,
						       	         G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, -1, NULL, sakura_spawn_callback, sk_tab);

				free(path);
				g_strfreev(command_argv);
			}
		}

		/* Fork shell if there is no execute option or if the command is not valid */
		if (restore_kind != SAKURA_TAB_CODEX &&
		    ((!option_execute && !option_xterm_execute) || (command_argc==0))) {
			if (option_hold == TRUE) {
				sakura_error("Hold option given without any command");
				option_hold = FALSE;
			}
			vte_terminal_spawn_async(VTE_TERMINAL(sk_tab->vte), VTE_PTY_NO_HELPER, cwd, sakura.argv, command_env,
					        G_SPAWN_SEARCH_PATH|G_SPAWN_FILE_AND_ARGV_ZERO, NULL, NULL, NULL, -1, NULL, sakura_spawn_callback, sk_tab);
		}
	}

	g_free(sk_tab->cwd);
	sk_tab->cwd = g_strdup(cwd);
	sakura_update_tab_metadata(sk_tab,
	                           vte_terminal_get_window_title(VTE_TERMINAL(sk_tab->vte)));
	free(cwd);

	/* Applying the restored title or tab title pattern from config
	 * (https://answers.launchpad.net/sakura/+question/267951) */
	if (restore_title_set) {
		default_label_text = (gchar *)restore_title;
		sk_tab->label_set_byuser = true;
	} else if (sakura.tab_default_title != NULL) {
		default_label_text = sakura.tab_default_title;
		sk_tab->label_set_byuser = true;
	} else {
		sk_tab->label_set_byuser=false;
	}

	/* Set the default title text (NULL is valid) */
	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	sakura_set_tab_label_text(default_label_text, page);
	sakura_sidebar_add_terminal(sk_tab, sidebar_parent);
	if (sk_tab->kind == SAKURA_TAB_CODEX)
		sakura_codex_sync_name(sk_tab);
	sakura_session_schedule_save();
	for (guint env_index = 0; command_env[env_index] != NULL; env_index++)
		g_free(command_env[env_index]);

	/* Init vte terminal */
	vte_terminal_set_scrollback_lines(VTE_TERMINAL(sk_tab->vte), sakura.scroll_lines);
	vte_terminal_match_add_regex(VTE_TERMINAL(sk_tab->vte), sakura.http_vteregexp, PCRE2_CASELESS);
	vte_terminal_match_add_regex(VTE_TERMINAL(sk_tab->vte), sakura.mail_vteregexp, PCRE2_CASELESS);
	vte_terminal_set_mouse_autohide(VTE_TERMINAL(sk_tab->vte), TRUE);
	vte_terminal_set_backspace_binding(VTE_TERMINAL(sk_tab->vte), VTE_ERASE_ASCII_DELETE);
	vte_terminal_set_word_char_exceptions(VTE_TERMINAL(sk_tab->vte), sakura.word_chars);
	vte_terminal_set_audible_bell (VTE_TERMINAL(sk_tab->vte), sakura.audible_bell ? TRUE : FALSE);
	vte_terminal_set_cursor_blink_mode (VTE_TERMINAL(sk_tab->vte), sakura.blinking_cursor ? VTE_CURSOR_BLINK_ON : VTE_CURSOR_BLINK_OFF);
	vte_terminal_set_cursor_shape (VTE_TERMINAL(sk_tab->vte), sakura.cursor_type);

}


/* Do all the work necessary before & after deleting the tab passed as a parameter */
static void
sakura_close_tab (gint page)
{
	gint npages, response; pid_t pgid;
	struct sakura_tab *sk_tab;
	GtkWidget *dialog;

	npages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));
	sk_tab = sakura_get_sktab(sakura, page);

	/* Only write configuration to disk if it's the last tab */
	if (npages == 1) {
		sakura_config_done();
	}

	/* Check if there are running processes for this tab. Use tcgetpgrp to compare to the shell PGID */
	pgid = tcgetpgrp(vte_pty_get_fd(vte_terminal_get_pty(VTE_TERMINAL(sk_tab->vte))));

	if ( (pgid != -1) && (pgid != sk_tab->pid) && (!sakura.less_questions) ) {
		dialog=gtk_message_dialog_new(GTK_WINDOW(sakura.main_window), GTK_DIALOG_MODAL,
                                              GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
                                              _("There is a running process in this terminal.\n\nDo you really want to close it?"));
		response=gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);

		if (response==GTK_RESPONSE_YES)
			sakura_del_tab(page);

	} else /* No processes */
		sakura_del_tab(page);

	/* And destroy sakura if it's the last tab */
	if (npages == 1)
		sakura_destroy();
}


/* Delete the notebook tab passed as a parameter */
static void
sakura_del_tab(gint page)
{
	struct sakura_tab *sk_tab;
	gint npages;

	sk_tab = sakura_get_sktab(sakura, page);
	npages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));

	/* Do the first tab checks BEFORE deleting the tab, to ensure correct
	 * sizes are calculated when the tab is deleted */
	if (npages == 2) {
		if (sakura.show_tab_bar == SHOW_TAB_BAR_ALWAYS) {
			gtk_notebook_set_show_tabs(GTK_NOTEBOOK(sakura.notebook), TRUE);
		} else {
			gtk_notebook_set_show_tabs(GTK_NOTEBOOK(sakura.notebook), FALSE);
		}
	}

	sakura_sidebar_remove_tab(sk_tab);
	sakura_remove_history_file(sk_tab);
	gtk_widget_hide(sk_tab->hbox);
	g_signal_handler_disconnect (sk_tab->vte, sk_tab->exit_handler_id);
	g_free(sk_tab->cwd);
	g_free(sk_tab->host);
	g_free(sk_tab->raw_title);
	g_free(sk_tab->terminal_id);
	g_free(sk_tab->codex_session_id);
	g_free(sk_tab->codex_session_name);
	g_free(sk_tab->codex_tracking_token);
	gtk_notebook_remove_page(GTK_NOTEBOOK(sakura.notebook), page);

	/* Find the next page, if it exists, and grab focus */
	if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook)) > 0) {
		page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
		sk_tab = sakura_get_sktab(sakura, page);
		gtk_widget_grab_focus(sk_tab->vte);
	}
	sakura_session_save();
}


/* New window -- launch a new instance */
static void
sakura_new_window()
{
	GPid pid;
	GError* error = NULL;
	char** spawn_argv = malloc(sizeof(char*) * ((sakura.orig_argc ? sakura.orig_argc : 1) + 2));
	if(!spawn_argv) {
		fprintf(stderr, "Error allocating memory for starting new instance!\n");
		return;
	}

#ifdef __linux__
	/* We try to get the full path of the currently running instance of sakura and use
	 * that for spawning a new process. This is to take care of the case when running
	 * an instance of sakura no in PATH. Unfortunately, procfs is not universally
	 * supported, we restrict it to Linux (and we assume that if we build on Linux,
	 * we will run on Linux as well) */
	char cmdline[PATH_MAX + 1];
	ssize_t tmp = readlink("/proc/self/exe", cmdline, PATH_MAX);
	cmdline[tmp] = 0;
	spawn_argv[0] = cmdline;
#else
	/* Otherwise, we rely on sakura being in PATH */
	spawn_argv[0] = "sakura";
#endif

	/* remove command arguments so that the new window will be in interactive mode */
	char** dst;
	char** src;
	if(sakura.orig_argc) for(dst = spawn_argv + 1, src = sakura.orig_argv + 1; *src; ++dst, ++src) {
		if(!strcmp(*src, "-e") || !strcmp(*src, "--xterm-execute")) {
			break;
		}
		if(!strcmp(*src, "-x") || !strcmp(*src, "--xterm")) {
			++src;
			if(!(*src)) {
				break;
			}
		} else {
			*dst = *src;
		}
	}
	*dst++ = "--new-window";
	*dst = NULL;

	/* Get a startup notification ID / xdg-activation token and add it to the environment */
	char **envp = NULL;
	/* TODO: keep this instead of recreating every time */
	GAppInfo *info = G_APP_INFO(g_desktop_app_info_new("sakura.desktop"));
	if (!info) info = g_app_info_create_from_commandline("sakura", "sakura", G_APP_INFO_CREATE_SUPPORTS_STARTUP_NOTIFICATION, NULL);
	if (info) {
		GdkAppLaunchContext *ctx = gdk_display_get_app_launch_context(gdk_display_get_default());
		char *startup_id = g_app_launch_context_get_startup_notify_id(G_APP_LAUNCH_CONTEXT(ctx), info, NULL);
		if (startup_id) {
			envp = g_get_environ();
			envp = g_environ_setenv(envp, "DESKTOP_STARTUP_ID", startup_id, TRUE);
			envp = g_environ_setenv(envp, "XDG_ACTIVATION_TOKEN", startup_id, TRUE);
		}
		g_object_unref(ctx);
	}

	if (!g_spawn_async(NULL, spawn_argv, envp,
			   G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
			   NULL, NULL, &pid, &error)) {
		fprintf(stderr, "Error starting new instance:\n%s\n", error->message);
		g_error_free(error);
	}
	g_spawn_close_pid(pid);
	free(spawn_argv);
	if (envp) g_strfreev(envp);
	if (info) g_object_unref(info);
}


/* Save configuration */
static void
sakura_config_done()
{
	GError *gerror = NULL;
	gsize len = 0;

	/* Don't save config file. Option only available thru the config file for users who know the risks */
	if (sakura.dont_save)
		return;

	gchar *cfgdata = g_key_file_to_data(sakura.cfg, &len, &gerror);
	if (!cfgdata) {
		fprintf(stderr, "%s\n", gerror->message);
		g_error_free(gerror);
		exit(EXIT_FAILURE);
	}

	bool overwrite = false;

	/* If there's been changes by another sakura process, ask whether to overwrite it or not */
	/* And if less_questions options is selected don't overwrite */
	if (sakura.externally_modified && !sakura.config_modified && !sakura.less_questions) {
		GtkWidget *dialog;
		gint response;

		dialog = gtk_message_dialog_new(GTK_WINDOW(sakura.main_window), GTK_DIALOG_MODAL,
						GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
						_("Configuration has been modified by another process. Overwrite?"));

		response = gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);

		if (response == GTK_RESPONSE_YES)
			overwrite = true;
	}

	/* Write to file IF there's been changes of IF we want to overwrite another process changes */
	if (sakura.config_modified || overwrite) {
		GIOChannel *cfgfile = g_io_channel_new_file(sakura.configfile, "w", &gerror);
		if (!cfgfile) {
			fprintf(stderr, "%s\n", gerror->message);
			g_error_free(gerror);
			exit(EXIT_FAILURE);
		}

		/* FIXME: if the number of chars written is not "len", something happened.
		 * Check for errors appropriately...*/
		GIOStatus status = g_io_channel_write_chars(cfgfile, cfgdata, len, NULL, &gerror);
		if (status != G_IO_STATUS_NORMAL) {
			// FIXME: we should deal with temporary failures (G_IO_STATUS_AGAIN)
			fprintf(stderr, "%s\n", gerror->message);
			g_error_free(gerror);
			exit(EXIT_FAILURE);
		}
		g_io_channel_shutdown(cfgfile, TRUE, &gerror);
		g_io_channel_unref(cfgfile);
	}
}


/*******************/
/* Misc. functions */
/*******************/

static void
sakura_error(const char *format, ...)
{
	GtkWidget *dialog;
	va_list args;
	char* buff;

	va_start(args, format);
	buff = g_malloc(sizeof(char)*ERROR_BUFFER_LENGTH);
	vsnprintf(buff, sizeof(char)*ERROR_BUFFER_LENGTH, format, args);
	va_end(args);

	dialog = gtk_message_dialog_new(GTK_WINDOW(sakura.main_window), GTK_DIALOG_DESTROY_WITH_PARENT,
	                                GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", buff);
	gtk_window_set_title(GTK_WINDOW(dialog), _("Error message"));
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
	g_free(buff);
}


static void
sakura_build_command(int *command_argc, char ***command_argv)
{
	GError *gerror = NULL;

	if (option_execute) {
		/* -x option: only one argument */
		if (!g_shell_parse_argv(option_execute, command_argc, command_argv, &gerror)) {
			switch (gerror->code) {
			case G_SHELL_ERROR_EMPTY_STRING:
				sakura_error("Empty exec string");
				exit(1);
				break;
			case G_SHELL_ERROR_BAD_QUOTING:
				sakura_error("Cannot parse command line arguments: mangled quoting");
				exit(1);
				break;
			case G_SHELL_ERROR_FAILED:
				sakura_error("Error in exec option command line arguments");
				exit(1);
			}
			g_error_free(gerror);
		}
	} else {
		/* -e option: last in the command line, takes all extra arguments */
		if (option_xterm_args) {

			guint size=0, i=0; gchar **quoted_args=NULL;

			do { size++; } while (option_xterm_args[size]); /* Get option_xterm_args size */

			/* Quote all arguments to be able to use parameters with spaces like filenames */
			quoted_args = g_malloc(sizeof(char *) * (size+1));
			while (option_xterm_args[i]) {
				quoted_args[i] = g_shell_quote(option_xterm_args[i]); i++;
			} 
			quoted_args[i]=NULL;

			/* Join all arguments and parse them to create argc&argv */
			gchar *command_joined= command_joined = g_strjoinv(" ", quoted_args);
			if (!g_shell_parse_argv(command_joined, command_argc, command_argv, &gerror)) {
				switch (gerror->code) {
				case G_SHELL_ERROR_EMPTY_STRING:
					sakura_error("Empty exec string");
					exit(1);
					break;
				case G_SHELL_ERROR_BAD_QUOTING:
					sakura_error("Cannot parse command line arguments: mangled quoting");
					exit(1);
				case G_SHELL_ERROR_FAILED:
					sakura_error("Error in exec option command line arguments");
					exit(1);
				}
			}

			if (gerror != NULL)
				g_error_free(gerror);
			g_free(command_joined);
			g_strfreev(quoted_args);
		}
	}
}


static void
sakura_set_keybind(const gchar *key, guint value)
{
	char *valname;

	valname = gdk_keyval_name(value);
	g_key_file_set_string(sakura.cfg, cfg_group, key, valname);
	sakura.config_modified = TRUE;
}


static guint
sakura_get_keybind(const gchar *key)
{
	gchar *value;
	guint retval = GDK_KEY_VoidSymbol;

	value = g_key_file_get_string(sakura.cfg, cfg_group, key, NULL);
	if (value != NULL) {
		retval = gdk_keyval_from_name(value);
		g_free(value);
	}

	/* For backwards compatibility with integer values */
	/* If gdk_keyval_from_name fail, it seems to be integer value*/
	if ((retval == GDK_KEY_VoidSymbol) || (retval == 0)) {
		retval = g_key_file_get_integer(sakura.cfg, cfg_group, key, NULL);
	}

	/* Always use uppercase value as keyval */
	return gdk_keyval_to_upper(retval);
}


/* Legacy function to use as fallback if our shell doesn't emit OSC7.
 * Retrieves the CWD of the specified sk_tab page.Original borrowed 
 * from gnome-terminal. Adapted by Hong Jen Yee and David Gómez */
static char *
sakura_get_term_cwd(struct sakura_tab* sk_tab)
{
	char *cwd = NULL;

	if (sk_tab->pid >= 0) {
		char *file, *buf;
		struct stat sb;
		int len;

		file = g_strdup_printf ("/proc/%d/cwd", sk_tab->pid);

		if (g_stat(file, &sb) == -1) {
			g_free(file);
			return cwd;
		}

		buf = g_malloc(sb.st_size + 1);

		if (buf == NULL) {
			g_free(file);
			return cwd;
		}

		len = readlink(file, buf, sb.st_size + 1);

		if (len > 0 && buf[0] == '/') {
			buf[len] = '\0';
			cwd = g_strdup(buf);
		}

		g_free(buf);
		g_free(file);
	}

	return cwd;
}


static char *
sakura_get_term_cwd_osc7(struct sakura_tab* sk_tab)
{
	gchar *cwd = NULL; gchar *osc7_hostname = NULL; 
	const char *osc7_uri = NULL; const char *hostname = NULL;

	osc7_uri = vte_terminal_get_current_directory_uri(VTE_TERMINAL(sk_tab->vte));

	if (osc7_uri) {
		cwd = g_filename_from_uri(osc7_uri, &osc7_hostname, NULL);
		/* Check if the hostname matchs. If not, return NULL */
		hostname = g_get_host_name();
		if ((strcmp(osc7_hostname, hostname) != 0) || (strcmp(osc7_hostname, "localhost") == 0)) cwd = NULL;
	}

	return cwd;
}


static void
sakura_update_tab_metadata (struct sakura_tab *sk_tab, const gchar *raw_title)
{
	const gchar *directory_uri;
	const gchar *local_host;
	gchar *uri_host = NULL;
	gchar *uri_cwd = NULL;
	gchar *fallback_cwd;

	if (sk_tab == NULL)
		return;

	g_free(sk_tab->raw_title);
	sk_tab->raw_title = g_strdup(raw_title != NULL ? raw_title : "");
	g_free(sk_tab->host);
	sk_tab->host = NULL;

	directory_uri = vte_terminal_get_current_directory_uri(VTE_TERMINAL(sk_tab->vte));
	if (directory_uri != NULL)
		uri_cwd = g_filename_from_uri(directory_uri, &uri_host, NULL);

	if (uri_cwd != NULL && uri_cwd[0] == '/') {
		g_free(sk_tab->cwd);
		sk_tab->cwd = g_strdup(uri_cwd);
	}

	local_host = g_get_host_name();
	if (uri_host != NULL && uri_host[0] != '\0' &&
	    g_ascii_strcasecmp(uri_host, "localhost") != 0 &&
	    g_ascii_strcasecmp(uri_host, local_host) != 0)
		sk_tab->host = g_strdup(uri_host);

	if (sk_tab->cwd == NULL || sk_tab->cwd[0] == '\0') {
		fallback_cwd = sakura_get_term_cwd(sk_tab);
		if (fallback_cwd != NULL) {
			g_free(sk_tab->cwd);
			sk_tab->cwd = fallback_cwd;
		}
	}

	g_free(uri_cwd);
	g_free(uri_host);
	sakura_sidebar_update_tab(sk_tab);
}


static guint
sakura_tokeycode (guint key)
{
	GdkKeymap *keymap;
	GdkKeymapKey *keys;
	gint n_keys;
	guint res = 0;

	keymap = gdk_keymap_get_for_display(gdk_display_get_default());

	/* Empty shortcut */
	if (key == 0) return 0;

	if (gdk_keymap_get_entries_for_keyval(keymap, key, &keys, &n_keys)) {
		if (n_keys > 0) {
			res = keys[0].keycode;
		}
		g_free(keys);
	}

	return res;
}


/* This function is used to fix bug #1393939 */
static void
sakura_sanitize_working_directory()
{
	const gchar *home_directory = g_getenv("HOME");
	if (home_directory == NULL) {
		home_directory = g_get_home_dir();
	}

	if (home_directory != NULL) {
		if (chdir(home_directory)) {
			fprintf(stderr, _("Cannot change working directory\n"));
			exit(1);
		}
	}
}


/********/
/* main */
/********/

int
main(int argc, char **argv)
{
	gchar *localedir;
	int i; int n;
	char **nargv; int nargc;
	gboolean have_e;

	/* Localization */
	setlocale(LC_ALL, "");
	localedir = g_strdup_printf("%s/locale", DATADIR);
	textdomain(GETTEXT_PACKAGE);
	bindtextdomain(GETTEXT_PACKAGE, localedir);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	g_free(localedir);

	/* Rewrites argv to include a -- after the -e argument this is required to make
	 * sure GOption doesn't grab any arguments meant for the command being called */

	/* Initialize nargv */
	nargv = (char**)calloc((argc+1), sizeof(char*));
	n = 0; nargc = argc;
	have_e = FALSE;
	/* Save original arguments to start a new instance if needed */
	sakura.orig_argc = argc;
	sakura.orig_argv = argv;

	for (i=0; i<argc; i++) {
		if (!have_e && g_strcmp0(argv[i],"-e") == 0)
		{
			nargv[n]="-e";
			n++;
			nargv[n]="--";
			nargc++;
			have_e = TRUE;
		} else {
			nargv[n]=g_strdup(argv[i]);
		}
		n++;
	}

	/* Options parsing */
	GError *error=NULL;
	GOptionContext *context; GOptionGroup *option_group;

	/* GTK reads GTK_THEME while its option group is initialized. */
	sakura_set_dark_theme_environment();
	context = g_option_context_new (_("- vte-based terminal emulator"));
	option_group = gtk_get_option_group(TRUE);
	g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
	g_option_group_set_translation_domain(option_group, GETTEXT_PACKAGE);
	g_option_context_add_group (context, option_group);
	if (!g_option_context_parse (context, &nargc, &nargv, &error)) {
		fprintf(stderr, "%s\n", error->message);
		g_error_free(error);
		exit(1);
	}

	g_option_context_free(context);

	if (option_workdir && chdir(option_workdir)) {
		fprintf(stderr, _("Cannot change working directory\n"));
		exit(1);
	}

	if (option_version) {
		fprintf(stderr, _("sakura version is %s\n"), VERSION);
		exit(1);
	}

	if (option_ntabs <= 0) {
		option_ntabs = 1;
	}

	/* Init stuff */
	gtk_init(&nargc, &nargv); g_strfreev(nargv);
	sakura_init();

	/* Restore the previous workspace for a normal launch. Explicit multi-tab
	 * launches and --new-session remain useful for starting fresh instances. */
	sakura.session_restoring = TRUE;
	if (option_codex_session != NULL) {
		sakura_add_tab_with_options(NULL, NULL, NULL, FALSE,
		                            SAKURA_TAB_CODEX, option_codex_session, NULL, NULL);
	} else if (option_new_session || option_new_window || option_ntabs > 1 || !sakura_session_restore()) {
		for (i=0; i<option_ntabs; i++)
			sakura_add_tab();
	}
	sakura.session_restoring = FALSE;
	sakura.session_ready = TRUE;
	if (!option_new_window)
		sakura_session_save();

	/* Post init stuff */
	sakura.first_run=false;
	g_strfreev(option_xterm_args);

	sakura_sanitize_working_directory();

	gtk_main();

	return 0;
}
