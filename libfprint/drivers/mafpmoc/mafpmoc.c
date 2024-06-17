#define FP_COMPONENT "mafpmoc"

#include "drivers_api.h"
#include "mafpmoc.h"

struct _FpiDeviceMafpmoc
{
  FpDevice          parent;
  FpiSsm           *task_ssm;
  FpiSsm           *cmd_ssm;
  FpiUsbTransfer   *cmd_transfer;
  gboolean          cmd_cancelable;
  gboolean          cmd_force_pass;
  gint              enroll_stage;
  gint              max_enroll_stage;
  gint              max_stored_prints;
  guint8            interface_num;
  guint8            press_state;
  gint32            finger_status;
  gchar            *serial_number;
  gint16            enroll_id;
  gchar            *enroll_user_id;
  guint             enroll_identify_index;
  guint16           enroll_identify_id;
  guint8            enroll_identify_state;
  guint8            enroll_dupl_del_state;
  guint8            enroll_dupl_area_state;
  pmafp_templates_t templates;
  gint16            search_id;
  guint             capture_cnt;
  FpPrint          *identify_new_print;
  FpPrint          *identify_match_print;
};

G_DEFINE_TYPE (FpiDeviceMafpmoc, fpi_device_mafpmoc, FP_TYPE_DEVICE)

typedef void (*SynCmdMsgCallback) (FpiDeviceMafpmoc    *self,
                                   mafp_cmd_response_t *resp,
                                   GError              *error);

typedef struct
{
  gint16            cmd;
  SynCmdMsgCallback callback;
  FpiUsbTransfer   *cmd_transfer;
  gboolean          cmd_cancelable;
  guint16           cmd_request_len;
  guint16           cmd_actual_len;
  uint8_t           recv_buffer[MAFP_USB_BUFFER_SIZE];
  gboolean          cmd_force_pass;
  guint16           crc;
} CommandData;

static void mafp_sensor_cmd (FpiDeviceMafpmoc *self,
                             gint16            cmd,
                             const guint8     *data,
                             guint8            data_len,
                             SynCmdMsgCallback callback);

static uint16_t
ma_protocol_crc16_calc (
  uint8_t *data,
  uint32_t data_len,
  uint32_t start)
{
  const uint8_t *temp = data;
  uint32_t sum = 0;
  uint32_t i;

  for (i = start; i < data_len; temp++, i++)
    sum += *(temp + start) & 0xff;
  uint16_t sum_s = (sum & 0xffff);

  return sum_s;
}

static void
init_pack_header (
  ppack_header pheader,
  uint16_t     frame_len)
{
  g_assert (pheader);

  memset (pheader, 0, sizeof (*pheader));
  pheader->head0 = 0xEF;
  pheader->head1 = 0x01;
  pheader->addr0 = 0xFF;
  pheader->addr1 = 0xFF;
  pheader->addr2 = 0xFF;
  pheader->addr3 = 0xFF;
  pheader->flag = (uint8_t) PACK_CMD;
  pheader->frame_len0 = (frame_len >> 8) & 0xff;
  pheader->frame_len1 = frame_len & 0xff;
}

/* data tansfer:
 *      while cmd_len = 0, put flag(end or not) in data[0]
 */
static uint8_t *
ma_protocol_build_package (
  uint32_t       package_len,
  int16_t        cmd,
  uint8_t        cmd_len,
  const uint8_t *data,
  uint32_t       data_len)
{
  g_autofree uint8_t *ppackage = g_new0 (uint8_t, package_len);
  pack_header header;

  init_pack_header (&header, package_len - PACKAGE_HEADER_SIZE);
  if (!cmd_len && data_len)
    header.flag = data[0];

  memcpy (ppackage, &header, PACKAGE_HEADER_SIZE);

  if (cmd_len)
    memcpy (ppackage + PACKAGE_HEADER_SIZE, &cmd, 1);

  if (data_len)
    memcpy (ppackage + PACKAGE_HEADER_SIZE + cmd_len, data + !cmd_len, data_len);

  uint16_t crc = ma_protocol_crc16_calc (ppackage, PACKAGE_HEADER_SIZE + cmd_len + data_len, 6);

  ppackage[package_len - 2] = (crc >> 8) & 0xFF;
  ppackage[package_len - 1] = crc & 0xFF;

/* gchar msg[1024] = {0};
   gchar pack_str[16] = {0};
   for (int i = 0; i < package_len; i++)
    {
      sprintf(pack_str, "0x%x ", ppackage[i]);
      strcat(msg, pack_str);
    }
   fp_dbg("build pack %s", msg);
 */
  return g_steal_pointer (&ppackage);
}

static int
ma_protocol_parse_header (
  uint8_t     *buffer,
  uint32_t     buffer_len,
  pack_header *pheader)
{
  if (!buffer || !pheader || buffer_len < PACKAGE_HEADER_SIZE)
    return -1;

  memcpy (pheader, buffer, sizeof (pack_header));
  return 0;
}

static uint8_t
get_one_bit_value (
  uint8_t src,
  uint8_t bit_num)
{
  return (uint8_t) ((src >> (bit_num - 1)) & 1);
}

static int
ma_protocol_parse_body (
  int16_t              cmd,
  uint8_t             *buffer,
  uint16_t             buffer_len,
  pmafp_cmd_response_t presp)
{
  const int data_len = buffer_len - 1 - PACKAGE_CRC_SIZE;

  if (!buffer || !presp || buffer_len < 1)
    return -1;

  presp->result = buffer[0];

  switch (cmd)
    {
    case MOC_CMD_HANDSHAKE:
      if (data_len >= sizeof (mafp_handshake_t))
        memcpy (&presp->handshake, buffer + 1, sizeof (mafp_handshake_t));
      break;

    case MOC_CMD_SEARCH:
      if (data_len >= sizeof (mafp_search_t))
        memcpy (&presp->search, buffer + 1, sizeof (mafp_search_t));
      break;

    case MOC_CMD_GET_TEMPLATE_NUM:
      if (data_len >= 2)
        presp->tpl_table.used_num = ((buffer[1] & 0xff) << 8) | (buffer[2] & 0xff);
      break;

    case MOC_CMD_GET_TEMPLATE_TABLE:
      if (data_len >= 32)
        {
          uint16_t num = 0;
          for (uint8_t i = 1; i < 33; i++)
            {
              uint8_t data = buffer[i];
              for (uint8_t bit = 1; bit <= 8 && num < sizeof (presp->tpl_table.list); bit++, num++)
                presp->tpl_table.list[num] = get_one_bit_value (data, bit);
            }
        }
      break;

    case MOC_CMD_GET_TEMPLATE_INFO:
      if (data_len >= 128)
        memcpy (&presp->tpl_info, buffer + 1, sizeof (mafp_tpl_info_t));
      break;

    case MOC_CMD_DUPAREA_TEST:
      if (data_len >= 1)
        presp->result = buffer[1];
      break;

    default:
      memcpy (presp, buffer, buffer_len);
      break;
    }
  return 0;
}

static void
mafp_clean_usb_bulk_in (FpDevice *device)
{
  g_autoptr(FpiUsbTransfer) transfer = fpi_usb_transfer_new (device);
  fpi_usb_transfer_fill_bulk (transfer, MAFP_EP_BULK_IN, MAFP_USB_BUFFER_SIZE);
  GError *error = NULL;

  fp_dbg ("bulk clean");
  if (!fpi_usb_transfer_submit_sync (transfer, 200, &error))
    fp_dbg ("bulk transfer out fail, %s", error->message);
}

static G_GNUC_PRINTF (4, 5) void
mafp_mark_failed (
  FpDevice *dev,
  FpiSsm *ssm,
  guint8 err_code,
  const gchar *msg, ...)
{
  if (err_code == FP_DEVICE_ERROR_PROTO)
    mafp_clean_usb_bulk_in (dev);

  if (msg == NULL)
    {
      fpi_ssm_mark_failed (ssm, fpi_device_error_new (err_code));
    }
  else
    {
      va_list args;
      va_start (args, msg);
      fpi_ssm_mark_failed (ssm, g_error_new_valist (FP_DEVICE_ERROR, err_code, msg, args));
      va_end (args);
    }
}

static void
fp_cmd_receive_cb (FpiUsbTransfer *transfer,
                   FpDevice       *device,
                   gpointer        user_data,
                   GError         *error)
{
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (device);
  CommandData *data = user_data;
  int ret = -1, ssm_state = 0;
  mafp_cmd_response_t cmd_reponse = {0, };
  pack_header header;
  uint32_t data_index = 0;
  gchar msg[1024] = {0};

  if (error)
    {
      fp_dbg ("error: %d", error->code);
      if (data->cmd_force_pass) //ex: G_USB_DEVICE_ERROR_TIMED_OUT
        {
          if (data->callback)
            data->callback (self, &cmd_reponse, NULL);
          fpi_ssm_mark_completed (transfer->ssm);
          return;
        }
      fpi_ssm_mark_failed (transfer->ssm, error);
      return;
    }
  if (data == NULL)
    {
      fp_dbg ("data null");
      mafp_mark_failed (device, transfer->ssm, FP_DEVICE_ERROR_PROTO, "resp data null");
      return;
    }
  ssm_state = fpi_ssm_get_cur_state (transfer->ssm);

  //fp_dbg("actual_length: %d", transfer->actual_length);

  /* skip zero length package */
  if (transfer->actual_length == 0)
    {
      fpi_ssm_jump_to_state (transfer->ssm, ssm_state);
      return;
    }

  if (ssm_state == FP_CMD_RECEIVE)
    {
      ret = ma_protocol_parse_header (transfer->buffer, transfer->actual_length, &header);
      if (ret != 0 || header.flag != PACK_ANSWER)
        {
          mafp_mark_failed (device, transfer->ssm, FP_DEVICE_ERROR_PROTO, "Corrupted resp header received");
          return;
        }
      data->cmd_request_len = ((header.frame_len0 & 0xff) << 8) | (header.frame_len1 & 0xff);
      if (!data->cmd_request_len)
        {
          mafp_mark_failed (device, transfer->ssm, FP_DEVICE_ERROR_PROTO, "Corrupted resp length received");
          return;
        }
      data_index = PACKAGE_HEADER_SIZE;
    }
  memcpy (data->recv_buffer + data->cmd_actual_len, transfer->buffer, transfer->actual_length);
  data->cmd_actual_len += transfer->actual_length - data_index;
  if (PRINT_CMD)
    {
      for (int i = 0; i < PACKAGE_HEADER_SIZE + data->cmd_actual_len && i < 1024; i++)
        sprintf (msg + i * 3, "%02X ", data->recv_buffer[i]);
      fp_dbg ("RECV: %s", msg);
    }
  // fp_dbg("cmd_request_len: %d, cmd_actual_len: %d", data->cmd_request_len, data->cmd_actual_len);

  if (data->cmd_request_len <= data->cmd_actual_len)
    {
      ret = ma_protocol_parse_body (data->cmd, &data->recv_buffer[PACKAGE_HEADER_SIZE],
                                    data->cmd_request_len, &cmd_reponse);
      if (ret != 0)
        {
          mafp_mark_failed (device, transfer->ssm, FP_DEVICE_ERROR_PROTO, "Corrupted resp body received");
          return;
        }
      uint32_t no_crc_len = PACKAGE_HEADER_SIZE + data->cmd_request_len - PACKAGE_CRC_SIZE;
      data->crc = ma_protocol_crc16_calc (&data->recv_buffer[0], no_crc_len, 6);
      uint16_t frame_crc = ((data->recv_buffer[no_crc_len] & 0xff) << 8)
                           | (data->recv_buffer[no_crc_len + 1] & 0xff);
      if (data->crc != frame_crc)
        {
          mafp_mark_failed (device, transfer->ssm, FP_DEVICE_ERROR_PROTO, "Package crc check failed");
          return;
        }
      if (data->callback)
        data->callback (self, &cmd_reponse, NULL);
      fpi_ssm_mark_completed (transfer->ssm);

    }
  else if (data->cmd_request_len > data->cmd_actual_len)
    {
      fpi_ssm_next_state (transfer->ssm);
      return;
    }
}

