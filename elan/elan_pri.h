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

#pragma once

#include <libusb.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <dlfcn.h>

#define ELAN_VEND_ID			0x04F3

#define ENROLL_STAGE_DEFAULT	18
#define ENROLL_STAGE_0C42		12

extern struct fp_driver *fp_shared_driver;

static const struct usb_id id_table[] = {
	{ .vendor = ELAN_VEND_ID, .product = 0x0C42},
	{ 0, 0, 0, }, /* terminating entry */
};

//enum and struct for libefd.so
enum EFD_RETURN {
	EFD_OK_SUCCESS			= 0,
	EFD_OUT_OF_MEM_ERR		= -1,
	EFD_ARGUMENT_ERR		= -2,
	EFD_ALGORITHM_ERR		= -3,
	EFD_DEVICE_ERR			= -4,
	EFD_NEED_CALIBRATION	= -5,
	EFD_TRANSMIT_ERR		= -6,
	EFD_TIMEOUT_ERR			= -7,
	EFD_SCAN_RETRY			= -8,
	EFD_ENROLL_DUPLICATE	= -9,
	EFD_VERIFY_NOT_MATCH	= -10
};

enum EFD_LOG_LEVEL {
	EFD_LOG_EMERG,
	EFD_LOG_ALERT,
	EFD_LOG_CRIT,
	EFD_LOG_ERR,
	EFD_LOG_WARNING,
	EFD_LOG_NOTICE,
	EFD_LOG_INFO,
	EFD_LOG_DEBUG,
	EFD_LOG_DEFAULT = EFD_LOG_ERR
};

struct efd_list {
	void *data;
	struct efd_list *next;
};

struct efd_dimension {	//start from 1, not 0
	uint16_t width;
	uint16_t height;
};

struct efd_background {
	uint16_t *raw;
	uint16_t mean;
	uint16_t dac;
};

typedef int (*efd_dev_handle_events_fn)(void);

struct efd_dev {
	libusb_device_handle *handle;
	efd_dev_handle_events_fn handle_events;
	uint16_t product_id;
};

struct efd {
	struct efd_dev *dev;
	struct efd_enroll *enroll;
	struct efd_verify *verify;
	uint16_t fw_ver;
	uint16_t sensor_gen;
	struct efd_dimension *sensor_dim;
	struct efd_background *sensor_base;
	uint8_t *img_finger;
};

typedef void (*efd_scan_cb_fn)(int result, void *user_data);

struct efd_template {
	size_t data_length;
	uint8_t *data;
};

struct efd_enroll {
	uint8_t *img_finger;
};

struct efd_verify {
	uint8_t **template_db;
	size_t *template_size;
	size_t	template_count;
	uint8_t match_index;
	uint8_t *img_finger;
};
