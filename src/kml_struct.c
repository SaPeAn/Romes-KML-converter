/*
 *               The functions of forming and processing the structure of kml data obtained from a kml file.
 * 
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "kml_struct.h"

extern void clean_stdin(void);

#define    FOLDER         0
#define    ENDFOLDER      1
#define    PLACEMARK      2
#define    ENDPLACEMARK   3

/* M_PI is not visible with -std=c99 (__STRICT_ANSI__), so the constant is spelled out.
   The old value 3.14159 shortened every distance by about 0.9 m per 1000 km. */
#define PI_VALUE       3.14159265358979323846
#define EARTH_RADIUS   6371.032

/*----Comparison for qsort(); the median only needs the array ordered.----*/
static int compare_double(const void* left, const void* right)
{
    double a = *(const double*)left;
    double b = *(const double*)right;
    if(a < b) return -1;
    if(a > b) return  1;
    return 0;
}

double getmedian(double *arr, int n)
{
    if(n <= 0) return 0.0;
    qsort(arr, (size_t)n, sizeof(double), compare_double);   /* was a selection sort, O(n^2) */
    if((n % 2) == 0)
        return 0.5 * (arr[n/2] + arr[n/2-1]);
    else
        return arr[n/2];
}

double distearth(double lat1d, double lon1d, double lat2d, double lon2d)
{
    double lat1r, lon1r, lat2r, lon2r, u, v;
    lat1r = (lat1d * PI_VALUE) / 180;
    lon1r = (lon1d * PI_VALUE) / 180;
    lat2r = (lat2d * PI_VALUE) / 180;
    lon2r = (lon2d * PI_VALUE) / 180;
    u = sin((lat2r - lat1r)/2);
    v = sin((lon2r - lon1r)/2);
    return (2.0 * EARTH_RADIUS * asin(sqrt(u * u + cos(lat1r) * cos(lat2r) * v * v)));
}

/*----Copies the text that follows an opening tag.  When the closing tag sits on the same
     line the text is cut there, otherwise the rest of the line is taken.  The length is
     always clamped to the buffer: a long <name> or <coordinates> line used to be copied
     into a 150 byte array and the terminator written past its end.----*/
static void copy_tag_text(const char* text, const char* text_end, char* buffer, size_t size)
{
    size_t length;

    if(text_end != NULL)
    {
        if(text_end <= text) { buffer[0] = '\0'; return; }
        length = (size_t)(text_end - text);
    }
    else
    {
        length = strlen(text);
    }
    if(length >= size) length = size - 1;
    memcpy(buffer, text, length);
    buffer[length] = '\0';
}