static void
fp_cmd_run_state (FpiSsm   *ssm,
                  FpDevice *dev)
{
  FpiUsbTransfer *transfer;
  CommandData *data = fpi_ssm_get_data (ssm);
  gchar msg[1024] = {0};

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case FP_CMD_SEND:
      if (data->cmd_transfer)
        {
          data->cmd_transfer->ssm = ssm;
          if (PRINT_CMD)
            {
              for (int i = 0; i < data->cmd_transfer->length && i < 1024; i++)
                sprintf (msg + i * 3, "%02X ", data->cmd_transfer->buffer[i]);
              fp_dbg ("SEND: %s", msg);
            }
          fpi_usb_transfer_submit (g_steal_pointer (&data->cmd_transfer),
                                   CMD_TIMEOUT, NULL, fpi_ssm_usb_transfer_cb, NULL);
        }
      else
        {
          fpi_ssm_next_state (ssm);
        }
      break;

    case FP_CMD_RECEIVE:
      transfer = fpi_usb_transfer_new (dev);
      transfer->ssm = ssm;
      fpi_usb_transfer_fill_bulk (transfer, MAFP_EP_BULK_IN, MAFP_USB_BUFFER_SIZE);
      fpi_usb_transfer_submit (transfer,
                               data->cmd_cancelable ? 0 : data->cmd_force_pass ? CTRL_TIMEOUT : CMD_TIMEOUT,
                               data->cmd_cancelable ? fpi_device_get_cancellable (dev) : NULL,
                               fp_cmd_receive_cb,
                               fpi_ssm_get_data (ssm));
      break;

    case FP_DATA_RECEIVE:
      fp_dbg ("req: %d, act: %d", data->cmd_request_len, data->cmd_actual_len);
      int req_len = MAFP_USB_BUFFER_SIZE;
      if (data->cmd_request_len > 0 && data->cmd_actual_len > 0 && (data->cmd_request_len > data->cmd_actual_len))
        req_len = data->cmd_request_len - data->cmd_actual_len;
      transfer = fpi_usb_transfer_new (dev);
      transfer->ssm = ssm;
      fpi_usb_transfer_fill_bulk (transfer, MAFP_EP_BULK_IN, req_len);
      fpi_usb_transfer_submit (transfer,
                               data->cmd_cancelable ? 0 : DATA_TIMEOUT,
                               data->cmd_cancelable ? fpi_device_get_cancellable (dev) : NULL,
                               fp_cmd_receive_cb,
                               fpi_ssm_get_data (ssm));
      break;
    }
}

static void
fp_cmd_ssm_done (FpiSsm *ssm, FpDevice *dev, GError *error)
{
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (dev);
  CommandData *data = fpi_ssm_get_data (ssm);

  self->cmd_ssm = NULL;
  if (error)
    {
      if (data->callback)
        data->callback (self, NULL, error);
      else
        g_error_free (error);
    }
}

static void
fp_cmd_ssm_done_data_free (CommandData *data)
{
  g_free (data);
}

static FpiUsbTransfer *
alloc_cmd_transfer (FpiDeviceMafpmoc *self,
                    gint16            cmd,
                    guint8            cmd_len,
                    const guint8     *data,
                    guint32           data_len)
{
  g_return_val_if_fail (data || data_len == 0, NULL);
  FpDevice *dev = FP_DEVICE (self);

  g_autoptr(FpiUsbTransfer) transfer = fpi_usb_transfer_new (dev);
  uint32_t total_len = PACKAGE_HEADER_SIZE + cmd_len + data_len + PACKAGE_CRC_SIZE;
  guint8 *buffer = ma_protocol_build_package (total_len, cmd, cmd_len, data, data_len);

  fpi_usb_transfer_fill_bulk_full (transfer, MAFP_EP_BULK_OUT, buffer, total_len, g_free);
  return g_steal_pointer (&transfer);
}

static void
mafp_sensor_cmd (FpiDeviceMafpmoc *self,
                 gint16            cmd,
                 const guint8     *data,
                 guint8            data_len,
                 SynCmdMsgCallback callback)
{
  g_autoptr(FpiUsbTransfer) transfer = alloc_cmd_transfer (self, cmd, (cmd < 0 ? 0 : 1), data, data_len);

  CommandData *cmd_data = g_new0 (CommandData, 1);

  cmd_data->cmd = cmd;
  cmd_data->callback = callback;
  cmd_data->cmd_transfer = g_steal_pointer (&transfer);
  cmd_data->cmd_cancelable = FALSE;
  cmd_data->cmd_force_pass = self->cmd_force_pass;
  cmd_data->cmd_request_len = 0;
  cmd_data->cmd_actual_len = 0;
  self->cmd_force_pass = FALSE;

  self->cmd_ssm = fpi_ssm_new (FP_DEVICE (self), fp_cmd_run_state, FP_TRANSFER_STATES);
  if (!PRINT_SSM_DEBUG)
    fpi_ssm_silence_debug (self->cmd_ssm);
  fpi_ssm_set_data (self->cmd_ssm, cmd_data, (GDestroyNotify) fp_cmd_ssm_done_data_free);
  fpi_ssm_start (self->cmd_ssm, fp_cmd_ssm_done);
}

static void
mafp_sensor_control (FpiDeviceMafpmoc      *self,
                     guint8                 request,
                     guint16                value,
                     FpiUsbTransferCallback callback,
                     gpointer               user_data,
                     guint16                timeout)
{
  FpiUsbTransfer *transfer = fpi_usb_transfer_new (FP_DEVICE (self));

  transfer->ssm = self->task_ssm;
  fpi_usb_transfer_fill_control (transfer,
                                 G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
                                 G_USB_DEVICE_REQUEST_TYPE_VENDOR,
                                 G_USB_DEVICE_RECIPIENT_DEVICE, request, value, 0, 1);
  fpi_usb_transfer_submit (transfer, timeout ? timeout : CTRL_TIMEOUT, NULL, callback, user_data);
  return;
}

static mafp_template_t
mafp_template_from_print (FpPrint *print)
{
  const guint16 tpl_id;

  g_autoptr(GVariant) data = NULL;
  g_autoptr(GVariant) tpl_uid = NULL;
  g_autoptr(GVariant) dev_sn = NULL;
  const char *user_id;
  const char *serial_num;
  gsize user_id_len = 0;
  gsize serial_num_len = 0;
  mafp_template_t template;

  g_object_get (print, "fpi-data", &data, NULL);
  g_variant_get (data, "(q@ay@ay)", &tpl_id, &tpl_uid, &dev_sn);
  user_id = g_variant_get_fixed_array (tpl_uid, &user_id_len, 1);
  serial_num = g_variant_get_fixed_array (dev_sn, &serial_num_len, 1);

  template.id = tpl_id;
  memset (template.uid, 0, TEMPLATE_UID_SIZE);
  memcpy (template.uid, user_id, user_id_len);
  memset (template.sn, 0, DEVICE_SN_SIZE);
  memcpy (template.sn, serial_num, serial_num_len);

  return template;
}

static FpPrint *
mafp_print_from_template (FpiDeviceMafpmoc *self, mafp_template_t template)
{
  FpPrint *print;
  GVariant *data;
  GVariant *uid;
  GVariant *dev_sn;
  guint user_id_len;
  guint serial_num_len;

  print = fp_print_new (FP_DEVICE (self));

  user_id_len = strlen (template.uid);
  user_id_len = MIN (TEMPLATE_UID_SIZE, user_id_len);
  uid = g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, template.uid, user_id_len, 1);

  serial_num_len = strlen (self->serial_number);
  dev_sn = g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, self->serial_number, serial_num_len, 1);
  fp_dbg ("print: %d/%s/%s", template.id, template.uid, self->serial_number);

  data = g_variant_new ("(q@ay@ay)", template.id, uid, dev_sn);

  fpi_print_set_type (print, FPI_PRINT_RAW);
  fpi_print_set_device_stored (print, true);
  g_object_set (print, "description", template.uid, NULL);
  g_object_set (print, "fpi-data", data, NULL);

  fpi_print_fill_from_user_id (print, template.uid);

  return print;
}

static void
mafp_load_enrolled_ids (FpiDeviceMafpmoc *self, mafp_cmd_response_t *resp)
{
  uint16_t num = 0;
  gchar msg[1024] = {0};
  gchar id_str[16] = {0};

  for (uint16_t i = 0; i < sizeof (resp->tpl_table.list); i++)
    {
      if (resp->tpl_table.list[i])
        {
          self->templates->total_list[num++].id = i;
          sprintf (id_str, "%d ", i);
          strcat (msg, id_str);
        }
    }
  self->templates->index = 0;
  self->templates->total_num = num;
  fp_dbg ("enrolled ids: %s", msg);
  fp_dbg ("enrolled num: %d", self->templates->total_num);
}

