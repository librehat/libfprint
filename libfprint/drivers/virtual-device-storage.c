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
dev_list (FpDevice *dev)
{
  FpDeviceVirtualDevice *vdev = FP_DEVICE_VIRTUAL_DEVICE (dev);
  g_autoptr(GPtrArray) prints_list = NULL;
  guint i;

  prints_list = g_ptr_array_new_full (vdev->prints_storage->len, NULL);
  for (i = 0; i < vdev->prints_storage->len; ++i)
    g_ptr_array_add (prints_list, vdev->prints_storage->pdata[i]);

  /* FIXME: Ideally here we would just return a reffed GPtrArray, but, it looks
   * like we've problems with the introspection...
   * fpi_device_list_complete (dev, g_ptr_array_ref (vdev->prints_storage), NULL);
   * As the array is actually free'd:
      Thread 1 "python3" hit Breakpoint 44, g_ptr_array_free (array=0xbe4c00, free_segment=1)
          at ../../glib/glib/garray.c:1426
      1426	{
      (gdb) print *(GRealPtrArray*)array
      $6 = {pdata = 0xbea350, len = 3, alloc = 16, ref_count = 2, 
        element_free_func = 0x7ffff71caee0 <g_object_unref>}
      (gdb) bt
      #0  g_ptr_array_free (array=0xbe4c00, free_segment=1) at ../../glib/glib/garray.c:1426
      #1  0x00007ffff736747f in pygi_marshal_cleanup_args_to_py_marshal_success (
          state=state@entry=0x7fffffffa410, cache=cache@entry=0xbe97b0) at gi/pygi-marshal-cleanup.c:141
      #2  0x00007ffff7365fe3 in pygi_invoke_c_callable (function_cache=0xbe97b0, state=<optimized out>, 
          py_args=<optimized out>, py_kwargs=<optimized out>) at gi/pygi-invoke.c:721
      #3  0x00007ffff735cf2c in pygi_function_cache_invoke (function_cache=<optimized out>, 
          py_args=<optimized out>, py_kwargs=<optimized out>) at gi/pygi-cache.c:863
      #4  0x00007ffff7366589 in pygi_callable_info_invoke (user_data=0x0, cache=<optimized out>, 
          kwargs=<optimized out>, py_args=<optimized out>, info=<optimized out>) at gi/pygi-invoke.c:770
      #5  _wrap_g_callable_info_invoke (self=<optimized out>, py_args=<optimized out>, kwargs=<optimized out>)
          at gi/pygi-invoke.c:770
      #6  0x00007ffff7361e30 in ?? () at /usr/include/python3.8/object.h:478
        from /opt/dev/GNOME/lib/python3.8/site-packages/gi/_gi.cpython-38-x86_64-linux-gnu.so
      #7  0x0000000000000000 in ?? ()
      (gdb)
   */
  fpi_device_list_complete (dev, g_steal_pointer (&prints_list), NULL);
  // fpi_device_list_complete (dev, g_ptr_array_ref (vdev->prints_storage), NULL);
}

static gint
compare_print (FpPrint *print_a,
               FpPrint *print_b)
{
  g_print("Comparing print %d %s\n", fp_print_get_finger (print_a), fp_print_get_username (print_a));
  if (fp_print_get_finger (print_a) != fp_print_get_finger (print_b))
    return fp_print_get_finger (print_a) - fp_print_get_finger (print_b);

  return g_strcmp0 (fp_print_get_username (print_a), fp_print_get_username (print_b));
}

static void
dev_delete (FpDevice *dev)
{
  FpDeviceVirtualDevice *vdev = FP_DEVICE_VIRTUAL_DEVICE (dev);
  GError *error = NULL;
  FpPrint *print = NULL;
  guint i;

  fpi_device_get_delete_data (dev, &print);

  fp_dbg ("Deleting print %s for user %s",
          finger_to_str (fp_print_get_finger (print)),
          fp_print_get_username (print));

  if (g_ptr_array_remove_fast (vdev->prints_storage, print))
    {
      fpi_device_delete_complete (dev, error);
      return;
    }

  if (g_ptr_array_find_with_equal_func (vdev->prints_storage, print, (GEqualFunc) compare_print, &i))
    {
      g_ptr_array_remove_index_fast (vdev->prints_storage, i);
      fpi_device_delete_complete (dev, error);
      return;
    }

  error = fpi_device_error_new (FP_DEVICE_ERROR_DATA_NOT_FOUND);
  fpi_device_delete_complete (dev, error);
}

static void
fpi_device_virtual_device_storage_init (FpDeviceVirtualDeviceStorage *self)
{
  FpDeviceVirtualDevice *vdev = FP_DEVICE_VIRTUAL_DEVICE (self);

  vdev->prints_storage = g_ptr_array_new_with_free_func (g_object_unref);
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
  dev_class->list = dev_list;
  dev_class->delete = dev_delete;
}
