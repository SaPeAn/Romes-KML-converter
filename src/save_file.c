#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "kml_struct.h"
#include "save_file.h"


static char kml_header[300] = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<kml xmlns=\"http://www.opengis.net/kml/2.2\" "
"xmlns:gx=\"http://www.google.com/kml/ext/2.2\" xmlns:kml=\"http://www.opengis.net/kml/2.2\" xmlns:atom=\"http://www.w3.org/2005/Atom\">\n";

static char signal_folder_header[200] = "<Folder>\n	<name>R&amp;S %s\n	</name>\n	<open>0\n	</open>\n";

static char placemark_template[2500] =
"   <Placemark>\n"
"		<name>R&amp;S %s Measurement #%d\n"
"		</name>\n"
"		<snippet>Timestamp: %d &lt;br/&gt; %.2f0000 dBm\n"
"		</snippet>\n"
"		<visibility>0\n"
"		</visibility>\n"
"		<Point>\n"
"			<altitudeMode>relativeToGround\n"
"			</altitudeMode>\n"
"			<extrude>1\n"
"			</extrude>\n"
"			<coordinates>%f,%f,%f\n"
"			</coordinates></Point>\n"
"		<styleUrl>#StdPlacemarkStyle\n"
"		</styleUrl>\n"
"		<Style ID=\"StdPlacemarkStyle\">\n"
"			<LineStyle>\n"
"				<color>%s\n"
"				</color>\n"
"				<width>4\n"
"				</width></LineStyle>\n"
"			<IconStyle>\n"
"				<Icon>\n"
"					<href>http://maps.google.com/mapfiles/kml/pal2/icon26.png\n"
"					</href></Icon>\n"
"				<color>%s\n"
"				</color>\n"
"				<colorMode>normal\n"
"				</colorMode>\n"
"				<scale>0.35\n"
"				</scale>\n"
"			</IconStyle>\n"
"			<LabelStyle>\n"
"				<scale>0\n"
"				</scale>\n"
"				<color>%s\n"
"				</color></LabelStyle>\n"
"		</Style>\n"
"		<description><![CDATA[<i>Timestamp:</i> %d <table border=\"0\"><tr><td nowrap><i><u> R&S %s: </u></i></td><td nowrap> %.2f0000 dBm </td></tr>"
"<tr><td nowrap><i> R&S GPS Device[1] - Latitude: </i></td><td nowrap> %f ° </td></tr>"
"<tr><td nowrap><i> R&S GPS Device[1] - Longitude: </i></td><td nowrap> %f ° </td></tr> </table> <br/>]]>\n"
"		</description>\n"
"   </Placemark>\n";

extern void clean_stdin(void);

/*----Where the results go.  The console build writes into the two folders next to the exe,
     as it always did; the window sets the folder the user picks when the coverage is
     calculated, so opening a file writes nothing at all.----*/
static char csv_output_dir[400] = "./Output_CSV_files/";
static char kml_output_dir[400] = "./Output_KML_files/";

/*----Copies a folder name and makes sure it ends with a separator.----*/
static void copy_output_dir(char* target, size_t size, const char* source)
{
    size_t length;

    if((source == NULL) || (source[0] == 0)) return;
    strncpy(target, source, size - 2);
    target[size - 2] = 0;
    length = strlen(target);
    if((length > 0) && (target[length - 1] != '\\') && (target[length - 1] != '/'))
    {
        target[length] = '\\';
        target[length + 1] = 0;
    }
}

void set_output_dirs(const char* csv_dir, const char* kml_dir)
{
    copy_output_dir(csv_output_dir, sizeof(csv_output_dir), csv_dir);
    copy_output_dir(kml_output_dir, sizeof(kml_output_dir), kml_dir);
}

/*----Appends to a path without ever writing past its end: strncat() takes the number of
     characters to append, not the size of the destination, so the old calls with a bound
     of 400 could overrun a 400 byte buffer.----*/
static void appendpath(char* path, size_t size, const char* text)
{
    size_t used = strlen(path);
    if(used + 1 >= size) return;
    strncat(path, text, size - used - 1);
}