static void
fp_init_handeshake_cb (FpiDeviceMafpmoc    *self,
                       mafp_cmd_response_t *resp,
                       GError              *error)
{
  FpDevice *dev = FP_DEVICE (self);

  if (error)
    {
      fp_dbg ("handshake fail");
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  fp_dbg ("result: %d", resp->result);

  if (resp->result == MAFP_SUCCESS &&
      resp->handshake.code[0] == MAFP_HANDSHAKE_CODE1 &&
      resp->handshake.code[1] == MAFP_HANDSHAKE_CODE2)
    {
      self->max_enroll_stage = DEFAULT_ENROLL_SAMPLES;
      gchar * value = getenv (MAFP_ENV_ENROLL_SAMPLES);
      if (value)
        self->max_enroll_stage = g_ascii_strtoll (value, NULL, 10);
      fp_dbg ("max_enroll_stage: %d", self->max_enroll_stage);
      fpi_device_set_nr_enroll_stages (dev, self->max_enroll_stage);

      fpi_ssm_next_state (self->task_ssm);
    }
  else
    {
      mafp_mark_failed (dev, self->task_ssm, FP_DEVICE_ERROR_GENERAL,
                        "Failed to handshake, result: 0x%x", resp->result);
    }
}

static void
fp_init_module_status_cb (FpiDeviceMafpmoc    *self,
                          mafp_cmd_response_t *resp,
                          GError              *error)
{
  if (error)
    resp->result = 0xff;

  fp_dbg ("result: %d", resp->result);

  if ((resp->result & MAFP_RE_CALIBRATE_ERROR) == MAFP_RE_CALIBRATE_ERROR)
    fp_dbg ("no calibrate data");

  fpi_ssm_next_state (self->task_ssm);
}

static void
fp_init_clean_epin_cb (FpiUsbTransfer *transfer,
                       FpDevice       *device,
                       gpointer        user_data,
                       GError         *error)
{
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (device);

  fpi_ssm_next_state (self->task_ssm);
}

static void
fp_init_clean_epout_cb (FpiUsbTransfer *transfer,
                        FpDevice       *device,
                        gpointer        user_data,
                        GError         *error)
{
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (device);

  fpi_ssm_next_state (self->task_ssm);
}

static void
fp_init_run_state (FpiSsm *ssm, FpDevice *device)
{
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (device);
  FpiUsbTransfer *transfer;

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case FP_INIT_CLEAN_EPIN:
      transfer = fpi_usb_transfer_new (device);
      fpi_usb_transfer_fill_bulk (transfer, MAFP_EP_BULK_IN, MAFP_USB_BUFFER_SIZE);
      fpi_usb_transfer_submit (transfer, 100, NULL, fp_init_clean_epin_cb, NULL);
      break;

    case FP_INIT_CLEAN_EPOUT:
      transfer = fpi_usb_transfer_new (device);
      fpi_usb_transfer_fill_bulk (transfer, MAFP_EP_BULK_OUT, MAFP_USB_BUFFER_SIZE);
      fpi_usb_transfer_submit (transfer, 100, NULL, fp_init_clean_epout_cb, NULL);
      break;

    case FP_INIT_CLEAN_EPIN2:
      transfer = fpi_usb_transfer_new (device);
      fpi_usb_transfer_fill_bulk (transfer, MAFP_EP_BULK_IN, MAFP_USB_BUFFER_SIZE);
      fpi_usb_transfer_submit (transfer, 100, NULL, fp_init_clean_epin_cb, NULL);
      break;

    case FP_INIT_HANDSHAKE:
      mafp_sensor_cmd (self, MOC_CMD_HANDSHAKE, NULL, 0, fp_init_handeshake_cb);
      break;

    case FP_INIT_MODULE_STATUS:
      self->cmd_force_pass = TRUE;
      mafp_sensor_cmd (self, MOC_CMD_GET_INIT_STATUS, NULL, 0, fp_init_module_status_cb);
      break;
    }
}

static void
fp_init_ssm_done (FpiSsm *ssm, FpDevice *dev, GError *error)
{
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (dev);

  if (error)
    {
      fp_dbg ("%d %s", error->code, error->message);
      fpi_device_open_complete (dev, error);
      return;
    }
  self->task_ssm = NULL;
  fpi_device_open_complete (dev, NULL);
}

static void
fp_enroll_tpl_table_cb (FpiDeviceMafpmoc    *self,
                        mafp_cmd_response_t *resp,
                        GError              *error)
{
  FpDevice *dev = FP_DEVICE (self);

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  fp_dbg ("result: %d", resp->result);

  if (resp->result == MAFP_SUCCESS)
    {
      mafp_load_enrolled_ids (self, resp);
      self->enroll_id = -1;
      for (uint16_t i = 0; i < sizeof (resp->tpl_table.list); i++)
        {
          if (!resp->tpl_table.list[i])
            {
              self->enroll_id = i;
              break;
            }
        }
      if (self->enroll_id < 0)
        {
          mafp_mark_failed (dev, self->task_ssm, FP_DEVICE_ERROR_DATA_FULL,
                            "fingerprints total num reached max");
          return;
        }
      fpi_ssm_next_state (self->task_ssm);
    }
  else
    {
      mafp_mark_failed (dev, self->task_ssm, FP_DEVICE_ERROR_GENERAL,
                        "Failed to get fingerprints index, result: 0x%x", resp->result);
    }
}

static void
fp_enroll_read_tpl_cb (FpiDeviceMafpmoc    *self,
                       mafp_cmd_response_t *resp,
                       GError              *error)
{
  FpDevice *dev = FP_DEVICE (self);

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  fp_dbg ("result: %d", resp->result);

  guint8 *resp_buff = (guint8 *) resp;
  guint16 max_id = 0;

  if (resp->result == MAFP_SUCCESS)
    {
      max_id = resp_buff[1] * 256 + resp_buff[2];
      fp_dbg ("max_id: %d, %x %x %x %x", max_id, resp_buff[0], resp_buff[1], resp_buff[2], resp_buff[3]);
      if (self->enroll_id >= max_id)
        {
          mafp_mark_failed (dev, self->task_ssm, FP_DEVICE_ERROR_DATA_FULL,
                            "fingerprints total num reached max");
          return;
        }
    }
  else
    {
      mafp_mark_failed (dev, self->task_ssm, FP_DEVICE_ERROR_DATA_FULL,
                        "fingerprints query total num fail");
      return;
    }
  fpi_device_report_finger_status (dev, FP_FINGER_STATUS_NONE | FP_FINGER_STATUS_NEEDED);
  fpi_ssm_next_state (self->task_ssm);
}

static void
fp_enroll_get_image_cb (FpiDeviceMafpmoc    *self,
                        mafp_cmd_response_t *resp,
                        GError              *error)
{
  FpDevice *dev = FP_DEVICE (self);

  if (fpi_device_action_is_cancelled (dev))
    g_cancellable_set_error_if_cancelled (fpi_device_get_cancellable (dev), &error);
  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }

  FpEnrollState nextState = FP_ENROLL_VERIFY_GET_IMAGE;

  //fp_dbg("result: %d, press_state: %d", resp->result, self->press_state);
  if (self->press_state == MAFP_PRESS_WAIT_DOWN)
    {
      fp_dbg ("wait finger down state %d", resp->result);
      if (resp->result == MAFP_RE_GET_IMAGE_SUCCESS)
        {
          nextState = FP_ENROLL_VERIFY_GENERATE_FEATURE;
        }
      else if (resp->result == MAFP_RE_GET_IMAGE_NONE)
        {
          self->capture_cnt++;
          fp_dbg ("capture_cnt %d", self->capture_cnt);
          if (self->capture_cnt > MAFP_IMAGE_ERR_TRRIGER)
            nextState = FP_ENROLL_REFRESH_INT_PARA;
          else
            nextState = FP_ENROLL_DETECT_MODE;
        }
    }
  else if (self->press_state == MAFP_PRESS_WAIT_UP)
    {
      fp_dbg ("wait finger up state %d", resp->result);
      if (resp->result == MAFP_RE_GET_IMAGE_SUCCESS)
        {
          nextState = FP_ENROLL_VERIFY_GET_IMAGE;
        }
      else if (resp->result == MAFP_RE_GET_IMAGE_NONE)
        {
          self->press_state = MAFP_PRESS_WAIT_DOWN;
          fpi_device_report_finger_status (dev, FP_FINGER_STATUS_NONE | FP_FINGER_STATUS_NEEDED);
          nextState = FP_ENROLL_CHECK_INT_PARA;
        }
    }
  fpi_ssm_jump_to_state (self->task_ssm, nextState);
}

static void
fp_enroll_verify_search_cb (FpiDeviceMafpmoc    *self,
                            mafp_cmd_response_t *resp,
                            GError              *error)
{
  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  fp_dbg ("result: %d", resp->result);

  if (resp->result == MAFP_SUCCESS)
    {
      self->search_id = ((resp->search.id[0] & 0xff) << 8) | (resp->search.id[1] & 0xff);
      fp_dbg ("search_id: %d", self->search_id);
      fpi_ssm_jump_to_state (self->task_ssm, FP_ENROLL_GET_TEMPLATE_INFO);
    }
  else
    {
      self->search_id = -1;
      if (self->enroll_stage >= self->max_enroll_stage)
        fpi_ssm_jump_to_state (self->task_ssm, FP_ENROLL_SAVE_TEMPLATE_INFO);
      else
        fpi_ssm_jump_to_state (self->task_ssm, FP_ENROLL_VERIFY_GET_IMAGE);
    }
}

static void
fp_enroll_get_tpl_info_cb (FpiDeviceMafpmoc    *self,
                           mafp_cmd_response_t *resp,
                           GError              *error)
{
  FpDevice *dev = FP_DEVICE (self);
  mafp_template_t tpl;
  FpPrint *print;

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  fp_dbg ("result: %d, %s", resp->result, resp->tpl_info.uid);

  if (resp->result == MAFP_SUCCESS)
    {
      if (resp->tpl_info.uid[0] == 'F' && resp->tpl_info.uid[1] == 'P')
        {
          tpl.id = self->search_id;
          memcpy (tpl.uid, resp->tpl_info.uid, sizeof (resp->tpl_info.uid));
          print = mafp_print_from_template (self, tpl);
          mafp_mark_failed (dev, self->task_ssm, FP_DEVICE_ERROR_DATA_DUPLICATE,
                            "Finger was already enrolled as '%s'", fp_print_get_description (print));
          return;
        }
    }
  if (self->enroll_stage >= self->max_enroll_stage)
    fpi_ssm_jump_to_state (self->task_ssm, FP_ENROLL_SAVE_TEMPLATE_INFO);
  else
    fpi_ssm_jump_to_state (self->task_ssm, FP_ENROLL_VERIFY_GET_IMAGE);
}

