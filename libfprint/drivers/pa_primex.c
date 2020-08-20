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

/*
 * PixelAuth storage index convert
 * Device only store increasing index, 00 ~ 09
 * List cmd will get such as 03 - 00 01 02  means 3 figners, index = 0/1/2
 * If index 1 deleted, list will return 02 - 00 02
 * When you enroll a new one at this situation, the list will be 03 - 00 01 02
 * The description in finger contains the index of from device
 * '//dev//[x]' x= 0-9
 * file pa-storage.variant contains other info such as username and finger name
 * get_dev_index fn handle the convert
 * Noted, when do the delete opt, the dev_index will be 0x01~0x0A since 00 means delete all
 */
#include "drivers_api.h"
#include "pa_primex.h"

struct _FpiDevicePa_Primex
{
    FpDevice parent;
    gint enroll_stage;
    GPtrArray *list_result;
};

pa_finger_list_t g_list;   //only use to figure enroll change out
pa_finger_list_t original; //only use to figure enroll change out

G_DECLARE_FINAL_TYPE(FpiDevicePa_Primex, fpi_device_pa_primex, FPI, DEVICE_PA_PRIME, FpDevice);
G_DEFINE_TYPE(FpiDevicePa_Primex, fpi_device_pa_primex, FP_TYPE_DEVICE);

static void p_print(unsigned char* buf, int len)
{
    g_print("buf len = %d \n", len);
    for(int i =0; i < len; i++)
    {
        g_print("0x%x ", buf[i]);
    }
    g_print("\n");
}

static void
fpi_device_pa_primex_init(FpiDevicePa_Primex *self)
{
    g_print("hello PA: fpi_device_pa_primex_init\n");
}
/*----------------------------Storage group------------------------------------*/
static char *
get_pa_data_descriptor(FpPrint *print, FpDevice *dev, FpFinger finger)
{
    const char *driver;
    const char *dev_id;
    const char *username;

    if (print)
    {
        driver = fp_print_get_driver(print);
        dev_id = fp_print_get_device_id(print);
    }
    else
    {
        driver = fp_device_get_driver(dev);
        dev_id = fp_device_get_device_id(dev);
    }
    //TODO not sure if we need username for descriptor
    username = g_get_user_name();

    return g_strdup_printf("%s/%s/%x",
                           driver,
                           dev_id,
                           finger);
}

static GVariantDict *
_load_data(void)
{
    GVariantDict *res;
    GVariant *var;
    gchar *contents = NULL;
    gsize length = 0;

    if (!g_file_get_contents(STORAGE_FILE, &contents, &length, NULL))
    {
        g_warning("Error loading storage, assuming it is empty");
        return g_variant_dict_new(NULL);
    }

    var = g_variant_new_from_data(G_VARIANT_TYPE_VARDICT,
                                  contents,
                                  length,
                                  FALSE,
                                  g_free,
                                  contents);

    res = g_variant_dict_new(var);
    g_variant_unref(var);
    return res;
}

static int
_save_data(GVariant *data)
{
    const gchar *contents = NULL;
    gsize length;

    length = g_variant_get_size(data);
    contents = (gchar *)g_variant_get_data(data);

    if (!g_file_set_contents(STORAGE_FILE, contents, length, NULL))
    {
        g_warning("Error saving storage,!");
        return -1;
    }

    g_variant_ref_sink(data);
    g_variant_unref(data);

    return 0;
}

FpPrint *
pa_data_load(FpDevice *dev, FpFinger finger)
{
    g_autofree gchar *descr = get_pa_data_descriptor(NULL, dev, finger);

    g_autoptr(GVariant) val = NULL;
    g_autoptr(GVariantDict) dict = NULL;
    const guchar *stored_data = NULL;
    gsize stored_len;

    dict = _load_data();
    val = g_variant_dict_lookup_value(dict, descr, G_VARIANT_TYPE("ay"));

    if (val)
    {
        FpPrint *print;
        g_autoptr(GError) error = NULL;

        stored_data = (const guchar *)g_variant_get_fixed_array(val, &stored_len, 1);
        print = fp_print_deserialize(stored_data, stored_len, &error);

        if (error)
            g_warning("Error deserializing data: %s", error->message);

        return print;
    }

    return NULL;
}

