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

G_DECLARE_FINAL_TYPE(FpiDevicePa_Primex, fpi_device_pa_primex, FPI, DEVICE_PA_PRIME,
                     FpDevice);
G_DEFINE_TYPE(FpiDevicePa_Primex, fpi_device_pa_primex, FP_TYPE_DEVICE);

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
initpa_run_state(FpiSsm *ssm, FpDevice *dev)
{
    g_print("hello PA: initsm_run_state %d\n", fpi_ssm_get_cur_state(ssm));
    g_print("hello PA: enroll_start_pa_run_state %d\n", fpi_ssm_get_cur_state(ssm));
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

static void
initpa_done(FpiSsm *ssm, FpDevice *dev, GError *error)
{
    g_print("hello PA: initpa_done\n");
    fpi_device_open_complete(dev, error);
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
enroll_iterate_cmd_cb(FpiUsbTransfer *transfer, FpDevice *device,
                      gpointer user_data, GError *error)
{
    g_print("hello PA: enroll_iterate_cmd_cb\n");
    alloc_get_cmd_transfer(device, handle_enroll_iterate_cb, NULL);
}

static void
enroll_iterate(FpDevice *dev)
{
    g_print("hello PA: enroll_iterate\n");
    alloc_send_cmd_transfer(dev, NULL, PA_CMD_FPSTATE, 0, 0, NULL, 1);
}

static void
do_enroll_done(FpDevice *dev)
{
    const char *p = "finger1\n";
    FpPrint *print = NULL;
    GError *err = NULL;
    GVariant *fp_data;
    fpi_device_get_enroll_data(dev, &print);
    fp_data = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
                                        p,
                                        strlen(p),
                                        1);
    fpi_print_set_type(print, FPI_PRINT_RAW);
    g_object_set(print, "fpi-data", fp_data, NULL);
    g_object_ref(print);
    fpi_device_enroll_complete(dev, print, err);
}

static void
enroll_started(FpiSsm *ssm, FpDevice *dev, GError *error)
{

    g_print("hello PA: enroll_started %d\n", fpi_ssm_get_cur_state(ssm));

    enroll_iterate(dev);
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
static void
verify_iterate(FpDevice *dev)
{
    g_print("hello PA: verify_iterate\n");
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
verify_started(FpiSsm *ssm, FpDevice *dev, GError *error)
{
    GError *err = NULL;
    fpi_device_verify_report(dev, FPI_MATCH_SUCCESS, NULL, NULL);
    fpi_device_verify_complete(dev, err);
}


static void
fpi_device_pa_primex_init(FpiDevicePa_Primex *self)
{
    g_print("hello PA: fpi_device_pa_primex_init\n");
}

static void
dev_init(FpDevice *dev)
{
    g_print("hello PA: dev_init\n");
    FpiSsm *ssm;
    GError *error = NULL;
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(dev);

    if (!g_usb_device_claim_interface(fpi_device_get_usb_device(dev), 0, 0, &error))
    {
        fpi_device_open_complete(dev, error);
        return;
    }

    padev->seq = 0xf0; /* incremented to 0x00 before first cmd */

    ssm = fpi_ssm_new(dev, initpa_run_state, INIT_DONE);
    fpi_ssm_start(ssm, initpa_done);
}

static void
dev_exit(FpDevice *dev)
{
    g_print("hello PA: dev_exit\n");
    GError *error = NULL;

    g_usb_device_release_interface(fpi_device_get_usb_device(dev), 0, 0, &error);

    fpi_device_close_complete(dev, error);
}


static void
enroll(FpDevice *dev)
{
    g_print("hello PA: enroll\n");
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(dev);
    /* do_init state machine first */
    FpiSsm *ssm = fpi_ssm_new(dev, enroll_start_pa_run_state,
                              ENROLL_UPDATE);

    padev->enroll_passed = FALSE;
    padev->enroll_stage = 0;
    fpi_ssm_start(ssm, enroll_started);
}



static void
verify(FpDevice *dev)
{
    g_print("hello PA: verify\n");
    FpiSsm *ssm = fpi_ssm_new(dev, verify_start_pa_run_state, VERIFY_FINAL);
    fpi_ssm_start(ssm, verify_started);
}

static void
delete(FpDevice *device)
{

}

static void
list(FpDevice *device)
{

}

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

