/*
 * Virtual driver for "simple" device debugging
 *
 * Copyright (C) 2019 Benjamin Berg <bberg@redhat.com>
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

#define FP_COMPONENT "virtual_device"

#include "virtual-device-private.h"
#include "fpi-log.h"

G_DEFINE_TYPE (FpDeviceVirtualDevice, fpi_device_virtual_device, FP_TYPE_DEVICE)

#define ADD_CMD_PREFIX "ADD "

static FpFinger
str_to_finger (const char *str)
{
  GEnumClass *eclass;
  GEnumValue *value;

  eclass = g_type_class_ref (FP_TYPE_FINGER);
  value = g_enum_get_value_by_nick (eclass, str);
  g_type_class_unref (eclass);

  if (value == NULL)
    return FP_FINGER_UNKNOWN;

  return value->value;
}

static const char *
finger_to_str (FpFinger finger)
{
  GEnumClass *eclass;
  GEnumValue *value;

  eclass = g_type_class_ref (FP_TYPE_FINGER);
  value = g_enum_get_value (eclass, finger);
  g_type_class_unref (eclass);

  if (value == NULL)
    return NULL;

  return value->value_nick;
}

static gboolean
parse_code (const char *str)
{
  if (g_strcmp0 (str, "1") == 0 ||
      g_strcmp0 (str, "success") == 0 ||
      g_strcmp0 (str, "SUCCESS") == 0 ||
      g_strcmp0 (str, "FPI_MATCH_SUCCESS") == 0)
    return FPI_MATCH_SUCCESS;

  return FPI_MATCH_FAIL;
}

static void
handle_command_line (FpDeviceVirtualDevice *self,
                     const char            *line)
{
  if (g_str_has_prefix (line, ADD_CMD_PREFIX))
    {
      g_auto(GStrv) elems;
      FpPrint *print;
      FpFinger finger;
      gboolean success;
      g_autofree char *description = NULL;
      char *key;

      /* Syntax: ADD <finger> <username> <error when used> */
      elems = g_strsplit (line + strlen (ADD_CMD_PREFIX), " ", 3);
      finger = str_to_finger (elems[0]);
      if (finger == FP_FINGER_UNKNOWN)
        {
          g_warning ("Unknown finger '%s'", elems[0]);
          return;
        }
      print = fp_print_new (FP_DEVICE (self));
      fp_print_set_finger (print, finger);
      fp_print_set_username (print, elems[1]);
      description = g_strdup_printf ("Fingerprint finger '%s' for user '%s'",
                                     elems[0], elems[1]);
      fp_print_set_description (print, description);
      success = parse_code (elems[2]);

      key = g_strdup_printf ("%s-%s", elems[0], elems[1]);
      g_hash_table_insert (self->pending_prints,
                           key, GINT_TO_POINTER (success));

      fp_dbg ("Added pending print %s for user %s (code: %s)",
              elems[0], elems[1], success ? "FPI_MATCH_SUCCESS" : "FPI_MATCH_FAIL");
    }
  else
    {
      g_warning ("Unhandled command sent: '%s'", line);
    }
}

static void
recv_instruction_cb (GObject      *source_object,
                     GAsyncResult *res,
                     gpointer      user_data)
{
  g_autoptr(GError) error = NULL;
  FpDeviceVirtualListener *listener = FP_DEVICE_VIRTUAL_LISTENER (source_object);
  gsize bytes;

  bytes = fp_device_virtual_listener_read_finish (listener, res, &error);
  fp_dbg ("Got instructions of length %ld\n", bytes);

  if (error)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

      g_warning ("Error receiving instruction data: %s", error->message);
      return;
    }

  if (bytes > 0)
    {
      FpDeviceVirtualDevice *self;

      self = FP_DEVICE_VIRTUAL_DEVICE (user_data);
      handle_command_line (self, (const char *) self->line);
    }

  fp_device_virtual_listener_connection_close (listener);
}

static void
recv_instruction (FpDeviceVirtualDevice *self)
{
  memset (&self->line, 0, sizeof (self->line));

  fp_device_virtual_listener_read (self->listener,
                                   self->line,
                                   sizeof (self->line),
                                   recv_instruction_cb,
                                   self);
}

