/*
 * PixelAuth PrimeX driver for libfprint
 * by Chester Lee <chester@lichester.com>
 * 
 * PrimeX is match on chip fingerprint module, 144x64 px
 * 10 fingerprints slot inside
 * 
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; version
 * 2.1 of the License.
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
#include "drivers_api.h"
#include "pa_primex.h"

struct _FpiDevicePa_Primex
{
    FpDevice parent;
    gint enroll_stage;
    GPtrArray *list_result;
};

G_DECLARE_FINAL_TYPE(FpiDevicePa_Primex, fpi_device_pa_primex, FPI, DEVICE_PA_PRIME, FpDevice);
G_DEFINE_TYPE(FpiDevicePa_Primex, fpi_device_pa_primex, FP_TYPE_DEVICE);

static void
fpi_device_pa_primex_init(FpiDevicePa_Primex *self)
{
    g_print("hello PA: fpi_device_pa_primex_init\n");
}

static char *
get_pa_data_descriptor (FpPrint *print, FpDevice *dev, FpFinger finger)
{
  const char *driver;
  const char *dev_id;

  if (print)
    {
      driver = fp_print_get_driver (print);
      dev_id = fp_print_get_device_id (print);
    }
  else
    {
      driver = fp_device_get_driver (dev);
      dev_id = fp_device_get_device_id (dev);
    }

  return g_strdup_printf ("%s/%s/%x",
                          driver,
                          dev_id,
                          finger);
}

static GVariantDict *
_load_data (void)
{
  GVariantDict *res;
  GVariant *var;
  gchar *contents = NULL;
  gsize length = 0;

  if (!g_file_get_contents (STORAGE_FILE, &contents, &length, NULL))
    {
      g_warning ("Error loading storage, assuming it is empty");
      return g_variant_dict_new (NULL);
    }

  var = g_variant_new_from_data (G_VARIANT_TYPE_VARDICT,
                                 contents,
                                 length,
                                 FALSE,
                                 g_free,
                                 contents);

  res = g_variant_dict_new (var);
  g_variant_unref (var);
  return res;
}

static int
_save_data (GVariant *data)
{
  const gchar *contents = NULL;
  gsize length;

  length = g_variant_get_size (data);
  contents = (gchar *) g_variant_get_data (data);

  if (!g_file_set_contents (STORAGE_FILE, contents, length, NULL))
    {
      g_warning ("Error saving storage,!");
      return -1;
    }

  g_variant_ref_sink (data);
  g_variant_unref (data);

  return 0;
}

FpPrint *
pa_data_load (FpDevice *dev, FpFinger finger)
{
  g_autofree gchar *descr = get_pa_data_descriptor (NULL, dev, finger);

  g_autoptr(GVariant) val = NULL;
  g_autoptr(GVariantDict) dict = NULL;
  const guchar *stored_data = NULL;
  gsize stored_len;

  dict = _load_data ();
  val = g_variant_dict_lookup_value (dict, descr, G_VARIANT_TYPE ("ay"));

  if (val)
    {
      FpPrint *print;
      g_autoptr(GError) error = NULL;

      stored_data = (const guchar *) g_variant_get_fixed_array (val, &stored_len, 1);
      print = fp_print_deserialize (stored_data, stored_len, &error);

      if (error)
        g_warning ("Error deserializing data: %s", error->message);

      return print;
    }

  return NULL;
}

int
pa_data_save (FpPrint *print, FpFinger finger)
{
  g_autofree gchar *descr = get_pa_data_descriptor (print, NULL, finger);

  g_autoptr(GError) error = NULL;
  g_autoptr(GVariantDict) dict = NULL;
  g_autofree guchar *data = NULL;
  GVariant *val;
  gsize size;
  int res;

  dict = _load_data ();

  fp_print_serialize (print, &data, &size, &error);
  if (error)
    {
      g_warning ("Error serializing data: %s", error->message);
      return -1;
    }
  val = g_variant_new_fixed_array (G_VARIANT_TYPE ("y"), data, size, 1);
  g_variant_dict_insert_value (dict, descr, val);

  res = _save_data (g_variant_dict_end (dict));

  return res;
}

static gboolean
parse_print_data(GVariant *data,
                 guint8 *finger,
                 const guint8 **user_id,
                 gsize *user_id_len)
{
    g_autoptr(GVariant) user_id_var = NULL;

    g_return_val_if_fail(data != NULL, FALSE);
    g_return_val_if_fail(finger != NULL, FALSE);
    g_return_val_if_fail(user_id != NULL, FALSE);
    g_return_val_if_fail(user_id_len != NULL, FALSE);

    *user_id = NULL;
    *user_id_len = 0;

    if (!g_variant_check_format_string(data, "(y@ay)", FALSE))
        return FALSE;

    g_variant_get(data,
                  "(y@ay)",
                  finger,
                  &user_id_var);

    *user_id = g_variant_get_fixed_array(user_id_var, user_id_len, 1);

    if (*user_id_len == 0 || *user_id_len > 100)
        return FALSE;

    if (*user_id_len <= 0 || *user_id[0] == ' ')
        return FALSE;

    return TRUE;
}

static void
alloc_send_cmd_transfer(FpDevice *dev,
                        FpiSsm *ssm,
                        unsigned char ins,
                        unsigned char p1,
                        unsigned char p2,
                        const unsigned char *data,
                        guint16 len)
{
    //g_print("hello PA: alloc_send_cmd_transfer len=%d \n", len);
    FpiUsbTransfer *transfer = fpi_usb_transfer_new(dev);
    int real_len = 0;
    if (ins == PA_CMD_FPSTATE)
    {
        len = 0;
    }
    real_len = PA_HEADER_LEN + PA_LEN_LEN + PA_INNER_HEADER_LEN + len;

    fpi_usb_transfer_fill_bulk(transfer, PA_OUT, real_len);
    memcpy(transfer->buffer, pa_header, PA_HEADER_LEN);
    transfer->buffer[5] = (len + PA_INNER_HEADER_LEN) >> 8;
    transfer->buffer[6] = (len + PA_INNER_HEADER_LEN) & 0xff;
    transfer->buffer[7] = PA_APDU_CLA;
    transfer->buffer[8] = ins;
    transfer->buffer[9] = p1;
    transfer->buffer[10] = p2;
    transfer->buffer[11] = 0;
    transfer->buffer[12] = len >> 8;
    transfer->buffer[13] = len & 0xff;
    if (ins == PA_CMD_FPSTATE)
        transfer->buffer[13] = 1;
    if (len != 0 && data != NULL)
        memcpy(transfer->buffer + PA_HEADER_LEN + PA_LEN_LEN + PA_INNER_HEADER_LEN, data, len);
    if (ssm != NULL)
        transfer->ssm = ssm;
    if (ins == PA_CMD_FPSTATE)
        fpi_usb_transfer_submit(transfer, TIMEOUT, NULL, enroll_iterate_cmd_cb, NULL);
    else
        fpi_usb_transfer_submit(transfer, TIMEOUT, NULL, fpi_ssm_usb_transfer_cb, NULL);
}

static void
alloc_get_cmd_transfer(FpDevice *device,
                       handle_get_fn callback,
                       void *user_data)
{
    //g_print("hello PA: alloc_get_cmd_transfer\n");
    FpiUsbTransfer *transfer = fpi_usb_transfer_new(device);
    struct prime_data *udata = g_new0(struct prime_data, 1);

    udata->buflen = 0;
    udata->buffer = NULL;
    udata->callback = callback;
    udata->user_data = user_data;

    udata->buffer = g_realloc(udata->buffer, PA_MAX_GET_LEN);
    udata->buflen = PA_MAX_GET_LEN;
    fpi_usb_transfer_fill_bulk_full(transfer, PA_IN, udata->buffer, udata->buflen, NULL);
    fpi_usb_transfer_submit(transfer, TIMEOUT, NULL, read_cb, udata);
}

static void
read_cb(FpiUsbTransfer *transfer, FpDevice *device,
        gpointer user_data, GError *error)
{
    struct prime_data *udata = user_data;
    //g_print("hello PA: read_cb len = %ld\n", transfer->actual_length);
    if (transfer->actual_length < PA_HEADER_LEN + PA_LEN_LEN + PA_SW_LEN)
    {
        g_print("hello PA: error %s\n", error->message);
        return;
    }

    handle_response(device, transfer, udata);

    return;
}

static void
handle_response(FpDevice *device, FpiUsbTransfer *transfer, struct prime_data *udata)
{
    guint8 *buf = udata->buffer;
    guint16 len = transfer->actual_length;
    udata->callback(device, buf, len, udata->user_data, NULL);
}

static int get_sw(unsigned char *data, size_t data_len)
{
    int len = data[6];
    //g_print("PA: SW %x %x\n", data[7 + len - 2], data[8 + len - 2]);
    if (data[7 + len - 2] == 0x90 && data[8 + len - 2] == 0)
        return PA_OK;
    if (data[7 + len - 2] == 0x6f && data[8 + len - 2] == 3)
        return PA_FPM_CONDITION;
    if (data[7 + len - 2] == 0x6f && data[8 + len - 2] == 5)
        return PA_FPM_REFDATA;

    g_print("PA: SW error %x %x\n", data[7 + len - 2], data[8 + len - 2]);
    return PA_ERROR;
}

static int get_data(unsigned char *data, size_t data_len, unsigned char *buf)
{
    int len = data[6];
    if (len == PA_SW_LEN)
        return PA_OK;
    if (len > PA_SW_LEN)
        memcpy(buf, data + 7, len - PA_SW_LEN);
    return len - PA_SW_LEN;
}
/* -----------------------------Init group -----------------------------------*/
static void
dev_init(FpDevice *dev)
{
    g_print("hello PA: dev_init\n");
    FpiSsm *ssm;
    GError *error = NULL;

    if (!g_usb_device_claim_interface(fpi_device_get_usb_device(dev), 0, 0, &error))
    {
        fpi_device_open_complete(dev, error);
        return;
    }

    ssm = fpi_ssm_new(dev, initpa_run_state, INIT_DONE);
    fpi_ssm_start(ssm, initpa_done);
}

