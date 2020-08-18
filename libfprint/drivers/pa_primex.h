
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

#define PA_MAX_FINGER_COUNT 10

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

#define STORAGE_FILE "pa-storage.variant"

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

enum delete_cmd_states
{
    DELETE_SEND = 0,
    DELETE_GET,
    DELETE_DONE
};

enum list_cmd_states
{
    LIST_SEND = 0,
    LIST_GET,
    LIST_DONE
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

typedef struct pa_finger_list
{
  int                total_number;                                   /**< Total query response messages */
  unsigned char      finger_map[PA_MAX_FINGER_COUNT];
} pa_finger_list_t;

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

const char* pa_description = "PixelAuth inside";

/*USB layer group*/
static void
alloc_send_cmd_transfer(FpDevice *dev,
                        FpiSsm *ssm,
                        unsigned char ins,
                        unsigned char p1,
                        unsigned char p2,
                        const unsigned char *data,
                        guint16 len);
static void alloc_get_cmd_transfer(FpDevice *device, handle_get_fn callback, void *user_data);
static void read_cb(FpiUsbTransfer *transfer, FpDevice *device, gpointer user_data, GError *error);
static void handle_response(FpDevice *device, FpiUsbTransfer *transfer, struct prime_data *udata);
static int get_sw(unsigned char *data, size_t data_len);
static int get_data(unsigned char *data, size_t data_len, unsigned char *buf);
/* Init group*/
static void dev_init(FpDevice *dev);
static void initpa_run_state(FpiSsm *ssm, FpDevice *dev);
static void handle_get_abort(FpDevice *dev,
                             unsigned char *data,
                             size_t data_len,
                             void *user_data,
                             GError *error);
static void initpa_done(FpiSsm *ssm, FpDevice *dev, GError *error);
/*Deinit group*/
static void dev_exit(FpDevice *dev);
/*Enroll group*/
static void enroll(FpDevice *dev);
static void enroll_start_pa_run_state(FpiSsm *ssm, FpDevice *dev);
static void handle_get_enroll(FpDevice *dev,
                              unsigned char *data,
                              size_t data_len,
                              void *user_data,
                              GError *error);
static void enroll_iterate(FpDevice *dev);
static void
enroll_iterate_cmd_cb(FpiUsbTransfer *transfer,
                      FpDevice *device,
                      gpointer user_data,
                      GError *error);
static void
handle_enroll_iterate_cb(FpDevice *dev,
                         unsigned char *data,
                         size_t data_len,
                         void *user_data,
                         GError *error);
static void enroll_started(FpiSsm *ssm, FpDevice *dev, GError *error);
static void do_enroll_done(FpDevice *dev);
static void save_finger(FpDevice *device, FpPrint *print);

/*Verify group*/
static void verify(FpDevice *dev);
static void verify_start_pa_run_state(FpiSsm *ssm, FpDevice *dev);
static void verify_iterate(FpDevice *dev);
static void verify_started(FpiSsm *ssm, FpDevice *dev, GError *error);
/*List group*/
static void list(FpDevice *device);

/*Delete group*/
static void delete (FpDevice *device);
static void delete_cmd_state(FpiSsm *ssm, FpDevice *dev);
static void handle_get_delete(FpDevice *dev,
                              unsigned char *data,
                              size_t data_len,
                              void *user_data,
                              GError *error);
static void delete_done(FpiSsm *ssm, FpDevice *dev, GError *error);