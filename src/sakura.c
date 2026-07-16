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
#include <sys/file.h>
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
#ifdef HAVE_WEBKITGTK
#include <webkit2/webkit2.h>
#endif

#include "sakura-private.h"


struct sakura_codex_name_query;

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
}\
#sakura-tab-bar {\
	padding: 2px;\
}\
#sakura-tab-bar button {\
min-height: 24px;\
padding: 2px 6px;\
background-image: none;\
background-color: transparent;\
border: 0;\
box-shadow: none;\
}\
#sakura-tab-bar .sakura-tab {\
min-height: 24px;\
border-radius: 4px;\
background-color: transparent;\
}\
#sakura-tab-bar .sakura-tab:hover {\
background-color: alpha(@theme_fg_color, 0.08);\
}\
#sakura-tab-bar .sakura-tab.selected {\
background-color: alpha(@theme_selected_bg_color, 0.22);\
}\
#sakura-tab-bar .sakura-tab > button:hover {\
background-image: none;\
background-color: transparent;\
}\
#sakura-tab-bar .sakura-tab > button.sakura-tab-close {\
min-width: 22px;\
padding-left: 2px;\
padding-right: 2px;\
}\
#sakura-tab-bar .sakura-tab > button.sakura-tab-close:hover {\
background-color: alpha(@theme_fg_color, 0.14);\
}"

#define DEFAULT_SIDEBAR_WIDTH 200

#define FADE_WINDOW_CSS "\
window#fade_window {\
	background-color: black;\
} "

#define FADE_WINDOW_OPACITY 0.5

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

SakuraApp sakura;

#define ICON_NAME "org.gnome.Terminal"
#define CODEX_ICON_NAME "sakura-codex"
#define GIT_ICON_NAME "git"
#define GITHUB_ICON_NAME "github"
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
#define DEFAULT_SPLIT_RIGHT_ACCELERATOR (GDK_CONTROL_MASK|GDK_SHIFT_MASK)
#define DEFAULT_SPLIT_DOWN_ACCELERATOR (GDK_CONTROL_MASK|GDK_SHIFT_MASK)
#define DEFAULT_FOCUS_PANE_ACCELERATOR (GDK_CONTROL_MASK|GDK_MOD1_MASK)
#define DEFAULT_PANE_ACTION_ACCELERATOR (GDK_CONTROL_MASK|GDK_SHIFT_MASK)
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
#define DEFAULT_SPLIT_RIGHT_KEY GDK_KEY_E
#define DEFAULT_SPLIT_DOWN_KEY GDK_KEY_P
#define DEFAULT_FOCUS_PANE_LEFT_KEY GDK_KEY_H
#define DEFAULT_FOCUS_PANE_RIGHT_KEY GDK_KEY_L
#define DEFAULT_FOCUS_PANE_UP_KEY GDK_KEY_K
#define DEFAULT_FOCUS_PANE_DOWN_KEY GDK_KEY_J
#define DEFAULT_PANE_CLOSE_KEY GDK_KEY_W
#define DEFAULT_PANE_EQUALIZE_KEY GDK_KEY_equal
#define DEFAULT_PANE_ZOOM_KEY GDK_KEY_Z
#define DEFAULT_SCROLLABLE_TABS TRUE
#define DEFAULT_PASTE_BUTTON 2
#define DEFAULT_MENU_BUTTON 3

/* make this an array instead of #defines to get a compile time
 * error instead of a runtime if NUM_COLORSETS changes */
static int cs_keys[NUM_COLORSETS] =
		{GDK_KEY_F1, GDK_KEY_F2, GDK_KEY_F3, GDK_KEY_F4, GDK_KEY_F5, GDK_KEY_F6};

#define ERROR_BUFFER_LENGTH 256
#define SAKURA_DEFAULT_MIN_WIDTH_CHARS 20
#define SAKURA_DEFAULT_MIN_HEIGHT_CHARS 1
const char cfg_group[] = "sakura";

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
/* VTE callbacks */
gboolean        sakura_term_buttonpressed_cb (GtkWidget *, GdkEventButton *, gpointer);
gboolean        sakura_term_buttonreleased_cb (GtkWidget *, GdkEventButton *, gpointer);
void            sakura_beep_cb (GtkWidget *, void *);
void            sakura_increase_font_cb (GtkWidget *, void *);
void            sakura_decrease_font_cb (GtkWidget *, void *);
void            sakura_child_exited_cb (GtkWidget *, void *);
void            sakura_eof_cb (GtkWidget *, void *);
static gboolean sakura_delete_event_cb (GtkWidget *, void *);
static void     sakura_destroy_window_cb (GtkWidget *, void *);
/* Main window callbacks */
gboolean        sakura_key_press_cb (GtkWidget *, GdkEventKey *, gpointer);
static gboolean sakura_resized_window_cb (GtkWidget *, GdkEventConfigure *, void *);
static gboolean sakura_focus_in_cb (GtkWidget *, GdkEvent *, void *);
static gboolean sakura_focus_out_cb (GtkWidget *, GdkEvent *, void *);
static void     sakura_conf_changed_cb (GtkWidget *, void *);
static void     sakura_show_event_cb (GtkWidget *, gpointer);
/* Notebook, notebook labels and notebook buttons callbacks */
void            sakura_switch_page_cb (GtkWidget *, GtkWidget *, guint, void *);
void            sakura_page_removed_cb (GtkWidget *, void *);
void            sakura_notebook_page_reordered_cb (GtkNotebook *, GtkWidget *, guint, void *);
gboolean        sakura_notebook_scroll_cb (GtkWidget *, GdkEventScroll *);
gboolean        sakura_label_clicked_cb (GtkWidget *, GdkEventButton *, void *);
gboolean        sakura_notebook_focus_cb (GtkWindow *, GdkEvent *, void *);
void            sakura_closebutton_clicked_cb (GtkWidget *, void *);
guint           sakura_tab_bar_visible_count (void);
gboolean        sakura_tab_is_in_active_scope (struct sakura_tab *);
gint            sakura_find_tab_by_terminal_id (const gchar *);
gboolean        sakura_tab_bar_select_relative (gint);
gint            sakura_tab_bar_nth_visible_page (guint);
void            sakura_tab_bar_add_tab (struct sakura_tab *);
void            sakura_tab_bar_remove_tab (struct sakura_tab *);
void            sakura_select_tab (struct sakura_tab *, gboolean);
/* Menuitem callbacks */
static void     sakura_font_dialog_cb (GtkWidget *, void *);
void            sakura_set_name_dialog_cb (GtkWidget *, void *);
static void     sakura_color_dialog_cb (GtkWidget *, void *);
//static void     sakura_set_title_dialog (GtkWidget *, void *);
void            sakura_new_tab_cb (GtkWidget *, void *);
void            sakura_new_codex_cb (GtkWidget *, void *);
void            sakura_resume_codex_cb (GtkWidget *, void *);
void            sakura_close_tab_cb (GtkWidget *, void *);
static void     sakura_fullscreen_cb (GtkWidget *, void *);
void            sakura_open_url_cb (GtkWidget *, void *);
void            sakura_open_mail_cb (GtkWidget *, void *);
void            sakura_copy_url_cb (GtkWidget *, void *);
static void     sakura_show_tab_bar_cb (GtkWidget *, void *);
static void     sakura_tabs_on_bottom_cb (GtkWidget *, void *);
static void     sakura_less_questions_cb (GtkWidget *, void *);
static void     sakura_copy_on_select_cb (GtkWidget *, void *);
static void     sakura_new_tab_after_current_cb (GtkWidget *, void *);
static void     sakura_show_scrollbar_cb (GtkWidget *, void *);
static void     sakura_disable_numbered_tabswitch_cb (GtkWidget *, void *);
//static void     sakura_use_fading_cb (GtkWidget *, void *);
static void     sakura_set_cursor_cb (GtkWidget *, void *);
static void     sakura_blinking_cursor_cb (GtkWidget *, void *);
static void     sakura_audible_bell_cb (GtkWidget *, void *);
static void     sakura_urgent_bell_cb (GtkWidget *, void *);
gboolean       sakura_sidebar_spinner_pulse_cb (gpointer);
void            sakura_tab_set_status (struct sakura_tab *, SakuraTabStatus, gboolean);
void            sakura_tab_clear_attention (struct sakura_tab *);
SakuraTab *       sakura_find_codex_tab_by_tracking_token (const gchar *);
void            sakura_codex_sync_name (struct sakura_tab *);
void            sakura_codex_name_helper_shutdown (void);
gchar *         sakura_find_codex_name_helper (void);
void            sakura_rename_codex_session_cb (GtkWidget *, void *);
void            sakura_codex_set_name_async (struct sakura_tab *, const gchar *);
void            sakura_codex_tracking_menu_update (GtkWidget *);
static void     sakura_codex_tracking_menu_show_cb (GtkWidget *, void *);
void            sakura_codex_tracking_status_cb (GtkWidget *, void *);

/* Misc */
void            sakura_error (const char *, ...);
static guint    sakura_tokeycode (guint key);
static void     sakura_set_keybind (const gchar *, guint);
static guint    sakura_get_keybind (const gchar *);
static guint    sakura_get_keybind_default (const gchar *, guint);
static gint     sakura_get_accelerator_default (const gchar *, gint);
static void     sakura_pane_menu_show_cb (GtkWidget *, gpointer);
static gboolean sakura_key_matches (GdkEventKey *, gint, gint);
static void     sakura_sanitize_working_directory (void);