static void
fp_enroll_once_complete_cb (FpiDeviceMafpmoc    *self,
                            mafp_cmd_response_t *resp)
{
  FpDevice *dev = FP_DEVICE (self);

  if (resp->result == MAFP_SUCCESS)
    {
      self->enroll_stage++;
      self->press_state = MAFP_PRESS_WAIT_UP;
      fpi_device_enroll_progress (dev, self->enroll_stage, NULL, NULL);

      if (self->enroll_identify_state == MAFP_ENROLL_IDENTIFY_DISABLED)
        {
          if (self->enroll_stage >= self->max_enroll_stage)
            fpi_ssm_jump_to_state (self->task_ssm, FP_ENROLL_SAVE_TEMPLATE_INFO);
          else
            fpi_ssm_jump_to_state (self->task_ssm, FP_ENROLL_VERIFY_GET_IMAGE);
          return;
        }
      if (self->enroll_identify_state == MAFP_ENROLL_IDENTIFY_ONCE)
        self->enroll_identify_state = MAFP_ENROLL_IDENTIFY_DISABLED;
      fpi_ssm_jump_to_state (self->task_ssm, FP_ENROLL_VERIFY_SEARCH);
    }
  else
    {
      self->press_state = MAFP_PRESS_WAIT_UP;
      fpi_device_enroll_progress (dev, self->enroll_stage, NULL,
                                  fpi_device_retry_new (FP_DEVICE_RETRY_GENERAL));
      fpi_ssm_jump_to_state (self->task_ssm, FP_ENROLL_VERIFY_GET_IMAGE);
    }
}

static void
fp_enroll_gen_feature_cb (FpiDeviceMafpmoc    *self,
                          mafp_cmd_response_t *resp,
                          GError              *error)
{
  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  fp_dbg ("result: %d", resp->result);

  if (self->enroll_dupl_area_state == MAFP_ENROLL_DUPLICATE_AREA_DENY)
    {
      int remain_stage = self->max_enroll_stage - self->enroll_stage;
      //check duplicate area in last 3 times
      if (remain_stage > 0 && remain_stage <= 3)
        {
          fpi_ssm_next_state (self->task_ssm);
          return;
        }
    }
  fp_enroll_once_complete_cb (self, resp);
}

static void
fp_enroll_verify_duparea_cb (FpiDeviceMafpmoc    *self,
                             mafp_cmd_response_t *resp,
                             GError              *error)
{
  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  fp_dbg ("result: %d", resp->result);

  if (resp->result != MAFP_SUCCESS)
    resp->result = 1;
  fp_enroll_once_complete_cb (self, resp);
}

static void
fp_enroll_save_tpl_info_cb (FpiDeviceMafpmoc    *self,
                            mafp_cmd_response_t *resp,
                            GError              *error)
{
  FpDevice *dev = FP_DEVICE (self);

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  fp_dbg ("result: %d", resp->result);

  if (resp->result == MAFP_RE_TPL_NUM_OVERSIZE)
    {
      mafp_mark_failed (dev, self->task_ssm, FP_DEVICE_ERROR_DATA_FULL,
                        "fingerprints total num reached max");
      return;
    }
  if (resp->result != MAFP_SUCCESS)
    {
      mafp_mark_failed (dev, self->task_ssm, FP_DEVICE_ERROR_GENERAL,
                        "Failed to save template info, result: 0x%x", resp->result);
      return;
    }
  fpi_ssm_next_state (self->task_ssm);
}

static void
fp_enroll_save_tpl_cb (FpiDeviceMafpmoc    *self,
                       mafp_cmd_response_t *resp,
                       GError              *error)
{
  FpDevice *dev = FP_DEVICE (self);
  FpPrint *print = NULL;
  GVariant *uid = NULL;
  GVariant *data = NULL;
  GVariant *dev_sn;
  guint user_id_len;
  guint serial_num_len;
  gchar *user_id = NULL;
  gchar *serial_num = NULL;

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  fp_dbg ("result: %d", resp->result);

  if (resp->result == MAFP_SUCCESS)
    {
      fpi_device_get_enroll_data (dev, &print);

      user_id = self->enroll_user_id;
      user_id_len = strlen (user_id);
      fp_dbg ("user_id(%d): %s", user_id_len, user_id);
      uid = g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, user_id, user_id_len, 1);

      serial_num = self->serial_number;
      serial_num_len = strlen (serial_num);
      fp_dbg ("dev_sn(%d): %s", serial_num_len, serial_num);
      dev_sn = g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, serial_num, serial_num_len, 1);

      data = g_variant_new ("(q@ay@ay)", self->enroll_id, uid, dev_sn);

      fpi_print_set_type (print, FPI_PRINT_RAW);
      fpi_print_set_device_stored (print, TRUE);
      g_object_set (print, "description", user_id, NULL);
      g_object_set (print, "fpi-data", data, NULL);

      fpi_ssm_jump_to_state (self->task_ssm, FP_ENROLL_EXIT);
      return;
    }
  fpi_ssm_next_state (self->task_ssm);
}

static void
fp_enroll_del_tpl_info_cb (FpiDeviceMafpmoc    *self,
                           mafp_cmd_response_t *resp,
                           GError              *error)
{
  FpDevice *dev = FP_DEVICE (self);

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  fp_dbg ("result: %d", resp->result);
  mafp_mark_failed (dev, self->task_ssm, FP_DEVICE_ERROR_GENERAL,
                    "Failed to save template, result: 0x%x", resp->result);
}

static void
mafp_sleep_cb (FpiDeviceMafpmoc    *self,
               mafp_cmd_response_t *resp,
               GError              *error)
{
  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  fpi_ssm_next_state (self->task_ssm);
}

static void
mafp_pwr_btn_shield_off_cb (FpiUsbTransfer *transfer,
                            FpDevice       *device,
                            gpointer        user_data,
                            GError         *error)
{
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (device);

  if (error)
    {
      fpi_ssm_mark_failed (transfer->ssm, error);
      return;
    }
  guint8 para = 0;

  mafp_sensor_cmd (self, MOC_CMD_SLEEP, &para, 1, mafp_sleep_cb);
}

static void
mafp_pwr_btn_shield_on_cb (FpiUsbTransfer *transfer,
                           FpDevice       *device,
                           gpointer        user_data,
                           GError         *error)
{
  if (error)
    {
      fpi_ssm_mark_failed (transfer->ssm, error);
      return;
    }
  fpi_ssm_next_state (transfer->ssm);
}

static void
mafp_pwr_btn_shield_on (FpiDeviceMafpmoc *self, int on)
{
  GError *pre_error = fpi_ssm_get_error (self->task_ssm);

  if (pre_error && pre_error->code == G_USB_DEVICE_ERROR_FAILED &&
      g_str_equal (g_quark_to_string (pre_error->domain), "g-usb-device-error-quark"))
    {
      fpi_ssm_next_state (self->task_ssm);
      return;
    }
  if (on)
    mafp_sensor_control (self, 0x8B, 0x01, mafp_pwr_btn_shield_on_cb, NULL, 1000);
  else
    mafp_sensor_control (self, 0x8B, 0x00, mafp_pwr_btn_shield_off_cb, NULL, 0);
}

static void
fp_enroll_int_check_cb (FpiDeviceMafpmoc    *self,
                        mafp_cmd_response_t *resp,
                        GError              *error)
{
  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  fpi_ssm_next_state (self->task_ssm);
}

static void
fp_enroll_int_detect_cb (FpiDeviceMafpmoc    *self,
                         mafp_cmd_response_t *resp,
                         GError              *error)
{
  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  fpi_ssm_next_state (self->task_ssm);
}

static void
fp_enroll_int_refresh_cb (FpiDeviceMafpmoc    *self,
                          mafp_cmd_response_t *resp,
                          GError              *error)
{
  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  self->capture_cnt = 0;
  fpi_ssm_jump_to_state (self->task_ssm, FP_ENROLL_VERIFY_GET_IMAGE);
}

static void
fp_enroll_enable_int_cb (FpiUsbTransfer *transfer,
                         FpDevice       *device,
                         gpointer        user_data,
                         GError         *error)
{
  if (error)
    {
      fpi_ssm_mark_failed (transfer->ssm, error);
      return;
    }
  fpi_ssm_next_state (transfer->ssm);
}

static void
fp_enroll_disable_int_cb (FpiUsbTransfer *transfer,
                          FpDevice       *device,
                          gpointer        user_data,
                          GError         *error)
{
  if (error)
    {
      fpi_ssm_mark_failed (transfer->ssm, error);
      return;
    }
  fpi_ssm_jump_to_state (transfer->ssm, FP_ENROLL_VERIFY_GET_IMAGE);
}