/*-----------------------The function generates an array of folders containing placemarks---------------------*/
int getfolders(folder_t * folder, filedata_t fdata)
{
    int fol_index = 0;
    int prev_tag = FOLDER;
    for(u32 i = 0; i < fdata.strings; i++)
    {
        if(strstr(fdata.farray[i], "<Folder>") != NULL)
        {
            if(fol_index >= (MAX_FOLDERS - 1))  // the folder array of main() has no room for one more folder
            {
                printf("\nError! The kml file contains more than %d folders with placemarks!\n", (MAX_FOLDERS - 1));
                clean_stdin();
                getchar();
                exit(1);
            }
            if(prev_tag == FOLDER)
            {
                folder[fol_index].startstr = i;
                prev_tag = FOLDER;
            }
            if(prev_tag == ENDFOLDER)
            {
                folder[fol_index].startstr = i;
                prev_tag = FOLDER;
            }
        }
        if(strstr(fdata.farray[i], "</Folder>") != NULL)
        {
            if(prev_tag == PLACEMARK)
            {
                folder[fol_index].endstr = i;
                fol_index++;
                prev_tag = ENDFOLDER;
            }
        }

        if(strstr(fdata.farray[i], "<Placemark>") != NULL)
        {
            if(prev_tag == FOLDER)
            {
                folder[fol_index].firstplacemark_startstr = i;
                prev_tag = PLACEMARK;
            }
            if(prev_tag == PLACEMARK)
            {
                folder[fol_index].placemark_quantity++;
                prev_tag = PLACEMARK;
            }
        }
    }
/*---------Getting the start and end line numbers for each placemark in each folder------------------*/
    for(int i = 0; i < fol_index; i++)
    {
        folder[i].placemark_startstr = malloc(sizeof(u32)*folder[i].placemark_quantity);
        folder[i].placemark_endstr = malloc(sizeof(u32)*folder[i].placemark_quantity);
        int plm_index = 0;
        for(u32 j = folder[i].startstr; j < folder[i].endstr; j++)
        {
            if(strstr(fdata.farray[j], "<Placemark>"))
            {
                folder[i].placemark_startstr[plm_index] = j;      
            }
            if(strstr(fdata.farray[j], "</Placemark>"))
            {
                folder[i].placemark_endstr[plm_index] = j; 
                plm_index++;     
            }
        }
    }


/*-------------------------------------------Reading folder names------------------------------------------*/
    for(int i = 0; i < fol_index; i++)
    {
        char* start_ptr = NULL;      /* these used to be reused between folders: a folder */
        char* end_ptr   = NULL;      /* without a <name> silently took the previous one    */
        u32   start_string = 0;
        u32   end_string   = 0;
        char* temp_ptr;

        for(u32 j = folder[i].startstr; j < folder[i].firstplacemark_startstr; j++)
        {
            if((temp_ptr = strstr(fdata.farray[j], "<name>")) != NULL)  { start_ptr = temp_ptr + 6; start_string = j; }
            if((temp_ptr = strstr(fdata.farray[j], "</name>")) != NULL) { end_ptr = temp_ptr; end_string = j; }
        }
        if(start_ptr == NULL) continue;                       /* no name: leave it empty */
        copy_tag_text(start_ptr, ((end_ptr != NULL) && (start_string == end_string)) ? end_ptr : NULL,
                      folder[i].name, sizeof(folder[i].name));
    }
    return fol_index;    // returns the number of folders found in the file (containing placemarks)
}



/*--------------------The function fills in the structure of a single placemark from the file data---------------------------*/


