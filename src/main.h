#ifndef __MAIN_FILE_H__
#define __MAIN_FILE_H__

#include "datatypes.h"

void clean_stdin(void);
void printkmldata(folder_t*, int, u32, u32);
void printcoveragetable(folder_t*, int, init_t);
init_t initialization(void);

#endif