/* Functions */
static void     sakura_init ();
static void     sakura_init_popup ();
static void     sakura_add_tab ();
void            sakura_add_tab_with_options (const gchar *, struct sakura_sidebar_node *,
                                             const gchar *, gboolean, SakuraTabKind,
                                             SakuraToolKind, const gchar *, const gchar *,
                                             const gchar *, const gchar *, const gchar *);
void            sakura_close_tab (gint); /* Save config, del tab and destroy sakura */
void            sakura_destroy ();
void            sakura_set_font ();
static gboolean sakura_prefers_dark_theme (void);
static void     sakura_set_dark_theme_environment (void);
static gchar *  sakura_get_default_font (void);
static gboolean sakura_load_gnome_terminal_colors (void);
void            sakura_config_done ();
static void     sakura_set_colorset (int);
void            sakura_set_colors (void);
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

gboolean
sakura_key_press_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	gint page;
	guint topage = 0;
	struct sakura_tab *current_tab;

	if (event->type != GDK_KEY_PRESS) return FALSE;
	if (sakura_key_matches(event, sakura.split_right_accelerator,
	                       sakura.split_right_key)) {
		sakura_split_current_cb(NULL, GINT_TO_POINTER(SAKURA_SPLIT_RIGHT));
		return TRUE;
	}
	if (sakura_key_matches(event, sakura.split_down_accelerator,
	                       sakura.split_down_key)) {
		sakura_split_current_cb(NULL, GINT_TO_POINTER(SAKURA_SPLIT_DOWN));
		return TRUE;
	}
	if (sakura_key_matches(event, sakura.focus_pane_left_accelerator,
	                       sakura.focus_pane_left_key)) {
		sakura_focus_direction_cb(NULL, GINT_TO_POINTER(SAKURA_FOCUS_LEFT));
		return TRUE;
	}
	if (sakura_key_matches(event, sakura.focus_pane_right_accelerator,
	                       sakura.focus_pane_right_key)) {
		sakura_focus_direction_cb(NULL, GINT_TO_POINTER(SAKURA_FOCUS_RIGHT));
		return TRUE;
	}
	if (sakura_key_matches(event, sakura.focus_pane_up_accelerator,
	                       sakura.focus_pane_up_key)) {
		sakura_focus_direction_cb(NULL, GINT_TO_POINTER(SAKURA_FOCUS_UP));
		return TRUE;
	}
	if (sakura_key_matches(event, sakura.focus_pane_down_accelerator,
	                       sakura.focus_pane_down_key)) {
		sakura_focus_direction_cb(NULL, GINT_TO_POINTER(SAKURA_FOCUS_DOWN));
		return TRUE;
	}
	if (sakura_key_matches(event, sakura.pane_zoom_accelerator,
	                       sakura.pane_zoom_key)) {
		sakura_toggle_zoom_current_cb(NULL, NULL);
		return TRUE;
	}
	if (sakura_key_matches(event, sakura.pane_equalize_accelerator,
	                       sakura.pane_equalize_key)) {
		sakura_equalize_current_cb(NULL, NULL);
		return TRUE;
	}
	if (sakura_key_matches(event, sakura.pane_close_accelerator,
	                       sakura.pane_close_key)) {
		sakura_close_tab_cb(NULL, NULL);
		return TRUE;
	}

	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	current_tab = page >= 0 ? sakura_tab_at_page(page) : NULL;
	if (current_tab != NULL && current_tab->text_selection_mode &&
	    event->keyval == GDK_KEY_Escape) {
		sakura_set_text_selection_mode(current_tab, FALSE);
		return TRUE;
	}

	/* Use keycodes instead of keyvals. With keyvals, key bindings work only in US/ISO8859-1 and similar locales */
	guint keycode = event->hardware_keycode;

	/* Get the GDK accel mask to compare with our accelerators */
	GdkModifierType accel_mask = gtk_accelerator_get_default_mod_mask();

	/* Add/delete tab keybinding pressed */
	if ((event->state & accel_mask) == sakura.add_tab_accelerator && keycode == sakura_tokeycode(sakura.add_tab_key)) {
		sakura_add_tab();
		return TRUE;
	} else if ((event->state & accel_mask) == sakura.del_tab_accelerator && keycode == sakura_tokeycode(sakura.del_tab_key)) {
		/* Delete only the terminal exposed by the active scope. */
		if (page >= 0 && sakura_tab_is_in_active_scope(sakura_tab_at_page(page)))
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
		/* The scoped tab strip is the source of truth for navigation. */
		guint visible_tabs = sakura_tab_bar_visible_count();
		if (visible_tabs >= 2) {
			if ((keycode >= sakura_tokeycode(GDK_KEY_1)) && (keycode <= sakura_tokeycode( GDK_KEY_9))) {

				/* User has explicitly disabled this branch, make sure to propagate the event */
				if (sakura.disable_numbered_tabswitch) return FALSE;

					if (sakura_tokeycode(GDK_KEY_1) == keycode) topage = 0;
					else if (sakura_tokeycode(GDK_KEY_2) == keycode) topage = 1;
					else if (sakura_tokeycode(GDK_KEY_3) == keycode) topage = 2;
					else if (sakura_tokeycode(GDK_KEY_4) == keycode) topage = 3;
					else if (sakura_tokeycode(GDK_KEY_5) == keycode) topage = 4;
					else if (sakura_tokeycode(GDK_KEY_6) == keycode) topage = 5;
					else if (sakura_tokeycode(GDK_KEY_7) == keycode) topage = 6;
					else if (sakura_tokeycode(GDK_KEY_8) == keycode) topage = 7;
					else if (sakura_tokeycode(GDK_KEY_9) == keycode) topage = 8;
					if (topage < visible_tabs) {
						gint visible_page = sakura_tab_bar_nth_visible_page(topage);
						if (visible_page >= 0)
							sakura_select_tab(sakura_tab_at_page(visible_page), FALSE);
					}
					return TRUE;
				} else if (keycode == sakura_tokeycode(sakura.prev_tab_key)) {
					sakura_tab_bar_select_relative(-1);
					return TRUE;
				} else if (keycode == sakura_tokeycode(sakura.next_tab_key)) {
					sakura_tab_bar_select_relative(1);
					return TRUE;
			}
		}
	}

	/* Move tab keybinding pressed */
	if ((event->state & accel_mask) == sakura.move_tab_accelerator) {
		if (keycode == sakura_tokeycode(sakura.prev_tab_key)) {
			sakura_tab_move_relative(-1);
			return TRUE;
		} else if (keycode == sakura_tokeycode(sakura.next_tab_key)) {
			sakura_tab_move_relative(1);
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
		if (sakura.fade_window != NULL)
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
	if (sakura.notebook != NULL) {
		sakura_tab_clear_attention(sakura.active_tab != NULL
		                         ? sakura.active_tab
		                         : sakura_tab_at_page(gtk_notebook_get_current_page(
		                               GTK_NOTEBOOK(sakura.notebook))));
	}

	return FALSE;
}


	/* Use focus-out-event to map the fade window */
static gboolean
sakura_focus_out_cb (GtkWidget *widget, GdkEvent *event, void *data)
{
	gint ax, ay, mx, my, x, y;

	if (event->type != GDK_FOCUS_CHANGE) return FALSE;
	if (!sakura.use_fading || sakura.fade_window == NULL) return FALSE;

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


/* Notebook focus is handled by the tab module. */






























void
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


void
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


static gboolean
sakura_delete_event_cb (GtkWidget *widget, void *data)
{
	struct sakura_tab *sk_tab;
	GtkWidget *dialog;
	gint response;
	gint npages;
	gint i;
	pid_t pgid;
	VtePty *pty;

	if (!sakura.less_questions) {
		npages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(sakura.notebook));

		/* Check for each tab if there are running processes. Use tcgetpgrp to compare to the shell PGID */
		for (i=0; i < npages; i++) {

			sk_tab = sakura_tab_at_page(i);
			pty = vte_terminal_get_pty(VTE_TERMINAL(sk_tab->vte));
			if (pty == NULL)
				continue;
			pgid = tcgetpgrp(vte_pty_get_fd(pty));

			/* If running processes are found, we ask one time and exit */
			if ( (pgid != -1) && (pgid != sk_tab->pid)) {
				dialog=gtk_message_dialog_new(GTK_WINDOW(sakura.main_window), GTK_DIALOG_MODAL,
											  GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
											  _("There are running processes.\n\nDo you really want to close Sakura?"));

				response=gtk_dialog_run(GTK_DIALOG(dialog));
				gtk_widget_destroy(dialog);

				if (response==GTK_RESPONSE_YES) {
					sakura_session_flush();
					sakura_config_done();
					return FALSE;
				} else {
					return TRUE;
				}
			}

		}
	}

	sakura_session_flush();
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
	gint i;


	sk_tab = sakura.active_tab != NULL ? sakura.active_tab :
	         sakura_tab_at_page(gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook)));
	if (sk_tab == NULL)
		return;

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
sakura_show_tab_bar_cb (GtkWidget *widget, void *data)
{
	char *setting_string = (char *)data;
	char *config_string;

	if (strcmp(setting_string, "always")==0) {
		sakura.show_tab_bar = SHOW_TAB_BAR_ALWAYS;
		config_string = "always";
	} else if (strcmp(setting_string, "multiple")==0) {
		sakura.show_tab_bar = SHOW_TAB_BAR_MULTIPLE;
		config_string = "multiple";
	} else if (strcmp(setting_string, "never")==0) {
		sakura.show_tab_bar = SHOW_TAB_BAR_NEVER;
		config_string = "never";
	} else {
		return;
	}

	sakura_set_config_string("show_tab_bar", config_string);
	sakura_tab_bar_refresh();

	sakura_set_size();
}