placemark_t getplacemarkdata(u32 placemark_startstring, u32 placemark_endstring, filedata_t fdata)
{
    placemark_t placemark;
    char  temp_arr[400];
    char* temp_ptr;
    char* start_ptr;
    char* end_ptr;
    u32   start_string;
    u32   end_string;
    int   coordfl = 0;    // if "Snippet" with a capital letter

    memset(&placemark, 0, sizeof(placemark));   /* fields that fail to parse must not hold garbage */
    /* hasdata stays 0 until a signal level is actually parsed */

    /*------------------Parsing the name field-------------------------------*/
    start_ptr = NULL; end_ptr = NULL; start_string = 0; end_string = 0;
    for(u32 i = placemark_startstring; i < placemark_endstring; i++)
    {
        if((temp_ptr = strstr(fdata.farray[i], "<name>")) != NULL)  { start_ptr = temp_ptr + 6; start_string = i; }
        if((temp_ptr = strstr(fdata.farray[i], "</name>")) != NULL) { end_ptr = temp_ptr; end_string = i; }
    }
    if(start_ptr != NULL)
    {
        const char* name_text = temp_arr;
        copy_tag_text(start_ptr, ((end_ptr != NULL) && (start_string == end_string)) ? end_ptr : NULL,
                      temp_arr, sizeof(temp_arr));

        temp_ptr = strstr(temp_arr, " Measurement #");       // getting measurement number
        if(temp_ptr != NULL)                                 // a placemark without it is not a measurement
        {
            placemark.measnumber = atoi(temp_ptr + 14);
            *temp_ptr = '\0';
        }
        if(strncmp(name_text, "R&amp;S ", 8) == 0) name_text += 8;  // "R&S " as it is written in kml
        strncpy(placemark.name, name_text, sizeof(placemark.name) - 1);
        placemark.name[sizeof(placemark.name) - 1] = '\0';
    }

    /*----------------------Parsing the snippet field---------------------*/
    //The KML specification and associated XML Schema clearly show the element "Snippet" form deprecated and "snippet" as the approved form to use.
    //KML Reference errata: https://kml4earth.appspot.com/kmlErrata.html#snippet)
    //In Romes exportKML "Snippet" form used in the folder "GPS Data"
    start_ptr = NULL; end_ptr = NULL; start_string = 0; end_string = 0;
    for(u32 i = placemark_startstring; i < placemark_endstring; i++)
    {
        if((temp_ptr = strstr(fdata.farray[i], "<snippet>")) != NULL)  { start_ptr = temp_ptr + 9; start_string = i; }
        if((temp_ptr = strstr(fdata.farray[i], "</snippet>")) != NULL) { end_ptr = temp_ptr; end_string = i; }
    }
    for(u32 i = placemark_startstring; i < placemark_endstring; i++)
    {
        if((temp_ptr = strstr(fdata.farray[i], "<Snippet>")) != NULL)  { start_ptr = temp_ptr + 9; start_string = i; coordfl = 1; }
        if((temp_ptr = strstr(fdata.farray[i], "</Snippet>")) != NULL) { end_ptr = temp_ptr; end_string = i; }
    }
    if(start_ptr != NULL)
    {
        copy_tag_text(start_ptr, ((end_ptr != NULL) && (start_string == end_string)) ? end_ptr : NULL,
                      temp_arr, sizeof(temp_arr));
        if(!coordfl)
        {
            if(sscanf(temp_arr, "Timestamp: %d &lt;br/&gt; %lf", &placemark.timestamp, &placemark.signallevel) == 2)
                placemark.hasdata = 1;
        }
        else
        {
            sscanf(temp_arr, "<![CDATA[<i>Timestamp:</i>  %d", &placemark.timestamp);
            strcpy(placemark.name, "GPS Data");   // correcting the name field
        }
    }

    /*-------------------------Parsing the coordinates field----------------*/
    start_ptr = NULL; end_ptr = NULL; start_string = 0; end_string = 0;
    for(u32 i = placemark_startstring; i < placemark_endstring; i++)
    {
        if((temp_ptr = strstr(fdata.farray[i], "<coordinates>")) != NULL)  { start_ptr = temp_ptr + 13; start_string = i; }
        if((temp_ptr = strstr(fdata.farray[i], "</coordinates>")) != NULL) { end_ptr = temp_ptr; end_string = i; }
    }
    if(start_ptr != NULL)
    {
        copy_tag_text(start_ptr, ((end_ptr != NULL) && (start_string == end_string)) ? end_ptr : NULL,
                      temp_arr, sizeof(temp_arr));
        sscanf(temp_arr, "%lf,%lf,%lf", &placemark.longitude, &placemark.latitude, &placemark.altitude);
    }

    /*--------------------------Parsing the color field----------------------*/
    start_ptr = NULL; end_ptr = NULL; start_string = 0; end_string = 0;
    for(u32 i = placemark_startstring; i < placemark_endstring; i++)
    {
        if((temp_ptr = strstr(fdata.farray[i], "<color>")) != NULL)  { start_ptr = temp_ptr + 7; start_string = i; }
        if((temp_ptr = strstr(fdata.farray[i], "</color>")) != NULL) { end_ptr = temp_ptr; end_string = i; }
    }
    if(start_ptr != NULL)
    {
        copy_tag_text(start_ptr, ((end_ptr != NULL) && (start_string == end_string)) ? end_ptr : NULL,
                      temp_arr, sizeof(temp_arr));
        sscanf(temp_arr, "%9s", placemark.color);   // color[10], the width keeps the neighbours safe
    }
    return placemark;
}


/*-----------The function of forming arrays of placemarks combined by folders--------------*/
/*----Index of the folder that holds the GPS track, or -1.  The track folder is special:
     Romes writes two placemarks per point there (the point and a line segment), and the
     whole coverage calculation uses its coordinates.  It used to be assumed to be
     folder[0], which silently halved a signal folder if Romes exported it elsewhere.----*/
