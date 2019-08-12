/*
 * Example fingerprint verification program, which verifies the right index
 * finger which has been previously enrolled to disk.
 * Copyright (C) 2007 Daniel Drake <dsd@gentoo.org>
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

#include <stdio.h>
#include <libfprint/fprint.h>

#include "libfprint/fp-print.h"
#include "storage.h"

typedef struct _VerifyData {
	GMainLoop *loop;
	int ret_value;
} VerifyData;

static void
verify_data_free (VerifyData *verify_data)
{
	g_main_loop_unref (verify_data->loop);
	g_free (verify_data);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC (VerifyData, verify_data_free)

FpDevice *discover_device (GPtrArray *devices)
{
	FpDevice *dev;
	if (!devices->len)
		return NULL;

	dev = g_ptr_array_index (devices, 0);
	printf("Found device claimed by %s driver\n", fp_device_get_driver (dev));
	return dev;
}

static void
on_device_closed (FpDevice *dev, GAsyncResult *res, void *user_data) {
	VerifyData *verify_data = user_data;
	g_autoptr(GError) error = NULL;

	fp_device_close_finish (dev, res, &error);

	if (error)
		g_warning ("Failed closing device %s\n", error->message);

	g_main_loop_quit (verify_data->loop);
}

static void start_verification (FpDevice *dev, VerifyData *verify_data);

static void
on_verify_completed (FpDevice *dev, GAsyncResult *res, void *user_data)
{
	VerifyData *verify_data = user_data;
	g_autoptr(FpPrint) print = NULL;
	g_autoptr(GError) error = NULL;
	char buffer[20];
	gboolean match;

	if (!fp_device_verify_finish (dev, res, &match, &print, &error)) {
		g_warning ("Failed to verify print: %s", error->message);
		g_main_loop_quit (verify_data->loop);
		return;
	}

	if (match) {
		g_print ("MATCH!\n");
		if (fp_device_supports_capture (dev) &&
		    print_image_save (print, "verify.pgm")) {
			g_print("Print image saved as verify.pgm");
		}

		verify_data->ret_value = EXIT_SUCCESS;
	} else {
		g_print ("NO MATCH!\n");
		verify_data->ret_value = EXIT_FAILURE;
	}

	g_print ("Verify again? [Y/n]? ");
	if (fgets (buffer, sizeof (buffer), stdin) &&
	    (buffer[0] == 'Y' || buffer[0] == 'y')) {
		start_verification (dev, verify_data);
		return;
	}

	fp_device_close (dev, NULL, (GAsyncReadyCallback) on_device_closed,
			 verify_data);
}

static void
on_list_completed (FpDevice *dev, GAsyncResult *res, gpointer user_data)
{
	VerifyData *verify_data = user_data;
	g_autoptr(GPtrArray) prints = NULL;
	g_autoptr(GError) error = NULL;

	prints = fp_device_list_prints_finish (dev, res, &error);

	if (!error) {
		FpPrint *verify_print = NULL;
		guint i;

		if (!prints->len)
			g_warning ("No prints saved on device");

		for (i = 0; i < prints->len; ++i) {
			FpPrint *print = prints->pdata[i];

			if (fp_print_get_finger (print) == FP_FINGER_RIGHT_INDEX &&
			    g_strcmp0 (fp_print_get_username (print), g_get_user_name ()) == 0) {
				if (!verify_print ||
				    (g_date_compare (fp_print_get_enroll_date (print),
				                     fp_print_get_enroll_date (verify_print)) >= 0))
					verify_print = print;
			}
		}

		if (!verify_print) {
			g_warning ("Did you remember to enroll your right index "
				   "finger first?");
			g_main_loop_quit (verify_data->loop);
			return;
		}

		g_debug ("Comparing print with %s",
		         fp_print_get_description (verify_print));

		g_print ("Print loaded. Time to verify!\n");
		fp_device_verify (dev, verify_print, NULL,
				  (GAsyncReadyCallback) on_verify_completed,
				  verify_data);
	} else {
		g_warning ("Loading prints failed with error %s", error->message);
		g_main_loop_quit (verify_data->loop);
	}
}

static void
start_verification (FpDevice *dev, VerifyData *verify_data)
{
	if (fp_device_has_storage (dev)) {
		g_print ("Creating finger template, using device storage...\n");
		fp_device_list_prints (dev, NULL,
				       (GAsyncReadyCallback) on_list_completed,
				        verify_data);
	} else {
		g_print ("Loading previously enrolled right index finger data...\n");
		g_autoptr(FpPrint) verify_print;

		verify_print = print_data_load (dev, FP_FINGER_RIGHT_INDEX);

		if (!verify_print) {
			g_warning ("Failed to load fingerprint data");
			g_warning ("Did you remember to enroll your right index "
				   "finger first?");
			g_main_loop_quit (verify_data->loop);
			return;
		}

		g_print ("Print loaded. Time to verify!\n");
		fp_device_verify (dev, verify_print, NULL,
				  (GAsyncReadyCallback) on_verify_completed,
				  verify_data);
	}
}

static void
on_device_opened (FpDevice *dev, GAsyncResult *res, void *user_data)
{
	VerifyData *verify_data = user_data;
	g_autoptr(GError) error = NULL;

	if (!fp_device_open_finish (dev, res, &error)) {
		g_warning ("Failed to open device: %s", error->message);
		g_main_loop_quit (verify_data->loop);
		return;
	}

	g_print ("Opened device. ");

	start_verification (dev, verify_data);
}

int main(void)
{
	g_autoptr (FpContext) ctx = NULL;
	g_autoptr (VerifyData) verify_data = NULL;
	GPtrArray *devices;
	FpDevice *dev;

	setenv ("G_MESSAGES_DEBUG", "all", 0);
	setenv ("LIBUSB_DEBUG", "3", 0);

	ctx = fp_context_new ();

	devices = fp_context_get_devices (ctx);
	if (!devices) {
		g_warning("Impossible to get devices");
		return EXIT_FAILURE;
	}

	dev = discover_device (devices);
	if (!dev) {
		g_warning("No devices detected.");
		return EXIT_FAILURE;
	}

	verify_data = g_new0 (VerifyData, 1);
	verify_data->ret_value = EXIT_FAILURE;
	verify_data->loop = g_main_loop_new (NULL, FALSE);

	fp_device_open (dev, NULL, (GAsyncReadyCallback) on_device_opened,
			verify_data);

	g_main_loop_run (verify_data->loop);

	return verify_data->ret_value;
}