static void
sakura_tabs_on_bottom_cb (GtkWidget *widget, void *data)
{

	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
		gtk_box_reorder_child(GTK_BOX(sakura.content_box), sakura.tab_bar_shell, 1);
		sakura_set_config_boolean("tabs_on_bottom", TRUE);
	} else {
		gtk_box_reorder_child(GTK_BOX(sakura.content_box), sakura.tab_bar_shell, 0);
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
	struct sakura_tab *sk_tab;

	sk_tab = sakura.active_tab != NULL ? sakura.active_tab :
	         sakura_tab_at_page(gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook)));
	if (sk_tab == NULL)
		return;

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
	struct sakura_tab *sk_tab;

	sk_tab = sakura.active_tab != NULL ? sakura.active_tab :
	         sakura_tab_at_page(gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook)));
	if (sk_tab == NULL)
		return;

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
	int i;

	char *cursor_string = (char *)data;
	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {

		if (strcmp(cursor_string, "block")==0) {
			sakura.cursor_type=VTE_CURSOR_SHAPE_BLOCK;
		} else if (strcmp(cursor_string, "underline")==0) {
			sakura.cursor_type=VTE_CURSOR_SHAPE_UNDERLINE;
		} else if (strcmp(cursor_string, "ibeam")==0) {
			sakura.cursor_type=VTE_CURSOR_SHAPE_IBEAM;
		}

		for (i = sakura.panes != NULL ? (gint)sakura.panes->len - 1 : -1; i >= 0; i--) {
			sk_tab = g_ptr_array_index(sakura.panes, i);
			if (sk_tab != NULL)
				vte_terminal_set_cursor_shape(VTE_TERMINAL(sk_tab->vte), sakura.cursor_type);
		}

		sakura_set_config_integer("cursor_type", sakura.cursor_type);
	}
}


void
sakura_new_tab_cb (GtkWidget *widget, void *data)
{
	sakura_session_accept_changes();
	sakura_add_tab();
}


void
sakura_split_current_cb (GtkWidget *widget, void *data)
{
	SakuraTabLaunchConfig config = { 0 };
	SakuraPage *page = sakura.active_page;
	SakuraTab *tab = sakura.active_tab;
	SakuraSplitDirection direction = data != NULL
	                              ? GPOINTER_TO_INT(data) : SAKURA_SPLIT_RIGHT;

	(void)widget;
	if (direction < SAKURA_SPLIT_RIGHT || direction > SAKURA_SPLIT_DOWN)
		return;
	if (page == NULL || tab == NULL || tab->page != page ||
	    !sakura_tab_can_split(tab) || page->container == NULL)
		return;

	config.target_page = page;
	config.target_layout = tab->layout_leaf;
	config.target_ratio = 0.5;
	config.split_direction = direction;
	sakura_session_accept_changes();
	sakura_tab_add_with_options(NULL, NULL, NULL, FALSE,
	                            SAKURA_TAB_SHELL, SAKURA_TOOL_NONE,
	                            NULL, NULL, NULL, NULL, NULL, &config);
}


static gboolean
sakura_layout_preset_add_pane(SakuraPage *page, SakuraLayoutNode *target,
                               SakuraSplitDirection direction, gdouble ratio,
                               SakuraTab **new_tab_out)
{
	SakuraTabLaunchConfig config = { 0 };
	guint old_len;
	SakuraTab *new_tab;

	if (page == NULL || target == NULL || page->panes == NULL)
		return FALSE;
	old_len = page->panes->len;
	config.target_page = page;
	config.target_layout = target;
	config.target_ratio = ratio;
	config.split_direction = direction;
	sakura_tab_add_with_options(NULL, NULL, NULL, FALSE,
	                            SAKURA_TAB_SHELL, SAKURA_TOOL_NONE,
	                            NULL, NULL, NULL, NULL, NULL, &config);
	if (page->panes->len != old_len + 1)
		return FALSE;
	new_tab = g_ptr_array_index(page->panes, old_len);
	if (new_tab == NULL || new_tab->layout_leaf == NULL ||
	    new_tab->layout_leaf->parent == NULL)
		return FALSE;
	sakura_layout_set_ratio(new_tab->layout_leaf->parent, ratio);
	if (new_tab_out != NULL)
		*new_tab_out = new_tab;
	return TRUE;
}


void
sakura_apply_layout_preset_cb(GtkWidget *widget, void *data)
{
	SakuraLayoutPreset preset = data != NULL
	                          ? GPOINTER_TO_INT(data)
	                          : SAKURA_LAYOUT_PRESET_TWO_COLUMNS;
	SakuraPage *page = sakura.active_page;
	SakuraTab *active = sakura.active_tab;
	SakuraTab *new_tab = NULL;
	SakuraLayoutNode *root, *first, *second;

	(void)widget;
	if (page == NULL || active == NULL || active->page != page ||
	    page->layout_root == NULL || page->panes == NULL ||
	    page->panes->len != 1 || !sakura_tab_can_split(active))
		return;

	root = page->layout_root;
	sakura_session_accept_changes();
	switch (preset) {
		case SAKURA_LAYOUT_PRESET_TWO_COLUMNS:
			if (!sakura_layout_preset_add_pane(page, root,
			                                  SAKURA_SPLIT_RIGHT, 0.5, &new_tab))
				return;
			break;
		case SAKURA_LAYOUT_PRESET_TWO_ROWS:
			if (!sakura_layout_preset_add_pane(page, root,
			                                  SAKURA_SPLIT_DOWN, 0.5, &new_tab))
				return;
			break;
		case SAKURA_LAYOUT_PRESET_GRID_2X2:
			if (!sakura_layout_preset_add_pane(page, root,
			                                  SAKURA_SPLIT_RIGHT, 0.5, &new_tab))
				return;
			first = root;
			second = new_tab->layout_leaf;
			if (!sakura_layout_preset_add_pane(page, first,
			                                  SAKURA_SPLIT_DOWN, 0.5, NULL) ||
			    !sakura_layout_preset_add_pane(page, second,
			                                  SAKURA_SPLIT_DOWN, 0.5, NULL))
				return;
			break;
		case SAKURA_LAYOUT_PRESET_MAIN_STACK:
			if (!sakura_layout_preset_add_pane(page, root,
			                                  SAKURA_SPLIT_RIGHT, 0.65, &new_tab) ||
			    !sakura_layout_preset_add_pane(page, new_tab->layout_leaf,
			                                  SAKURA_SPLIT_DOWN, 0.5, NULL))
				return;
			break;
		default:
			return;
	}

	page->active_tab = active;
	sakura.active_tab = active;
	sakura.active_page = page;
	sakura_select_tab(active, TRUE);
	sakura_update_geometry_hints();
	sakura_session_mark_dirty();
}


static gboolean
sakura_focus_candidate_better(SakuraFocusDirection direction,
                              gint active_x, gint active_y,
                              gint active_width, gint active_height,
                              gint candidate_x, gint candidate_y,
                              gint candidate_width, gint candidate_height,
                              gint *distance_out)
{
	gint active_right = active_x + active_width;
	gint active_bottom = active_y + active_height;
	gint candidate_right = candidate_x + candidate_width;
	gint candidate_bottom = candidate_y + candidate_height;
	gint gap, perpendicular;
	gboolean overlap;

	switch (direction) {
		case SAKURA_FOCUS_LEFT:
			if (candidate_right > active_x)
				return FALSE;
			gap = active_x - candidate_right;
			overlap = candidate_bottom > active_y && candidate_y < active_bottom;
			perpendicular = abs((candidate_y + candidate_height / 2) -
			                   (active_y + active_height / 2));
			break;
		case SAKURA_FOCUS_RIGHT:
			if (candidate_x < active_right)
				return FALSE;
			gap = candidate_x - active_right;
			overlap = candidate_bottom > active_y && candidate_y < active_bottom;
			perpendicular = abs((candidate_y + candidate_height / 2) -
			                   (active_y + active_height / 2));
			break;
		case SAKURA_FOCUS_UP:
			if (candidate_bottom > active_y)
				return FALSE;
			gap = active_y - candidate_bottom;
			overlap = candidate_right > active_x && candidate_x < active_right;
			perpendicular = abs((candidate_x + candidate_width / 2) -
			                   (active_x + active_width / 2));
			break;
		case SAKURA_FOCUS_DOWN:
			if (candidate_y < active_bottom)
				return FALSE;
			gap = candidate_y - active_bottom;
			overlap = candidate_right > active_x && candidate_x < active_right;
			perpendicular = abs((candidate_x + candidate_width / 2) -
			                   (active_x + active_width / 2));
			break;
		default:
			return FALSE;
	}
	*distance_out = gap * 1000 + (overlap ? 0 : 100000) + perpendicular;
	return TRUE;
}