static void
initpa_run_state(FpiSsm *ssm, FpDevice *dev)
{
    g_print("hello PA: initsm_run_state %d\n", fpi_ssm_get_cur_state(ssm));
    g_print("hello PA: initsm_run_state %d\n", fpi_ssm_get_cur_state(ssm));
    switch (fpi_ssm_get_cur_state(ssm))
    {
    case ABORT_PUT:
        g_print("hello PA: ABORT_PUT\n");
        //alloc_send_cmd_transfer(dev, ssm, PA_CMD_ABORT, 0, 0, str_abort, strlen(str_abort));
        alloc_send_cmd_transfer(dev, ssm, PA_CMD_ABORT, 0, 0, (unsigned char *)str_abort, strlen(str_abort));
        break;
    case ABORT_GET:
        g_print("hello PA: ABORT_GET\n");
        alloc_get_cmd_transfer(dev, handle_get_abort, ssm);
        break;
    default:
        g_print("hello PA: enroll_start_pa_run_state DEFAULT %d\n", fpi_ssm_get_cur_state(ssm));
        break;
    }
}

static void handle_get_abort(FpDevice *dev,
                             unsigned char *data,
                             size_t data_len,
                             void *user_data,
                             GError *error)
{
    FpiSsm *ssm = user_data;
    g_print("hello PA:handle_get_abort %ld\n", data_len);
    int result = get_sw(data, data_len);
    if (result == PA_OK || result == PA_FPM_CONDITION)
        fpi_ssm_next_state(ssm);
    else
    {
        //TODO: error handle
    }
}

