#ifndef __TYPES_N_STRUCTS__
#define __TYPES_N_STRUCTS__

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

/* Size of the folder array declared in main(). One slot on top of the data folders is
   reserved: opencsv() and createcovtab() use folder[folder_quantity] for the averaged
   coverage data, so no more than (MAX_FOLDERS - 1) data folders may be read from a file. */
#define MAX_FOLDERS 20

#ifdef GUI_BUILD
/* The gui build reuses the converter functions as they are.  They print with printf() and
   stop the program with exit() from deep inside the parsers, which a window can not afford:
   the output goes to the log pane and a fatal error unwinds back to the worker thread
   instead of killing the process.  The "press a key" pauses disappear together with the
   console.  The console build is not affected - none of this is defined without GUI_BUILD. */
void app_log(const char* format, ...);
void app_perror(const char* prefix);
void app_abort(int code);

#define printf    app_log
#define perror    app_perror
#define exit(code) app_abort(code)
#define getchar()  (0)
#endif



typedef struct {
    int* string_length;
    u32 strings;
    char** farray;
} filedata_t;

typedef struct {
    char        name[400];
    int         measnumber;
    int         timestamp;
    double      signallevel;
    double      latitude;
    double      longitude;
    double      altitude;
    char        color[10];
    double      averagelevel;
    int         coverfl;
    double      distfromstart;
    int         hasdata;       /* 0 - the point has no measured level (an empty cell in csv) */
} placemark_t;

typedef struct {
    int        coverfl;
    double     startlat;
    double     startlong;
    double     endlat;
    double     endlong;
    double     distance;
    u32        startnumber;
    u32        endnumber;
} covreg_t;

typedef struct {
    int           number;
    u32           startstr;
    u32           endstr;
    char          name[400];
    char          short_name[400];
    u32           placemark_quantity;
    u32           firstplacemark_startstr;
    u32*          placemark_startstr;
    u32*          placemark_endstr;
    placemark_t*  placemark_arr;
    covreg_t*     covreg;
    double        threshold;
    u32           reg_quantity;
    double        totdist;
    double        uncovtotdist;
    double        covtotdist;
} folder_t;

typedef struct {
    u32        string;
    char*      pos_ptr;
} posptr_t;

typedef enum {
	separate,
	total,
}COVER_TYPE;

typedef enum {
	sma,
	median,
}AVG_TYPE;

typedef struct{
int          fillinflag;
double       GSMcoveragelvl;
double       UMTScoveragelvl;
double       LTEcoveragelvl;
double       defaultcovlvl;
COVER_TYPE   covercalctype;
AVG_TYPE     avgtype;
int          avgdepth;
double       maxskipdist;
} init_t;

#endif