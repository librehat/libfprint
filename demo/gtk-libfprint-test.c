/*
 * Example libfprint GTK+ image capture program
 * Copyright (C) 2018 Bastien Nocera <hadess@hadess.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"

#include <gtk/gtk.h>
#include <libfprint/fprint.h>

#include "loop.h"

typedef GtkApplication LibfprintDemo;
typedef GtkApplicationClass LibfprintDemoClass;

G_DEFINE_TYPE (LibfprintDemo, libfprint_demo, GTK_TYPE_APPLICATION)

typedef struct {
	GtkApplicationWindow parent_instance;

	GtkWidget *header_bar;
	GtkWidget *mode_stack;
	GtkWidget *capture_button;
	GtkWidget *capture_image;

	struct fp_dscv_dev *ddev;
	struct fp_dev *dev;
} LibfprintDemoWindow;

typedef GtkApplicationWindowClass LibfprintDemoWindowClass;

G_DEFINE_TYPE (LibfprintDemoWindow, libfprint_demo_window, GTK_TYPE_APPLICATION_WINDOW)

typedef enum {
	EMPTY_MODE,
	NOIMAGING_MODE,
	CAPTURE_MODE,
	ERROR_MODE
} LibfprintDemoMode;

static void libfprint_demo_set_mode (LibfprintDemoWindow *win,
				     LibfprintDemoMode    mode);

static void
activate_show_minutiae (GSimpleAction *action,
			GVariant      *parameter,
			gpointer       user_data)
{
	GVariant *state;

	//show_action_dialog (action);

	state = g_action_get_state (G_ACTION (action));
	g_action_change_state (G_ACTION (action), g_variant_new_boolean (!g_variant_get_boolean (state)));
	g_variant_unref (state);
}

static void
pixbuf_destroy (guchar *pixels, gpointer data)
{
	if (pixels == NULL)
		return;
	g_free (pixels);
}

static unsigned char *
img_to_rgbdata (struct fp_img *img,
		int            width,
		int            height)
{
	int size = width * height;
	unsigned char *imgdata = fp_img_get_data (img);
	unsigned char *rgbdata = g_malloc (size * 3);
	size_t i;
	size_t rgb_offset = 0;

	for (i = 0; i < size; i++) {
		unsigned char pixel = imgdata[i];

		rgbdata[rgb_offset++] = pixel;
		rgbdata[rgb_offset++] = pixel;
		rgbdata[rgb_offset++] = pixel;
	}

	return rgbdata;
}

static GdkPixbuf *
img_to_pixbuf (struct fp_img *img)
{
	int width;
	int height;
	unsigned char *rgbdata;

	width = fp_img_get_width (img);
	height = fp_img_get_height (img);
	rgbdata = img_to_rgbdata (img, width, height);

	return gdk_pixbuf_new_from_data (rgbdata, GDK_COLORSPACE_RGB,
					 FALSE, 8, width, height,
					 width * 3, pixbuf_destroy,
					 NULL);
}

static void
dev_capture_start_cb (struct fp_dev *dev,
		      int            result,
		      struct fp_img *img,
		      void          *user_data)
{
	LibfprintDemoWindow *win = user_data;
	GdkPixbuf *pixbuf;

	//FIXME check result

	g_message ("got result");

	fp_async_capture_stop (dev, NULL, NULL);

	pixbuf = img_to_pixbuf (img);
	//FIXME error checking
	//FIXME add minutia plotting
	gtk_image_set_from_pixbuf (GTK_IMAGE (win->capture_image), pixbuf);
}

static void
dev_open_cb(struct fp_dev *dev, int status, void *user_data)
{
	LibfprintDemoWindow *win = user_data;
	int r;

	//FIXME check status

	win->dev = dev;
	//FIXME unconditional????
	r = fp_async_capture_start (win->dev, FALSE, dev_capture_start_cb, user_data);
	if (r < 0) {
		g_warning ("fp_async_capture_start failed: %d", r);
		libfprint_demo_set_mode (win, ERROR_MODE);
		return;
	}
}

static void
activate_capture (GSimpleAction *action,
		  GVariant      *parameter,
		  gpointer       user_data)
{
	LibfprintDemoWindow *win = user_data;
	int r;

	//FIXME switch to spinner

	r = fp_async_dev_open (win->ddev, dev_open_cb, user_data);
	if (r < 0) {
		g_warning ("fp_async_dev_open failed: %d", r);
		libfprint_demo_set_mode (win, ERROR_MODE);
		return;
	}
}

static void
activate_quit (GSimpleAction *action,
	       GVariant      *parameter,
	       gpointer       user_data)
{
	GtkApplication *app = user_data;
	GtkWidget *win;
	GList *list, *next;

	list = gtk_application_get_windows (app);
	while (list)
	{
		win = list->data;
		next = list->next;

		gtk_widget_destroy (GTK_WIDGET (win));

		list = next;
	}
}

static void
change_show_minutiae_state (GSimpleAction *action,
			    GVariant      *state,
			    gpointer       user_data)
{
	g_simple_action_set_state (action, state);
}

static GActionEntry app_entries[] = {
	{ "quit", activate_quit, NULL, NULL, NULL },
};

static GActionEntry win_entries[] = {
	{ "show-minutiae", activate_show_minutiae, NULL, "false", change_show_minutiae_state },
	{ "capture", activate_capture, NULL, NULL, NULL }
};

static void
startup (GApplication *app)
{
	GtkBuilder *builder;
	GMenuModel *appmenu;
	GMenuModel *menubar;

	G_APPLICATION_CLASS (libfprint_demo_parent_class)->startup (app);

	builder = gtk_builder_new ();
	gtk_builder_add_from_resource (builder, "/application_demo/menus.ui", NULL);

	appmenu = (GMenuModel *)gtk_builder_get_object (builder, "appmenu");
	menubar = (GMenuModel *)gtk_builder_get_object (builder, "menubar");

	gtk_application_set_app_menu (GTK_APPLICATION (app), appmenu);
	gtk_application_set_menubar (GTK_APPLICATION (app), menubar);

	g_object_unref (builder);
}

static void
activate (GApplication *app)
{
	LibfprintDemoWindow *window;

	window = g_object_new (libfprint_demo_window_get_type (),
			       "application", app,
			       NULL);
	gtk_widget_show (GTK_WIDGET (window));
}

static void
libfprint_demo_set_mode (LibfprintDemoWindow *win,
			 LibfprintDemoMode    mode)
{
	struct fp_driver *drv;
	char *title;

	switch (mode) {
	case EMPTY_MODE:
		gtk_stack_set_visible_child_name (GTK_STACK (win->mode_stack), "empty-mode");
		gtk_widget_set_sensitive (win->capture_button, FALSE);
		break;
	case NOIMAGING_MODE:
		gtk_stack_set_visible_child_name (GTK_STACK (win->mode_stack), "noimaging-mode");
		gtk_widget_set_sensitive (win->capture_button, FALSE);
		break;
	case CAPTURE_MODE:
		gtk_stack_set_visible_child_name (GTK_STACK (win->mode_stack), "capture-mode");
		gtk_widget_set_sensitive (win->capture_button, TRUE);

		drv = fp_dscv_dev_get_driver (win->ddev);
		title = g_strdup_printf ("%s Test", fp_driver_get_full_name (drv));
		gtk_header_bar_set_title (GTK_HEADER_BAR (win->header_bar), title);
		g_free (title);

		//FIXME reset spinner if needed
		break;
	case ERROR_MODE:
		gtk_stack_set_visible_child_name (GTK_STACK (win->mode_stack), "error-mode");
		gtk_widget_set_sensitive (win->capture_button, FALSE);
		break;
	default:
		g_assert_not_reached ();
	}
}

static void
libfprint_demo_init (LibfprintDemo *app)
{
	g_action_map_add_action_entries (G_ACTION_MAP (app),
					 app_entries, G_N_ELEMENTS (app_entries),
					 app);
}

static void
libfprint_demo_class_init (LibfprintDemoClass *class)
{
	GApplicationClass *app_class = G_APPLICATION_CLASS (class);

	app_class->startup = startup;
	app_class->activate = activate;
}

static void
libfprint_demo_window_init (LibfprintDemoWindow *window)
{
	struct fp_dscv_dev **discovered_devs;

	g_message ("init");

	gtk_widget_init_template (GTK_WIDGET (window));
	gtk_window_set_default_size (GTK_WINDOW (window), 700, 500);

	g_action_map_add_action_entries (G_ACTION_MAP (window),
					 win_entries, G_N_ELEMENTS (win_entries),
					 window);

	if (fp_init () < 0) {
		libfprint_demo_set_mode (window, ERROR_MODE);
		return;
	}

	setup_pollfds ();

	discovered_devs = fp_discover_devs();
	if (!discovered_devs)
		return;

	if (!fp_driver_supports_imaging(fp_dscv_dev_get_driver(discovered_devs[0]))) {
		libfprint_demo_set_mode (window, NOIMAGING_MODE);
		return;
	}

	window->ddev = discovered_devs[0];
	libfprint_demo_set_mode (window, CAPTURE_MODE);
}

static void
libfprint_demo_window_class_init (LibfprintDemoWindowClass *class)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);

	gtk_widget_class_set_template_from_resource (widget_class, "/libfprint_demo/gtk-libfprint-test.ui");
	gtk_widget_class_bind_template_child (widget_class, LibfprintDemoWindow, header_bar);
	gtk_widget_class_bind_template_child (widget_class, LibfprintDemoWindow, mode_stack);
	gtk_widget_class_bind_template_child (widget_class, LibfprintDemoWindow, capture_button);
	gtk_widget_class_bind_template_child (widget_class, LibfprintDemoWindow, capture_image);

	//FIXME setup dispose
}

int main (int argc, char **argv)
{
	GtkApplication *app;

	app = GTK_APPLICATION (g_object_new (libfprint_demo_get_type (),
					     "application-id", "org.freedesktop.libfprint.Demo",
					     "flags", G_APPLICATION_FLAGS_NONE,
					     NULL));

	return g_application_run (G_APPLICATION (app), 0, NULL);
}
