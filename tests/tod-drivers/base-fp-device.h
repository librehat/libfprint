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

typedef struct _FpDevice FpDevice;

typedef enum {
  FP_DEVICE_TYPE_TODV1_VIRTUAL,
  FP_DEVICE_TYPE_TODV1_USB,
} FpDeviceTypeTODV1;

typedef enum {
  FP_SCAN_TYPE_TODV1_SWIPE,
  FP_SCAN_TYPE_TODV1_PRESS,
} FpScanTypeTODV1;

typedef enum {
  FP_DEVICE_RETRY_TODV1_GENERAL,
  FP_DEVICE_RETRY_TODV1_TOO_SHORT,
  FP_DEVICE_RETRY_TODV1_CENTER_FINGER,
  FP_DEVICE_RETRY_TODV1_REMOVE_FINGER,
} FpDeviceRetryTODV1;

typedef enum {
  FP_DEVICE_ERROR_TODV1_GENERAL,
  FP_DEVICE_ERROR_TODV1_NOT_SUPPORTED,
  FP_DEVICE_ERROR_TODV1_NOT_OPEN,
  FP_DEVICE_ERROR_TODV1_ALREADY_OPEN,
  FP_DEVICE_ERROR_TODV1_BUSY,
  FP_DEVICE_ERROR_TODV1_PROTO,
  FP_DEVICE_ERROR_TODV1_DATA_INVALID,
  FP_DEVICE_ERROR_TODV1_DATA_NOT_FOUND,
  FP_DEVICE_ERROR_TODV1_DATA_FULL,
} FpDeviceErrorTODV1;