static void
fp_enroll_wait_int_cb (FpiUsbTransfer *transfer,
                       FpDevice       *device,
                       gpointer        user_data,
                       GError         *error)
{
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (device);

  if (error)
    {
      fp_dbg ("code %d", error->code);
      if (error->code == G_USB_DEVICE_ERROR_TIMED_OUT)
        fpi_ssm_jump_to_state (self->task_ssm, FP_ENROLL_VERIFY_GET_IMAGE);
      else
        fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  fp_dbg ("actual_length %zd", transfer->actual_length);
  if (transfer->actual_length == 2)
    {
      if (transfer->buffer[0] == 0x04 && transfer->buffer[1] == 0xe5)
        {
          fp_dbg ("int trigger");
          fpi_ssm_next_state (self->task_ssm);
          return;
        }
    }
  fpi_ssm_mark_failed (self->task_ssm, fpi_device_error_new (FP_DEVICE_ERROR_GENERAL));
}

static void
fp_enroll_wait_int (FpiDeviceMafpmoc *self)
{
  fp_dbg ("wait interrupt");
  FpiUsbTransfer *transfer = fpi_usb_transfer_new (FP_DEVICE (self));

  fpi_usb_transfer_fill_interrupt (transfer, MAFP_EP_INT_IN, 2);
  fpi_usb_transfer_submit (transfer,
                           30 * 1000,
                           fpi_device_get_cancellable (FP_DEVICE (self)),
                           fp_enroll_wait_int_cb,
                           NULL);
}

static int
load_fp_data (FpDevice *dev)
{
  const gchar *filename = NULL;
  gboolean fp_exist = false;
  GDir *dir = g_dir_open (FPRINT_DATA_PATH, 0, NULL);

  if (dir == NULL)
    {
      if (g_file_test (FPRINT_DATA_PATH, G_FILE_TEST_EXISTS))
        {
          fp_dbg ("open dir %s failed", FPRINT_DATA_PATH);
          return 1;
        }
      fp_dbg ("dir %s not exsit", FPRINT_DATA_PATH);
      return 0;
    }
  while (NULL != (filename = g_dir_read_name (dir)))
    {
      g_autofree gchar *user_path = g_strconcat (FPRINT_DATA_PATH, filename, NULL);
      if (g_file_test (user_path, G_FILE_TEST_IS_DIR))
        {
          FpDeviceClass *cls = FP_DEVICE_GET_CLASS (dev);
          g_autofree gchar *module_path = g_strconcat (user_path, "/", cls->id, NULL);
          fp_dbg ("found data path: %s", module_path);
          if (g_file_test (module_path, G_FILE_TEST_IS_DIR))
            {
              fp_exist = true;
              break;
            }
        }
    }
  if (!fp_exist)
    return 0;

  g_dir_close (dir);
  return 1;
}

static void
fp_empty_cb (FpiDeviceMafpmoc    *self,
             mafp_cmd_response_t *resp,
             GError              *error)
{
  fp_dbg ("result: %d", resp->result);
  fpi_ssm_next_state (self->task_ssm);
}

static int
mafp_check_empty (FpiDeviceMafpmoc *self)
{
  FpDevice *dev = FP_DEVICE (self);
  struct utsname sysinfo;

  if (uname (&sysinfo) == -1)
    {
      fp_dbg ("sysinfo err");
      fpi_ssm_next_state (self->task_ssm);
      return 0;
    }
  const gchar * str_ubuntu = "ubuntu";
  GString *version = g_string_new (sysinfo.version);
  gchar *sys_ver = g_string_ascii_down (version)->str;

  fp_dbg ("check system: %s", g_strrstr (sys_ver, str_ubuntu) ? "ubuntu" : "other");
  gboolean empty = (g_strrstr (sys_ver, str_ubuntu) && !load_fp_data (dev));

  g_string_free (version, TRUE);

  if (empty)
    {
      fp_dbg ("empty fp");
      mafp_sensor_cmd (self, MOC_CMD_EMPTY, NULL, 0, fp_empty_cb);
      return 1;
    }
  fp_dbg ("check fp end");
  fpi_ssm_next_state (self->task_ssm);
  return 0;
}

static void
fp_enroll_sm_run_state (FpiSsm *ssm, FpDevice *device)
{
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (device);
  guint8 para[PACKAGE_DATA_SIZE_MAX] = { 0 };
  FpPrint *print = NULL;
  uint16_t range = 1000;

  switch(fpi_ssm_get_cur_state (ssm))
    {
    case FP_ENROLL_PWR_BTN_SHIELD_ON:
      mafp_pwr_btn_shield_on (self, 1);
      break;

    case FP_ENROLL_CHECK_EMPTY:
      mafp_check_empty (self);
      break;

    case FP_ENROLL_TEMPLATE_TABLE:
      para[0] = 0;   //page no.
      mafp_sensor_cmd (self, MOC_CMD_GET_TEMPLATE_TABLE, (const guint8 *) &para, 1, fp_enroll_tpl_table_cb);
      break;

    case FP_ENROLL_READ_TEMPLATE:
      mafp_sensor_cmd (self, MOC_CMD_GET_MAX_ID, NULL, 0, fp_enroll_read_tpl_cb);
      break;

    case FP_ENROLL_VERIFY_GET_IMAGE:
      mafp_sensor_cmd (self, MOC_CMD_GET_IMAGE, NULL, 0, fp_enroll_get_image_cb);
      break;

    case FP_ENROLL_CHECK_INT_PARA:
      para[0] = MAFP_SLEEP_INT_CHECK;
      mafp_sensor_cmd (self, MOC_CMD_SLEEP, para, 1, fp_enroll_int_check_cb);
      break;

    case FP_ENROLL_DETECT_MODE:
      para[0] = MAFP_SLEEP_INT_WAIT;
      mafp_sensor_cmd (self, MOC_CMD_SLEEP, para, 1, fp_enroll_int_detect_cb);
      break;

    case FP_ENROLL_ENABLE_INT:
      mafp_sensor_control (self, 0x89, 1, fp_enroll_enable_int_cb, NULL, 0);
      break;

    case FP_ENROLL_WAIT_INT:
      fp_enroll_wait_int (self);
      break;

    case FP_ENROLL_DISBALE_INT:
      mafp_sensor_control (self, 0x89, 0, fp_enroll_disable_int_cb, NULL, 0);
      break;

    case FP_ENROLL_REFRESH_INT_PARA:
      fp_dbg ("refresh param");
      para[0] = MAFP_SLEEP_INT_REFRESH;
      mafp_sensor_cmd (self, MOC_CMD_SLEEP, para, 1, fp_enroll_int_refresh_cb);
      break;

    case FP_ENROLL_VERIFY_GENERATE_FEATURE:
      para[0] = self->enroll_stage + 1;   //verify buffer id start from 1
      mafp_sensor_cmd (self, MOC_CMD_GEN_FEATURE, (const guint8 *) &para, 1, fp_enroll_gen_feature_cb);
      break;

    case FP_ENROLL_VERIFY_DUPLICATE_AREA:
      mafp_sensor_cmd (self, MOC_CMD_DUPAREA_TEST, NULL, 0, fp_enroll_verify_duparea_cb);
      break;

    case FP_ENROLL_VERIFY_SEARCH:
      para[0] = 1;                     //buffer id
      para[1] = 0;                     //start id high
      para[2] = 0;                     //start id low
      para[3] = (range >> 8) & 0xff;   //range high
      para[4] = range & 0xff;          //range low
      mafp_sensor_cmd (self, MOC_CMD_SEARCH, (const guint8 *) &para, 5, fp_enroll_verify_search_cb);
      break;

    case FP_ENROLL_GET_TEMPLATE_INFO:
      para[0] = (self->search_id >> 8) & 0xff;   //fp id high
      para[1] = self->search_id & 0xff;          //fp id low
      mafp_sensor_cmd (self, MOC_CMD_GET_TEMPLATE_INFO, (const guint8 *) &para, 2, fp_enroll_get_tpl_info_cb);
      break;

    case FP_ENROLL_SAVE_TEMPLATE_INFO:
      fpi_device_get_enroll_data (device, &print);
      self->enroll_user_id = fpi_print_generate_user_id (print);
      para[0] = (self->enroll_id >> 8) & 0xff;   //fp id high
      para[1] = self->enroll_id & 0xff;          //fp id low
      memcpy (para + 2, self->enroll_user_id, strlen (self->enroll_user_id));
      fp_dbg ("user_id: %s", self->enroll_user_id);
      mafp_sensor_cmd (self, MOC_CMD_SAVE_TEMPLATE_INFO, (const guint8 *) &para, 2 + TEMPLATE_UID_SIZE, fp_enroll_save_tpl_info_cb);
      break;

    case FP_ENROLL_SAVE_TEMPLATE:
      para[0] = 1;                               //buffer id
      para[1] = (self->enroll_id >> 8) & 0xff;   //fp id high
      para[2] = self->enroll_id & 0xff;          //fp id low
      mafp_sensor_cmd (self, MOC_CMD_SAVE_TEMPLATE, (const guint8 *) &para, 3, fp_enroll_save_tpl_cb);
      break;

    case FP_ENROLL_DELETE_TEMPLATE_INFO_IF_FAILED:
      para[0] = (self->enroll_id >> 8) & 0xff;   //fp id high
      para[1] = self->enroll_id & 0xff;          //fp id low
      mafp_sensor_cmd (self, MOC_CMD_SAVE_TEMPLATE_INFO, (const guint8 *) &para, 130, fp_enroll_del_tpl_info_cb);
      break;

    case FP_ENROLL_EXIT:
      mafp_pwr_btn_shield_on (self, 0);
      break;
    }
}

static void
fp_enroll_ssm_done (FpiSsm *ssm, FpDevice *dev, GError *error)
{
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (dev);
  FpPrint *print = NULL;

  if (error)
    {
      fp_dbg ("enroll done fail");
      fpi_device_enroll_complete (dev, NULL, error);
      return;
    }
  fp_dbg ("enroll completed");

  fpi_device_get_enroll_data (dev, &print);
  fpi_device_enroll_complete (dev, g_object_ref (print), NULL);
  self->task_ssm = NULL;
}


static void
fp_verify_tpl_table_cb (FpiDeviceMafpmoc    *self,
                        mafp_cmd_response_t *resp,
                        GError              *error)
{
  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  fp_dbg ("result: %d", resp->result);

  if (resp->result == MAFP_SUCCESS)
    mafp_load_enrolled_ids (self, resp);
  fpi_device_report_finger_status (FP_DEVICE (self), FP_FINGER_STATUS_NONE | FP_FINGER_STATUS_NEEDED);
  fpi_ssm_next_state (self->task_ssm);
}

static void
fp_verify_get_image_cb (FpiDeviceMafpmoc    *self,
                        mafp_cmd_response_t *resp,
                        GError              *error)
{
  FpDevice *dev = FP_DEVICE (self);

  if (fpi_device_action_is_cancelled (dev))
    g_cancellable_set_error_if_cancelled (fpi_device_get_cancellable (dev), &error);
  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  FpVerifyState nextState = FP_VERIFY_GET_IMAGE;

  //fp_dbg("result: %d, press_state: %d", resp->result, self->press_state);
  if (self->press_state == MAFP_PRESS_WAIT_DOWN)
    {
      fp_dbg ("wait finger down state %d", resp->result);
      if (resp->result == MAFP_RE_GET_IMAGE_SUCCESS)
        {
          nextState = FP_VERIFY_GENERATE_FEATURE;
        }
      else if (resp->result == MAFP_RE_GET_IMAGE_NONE)
        {
          self->capture_cnt++;
          fp_dbg ("self->capture_cnt %d", self->capture_cnt);
          if (self->capture_cnt > MAFP_IMAGE_ERR_TRRIGER)
            nextState = FP_VERIFY_REFRESH_INT_PARA;
          else
            nextState = FP_VERIFY_DETECT_MODE;
        }
    }
  else if (self->press_state == MAFP_PRESS_WAIT_UP)
    {
      fp_dbg ("wait finger up state %d", resp->result);
      if (resp->result == MAFP_RE_GET_IMAGE_SUCCESS)
        {
          nextState = FP_VERIFY_GET_IMAGE;
        }
      else if (resp->result == MAFP_RE_GET_IMAGE_NONE)
        {
          self->press_state = MAFP_PRESS_WAIT_DOWN;
          fpi_device_report_finger_status (dev, FP_FINGER_STATUS_NONE | FP_FINGER_STATUS_NEEDED);
          nextState = FP_VERIFY_CHECK_INT_PARA;
        }
    }

  fpi_ssm_jump_to_state (self->task_ssm, nextState);
}

static void
fp_verify_gen_feature_cb (FpiDeviceMafpmoc    *self,
                          mafp_cmd_response_t *resp,
                          GError              *error)
{
  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  fp_dbg ("result: %d", resp->result);

  if (resp->result == MAFP_SUCCESS)
    {
      self->enroll_identify_index = 0;
      self->press_state = MAFP_PRESS_WAIT_UP;
      fpi_ssm_jump_to_state (self->task_ssm, FP_VERIFY_SEARCH_STEP);
    }
  else
    {
      self->press_state = MAFP_PRESS_WAIT_UP;
      fpi_ssm_jump_to_state (self->task_ssm, FP_VERIFY_GET_IMAGE);
    }
}

static void
mafp_scl_ctl_cb (FpiUsbTransfer *transfer,
                 FpDevice       *device,
                 gpointer        user_data,
                 GError         *error)
{
  if (error)
    fp_dbg ("control transfer out fail, %s", error->message);

  fpi_ssm_jump_to_state (transfer->ssm, FP_VERIFY_EXIT);
}

static void
fp_verify_get_tpl_info_cb (FpiDeviceMafpmoc    *self,
                           mafp_cmd_response_t *resp,
                           GError              *error)
{
  FpDevice *dev = FP_DEVICE (self);
  FpPrint *new_scan = NULL;
  FpPrint *matching = NULL;
  mafp_template_t tpl;

  if (fpi_device_action_is_cancelled (dev))
    g_cancellable_set_error_if_cancelled (fpi_device_get_cancellable (dev), &error);
  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  fp_dbg ("result: %d", resp->result);

  if (resp->result == MAFP_SUCCESS)
    {
      if (resp->tpl_info.uid[0] == 'F' && resp->tpl_info.uid[1] == 'P')
        {
          tpl.id = self->search_id;
          memcpy (tpl.uid, resp->tpl_info.uid, sizeof (resp->tpl_info.uid));
          new_scan = mafp_print_from_template (self, tpl);
        }
      if (new_scan != NULL)
        {
          if (fpi_device_get_current_action (dev) == FPI_DEVICE_ACTION_VERIFY)
            {
              fpi_device_get_verify_data (dev, &matching);
              if (!fp_print_equal (matching, new_scan))
                matching = NULL;
            }
          else
            {
              GPtrArray *templates = NULL;
              fpi_device_get_identify_data (dev, &templates);
              for (int i = 0; i < templates->len; i++)
                {
                  if (fp_print_equal (g_ptr_array_index (templates, i), new_scan))
                    {
                      matching = g_ptr_array_index (templates, i);
                      break;
                    }
                }
            }
        }
    }
  self->identify_match_print = matching;
  self->identify_new_print = new_scan;

  if (!matching)
    {
      mafp_sensor_control (self, 0x8C, 0x00, mafp_scl_ctl_cb, NULL, 0);
      return;
    }
  fpi_ssm_jump_to_state (self->task_ssm, FP_VERIFY_EXIT);
}

static void
fp_verify_search_step_cb (FpiDeviceMafpmoc    *self,
                          mafp_cmd_response_t *resp,
                          GError              *error)
{
  GPtrArray *prints = NULL;
  FpDevice *dev = FP_DEVICE (self);

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  fp_dbg ("result: %d", resp->result);
  if (resp->result == MAFP_SUCCESS)
    {
      fp_dbg ("identify ok, search_id: %d", self->search_id);
      fpi_ssm_jump_to_state (self->task_ssm, FP_VERIFY_GET_TEMPLATE_INFO);
    }
  else
    {
      fp_dbg ("identify fail");
      if (fpi_device_get_current_action (dev) == FPI_DEVICE_ACTION_IDENTIFY)
        {
          fpi_device_get_identify_data (dev, &prints);
          self->enroll_identify_index++;
          if (self->enroll_identify_index < prints->len)
            {
              fpi_ssm_jump_to_state (self->task_ssm, FP_VERIFY_SEARCH_STEP);
              return;
            }
        }
      self->search_id = -1;
      fpi_ssm_jump_to_state (self->task_ssm, FP_VERIFY_GET_TEMPLATE_INFO);
    }
}

static void
mafp_get_startup_result_cb (FpiUsbTransfer *transfer,
                            FpDevice       *device,
                            gpointer        user_data,
                            GError         *error)
{
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (device);

  if (error)
    {
      fpi_ssm_next_state (transfer->ssm);
      return;
    }
  if (transfer->actual_length >= 5)
    {
      fp_dbg ("0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x", transfer->buffer[0], transfer->buffer[1],
              transfer->buffer[2], transfer->buffer[3], transfer->buffer[4]);
      if (transfer->buffer[0])
        {
          self->search_id = transfer->buffer[2] * 256 + transfer->buffer[1];
          usleep (1000 * 1000);
          fpi_ssm_jump_to_state (transfer->ssm, FP_VERIFY_GET_TEMPLATE_INFO);
          return;
        }
    }
  fpi_ssm_next_state (transfer->ssm);
}

static void
fp_verify_int_check_cb (FpiDeviceMafpmoc    *self,
                        mafp_cmd_response_t *resp,
                        GError              *error)
{
  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  fpi_ssm_next_state (self->task_ssm);
}

static void
fp_verify_int_detect_cb (FpiDeviceMafpmoc    *self,
                         mafp_cmd_response_t *resp,
                         GError              *error)
{
  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  fpi_ssm_next_state (self->task_ssm);
}

static void
fp_verify_int_refresh_cb (FpiDeviceMafpmoc    *self,
                          mafp_cmd_response_t *resp,
                          GError              *error)
{
  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  self->capture_cnt = 0;
  fpi_ssm_jump_to_state (self->task_ssm, FP_VERIFY_GET_IMAGE);
}

static void
fp_verify_enable_int_cb (FpiUsbTransfer *transfer,
                         FpDevice       *device,
                         gpointer        user_data,
                         GError         *error)
{
  if (error)
    {
      fpi_ssm_mark_failed (transfer->ssm, error);
      return;
    }
  fpi_ssm_next_state (transfer->ssm);
}

static void
fp_verify_disable_int_cb (FpiUsbTransfer *transfer,
                          FpDevice       *device,
                          gpointer        user_data,
                          GError         *error)
{
  if (error)
    {
      fpi_ssm_mark_failed (transfer->ssm, error);
      return;
    }
  fpi_ssm_jump_to_state (transfer->ssm, FP_VERIFY_GET_IMAGE);
}

static void
fp_verify_wait_int_cb (FpiUsbTransfer *transfer,
                       FpDevice       *device,
                       gpointer        user_data,
                       GError         *error)
{
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (device);

  if (error)
    {
      fp_dbg ("code %d", error->code);
      if (error->code == G_USB_DEVICE_ERROR_TIMED_OUT)
        fpi_ssm_jump_to_state (self->task_ssm, FP_VERIFY_GET_IMAGE);
      else
        fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  fp_dbg ("actual_length %zd", transfer->actual_length);
  if (transfer->actual_length == 2)
    {
      if (transfer->buffer[0] == 0x04 && transfer->buffer[1] == 0xe5)
        {
          fp_dbg ("int trigger");
          fpi_ssm_next_state (self->task_ssm);
          return;
        }
    }
  fpi_ssm_mark_failed (self->task_ssm, fpi_device_error_new (FP_DEVICE_ERROR_GENERAL));
}

static void
fp_verify_wait_int (FpiDeviceMafpmoc *self)
{
  fp_dbg ("wait interrupt");
  FpiUsbTransfer *transfer = fpi_usb_transfer_new (FP_DEVICE (self));

  fpi_usb_transfer_fill_interrupt (transfer, MAFP_EP_INT_IN, 2);
  fpi_usb_transfer_submit (transfer,
                           30 * 60 * 1000,
                           fpi_device_get_cancellable (FP_DEVICE (self)),
                           fp_verify_wait_int_cb,
                           NULL);
}

static void
fp_verify_sm_run_state (FpiSsm *ssm, FpDevice *device)
{
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (device);
  guint8 para[PACKAGE_DATA_SIZE_MAX] = { 0 };
  GPtrArray *prints = NULL;
  FpPrint *print = NULL;

  switch(fpi_ssm_get_cur_state (ssm))
    {
    case FP_VERIFY_PWR_BTN_SHIELD_ON:
      mafp_pwr_btn_shield_on (self, 1);
      break;

    case FP_VERIFY_TEMPLATE_TABLE:
      para[0] = 0;   //page no.
      mafp_sensor_cmd (self, MOC_CMD_GET_TEMPLATE_TABLE, (const guint8 *) &para, 1, fp_verify_tpl_table_cb);
      break;

    case FP_VERIFY_GET_STARTUP_RESULT:
      mafp_sensor_control (self, 0x8D, 0x00, mafp_get_startup_result_cb, NULL, 0);
      break;

    case FP_VERIFY_GET_IMAGE:
      mafp_sensor_cmd (self, MOC_CMD_GET_IMAGE, NULL, 0, fp_verify_get_image_cb);
      break;

    case FP_VERIFY_CHECK_INT_PARA:
      para[0] = MAFP_SLEEP_INT_CHECK;
      mafp_sensor_cmd (self, MOC_CMD_SLEEP, para, 1, fp_verify_int_check_cb);
      break;

    case FP_VERIFY_DETECT_MODE:
      para[0] = MAFP_SLEEP_INT_WAIT;
      mafp_sensor_cmd (self, MOC_CMD_SLEEP, para, 1, fp_verify_int_detect_cb);
      break;

    case FP_VERIFY_ENABLE_INT:
      mafp_sensor_control (self, 0x89, 1, fp_verify_enable_int_cb, NULL, 0);
      break;

    case FP_VERIFY_WAIT_INT:
      fp_verify_wait_int (self);
      break;

    case FP_VERIFY_DISBALE_INT:
      mafp_sensor_control (self, 0x89, 0, fp_verify_disable_int_cb, NULL, 0);
      break;

    case FP_VERIFY_REFRESH_INT_PARA:
      fp_dbg ("refresh param");
      para[0] = MAFP_SLEEP_INT_REFRESH;
      mafp_sensor_cmd (self, MOC_CMD_SLEEP, para, 1, fp_verify_int_refresh_cb);
      break;

    case FP_VERIFY_GENERATE_FEATURE:
      para[0] = 1;   //buffer id
      mafp_sensor_cmd (self, MOC_CMD_GEN_FEATURE, (const guint8 *) &para, 1, fp_verify_gen_feature_cb);
      break;

    case FP_VERIFY_SEARCH_STEP:
      if (fpi_device_get_current_action (device) == FPI_DEVICE_ACTION_VERIFY)
        {
          fpi_device_get_verify_data (device, &print);
          if (!print)
            {
              self->search_id = -1;
              fpi_ssm_jump_to_state (self->task_ssm, FP_VERIFY_GET_TEMPLATE_INFO);
              break;
            }
        }
      else
        {
          fpi_device_get_identify_data (device, &prints);
          if (!prints || prints->len == 0)
            {
              self->search_id = -1;
              fpi_ssm_jump_to_state (self->task_ssm, FP_VERIFY_GET_TEMPLATE_INFO);
              break;
            }
          print = g_ptr_array_index (prints, self->enroll_identify_index);
        }
      mafp_template_t tpl = mafp_template_from_print (print);
      self->search_id = tpl.id;
      para[0] = (tpl.id >> 8) & 0xff;
      para[1] = tpl.id & 0xff;
      mafp_sensor_cmd (self, MOC_CMD_MATCH_WITHFID, (const guint8 *) &para, 2, fp_verify_search_step_cb);
      break;

    case FP_VERIFY_GET_TEMPLATE_INFO:
      if (self->search_id == -1)
        {
          mafp_cmd_response_t resp;
          resp.result = 1;
          fp_verify_get_tpl_info_cb (self, &resp, NULL);
        }
      else
        {
          para[0] = (self->search_id >> 8) & 0xff;   //fp id high
          para[1] = self->search_id & 0xff;          //fp id low
          mafp_sensor_cmd (self, MOC_CMD_GET_TEMPLATE_INFO, (const guint8 *) &para, 2, fp_verify_get_tpl_info_cb);
        }
      break;

    case FP_VERIFY_EXIT:
      mafp_pwr_btn_shield_on (self, 0);
      break;
    }
}

static void
fp_verify_ssm_done (FpiSsm *ssm, FpDevice *dev, GError *error)
{
  fp_dbg ("verify completed");
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (dev);

  if (error && error->domain == FP_DEVICE_RETRY)
    {
      if (fpi_device_get_current_action (dev) == FPI_DEVICE_ACTION_VERIFY)
        fpi_device_verify_report (dev, FPI_MATCH_ERROR, NULL, g_steal_pointer (&error));
      else
        fpi_device_identify_report (dev, NULL, NULL, g_steal_pointer (&error));
    }

  if (fpi_device_get_current_action (dev) == FPI_DEVICE_ACTION_VERIFY)
    {
      fpi_device_verify_report (dev, self->identify_match_print ? FPI_MATCH_SUCCESS : FPI_MATCH_FAIL,
                                self->identify_new_print, NULL);
      fpi_device_verify_complete (dev, error);
    }
  else
    {
      fpi_device_identify_report (dev, self->identify_match_print,
                                  self->enroll_dupl_del_state ? self->identify_new_print : NULL, NULL);
      fpi_device_identify_complete (dev, error);
    }
  self->task_ssm = NULL;
}

static void
fp_list_tpl_table_cb (FpiDeviceMafpmoc    *self,
                      mafp_cmd_response_t *resp,
                      GError              *error)
{
  FpDevice *dev = FP_DEVICE (self);

  if (error)
    {
      fpi_device_list_complete (dev, NULL, error);
      return;
    }
  fp_dbg ("result: %d", resp->result);

  if (resp->result == MAFP_SUCCESS)
    {
      mafp_load_enrolled_ids (self, resp);
      if (self->templates->list)
        g_clear_pointer (&self->templates->list, g_object_unref);
      self->templates->list = g_ptr_array_new_with_free_func (g_object_unref);
      if (self->templates->total_num == 0)
        {
          fpi_ssm_jump_to_state (self->task_ssm, FP_LIST_STATES);
          return;
        }
      fpi_ssm_next_state (self->task_ssm);
    }
  else
    {
      mafp_mark_failed (dev, self->task_ssm, FP_DEVICE_ERROR_GENERAL,
                        "Failed to get fingerprints index, result: 0x%x", resp->result);
    }
}

static void
fp_list_get_tpl_info_cb (FpiDeviceMafpmoc    *self,
                         mafp_cmd_response_t *resp,
                         GError              *error)
{
  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  fp_dbg ("result: %d", resp->result);

  if (resp->result == MAFP_SUCCESS)
    {
      memcpy (self->templates->total_list[self->templates->index].uid,
              resp->tpl_info.uid, sizeof (resp->tpl_info.uid));

      FpPrint *print = mafp_print_from_template (self,
                                                 self->templates->total_list[self->templates->index]);

      g_ptr_array_add (self->templates->list, g_object_ref_sink (print));
    }
  if (++self->templates->index < self->templates->total_num)
    {
      fpi_ssm_jump_to_state (self->task_ssm, FP_LIST_GET_TEMPLATE_INFO);
      return;
    }
  fpi_ssm_next_state (self->task_ssm);
}

static void
fp_list_run_state (FpiSsm *ssm, FpDevice *device)
{
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (device);
  guint8 para[PACKAGE_DATA_SIZE_MAX] = { 0 };

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case FP_LIST_TEMPLATE_TABLE:
      para[0] = 0;   //page no.
      mafp_sensor_cmd (self, MOC_CMD_GET_TEMPLATE_TABLE, (const guint8 *) &para, 1, fp_list_tpl_table_cb);
      break;

    case FP_LIST_GET_TEMPLATE_INFO:
      para[0] = (self->templates->total_list[self->templates->index].id >> 8) & 0xff;   //fp id high
      para[1] = self->templates->total_list[self->templates->index].id & 0xff;          //fp id low
      mafp_sensor_cmd (self, MOC_CMD_GET_TEMPLATE_INFO, (const guint8 *) &para, 2, fp_list_get_tpl_info_cb);
      break;
    }
}

static void
fp_list_ssm_done (FpiSsm *ssm, FpDevice *dev, GError *error)
{
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (dev);

  if (error)
    {
      fp_dbg ("list tpl fail");
      fpi_device_list_complete (dev, NULL, error);
      return;
    }
  fpi_device_list_complete (FP_DEVICE (self), g_steal_pointer (&self->templates->list), NULL);
  self->task_ssm = NULL;
}

static void
fp_delete_tpl_table_cb (FpiDeviceMafpmoc    *self,
                        mafp_cmd_response_t *resp,
                        GError              *error)
{
  FpDevice *dev = FP_DEVICE (self);
  FpPrint *print = NULL;
  gboolean id_exist = FALSE;

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  fp_dbg ("result: %d", resp->result);

  if (resp->result == MAFP_SUCCESS)
    {
      mafp_load_enrolled_ids (self, resp);
      fpi_device_get_delete_data (dev, &print);
      mafp_template_t tpl = mafp_template_from_print (print);
      for (int i = 0; i < self->templates->total_num; i++)
        {
          if (self->templates->total_list[i].id == tpl.id)
            {
              id_exist = true;
              break;
            }
        }
    }
  if (!id_exist)
    {
      fpi_ssm_jump_to_state (self->task_ssm, FP_DELETE_CLEAR_TEMPLATE_INFO);
      return;
    }
  fpi_ssm_next_state (self->task_ssm);
}

static void
fp_delete_get_tpl_info_cb (FpiDeviceMafpmoc    *self,
                           mafp_cmd_response_t *resp,
                           GError              *error)
{
  FpDevice *dev = FP_DEVICE (self);
  FpPrint *print = NULL;

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  fp_dbg ("result: %d", resp->result);

  if (resp->result == MAFP_SUCCESS)
    {
      fpi_device_get_delete_data (dev, &print);
      mafp_template_t tpl = mafp_template_from_print (print);
      fp_dbg ("target: %s/%s", tpl.uid, tpl.sn);
      fp_dbg ("find: %s/%s", resp->tpl_info.uid, self->serial_number);
      if (g_strcmp0 (self->serial_number, tpl.sn) != 0)
        {
          mafp_mark_failed (dev, self->task_ssm, FP_DEVICE_ERROR_GENERAL,
                            "Failed to match device serial number");
          return;
        }
      if (g_strcmp0 (resp->tpl_info.uid, tpl.uid) != 0)
        {
          mafp_mark_failed (dev, self->task_ssm, FP_DEVICE_ERROR_GENERAL,
                            "Failed to match template uid");
          return;
        }
      fpi_ssm_next_state (self->task_ssm);
    }
  else
    {
      mafp_mark_failed (dev, self->task_ssm, FP_DEVICE_ERROR_GENERAL,
                        "Failed to get template info, result: 0x%x", resp->result);
    }
}

static void
fp_delete_clear_tpl_info_cb (FpiDeviceMafpmoc    *self,
                             mafp_cmd_response_t *resp,
                             GError              *error)
{
  FpDevice *dev = FP_DEVICE (self);

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  fp_dbg ("result: %d", resp->result);

  if (resp->result != MAFP_SUCCESS)
    {
      mafp_mark_failed (dev, self->task_ssm, FP_DEVICE_ERROR_GENERAL,
                        "Failed to delete template info, result: 0x%x", resp->result);
      return;
    }
  fpi_ssm_next_state (self->task_ssm);
}

static void
fp_delete_tpl_cb (FpiDeviceMafpmoc    *self,
                  mafp_cmd_response_t *resp,
                  GError              *error)
{
  FpDevice *dev = FP_DEVICE (self);

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  fp_dbg ("result: %d", resp->result);

  if (resp->result != MAFP_SUCCESS)
    {
      mafp_mark_failed (dev, self->task_ssm, FP_DEVICE_ERROR_GENERAL,
                        "Failed to delete template, result: 0x%x", resp->result);
      return;
    }
  fpi_ssm_next_state (self->task_ssm);
}

static void
fp_delete_run_state (FpiSsm *ssm, FpDevice *device)
{
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (device);
  guint8 para[PACKAGE_DATA_SIZE_MAX] = { 0 };
  FpPrint *print = NULL;

  fpi_device_get_delete_data (device, &print);
  mafp_template_t delete_tpl = mafp_template_from_print (print);

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case FP_DELETE_TEMPLATE_TABLE:
      para[0] = 0;                             //page no.
      mafp_sensor_cmd (self, MOC_CMD_GET_TEMPLATE_TABLE, (const guint8 *) &para, 1, fp_delete_tpl_table_cb);
      break;

    case FP_DELETE_GET_TEMPLATE_INFO:
      para[0] = (delete_tpl.id >> 8) & 0xff;   //fp id high
      para[1] = delete_tpl.id & 0xff;          //fp id low
      mafp_sensor_cmd (self, MOC_CMD_GET_TEMPLATE_INFO, (const guint8 *) &para, 2, fp_delete_get_tpl_info_cb);
      break;

    case FP_DELETE_CLEAR_TEMPLATE_INFO:
      para[0] = (delete_tpl.id >> 8) & 0xff;   //fp id high
      para[1] = delete_tpl.id & 0xff;          //fp id low
      mafp_sensor_cmd (self, MOC_CMD_SAVE_TEMPLATE_INFO, (const guint8 *) &para, 130, fp_delete_clear_tpl_info_cb);
      break;

    case FP_DELETE_TEMPLATE:
      para[0] = (delete_tpl.id >> 8) & 0xff;   //tpl id high
      para[1] = delete_tpl.id & 0xff;          //tpl id low
      para[2] = 0;                             //range high
      para[3] = 1;                             //range low
      mafp_sensor_cmd (self, MOC_CMD_DELETE_TEMPLATE, (const guint8 *) &para, 4, fp_delete_tpl_cb);
      break;
    }
}

