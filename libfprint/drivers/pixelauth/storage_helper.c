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
 * Pixelauth storage index convert
 * Device only store increasing index, 00 ~ 09
 * List cmd will get such as 03 - 00 01 02  means 3 figners, index = 0/1/2
 * If index 1 deleted, list will return 02 - 00 02
 * When you enroll a new one at this situation, the list will be 03 - 00 01 02
 * The description in finger contains the index of from device
 * '//dev//[x]' x= 0-9
 * file pa-storage.variant contains other info such as username and finger name
 * file pa-storage.variant's key index is made by dev index
 * finger index could be get from data inside
 * get_dev_index fn handle the convert
 * Noted, when do the delete opt, the dev_index will be 0x01~0x0A since 00 means delete all
 */
#include <glib.h>
#include "drivers_api.h"
#include "pixelauth.h"
#include "storage_helper.h"

const gchar *pa_description = "/dev/";

gchar *
get_pa_data_descriptor(FpPrint *print,
                       FpDevice *self,
                       gint dev_index)
{
    const gchar *driver;
    const gchar *dev_id;

    if (print)
    {
        driver = fp_print_get_driver(print);
        dev_id = fp_print_get_device_id(print);
    }
    else
    {
        driver = fp_device_get_driver(self);
        dev_id = fp_device_get_device_id(self);
    }

    return g_strdup_printf("%s/%s/%x",
                           driver,
                           dev_id,
                           dev_index);
}

GVariantDict *
_load_data(void)
{
    GVariantDict *res;
    GVariant *var;
    gchar *contents = NULL;
    gsize length = 0;
    GError *err = NULL;

    if (!g_file_get_contents(STORAGE_FILE, &contents, &length, &err))
    {
        g_warning("Error loading storage, assuming it is empty, message:%s\n", err->message);
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

gint
_save_data(GVariant *data)
{
    const gchar *contents = NULL;
    gsize length;
    GError *err = NULL;

    length = g_variant_get_size(data);
    contents = (gchar *)g_variant_get_data(data);

    if (!g_file_set_contents(STORAGE_FILE, contents, length, &err))
    {
        g_warning("Error saving storage, message:%s\n!", err->message);
        return -1;
    }

    g_variant_ref_sink(data);
    g_variant_unref(data);

    return 0;
}

FpPrint *
pa_data_load(FpDevice *self,
             FpFinger finger,
             const gchar *username,
             gint db_count)
{
    g_autofree gchar *descr = NULL;
    g_autoptr(GVariant) val = NULL;
    g_autoptr(GVariantDict) dict = NULL;
    const guchar *stored_data = NULL;
    gsize stored_len;

    dict = _load_data();
    for (int i = 0; i < db_count; i++)
    {
        descr = get_pa_data_descriptor(NULL, self, i);
        val = g_variant_dict_lookup_value(dict, descr, G_VARIANT_TYPE("ay"));
        if (val)
        {
            FpPrint *print;
            g_autoptr(GError) error = NULL;

            stored_data = (const guchar *)g_variant_get_fixed_array(val, &stored_len, 1);
            print = fp_print_deserialize(stored_data, stored_len, &error);

            if (error)
                g_warning("Error deserializing data: %s", error->message);

            if (fp_print_get_finger(print) != finger)
                continue;

            if (username)
            {
                if (g_strcmp0(fp_print_get_username(print), username) == 0)
                    return print;
                else
                    return NULL;
            }
            return print;
        }
    }

    return NULL;
}

gint pa_data_save(FpPrint *print, gint dev_index)
{
    g_autofree gchar *descr = get_pa_data_descriptor(print, NULL, dev_index);

    g_autoptr(GError) error = NULL;
    g_autoptr(GVariantDict) dict = NULL;
    g_autofree guchar *data = NULL;
    GVariant *val;
    gsize size;
    gint res;

    dict = _load_data();

    fp_print_serialize(print, &data, &size, &error);
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

gint pa_data_del(FpDevice *self,
                 FpPrint *print,
                 const gchar *username,
                 gint db_count)
{
    g_autofree gchar *descr = NULL;

    g_autoptr(GVariant) val = NULL;
    g_autoptr(GVariantDict) dict = NULL;
    const guchar *stored_data = NULL;
    gsize stored_len;

    if (!username)
        return PA_OK;

    dict = _load_data();
    for (int i = 0; i < db_count; i++)
    {
        descr = get_pa_data_descriptor(print, self, i);
        val = g_variant_dict_lookup_value(dict, descr, G_VARIANT_TYPE("ay"));

        if (val)
        {
            FpPrint *print_got;
            g_autoptr(GError) error = NULL;

            stored_data = (const guchar *)g_variant_get_fixed_array(val, &stored_len, 1);
            print_got = fp_print_deserialize(stored_data, stored_len, &error);

            if (error)
                g_warning("Error deserializing data: %s", error->message);

            if (fp_print_get_finger(print) != fp_print_get_finger(print_got))
                continue;

            if (g_strcmp0(fp_print_get_username(print_got), username) == 0)
            {
                g_variant_dict_remove(dict, descr);
                _save_data(g_variant_dict_end(dict));
                return PA_OK;
            }
        }
    }

    return PA_ERROR;
}

gint get_dev_index(FpDevice *self, FpPrint *print,gint db_count)
{
    FpPrint *enroll_print = NULL;
    enroll_print = pa_data_load(self, fp_print_get_finger(print), fp_print_get_username(print),db_count);
    if (enroll_print == NULL)
        return PA_ERROR;
    const gchar *dev_str = fp_print_get_description(enroll_print);
    fp_info("get_dev_index %s \n", dev_str);
    gint dev_index = dev_str[6] - 0x30; // /dev//[x]
    return dev_index;
}

void
gen_finger(gint dev_index, FpPrint *print)
{
    GVariant *data = NULL;
    GVariant *uid = NULL;
    g_autoptr(GDate) date = NULL;
    guint finger;
    g_autofree gchar *user_id = fpi_print_generate_user_id(print);
    gssize user_id_len = strlen(user_id);

    finger = fp_print_get_finger(print);
    uid = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
                                    user_id,
                                    user_id_len,
                                    1);
    data = g_variant_new("(y@ay)", finger, uid);
    fpi_print_set_type(print, FPI_PRINT_RAW);
    fpi_print_set_device_stored(print, TRUE);
    g_object_set(print, "fpi-data", data, NULL);
    date = g_date_new();
    fp_print_set_enroll_date(print, date);

    g_object_set(print, "description", g_strdup_printf("%s/%d", pa_description, dev_index), NULL);
}