void
sakura_focus_direction_cb(GtkWidget *widget, void *data)
{
	SakuraPage *page = sakura.active_page;
	SakuraTab *active = sakura.active_tab;
	SakuraTab *best = NULL;
	SakuraFocusDirection direction = GPOINTER_TO_INT(data);
	gint active_x, active_y, active_width, active_height;
	gint best_distance = G_MAXINT;
	guint index;

	(void)widget;
	if (page == NULL || active == NULL || active->hbox == NULL ||
	    direction < SAKURA_FOCUS_LEFT || direction > SAKURA_FOCUS_DOWN ||
	    !gtk_widget_translate_coordinates(active->hbox,
                                                        page->container, 0, 0,
                                                        &active_x, &active_y))
		return;
	active_width = gtk_widget_get_allocated_width(active->hbox);
	active_height = gtk_widget_get_allocated_height(active->hbox);
	for (index = 0; page->panes != NULL && index < page->panes->len; index++) {
		SakuraTab *candidate = g_ptr_array_index(page->panes, index);
		gint candidate_x, candidate_y, candidate_width, candidate_height, distance;
		if (candidate == NULL || candidate == active || candidate->hbox == NULL ||
		    !gtk_widget_get_visible(candidate->hbox) ||
		    !gtk_widget_translate_coordinates(candidate->hbox, page->container,
	                                           0, 0, &candidate_x, &candidate_y))
			continue;
		candidate_width = gtk_widget_get_allocated_width(candidate->hbox);
		candidate_height = gtk_widget_get_allocated_height(candidate->hbox);
		if (sakura_focus_candidate_better(direction, active_x, active_y,
		                                  active_width, active_height,
		                                  candidate_x, candidate_y,
		                                  candidate_width, candidate_height,
		                                  &distance) && distance < best_distance) {
			best = candidate;
			best_distance = distance;
		}
	}
	if (best != NULL)
		sakura_select_tab(best, TRUE);
}


void
sakura_toggle_zoom_current_cb(GtkWidget *widget, void *data)
{
	SakuraPage *page = sakura.active_page;

	(void)widget;
	(void)data;
	if (page == NULL || sakura.active_tab == NULL)
		return;
	sakura_layout_set_zoomed(page, sakura.active_tab, !page->zoomed);
	sakura_session_mark_dirty();
}


void
sakura_equalize_current_cb(GtkWidget *widget, void *data)
{
	SakuraLayoutNode *split;
	GtkAllocation allocation;

	(void)widget;
	(void)data;
	if (sakura.active_tab == NULL || sakura.active_tab->layout_leaf == NULL)
		return;
	split = sakura.active_tab->layout_leaf->parent;
	if (split == NULL || split->kind != SAKURA_LAYOUT_SPLIT || split->widget == NULL)
		return;
	split->data.split.ratio = 0.5;
	gtk_widget_get_allocation(split->widget, &allocation);
	gtk_paned_set_position(GTK_PANED(split->widget),
	                       split->data.split.direction == SAKURA_SPLIT_RIGHT
	                     ? allocation.width / 2 : allocation.height / 2);
	sakura_session_mark_dirty();
}


void
sakura_close_tab_cb (GtkWidget *widget, void *data)
{
	struct sakura_tab *sk_tab = data;
	gint page;

	if (sk_tab == NULL && sakura.active_tab != NULL &&
	    sakura.active_tab->page != NULL && sakura.active_tab->page->panes != NULL &&
	    sakura.active_tab->page->panes->len > 1) {
		sakura_tab_delete_pane(sakura.active_tab);
		return;
	}
	page = sk_tab != NULL
	     ? sakura_page_for_tab(sk_tab)
	     : gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));

	if (page >= 0)
		sakura_close_tab(page);
}