static void
fp_delete_ssm_done (FpiSsm *ssm, FpDevice *dev, GError *error)
{
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (dev);

  if (error)
    {
      fp_dbg ("delete tpl fail");
      fpi_device_delete_complete (dev, error);
      return;
    }
  fp_dbg ("delete tpl success");
  fpi_device_delete_complete (dev, NULL);
  self->task_ssm = NULL;
}

static void
fp_delete_all_cb (FpiDeviceMafpmoc    *self,
                  mafp_cmd_response_t *resp,
                  GError              *error)
{
  FpDevice *dev = FP_DEVICE (self);

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }
  fp_dbg ("result: %d", resp->result);

  if (resp->result != MAFP_SUCCESS)
    {
      mafp_mark_failed (dev, self->task_ssm, FP_DEVICE_ERROR_GENERAL,
                        "Failed to empty templates, result: 0x%x", resp->result);
      return;
    }
  fpi_ssm_next_state (self->task_ssm);
}

static void
fp_delete_all_run_state (FpiSsm *ssm, FpDevice *device)
{
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (device);

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case FP_EMPTY_TEMPLATE:
      mafp_sensor_cmd (self, MOC_CMD_EMPTY, NULL, 0, fp_delete_all_cb);
      break;
    }
}

static void
fp_delete_all_ssm_done (FpiSsm *ssm, FpDevice *dev, GError *error)
{
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (dev);

  if (error)
    {
      fp_dbg ("delete all fail");
      fpi_device_clear_storage_complete (dev, error);
      return;
    }
  fp_dbg ("delete all success");
  fpi_device_clear_storage_complete (dev, NULL);
  self->task_ssm = NULL;
}