void savecsv(char* f_name, folder_t* folder, int folder_quantity, int fillinflag)
{    
    printf("\nSaving csv-file..");
    char file_name[400];
    strcpy(file_name, f_name);
    char path[400];
    strcpy(path, csv_output_dir);
    strcat(file_name, ".csv");
    appendpath(path, sizeof(path), file_name);
    FILE* csv_file = fopen(path, "w");
    if(csv_file == NULL)
    {
        perror(file_name);
        printf("Or the file name is too long");
        clean_stdin();
        getchar();
        exit(1);
    }
    for(int i = 0; i < folder_quantity; i++)
    { 
        fprintf(csv_file, "name;measnumber;timestamp;signallevel;latitude;longitude;altitude;color"); // name[120], measnumber, timestamp, signallevel, latitude, longitude, altitude, color[20]
        if(i < (folder_quantity - 1)) fprintf(csv_file, ";");
    }
    fprintf(csv_file, "\n");

    int k[MAX_FOLDERS] = {0};

    for(int j = 0; j < (int)folder[0].placemark_quantity; j++)
    {
        for(int i = 0; i < folder_quantity; i++)
        {
            const placemark_t* track = &folder[0].placemark_arr[j];
            const placemark_t* point;

            if(i == 0)
            {
                k[i] = j;
                fprintf(csv_file, "%s;%d;%d;%f;%f;%f;%f;%s", track->name, track->measnumber, track->timestamp,
                        track->signallevel, track->latitude, track->longitude, track->altitude, track->color);
                if(i < (folder_quantity - 1)) fprintf(csv_file, ";");
                continue;
            }

            /* A folder can run out of points before the track does; without this check the
               reading went past the end of placemark_arr. */
            point = (k[i] < (int)folder[i].placemark_quantity) ? &folder[i].placemark_arr[k[i]] : NULL;

            if((point != NULL) && (track->timestamp == point->timestamp))
            {
                if(fillinflag)
                    fprintf(csv_file, "%s;%d;%d;%f;%f;%f;%f;%s", point->name, track->measnumber, point->timestamp,
                            point->signallevel, point->latitude, point->longitude, point->altitude, point->color);
                else
                    fprintf(csv_file, "%s;%d;%d;%f;%f;%f;%f;%s", point->name, point->measnumber, point->timestamp,
                            point->signallevel, point->latitude, point->longitude, point->altitude, point->color);
                k[i]++;
            }
            else if(fillinflag && (point != NULL))
            {
                /* No measurement at this moment: the last known level of this folder is held
                   and the position is taken from the track. */
                fprintf(csv_file, "%s;%d;%d;%f;%f;%f;%f;%s", point->name, track->measnumber, track->timestamp,
                        point->signallevel, track->latitude, track->longitude, point->altitude, point->color);
            }
            else
            {
                fprintf(csv_file, ";;;;;;;");
            }
            if(i < (folder_quantity - 1)) fprintf(csv_file, ";");
        }
        fprintf(csv_file, "\n");
    }
    fclose(csv_file);
    printf("..ok\n");
}


void savekml(char* f_name, folder_t* folder, int folder_quantity)
{
    printf("\nSaving kml-file(s)\n");
    char path[400];

    for(int i = 0; i < folder_quantity; i++)
    {
        if(strstr(folder[i].name, "GPS Data") != NULL) continue;

        strcpy(path, kml_output_dir);
        appendpath(path, sizeof(path), f_name);
        appendpath(path, sizeof(path), "_");
        appendpath(path, sizeof(path), folder[i].short_name);
        appendpath(path, sizeof(path), ".kml");

        FILE* kml_file = fopen(path, "w");
        if(kml_file == NULL)
        {
            perror(path);
            printf("Or the file name is too long");
            clean_stdin();
            getchar();
            exit(1);
        }

        fputs(kml_header, kml_file);          /* a header is text, not a format string */
        fprintf(kml_file, signal_folder_header, folder[i].name);

        for(u32 j = 0; j < folder[i].placemark_quantity; j++)
        {
            const placemark_t* placemark = &folder[i].placemark_arr[j];
            if(!placemark->hasdata) continue;   /* a point without a measurement is not exported */

            fprintf(kml_file, placemark_template,
                placemark->name,        placemark->measnumber, placemark->timestamp,
                placemark->signallevel, placemark->longitude,  placemark->latitude,
                placemark->altitude,    placemark->color,      placemark->color,
                placemark->color,       placemark->timestamp,  placemark->name,
                placemark->signallevel, placemark->latitude,   placemark->longitude);
        }
        fprintf(kml_file, "</Folder>\n");
        fprintf(kml_file, "</kml>");
        fclose(kml_file);
        printf("%s file saved.\n", path);
    }
}

