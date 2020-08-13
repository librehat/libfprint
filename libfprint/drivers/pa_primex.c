#include "drivers_api.h"

struct _FpiDevicePa_Primex
{
    FpDevice parent;

    gboolean enroll_passed;
    gint enroll_stage;
    gboolean first_verify_iteration;
    guint8 seq; /* FIXME: improve/automate seq handling */
};

enum pa_primex_driver_data
{
    PRIMEX,
};

G_DECLARE_FINAL_TYPE(FpiDevicePa_Primex, fpi_device_pa_primex, FPI, DEVICE_PA_PRIME,
                     FpDevice);
G_DEFINE_TYPE(FpiDevicePa_Primex, fpi_device_pa_primex, FP_TYPE_DEVICE);

static const FpIdEntry id_table[] = {
    {.vid = 0x2F0A, .pid = 0x0201, .driver_data = PRIMEX},
    {.vid = 0, .pid = 0, .driver_data = 0},
};

enum initpa_states
{
    ABORT = 0,
    DONE,
};

typedef struct
{
    FpPrint *print;
    GError *error;
} EnrollStopData;

#define PA_HEADER_LEN 5
#define PA_INNER_HEADER_LEN 7
#define PA_APDU_CLA 0xfe
#define PA_CMD_ENROLL 0x71
#define PA_CMD_DELETE 0x73
#define PA_CMD_ABORT 0x74
#define PA_CMD_FPSTATE 0x75
#define PA_CMD_LIST 0x76
#define PA_CMD_VERIFY 0x80
#define PA_CMD_VID 0x81

#define TIMEOUT 5000
#define PA_IN (2 | FPI_USB_ENDPOINT_IN)
#define PA_OUT (1 | FPI_USB_ENDPOINT_OUT)

const unsigned char pa_header[] = {0x50, 0x58, 0x41, 0x54, 0xc0};
const char *str_enroll = "u2f enroll fp";
const char *str_delete = "u2f delete fp";
const char *str_abort = "u2f abort fp";
const char *str_verify = "wbf verify fp";

static void
read_res_back(FpiUsbTransfer *transfer, FpDevice *device,
              gpointer user_data, GError *error);

static void
handle_response(FpDevice *device, unsigned char *udata);

static void
read_cb(FpiUsbTransfer *transfer, FpDevice *device,
        gpointer user_data, GError *error);

static void
alloc_send_cmd_transfer(FpDevice *dev,
                        unsigned char ins,
                        unsigned char p1,
                        unsigned char p2,
                        const unsigned char *data,
                        guint16 len)
{
    g_print("hello PA: alloc_send_cmd_transfer len=%d \n", len);
    g_print("hello PA: alloc_send_cmd_transfer %s \n", data);
    FpiUsbTransfer *transfer = fpi_usb_transfer_new(dev);
    fpi_usb_transfer_fill_bulk(transfer, PA_OUT, len + PA_HEADER_LEN + PA_INNER_HEADER_LEN);
    memcpy(transfer->buffer, pa_header, PA_HEADER_LEN);
    transfer->buffer[5] = (len + PA_INNER_HEADER_LEN) >> 8;
    transfer->buffer[6] = (len + PA_INNER_HEADER_LEN) & 0xff;
    transfer->buffer[7] = PA_APDU_CLA;
    transfer->buffer[8] = ins;
    transfer->buffer[9] = p1;
    transfer->buffer[10] = p2;
    transfer->buffer[10] = 0;
    transfer->buffer[11] = len >> 8;
    transfer->buffer[12] = len & 0xff;
    g_print("hello PA: alloc_send_cmd_transfer header one \n");
    if (len != 0)
        memcpy(transfer->buffer + PA_HEADER_LEN + PA_INNER_HEADER_LEN, data, len);
    g_print("hello PA: alloc_send_cmd_transfer leave\n");
    fpi_usb_transfer_submit(transfer, TIMEOUT, NULL, read_res_back, NULL);
    g_print("hello PA: fpi_usb_transfer_submit\n");
}

static void
read_res_back(FpiUsbTransfer *transfer, FpDevice *device,
              gpointer user_data, GError *error)
{
    unsigned char *udata = user_data;
    udata = g_realloc(udata, 128);
    int needed = 7 + 2;
    g_print("hello PA: read_res_back\n");

    FpiUsbTransfer *etransfer = fpi_usb_transfer_new(device);

    fpi_usb_transfer_fill_bulk_full(etransfer, PA_IN, udata, needed, NULL);
    fpi_usb_transfer_submit(etransfer, TIMEOUT, NULL, read_cb, udata);
}