static void
mafp_probe (FpDevice *device)
{
  fp_dbg ("mafp_probe");
  GUsbDevice *usb_dev;
  GError *error = NULL;
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (device);
  g_autofree gchar *serial = NULL;

  g_autoptr(GUsbInterface) interface = NULL;
  guint64 driver_data;

  usb_dev = fpi_device_get_usb_device (device);
  if (!g_usb_device_open (usb_dev, &error))
    {
      fpi_device_probe_complete (device, NULL, NULL, error);
      return;
    }

  driver_data = fpi_device_get_driver_data (device);
  fp_dbg ("driver_data 0x%zx", driver_data);
  fp_dbg ("g_usb_device_reset");
  if (!g_usb_device_reset (usb_dev, &error))
    goto err_close;

  fp_dbg ("g_usb_device_get_interface");
  interface = g_usb_device_get_interface (usb_dev, MAFP_INTERFACE_CLASS,
                                          MAFP_INTERFACE_SUB_CLASS, MAFP_INTERFACE_PROTOCOL, &error);
  if (!interface)
    {
      fp_dbg ("interface null");
      goto err_close;
    }
  self->interface_num = g_usb_interface_get_number (interface);
  fp_dbg ("interface number %d", self->interface_num);

  /* Claim usb interface */
  if (!g_usb_device_claim_interface (usb_dev, self->interface_num, 0, &error))
    goto err_close;

  if (g_strcmp0 (g_getenv ("FP_DEVICE_EMULATION"), "1") == 0)
    {
      serial = g_strdup ("emulated-device");
    }
  else
    {
      serial = g_usb_device_get_string_descriptor (usb_dev,
                                                   g_usb_device_get_serial_number_index (usb_dev),
                                                   &error);
      if (error)
        {
          g_usb_device_release_interface (fpi_device_get_usb_device (device), 0, 0, NULL);
          goto err_close;
        }
    }

  self->serial_number = g_new0 (char, DEVICE_SN_SIZE);
  memcpy (self->serial_number, serial, strlen (serial));
  fp_dbg ("serial: %s", serial);

  fpi_device_set_nr_enroll_stages (device, DEFAULT_ENROLL_SAMPLES);

  g_usb_device_close (usb_dev, NULL);
  fpi_device_probe_complete (device, serial, NULL, NULL);
  return;

err_close:
  g_usb_device_close (usb_dev, NULL);
  fpi_device_probe_complete (device, NULL, NULL, error);
}

