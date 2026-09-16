#ifndef SD_STORAGE_H
#define SD_STORAGE_H

#include <stdbool.h>

void sd_storage_init(void);

bool sd_storage_create_event(
    unsigned int event_id
);

bool sd_storage_save_photo(
    unsigned int event_id,
    unsigned int photo_number,
    const unsigned char *data,
    unsigned int size
);

bool sd_storage_save_metadata(
    unsigned int event_id,
    const char *metadata
);

bool sd_storage_has_pending(void);

#endif