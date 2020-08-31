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
#include "pixelauth.h"
#include "storage_helper.h"
#include "pa_primex.h"

struct _FpiDevicePa_Primex
{
    FpDevice parent;
    gint enroll_stage;
    GPtrArray *list_result;
    guchar matched_index[PA_MAX_FINGER_COUNT];
    gint opt_stage;
    pa_finger_list_t g_list;   //only use to figure enroll change out
    pa_finger_list_t original; //only use to figure enroll change out
    gboolean is_canceled;
};

G_DECLARE_FINAL_TYPE(FpiDevicePa_Primex, fpi_device_pa_primex, FPI, DEVICE_PA_PRIME, FpDevice);
G_DEFINE_TYPE(FpiDevicePa_Primex, fpi_device_pa_primex, FP_TYPE_DEVICE);

static void p_print(guchar *buf, gint len)
{
    g_print("buf len = %d \n", len);
    for (gint i = 0; i < len; i++)
    {
        g_print("0x%x ", buf[i]);
    }
    g_print("\n");
}

static void
fpi_device_pa_primex_init(FpiDevicePa_Primex *self)
{
}

/* -----------------------------USB layer group -----------------------------------*/
static void
alloc_send_cmd_transfer(FpDevice *self,
                        FpiSsm *ssm,
                        guchar ins,
                        guchar p1,
                        guchar p2,
                        const gchar *data,
                        guint16 len)
{
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(self);
    FpiUsbTransfer *transfer = fpi_usb_transfer_new(self);
    gint real_len = 0;
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
#if PA_DEBUG_USB
    p_print(transfer->buffer, real_len);
    fp_info("PixelAuth: padev->op_state %x ins %x\n", padev->opt_stage, ins);
#endif
    if (ins == PA_CMD_FPSTATE && padev->opt_stage == PA_CMD_ENROLL)
        fpi_usb_transfer_submit(transfer, TIMEOUT, NULL, enroll_iterate_cmd_cb, NULL);
    else if (ins == PA_CMD_FPSTATE && padev->opt_stage == PA_CMD_VERIFY)
        fpi_usb_transfer_submit(transfer, TIMEOUT, NULL, verify_iterate_cmd_cb, NULL);
    else
        fpi_usb_transfer_submit(transfer, TIMEOUT, NULL, fpi_ssm_usb_transfer_cb, NULL);
}

