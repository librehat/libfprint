#pragma once

#define STORAGE_FILE "/usr/lib/fprintd/pa-storage.variant"

gchar *
get_pa_data_descriptor(FpPrint *print,
                       FpDevice *self,
                       gint dev_index);

GVariantDict *
_load_data(void);

gint
_save_data(GVariant *data);

FpPrint *pa_data_load(FpDevice *self,
                      FpFinger finger,
                      const gchar *username,
                      gint db_count);

gint pa_data_save(FpPrint *print, gint dev_index);

gint pa_data_del(FpDevice *self,
                 FpPrint *print,
                 const gchar *username,
                 gint db_count);

gint get_dev_index(FpDevice *self, FpPrint *print, gint db_count);

void
gen_finger(gint dev_index, FpPrint *print);
