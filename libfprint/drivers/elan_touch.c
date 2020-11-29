/*
 * Elan driver for libfprint
 *
 * Copyright (C) 2017 Igor Filatov <ia.filatov@gmail.com>
 * Copyright (C) 2018 Sébastien Béchet <sebastien.bechet@osinix.com >
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
 * The algorithm which libfprint uses to match fingerprints doesn't like small
 * images like the ones these drivers produce. There's just not enough minutiae
 * (recognizable print-specific points) on them for a reliable match. This means
 * that unless another matching algo is found/implemented, these readers will
 * not work as good with libfprint as they do with vendor drivers.
 *
 * To get bigger images the driver expects you to swipe the finger over the
 * reader. This works quite well for readers with a rectangular 144x64 sensor.
 * Worse than real swipe readers but good enough for day-to-day use. It needs
 * a steady and relatively slow swipe. There are also square 96x96 sensors and
 * I don't know whether they are in fact usable or not because I don't have one.
 * I imagine they'd be less reliable because the resulting image is even
 * smaller. If they can't be made usable with libfprint, I might end up dropping
 * them because it's better than saying they work when they don't.
 */

#define FP_COMPONENT "elan_touch"

#include "drivers_api.h"
#include "elan.h"

struct _FpiDeviceElanTouch
{
  FpiDeviceElan parent;
};
G_DECLARE_FINAL_TYPE (FpiDeviceElanTouch, fpi_device_elan_touch, FPI,
                      DEVICE_ELAN_TOUCH, FpiDeviceElan);
G_DEFINE_TYPE (FpiDeviceElanTouch, fpi_device_elan_touch, FPI_TYPE_DEVICE_ELAN);

static const FpIdEntry id_table[] = {
  { .vid = 0x04f3, .pid = 0x0c28 },
  { .vid = 0,  .pid = 0,  .driver_data = 0 },
};

static void
elan_touch_submit_image (FpImageDevice *dev)
{
  FpiDeviceElanClass *self = FPI_DEVICE_ELAN_GET_CLASS (FPI_DEVICE_ELAN (dev));
  GSList *raw_frames = NULL;
  FpImage *img, *tmp;

  G_DEBUG_HERE ();

  // Prefer the middle frame
  raw_frames = g_slist_prepend (raw_frames, g_slist_nth_data (self->frames, g_slist_length (self->frames) / 2));
  tmp = self->assemble_image (self, raw_frames, self->frame_width);
  /* Make the image big enough for NBIS to process reliably */
  img = fpi_image_resize (tmp, 2, 2, TRUE);
  g_object_unref (tmp);

  g_slist_free (raw_frames);
  fpi_image_device_image_captured (dev, img);
}

static void
fpi_device_elan_touch_init (FpiDeviceElanTouch *self)
{
}

static void
fpi_device_elan_touch_class_init (FpiDeviceElanTouchClass *klass)
{
  FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);
  FpiDeviceElanClass *elan_class = FPI_DEVICE_ELAN_CLASS (klass);
  FpImageDeviceClass *img_class = FP_IMAGE_DEVICE_CLASS (klass);

  dev_class->id = "elan_touch";
  dev_class->full_name = "ElanTech Fingerprint Sensor";
  dev_class->id_table = id_table;
  dev_class->scan_type = FP_SCAN_TYPE_PRESS;

  /* Low due to low image quality. */
  img_class->bz3_threshold = 9;

  elan_class->min_frames = ELAN_TOUCH_MIN_FRAMES;
  elan_class->max_frames = ELAN_TOUCH_MAX_FRAMES;
  elan_class->max_frame_height = ELAN_TOUCH_MAX_FRAME_HEIGHT;
  elan_class->submit_image = elan_touch_submit_image;
}