static void
initpa_done(FpiSsm *ssm, FpDevice *dev, GError *error)
{
    g_print("hello PA: initpa_done\n");
    // FpPrint *print = NULL;

    // g_autoptr(GVariant) data = NULL;
    // guint8 finger;
    // const guint8 *user_id;
    // gsize user_id_len = 0;

    // fpi_device_get_verify_data(dev, &print);

    // g_object_get(print, "fpi-data", &data, NULL);
    // g_debug("data is %p", data);
    // if (!parse_print_data(data, &finger, &user_id, &user_id_len))
    // {
    //     fpi_device_verify_complete(dev,fpi_device_error_new(FP_DEVICE_ERROR_DATA_INVALID));
    //     return;
    // }
    fpi_device_open_complete(dev, error);
}
/* -----------------------------De-Init group -----------------------------------*/
static void
dev_exit(FpDevice *dev)
{
    g_print("hello PA: dev_exit\n");
    GError *error = NULL;

    g_usb_device_release_interface(fpi_device_get_usb_device(dev), 0, 0, &error);

    fpi_device_close_complete(dev, error);
}

/* -----------------------------Enroll group -----------------------------------*/
static void
enroll(FpDevice *dev)
{
    g_print("hello PA: enroll\n");
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(dev);
    FpPrint *print = NULL;
    g_autoptr(GVariant) data = NULL;
    g_autofree gchar *user_id = NULL;
    fpi_device_get_enroll_data(dev, &print);
    user_id = fpi_print_generate_user_id(print);
    g_debug("user_id: %s, finger: %d", user_id, print);

    G_DEBUG_HERE();
    g_object_get(print, "fpi-data", &data, NULL);
    g_debug("data is %p", data);
    /* do_init state machine first */
    // FpiSsm *ssm = fpi_ssm_new(dev, enroll_start_pa_run_state,
    //                           ENROLL_UPDATE);

    // padev->enroll_stage = 0;
    // fpi_ssm_start(ssm, enroll_started);
}