int gpsfolderindex(folder_t* folder, int folder_quantity)
{
    for(int i = 0; i < folder_quantity; i++)
    {
        if(strstr(folder[i].name, "GPS Data") != NULL) return i;
    }
    return -1;
}

void getkmldata(folder_t* folder, int folder_quantity, filedata_t fdata)
{
    int gps_index;

    for(int i = 0; i < folder_quantity; i++)
    {
        int is_gps = (strcmp(folder[i].name, "GPS Data") == 0);

        folder[i].placemark_arr = malloc(folder[i].placemark_quantity * sizeof(placemark_t));
        if(folder[i].placemark_arr == NULL)
        {
            printf("Memory allocation error!\n");
            clean_stdin();
            getchar();
            exit(1);
        }
        for(u32 j = 0; j < folder[i].placemark_quantity; j++)
        {
            if(is_gps && (j % 2)) continue;
            if(is_gps) folder[i].placemark_arr[j/2] = getplacemarkdata(folder[i].placemark_startstr[j], folder[i].placemark_endstr[j], fdata);
            else       folder[i].placemark_arr[j]   = getplacemarkdata(folder[i].placemark_startstr[j], folder[i].placemark_endstr[j], fdata);
        }
        if(folder[i].placemark_quantity > 0)
        {
            memcpy(folder[i].name, folder[i].placemark_arr[0].name, sizeof(folder[i].name));
            folder[i].name[sizeof(folder[i].name) - 1] = '\0';
        }
    }

    /* Only the GPS folder holds two placemarks per point, so only its counter is halved. */
    gps_index = gpsfolderindex(folder, folder_quantity);
    if(gps_index >= 0) folder[gps_index].placemark_quantity /= 2;

    for(int i = 0; i < folder_quantity; i++)  // clearing the memory allocated for arrays with line numbers of the beginning and end of each placemark
    {
        free(folder[i].placemark_startstr);
        free(folder[i].placemark_endstr);
        folder[i].placemark_startstr = NULL;
        folder[i].placemark_endstr = NULL;
    }
}

/*---------------------------------------Coverage calculation functions--------------------------------*/

void getaverlvl(folder_t* folder, int folder_quantity, int averageingdepth, AVG_TYPE avgtype) // averaging level for N points (N = averageingdepth)
{
	double tmp[110];
	int halfdepth = averageingdepth / 2;
	/* halfdepth == 0 means no averaging at all: the point keeps its own level */
	if(halfdepth > 50) halfdepth = 50;   // tmp[] holds no more than 2 * 50 points

	for(int i = 0; i < folder_quantity; i++)
	{
		int quantity = (int)folder[i].placemark_quantity;
		for(int j = 0; j < quantity; j++)
		{
			/* The window is clamped to the array bounds. Without clamping a track shorter
			   than the averaging depth was read beyond the end of placemark_arr, and on
			   short tracks the "head" and the "tail" branches used to run both at once. */
			int lo = (halfdepth > 0) ? (j - halfdepth) : j;
			int hi = (halfdepth > 0) ? (j + halfdepth) : (j + 1);
			int used = 0;
			if(lo < 0) lo = 0;
			if(hi > quantity) hi = quantity;

			folder[i].placemark_arr[j].averagelevel = 0;

			switch(avgtype)
			{
			case sma:  // simple moving average
				for(int k = lo; k < hi; k++) {
					if(!folder[i].placemark_arr[k].hasdata) continue;   // an empty cell is not a level of 0 dBm
					folder[i].placemark_arr[j].averagelevel = folder[i].placemark_arr[j].averagelevel + folder[i].placemark_arr[k].signallevel;
					used++;
				}
				if(used > 0) folder[i].placemark_arr[j].averagelevel /= used;
			break;
			case median: // median average
			{
				for(int k = lo; k < hi; k++) {
					if(!folder[i].placemark_arr[k].hasdata) continue;
					tmp[used] =  folder[i].placemark_arr[k].signallevel;
					used++;
				}
				if(used > 0) folder[i].placemark_arr[j].averagelevel = getmedian(tmp, used);
			}
			break;
			}
			/* A point whose whole window is empty has no averaged level at all. */
			folder[i].placemark_arr[j].hasdata = (used > 0) ? folder[i].placemark_arr[j].hasdata : 0;
		}
	}
}

