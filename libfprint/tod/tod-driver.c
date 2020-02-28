/*
 * Virtual driver for device debugging
 *
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

#define FP_COMPONENT "fake_test_dev"

#include "tod-fake-device-driver.h"
#include "drivers_api.h"

struct _FpiDeviceFake
{
  FpDevice parent;

  gboolean opening;
  gboolean opened;

  GCancellable *cancellable;
};

#define FPI_TYPE_DEVICE_FAKE (fpi_device_fake_get_type ())
G_DECLARE_FINAL_TYPE (FpiDeviceFake, fpi_device_fake, FPI, DEVICE_FAKE, FpDevice)
G_DEFINE_TYPE (FpiDeviceFake, fpi_device_fake, FP_TYPE_DEVICE)

GType
fpi_tod_shared_driver_get_type (void)
{
  return fpi_device_fake_get_type ();
}

static const FpIdEntry driver_ids[] = {
  { .virtual_envvar = "FP_TOD_DRIVER_EXAMPLE" },
  { .virtual_envvar = NULL }
};

static void
fpi_device_fake_probe (FpDevice *device)
{
  FpDeviceClass *dev_class = FP_DEVICE_GET_CLASS (device);

  g_assert_cmpuint (fpi_device_get_current_action (device), ==, FPI_DEVICE_ACTION_PROBE);

  fpi_device_probe_complete (device, dev_class->id, dev_class->full_name, NULL);
}

enum
{
  FPI_DEVICE_FAKE_OPEN_STEP_0,
  FPI_DEVICE_FAKE_OPEN_STEP_1,
  FPI_DEVICE_FAKE_OPEN_STEP_2,
  FPI_DEVICE_FAKE_OPEN_STEP_NUM,
};


static void
fpi_device_fake_open_ssm_completed_callback (FpiSsm   *ssm,
                                             FpDevice *dev,
                                             GError   *error)
{
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (dev);

  g_assert_true (fake_dev->opening);

  g_clear_object (&fake_dev->cancellable);

  fake_dev->opening = FALSE;
  fake_dev->opened = TRUE;

  fp_dbg ("Yes, device opened!");

  fpi_device_open_complete (dev, error);
}

static void
fpi_device_fake_open_ssm_handler (FpiSsm   *ssm,
                                  FpDevice *dev)
{
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (dev);

  g_assert_true (fake_dev->opening);

  switch (fpi_ssm_get_cur_state (ssm))
    {
      case FPI_DEVICE_FAKE_OPEN_STEP_0:
        fpi_ssm_jump_to_state (ssm, FPI_DEVICE_FAKE_OPEN_STEP_1);
        break;
      case FPI_DEVICE_FAKE_OPEN_STEP_1:
        fpi_ssm_next_state (ssm);
        break;
      case FPI_DEVICE_FAKE_OPEN_STEP_2:
        fake_dev->cancellable = g_cancellable_new ();
        fpi_ssm_next_state_delayed (ssm, 500, fake_dev->cancellable);
        break;
      default:
        g_assert_not_reached ();
    }
}

static void
fpi_device_fake_open (FpDevice *device)
{
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);
  FpiSsm *ssm;

  g_assert_cmpuint (fpi_device_get_current_action (device), ==, FPI_DEVICE_ACTION_OPEN);

  fake_dev->opening = TRUE;

  ssm = fpi_ssm_new_full (device, fpi_device_fake_open_ssm_handler,
                          FPI_DEVICE_FAKE_OPEN_STEP_NUM, "OPEN_STATE_MACHINE");
  fpi_ssm_start (ssm, fpi_device_fake_open_ssm_completed_callback);
}

static void
fpi_device_fake_close (FpDevice *device)
{
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);
  g_assert_cmpuint (fpi_device_get_current_action (device), ==, FPI_DEVICE_ACTION_CLOSE);

  fake_dev->opened = FALSE;
  fpi_device_close_complete (device, NULL);
}

static void
fpi_device_fake_enroll (FpDevice *device)
{
  FpPrint *print;

  g_assert_cmpuint (fpi_device_get_current_action (device), ==, FPI_DEVICE_ACTION_ENROLL);

  fpi_device_get_enroll_data (device, &print);

  fpi_device_enroll_complete (device,
                              print ? g_object_ref (print) : fp_print_new (device),
                              NULL);
}

static void
fpi_device_fake_verify (FpDevice *device)
{
  FpPrint *print;

  g_assert_cmpuint (fpi_device_get_current_action (device), ==, FPI_DEVICE_ACTION_VERIFY);

  fpi_device_get_verify_data (device, &print);

  fpi_device_verify_complete (device, FPI_MATCH_SUCCESS, print, NULL);
}

static void
fpi_device_fake_identify (FpDevice *device)
{
  FpPrint *match = NULL;
  GPtrArray *prints;
  unsigned int i;

  g_assert_cmpuint (fpi_device_get_current_action (device), ==, FPI_DEVICE_ACTION_IDENTIFY);

  fpi_device_get_identify_data (device, &prints);

  for (i = 0; prints && i < prints->len; ++i)
    {
      FpPrint *print = g_ptr_array_index (prints, i);

      if (g_strcmp0 (fp_print_get_description (print), "fake-verified") == 0)
        {
          match = print;
          break;
        }
    }

  fpi_device_identify_complete (device, match, fp_print_new (device), NULL);
}

static void
fpi_device_fake_capture (FpDevice *device)
{
  g_assert_cmpuint (fpi_device_get_current_action (device), ==, FPI_DEVICE_ACTION_CAPTURE);

  fpi_device_capture_complete (device, fp_image_new (100, 100), NULL);
}

static void
fpi_device_fake_list (FpDevice *device)
{
  g_assert_cmpuint (fpi_device_get_current_action (device), ==, FPI_DEVICE_ACTION_LIST);

  fpi_device_list_complete (device, g_ptr_array_new_with_free_func (g_object_unref), NULL);
}

static void
fpi_device_fake_delete (FpDevice *device)
{
  g_assert_cmpuint (fpi_device_get_current_action (device), ==, FPI_DEVICE_ACTION_DELETE);

  fpi_device_delete_complete (device, NULL);
}

static void
fpi_device_fake_cancel (FpDevice *device)
{
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);

  g_assert_cmpuint (fpi_device_get_current_action (device), !=, FPI_DEVICE_ACTION_NONE);

  g_cancellable_cancel (fake_dev->cancellable);
  g_clear_object (&fake_dev->cancellable);
}

static void
fpi_device_fake_init (FpiDeviceFake *self)
{
}

static void
fpi_device_fake_finalize (GObject *object)
{
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (object);

  g_clear_object (&fake_dev->cancellable);
}


static void
fpi_device_fake_class_init (FpiDeviceFakeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);

  dev_class->id = FP_COMPONENT;
  dev_class->full_name = "Libfprint TOD fake device driver example";
  dev_class->type = FP_DEVICE_TYPE_VIRTUAL;
  dev_class->id_table = driver_ids;
  dev_class->nr_enroll_stages = 5;
  dev_class->scan_type = FP_SCAN_TYPE_PRESS;

  dev_class->probe = fpi_device_fake_probe;
  dev_class->open = fpi_device_fake_open;
  dev_class->close = fpi_device_fake_close;
  dev_class->enroll = fpi_device_fake_enroll;
  dev_class->verify = fpi_device_fake_verify;
  dev_class->identify = fpi_device_fake_identify;
  dev_class->capture = fpi_device_fake_capture;
  dev_class->list = fpi_device_fake_list;
  dev_class->delete = fpi_device_fake_delete;
  dev_class->cancel = fpi_device_fake_cancel;

  object_class->finalize = fpi_device_fake_finalize;
}