static void
read_cb(FpiUsbTransfer *transfer, FpDevice *device,
        gpointer user_data, GError *error)
{
    unsigned char *udata = user_data;
    g_print("hello PA: read_cb len = %d\n", transfer->actual_length);
    handle_response(device, udata);
}

static void
handle_response(FpDevice *device, unsigned char *udata)
{
    g_print("hello PA: handle_response %d %d %d \n", udata[0], udata[1], udata[2]);
}

static void pa_abort(FpDevice *dev)
{
    g_print("hello PA: pa_abort \n");
    alloc_send_cmd_transfer(dev, PA_CMD_ABORT, 0, 0, str_abort, strlen(str_abort));
}

static void
initpa_run_state(FpiSsm *ssm, FpDevice *dev)
{
    g_print("hello PA: initsm_run_state\n");
    pa_abort(dev);
    g_print("hello PA: initsm_run_state %d\n", fpi_ssm_get_cur_state(ssm));
    fpi_ssm_next_state(ssm);
}

static void
initpa_done(FpiSsm *ssm, FpDevice *dev, GError *error)
{
    g_print("hello PA: initpa_done\n");
    fpi_device_open_complete(dev, error);
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

    ssm = fpi_ssm_new(dev, initpa_run_state, DONE);
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

enum enroll_start_pa_states
{
    ENROLL_INIT = 0,
    ENROLL_UPDATE,
    ENROLL_FINAL
};

static void
enroll_iterate(FpDevice *dev)
{
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(dev);
    padev->enroll_stage += 1;
    fpi_device_enroll_progress(dev, padev->enroll_stage, NULL, NULL);
    if (padev->enroll_stage < 16)
        enroll_iterate(dev);
}

static void
enroll_started(FpiSsm *ssm, FpDevice *dev, GError *error)
{
    const char *p = "finger1\n";
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(dev);
    g_print("hello PA: enroll_started %d\n", fpi_ssm_get_cur_state(ssm));
    padev->enroll_passed = TRUE;

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
enroll_start_pa_run_state(FpiSsm *ssm, FpDevice *dev)
{
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(dev);
    g_print("hello PA: enroll_start_pa_run_state %d\n", fpi_ssm_get_cur_state(ssm));
    switch (fpi_ssm_get_cur_state(ssm))
    {
    case ENROLL_INIT:
        g_print("hello PA: ENROLL_INIT\n");
        fpi_ssm_next_state(ssm);
        break;
    case ENROLL_UPDATE:
        g_print("hello PA: ENROLL_UPDATE\n");
        enroll_iterate(dev);
        fpi_ssm_next_state(ssm);
        break;
    default:
        g_print("hello PA: enroll_start_pa_run_state DEFAULT %d\n", fpi_ssm_get_cur_state(ssm));
        break;
    }
}

static void
enroll(FpDevice *dev)
{
    g_print("hello PA: enroll\n");
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(dev);
    sleep(2);
    /* do_init state machine first */
    FpiSsm *ssm = fpi_ssm_new(dev, enroll_start_pa_run_state,
                              ENROLL_FINAL);

    padev->enroll_passed = FALSE;
    padev->enroll_stage = 0;
    fpi_ssm_start(ssm, enroll_started);
}

enum verify_start_pa_states
{
    VERIFY_INIT = 0,
    VERIFY_UPDATE,
    VERIFY_FINAL
};

static void
verify_iterate(FpDevice *dev)
{
    g_print("hello PA: verify_iterate\n");
}

static void
verify_start_pa_run_state(FpiSsm *ssm, FpDevice *dev)
{
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(dev);
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
verify(FpDevice *dev)
{
    g_print("hello PA: verify\n");
    FpiSsm *ssm = fpi_ssm_new(dev, verify_start_pa_run_state, VERIFY_FINAL);
    fpi_ssm_start(ssm, verify_started);
}

static void
fpi_device_pa_primex_init(FpiDevicePa_Primex *self)
{
    g_print("hello PA: fpi_device_pa_primex_init\n");
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
}