/*
 * FpDevice Unit tests
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

#include <libfprint/fprint.h>

#include "drivers_api.h"
#include "fp-todv1-enums.h"

#include "tod-drivers/base-fp-device.h"
#include "tod-drivers/base-fp-print.h"
#include "tod-drivers/base-fpi-device.h"
#include "tod-drivers/base-fpi-image-device.h"
#include "tod-drivers/base-fpi-usb.h"

static void
check_enum_compatibility (GType old_type, GType current_type)
{
  g_autoptr(GEnumClass) old_class = g_type_class_ref (old_type);
  g_autoptr(GEnumClass) current_class = g_type_class_ref (current_type);
  int i;

  g_debug ("Checking Enum %s", g_type_name (current_type));

  for (i = 0; g_enum_get_value (old_class, i); ++i)
    {
      GEnumValue *old_value = g_enum_get_value (old_class, i);
      GEnumValue *current_value = g_enum_get_value_by_nick (current_class,
                                                            old_value->value_nick);

      g_debug (" .. %s", old_value->value_nick);
      g_assert_nonnull (current_value);
      g_assert_cmpuint (old_value->value, ==, current_value->value);
    }
}

static void
check_flags_compatibility (GType old_type, GType current_type)
{
  g_autoptr(GFlagsClass) old_class = g_type_class_ref (old_type);
  g_autoptr(GFlagsClass) current_class = g_type_class_ref (current_type);
  int i;

  g_debug ("Checking Flags %s", g_type_name (current_type));

  for (i = 0; i < old_class->n_values; ++i)
    {
      GFlagsValue *old_value = &old_class->values[i];
      GFlagsValue *current_value = g_flags_get_value_by_nick (current_class,
                                                              old_value->value_nick);

      g_debug (" .. %s", old_value->value_nick);
      g_assert_nonnull (current_value);
      g_assert_cmpuint (old_value->value, ==, current_value->value);
    }
}

static void
check_compatiblity_auto (GType old_type, GType current_type)
{
  if (G_TYPE_IS_ENUM (old_type))
    return check_enum_compatibility (old_type, current_type);

  if (G_TYPE_IS_FLAGS (old_type))
    return check_flags_compatibility (old_type, current_type);

  g_assert_not_reached ();
}

#define check_type_compatibility(type) \
  check_compatiblity_auto (type ## _TOD_V1, type)

#define check_struct_size(type) \
  g_debug ("Checking " # type " size"); \
  g_assert_cmpuint (sizeof (type ## TODV1), ==, sizeof (type))

#define check_struct_member(type, member) \
  g_debug ("Checking " # type "'s " # member " offset"); \
  g_assert_cmpuint (G_STRUCT_OFFSET (type ## TODV1, member), ==, G_STRUCT_OFFSET (type, member))

static void
test_device_type (void)
{
  check_struct_size (FpIdEntry);
  check_struct_size (FpDeviceClass);

  check_struct_member (FpIdEntry, virtual_envvar);
  check_struct_member (FpIdEntry, driver_data);

  check_struct_member (FpDeviceClass, id);
  check_struct_member (FpDeviceClass, full_name);
  check_struct_member (FpDeviceClass, type);
  check_struct_member (FpDeviceClass, id_table);

  check_struct_member (FpDeviceClass, nr_enroll_stages);
  check_struct_member (FpDeviceClass, scan_type);

  check_struct_member (FpDeviceClass, usb_discover);
  check_struct_member (FpDeviceClass, probe);
  check_struct_member (FpDeviceClass, open);
  check_struct_member (FpDeviceClass, close);
  check_struct_member (FpDeviceClass, enroll);
  check_struct_member (FpDeviceClass, verify);
  check_struct_member (FpDeviceClass, identify);
  check_struct_member (FpDeviceClass, capture);
  check_struct_member (FpDeviceClass, list);
  check_struct_member (FpDeviceClass, delete);
  check_struct_member (FpDeviceClass, cancel);
}

static void
test_image_device_private (void)
{
  check_struct_size (FpImage);
  check_struct_size (FpImageDeviceClass);

  check_struct_member (FpImageDeviceClass, bz3_threshold);
  check_struct_member (FpImageDeviceClass, img_width);
  check_struct_member (FpImageDeviceClass, img_height);
  check_struct_member (FpImageDeviceClass, img_open);
  check_struct_member (FpImageDeviceClass, img_close);
  check_struct_member (FpImageDeviceClass, activate);
  check_struct_member (FpImageDeviceClass, change_state);
  check_struct_member (FpImageDeviceClass, deactivate);
}

static void
test_usb_private (void)
{
  check_struct_size (FpiUsbTransfer);

  check_struct_member (FpiUsbTransfer, device);
  check_struct_member (FpiUsbTransfer, ssm);
  check_struct_member (FpiUsbTransfer, length);
  check_struct_member (FpiUsbTransfer, actual_length);
  check_struct_member (FpiUsbTransfer, buffer);
  check_struct_member (FpiUsbTransfer, ref_count);
  check_struct_member (FpiUsbTransfer, type);
  check_struct_member (FpiUsbTransfer, endpoint);
  check_struct_member (FpiUsbTransfer, direction);
  check_struct_member (FpiUsbTransfer, request_type);
  check_struct_member (FpiUsbTransfer, recipient);
  check_struct_member (FpiUsbTransfer, request);
  check_struct_member (FpiUsbTransfer, value);
  check_struct_member (FpiUsbTransfer, idx);
  check_struct_member (FpiUsbTransfer, short_is_error);
  check_struct_member (FpiUsbTransfer, user_data);
  check_struct_member (FpiUsbTransfer, callback);
  check_struct_member (FpiUsbTransfer, free_buffer);
}

static void
test_device_public_enums (void)
{
  check_type_compatibility (FP_TYPE_DEVICE_TYPE);
  check_type_compatibility (FP_TYPE_SCAN_TYPE);
  check_type_compatibility (FP_TYPE_DEVICE_RETRY);
  check_type_compatibility (FP_TYPE_DEVICE_ERROR);
}

static void
test_device_private_enums (void)
{
  check_type_compatibility (FPI_TYPE_DEVICE_ACTION);
}

static void
test_print_public_enums (void)
{
  check_type_compatibility (FP_TYPE_FINGER);
  check_type_compatibility (FP_TYPE_FINGER_STATUS_FLAGS);
}

static void
test_print_private_enums (void)
{
  check_type_compatibility (FPI_TYPE_PRINT_TYPE);
  check_type_compatibility (FPI_TYPE_MATCH_RESULT);
}

static void
test_image_device_enums (void)
{
  check_type_compatibility (FPI_TYPE_IMAGE_FLAGS);
  check_type_compatibility (FPI_TYPE_IMAGE_DEVICE_STATE);
}

static void
test_usb_enums (void)
{
  check_type_compatibility (FPI_TYPE_TRANSFER_TYPE);
}

int
main (int argc, char *argv[])
{
  if (!strstr (g_getenv ("FP_TOD_TEST_DRIVER_NAME"), "v1"))
    return 77;

  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/type/device/private", test_device_type);
  g_test_add_func ("/type/device/enums", test_device_public_enums);
  g_test_add_func ("/type/device/private/enums", test_device_private_enums);
  g_test_add_func ("/type/print/enums", test_print_public_enums);
  g_test_add_func ("/type/print/private/enums", test_print_private_enums);
  g_test_add_func ("/type/image-device/private", test_image_device_private);
  g_test_add_func ("/type/image-device/enums", test_image_device_enums);
  g_test_add_func ("/type/usb/private", test_usb_private);
  g_test_add_func ("/type/usb/enums", test_usb_enums);

  return g_test_run ();
}