static void
mafp_init (FpDevice *device)
{
  fp_dbg ("mafp_init");
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (device);
  GError *error = NULL;
  guint64 driver_data;

  driver_data = fpi_device_get_driver_data (device);
  fp_dbg ("driver_data 0x%zx", driver_data);
  fp_dbg ("g_usb_device_reset");
  if (!g_usb_device_reset (fpi_device_get_usb_device (device), &error))
    {
      fp_dbg ("g_usb_device_reset err: %s", error->message);
      fpi_device_open_complete (FP_DEVICE (self), error);
      return;
    }

  /* Claim usb interface */
  fp_dbg ("g_usb_device_claim_interface");
  if (!g_usb_device_claim_interface (fpi_device_get_usb_device (device), 0, 0, &error))
    {
      fpi_device_open_complete (FP_DEVICE (self), error);
      return;
    }

  if (fp_device_has_feature (device, FP_DEVICE_FEATURE_STORAGE))
    fp_dbg ("device has storage");
  else
    fp_dbg ("device no storage");

  self->templates = g_new0 (mafp_templates_t, 1);
  self->task_ssm = fpi_ssm_new (device, fp_init_run_state, FP_INIT_STATES);
  if (!PRINT_SSM_DEBUG)
    fpi_ssm_silence_debug (self->task_ssm);
  fpi_ssm_start (self->task_ssm, fp_init_ssm_done);
}

static void
mafp_enroll (FpDevice *device)
{
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (device);

  self->enroll_stage = 0;
  self->finger_status = 0;
  self->press_state = MAFP_PRESS_WAIT_UP;
  self->capture_cnt = 0;
  self->enroll_identify_state = MAFP_ENROLL_IDENTIFY_ENABLED;
  self->enroll_dupl_del_state = MAFP_ENROLL_DUPLICATE_DELETE_ENABLED;
  self->enroll_dupl_area_state = MAFP_ENROLL_DUPLICATE_AREA_DENY;
  memset (self->templates, 0, sizeof (mafp_templates_t));

  self->task_ssm = fpi_ssm_new_full (device, fp_enroll_sm_run_state,
                                     FP_ENROLL_STATES,
                                     FP_ENROLL_EXIT,
                                     "enroll");

  if (!PRINT_SSM_DEBUG)
    fpi_ssm_silence_debug (self->task_ssm);
  fpi_ssm_start (self->task_ssm, fp_enroll_ssm_done);
}

static void
mafp_verify_identify (FpDevice *device)
{
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (device);

  memset (self->templates, 0, sizeof (mafp_templates_t));

  self->press_state = MAFP_PRESS_WAIT_UP;
  self->capture_cnt = 0;
  self->identify_match_print = NULL;
  self->identify_new_print = NULL;
  self->task_ssm = fpi_ssm_new_full (device, fp_verify_sm_run_state,
                                     FP_VERIFY_STATES,
                                     FP_VERIFY_EXIT,
                                     "verify");

  if (!PRINT_SSM_DEBUG)
    fpi_ssm_silence_debug (self->task_ssm);
  fpi_ssm_start (self->task_ssm, fp_verify_ssm_done);
}

static void
mafp_template_list (FpDevice *device)
{
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (device);

  memset (self->templates, 0, sizeof (mafp_templates_t));
  self->task_ssm = fpi_ssm_new (device, fp_list_run_state, FP_LIST_STATES);
  if (!PRINT_SSM_DEBUG)
    fpi_ssm_silence_debug (self->task_ssm);
  fpi_ssm_start (self->task_ssm, fp_list_ssm_done);
}

static void
mafp_template_delete (FpDevice *device)
{
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (device);

  self->task_ssm = fpi_ssm_new (device, fp_delete_run_state, FP_DELETE_STATES);
  if (!PRINT_SSM_DEBUG)
    fpi_ssm_silence_debug (self->task_ssm);
  fpi_ssm_start (self->task_ssm, fp_delete_ssm_done);
}

static void
mafp_template_delete_all (FpDevice *device)
{
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (device);

  self->task_ssm = fpi_ssm_new (device, fp_delete_all_run_state, FP_EMPTY_STATES);
  if (!PRINT_SSM_DEBUG)
    fpi_ssm_silence_debug (self->task_ssm);
  fpi_ssm_start (self->task_ssm, fp_delete_all_ssm_done);
}

static void
mafp_cancel (FpDevice *device)
{
  fp_dbg ("mafp_cancel");
}

static void
mafp_release_interface (FpiDeviceMafpmoc *self,
                        GError           *error)
{
  g_free (self->serial_number);
  g_free (self->enroll_user_id);

  g_autoptr(GError) release_error = NULL;
  /* Release usb interface */
  g_usb_device_release_interface (fpi_device_get_usb_device (FP_DEVICE (self)),
                                  0, 0, &release_error);
  /* Retain passed error if set, otherwise propagate error from release. */
  if (error == NULL)
    error = g_steal_pointer (&release_error);
  /* Notify close complete */
  fpi_device_close_complete (FP_DEVICE (self), error);
}

static void
mafp_exit (FpDevice *device)
{
  fp_dbg ("mafp_exit");
  FpiDeviceMafpmoc *self = FPI_DEVICE_MAFPMOC (device);

  mafp_release_interface (self, NULL);
}

static void
fpi_device_mafpmoc_init (FpiDeviceMafpmoc *self)
{
  fp_dbg ("fpi_device_mafpmoc_init");
}

static const FpIdEntry id_table[] = {
  { .vid = 0x3274,  .pid = 0x8012,  },
  { .vid = 0,  .pid = 0,  .driver_data = 0 },   /* terminating entry */
};

static void
fpi_device_mafpmoc_class_init (FpiDeviceMafpmocClass *klass)
{
  FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);

  dev_class->id = "mafpmoc";
  dev_class->full_name = "MAFP MOC Fingerprint Sensor";
  dev_class->type = FP_DEVICE_TYPE_USB;
  dev_class->scan_type = FP_SCAN_TYPE_PRESS;
  dev_class->id_table = id_table;
  dev_class->nr_enroll_stages = DEFAULT_ENROLL_SAMPLES;
  dev_class->temp_hot_seconds = -1;

  dev_class->open   = mafp_init;
  dev_class->close  = mafp_exit;
  dev_class->probe  = mafp_probe;
  dev_class->enroll = mafp_enroll;
  dev_class->cancel = mafp_cancel;
  dev_class->verify   = mafp_verify_identify;
  dev_class->identify = mafp_verify_identify;
  dev_class->delete = mafp_template_delete;
  dev_class->clear_storage = mafp_template_delete_all;
  dev_class->list = mafp_template_list;

  fpi_device_class_auto_initialize_features (dev_class);
}