static void
enroll_start_pa_run_state(FpiSsm *ssm, FpDevice *dev)
{
    g_print("hello PA: enroll_start_pa_run_state %d\n", fpi_ssm_get_cur_state(ssm));
    switch (fpi_ssm_get_cur_state(ssm))
    {
    case ENROLL_CMD_SEND:
        g_print("hello PA: ENROLL_CMD_SEND\n");
        alloc_send_cmd_transfer(dev, ssm, PA_CMD_ENROLL, 0, 0, (unsigned char *)str_enroll, strlen(str_enroll));
        break;
    case ENROLL_CMD_GET:
        g_print("hello PA: ENROLL_CMD_GET\n");
        alloc_get_cmd_transfer(dev, handle_get_enroll, ssm);
        break;
    default:
        g_print("hello PA: enroll_start_pa_run_state DEFAULT %d\n", fpi_ssm_get_cur_state(ssm));
        break;
    }
}

static void handle_get_enroll(FpDevice *dev,
                              unsigned char *data,
                              size_t data_len,
                              void *user_data,
                              GError *error)
{
    FpiSsm *ssm = user_data;
    g_print("hello PA:handle_get_enroll %ld\n", data_len);
    int result = get_sw(data, data_len);
    if (result == PA_OK)
        fpi_ssm_next_state(ssm);
    else
    {
        //TODO: error handle
    }
}

static void
enroll_iterate(FpDevice *dev)
{
    g_print("hello PA: enroll_iterate\n");
    alloc_send_cmd_transfer(dev, NULL, PA_CMD_FPSTATE, 0, 0, NULL, 1);
}

static void
enroll_iterate_cmd_cb(FpiUsbTransfer *transfer, FpDevice *device,
                      gpointer user_data, GError *error)
{
    g_print("hello PA: enroll_iterate_cmd_cb\n");
    alloc_get_cmd_transfer(device, handle_enroll_iterate_cb, NULL);
}

