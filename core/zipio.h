#ifndef ADOFAI_ZIPIO_H
#define ADOFAI_ZIPIO_H

#include <stdio.h>

#define ZIP_MAX_NAME 512

typedef struct zip_file zip_file;

zip_file *zip_open(const char *path);
void      zip_close(zip_file *z);
int       zip_read_first(zip_file *z, char *name_out, size_t name_cap);
int       zip_read_next (zip_file *z, char *name_out, size_t name_cap);
int       zip_extract_current(zip_file *z, const char *dest_dir);

#endif /* ADOFAI_ZIPIO_H */