/*----Writes the two coverage overlays.  The track and its coordinates come from the GPS
     folder, the raw flags from it as well, and the averaged flags from the reserved slot
     folder[folder_quantity] - which used to be folder[1], a real operator folder.----*/
void savekml_test(char* f_name, folder_t* folder, int folder_quantity)
{
    char  path[400];
    char  title[440];
    int   gps = gpsfolderindex(folder, folder_quantity);
    int   avg = folder_quantity;
    int   pass;

    if(gps < 0) gps = 0;

    for(pass = 0; pass < 2; pass++)
    {
        const folder_t* flags = (pass == 0) ? &folder[gps] : &folder[avg];
        const char*     suffix = (pass == 0) ? "_rawcov.kml" : "_avgcov.kml";
        const char*     label  = (pass == 0) ? "RAW coverage data" : "AVG coverage data";
        double          altitude = (pass == 0) ? 0.0 : 100.0;
        FILE*           kml_file;

        printf(pass == 0 ? "\nSaving raw coverage kml-file\n" : "\nSaving averaging coverage kml-file\n");

        strcpy(path, kml_output_dir);
        appendpath(path, sizeof(path), f_name);
        appendpath(path, sizeof(path), suffix);

        snprintf(title, sizeof(title), "%s%s", f_name, (pass == 0) ? "_rawcov" : "_avgcov");

        kml_file = fopen(path, "w");
        if(kml_file == NULL)
        {
            perror(path);
            printf("Or the file name is too long");
            clean_stdin();
            getchar();
            exit(1);
        }

        fputs(kml_header, kml_file);
        fprintf(kml_file, signal_folder_header, title);

        for(u32 j = 0; j < folder[gps].placemark_quantity; j++)
        {
            const char* color = (flags->placemark_arr[j].coverfl) ? "AB00E660" : "AB000000";
            fprintf(kml_file, placemark_template,
                title,   folder[gps].placemark_arr[j].measnumber, folder[gps].placemark_arr[j].timestamp,
                0.0,     folder[gps].placemark_arr[j].longitude,  folder[gps].placemark_arr[j].latitude,
                altitude, color, color,
                color,   folder[gps].placemark_arr[j].timestamp,  label,
                0.0,     folder[gps].placemark_arr[j].latitude,   folder[gps].placemark_arr[j].longitude);
        }
        fprintf(kml_file, "</Folder>\n");
        fprintf(kml_file, "</kml>");
        fclose(kml_file);
        printf("%s file saved.\n", path);
    }
}

void savecsvcovchart(char *filename, folder_t *folder, int folder_quantity)
{
    char path[400];
    strcpy(path, csv_output_dir);
    appendpath(path, sizeof(path), filename);
    appendpath(path, sizeof(path), "_coverchart.csv");
    FILE* csv_file = fopen(path, "w");
    if(csv_file == NULL)
    {
        perror(path);
        printf("Or the file name is too long");
        clean_stdin();
        getchar();
        exit(1);
    }

    for(int i = 0; i < folder_quantity; i ++)
    {
        if(strstr(folder[i].name, "GPS Data") != NULL) continue;   /* the header and the rows must filter alike, otherwise the columns shift */
        fprintf(csv_file, "%s;;;;;", folder[i].short_name);
    }
    fprintf(csv_file, "\n");

    for(int i = 0; i < (int)folder[0].placemark_quantity; i ++)
    {        
        for(int l = 0; l < folder_quantity; l++)
        {
            if(strstr(folder[l].name, "GPS Data") != NULL) continue;
            fprintf(csv_file, ";%d;%f;%f;%f;", folder[l].placemark_arr[i].measnumber, folder[l].threshold, folder[l].placemark_arr[i].signallevel, folder[l].placemark_arr[i].averagelevel);
        }
        fprintf(csv_file, "\n");
    }
    fclose(csv_file);
}