static void
on_listener_connected (FpDeviceVirtualListener *listener,
                       gpointer                 user_data)
{
  FpDeviceVirtualDevice *self = FP_DEVICE_VIRTUAL_DEVICE (user_data);

  recv_instruction (self);
}

static void
dev_init (FpDevice *dev)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GCancellable) cancellable = NULL;
  g_autoptr(FpDeviceVirtualListener) listener = NULL;
  FpDeviceVirtualDevice *self = FP_DEVICE_VIRTUAL_DEVICE (dev);
  G_DEBUG_HERE ();

  listener = fp_device_virtual_listener_new ();
  cancellable = g_cancellable_new ();

  if (!fp_device_virtual_listener_start (listener,
                                         fpi_device_get_virtual_env (FP_DEVICE (self)),
                                         cancellable,
                                         on_listener_connected,
                                         self,
                                         &error))
    {
      fpi_device_open_complete (dev, g_steal_pointer (&error));
      return;
    }

  self->listener = g_steal_pointer (&listener);
  self->cancellable = g_steal_pointer (&cancellable);

  fpi_device_open_complete (dev, NULL);
}

static void
dev_verify (FpDevice *dev)
{
  FpPrint *print;

  g_autoptr(GVariant) data = NULL;
  gboolean success;

  fpi_device_get_verify_data (dev, &print);
  g_object_get (print, "fpi-data", &data, NULL);
  success = g_variant_get_boolean (data);

  fpi_device_verify_report (dev,
                            success ? FPI_MATCH_SUCCESS : FPI_MATCH_FAIL,
                            NULL, NULL);
  fpi_device_verify_complete (dev, NULL);
}

static void
dev_enroll (FpDevice *dev)
{
  FpDeviceVirtualDevice *self = FP_DEVICE_VIRTUAL_DEVICE (dev);
  gpointer success_ptr;
  FpPrint *print = NULL;
  g_autofree char *key = NULL;

  fpi_device_get_enroll_data (dev, &print);
  key = g_strdup_printf ("%s-%s",
                         finger_to_str (fp_print_get_finger (print)),
                         fp_print_get_username (print));

  if (g_hash_table_lookup_extended (self->pending_prints, key, NULL, &success_ptr))
    {
      gboolean success = GPOINTER_TO_INT (success_ptr);
      GVariant *fp_data;

      fp_data = g_variant_new_boolean (success);
      fpi_print_set_type (print, FPI_PRINT_RAW);
      if (fp_device_has_storage (dev))
        fpi_print_set_device_stored (print, TRUE);
      g_object_set (print, "fpi-data", fp_data, NULL);
      fpi_device_enroll_complete (dev, g_object_ref (print), NULL);
    }
  else
    {
      fpi_device_enroll_complete (dev, NULL,
                                  fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                            "No pending result for this username/finger combination"));
    }
}

static void
dev_deinit (FpDevice *dev)
{
  FpDeviceVirtualDevice *self = FP_DEVICE_VIRTUAL_DEVICE (dev);

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);
  g_clear_object (&self->listener);
  g_clear_object (&self->listener);

  fpi_device_close_complete (dev, NULL);
}

static void
fpi_device_virtual_device_finalize (GObject *object)
{
  FpDeviceVirtualDevice *self = FP_DEVICE_VIRTUAL_DEVICE (object);

  G_DEBUG_HERE ();

  g_hash_table_destroy (self->pending_prints);
}

static void
fpi_device_virtual_device_init (FpDeviceVirtualDevice *self)
{
  self->pending_prints = g_hash_table_new_full (g_str_hash,
                                                g_str_equal,
                                                g_free,
                                                NULL);
}

static const FpIdEntry driver_ids[] = {
  { .virtual_envvar = "FP_VIRTUAL_DEVICE", },
  { .virtual_envvar = NULL }
};

static void
fpi_device_virtual_device_class_init (FpDeviceVirtualDeviceClass *klass)
{
  FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = fpi_device_virtual_device_finalize;

  dev_class->id = FP_COMPONENT;
  dev_class->full_name = "Virtual device for debugging";
  dev_class->type = FP_DEVICE_TYPE_VIRTUAL;
  dev_class->id_table = driver_ids;
  dev_class->nr_enroll_stages = 5;

  dev_class->open = dev_init;
  dev_class->close = dev_deinit;
  dev_class->verify = dev_verify;
  dev_class->enroll = dev_enroll;
}
