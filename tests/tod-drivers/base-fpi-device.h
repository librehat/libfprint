/*
 * FpDevice - A fingerprint reader device
 * Copyright (C) 2021 Marco Trevisan <marco.trevisan@canonical.com>
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

#pragma once

#include <glib.h>
#include <glib-object.h>

#include "base-fp-device.h"

typedef struct _GUsbDevice     GUsbDevice;

typedef struct _FpIdEntryTODV1 FpIdEntryTODV1;

struct _FpIdEntryTODV1
{
  union
  {
    struct
    {
      guint pid;
      guint vid;
    };
    const gchar *virtual_envvar;
  };
  guint64 driver_data;

  /*< private >*/
  /* padding for future expansion */
  gpointer _padding_dummy[16];
};

struct _FpDeviceClassTODV1
{
  /*< private >*/
  GObjectClass parent_class;

  /*< public >*/
  /* Static information about the driver. */
  const gchar          *id;
  const gchar          *full_name;
  FpDeviceTypeTODV1     type;
  const FpIdEntryTODV1 *id_table;

  /* Defaults for device properties */
  gint            nr_enroll_stages;
  FpScanTypeTODV1 scan_type;

  /* Callbacks */
  gint (*usb_discover) (GUsbDevice *usb_device);
  void (*probe)    (FpDevice *device);
  void (*open)     (FpDevice *device);
  void (*close)    (FpDevice *device);
  void (*enroll)   (FpDevice *device);
  void (*verify)   (FpDevice *device);
  void (*identify) (FpDevice *device);
  void (*capture)  (FpDevice *device);
  void (*list)     (FpDevice *device);
  void (*delete)   (FpDevice * device);

  void (*cancel)   (FpDevice *device);

  /*< private >*/
  /* padding for future expansion */
  gpointer _padding_dummy[32];
};

typedef struct _FpDeviceClassTODV1 FpDeviceClassTODV1;

typedef enum {
  FPI_DEVICE_ACTION_TODV1_NONE = 0,
  FPI_DEVICE_ACTION_TODV1_PROBE,
  FPI_DEVICE_ACTION_TODV1_OPEN,
  FPI_DEVICE_ACTION_TODV1_CLOSE,
  FPI_DEVICE_ACTION_TODV1_ENROLL,
  FPI_DEVICE_ACTION_TODV1_VERIFY,
  FPI_DEVICE_ACTION_TODV1_IDENTIFY,
  FPI_DEVICE_ACTION_TODV1_CAPTURE,
  FPI_DEVICE_ACTION_TODV1_LIST,
  FPI_DEVICE_ACTION_TODV1_DELETE,
} FpiDeviceActionTODV1;
