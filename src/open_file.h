#ifndef __OPEN_FILE_H__
#define __OPEN_FILE_H__

#include "datatypes.h"

int openkml(char*, folder_t*);
int opencsv(char*, folder_t*);
void fdatamemfree(filedata_t);
void set_csv_input_dir(const char* dir);

#endif