static void
sakura_pane_menu_show_cb(GtkWidget *widget, gpointer data)
{
	SakuraTab *tab = sakura.active_tab;
	SakuraLayoutNode *parent = tab != NULL ? tab->layout_leaf != NULL
	                                      ? tab->layout_leaf->parent : NULL : NULL;
	gboolean can_split = sakura_tab_can_split(tab);
	gboolean has_panes = tab != NULL && tab->page != NULL &&
	                     tab->page->panes != NULL && tab->page->panes->len > 1;
	gboolean single_pane = tab != NULL && tab->page != NULL &&
	                      tab->page->panes != NULL && tab->page->panes->len == 1;

	(void)widget;
	(void)data;
	if (sakura.pane_split_right != NULL)
		gtk_widget_set_sensitive(sakura.pane_split_right, can_split);
	if (sakura.pane_split_down != NULL)
		gtk_widget_set_sensitive(sakura.pane_split_down, can_split);
	if (sakura.pane_focus_left != NULL)
		gtk_widget_set_sensitive(sakura.pane_focus_left, has_panes);
	if (sakura.pane_focus_right != NULL)
		gtk_widget_set_sensitive(sakura.pane_focus_right, has_panes);
	if (sakura.pane_focus_up != NULL)
		gtk_widget_set_sensitive(sakura.pane_focus_up, has_panes);
	if (sakura.pane_focus_down != NULL)
		gtk_widget_set_sensitive(sakura.pane_focus_down, has_panes);
	if (sakura.pane_close != NULL)
		gtk_widget_set_sensitive(sakura.pane_close, tab != NULL);
	if (sakura.pane_equalize != NULL)
		gtk_widget_set_sensitive(sakura.pane_equalize, parent != NULL &&
		                         parent->kind == SAKURA_LAYOUT_SPLIT);
	if (sakura.pane_zoom != NULL)
		gtk_widget_set_sensitive(sakura.pane_zoom, has_panes);
	if (sakura.pane_layout_menu != NULL)
		gtk_widget_set_sensitive(sakura.pane_layout_menu, single_pane && can_split);
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

	sakura.tabs = g_ptr_array_new();
	sakura.pages = g_ptr_array_new();
	sakura.panes = g_ptr_array_new();
	sakura.session_new_window = option_new_window;

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

	sakura.split_right_accelerator = sakura_get_accelerator_default(
		"split_right_accelerator", DEFAULT_SPLIT_RIGHT_ACCELERATOR);
	sakura.split_down_accelerator = sakura_get_accelerator_default(
		"split_down_accelerator", DEFAULT_SPLIT_DOWN_ACCELERATOR);
	sakura.focus_pane_left_accelerator = sakura_get_accelerator_default(
		"focus_pane_left_accelerator", DEFAULT_FOCUS_PANE_ACCELERATOR);
	sakura.focus_pane_right_accelerator = sakura_get_accelerator_default(
		"focus_pane_right_accelerator", DEFAULT_FOCUS_PANE_ACCELERATOR);
	sakura.focus_pane_up_accelerator = sakura_get_accelerator_default(
		"focus_pane_up_accelerator", DEFAULT_FOCUS_PANE_ACCELERATOR);
	sakura.focus_pane_down_accelerator = sakura_get_accelerator_default(
		"focus_pane_down_accelerator", DEFAULT_FOCUS_PANE_ACCELERATOR);
	sakura.pane_close_accelerator = sakura_get_accelerator_default(
		"pane_close_accelerator", DEFAULT_PANE_ACTION_ACCELERATOR);
	sakura.pane_equalize_accelerator = sakura_get_accelerator_default(
		"pane_equalize_accelerator", DEFAULT_PANE_ACTION_ACCELERATOR);
	sakura.pane_zoom_accelerator = sakura_get_accelerator_default(
		"pane_zoom_accelerator", DEFAULT_PANE_ACTION_ACCELERATOR);

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

	sakura.split_right_key = sakura_get_keybind_default(
		"split_right_key", DEFAULT_SPLIT_RIGHT_KEY);
	sakura.split_down_key = sakura_get_keybind_default(
		"split_down_key", DEFAULT_SPLIT_DOWN_KEY);
	sakura.focus_pane_left_key = sakura_get_keybind_default(
		"focus_pane_left_key", DEFAULT_FOCUS_PANE_LEFT_KEY);
	sakura.focus_pane_right_key = sakura_get_keybind_default(
		"focus_pane_right_key", DEFAULT_FOCUS_PANE_RIGHT_KEY);
	sakura.focus_pane_up_key = sakura_get_keybind_default(
		"focus_pane_up_key", DEFAULT_FOCUS_PANE_UP_KEY);
	sakura.focus_pane_down_key = sakura_get_keybind_default(
		"focus_pane_down_key", DEFAULT_FOCUS_PANE_DOWN_KEY);
	sakura.pane_close_key = sakura_get_keybind_default(
		"pane_close_key", DEFAULT_PANE_CLOSE_KEY);
	sakura.pane_equalize_key = sakura_get_keybind_default(
		"pane_equalize_key", DEFAULT_PANE_EQUALIZE_KEY);
	sakura.pane_zoom_key = sakura_get_keybind_default(
		"pane_zoom_key", DEFAULT_PANE_ZOOM_KEY);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "set_colorset_accelerator", NULL)) {
		sakura_set_config_integer("set_colorset_accelerator", DEFAULT_SELECT_COLORSET_ACCELERATOR);
	}
	sakura.set_colorset_accelerator = g_key_file_get_integer(sakura.cfg, cfg_group, "set_colorset_accelerator", NULL);

	if (!g_key_file_has_key(sakura.cfg, cfg_group, "icon_file", NULL)) {
		sakura_set_config_string("icon_file", ICON_NAME);
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
	sakura.editor_command = g_key_file_get_string(sakura.cfg, cfg_group, "editor_command", NULL);

	/* Default terminal. Only in config file */
	sakura.term = g_key_file_get_value(sakura.cfg, cfg_group, "term", NULL);
	sakura.session_lock_fd = -1;

	if (!sakura.dont_save) {
		int lock_result;

		sakura.sessionfile = g_strdup_printf("%s.session", sakura.configfile);
		if (!option_new_window) {
			lock_result = sakura_session_lock_acquire(&sakura, sakura.sessionfile);
			if (lock_result == 0) {
				if (!sakura_session_confirm_new_instance(&sakura))
					exit(EXIT_SUCCESS);
				if (!sakura_session_start_new_instance(&sakura)) {
					fprintf(stderr, _("Could not create a separate persistent session.\n"));
					exit(EXIT_FAILURE);
				}
			} else if (lock_result < 0) {
				fprintf(stderr, _("Could not lock the Sakura session.\n"));
				exit(EXIT_FAILURE);
			}
		}
		sakura.codex_tracking_dir = g_strdup_printf("%s.codex", sakura.sessionfile);
		sakura.history_dir = g_strdup_printf("%s.history", sakura.sessionfile);
		if (g_mkdir_with_parents(sakura.codex_tracking_dir, 0700) != 0)
			SAY("Could not create Codex tracking directory: %s", g_strerror(errno));
		if (g_mkdir_with_parents(sakura.history_dir, 0700) != 0)
			SAY("Could not create terminal history directory: %s", g_strerror(errno));
		else if (chmod(sakura.history_dir, 0700) != 0)
			SAY("Could not secure terminal history directory: %s", g_strerror(errno));
		sakura_session_prepare_bash_integration(&sakura);
		if (!option_new_window)
			sakura_session_load_file(&sakura, !option_new_session && !option_new_window);
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

	if (sakura.use_fading) {
		sakura.fade_window = gtk_window_new(GTK_WINDOW_POPUP);
		gtk_widget_set_name(sakura.fade_window, "fade_window");
		gtk_window_set_position(GTK_WINDOW(sakura.fade_window), GTK_WIN_POS_NONE);
		gtk_widget_set_opacity(sakura.fade_window, FADE_WINDOW_OPACITY);
		gtk_window_set_transient_for(GTK_WINDOW(sakura.fade_window), GTK_WINDOW(sakura.main_window));
	}

	/* Add CSS styles for main and fade window*/
	GtkCssProvider *provider = gtk_css_provider_new();
	GdkScreen *screen = gtk_widget_get_screen(GTK_WIDGET(sakura.main_window));
	gtk_css_provider_load_from_data(provider, SAKURA_CSS, -1, NULL);
	gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER (provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(provider);

	if (sakura.fade_window != NULL) {
		provider = gtk_css_provider_new();
		screen = gtk_widget_get_screen(GTK_WIDGET(sakura.fade_window));
		gtk_css_provider_load_from_data(provider, FADE_WINDOW_CSS, -1, NULL);
		gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER (provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		g_object_unref(provider);
	}

	/* Create notebook and set style */
	sakura.notebook = gtk_notebook_new();
	gtk_notebook_set_scrollable((GtkNotebook*)sakura.notebook, sakura.scrollable_tabs);
	sakura_register_codex_icon();
	sakura_sidebar_init(!option_new_session && !option_new_window);
	sakura.cwd_tracking_source_id = g_timeout_add(500, sakura_cwd_tracking_poll_cb, NULL);

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

	/* Use GNOME Terminal's themed icon by default. Keep accepting a configured
	 * file name for compatibility with existing Sakura configurations. */
	gchar *icon_path; gerror=NULL;
	if (!option_icon && g_strcmp0(sakura.icon, ICON_NAME) == 0) {
		gtk_window_set_icon_name(GTK_WINDOW(sakura.main_window), ICON_NAME);
		icon_path = NULL;
	} else if (option_icon) {
		icon_path = g_strdup_printf("%s", option_icon);
	} else {
		icon_path = g_strdup_printf(DATADIR "/pixmaps/%s", sakura.icon);
	}
	if (icon_path != NULL) {
		gtk_window_set_icon_from_file(GTK_WINDOW(sakura.main_window), icon_path, &gerror);
		g_free(icon_path); icon_path=NULL;
	}
	if (gerror) {
		g_error_free(gerror);
		gtk_window_set_icon_name(GTK_WINDOW(sakura.main_window), ICON_NAME);
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
	/* Restored pages are built before the workspace is mounted. Apply the
	 * saved visible selection now that GTK can honor set_current_page(). */
	sakura_workspace_finish_restore();

	sakura_init_popup();

	g_signal_connect(G_OBJECT(sakura.main_window), "delete_event", G_CALLBACK(sakura_delete_event_cb), NULL);
	g_signal_connect(G_OBJECT(sakura.main_window), "destroy", G_CALLBACK(sakura_destroy_window_cb), NULL);
	g_signal_connect(G_OBJECT(sakura.main_window), "key-press-event", G_CALLBACK(sakura_key_press_cb), NULL);
	g_signal_connect(G_OBJECT(sakura.main_window), "configure-event", G_CALLBACK(sakura_resized_window_cb), NULL);
	g_signal_connect(G_OBJECT(sakura.main_window), "focus-out-event", G_CALLBACK(sakura_focus_out_cb), NULL);
	g_signal_connect(G_OBJECT(sakura.main_window), "focus-in-event", G_CALLBACK(sakura_focus_in_cb), NULL);
	g_signal_connect(G_OBJECT(sakura.main_window), "show", G_CALLBACK(sakura_show_event_cb), NULL);
}


void
sakura_codex_tracking_menu_update (GtkWidget *item)
{
	SakuraCodexTrackingState state;

	if (item == NULL)
		return;

	state = sakura_codex_tracking_state();
	switch (state) {
	case SAKURA_CODEX_TRACKING_ENABLED:
		gtk_menu_item_set_label(GTK_MENU_ITEM(item),
		                        _("Codex session tracking enabled"));
		gtk_widget_set_sensitive(item, FALSE);
		break;
	case SAKURA_CODEX_TRACKING_PARTIAL:
		gtk_menu_item_set_label(GTK_MENU_ITEM(item),
		                        _("Repair Codex session tracking"));
		gtk_widget_set_sensitive(item, TRUE);
		break;
	case SAKURA_CODEX_TRACKING_MISSING:
	default:
		gtk_menu_item_set_label(GTK_MENU_ITEM(item),
		                        _("Enable Codex session tracking"));
		gtk_widget_set_sensitive(item, TRUE);
		break;
	}
}


static void
sakura_codex_tracking_menu_show_cb (GtkWidget *widget, void *data)
{
	(void)widget;
	sakura_codex_tracking_menu_update(GTK_WIDGET(data));
}


static GtkWidget *
sakura_codex_reasoning_menu_new(void)
{
	static const gchar *efforts[] = { "low", "medium", "high", "xhigh" };
	const gchar *labels[] = { _("Fast"), _("Balanced"), _("Deep"), _("Max") };
	GtkWidget *menu = gtk_menu_new();
	guint index;

	for (index = 0; index < G_N_ELEMENTS(efforts); index++) {
		GtkWidget *item = gtk_menu_item_new_with_label(labels[index]);
		g_object_set_data(G_OBJECT(item), SAKURA_CODEX_REASONING_EFFORT_DATA_KEY,
		                  (gpointer)efforts[index]);
		g_signal_connect(item, "activate", G_CALLBACK(sakura_new_codex_cb), NULL);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	}
	return menu;
}


static void
sakura_init_popup()
{
	GtkWidget *item_new_tab, *item_tools, *item_open_here,
	          *item_gitui, *item_git_cola, *item_gh_dash, *item_open_pr,
	          *item_set_name, *item_close_tab, *item_pane, *item_copy,
	          *item_paste, *item_select_text, *item_fullscreen,
	          *item_select_font, *item_select_colors,
	          *item_show_tab_bar, *item_sidebar,
	          *item_show_tab_bar_always, *item_show_tab_bar_multiple, *item_show_tab_bar_never,
	          *item_toggle_scrollbar, *item_options,
	          *item_urgent_bell, *item_audible_bell, *item_blinking_cursor,
	          *item_cursor, *item_cursor_block, *item_cursor_underline, *item_cursor_ibeam,
		  *item_tabs_on_bottom, *item_less_questions, *item_copy_on_select,
	          *item_disable_numbered_tabswitch, *item_new_tab_after_current; // *item_use_fading;
	GtkWidget *options_menu, *show_tab_bar_menu, *cursor_menu, *tools_menu, *pane_menu;
	GtkWidget *pane_layout_menu, *pane_layout_item, *pane_two_columns,
	          *pane_two_rows, *pane_grid_2x2, *pane_main_stack;
	GtkWidget *pane_split_right, *pane_split_down, *pane_focus_left,
	          *pane_focus_right, *pane_focus_up, *pane_focus_down,
	          *pane_close, *pane_equalize, *pane_zoom;
	GtkWidget *codex_menu, *item_codex, *item_new_codex, *item_new_codex_reasoning,
	          *item_resume_codex,
	          *item_attach_codex, *item_rename_codex, *item_refresh_codex_name, *item_codex_status,
	          *item_install_codex;

	sakura.item_open_mail = gtk_menu_item_new_with_label(_("Open mail"));
	sakura.item_open_link = gtk_menu_item_new_with_label(_("Open link"));
	sakura.item_copy_link = gtk_menu_item_new_with_label(_("Copy link"));
	item_new_tab = gtk_menu_item_new_with_label(_("New tab"));
	item_pane = gtk_menu_item_new_with_label(_("Pane"));
	pane_split_right = gtk_menu_item_new_with_label(_("Split Right"));
	pane_split_down = gtk_menu_item_new_with_label(_("Split Down"));
	pane_focus_left = gtk_menu_item_new_with_label(_("Focus Left"));
	pane_focus_right = gtk_menu_item_new_with_label(_("Focus Right"));
	pane_focus_up = gtk_menu_item_new_with_label(_("Focus Up"));
	pane_focus_down = gtk_menu_item_new_with_label(_("Focus Down"));
	pane_close = gtk_menu_item_new_with_label(_("Close pane"));
	pane_equalize = gtk_menu_item_new_with_label(_("Equalize split"));
	pane_zoom = gtk_menu_item_new_with_label(_("Zoom pane"));
	pane_layout_item = gtk_menu_item_new_with_label(_("Layout preset"));
	pane_two_columns = gtk_menu_item_new_with_label(_("Two columns"));
	pane_two_rows = gtk_menu_item_new_with_label(_("Two rows"));
	pane_grid_2x2 = gtk_menu_item_new_with_label(_("2 × 2 grid"));
	pane_main_stack = gtk_menu_item_new_with_label(_("Main + stack"));
	item_tools = gtk_menu_item_new_with_label(_("Tools"));
	item_open_here = gtk_menu_item_new_with_label(_("Open Here"));
	item_gitui = gtk_menu_item_new_with_label(_("GitUI"));
	item_git_cola = gtk_menu_item_new_with_label(_("Git Cola"));
	item_gh_dash = gtk_menu_item_new_with_label(_("GitHub Dashboard"));
	item_open_pr = gtk_menu_item_new_with_label(_("Open pull request..."));
	item_codex = gtk_menu_item_new_with_label(_("Codex"));
	item_new_codex = gtk_menu_item_new_with_label(_("New Codex session"));
	item_new_codex_reasoning = gtk_menu_item_new_with_label(
		_("New session with reasoning"));
	item_resume_codex = gtk_menu_item_new_with_label(_("Resume session..."));
	item_attach_codex = gtk_menu_item_new_with_label(_("Attach current tab..."));
	item_rename_codex = gtk_menu_item_new_with_label(_("Rename session..."));
	item_refresh_codex_name = gtk_menu_item_new_with_label(_("Refresh session name"));
	item_codex_status = gtk_menu_item_new_with_label(_("Check session tracking"));
	item_install_codex = gtk_menu_item_new_with_label(_("Enable Codex session tracking"));
	item_set_name = gtk_menu_item_new_with_label(_("Set tab name..."));
	item_close_tab = gtk_menu_item_new_with_label(_("Close tab"));
	item_fullscreen = gtk_menu_item_new_with_label(_("Full screen"));
	item_copy = gtk_menu_item_new_with_label(_("Copy"));
	item_paste = gtk_menu_item_new_with_label(_("Paste"));
	item_select_text = gtk_check_menu_item_new_with_label(_("Select text mode"));
	sakura.item_select_text = item_select_text;

	item_options = gtk_menu_item_new_with_label(_("Options"));

	item_select_font = gtk_menu_item_new_with_label(_("Select font..."));
	item_select_colors = gtk_menu_item_new_with_label(_("Select colors..."));
	item_sidebar = gtk_check_menu_item_new_with_label(_("Show terminal sidebar"));
	item_show_tab_bar = gtk_menu_item_new_with_label(_("Tab bar"));
	item_show_tab_bar_always = gtk_radio_menu_item_new_with_label(NULL, _("Always"));
	item_show_tab_bar_multiple = gtk_radio_menu_item_new_with_label_from_widget(
		GTK_RADIO_MENU_ITEM(item_show_tab_bar_always), _("When there are multiple visible terminals"));
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
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), item_copy);
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), item_paste);
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), item_select_text);
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), gtk_separator_menu_item_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), item_new_tab);
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), item_pane);
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), item_tools);
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), item_open_here);
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), item_codex);
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), item_set_name);
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), item_close_tab);
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), gtk_separator_menu_item_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), item_fullscreen);
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), gtk_separator_menu_item_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(sakura.menu), item_options);

	options_menu = gtk_menu_new();
	show_tab_bar_menu = gtk_menu_new();
	cursor_menu = gtk_menu_new();
	tools_menu = gtk_menu_new();
	pane_menu = gtk_menu_new();
	pane_layout_menu = gtk_menu_new();
	sakura.pane_menu = pane_menu;
	sakura.pane_layout_menu = pane_layout_item;
	sakura.pane_split_right = pane_split_right;
	sakura.pane_split_down = pane_split_down;
	sakura.pane_focus_left = pane_focus_left;
	sakura.pane_focus_right = pane_focus_right;
	sakura.pane_focus_up = pane_focus_up;
	sakura.pane_focus_down = pane_focus_down;
	sakura.pane_close = pane_close;
	sakura.pane_equalize = pane_equalize;
	sakura.pane_zoom = pane_zoom;
	codex_menu = gtk_menu_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(tools_menu), item_gitui);
	gtk_menu_shell_append(GTK_MENU_SHELL(tools_menu), item_git_cola);
	gtk_menu_shell_append(GTK_MENU_SHELL(tools_menu), item_gh_dash);
	gtk_menu_shell_append(GTK_MENU_SHELL(tools_menu), item_open_pr);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_tools), tools_menu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_open_here), sakura_open_here_menu_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(codex_menu), item_new_codex);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_new_codex_reasoning),
	                          sakura_codex_reasoning_menu_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(codex_menu), item_new_codex_reasoning);
	gtk_menu_shell_append(GTK_MENU_SHELL(codex_menu), item_resume_codex);
	gtk_menu_shell_append(GTK_MENU_SHELL(codex_menu), item_attach_codex);
	gtk_menu_shell_append(GTK_MENU_SHELL(codex_menu), item_rename_codex);
	gtk_menu_shell_append(GTK_MENU_SHELL(codex_menu), item_refresh_codex_name);
	gtk_menu_shell_append(GTK_MENU_SHELL(codex_menu), item_codex_status);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_codex), codex_menu);
	gtk_menu_shell_append(GTK_MENU_SHELL(pane_menu), pane_split_right);
	gtk_menu_shell_append(GTK_MENU_SHELL(pane_menu), pane_split_down);
	gtk_menu_shell_append(GTK_MENU_SHELL(pane_menu), gtk_separator_menu_item_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(pane_menu), pane_focus_left);
	gtk_menu_shell_append(GTK_MENU_SHELL(pane_menu), pane_focus_right);
	gtk_menu_shell_append(GTK_MENU_SHELL(pane_menu), pane_focus_up);
	gtk_menu_shell_append(GTK_MENU_SHELL(pane_menu), pane_focus_down);
	gtk_menu_shell_append(GTK_MENU_SHELL(pane_menu), gtk_separator_menu_item_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(pane_menu), pane_close);
	gtk_menu_shell_append(GTK_MENU_SHELL(pane_menu), pane_equalize);
	gtk_menu_shell_append(GTK_MENU_SHELL(pane_menu), pane_zoom);
	gtk_menu_shell_append(GTK_MENU_SHELL(pane_layout_menu), pane_two_columns);
	gtk_menu_shell_append(GTK_MENU_SHELL(pane_layout_menu), pane_two_rows);
	gtk_menu_shell_append(GTK_MENU_SHELL(pane_layout_menu), pane_grid_2x2);
	gtk_menu_shell_append(GTK_MENU_SHELL(pane_layout_menu), pane_main_stack);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(pane_layout_item), pane_layout_menu);
	gtk_menu_shell_append(GTK_MENU_SHELL(pane_menu), gtk_separator_menu_item_new());
	gtk_menu_shell_append(GTK_MENU_SHELL(pane_menu), pane_layout_item);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_pane), pane_menu);
	g_signal_connect(pane_menu, "show", G_CALLBACK(sakura_pane_menu_show_cb), NULL);
	g_signal_connect(pane_two_columns, "activate",
	                 G_CALLBACK(sakura_apply_layout_preset_cb),
	                 GINT_TO_POINTER(SAKURA_LAYOUT_PRESET_TWO_COLUMNS));
	g_signal_connect(pane_two_rows, "activate",
	                 G_CALLBACK(sakura_apply_layout_preset_cb),
	                 GINT_TO_POINTER(SAKURA_LAYOUT_PRESET_TWO_ROWS));
	g_signal_connect(pane_grid_2x2, "activate",
	                 G_CALLBACK(sakura_apply_layout_preset_cb),
	                 GINT_TO_POINTER(SAKURA_LAYOUT_PRESET_GRID_2X2));
	g_signal_connect(pane_main_stack, "activate",
	                 G_CALLBACK(sakura_apply_layout_preset_cb),
	                 GINT_TO_POINTER(SAKURA_LAYOUT_PRESET_MAIN_STACK));

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
	g_signal_connect(G_OBJECT(pane_split_right), "activate", G_CALLBACK(sakura_split_current_cb),
	                 GINT_TO_POINTER(SAKURA_SPLIT_RIGHT));
	g_signal_connect(G_OBJECT(pane_split_down), "activate", G_CALLBACK(sakura_split_current_cb),
	                 GINT_TO_POINTER(SAKURA_SPLIT_DOWN));
	g_signal_connect(G_OBJECT(pane_focus_left), "activate", G_CALLBACK(sakura_focus_direction_cb),
	                 GINT_TO_POINTER(SAKURA_FOCUS_LEFT));
	g_signal_connect(G_OBJECT(pane_focus_right), "activate", G_CALLBACK(sakura_focus_direction_cb),
	                 GINT_TO_POINTER(SAKURA_FOCUS_RIGHT));
	g_signal_connect(G_OBJECT(pane_focus_up), "activate", G_CALLBACK(sakura_focus_direction_cb),
	                 GINT_TO_POINTER(SAKURA_FOCUS_UP));
	g_signal_connect(G_OBJECT(pane_focus_down), "activate", G_CALLBACK(sakura_focus_direction_cb),
	                 GINT_TO_POINTER(SAKURA_FOCUS_DOWN));
	g_signal_connect(G_OBJECT(pane_close), "activate", G_CALLBACK(sakura_close_tab_cb), NULL);
	g_signal_connect(G_OBJECT(pane_equalize), "activate", G_CALLBACK(sakura_equalize_current_cb), NULL);
	g_signal_connect(G_OBJECT(pane_zoom), "activate", G_CALLBACK(sakura_toggle_zoom_current_cb), NULL);
	g_signal_connect(G_OBJECT(item_gitui), "activate", G_CALLBACK(sakura_new_tool_cb),
	                 GINT_TO_POINTER(SAKURA_TOOL_GITUI));
	g_signal_connect(G_OBJECT(item_git_cola), "activate", G_CALLBACK(sakura_new_tool_cb),
	                 GINT_TO_POINTER(SAKURA_TOOL_GIT_COLA));
	g_signal_connect(G_OBJECT(item_gh_dash), "activate", G_CALLBACK(sakura_new_tool_cb),
	                 GINT_TO_POINTER(SAKURA_TOOL_GH_DASH));
	g_signal_connect(G_OBJECT(item_open_pr), "activate", G_CALLBACK(sakura_open_pr_cb), NULL);
	g_signal_connect(G_OBJECT(item_new_codex), "activate", G_CALLBACK(sakura_new_codex_cb), NULL);
	g_signal_connect(G_OBJECT(item_resume_codex), "activate", G_CALLBACK(sakura_resume_codex_cb), NULL);
	g_signal_connect(G_OBJECT(item_attach_codex), "activate", G_CALLBACK(sakura_attach_codex_cb), NULL);
	g_signal_connect(G_OBJECT(item_rename_codex), "activate", G_CALLBACK(sakura_rename_codex_session_cb), NULL);
	g_signal_connect(G_OBJECT(item_refresh_codex_name), "activate", G_CALLBACK(sakura_refresh_codex_name_cb), NULL);
	g_signal_connect(G_OBJECT(item_codex_status), "activate", G_CALLBACK(sakura_codex_tracking_status_cb), NULL);
	g_signal_connect(G_OBJECT(item_install_codex), "activate", G_CALLBACK(sakura_install_codex_hook_cb), NULL);
	g_signal_connect(G_OBJECT(options_menu), "show",
	                 G_CALLBACK(sakura_codex_tracking_menu_show_cb), item_install_codex);
	sakura_codex_tracking_menu_update(item_install_codex);
	g_signal_connect(G_OBJECT(item_set_name), "activate", G_CALLBACK(sakura_set_name_dialog_cb), NULL);
	g_signal_connect(G_OBJECT(item_close_tab), "activate", G_CALLBACK(sakura_close_tab_cb), NULL);
	g_signal_connect(G_OBJECT(item_select_font), "activate", G_CALLBACK(sakura_font_dialog_cb), NULL);
	g_signal_connect(G_OBJECT(item_copy), "activate", G_CALLBACK(sakura_copy_cb), NULL);
	g_signal_connect(G_OBJECT(item_paste), "activate", G_CALLBACK(sakura_paste_cb), NULL);
	g_signal_connect(G_OBJECT(item_select_text), "activate", G_CALLBACK(sakura_select_text_cb), NULL);
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