int pa_data_save(FpPrint *print, FpFinger finger)
{
    g_autofree gchar *descr = get_pa_data_descriptor(print, NULL, finger);

    g_autoptr(GError) error = NULL;
    g_autoptr(GVariantDict) dict = NULL;
    g_autofree guchar *data = NULL;
    GVariant *val;
    gsize size;
    int res;

    dict = _load_data();

    fp_print_serialize(print, &data, &size, &error);
    g_print("hello PA: fp_print_serialize %d \n", size);
    if (error)
    {
        g_warning("Error serializing data: %s", error->message);
        return -1;
    }
    val = g_variant_new_fixed_array(G_VARIANT_TYPE("y"), data, size, 1);
    g_variant_dict_insert_value(dict, descr, val);

    res = _save_data(g_variant_dict_end(dict));

    return res;
}

int pa_data_del(FpDevice *dev, FpFinger finger)
{
    g_autofree gchar *descr = get_pa_data_descriptor(NULL, dev, finger);

    g_autoptr(GVariant) val = NULL;
    g_autoptr(GVariantDict) dict = NULL;

    dict = _load_data();
    val = g_variant_dict_lookup_value(dict, descr, G_VARIANT_TYPE("ay"));

    if (val)
    {
        g_variant_dict_remove(dict, descr);
        _save_data(g_variant_dict_end(dict));

        return PA_OK;
    }

    return PA_ERROR;
}

int get_dev_index(FpDevice *dev, FpPrint *print)
{
    FpPrint *enroll_print = pa_data_load(dev, fp_print_get_finger(print));
    char *dev_str = fp_print_get_description(enroll_print);
    g_print("get_dev_index %s \n", dev_str);
    int dev_index = dev_str[6] - 0x30; // /dev//[x]
    return dev_index;
}
/* -----------------------------USB layer group -----------------------------------*/
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
    //if(ins!=PA_CMD_FPSTATE)
        p_print(transfer->buffer,real_len);
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
    
    if (transfer->actual_length < PA_HEADER_LEN + PA_LEN_LEN + PA_SW_LEN)
    {
        g_print("hello PA: read_cb len = %ld\n", transfer->actual_length);
        g_print("hello PA: error %s\n", error->message);
        return;
    }
    p_print(transfer->buffer,transfer->actual_length);
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
    if (data[7 + len - 2] == 0x6a && data[8 + len - 2] == 0x86)
        return PA_P1P2;

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
    memset(g_list.finger_map,0xff,PA_MAX_FINGER_COUNT);
    memset(original.finger_map,0xff,PA_MAX_FINGER_COUNT);
    g_list.total_number = 0;
    original.total_number = 0;
    g_list.modified_by = 0;
    original.modified_by = 0;

    /* do_init state machine first */
    FpiSsm *ssm = fpi_ssm_new(dev, enroll_start_pa_run_state,
                              ENROLL_UPDATE);

    padev->enroll_stage = 0;
    fpi_ssm_start(ssm, enroll_started);
}

