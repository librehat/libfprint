/*
 * Goodix Moc driver for libfprint
 * Copyright (C) 2019 Shenzhen Goodix Technology Co., Ltd.
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
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define PACKAGE_CRC_SIZE (4)
#define PACKAGE_HEADER_SIZE (8)

#define FP_MAX_FINGERNUM (10)

#define TEMPLATE_ID_SIZE (32)

#define GX_VERSION_LEN (8)

/* Type covert */
#define MAKE_CMD_EX(cmd0, cmd1)    ((uint16_t) (((cmd0) << 8) | (cmd1)))
#define MAKEWORDIDX(value, index)  ((value[index + 1] << 8)  | (value[index]))
#define MAKEDWORDIDX(value, index) ((value[index + 3] << 24) | (value[index + 2] << 16) | (value[index + 1] << 8) | (value[index]))
#define LOBYTE(value) ((uint8_t) (value))
#define HIBYTE(value) ((uint8_t) (((uint16_t) (value) >> 8) & 0xFF))


/* Error code */
#define GX_SUCCESS 0x00
#define GX_FAILED 0x80
#define GX_ERROR_FINGER_ID_NOEXIST 0x9C
#define GX_ERROR_TEMPLATE_INCOMPLETE 0xB8

/* Command Type Define */
#define RESPONSE_PACKAGE_CMD 0xAA

#define MOC_CMD0_ENROLL 0xA0
#define MOC_CMD0_ENROLL_INIT 0xA1
#define MOC_CMD0_CAPTURE_DATA 0xA2
#define MOC_CMD0_CHECK4DUPLICATE 0xA3
#define MOC_CMD0_COMMITENROLLMENT 0xA4

#define MOC_CMD0_IDENTIFY 0xA5
#define MOC_CMD0_GETFINGERLIST 0xA6
#define MOC_CMD0_DELETETEMPLATE 0xA7

#define MOC_CMD1_DEFAULT 0x00
#define MOC_CMD1_UNTIL_DOWN 0x00
#define MOC_CMD1_WHEN_DOWN 0x01

#define MOC_CMD1_DELETE_TEMPLATE 0x04
#define MOC_CMD1_DELETE_ALL 0xFF

#define MOC_CMD0_GET_VERSION 0xD0

#define MOC_CMD0_UPDATE_CONFIG 0xC0
#define MOC_CMD1_NWRITE_CFG_TO_FLASH 0x00
#define MOC_CMD1_WRITE_CFG_TO_FLASH 0x01
/* */

typedef struct _fp_version_info
{
  uint8_t format[2];
  uint8_t fwtype[GX_VERSION_LEN];
  uint8_t fwversion[GX_VERSION_LEN];
  uint8_t customer[GX_VERSION_LEN];
  uint8_t mcu[GX_VERSION_LEN];
  uint8_t sensor[GX_VERSION_LEN];
  uint8_t algversion[GX_VERSION_LEN];
  uint8_t interface[GX_VERSION_LEN];
  uint8_t protocol[GX_VERSION_LEN];
  uint8_t flashVersion[GX_VERSION_LEN];
  uint8_t reserved[62];
} fp_version_info_t, *pfp_version_info_t;


typedef struct _fp_parse_msg
{
  uint8_t ack_cmd;
  bool    has_no_config;
} fp_parse_msg_t, *pfp_parse_msg_t;


typedef struct _fp_enroll_init
{
  uint8_t tid[TEMPLATE_ID_SIZE];
} fp_enroll_init_t, *pfp_enroll_init_t;

#pragma pack(push, 1)
typedef struct _template_format
{
  uint8_t type;
  uint8_t finger_index;
  uint8_t accountid[32];
  uint8_t tid[32];
  struct
  {
    uint32_t size;
    uint8_t  data[56];
  } payload;
  uint8_t reserve[2];
} template_format_t, *ptemplate_format_t;

#pragma pack(pop)


typedef struct _fp_verify
{
  bool     match;
  uint32_t rejectdetail;
  template_format_t  template;
} fp_verify_t, *pfp_verify_t;


typedef struct _fp_capturedata
{
  uint8_t img_quality;
  uint8_t img_coverage;
} fp_capturedata_t, *pfp_capturedata_t;

typedef struct _fp_check_duplicate
{
  bool duplicate;
  template_format_t  template;
} fp_check_duplicate_t, *pfp_check_duplicate_t;


typedef struct _fp_enroll_update
{
  bool    rollback;
  uint8_t img_overlay;
  uint8_t img_preoverlay;
} fp_enroll_update_t, *Pfp_enroll_update_t;

typedef struct _fp_enum_fingerlist
{
  uint8_t           finger_num;
  template_format_t finger_list[FP_MAX_FINGERNUM];
} fp_enum_fingerlist_t, *pfp_enum_fingerlist_t;

typedef struct _fp_enroll_commit
{
  uint8_t result;
} fp_enroll_commit_t, *pfp_enroll_commit_t;


typedef struct _fp_cmd_response
{
  uint8_t result;
  union
  {
    fp_parse_msg_t       parse_msg;
    fp_verify_t          verify;
    fp_enroll_init_t     enroll_init;
    fp_capturedata_t     capture_data_resp;
    fp_check_duplicate_t check_duplicate_resp;
    fp_enroll_commit_t   enroll_commit;
    fp_enroll_update_t   enroll_update;
    fp_enum_fingerlist_t finger_list_resp;
    fp_version_info_t    version_info;
  };
} fp_cmd_response_t, *pfp_cmd_response_t;


typedef struct _pack_header
{
  uint8_t  cmd0;
  uint8_t  cmd1;
  uint8_t  packagenum;
  uint8_t  reserved;
  uint16_t len;
  uint8_t  crc8;
  uint8_t  rev_crc8;
} pack_header, *ppack_header;


typedef struct _fp_sensor_config
{
  uint8_t config[26];
  uint8_t reserved[98];
  uint8_t crc_value[4];
} fp_sensor_cfg_t, *pfp_sensor_cfg_t;
/* */

int gx_proto_build_package (uint8_t       *ppackage,
                            uint32_t      *package_len,
                            uint16_t       cmd,
                            const uint8_t *payload,
                            uint32_t       payload_size);

int gx_proto_parse_header (uint8_t     *buffer,
                           uint32_t     buffer_len,
                           pack_header *pheader);

int gx_proto_parse_body (uint16_t           cmd,
                         uint8_t           *buffer,
                         uint32_t           buffer_len,
                         pfp_cmd_response_t presponse);

int gx_proto_init_sensor_config (pfp_sensor_cfg_t pconfig);

uint8_t gx_proto_crc8_calc (uint8_t *lubp_date,
                            uint32_t lui_len);

uint8_t gx_proto_crc32_calc (uint8_t *pchMsg,
                             uint32_t wDataLen,
                             uint8_t *pchMsgDst);