/*------------------Function form a table of uncovered sections-----------------------------------*/

/*----Threshold of a folder, chosen by the standard mentioned in its name.----*/
static double folderthreshold(const folder_t* folder, const init_t* settings)
{
    if(strstr(folder->name, "GSM") != NULL) return settings->GSMcoveragelvl;
    if(strstr(folder->name, "LTE") != NULL) return settings->LTEcoveragelvl;
    if((strstr(folder->name, "3G") != NULL) || (strstr(folder->name, "UMTS") != NULL)) return settings->UMTScoveragelvl;
    return settings->defaultcovlvl;
}

/*----A point without a measurement is not coverage.  Before, an empty cell of a csv read
     as 0 dBm, which is above every threshold, so gaps counted as covered.----*/
static int pointcovered(const placemark_t* placemark, double threshold)
{
    return (placemark->hasdata && (placemark->averagelevel >= threshold)) ? 1 : 0;
}

static covreg_t* allocregions(u32 quantity)
{
    covreg_t* regions = malloc(quantity * sizeof(covreg_t));
    if(regions == NULL)
    {
        printf("Memory allocation error!\n");
        clean_stdin();
        getchar();
        exit(1);
    }
    memset(regions, 0, quantity * sizeof(covreg_t));
    return regions;
}

/*----Closes the region k with the point "last" and returns the number of regions.----*/
static u32 closeregion(folder_t* folder, const folder_t* track, u32 k, u32 last)
{
    folder->covreg[k].endlat    = track->placemark_arr[last].latitude;
    folder->covreg[k].endlong   = track->placemark_arr[last].longitude;
    folder->covreg[k].endnumber = track->placemark_arr[last].measnumber;
    return k + 1;
}

static void sumregions(folder_t* folder)
{
    folder->covtotdist = 0;
    folder->uncovtotdist = 0;
    for(u32 m = 0; m < folder->reg_quantity; m++)
    {
        if(folder->covreg[m].coverfl) folder->covtotdist += folder->covreg[m].distance;
        else                          folder->uncovtotdist += folder->covreg[m].distance;
    }
}

