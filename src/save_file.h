#ifndef __SAVE_FILE_H__
#define __SAVE_FILE_H__

#include "datatypes.h"

void savecsv(char*, folder_t*, int, int);
void savekml(char*, folder_t*, int);
void savekml_test(char*, folder_t*, int);
void savecsvcovchart(char*, folder_t*, int);
void savecsvcovtab(char*, folder_t*, int, init_t);
void set_output_dirs(const char* csv_dir, const char* kml_dir);

#endif
