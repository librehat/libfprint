/*
 * FpImageDevice - An image based fingerprint reader device
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

#include "base-fpi-device.h"

typedef struct _FpImageDevice FpImageDevice;

typedef enum {
  FPI_IMAGE_DEVICE_STATE_TODV1_INACTIVE,
  FPI_IMAGE_DEVICE_STATE_TODV1_AWAIT_FINGER_ON,
  FPI_IMAGE_DEVICE_STATE_TODV1_CAPTURE,
  FPI_IMAGE_DEVICE_STATE_TODV1_AWAIT_FINGER_OFF,
} FpiImageDeviceStateTODV1;

typedef struct _FpImageDeviceClassTODV1
{
  FpDeviceClassTODV1 parent_class;

  gint               bz3_threshold;
  gint               img_width;
  gint               img_height;

  void (*img_open)(FpImageDevice *dev);
  void (*img_close)(FpImageDevice *dev);
  void (*activate)(FpImageDevice *dev);
  void (*change_state)(FpImageDevice           *dev,
                       FpiImageDeviceStateTODV1 state);
  void (*deactivate)(FpImageDevice *dev);

  /*< private >*/
  /* padding for future expansion */
  gpointer _padding_dummy[32];
} FpImageDeviceClassTODV1;

/* fpi-image */

typedef enum {
  FPI_IMAGE_TODV1_V_FLIPPED       = 1 << 0,
  FPI_IMAGE_TODV1_H_FLIPPED       = 1 << 1,
  FPI_IMAGE_TODV1_COLORS_INVERTED = 1 << 2,
  FPI_IMAGE_TODV1_PARTIAL         = 1 << 3,
} FpiImageFlagsTODV1;

typedef struct _FpImageTODV1
{
  /*< private >*/
  GObject parent;

  /*< public >*/
  guint              width;
  guint              height;

  gdouble            ppmm;

  FpiImageFlagsTODV1 flags;

  /*< private >*/
  guint8    *data;
  guint8    *binarized;

  GPtrArray *minutiae;
  guint      ref_count;

  gpointer   _padding_dummy[32];
} FpImageTODV1;
