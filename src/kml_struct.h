#ifndef __KML_STRUCT_H__
#define __KML_STRUCT_H__

#include "datatypes.h"

double getmedian(double*, int);
int getfolders(folder_t *, filedata_t);
int gpsfolderindex(folder_t*, int);
placemark_t getplacemarkdata(u32, u32, filedata_t);
void getkmldata(folder_t*, int, filedata_t);
void foldermemfree(folder_t*, int);
void getaverlvl(folder_t*, int, int, AVG_TYPE);
double distearth(double, double, double, double);
void createcovtab(folder_t*, int, init_t);

int covercheck(folder_t*, u32, init_t, u32, u32);

#endif