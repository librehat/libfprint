/*
 * Shared library loader for libfprint
 * Copyright (C) 2019 Marco Trevisan <marco.trevisan@canonical.com>
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


#include <gmodule.h>
#include "shared_loader.h"
#include "fp_internal.h"

#define DRIVERS_PATH "/opt/dev/GNOME/fpi-drivers"

static GSList *shared_drivers = NULL;
static GList *shared_modules = NULL;

void
fpi_shared_drivers_register (void)
{
	g_autofree char *path = NULL;
	GModule *module = NULL;

	/* go for each *.so file... */

	path = g_module_build_path (DRIVERS_PATH, "fp-driver");
	g_print("Opening driver %s, we suppport modules %d\n",path,g_module_supported());

	module = g_module_open (path, G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
	g_print("Module is %p, error %s\n",module, g_module_error());

	if (module) {
		// union fp_shared_driver* shared_driver = NULL;
		struct fp_driver *driver = NULL;
		gpointer symbol;

		// if (g_module_symbol (module, "fp_shared_driver", &symbol)) {
		// 	shared_driver = g_new0 (union fp_shared_driver, 1);
		// 	shared_driver->type = DRIVER_PRIMITIVE;
		// 	shared_driver->primitive = (struct fp_driver *) symbol;
		// 	shared_driver->primitive->id = ++SHARED_DRIVER_ID;
		// }
		// else if (g_module_symbol (module, "fp_shared_driver_img", &symbol)) {
		// 	shared_driver = g_new0 (union fp_shared_driver, 1);
		// 	shared_driver->type = DRIVER_IMAGING;
		// 	shared_driver->image = (struct fp_img_driver *) symbol;
		// 	shared_driver->image->driver.id = ++SHARED_DRIVER_ID;
		// }

		if (g_module_symbol (module, "fp_shared_driver", &symbol)) {
			struct fp_driver **primitive_driver = symbol;

			driver = *primitive_driver;
			// shared_driver = g_new0 (union fp_shared_driver, 1);
			// shared_driver->type = DRIVER_PRIMITIVE;
			// shared_driver->primitive = (struct fp_driver *) symbol;
			// shared_driver->primitive->id = ++SHARED_DRIVER_ID;
			g_print("Found symbol %p, old DRIVER\n",symbol);
		}
		else if (g_module_symbol (module, "fp_shared_driver_img", &symbol)) {
			struct fp_img_driver **img_driver = symbol;

			fpi_img_driver_setup (*img_driver);
			driver = &((*img_driver)->driver);
			// shared_driver = g_new0 (union fp_shared_driver, 1);
			// shared_driver->type = DRIVER_IMAGING;
			// shared_driver->image = (struct fp_img_driver *) symbol;
			// shared_driver->image->driver.id = ++SHARED_DRIVER_ID;
			g_print("Found symbol %p, img driver\n",symbol);
		}
		else
			g_print("No symbol found...\n");

		if (driver) {
			g_print("Loading now driver %s (%s)\n",driver->name,driver->full_name);
			shared_modules = g_list_prepend (shared_modules,
				g_steal_pointer (&module));

			shared_drivers = g_slist_append (shared_drivers, driver);
		}
	}

	g_clear_pointer (&module, (GDestroyNotify) g_module_close);
}

void
fpi_shared_drivers_unregister (void)
{
	g_list_free_full (shared_modules, (GDestroyNotify) g_module_close);
	g_clear_pointer (&shared_drivers, g_slist_free);
}

// GPtrArray *
// shared_drivers_get_primitive ()
// {

// }

GSList *
fpi_shared_drivers_get (void)
{
	return shared_drivers;
}

/* Device driver definition */
// struct fp_driver primitive_loader = { 0 };
// struct fp_img_driver img_loader = { 0 };

	// /* Driver specification */
	// .driver = {
	// 	   .id = VFS0050_ID,
	// 	   .name = FP_COMPONENT,
	// 	   .full_name = "Validity VFS0050",
	// 	   .id_table = id_table,
	// 	   .scan_type = FP_SCAN_TYPE_SWIPE,
	// 	   },

	// /* Image specification */
	// .flags = 0,
	// .img_width = VFS_IMAGE_WIDTH,
	// .img_height = -1,
	// .bz3_threshold = 24,

	// /* Routine specification */
	// .open = dev_open,
	// .close = dev_close,
	// .activate = dev_activate,
	// .deactivate = dev_deactivate,
// };