void
sakura_destroy()
{
	GList *group;
	SakuraTab *tab;
	guint index;

	if (sakura.session_shutting_down)
		return;

	/* A hook can be detected just before the window closes. Process any
	 * pending tracking record and flush the debounced save before tearing down
	 * the notebook, otherwise the freshly learned Codex ID is lost. */
	if (!sakura.session_shutting_down && sakura.sessionfile != NULL &&
	    !option_new_window && !sakura.dont_save) {
		sakura_codex_tracking_poll_cb(NULL);
		if (sakura.session_ready)
			sakura_session_flush();
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
	if (sakura.cwd_tracking_source_id != 0) {
		g_source_remove(sakura.cwd_tracking_source_id);
		sakura.cwd_tracking_source_id = 0;
	}
	if (sakura.sidebar_spinner_source_id != 0) {
		g_source_remove(sakura.sidebar_spinner_source_id);
		sakura.sidebar_spinner_source_id = 0;
	}
	sakura_sidebar_cancel_pending_selection();

	/* The destroy signal runs after GTK has begun tearing down the main
	 * window. Do not call notebook/VTE widget APIs here: their native windows
	 * may already be gone. Release the application-owned records directly and
	 * let GTK finish destroying the widget tree. */
	if (sakura.pages != NULL) {
		for (index = 0; index < sakura.pages->len; index++)
			sakura_page_free(g_ptr_array_index(sakura.pages, index));
		g_ptr_array_set_size(sakura.pages, 0);
	}
	if (sakura.tabs != NULL) {
		for (index = 0; index < sakura.tabs->len; index++) {
			tab = g_ptr_array_index(sakura.tabs, index);
			if (tab != NULL) {
				sakura_remove_history_file(tab);
				sakura_tab_free(tab);
			}
		}
		g_ptr_array_set_size(sakura.tabs, 0);
	}
	if (sakura.panes != NULL)
		g_ptr_array_set_size(sakura.panes, 0);
	for (group = sakura.sidebar_groups; group != NULL; group = group->next)
		sakura_sidebar_free_node(group->data);
	g_list_free(sakura.sidebar_groups);
	sakura.sidebar_groups = NULL;
	sakura.sidebar_root = NULL;
	sakura_codex_name_helper_shutdown();

	g_key_file_free(sakura.cfg);
	if (sakura.session_cfg != NULL)
		g_key_file_free(sakura.session_cfg);
	sakura_session_snapshot_free(sakura.session_snapshot);
	sakura.session_snapshot = NULL;

	pango_font_description_free(sakura.font);

	free(sakura.configfile);
	g_free(sakura.sessionfile);
	if (sakura.session_lock_fd >= 0)
		close(sakura.session_lock_fd);
	g_free(sakura.session_lock_path);
	g_free(sakura.codex_tracking_dir);
	g_free(sakura.history_dir);
	g_free(sakura.bash_history_rc);
	g_free(sakura.editor_command);
	g_clear_pointer(&sakura.tabs, g_ptr_array_unref);
	g_clear_pointer(&sakura.pages, g_ptr_array_unref);
	g_clear_pointer(&sakura.panes, g_ptr_array_unref);

	gtk_main_quit();
}


static void
sakura_show_scrollbar (void)
{
	gint page;
	struct sakura_tab *sk_tab;
	int i;

	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	sk_tab = sakura.active_tab != NULL ? sakura.active_tab : sakura_tab_at_page(page);

	if (!g_key_file_get_boolean(sakura.cfg, cfg_group, "scrollbar", NULL)) {
		sakura.show_scrollbar = true;
		sakura_set_config_boolean("scrollbar", TRUE);
	} else {
		sakura.show_scrollbar = false;
		sakura_set_config_boolean("scrollbar", FALSE);
	}

	/* Toggle/Untoggle the scrollbar for all tabs */
	for (i = sakura.panes != NULL ? (gint)sakura.panes->len - 1 : -1; i >= 0; i--) {
		sk_tab = g_ptr_array_index(sakura.panes, i);
		if (sk_tab == NULL)
			continue;
		if (!sakura.show_scrollbar)
			gtk_widget_hide(sk_tab->scrollbar);
		else
			gtk_widget_show(sk_tab->scrollbar);
	}
	sakura_set_size();
}


void
sakura_update_geometry_hints(void)
{
	SakuraPage *page;
	SakuraTab *tab;
	GdkGeometry hints;
	gint page_index;
	gint char_width, char_height;
	gboolean split;
	guint index;

	if (sakura.main_window == NULL || sakura.notebook == NULL)
		return;
	page_index = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	page = sakura_page_at_page(page_index);
	tab = page != NULL ? page->active_tab : NULL;
	if (tab == NULL && page_index >= 0)
		tab = sakura_tab_at_page(page_index);
	if (tab == NULL || tab->vte == NULL)
		return;

	split = page != NULL && page->panes != NULL && page->panes->len > 1;
	if (split) {
		/* A split is sized by its child panes rather than by one terminal's
		 * character grid.  Remove the single-terminal increments so dividers
		 * can land at arbitrary pixel positions. */
		gtk_window_set_geometry_hints(GTK_WINDOW(sakura.main_window), NULL, NULL, 0);
		char_width = vte_terminal_get_char_width(VTE_TERMINAL(tab->vte));
		char_height = vte_terminal_get_char_height(VTE_TERMINAL(tab->vte));
		char_height = (gint)(sakura.line_height * char_height);
		for (index = 0; index < page->panes->len; index++) {
			SakuraTab *pane = g_ptr_array_index(page->panes, index);
			if (pane != NULL && pane->hbox != NULL)
				gtk_widget_set_size_request(pane->hbox,
				                            char_width * SAKURA_DEFAULT_MIN_WIDTH_CHARS,
				                            char_height * SAKURA_DEFAULT_MIN_HEIGHT_CHARS);
		}
		return;
	}

	if (tab->hbox != NULL)
		gtk_widget_set_size_request(tab->hbox, -1, -1);
	char_width = vte_terminal_get_char_width(VTE_TERMINAL(tab->vte));
	char_height = vte_terminal_get_char_height(VTE_TERMINAL(tab->vte));
	hints.base_width = char_width;
	hints.base_height = char_height;
	hints.min_width = char_width * SAKURA_DEFAULT_MIN_WIDTH_CHARS;
	hints.min_height = char_height * SAKURA_DEFAULT_MIN_HEIGHT_CHARS;
	hints.width_inc = char_width;
	hints.height_inc = char_height;
	gtk_window_set_geometry_hints(GTK_WINDOW(sakura.main_window), tab->vte,
	                              &hints,
	                              GDK_HINT_RESIZE_INC | GDK_HINT_MIN_SIZE |
	                              GDK_HINT_BASE_SIZE);
}


void
sakura_set_size (void)
{
	struct sakura_tab *sk_tab;
	struct sakura_tab *scroll_tab;
	SakuraPage *current_page;
	gint pad_x, pad_y;
	gint char_width, char_height;
	gint min_width, natural_width;
	gint page;

	page = gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook));
	current_page = sakura_page_at_page(page);
	sk_tab = current_page != NULL ? current_page->active_tab : NULL;
	if (sk_tab == NULL && page >= 0)
		sk_tab = sakura_tab_at_page(page);
	if (sk_tab == NULL)
		sk_tab = sakura_tab_at_page(0);
	if (sk_tab == NULL || sk_tab->vte == NULL)
		return;
	sakura_update_geometry_hints();
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

	if (sakura.show_tab_bar == SHOW_TAB_BAR_ALWAYS ||
	    (sakura.show_tab_bar == SHOW_TAB_BAR_MULTIPLE &&
	     sakura_tab_bar_visible_count() > 1)) {

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

	scroll_tab = page >= 0 ? sakura_tab_at_page(page) : NULL;
	if (scroll_tab == NULL)
		scroll_tab = sk_tab;

	gtk_widget_get_preferred_width(scroll_tab->scrollbar, &min_width, &natural_width);
	//SAY("SCROLLBAR min width %d natural width %d", min_width, natural_width);
	if (sakura.show_scrollbar) {
		sakura.width += min_width;
	}

	if (sakura.sidebar_visible)
		sakura.width += sakura.sidebar_width;

	gtk_window_resize(GTK_WINDOW(sakura.main_window), sakura.width, sakura.height);
}