static void handle_enroll_iterate_cb(FpDevice *dev,
                                     unsigned char *data,
                                     size_t data_len,
                                     void *user_data,
                                     GError *error)
{
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(dev);
    unsigned char code = 0;
    g_print("hello PA: handle_enroll_iterate_cb %d \n", padev->enroll_stage);
    int result = get_sw(data, data_len);
    if (result == PA_OK)
    {
        get_data(data, data_len, &code);
        g_print("hello PA: enroll_state %d\n", code);
        // for enroll test, not complete the enroll since delete not done
        // if(padev->enroll_stage==10)
        // {
        //     g_print("hello PA: 10 %d\n", code);
        //     do_enroll_done(dev);
        //     return;
        // }
        if (code == PA_FPM_ENROLL_GOOD)
        {
            g_print("hello PA: PA_FPM_ENROLL_GOOD %d\n", padev->enroll_stage);
            padev->enroll_stage += 1;
            fpi_device_enroll_progress(dev, padev->enroll_stage, NULL, NULL);
        }
        if (code == PA_FPM_ENROLL_OK)
        {
            padev->enroll_stage = 16;
            g_print("hello PA: PA_FPM_ENROLL_OK\n");
            fpi_device_enroll_progress(dev, padev->enroll_stage, NULL, NULL);
            g_print("hello PA: PA_FPM_ENROLL_OK %d\n", padev->enroll_stage);
            do_enroll_done(dev);
        }
    }
    else
    {
        //TODO: error handle
    }
    enroll_iterate(dev);
}

static void
enroll_started(FpiSsm *ssm, FpDevice *dev, GError *error)
{

    g_print("hello PA: enroll_started %d\n", fpi_ssm_get_cur_state(ssm));

    enroll_iterate(dev);
}

static void
do_enroll_done(FpDevice *dev)
{
    FpPrint *print = NULL;
    GError *err = NULL;
    save_finger(dev, print);
    fpi_device_enroll_complete(dev, print, err);
}
/* -----------------------------Verify group -----------------------------------*/
static void
verify(FpDevice *dev)
{
    g_print("hello PA: verify\n");
    // FpiSsm *ssm = fpi_ssm_new(dev, verify_start_pa_run_state, VERIFY_FINAL);
    // fpi_ssm_start(ssm, verify_started);
    FpPrint *print = NULL;

    g_autoptr(GVariant) data = NULL;
    guint8 finger;
    const guint8 *user_id;
    gsize user_id_len = 0;

    fpi_device_get_verify_data(dev, &print);

    g_object_get(print, "fpi-data", &data, NULL);
    g_debug("data is %p", data);
}

static void
verify_start_pa_run_state(FpiSsm *ssm, FpDevice *dev)
{
    g_print("hello PA: verify_start_pa_run_state %d\n", fpi_ssm_get_cur_state(ssm));
    switch (fpi_ssm_get_cur_state(ssm))
    {
    case VERIFY_INIT:
        g_print("hello PA: VERIFY_INIT\n");
        fpi_ssm_next_state(ssm);
        break;
    case VERIFY_UPDATE:
        g_print("hello PA: VERIFY_UPDATE\n");
        verify_iterate(dev);
        fpi_ssm_next_state(ssm);
        break;
    default:
        g_print("hello PA: enroll_start_pa_run_state DEFAULT %d\n", fpi_ssm_get_cur_state(ssm));
        break;
    }
}
static void
verify_iterate(FpDevice *dev)
{
    g_print("hello PA: verify_iterate\n");
}

static void
verify_started(FpiSsm *ssm, FpDevice *dev, GError *error)
{
    GError *err = NULL;
    fpi_device_verify_report(dev, FPI_MATCH_SUCCESS, NULL, NULL);
    fpi_device_verify_complete(dev, err);
}
/* -----------------------------List group -----------------------------------*/
static void
save_finger(FpDevice *device, FpPrint *print)
{
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(device);
    //padev->list_result = g_ptr_array_new_with_free_func(g_object_unref);
    //pa_finger_list_t l;
    //l.total_number = 1;

    //FpPrint *print = fp_print_new(FP_DEVICE(device));
    gchar *userid;
    g_autofree gchar *user_id = NULL;
    gssize user_id_len;

    fpi_device_get_enroll_data(device, &print);
    G_DEBUG_HERE();
    user_id = fpi_print_generate_user_id(print);
    user_id_len = strlen(user_id);
    g_print("hello PA: gen_report  %s len =%d \n", userid, user_id_len);

    GVariant *data = NULL;
    GVariant *uid = NULL;
    uid = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
                                    user_id,
                                    user_id_len,
                                    1);
    data = g_variant_new("(y@ay)",
                         3,
                         uid);
    fpi_print_set_type(print, FPI_PRINT_RAW);
    fpi_print_set_device_stored(print, TRUE);
    g_object_set(print, "fpi-data", data, NULL);
    g_object_set(print, "description", pa_description, NULL);
    fpi_print_fill_from_user_id(print, user_id);

    //g_ptr_array_add(padev->list_result, g_object_ref_sink(print));
    // fpi_device_list_complete(FP_DEVICE(padev),
    //                          g_steal_pointer(&padev->list_result),
    //                          NULL);
}