static void
enroll_start_pa_run_state(FpiSsm *ssm, FpDevice *dev)
{
    g_print("hello PA: enroll_start_pa_run_state %d\n", fpi_ssm_get_cur_state(ssm));
    switch (fpi_ssm_get_cur_state(ssm))
    {
    case ENROLL_LIST_BEFORE_SEND:
        g_print("hello PA: ENROLL_LIST_BEFORE_SEND\n");
        alloc_send_cmd_transfer(dev, ssm, PA_CMD_LIST, 0x80, 0, NULL, 0);
        break;
    case ENROLL_LIST_BEFORE_GET:
        g_print("hello PA: ENROLL_LIST_BEFORE_GET\n");
        g_list.modified_by = ENROLL_LIST_BEFORE_GET;
        alloc_get_cmd_transfer(dev, handle_get_list, ssm);
        break;
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
    //g_print("hello PA: enroll_iterate\n");
    alloc_send_cmd_transfer(dev, NULL, PA_CMD_FPSTATE, 0, 0, NULL, 1);
}

static void
enroll_iterate_cmd_cb(FpiUsbTransfer *transfer, FpDevice *device,
                      gpointer user_data, GError *error)
{
    //g_print("hello PA: enroll_iterate_cmd_cb\n");
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
    //g_print("hello PA: handle_enroll_iterate_cb %d \n", padev->enroll_stage);
    int result = get_sw(data, data_len);
    if (result == PA_OK)
    {
        get_data(data, data_len, &code);
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
    if(padev->enroll_stage<16)
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
    //backup original list
    original.total_number = g_list.total_number;
    memcpy(original.finger_map, g_list.finger_map,PA_MAX_FINGER_COUNT);
    original.modified_by = g_list.modified_by;
    //start ssm to get list
    FpiSsm *ssm = fpi_ssm_new(dev, enroll_finish_pa_run_state,
                              ENROLL_DONE);

    fpi_ssm_start(ssm, enroll_save);

    
}
static void
enroll_save(FpiSsm *ssm, FpDevice *dev, GError *error)
{
    FpPrint *print = NULL;
    fpi_device_get_enroll_data(dev, &print);
    int dev_new_index = -1;
    g_print("hello PA:enroll done finger %d \n", fp_print_get_finger(print));
    //TODO compare two lists to device which is new dev_index and
    if(g_list.total_number- original.total_number!=1)//not enroll 1 finger
        return;
    for(int i=0; i<PA_MAX_FINGER_COUNT; i++)
    {
        if(g_list.finger_map[i]!=original.finger_map[i])
        {
            dev_new_index = g_list.finger_map[i];
            break;
        }
            
    }
    gen_finger(dev, dev_new_index, print);
    pa_data_save(print, fp_print_get_finger(print));
    fpi_device_enroll_complete(dev, print, error);
}

static void
enroll_finish_pa_run_state(FpiSsm *ssm, FpDevice *dev)
{
    g_print("hello PA: enroll_finish_pa_run_state %d\n", fpi_ssm_get_cur_state(ssm));
    switch (fpi_ssm_get_cur_state(ssm))
    {
    case ENROLL_LIST_AFTER_SEND:
        g_print("hello PA: ENROLL_LIST_AFTER_SEND\n");
        alloc_send_cmd_transfer(dev, ssm, PA_CMD_LIST, 0x80, 0, NULL, 0);
        break;
    case ENROLL_LIST_AFTER_GET:
        g_print("hello PA: ENROLL_LIST_AFTER_GET\n");
        g_list.modified_by = ENROLL_LIST_AFTER_GET+0x20;
        alloc_get_cmd_transfer(dev, handle_get_list, ssm);
        break;
    default:
        g_print("hello PA: enroll_finish_pa_run_state DEFAULT %d\n", fpi_ssm_get_cur_state(ssm));
        break;
    }
}

static void
gen_finger(FpDevice *device, int dev_index, FpPrint *print)
{
    GVariant *data = NULL;
    GVariant *uid = NULL;
    guint finger;
    g_autofree gchar *user_id = fpi_print_generate_user_id(print);
    gssize user_id_len = strlen(user_id);
    /*dummmy data must be exist*/
    finger = fp_print_get_finger(print); /* this one doesnt count*/
    uid = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
                                    user_id,
                                    user_id_len,
                                    1);
    data = g_variant_new("(y@ay)", finger, uid);
    fpi_print_set_type(print, FPI_PRINT_RAW);
    fpi_print_set_device_stored(print, TRUE);
    g_object_set(print, "fpi-data", data, NULL);
    /*the followings are useful*/
    fp_print_set_username(print, g_get_user_name());
    g_print("hello PA: gen_fginer username %s \n", g_get_user_name());
    g_object_set(print, "description", g_strdup_printf("%s/%d", pa_description, dev_index), NULL);
    fpi_print_fill_from_user_id(print, user_id);
}

/* -----------------------------Verify group -----------------------------------*/
static void
verify(FpDevice *dev)
{
    g_print("hello PA: verify\n");
    FpiSsm *ssm = fpi_ssm_new(dev, verify_start_pa_run_state, VERIFY_FINAL);
    fpi_ssm_start(ssm, verify_started);
}

static void
verify_start_pa_run_state(FpiSsm *ssm, FpDevice *dev)
{
    g_print("hello PA: verify_start_pa_run_state %d\n", fpi_ssm_get_cur_state(ssm));
    switch (fpi_ssm_get_cur_state(ssm))
    {
    case VERIFY_CMD_SEND:
        g_print("hello PA: VERIFY_INIT\n");
        fpi_ssm_next_state(ssm);
        break;
    case VERIFY_CMD_GET:
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
list(FpDevice *device)
{  
    FpiSsm *ssm = fpi_ssm_new(device, list_pa_run_state,LIST_DONE);
    fpi_ssm_start(ssm, list_done);  
}
static void
list_done(FpiSsm *ssm, FpDevice *device, GError *error)
{
    FpiDevicePa_Primex *padev = FPI_DEVICE_PA_PRIME(device);
    padev->list_result = g_ptr_array_new_with_free_func(g_object_unref);
    for (int i = 1; i < PA_MAX_FINGER_COUNT + 1; i++)
    {
        FpPrint *back = pa_data_load(device, i);
        if (!back)
            continue;

        FpPrint *print = fp_print_new(FP_DEVICE(device));
        fpi_print_set_type(print, FPI_PRINT_RAW);
        fpi_print_set_device_stored(print, TRUE);

        g_print("hello PA: username %s finger %d\n", fp_print_get_username(back), fp_print_get_finger(back));
        fp_print_set_username(print, fp_print_get_username(back));
        fp_print_set_finger(print, fp_print_get_finger(back));
        g_object_set(print, "description", fp_print_get_description(back), NULL);
        g_ptr_array_add(padev->list_result, g_object_ref_sink(print));
    }

    fpi_device_list_complete(FP_DEVICE(device),
                             g_steal_pointer(&padev->list_result),
                             NULL);
}
static void
list_pa_run_state(FpiSsm *ssm, FpDevice *dev)
{
    g_print("hello PA: enroll_finish_pa_run_state %d\n", fpi_ssm_get_cur_state(ssm));
    switch (fpi_ssm_get_cur_state(ssm))
    {
    case LIST_SEND:
        g_print("hello PA: ENROLL_LIST_AFTER_SEND\n");
        alloc_send_cmd_transfer(dev, ssm, PA_CMD_LIST, 0x80, 0, NULL, 0);
        break;
    case LIST_GET:
        g_print("hello PA: ENROLL_LIST_AFTER_GET\n");
        g_list.modified_by = LIST_GET+0x40;
        alloc_get_cmd_transfer(dev, handle_get_list, ssm);
        break;
    default:
        g_print("hello PA: enroll_finish_pa_run_state DEFAULT %d\n", fpi_ssm_get_cur_state(ssm));
        break;
    }
}


static void handle_get_list(FpDevice *dev,
                            unsigned char *data,
                            size_t data_len,
                            void *user_data,
                            GError *error)
{
    FpiSsm *ssm = user_data;

    g_print("hello PA:handle_get_list %ld\n", data_len);
    p_print(data,data_len);
    int result = get_sw(data, data_len);
    if (result != PA_OK)
    {
        //TODO: error handle
    }
    else
    {
        g_list.total_number = get_data(data, data_len, g_list.finger_map);
        g_list.modified_by = g_list.modified_by + 0x80;
        g_print("hello PA: handle_get_list number %d  by %d\n", g_list.total_number, g_list.modified_by);
        g_print("hello PA: g_list ");
        for(int i=0;i<PA_MAX_FINGER_COUNT; i++)
        {
                g_print("%d ", g_list.finger_map[i]);
        }
         g_print("\n");
        fpi_ssm_next_state(ssm);
    }
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
        FpPrint *print = NULL;
        fpi_device_get_delete_data(dev, &print);
        int dev_index = get_dev_index(dev, print);
        g_print("hello PA: delete_cmd_state DELETE_SEND dev_index %d \n", dev_index);
        alloc_send_cmd_transfer(dev, ssm, PA_CMD_DELETE, dev_index + 1, 0, (unsigned char *)str_delete, strlen(str_delete));
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
    g_print("hello PA:handle_get_delete %ld\n", data_len);
    int result = get_sw(data, data_len);
    if (result == PA_OK || result == PA_FPM_REFDATA)
        fpi_ssm_next_state(ssm);
    else
    {
        //TODO: error handle
    }
}

static void
delete_done(FpiSsm *ssm, FpDevice *dev, GError *error)
{
    FpPrint *print = NULL;
    fpi_device_get_delete_data(dev, &print);
    pa_data_del(dev, fp_print_get_finger(print));
    fpi_device_delete_complete(dev, NULL);
}

static void
cancel (FpDevice *dev)
{
  //FpiDevicePa_Primex *self = FPI_DEVICE_PA_PRIME(device);

//   /* We just send out a cancel command and hope for the best. */
//   synaptics_sensor_cmd (self, -1, BMKT_CMD_CANCEL_OP, NULL, 0, NULL);

  /* Cancel any current interrupt transfer (resulting us to go into
   * response reading mode again); then create a new cancellable
   * for the next transfers. */
//   g_cancellable_cancel (self->interrupt_cancellable);
//   g_clear_object (&self->interrupt_cancellable);
//   self->interrupt_cancellable = g_cancellable_new ();
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
    dev_class->cancel = cancel;
}
