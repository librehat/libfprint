
#define PA_HEADER_LEN 5
#define PA_LEN_LEN 2
#define PA_INNER_HEADER_LEN 7
#define PA_SW_LEN 2
#define PA_MAX_GET_LEN 256
#define PA_APDU_CLA 0xfe
#define PA_CMD_ENROLL 0x71
#define PA_CMD_DELETE 0x73
#define PA_CMD_ABORT 0x74
#define PA_CMD_FPSTATE 0x75
#define PA_CMD_LIST 0x76
#define PA_CMD_VERIFY 0x80
#define PA_CMD_VID 0x81

#define PA_OK 0
#define PA_FPM_CONDITION 1
#define PA_FPM_REFDATA 2
#define PA_BUSY 3
#define PA_ERROR -1

#define PA_FPM_ENROLL_OK 0xe1
#define PA_FPM_ENROLL_GOOD 0xe4
#define PA_FPM_ENROLL_REDUNDANT 0xe5
#define PA_FPM_ENROLL_NOFINGER 0xe7
#define PA_FPM_ENROLL_NOTFULLFINGER 0xe8
#define PA_FPM_ENROLL_WAITING 0xe0
#define PA_FPM_IDLE 0

#define TIMEOUT 5000
#define PA_IN (2 | FPI_USB_ENDPOINT_IN)
#define PA_OUT (1 | FPI_USB_ENDPOINT_OUT)

const unsigned char pa_header[] = {0x50, 0x58, 0x41, 0x54, 0xc0};
const char *str_enroll = "u2f enroll fp";
const char *str_delete = "u2f delete fp";
const char *str_abort = "u2f abort fp";
const char *str_verify = "wbf verify fp";


enum pa_primex_driver_data
{
    PRIMEX,
};

enum initpa_states
{
    ABORT_PUT = 0,
    ABORT_GET,
    INIT_DONE,
};

enum enroll_start_pa_states
{
    ENROLL_CMD_SEND = 0,
    ENROLL_CMD_GET,
    ENROLL_UPDATE
};

enum verify_start_pa_states
{
    VERIFY_INIT = 0,
    VERIFY_UPDATE,
    VERIFY_FINAL
};

static const FpIdEntry id_table[] = {
    {.vid = 0x2F0A, .pid = 0x0201, .driver_data = PRIMEX},
    {.vid = 0, .pid = 0, .driver_data = 0},
};

typedef struct
{
    FpPrint *print;
    GError *error;
} EnrollStopData;


typedef void (*handle_get_fn)(FpDevice *dev,
                              unsigned char *data,
                              size_t data_len,
                              void *user_data,
                              GError *error);

struct prime_data
{
    gssize buflen;
    guint8 *buffer;
    handle_get_fn callback;
    void *user_data;
};

struct _FpiDevicePa_Primex
{
    FpDevice parent;

    gboolean enroll_passed;
    gint enroll_stage;
    gboolean first_verify_iteration;
    guint8 seq; /* FIXME: improve/automate seq handling */
};


static void
alloc_get_cmd_transfer(FpDevice *device,
                       handle_get_fn callback,
                       void *user_data);

static void
handle_response(FpDevice *device, FpiUsbTransfer *transfer, struct prime_data *udata);

static void
read_cb(FpiUsbTransfer *transfer, FpDevice *device,
        gpointer user_data, GError *error);

static void handle_enroll_iterate_cb(FpDevice *dev,
                                     unsigned char *data,
                                     size_t data_len,
                                     void *user_data,
                                     GError *error);
static void
enroll_iterate_cmd_cb(FpiUsbTransfer *transfer, FpDevice *device,
                      gpointer user_data, GError *error);

static void
enroll_iterate(FpDevice *dev);

static void
do_enroll_done(FpDevice *dev);

static void
delete(FpDevice *device);

static void
list(FpDevice *device);

