/*
 * Virtual driver for "simple" device debugging
 *
 * Copyright (C) 2019 Benjamin Berg <bberg@redhat.com>
 * Copyright (C) 2020 Bastien Nocera <hadess@hadess.net>
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

#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>

#include "fpi-device.h"

#define MAX_LINE_LEN 1024

struct _FpDeviceVirtualDevice
{
  FpDevice           parent;

  GSocketListener   *listener;
  GSocketConnection *connection;
  GCancellable      *cancellable;

  gint               socket_fd;
  gint               client_fd;
  guint              line[MAX_LINE_LEN];

  GHashTable        *pending_prints; /* key: finger+username value: gboolean */
};

/* Not really final here, but we can do this to share the FpDeviceVirtualDevice
 * contents without having to use a shared private struct instead. */
G_DECLARE_FINAL_TYPE (FpDeviceVirtualDevice, fpi_device_virtual_device, FP, DEVICE_VIRTUAL_DEVICE, FpDevice)

struct _FpDeviceVirtualDeviceIdent
{
  FpDeviceVirtualDevice parent;
};

G_DECLARE_FINAL_TYPE (FpDeviceVirtualDeviceIdent, fpi_device_virtual_device_ident, FP, DEVICE_VIRTUAL_DEVICE_IDENT, FpDeviceVirtualDevice)