/*----Writes the table of uncovered sections.  In total mode the sections and the totals
     now come from the same pass: both from the averaged coverage.  The file used to list
     the averaged sections but sum up the raw ones, so the total did not match the rows.
     The raw figure is kept as an extra line for reference.----*/
void savecsvcovtab(char *filename, folder_t *folder, int folder_quantity, init_t settings)
{
    char path[400];

    if(settings.covercalctype == separate)
    {
        for(int i = 0; i < folder_quantity; i++)
        {
            FILE* csvcovtab;

            if(strstr(folder[i].name, "GPS Data") != NULL) continue;

            strcpy(path, csv_output_dir);
            appendpath(path, sizeof(path), "Coverage_");
            appendpath(path, sizeof(path), filename);
            appendpath(path, sizeof(path), "_");
            appendpath(path, sizeof(path), folder[i].short_name);
            appendpath(path, sizeof(path), ".csv");

            csvcovtab = fopen(path, "w");
            if(csvcovtab == NULL)
            {
                perror(path);
                printf("Or the file name is too long");
                clean_stdin();
                getchar();
                exit(1);
            }
            fprintf(csvcovtab, "distance;start latitude;start longitude;end latitude;end longitude;\n");
            for(u32 j = 0; j < folder[i].reg_quantity; j++)
            {
                if(!folder[i].covreg[j].coverfl)
                {
                    fprintf(csvcovtab, "%f;%f;%f;%f;%f\n", folder[i].covreg[j].distance, folder[i].covreg[j].startlat,
                    folder[i].covreg[j].startlong, folder[i].covreg[j].endlat, folder[i].covreg[j].endlong);
                }
            }
            fprintf(csvcovtab, "\ntotal distance;%f;\ncovered distance;%f;\nuncovered distance;%f;\n",
                    folder[i].totdist, folder[i].covtotdist, folder[i].uncovtotdist);
            fclose(csvcovtab);
        }
    }

    if(settings.covercalctype == total)
    {
        int   gps = gpsfolderindex(folder, folder_quantity);
        int   avg = folder_quantity;
        FILE* csvcovtab;

        if(gps < 0) gps = 0;

        strcpy(path, csv_output_dir);
        appendpath(path, sizeof(path), "Coverage_");
        appendpath(path, sizeof(path), filename);
        appendpath(path, sizeof(path), ".csv");

        csvcovtab = fopen(path, "w");
        if(csvcovtab == NULL)
        {
            perror(path);
            printf("Or the file name is too long");
            clean_stdin();
            getchar();
            exit(1);
        }
        fprintf(csvcovtab, "distance;start latitude;start longitude;end latitude;end longitude;\n");
        for(u32 j = 0; j < folder[avg].reg_quantity; j++)
        {
            if(!folder[avg].covreg[j].coverfl)
            {
                fprintf(csvcovtab, "%f;%f;%f;%f;%f\n", folder[avg].covreg[j].distance, folder[avg].covreg[j].startlat,
                    folder[avg].covreg[j].startlong, folder[avg].covreg[j].endlat, folder[avg].covreg[j].endlong);
            }
        }
        fprintf(csvcovtab, "\ntotal distance;%f;\ncovered distance;%f;\nuncovered distance;%f;\n",
                folder[avg].totdist, folder[avg].covtotdist, folder[avg].uncovtotdist);
        fprintf(csvcovtab, "uncovered distance without distance averaging;%f;\n", folder[gps].uncovtotdist);
        fclose(csvcovtab);
    }
}