static void
alloc_get_cmd_transfer(FpDevice *self,
                       handle_get_fn callback,
                       void *user_data)
{
    FpiUsbTransfer *transfer = fpi_usb_transfer_new(self);
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
read_cb(FpiUsbTransfer *transfer, FpDevice *self,
        gpointer user_data, GError *error)
{
    struct prime_data *udata = user_data;

    if (transfer->actual_length < PA_HEADER_LEN + PA_LEN_LEN + PA_SW_LEN)
    {
        fp_info("PixelAuth: read_cb len = %ld\n", transfer->actual_length);
        fp_info("PixelAuth: error %s\n", error->message);
        return;
    }
#if PA_DEBUG_USB
    p_print(transfer->buffer, transfer->actual_length);
#endif
    handle_response(self, transfer, udata);

    return;
}

static void
handle_response(FpDevice *self, FpiUsbTransfer *transfer, struct prime_data *udata)
{
    guint8 *buf = udata->buffer;
    guint16 len = transfer->actual_length;
    udata->callback(self, buf, len, udata->user_data, NULL);
}

static gint get_sw(guchar *data, gssize data_len)
{
    gint len = data[6];
    if (data[7 + len - 2] == 0x90 && data[8 + len - 2] == 0)
        return PA_OK;
    if (data[7 + len - 2] == 0x6f && data[8 + len - 2] == 3)
        return PA_FPM_CONDITION;
    if (data[7 + len - 2] == 0x6f && data[8 + len - 2] == 5)
        return PA_FPM_REFDATA;
    if (data[7 + len - 2] == 0x6a && data[8 + len - 2] == 0x86)
        return PA_P1P2;
    if (data[7 + len - 2] == 0x6a && data[8 + len - 2] == 0x84)
        return PA_NOSPACE;

    fp_warn("PA: SW error %x %x\n", data[7 + len - 2], data[8 + len - 2]);
    return PA_ERROR;
}

static gint get_data(guchar *data, gssize data_len, guchar *buf)
{
    gint len = data[6];
    if (len == PA_SW_LEN)
        return PA_OK;
    if (len > PA_SW_LEN)
        memcpy(buf, data + 7, len - PA_SW_LEN);
    return len - PA_SW_LEN;
}
/* -----------------------------Init group -----------------------------------*/
static void
dev_init(FpDevice *self)
{
    FpiSsm *ssm;
    GError *error = NULL;

    if (!g_usb_device_claim_interface(fpi_device_get_usb_device(self), 0, 0, &error))
    {
        fpi_device_open_complete(self, error);
        return;
    }

    ssm = fpi_ssm_new(self, abort_run_state, INIT_DONE);
    fpi_ssm_start(ssm, init_done);
}

static void
abort_run_state(FpiSsm *ssm, FpDevice *self)
{
    switch (fpi_ssm_get_cur_state(ssm))
    {
    case ABORT_PUT:
        alloc_send_cmd_transfer(self, ssm, PA_CMD_ABORT, 0, 0, str_abort, strlen(str_abort));
        break;
    case ABORT_GET:
        alloc_get_cmd_transfer(self, handle_get_abort, ssm);
        break;
    default:
        break;
    }
}

static void handle_get_abort(FpDevice *self,
                             guchar *data,
                             gssize data_len,
                             void *user_data,
                             GError *error)
{
    FpiSsm *ssm = user_data;
    gint result = get_sw(data, data_len);
    if (result == PA_OK || result == PA_FPM_CONDITION)
        fpi_ssm_next_state(ssm);
    else
    {
        //TODO: error handle ??
    }
}

static void abort_done(FpiSsm *ssm, FpDevice *self, GError *error)
{
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(self);
    fp_info("PixelAuth:cancel sent!\n");
    if (padev->opt_stage == PA_CMD_ENROLL)
    {
        enroll_deinit(self, NULL, g_error_new_literal(G_IO_ERROR, G_IO_ERROR_CANCELLED, "Cancelled"));
    }
    else if (padev->opt_stage == PA_CMD_VERIFY)
    {
        verify_deinit(self, NULL, FPI_MATCH_FAIL, g_error_new_literal(G_IO_ERROR, G_IO_ERROR_CANCELLED, "Cancelled"));
    }
}

static void init_done(FpiSsm *ssm, FpDevice *self, GError *error)
{
    fpi_device_open_complete(self, error);
}
/* -----------------------------De-Init group -----------------------------------*/
static void
dev_exit(FpDevice *self)
{
    GError *error = NULL;
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(self);
    g_clear_pointer(&padev->list_result, g_ptr_array_unref);
    G_DEBUG_HERE();
    g_usb_device_release_interface(fpi_device_get_usb_device(self), 0, 0, &error);
    fpi_device_close_complete(self, error);
}

/* -----------------------------Enroll group -----------------------------------*/
static void
enroll_init(FpDevice *self)
{
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(self);
    memset(padev->g_list.finger_map, 0xff, PA_MAX_FINGER_COUNT);
    memset(padev->original.finger_map, 0xff, PA_MAX_FINGER_COUNT);
    padev->g_list.total_number = 0;
    padev->original.total_number = 0;
    padev->enroll_stage = 0;
    padev->is_canceled = FALSE;
}
static void
enroll(FpDevice *self)
{
    FpPrint *print = NULL;
    fpi_device_get_enroll_data(self, &print);
    FpPrint *enroll_print = pa_data_load(self,
                                         fp_print_get_finger(print),
                                         fp_print_get_username(print),
                                         PA_MAX_FINGER_COUNT);
    if (enroll_print) //refuse to enroll again if same user same finger
    {
        enroll_deinit(self,
                      NULL,
                      fpi_device_error_new(FP_DEVICE_ERROR_DATA_INVALID)); //no useful error code
        return;
    }

    enroll_init(self);
    FpiSsm *ssm = fpi_ssm_new(self, enroll_start_run_state, ENROLL_UPDATE);
    fpi_ssm_start(ssm, enroll_started);
}

static void
enroll_start_run_state(FpiSsm *ssm, FpDevice *self)
{
    switch (fpi_ssm_get_cur_state(ssm))
    {
    case ENROLL_LIST_BEFORE_SEND:
        alloc_send_cmd_transfer(self, ssm, PA_CMD_LIST, 0x80, 0, NULL, 0);
        break;
    case ENROLL_LIST_BEFORE_GET:
        alloc_get_cmd_transfer(self, handle_get_list, ssm);
        break;
    case ENROLL_CMD_SEND:
        alloc_send_cmd_transfer(self, ssm, PA_CMD_ENROLL, 0, 0, str_enroll, strlen(str_enroll));
        break;
    case ENROLL_CMD_GET:
        alloc_get_cmd_transfer(self, handle_get_enroll, ssm);
        break;
    default:
        break;
    }
}

static void handle_get_enroll(FpDevice *self,
                              guchar *data,
                              gssize data_len,
                              void *user_data,
                              GError *error)
{
    FpiSsm *ssm = user_data;
    gint result = get_sw(data, data_len);
    if (result == PA_OK)
        fpi_ssm_next_state(ssm);
    else if (result == PA_NOSPACE)
    {
        enroll_deinit(self,
                      NULL,
                      fpi_device_error_new(FP_DEVICE_ERROR_DATA_FULL));
    }
    else
    {
        enroll_deinit(self,
                      NULL,
                      fpi_device_error_new_msg(FP_DEVICE_ERROR_GENERAL,
                                               "Enrollment failed (%d)",
                                               result));
    }
}

static void
enroll_iterate(FpDevice *self)
{
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(self);
    if (padev->is_canceled)
    {
        FpiSsm *ssm = fpi_ssm_new(self, abort_run_state, INIT_DONE);
        fpi_ssm_start(ssm, abort_done);
        return;
    }
    alloc_send_cmd_transfer(self, NULL, PA_CMD_FPSTATE, 0, 0, NULL, 1);
}

static void
enroll_iterate_cmd_cb(FpiUsbTransfer *transfer, FpDevice *self,
                      gpointer user_data, GError *error)
{
    alloc_get_cmd_transfer(self, handle_enroll_iterate_cb, NULL);
}

static void handle_enroll_iterate_cb(FpDevice *self,
                                     guchar *data,
                                     gssize data_len,
                                     void *user_data,
                                     GError *error)
{
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(self);
    guchar code = 0;
    gint result = get_sw(data, data_len);
    if (result == PA_OK)
    {
        get_data(data, data_len, &code);
        if (code == PA_FPM_ENROLL_REDUNDANT)
        {
            fpi_device_enroll_progress(self,
                                       padev->enroll_stage,
                                       NULL,
                                       fpi_device_retry_new(FP_DEVICE_RETRY_GENERAL));
        }
        if (code == PA_FPM_ENROLL_NOTFULLFINGER)
        {
            fpi_device_enroll_progress(self,
                                       padev->enroll_stage,
                                       NULL,
                                       fpi_device_retry_new(FP_DEVICE_RETRY_CENTER_FINGER));
        }
        if (code == PA_FPM_ENROLL_GOOD)
        {
            padev->enroll_stage += 1;
            fpi_device_enroll_progress(self, padev->enroll_stage, NULL, NULL);
        }
        if (code == PA_FPM_ENROLL_OK)
        {
            padev->enroll_stage = PA_MAX_ENROLL_COUNT;
            fpi_device_enroll_progress(self, padev->enroll_stage, NULL, NULL);
            do_enroll_done(self);
            return;
        }
    }
    else
    {
        enroll_deinit(self,
                      NULL,
                      fpi_device_error_new_msg(FP_DEVICE_ERROR_GENERAL,
                                               "Enrollment failed"));
        return;
    }
    if (padev->enroll_stage < PA_MAX_ENROLL_COUNT)
        enroll_iterate(self);
}

static void
enroll_started(FpiSsm *ssm, FpDevice *self, GError *error)
{
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(self);
    padev->opt_stage = PA_CMD_ENROLL;
    enroll_iterate(self);
}
static void
enroll_deinit(FpDevice *self, FpPrint *print, GError *error)
{
    if (error)
    {
        fp_warn("Error enroll deinitializing: %s", error->message);
        fpi_device_enroll_complete(self, NULL, error);
    }
    else
    {
        fpi_device_enroll_complete(self, print, NULL);
    }
}
static void
do_enroll_done(FpDevice *self)
{
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(self);
    padev->opt_stage = 0;
    //backup original list
    padev->original.total_number = padev->g_list.total_number;
    memcpy(padev->original.finger_map, padev->g_list.finger_map, PA_MAX_FINGER_COUNT);
    //start ssm to get list
    FpiSsm *ssm = fpi_ssm_new(self, enroll_finish_run_state, ENROLL_DONE);
    fpi_ssm_start(ssm, enroll_save);
}
static void
enroll_save(FpiSsm *ssm, FpDevice *self, GError *error)
{
    FpPrint *print = NULL;
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(self);
    fpi_device_get_enroll_data(self, &print);
    gint dev_new_index = -1;
    fp_info("PixelAuth:enroll done finger %d \n", fp_print_get_finger(print));
    if (padev->g_list.total_number - padev->original.total_number != 1) //not enroll 1 finger
        return;
    for (gint i = 0; i < PA_MAX_FINGER_COUNT; i++)
    {
        if (padev->g_list.finger_map[i] != padev->original.finger_map[i])
        {
            dev_new_index = padev->g_list.finger_map[i];
            break;
        }
    }
    gen_finger(dev_new_index, print);
    //pa_data_save(print, fp_print_get_finger(print));
    pa_data_save(print, dev_new_index);
    enroll_deinit(self, print, error);
}

static void
enroll_finish_run_state(FpiSsm *ssm, FpDevice *self)
{
    switch (fpi_ssm_get_cur_state(ssm))
    {
    case ENROLL_LIST_AFTER_SEND:
        alloc_send_cmd_transfer(self, ssm, PA_CMD_LIST, 0x80, 0, NULL, 0);
        break;
    case ENROLL_LIST_AFTER_GET:
        alloc_get_cmd_transfer(self, handle_get_list, ssm);
        break;
    default:
        break;
    }
}

/* -----------------------------Verify group -----------------------------------*/
static void
verify(FpDevice *self)
{
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(self);
    FpPrint *print = NULL;
    fpi_device_get_verify_data(self, &print);
    gint dev_index = get_dev_index(self, print,PA_MAX_FINGER_COUNT);
    if (dev_index == PA_ERROR)
    {
        verify_deinit(self, NULL, FPI_MATCH_FAIL, NULL);
        return;
    }

    padev->is_canceled = FALSE;
    memset(padev->matched_index, 0xff, PA_MAX_FINGER_COUNT);
    FpiSsm *ssm = fpi_ssm_new(self, verify_start_run_state, VERIFY_UPDATE);
    fpi_ssm_start(ssm, verify_started);
}

static void
verify_start_run_state(FpiSsm *ssm, FpDevice *self)
{
    switch (fpi_ssm_get_cur_state(ssm))
    {
    case VERIFY_CMD_SEND:
        alloc_send_cmd_transfer(self, ssm, PA_CMD_VERIFY, 0, 0, str_verify, strlen(str_verify));
        break;
    case VERIFY_CMD_GET:
        alloc_get_cmd_transfer(self, handle_get_verify, ssm);
        break;
    default:
        break;
    }
}

static void
verify_deinit(FpDevice *self, FpPrint *print, FpiMatchResult result, GError *error)
{
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(self);
    memset(padev->matched_index, 0xff, PA_MAX_FINGER_COUNT);
    fpi_device_verify_report(self, result, print, NULL);
    fpi_device_verify_complete(self, error);
}

static void handle_get_verify(FpDevice *self,
                              guchar *data,
                              gssize data_len,
                              void *user_data,
                              GError *error)
{
    FpiSsm *ssm = user_data;
    gint result = get_sw(data, data_len);
    if (result == PA_OK)
        fpi_ssm_next_state(ssm);
    else if (result == PA_FPM_REFDATA)
    { //no finger inside
        verify_deinit(self, NULL, FPI_MATCH_FAIL, NULL);
    }
    else
    {
        verify_deinit(self, NULL, FPI_MATCH_ERROR, NULL);
    }
}

static void
verify_iterate(FpDevice *self)
{
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(self);
    if (padev->is_canceled)
    {
        FpiSsm *ssm = fpi_ssm_new(self, abort_run_state, INIT_DONE);
        fpi_ssm_start(ssm, abort_done);
        return;
    }
    alloc_send_cmd_transfer(self, NULL, PA_CMD_FPSTATE, 0, 0, NULL, 1);
}

static void
verify_started(FpiSsm *ssm, FpDevice *self, GError *error)
{
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(self);
    padev->opt_stage = PA_CMD_VERIFY;
    verify_iterate(self);
}
static void
verify_iterate_cmd_cb(FpiUsbTransfer *transfer, FpDevice *self,
                      gpointer user_data, GError *error)
{
    alloc_get_cmd_transfer(self, handle_verify_iterate_cb, NULL);
}

static void
handle_verify_iterate_cb(FpDevice *self,
                         guchar *data,
                         gssize data_len,
                         void *user_data,
                         GError *error)
{
    guchar code = 0;
    gint result = get_sw(data, data_len);
    if (result == PA_OK)
    {
        get_data(data, data_len, &code);
        if (code == PA_FPM_VERIFY_OK)
        {
            do_verify_done(self);
            return;
        }
        else if (code == PA_FPM_VERIFY_WAITING)
        {
            //nothing to do
        }
        else
        {
            verify_deinit(self, NULL, FPI_MATCH_FAIL, NULL);
            return;
        }
    }
    else
    {
        verify_deinit(self, NULL, FPI_MATCH_ERROR, NULL);
        return;
    }

    verify_iterate(self);
}

static void
do_verify_done(FpDevice *self)
{
    FpiSsm *ssm = fpi_ssm_new(self, verify_finish_run_state, VERIFY_FINAL);
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(self);
    padev->opt_stage = 0;
    fpi_ssm_start(ssm, verify_report);
}

static void verify_finish_run_state(FpiSsm *ssm, FpDevice *self)
{
    switch (fpi_ssm_get_cur_state(ssm))
    {
    case VERIFY_GET_ID_SEND:
        alloc_send_cmd_transfer(self, ssm, PA_CMD_VID, 0, 0, NULL, 0);
        break;
    case VERIFY_GET_ID_GET:
        alloc_get_cmd_transfer(self, handle_get_vid, ssm);
        break;
    default:
        break;
    }
}

static void handle_get_vid(FpDevice *self,
                           guchar *data,
                           gssize data_len,
                           void *user_data,
                           GError *error)
{
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(self);
    FpiSsm *ssm = user_data;
    guchar index[PA_MAX_FINGER_COUNT] = {0};
    gint result = get_sw(data, data_len);
    if (result == PA_OK)
    {
        get_data(data, data_len, index);
        memcpy(padev->matched_index, index, PA_MAX_FINGER_COUNT);
        fpi_ssm_next_state(ssm);
    }
    else
    {
        verify_deinit(self, NULL, FPI_MATCH_ERROR, NULL);
    }
}

static void
verify_report(FpiSsm *ssm, FpDevice *self, GError *error)
{
    FpPrint *print = NULL;
    fpi_device_get_verify_data(self, &print);
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(self);
    gint dev_index = get_dev_index(self, print,PA_MAX_FINGER_COUNT);
    if (dev_index == PA_ERROR)
        verify_deinit(self, NULL, FPI_MATCH_ERROR, NULL);

    for (gint i = 0; i < PA_MAX_FINGER_COUNT; i++)
    {
        if (dev_index == padev->matched_index[i])
        {
            verify_deinit(self, print, FPI_MATCH_SUCCESS, NULL);
            return;
        }
    }
    verify_deinit(self, NULL, FPI_MATCH_FAIL, NULL);
}
/* -----------------------------List group -----------------------------------*/

static void
list(FpDevice *self)
{
    FpiSsm *ssm = fpi_ssm_new(self, list_run_state, LIST_DONE);
    fpi_ssm_start(ssm, list_done);
}
static void
list_done(FpiSsm *ssm, FpDevice *self, GError *error)
{
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(self);
    padev->list_result = g_ptr_array_new_with_free_func(g_object_unref);
    for (gint i = 1; i < PA_MAX_FINGER_COUNT + 1; i++)
    {
        FpPrint *back = pa_data_load(self, i, NULL, PA_MAX_FINGER_COUNT);
        if (!back)
            continue;

        FpPrint *print = fp_print_new(FP_DEVICE(self));
        fpi_print_set_type(print, FPI_PRINT_RAW);
        fpi_print_set_device_stored(print, TRUE);
        fp_info("PixelAut: username %s finger %d\n",
                fp_print_get_username(back),
                fp_print_get_finger(back));
        fp_print_set_username(print, fp_print_get_username(back));
        fp_print_set_finger(print, fp_print_get_finger(back));
        g_object_set(print,
                     "description",
                     fp_print_get_description(back),
                     NULL);
        g_ptr_array_add(padev->list_result, g_object_ref_sink(print));
    }

    fpi_device_list_complete(FP_DEVICE(self),
                             g_steal_pointer(&padev->list_result),
                             NULL);
}
static void
list_run_state(FpiSsm *ssm, FpDevice *self)
{
    switch (fpi_ssm_get_cur_state(ssm))
    {
    case LIST_SEND:
        alloc_send_cmd_transfer(self, ssm, PA_CMD_LIST, 0x80, 0, NULL, 0);
        break;
    case LIST_GET:
        alloc_get_cmd_transfer(self, handle_get_list, ssm);
        break;
    default:
        break;
    }
}

static void handle_get_list(FpDevice *self,
                            guchar *data,
                            gssize data_len,
                            void *user_data,
                            GError *error)
{
    FpiSsm *ssm = user_data;
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(self);
    gint result = get_sw(data, data_len);
    if (result != PA_OK)
    {
        //TODO: error handle
    }
    else
    {
        padev->g_list.total_number = get_data(data, data_len, padev->g_list.finger_map);
        fp_info("PixelAuth: handle_get_list number %d\n", padev->g_list.total_number);
        p_print(padev->g_list.finger_map, PA_MAX_FINGER_COUNT);
        fpi_ssm_next_state(ssm);
    }
}
/* -----------------------------Delete group -----------------------------------*/
static void delete (FpDevice *self)
{
    FpiSsm *ssm = fpi_ssm_new(self, delete_cmd_state, DELETE_DONE);
    fpi_ssm_start(ssm, delete_done);
}

static void
delete_cmd_state(FpiSsm *ssm, FpDevice *self)
{
    FpPrint *print = NULL;
    switch (fpi_ssm_get_cur_state(ssm))
    {
    case DELETE_SEND:
        fpi_device_get_delete_data(self, &print);
        gint dev_index = get_dev_index(self, print,PA_MAX_FINGER_COUNT);
        alloc_send_cmd_transfer(self, ssm, PA_CMD_DELETE, dev_index + 1, 0, str_delete, strlen(str_delete));
        //for FK, close upper and open lower to delete all exists
        //alloc_send_cmd_transfer(self, ssm, PA_CMD_DELETE, 0, 0, str_delete, strlen(str_delete));
        break;
    case DELETE_GET:
        alloc_get_cmd_transfer(self, handle_get_delete, ssm);
        break;
    default:
        break;
    }
}

static void handle_get_delete(FpDevice *self,
                              guchar *data,
                              gssize data_len,
                              void *user_data,
                              GError *error)
{
    FpiSsm *ssm = user_data;
    gint result = get_sw(data, data_len);
    if (result == PA_OK || result == PA_FPM_REFDATA)
        fpi_ssm_next_state(ssm);
    else
    {
        //TODO: error handle
    }
}

static void
delete_done(FpiSsm *ssm, FpDevice *self, GError *error)
{
    FpPrint *print = NULL;
    fpi_device_get_delete_data(self, &print);
    pa_data_del(self, print, fp_print_get_username(print),PA_MAX_FINGER_COUNT);
    fpi_device_delete_complete(self, NULL);
}

static void
cancel(FpDevice *self)
{
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(self);
    padev->is_canceled = TRUE;
    fp_info("PixelAuth: opt canceled\n");
}

static void
fpi_device_pa_primex_class_init(FpiDevicePa_PrimexClass *klass)
{
    FpDeviceClass *dev_class = FP_DEVICE_CLASS(klass);
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
    dev_class->list = list;
    dev_class->cancel = cancel;
}
