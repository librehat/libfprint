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

static void
initpa_run_state(FpiSsm *ssm, FpDevice *dev)
{
    g_print("hello PA: initsm_run_state\n");
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
    fpi_device_verify_report (dev, FPI_MATCH_SUCCESS, NULL, NULL);
    fpi_device_verify_complete (dev, err);
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