/*
 * Elan primitive driver for libfprint
 *
 * Copyright (C) 2019 ELAN Microelectronics Corp.,Ltd.
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
 * This primitive driver hooks onto the proprietary libefd.so provided from
 * ELAN Microelectronics Corp.,Ltd.
 */

#define FP_COMPONENT "elan_pri"

#include "drivers_api.h"
#include "fpi-async.h"
#include "elan_pri.h"
#include "fp_internal.h"

#define EFD_LIB		"libefd.so"

#define DLSYM_LOAD(s, f)					\
	do {									\
		(s->f) = dlsym((s)->handle, #f);	\
		if (!(s->f)) {						\
			fp_err("(%s) dlsym(%s) = %s\n", __func__, #f, dlerror());	\
			goto error; }					\
	} while (0)

struct elan_pri {
	void *handle;
	uint64_t (*efd_version)(void);
	void (*efd_output_log_level)(uint8_t level);
	struct efd *(*efd_init)(libusb_device_handle *handle,
			efd_dev_handle_events_fn handle_events);
	int (*efd_release)(struct efd *efd);
	int (*efd_scan)(struct efd *efd, efd_scan_cb_fn callback, void *user_data);
	int (*efd_enroll_init)(struct efd *efd);
	int (*efd_enroll_result)(struct efd *efd);
	int (*efd_enroll_abandon)(struct efd *efd);
	struct efd_template *(*efd_enroll_done)(struct efd *efd);
	int (*efd_template_release)(struct efd_template *efd_template);
	int (*efd_list_prepend)(struct efd_list **list, void *data);
	void (*efd_list_free)(struct efd_list *list);
	int (*efd_verify_init)(struct efd *efd, struct efd_list *efd_db_head);
	int (*efd_verify_result)(struct efd *efd);
	int (*efd_verify_release)(struct efd *efd);

	struct efd *efd;
	struct efd_list *efd_db;
	uint64_t efd_ver;
	uint8_t stop_verify;
	int enroll_stage;
};

static void *load_libefd(struct elan_pri *elanpri)
{
	void *handle = NULL;

	if (!elanpri)
		goto exit;

	handle = dlopen(EFD_LIB, RTLD_LAZY | RTLD_GLOBAL);
	if (!handle) {
		fp_err("(%s) dlopen failed, %s\n", __func__, dlerror());
		goto exit;
	}

	DLSYM_LOAD(elanpri, efd_output_log_level);
	DLSYM_LOAD(elanpri, efd_version);
	DLSYM_LOAD(elanpri, efd_init);
	DLSYM_LOAD(elanpri, efd_release);
	DLSYM_LOAD(elanpri, efd_scan);
	DLSYM_LOAD(elanpri, efd_enroll_init);
	DLSYM_LOAD(elanpri, efd_enroll_result);
	DLSYM_LOAD(elanpri, efd_enroll_abandon);
	DLSYM_LOAD(elanpri, efd_enroll_done);
	DLSYM_LOAD(elanpri, efd_template_release);
	DLSYM_LOAD(elanpri, efd_list_prepend);
	DLSYM_LOAD(elanpri, efd_list_free);
	DLSYM_LOAD(elanpri, efd_verify_init);
	DLSYM_LOAD(elanpri, efd_verify_result);
	DLSYM_LOAD(elanpri, efd_verify_release);

	goto exit;

error:
	if (handle)
		dlclose(handle);
	handle = NULL;

exit:
	return handle;
}

static int dev_discover(struct libusb_device_descriptor *dsc, uint32_t *devtype)
{
	void *handle = NULL;

	handle = dlopen(EFD_LIB, RTLD_LAZY);
	if (!handle) {
		fp_dbg("(%s) dlopen failed, %s\n", __func__, dlerror());
		return 0;
	}
	dlclose(handle);

	return 1;
}

static int dev_init(struct fp_dev *dev, unsigned long driver_data)
{
	struct elan_pri *elanpri = FP_INSTANCE_DATA(dev);
	int rtn = 0;

	elanpri = g_malloc(sizeof(*elanpri));
	if (!elanpri) {
		fp_err("(%s) g_malloc(elan_pri) failed\n", __func__);
		rtn = -ENOMEM;
		goto exit;
	}
	memset(elanpri, 0, sizeof(*elanpri));

	elanpri->handle = load_libefd(elanpri);
	if (!elanpri->handle) {
		fp_err("(%s) load_libefd failed\n", __func__);
		rtn = -EFAULT;
		goto error;
	}

	elanpri->efd_ver = elanpri->efd_version();
	if ((uint16_t) elanpri->efd_ver) {
		fp_dbg("(%s) efd version: %u.%u.%u Beta %u\n", __func__,
			(uint16_t) (elanpri->efd_ver>>48),
			(uint16_t) (elanpri->efd_ver>>32),
			(uint16_t) (elanpri->efd_ver>>16),
			(uint16_t) elanpri->efd_ver);
			elanpri->efd_output_log_level(EFD_LOG_DEBUG);
	} else {
		fp_dbg("(%s) efd version: %u.%u.%u\n", __func__,
			(uint16_t) (elanpri->efd_ver>>48),
			(uint16_t) (elanpri->efd_ver>>32),
			(uint16_t) (elanpri->efd_ver>>16));
	}

	elanpri->efd = elanpri->efd_init(
			fpi_dev_get_usb_dev(dev), fp_handle_events);
	if (!elanpri->efd) {
		fp_err("(%s) Failed to get structure from efd_init\n", __func__);
		rtn = -EPERM;
		goto error;
	}

	if (elanpri->efd->dev->product_id == 0x0C42)
		fpi_dev_set_nr_enroll_stages(dev, ENROLL_STAGE_0C42);
	else
		fpi_dev_set_nr_enroll_stages(dev, ENROLL_STAGE_DEFAULT);

	fp_dev_set_instance_data(dev, elanpri);
	fpi_drvcb_open_complete(dev, 0);

	rtn = 0;
	goto exit;

error:
	if (elanpri->handle)
		dlclose(elanpri->handle);
	g_free(elanpri);
exit:
	return rtn;
}

static void dev_exit(struct fp_dev *dev)
{
	struct elan_pri *elanpri = FP_INSTANCE_DATA(dev);
	int rtn;

	if (elanpri == NULL)
		return;

	rtn = elanpri->efd_release(elanpri->efd);
	if (rtn != EFD_OK_SUCCESS)
		fp_err("(%s) Failed to release efd\n", __func__);

	if (elanpri->handle)
		dlclose(elanpri->handle);
	g_free(elanpri);

	fpi_drvcb_close_complete(dev);
}

static struct fp_img *get_scan_image(struct fp_dev *dev)
{
	struct elan_pri *elanpri = FP_INSTANCE_DATA(dev);
	struct efd *efd = elanpri->efd;
	struct fp_img *img = NULL;
	size_t img_size = sizeof(uint8_t) *
		(efd->sensor_dim->width * efd->sensor_dim->height);

	if (!img_size) {
		fp_err("(%s) image size is zero\n", __func__);
		goto exit;
	}

	img = fpi_img_new(img_size);
	if (!img) {
		fp_err("(%s) fpi_img_new failed\n", __func__);
		goto exit;
	}
	img->width =  efd->sensor_dim->width;
	img->height = efd->sensor_dim->height;
	img->length = img_size;
	memcpy(img->data, efd->img_finger, img_size);

exit:
	return img;
}

static int capture_stop(struct fp_dev *dev)
{
	fpi_drvcb_capture_stopped(dev);
	return 0;
}

static void efd_capture_scan_cb(int result, void *user_data)
{
	struct fp_dev *dev = user_data;
	struct fp_img *img = NULL;
	int rtn = FP_CAPTURE_COMPLETE;

	if (result != EFD_OK_SUCCESS) {
		fp_err("(%s) efd_scan return %d\n", __func__, result);
		rtn = FP_CAPTURE_FAIL;
		goto exit;
	}

	img = get_scan_image(dev);

exit:
	fpi_drvcb_report_capture_result(dev, rtn, img);
}

static int capture_start(struct fp_dev *dev)
{
	struct elan_pri *elanpri = FP_INSTANCE_DATA(dev);
	int status = 0;
	int efdrtn;

	efdrtn = elanpri->efd_scan(
			elanpri->efd, efd_capture_scan_cb, (void *) dev);
	if (efdrtn != EFD_OK_SUCCESS) {
		fp_err("(%s) efd_scan return %d\n", __func__, efdrtn);
		status = -EFAULT;
		goto err;
	}

	fpi_drvcb_capture_started(dev, status);

err:
	return status;
}

static int identify_stop(struct fp_dev *dev, gboolean iterating)
{
	struct elan_pri *elanpri = FP_INSTANCE_DATA(dev);

	if (!iterating)
		fpi_drvcb_identify_stopped(dev);
	else
		elanpri->stop_verify = 1;

	return 0;
}

static void efd_identify_scan_cb(int result, void *user_data)
{
	struct fp_dev *dev = user_data;
	struct elan_pri *elanpri = FP_INSTANCE_DATA(dev);
	struct efd_list *list = elanpri->efd_db;
	struct fp_img *img = NULL;
	size_t match_offset = 0;
	int efdrtn = EFD_OK_SUCCESS;
	int rtn = 0;

	if (elanpri->stop_verify) {
		identify_stop(dev, (dev->state == DEV_STATE_VERIFYING));
		goto err;
	}

	if (result == EFD_SCAN_RETRY) {
		rtn = FP_VERIFY_RETRY;
		goto scan;
	/*} else if (result == EFD_NEED_CALIBRATION) {
		rtn = FP_VERIFY_RETRY_REMOVE_FINGER;
		goto scan;*/
	} else if (result != EFD_OK_SUCCESS) {
		fp_err("(%s) efd_scan return %d\n", __func__, result);
		rtn = -EFAULT;
		goto err;
	}

	efdrtn = elanpri->efd_verify_result(elanpri->efd);
	if (efdrtn == EFD_OK_SUCCESS) {
		match_offset = elanpri->efd->verify->match_index;
		rtn = FP_VERIFY_MATCH;
	} else if (efdrtn == EFD_VERIFY_NOT_MATCH)
		rtn = FP_VERIFY_NO_MATCH;
	else {
		fp_warn("(%s) efd_verify_result return %d\n", __func__, efdrtn);
		//rtn = FP_VERIFY_NO_MATCH;
		rtn = -EFAULT;
	}

	if (rtn == FP_VERIFY_MATCH ||
		rtn == FP_VERIFY_NO_MATCH)
		img = get_scan_image(dev);

scan:
	if (rtn >= FP_VERIFY_RETRY) {
		efdrtn = elanpri->efd_scan(
				elanpri->efd, efd_identify_scan_cb, (void *) dev);
		if (efdrtn != EFD_OK_SUCCESS) {
			fp_err("(%s) efd_scan return %d\n", __func__, efdrtn);
			rtn = -EFAULT;
			goto err;
		}
		goto exit;
	}

err:
	while (list) {
		g_free(list->data);
		list = list->next;
	}
	elanpri->efd_list_free(elanpri->efd_db);

	if (elanpri->efd_verify_release(elanpri->efd) != EFD_OK_SUCCESS)
		fp_err("(%s) efd_verify_release failed\n", __func__);

exit:
	if (!elanpri->stop_verify)
		fpi_drvcb_report_identify_result(dev, rtn, match_offset, img);
}

static int identify_start(struct fp_dev *dev)
{
	struct elan_pri *elanpri = FP_INSTANCE_DATA(dev);
	struct efd_list *list = NULL;
	struct efd_template *template = NULL;
	struct fp_print_data **gallery = dev->identify_gallery;
	struct fp_print_data *gallery_print = NULL;
	struct fp_print_data_item *item = NULL;
	int status = 0;
	int efdrtn = 0;
	size_t i = 0;

	//get enrolled template
	elanpri->efd_db = NULL;
	while ((gallery_print = gallery[i++])) {
		template = g_malloc(sizeof(*template));
		if (!template) {
			fp_err("(%s) g_malloc(efd_template) failed\n", __func__);
			status = -ENOMEM;
			goto err;
		}
		item = gallery_print->prints->data;
		template->data = item->data;
		template->data_length = item->length;
		efdrtn = elanpri->efd_list_prepend(
				&elanpri->efd_db, (void *) template);
		if (efdrtn != EFD_OK_SUCCESS) {
			fp_err("(%s) efd_list_prepend return %d\n", __func__, efdrtn);
			status = -ENOMEM;
			goto err;
		}
	}

	efdrtn = elanpri->efd_verify_init(elanpri->efd, elanpri->efd_db);
	if (efdrtn != EFD_OK_SUCCESS) {
		fp_err("(%s) efd_verify_init failed %d\n", __func__, efdrtn);
		status = -EFAULT;
		goto exit;
	}

	efdrtn = elanpri->efd_scan(
			elanpri->efd, efd_identify_scan_cb, (void *) dev);
	if (efdrtn != EFD_OK_SUCCESS) {
		fp_err("(%s) efd_scan return %d\n", __func__, efdrtn);
		status = -EFAULT;
		goto err;
	}

	elanpri->stop_verify = 0;
	fpi_drvcb_identify_started(dev, status);
	goto exit;

err:
	//free memory space allocated before
	list = elanpri->efd_db;
	while (list != NULL) {
		g_free(list->data);
		list = list->next;
	}
	elanpri->efd_list_free(elanpri->efd_db);

exit:
	return status;
}

static int verify_stop(struct fp_dev *dev, gboolean iterating)
{
	struct elan_pri *elanpri = FP_INSTANCE_DATA(dev);

	if (!iterating)
		fpi_drvcb_verify_stopped(dev);
	else
		elanpri->stop_verify = 1;

	return 0;
}

static void efd_verify_scan_cb(int result, void *user_data)
{
	struct fp_dev *dev = user_data;
	struct elan_pri *elanpri = FP_INSTANCE_DATA(dev);
	struct efd_list *efd_list = elanpri->efd_db;
	struct fp_img *img = NULL;
	int efdrtn = EFD_OK_SUCCESS;
	int rtn = 0;

	if (elanpri->stop_verify) {
		verify_stop(dev, (dev->state == DEV_STATE_VERIFYING));
		goto err;
	}

	if (result == EFD_SCAN_RETRY) {
		rtn = FP_VERIFY_RETRY;
		goto scan;
	/*} else if (result == EFD_NEED_CALIBRATION) {
		rtn = FP_VERIFY_RETRY_REMOVE_FINGER;
		goto scan;*/
	} else if (result != EFD_OK_SUCCESS) {
		fp_err("(%s) efd_scan return %d\n", __func__, result);
		rtn = -EFAULT;
		goto err;
	}

	efdrtn = elanpri->efd_verify_result(elanpri->efd);
	if (efdrtn == EFD_OK_SUCCESS)
		rtn = FP_VERIFY_MATCH;
	else if (efdrtn == EFD_VERIFY_NOT_MATCH)
		rtn = FP_VERIFY_NO_MATCH;
	else {
		fp_warn("(%s) efd_verify_result return %d\n", __func__, efdrtn);
		//rtn = FP_VERIFY_NO_MATCH;
		rtn = -EFAULT;
	}

	if (rtn == FP_VERIFY_MATCH ||
		rtn == FP_VERIFY_NO_MATCH)
		img = get_scan_image(dev);

scan:
	if (rtn >= FP_VERIFY_RETRY) {
		efdrtn = elanpri->efd_scan(
				elanpri->efd, efd_verify_scan_cb, (void *) dev);
		if (efdrtn != EFD_OK_SUCCESS) {
			fp_err("(%s) efd_scan return %d\n", __func__, efdrtn);
			rtn = -EFAULT;
			goto err;
		}
		goto exit;
	}

err:
	while (efd_list) {
		g_free(efd_list->data);
		efd_list = efd_list->next;
	}
	elanpri->efd_list_free(elanpri->efd_db);

	if (elanpri->efd_verify_release(elanpri->efd) != EFD_OK_SUCCESS)
		fp_err("(%s) efd_verify_release failed\n", __func__);

exit:
	if (!elanpri->stop_verify)
		fpi_drvcb_report_verify_result(dev, rtn, img);
}

static int verify_start(struct fp_dev *dev)
{
	struct elan_pri *elanpri = FP_INSTANCE_DATA(dev);
	struct efd_template *efd_template = NULL;
	struct fp_print_data *print = fpi_dev_get_verify_data(dev);
	struct fp_print_data_item *item = print->prints->data;
	int status = 0;
	int efdrtn = 0;

	//get enrolled template
	elanpri->efd_db = NULL;
	efd_template = g_malloc(sizeof(*efd_template));
	if (!efd_template) {
		fp_err("(%s) g_malloc(efd_template) failed\n", __func__);
		status = -ENOMEM;
		goto err;
	}
	efd_template->data = item->data;
	efd_template->data_length = item->length;
	efdrtn = elanpri->efd_list_prepend(
			&elanpri->efd_db, efd_template);
	if (efdrtn != EFD_OK_SUCCESS) {
		fp_err("(%s) efd_list_prepend return %d\n", __func__, efdrtn);
		status = -ENOMEM;
		goto err;
	}

	efdrtn = elanpri->efd_verify_init(elanpri->efd, elanpri->efd_db);
	if (efdrtn != EFD_OK_SUCCESS) {
		fp_err("(%s) efd_verify_init failed %d\n", __func__, efdrtn);
		status = -EFAULT;
		goto err;
	}

	efdrtn = elanpri->efd_scan(
			elanpri->efd, efd_verify_scan_cb, (void *) dev);
	if (efdrtn != EFD_OK_SUCCESS) {
		fp_err("(%s) efd_scan return %d\n", __func__, efdrtn);
		status = -EFAULT;
		goto err;
	}

	elanpri->stop_verify = 0;
	fpi_drvcb_verify_started(dev, status);
	goto exit;

err:
	//free memory space allocated before
	g_free(efd_template);
	elanpri->efd_list_free(elanpri->efd_db);

exit:
	return status;
}

static int enroll_stop(struct fp_dev *dev)
{
	fpi_drvcb_enroll_stopped(dev);
	return 0;
}

struct fp_print_data *save_enroll_template(struct fp_dev *dev)
{
	struct elan_pri *elanpri = FP_INSTANCE_DATA(dev);
	struct efd_template *template = NULL;
	struct fp_print_data *fdata = NULL;
	struct fp_print_data_item *item = NULL;

	template = elanpri->efd_enroll_done(elanpri->efd);
	if (template == NULL) {
		fp_err("(%s) efd_enroll_done failed\n", __func__);
		return fdata;
	}

	fdata = fpi_print_data_new(dev);
	item = fpi_print_data_item_new(template->data_length);
	memcpy(item->data, template->data, template->data_length);
	fpi_print_data_add_item(fdata, item);

	if (elanpri->efd_template_release(template) != EFD_OK_SUCCESS)
		fp_err("(%s) efd_template_release fail\n", __func__);

	return fdata;
}

static void efd_enroll_scan_cb(int result, void *user_data)
{
	struct fp_dev *dev = user_data;
	struct elan_pri *elanpri = FP_INSTANCE_DATA(dev);
	struct fp_print_data *fdata = NULL;
	struct fp_img *img = NULL;
	int efdrtn = EFD_OK_SUCCESS;
	int rtn = 0;

	if (result == EFD_SCAN_RETRY) {
		rtn = FP_ENROLL_RETRY;
		goto scan;
	/*} else if (result == EFD_NEED_CALIBRATION) {
		rtn = FP_ENROLL_RETRY_REMOVE_FINGER;
		goto scan;*/
	} else if (result != EFD_OK_SUCCESS) {
		fp_err("(%s) efd_scan return %d\n", __func__, result);
		rtn = FP_ENROLL_FAIL;
		goto err;
	}

	efdrtn = elanpri->efd_enroll_result(elanpri->efd);
	if (efdrtn == EFD_OK_SUCCESS) {
		rtn = FP_ENROLL_PASS;
		elanpri->enroll_stage++;
		fp_dbg("(%s) enroll_stage = %d\n", __func__, elanpri->enroll_stage);

		if (elanpri->enroll_stage == dev->nr_enroll_stages) {
			rtn = FP_ENROLL_COMPLETE;
			fdata = save_enroll_template(dev);
			if (fdata == NULL) {
				fp_err("(%s) save_enroll_template failed\n", __func__);
				rtn = FP_ENROLL_FAIL;
				goto err;
			}
		}
	} else if (efdrtn == EFD_ENROLL_DUPLICATE)
		rtn = FP_ENROLL_RETRY;
	else {
		fp_err("(%s) efd_scan return %d\n", __func__, efdrtn);
		rtn = FP_ENROLL_FAIL;
		goto err;
	}

	if (rtn == FP_ENROLL_COMPLETE ||
		rtn == FP_ENROLL_PASS)
		img = get_scan_image(dev);

scan:
	if (rtn == FP_ENROLL_PASS || rtn >= FP_ENROLL_RETRY) {
		efdrtn = elanpri->efd_scan(
				elanpri->efd, efd_enroll_scan_cb, (void *) dev);
		if (efdrtn != EFD_OK_SUCCESS) {
			fp_err("(%s) efd_scan return %d\n", __func__, efdrtn);
			rtn = FP_ENROLL_FAIL;
			goto err;
		}
	}

	goto exit;

err:
	if (elanpri->efd_enroll_abandon(elanpri->efd) != EFD_OK_SUCCESS)
		fp_err("(%s) efd_enroll_abandon fail\n", __func__);

exit:
	fpi_drvcb_enroll_stage_completed(dev, rtn, fdata, img);
}

static int enroll_start(struct fp_dev *dev)
{
	struct elan_pri *elanpri = FP_INSTANCE_DATA(dev);
	int efdrtn = 0;

	efdrtn = elanpri->efd_enroll_init(elanpri->efd);
	if (efdrtn != EFD_OK_SUCCESS) {
		fp_err("(%s) efd_enroll_init failed %d\n", __func__, efdrtn);
		return -EFAULT;
	}

	elanpri->enroll_stage = 0;
	efdrtn = elanpri->efd_scan(
			elanpri->efd, efd_enroll_scan_cb, (void *) dev);
	if (efdrtn != EFD_OK_SUCCESS) {
		fp_err("(%s) efd_scan return %d\n", __func__, efdrtn);
		return -EFAULT;
	}

	fpi_drvcb_enroll_started(dev, 0);
	return 0;
}

struct fp_driver elan_pri_driver = {
	.id = 1245,
	.name = FP_COMPONENT,
	.full_name = "ELAN Fingerprint Sensor",
	.id_table = id_table,
	.scan_type = FP_SCAN_TYPE_PRESS,
	.type = DRIVER_PRIMITIVE,
	.discover = dev_discover,
	.open = dev_init,
	.close = dev_exit,
	.enroll_start = enroll_start,
	.enroll_stop = enroll_stop,
	.verify_start = verify_start,
	.verify_stop = verify_stop,
	.identify_start = identify_start,
	.identify_stop = identify_stop,
	.capture_start = capture_start,
	.capture_stop = capture_stop,
};

struct fp_driver *fp_shared_driver = &elan_pri_driver;