static void
list(FpDevice *device)
{
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(device);
    padev->list_result = g_ptr_array_new_with_free_func(g_object_unref);
    pa_finger_list_t l;
    l.total_number = 1;
    FpPrint *print = fp_print_new(FP_DEVICE(device));
    GVariant *data = NULL;
    GVariant *uid = NULL;

    uid = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
                                    "root",
                                    4,
                                    1);
    data = g_variant_new("(y@ay)",
                         3,
                         uid);

    fpi_print_set_type(print, FPI_PRINT_RAW);
    fpi_print_set_device_stored(print, TRUE);
    g_object_set(print, "fpi-data", data, NULL);
    g_object_set(print, "description", pa_description, NULL);
    fp_print_set_username(print,"root");
    fp_print_set_finger(print,3);

    fpi_print_fill_from_user_id(print, "root");
    g_ptr_array_add(padev->list_result, g_object_ref_sink(print));
    fpi_device_list_complete(FP_DEVICE(device),
                             g_steal_pointer(&padev->list_result),
                             NULL);
}
/* -----------------------------Delete group -----------------------------------*/
static void delete (FpDevice *device)
{
    g_print("hello PA: verify\n");
    FpiSsm *ssm = fpi_ssm_new(device, delete_cmd_state, DELETE_DONE);
    fpi_ssm_start(ssm, delete_done);
}

static void
delete_cmd_state(FpiSsm *ssm, FpDevice *dev)
{
    g_print("hello PA: delete_cmd_state %d\n", fpi_ssm_get_cur_state(ssm));
    switch (fpi_ssm_get_cur_state(ssm))
    {
    case DELETE_SEND:
        g_print("hello PA: DELETE_SEND\n");
        alloc_send_cmd_transfer(dev, ssm, PA_CMD_DELETE, 0, 0, (unsigned char *)str_delete, strlen(str_delete));
        break;
    case DELETE_GET:
        g_print("hello PA: DELETE_GET\n");
        alloc_get_cmd_transfer(dev, handle_get_delete, ssm);
        break;
    default:
        g_print("hello PA: delete_cmd_state DEFAULT %d\n", fpi_ssm_get_cur_state(ssm));
        break;
    }
}

static void handle_get_delete(FpDevice *dev,
                              unsigned char *data,
                              size_t data_len,
                              void *user_data,
                              GError *error)
{
    FpiSsm *ssm = user_data;
    g_print("hello PA:handle_get_enroll %ld\n", data_len);
    int result = get_sw(data, data_len);
    if (result == PA_OK)
        fpi_ssm_next_state(ssm);
    else
    {
        //TODO: error handle
    }
}

static void
delete_done(FpiSsm *ssm, FpDevice *dev, GError *error)
{
    fpi_device_delete_complete(dev, NULL);
}

//main entry
static void
fpi_device_pa_primex_class_init(FpiDevicePa_PrimexClass *klass)
{
    FpDeviceClass *dev_class = FP_DEVICE_CLASS(klass);
    g_print("hello PA: fpi_device_pa_primex_class_init\n");

    dev_class->id = "pa_primex";
    dev_class->full_name = "Pixelauth PrimeX";
    dev_class->type = FP_DEVICE_TYPE_USB;
    dev_class->id_table = id_table;
    dev_class->scan_type = FP_SCAN_TYPE_PRESS;

    dev_class->nr_enroll_stages = 16;

    dev_class->open = dev_init;
    dev_class->close = dev_exit;
    dev_class->verify = verify;
    dev_class->enroll = enroll;
    dev_class->delete = delete;
    dev_class->list = list; //ifdef list, the device has storage will return true
}