void createcovtab(folder_t *folder, int folder_quantity, init_t settings)
{
    double distance_element;
    double thresholds[MAX_FOLDERS];
    int    curr_cover = 0;
    int    gps_index = gpsfolderindex(folder, folder_quantity);
    int    avg_index = folder_quantity;   /* the slot reserved next to the data folders */

    if(gps_index < 0) gps_index = 0;      /* a source without a GPS folder: the first one carries the track */

    for(int i = 0; i < folder_quantity; i++)
    {
        thresholds[i] = folderthreshold(&folder[i], &settings);
        folder[i].threshold = thresholds[i];
    }

    if(settings.covercalctype == separate)
    {
        for(int i = 0; i < folder_quantity; i++)
        {
            u32 k = 0;
            u32 last;

            if(i == gps_index) continue;
            if(folder[i].placemark_quantity < 2) continue;

            folder[i].totdist = 0;
            folder[i].uncovtotdist = 0;
            folder[i].covtotdist = 0;
            folder[i].covreg = allocregions(folder[i].placemark_quantity);

            curr_cover = pointcovered(&folder[i].placemark_arr[0], thresholds[i]);
            folder[i].placemark_arr[0].coverfl = curr_cover;
            folder[i].placemark_arr[0].distfromstart = 0;

            folder[i].covreg[k].coverfl     = curr_cover;
            folder[i].covreg[k].startlat    = folder[i].placemark_arr[0].latitude;
            folder[i].covreg[k].startlong   = folder[i].placemark_arr[0].longitude;
            folder[i].covreg[k].startnumber = folder[i].placemark_arr[0].measnumber;
            folder[i].covreg[k].distance    = 0;

            last = folder[i].placemark_quantity - 1;
            for(u32 j = 1; j <= last; j++)   /* the last point is no longer a special case */
            {
                distance_element = distearth(folder[i].placemark_arr[j].latitude, folder[i].placemark_arr[j].longitude,
                                             folder[i].placemark_arr[j-1].latitude, folder[i].placemark_arr[j-1].longitude);
                folder[i].totdist += distance_element;
                folder[i].placemark_arr[j].distfromstart = folder[i].totdist;

                curr_cover = pointcovered(&folder[i].placemark_arr[j], thresholds[i]);
                folder[i].placemark_arr[j].coverfl = curr_cover;
                folder[i].covreg[k].distance += distance_element;

                if((folder[i].covreg[k].coverfl != curr_cover) && (j != last))
                {
                    /* end of one region, start of the next; the coordinates come from this
                       folder now - they used to be taken from folder[0] by mistake */
                    k = closeregion(&folder[i], &folder[i], k, j - 1);
                    folder[i].covreg[k].coverfl     = curr_cover;
                    folder[i].covreg[k].startlat    = folder[i].placemark_arr[j].latitude;
                    folder[i].covreg[k].startlong   = folder[i].placemark_arr[j].longitude;
                    folder[i].covreg[k].startnumber = folder[i].placemark_arr[j].measnumber;
                    folder[i].covreg[k].distance    = 0;
                }
            }
            folder[i].reg_quantity = closeregion(&folder[i], &folder[i], k, last);
            sumregions(&folder[i]);
        }
    }

    if(settings.covercalctype == total)
    {
        folder_t* track = &folder[gps_index];      /* coordinates and distances come from the track */
        folder_t* raw   = &folder[gps_index];      /* raw coverage is stored next to the track */
        folder_t* avg   = &folder[avg_index];      /* averaged coverage no longer overwrites a real folder */
        u32 k = 0;
        u32 last;

        if(track->placemark_quantity < 2) return;
        last = track->placemark_quantity - 1;

        raw->covreg  = allocregions(track->placemark_quantity);
        raw->totdist = 0;

        curr_cover = 0;
        for(int i = 0; i < folder_quantity; i++)
        {
            if(i == gps_index) continue;
            if(pointcovered(&folder[i].placemark_arr[0], thresholds[i])) { curr_cover = 1; break; }
        }
        track->placemark_arr[0].coverfl = curr_cover;
        track->placemark_arr[0].distfromstart = 0;

        raw->covreg[k].coverfl     = curr_cover;
        raw->covreg[k].startlat    = track->placemark_arr[0].latitude;
        raw->covreg[k].startlong   = track->placemark_arr[0].longitude;
        raw->covreg[k].startnumber = track->placemark_arr[0].measnumber;
        raw->covreg[k].distance    = 0;

        for(u32 j = 1; j <= last; j++)
        {
            distance_element = distearth(track->placemark_arr[j].latitude, track->placemark_arr[j].longitude,
                                         track->placemark_arr[j-1].latitude, track->placemark_arr[j-1].longitude);
            raw->totdist += distance_element;

            curr_cover = 0;
            for(int i = 0; i < folder_quantity; i++)
            {
                if(i == gps_index) continue;
                if(pointcovered(&folder[i].placemark_arr[j], thresholds[i])) { curr_cover = 1; break; }
            }
            track->placemark_arr[j].coverfl = curr_cover;
            track->placemark_arr[j].distfromstart = raw->totdist;
            raw->covreg[k].distance += distance_element;

            if((raw->covreg[k].coverfl != curr_cover) && (j != last))
            {
                k = closeregion(raw, track, k, j - 1);
                raw->covreg[k].coverfl     = curr_cover;
                raw->covreg[k].startlat    = track->placemark_arr[j].latitude;
                raw->covreg[k].startlong   = track->placemark_arr[j].longitude;
                raw->covreg[k].startnumber = track->placemark_arr[j].measnumber;
                raw->covreg[k].distance    = 0;
            }
        }
        raw->reg_quantity = closeregion(raw, track, k, last);
        sumregions(raw);

/*-----------------------Averaging the coverage by distance----------------------------*/

        avg->placemark_quantity = track->placemark_quantity;
        avg->placemark_arr[0].coverfl = track->placemark_arr[0].coverfl;

        for(u32 j = 1; j <= last; j++)
        {
            int    m;
            int    plmk_quant;
            int    plmk_covflsum;
            double dist;
            double cov_1, cov_2, coverage;

            /*----The half window is taken literally: a measurement joins the average only
                 when it really lies within maxskipdist/2 of point j.  The test used to be
                 made after the point had already been counted, so the window reached one
                 measurement further on either side and maxskipdist=0 still averaged three
                 points instead of leaving the coverage untouched.----*/
            plmk_quant = 1;
            plmk_covflsum = track->placemark_arr[j].coverfl;
            m = (int)j;
            while(m > 0)
            {
                m--;
                dist = track->placemark_arr[j].distfromstart - track->placemark_arr[m].distfromstart;
                if(dist > (settings.maxskipdist / 2)) break;
                plmk_covflsum += track->placemark_arr[m].coverfl;
                plmk_quant++;
            }
            cov_1 = (double) plmk_covflsum / plmk_quant;

            m = (int)j;
            plmk_covflsum = track->placemark_arr[j].coverfl;
            plmk_quant = 1;
            while(m < (int)last)   /* the final point of the track used to be left out here */
            {
                m++;
                dist = track->placemark_arr[m].distfromstart - track->placemark_arr[j].distfromstart;
                if(dist > (settings.maxskipdist / 2)) break;
                plmk_covflsum += track->placemark_arr[m].coverfl;
                plmk_quant++;
            }
            cov_2 = (double) plmk_covflsum / plmk_quant;

            coverage = (cov_1 + cov_2) / 2;

            if(coverage > 0.52)      avg->placemark_arr[j].coverfl = 1;
            else if(coverage < 0.48) avg->placemark_arr[j].coverfl = 0;
            else                     avg->placemark_arr[j].coverfl = avg->placemark_arr[j-1].coverfl;
        }

        avg->covreg  = allocregions(track->placemark_quantity);
        avg->totdist = 0;
        k = 0;

        curr_cover = avg->placemark_arr[0].coverfl;
        avg->covreg[k].coverfl     = curr_cover;
        avg->covreg[k].startlat    = track->placemark_arr[0].latitude;
        avg->covreg[k].startlong   = track->placemark_arr[0].longitude;
        avg->covreg[k].startnumber = track->placemark_arr[0].measnumber;
        avg->covreg[k].distance    = 0;

        for(u32 j = 1; j <= last; j++)   /* the last segment used to be added twice: once here and once after the loop */
        {
            distance_element = distearth(track->placemark_arr[j].latitude, track->placemark_arr[j].longitude,
                                         track->placemark_arr[j-1].latitude, track->placemark_arr[j-1].longitude);
            avg->totdist += distance_element;
            curr_cover = avg->placemark_arr[j].coverfl;
            avg->covreg[k].distance += distance_element;

            if((avg->covreg[k].coverfl != curr_cover) && (j != last))
            {
                k = closeregion(avg, track, k, j - 1);
                avg->covreg[k].coverfl     = curr_cover;
                avg->covreg[k].startlat    = track->placemark_arr[j].latitude;
                avg->covreg[k].startlong   = track->placemark_arr[j].longitude;
                avg->covreg[k].startnumber = track->placemark_arr[j].measnumber;
                avg->covreg[k].distance    = 0;
            }
        }
        avg->reg_quantity = closeregion(avg, track, k, last);
        sumregions(avg);
    }
}

void foldermemfree(folder_t* folder, int folder_quantity)
{
    for(int i = 0; i < folder_quantity; i++)
    {
        free(folder[i].placemark_arr);
        folder[i].placemark_arr = NULL;
        free(folder[i].covreg);
        folder[i].covreg = NULL;
        folder[i].reg_quantity = 0;
    }
}