void
sakura_set_font()
{
	struct sakura_tab *sk_tab;
	int i;

	/* Set the font for all tabs */
	for (i = sakura.panes != NULL ? (gint)sakura.panes->len - 1 : -1; i >= 0; i--) {
		sk_tab = g_ptr_array_index(sakura.panes, i);
		if (sk_tab == NULL)
			continue;
		vte_terminal_set_font(VTE_TERMINAL(sk_tab->vte), sakura.font);
		vte_terminal_set_cell_height_scale(VTE_TERMINAL(sk_tab->vte), sakura.line_height);
	}
}

/* Set colorset when colosert keybinding is used */
static void
sakura_set_colorset (int cs)
{
	struct sakura_tab *sk_tab;

	if (cs < 0 || cs >= NUM_COLORSETS)
		return;

	sk_tab = sakura.active_tab != NULL ? sakura.active_tab :
	         sakura_tab_at_page(gtk_notebook_get_current_page(GTK_NOTEBOOK(sakura.notebook)));
	if (sk_tab == NULL)
		return;
	sk_tab->colorset = cs;

	sakura_set_config_integer("last_colorset", sk_tab->colorset+1);

	sakura_set_colors();
}


/* Set the terminal colors for all notebook tabs */
void
sakura_set_colors ()
{
	int i;
	struct sakura_tab *sk_tab;
	gdouble window_opacity = 1.0;

	for (i = sakura.panes != NULL ? (gint)sakura.panes->len - 1 : -1; i >= 0; i--) {
		sk_tab = g_ptr_array_index(sakura.panes, i);
		if (sk_tab == NULL)
			continue;
		window_opacity = sakura.backcolors[sk_tab->colorset].alpha;

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
	gtk_widget_set_opacity(sakura.main_window, window_opacity);
}


static void
sakura_add_tab (void)
{
	sakura_add_tab_with_options(NULL, NULL, NULL, FALSE, SAKURA_TAB_SHELL,
                            SAKURA_TOOL_NONE, NULL, NULL, NULL, NULL, NULL);
}




void
sakura_add_tab_with_options (const gchar *restore_cwd,
                              struct sakura_sidebar_node *restore_parent,
                              const gchar *restore_title,
                              gboolean restore_title_set,
                              SakuraTabKind restore_kind,
                              SakuraToolKind restore_tool,
                              const gchar *restore_codex_session_id,
                              const gchar *restore_codex_session_name,
                              const gchar *restore_codex_reasoning_effort,
                              const gchar *restore_tool_target,
                              const gchar *restore_terminal_id)
{
	SakuraTabLaunchConfig launch_config = {
			.execute_command = option_execute,
			.xterm_args = option_xterm_args,
			.login_shell = option_login,
			.hold = option_hold,
			.execute_on_existing_tabs = sakura.first_run,
			.target_page = NULL,
			.target_layout = NULL,
			.target_ratio = 0.5,
			.split_direction = SAKURA_SPLIT_RIGHT
		};

	sakura_tab_add_with_options(restore_cwd, restore_parent, restore_title,
	                            restore_title_set, restore_kind, restore_tool,
	                            restore_codex_session_id, restore_codex_session_name,
	                            restore_codex_reasoning_effort,
	                            restore_tool_target, restore_terminal_id,
	                            &launch_config);
}


/* Delete the notebook tab passed as a parameter */

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
	char** dst = spawn_argv + 1;
	char** src = sakura.orig_argv != NULL ? sakura.orig_argv + 1 : NULL;
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
void
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

void
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


static guint
sakura_get_keybind_default(const gchar *key, guint default_value)
{
	if (!g_key_file_has_key(sakura.cfg, cfg_group, key, NULL))
		sakura_set_keybind(key, default_value);
	return sakura_get_keybind(key);
}


static gint
sakura_get_accelerator_default(const gchar *key, gint default_value)
{
	if (!g_key_file_has_key(sakura.cfg, cfg_group, key, NULL))
		sakura_set_config_integer(key, default_value);
	return g_key_file_get_integer(sakura.cfg, cfg_group, key, NULL);
}


static gboolean
sakura_key_matches(GdkEventKey *event, gint accelerator, gint key)
{
	GdkModifierType accel_mask;

	if (event == NULL)
		return FALSE;
	accel_mask = gtk_accelerator_get_default_mod_mask();
	return (event->state & accel_mask) == (accelerator & accel_mask) &&
	       event->hardware_keycode == sakura_tokeycode(key);
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
	gboolean preserve_failed_session = FALSE;

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
		                            SAKURA_TAB_CODEX, SAKURA_TOOL_NONE,
		                            option_codex_session, NULL, NULL, NULL, NULL);
	} else if (option_new_session || option_new_window || option_ntabs > 1) {
		for (i=0; i<option_ntabs; i++)
			sakura_add_tab();
	} else if (!sakura_workspace_restore_snapshot(sakura.session_snapshot)) {
		if (sakura.sessionfile != NULL &&
		    g_file_test(sakura.sessionfile, G_FILE_TEST_IS_REGULAR)) {
			preserve_failed_session = TRUE;
			sakura.session_restore_failed = TRUE;
			SAY("Could not restore the saved session; preserving %s",
			    sakura.sessionfile);
		}
		for (i=0; i<option_ntabs; i++)
			sakura_add_tab();
	}
	sakura.session_restoring = FALSE;
	sakura.session_ready = TRUE;
	if (!option_new_window && !preserve_failed_session) {
		sakura_session_mark_dirty();
		sakura_session_flush();
	}
	/* Start maximized after session restoration and initial layout. This keeps
	 * the default launch state consistent and prevents restored sizing from
	 * overriding it. Fullscreen remains the stronger window state. */
	if (!option_fullscreen)
		gtk_window_maximize(GTK_WINDOW(sakura.main_window));

	/* Post init stuff */
	sakura.first_run=false;
	g_strfreev(option_xterm_args);

	sakura_sanitize_working_directory();

	gtk_main();

	return 0;
}
