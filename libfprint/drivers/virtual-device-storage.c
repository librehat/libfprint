/*
 * Virtual driver for "simple" device debugging with storage
 *
 * Copyright (C) 2020 Bastien Nocera <hadess@hadess.net>
 * Copyright (C) 2020 Marco Trevisan <marco.trevisan@canonical.com>
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

/*
 * This is a virtual driver to debug the non-image based drivers. A small
 * python script is provided to connect to it via a socket, allowing
 * prints to registered programmatically.
 * Using this, it is possible to test libfprint and fprintd.
 */

#define FP_COMPONENT "virtual_device_storage"

#include "virtual-device-private.h"
#include "fpi-log.h"

G_DEFINE_TYPE (FpDeviceVirtualDeviceStorage, fpi_device_virtual_device_storage, fpi_device_virtual_device_get_type ())

static void
dev_identify (FpDevice *dev)
{
  GPtrArray *templates;
  FpPrint *result = NULL;
  guint i;

  fpi_device_get_identify_data (dev, &templates);

  for (i = 0; i < templates->len; i++)
    {
      FpPrint *template = g_ptr_array_index (templates, i);
      g_autoptr(GVariant) data = NULL;
      gboolean success;

      g_object_get (dev, "fpi-data", &template, NULL);
      success = g_variant_get_boolean (data);
      if (success)
        {
          result = template;
          break;
        }
    }

  if (result)
    fpi_device_identify_report (dev, result, NULL, NULL);
  fpi_device_identify_complete (dev, NULL);
}

static void
fpi_device_virtual_device_storage_init (FpDeviceVirtualDeviceStorage *self)
{
}

static const FpIdEntry driver_ids[] = {
  { .virtual_envvar = "FP_VIRTUAL_DEVICE_STORAGE" },
  { .virtual_envvar = "FP_VIRTUAL_DEVICE_IDENT" },
  { .virtual_envvar = NULL }
};

static void
fpi_device_virtual_device_storage_class_init (FpDeviceVirtualDeviceStorageClass *klass)
{
  FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);

  dev_class->id = FP_COMPONENT;
  dev_class->full_name = "Virtual device with storage and identification for debugging";
  dev_class->id_table = driver_ids;

  dev_class->identify = dev_identify;
}
