#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "open_file.h"
#include "kml_struct.h"

extern void clean_stdin(void);

/*----Where a csv is read from.  The console reads back the csv it has just written into
     ./Output_CSV_files; the window reads the file the user picked, or the one it keeps in
     the temporary folder while a kml is being converted.----*/
static char csv_input_dir[400] = "./Output_CSV_files/";

void set_csv_input_dir(const char* dir)
{
    size_t length;

    if((dir == NULL) || (dir[0] == 0)) return;
    strncpy(csv_input_dir, dir, sizeof(csv_input_dir) - 2);
    csv_input_dir[sizeof(csv_input_dir) - 2] = 0;
    length = strlen(csv_input_dir);
    if((length > 0) && (csv_input_dir[length - 1] != '\\') && (csv_input_dir[length - 1] != '/'))
    {
        csv_input_dir[length] = '\\';
        csv_input_dir[length + 1] = 0;
    }
}

/*----------------------------------Service functions of the csv parser--------------------------------------*/

static void filetruncated(const char* message)
{
    printf("Error! %s\n", message);
    clean_stdin();
    getchar();
    exit(1);
}

/*----Reads one csv field up to the delimiter.  Characters that do not fit into the buffer are discarded,
      so the buffer can not be overflowed.  A file that ends in the middle of a line stops the program:
      without this check the reading loop never ends, because fgetc() keeps returning EOF.----*/
static void readfield(FILE* stream, char* buffer, u32 buffer_size, int delimiter)
{
    u32 k = 0;
    int ch;
    while(((ch = fgetc(stream)) != EOF) && (ch != delimiter))
    {
        if(k < (buffer_size - 1)) buffer[k++] = (char)ch;
    }
    buffer[k] = '\0';
    if(ch == EOF) filetruncated("Unexpected end of csv file!");
}

/*--------The function opens the kml file, copies it into an array of strings and parses it, forming the kml data structure.------*/
int openkml(char* f_name, folder_t* folder)
{
    char file_name[400];
    strcpy(file_name, f_name);
    strcat(file_name, ".kml");
    FILE* file_stream = fopen(file_name, "r");
    if(file_stream == NULL)
    {
        perror(file_name);
        printf("Or the file name is too long");
        clean_stdin();
        getchar();
        exit(1);
    }
    printf("\nAnalizing file structure..");
    int ch = 0;    // must be int, not char: fgetc() returns EOF (-1), and a signed char turns the byte 0xFF ("ya" in cp1251) into EOF
    u32 char_counter = 0;
    u32 char_max = 1;
    u32 str_max = 1;
    while((ch = fgetc(file_stream)) != EOF) // counting the number of characters and the number of lines in a file
    {
        char_max++;
        if(ch == '\n') str_max++;
    } 
    printf("..ok\n%d symbols, %d lines\n", char_max, str_max);

    fseek(file_stream, 0, SEEK_SET);
    printf("Creating and marking memory..");
    size_t mem_vol = (size_t)str_max * sizeof(char*) + (size_t)char_max * sizeof(char);   /* u32 overflowed on files above 4 GB */
    char** file_array = (char**) malloc(mem_vol); // allocation of memory for an array of pointers to the beginning of each line and for the line themselves
    if(file_array == NULL)
    {
        printf("\nMemory allocation error! The file needs %lu KB.\n", (unsigned long)(mem_vol / 1024));
        clean_stdin();
        getchar();
        exit(1);
    }
    char* start = (char*)file_array + str_max*sizeof(char*);
    int* str_length = malloc(str_max*sizeof(int)); // the number of characters in each line
    if(str_length == NULL)
    {
        printf("\nMemory allocation error!\n");
        clean_stdin();
        getchar();
        exit(1);
    }
    file_array[0] = start;
    for(u32 i = 1; i <= str_max; ++i)  // a cycle of "marking up" lines of allocated memory
    {
        char_counter = 0;
        while((ch = fgetc(file_stream)) != '\n') 
        {
            if(ch == EOF) break;
            char_counter++;
        }
        char_counter++;
        str_length[i-1] = char_counter;
        if(i == str_max) break;                 // file_array holds str_max pointers: index str_max is past its end
        file_array[i] = file_array[i-1] + char_counter;
    }

/* Copying data from a file stream to an array */
    printf("..ok\nCopying file data..");
    fseek(file_stream, 0, SEEK_SET);
    for(u32 i = 0; i < str_max; i++)
    {   
        int j = 0;
        while((ch = fgetc(file_stream)) != '\n') 
        {   
            if(ch == EOF) break;
            file_array[i][j] = ch;
            j++;
        }
        file_array[i][j] = '\0';
    }
    filedata_t f_data;    
    f_data.farray = file_array;
    f_data.string_length = str_length;
    f_data.strings = str_max;

    int folder_quantity = getfolders(folder, f_data);
    getkmldata(folder, folder_quantity, f_data);

    fclose(file_stream);
    free(f_data.farray);
    free(f_data.string_length);
    
    printf("..ok\nUsing memory - %lu KB\n", (unsigned long)((mem_vol + str_max * sizeof(int)) / 1024));

    return folder_quantity;
}

/*-------------------------------------The function opens a csv file and creates a kml data structure----------------------------------------- */

int opencsv(char* f_name, folder_t* folder)
{
    char file_name[400];
    strcpy(file_name, f_name);
    char path[400];
    strcpy(path, csv_input_dir);
    strcat(file_name, ".csv");
    strncat(path, file_name, sizeof(path) - strlen(path) - 1);

    FILE* file_stream = fopen(path, "r");
    if(file_stream == NULL)
    {
        perror(file_name);
        printf("Or the file name is too long");
        clean_stdin();
        getchar();
        exit(1);
    }
    /*-Counting the number of cells in a line and the number of lines in a file (checking for the same number of cells in each line)-*/
    printf("\nAnalizing file structure...");
    int ch = 0;    // must be int, not char: fgetc() returns EOF (-1), and a signed char turns the byte 0xFF ("ya" in cp1251) into EOF
    u32 cell_quantity[2] = {0};
    u32 cell_counter = 0;
    u32 str_max = 0;
    while(((ch = fgetc(file_stream)) != EOF) && (ch != '\n'))
    {
        if(ch == ';')cell_counter++;
    }
    if(ch == EOF) filetruncated("The csv file is empty!");   // without this check the loop above never ends on an empty file
    cell_quantity[0] = cell_counter;
    cell_counter = 0;
    while((ch = fgetc(file_stream)) != EOF)
    {   
        if(ch == ';')cell_counter++;
        if(ch == '\n') 
        {
            cell_quantity[1] = cell_quantity[0];
            cell_quantity[0] = cell_counter;
            if(cell_quantity[0] != cell_quantity[1]) 
            {
                        printf("Error! Uncorrect structure of csv file!\n");
                        clean_stdin();
                        getchar();
                        exit(1);
            }
            cell_counter = 0;
            str_max++;
        }
    }
    int folder_quantity = (cell_quantity[0] + 1) / 8;
    if(((cell_quantity[0] + 1) % 8) != 0)
    {
        printf("Error! Uncorrect structure of csv file!\n");
        clean_stdin();
        getchar();
        exit(1);
    }
    if(str_max == 0) filetruncated("The csv file has no data lines!");
    if(folder_quantity > (MAX_FOLDERS - 1))   // the folder array of main() has no room for so many folders
    {
        printf("Error! The csv file contains more than %d folders!\n", (MAX_FOLDERS - 1));
        clean_stdin();
        getchar();
        exit(1);
    }
    printf("..ok\n%d columns, %d lines\n", cell_quantity[1], str_max);
    fseek(file_stream, 0, SEEK_SET);
    for(int i = 0; i < (folder_quantity + 1); i++){
        folder[i].placemark_arr = malloc((str_max + 10) * sizeof(placemark_t));
        if(folder[i].placemark_arr == NULL){
            printf("Memory allocation error!");
            clean_stdin();
            getchar();
            exit(1);
        }
        folder[i].placemark_quantity = str_max;
    }

    char temp_string[400] = {0};   // as large as the name field of placemark_t

    while(((ch = fgetc(file_stream)) != EOF) && (ch != '\n')){}   // skipping the header line

    for(u32 i = 0; i < str_max; i++)
    {
        for(u32 j = 0; j < (u32)folder_quantity; j++)
        {
            placemark_t* placemark = &folder[j].placemark_arr[i];
            // the color of the last folder in a line is terminated by '\n', all the other fields by ';'
            int delimiter = (j == (u32)(folder_quantity - 1)) ? '\n' : ';';

            memset(placemark, 0, sizeof(*placemark));

            /*----copying the name field---------*/
            readfield(file_stream, temp_string, sizeof(temp_string), ';');
            strncpy(placemark->name, temp_string, sizeof(placemark->name) - 1);
            placemark->name[sizeof(placemark->name) - 1] = '\0';
            /*----copying the measnumber field---*/
            readfield(file_stream, temp_string, sizeof(temp_string), ';');
            sscanf(temp_string, "%d", &placemark->measnumber);
            /*----copying the timestamp field----*/
            readfield(file_stream, temp_string, sizeof(temp_string), ';');
            sscanf(temp_string, "%d", &placemark->timestamp);
            /*----copying the signallevel field--*/
            /* An empty cell means "no measurement at this point" (savecsv writes those when
               filflag=0).  sscanf leaves the variable untouched on an empty string, so the
               level used to keep whatever was in the freshly allocated memory - in practice
               0 dBm, which is above every threshold and counted the gap as covered. */
            readfield(file_stream, temp_string, sizeof(temp_string), ';');
            placemark->hasdata = (temp_string[0] != '\0') ? 1 : 0;
            if(placemark->hasdata) sscanf(temp_string, "%lf", &placemark->signallevel);
            /*----copying the latitude field---------*/
            readfield(file_stream, temp_string, sizeof(temp_string), ';');
            if(temp_string[0] != '\0') sscanf(temp_string, "%lf", &placemark->latitude);
            else                       placemark->latitude = folder[0].placemark_arr[i].latitude;
            /*----copying the longitude field---------*/
            readfield(file_stream, temp_string, sizeof(temp_string), ';');
            if(temp_string[0] != '\0') sscanf(temp_string, "%lf", &placemark->longitude);
            else                       placemark->longitude = folder[0].placemark_arr[i].longitude;
            /*----copying the altitude field---------*/
            readfield(file_stream, temp_string, sizeof(temp_string), ';');
            if(temp_string[0] != '\0') sscanf(temp_string, "%lf", &placemark->altitude);
            /*----copying the color field---------*/
            readfield(file_stream, temp_string, sizeof(temp_string), delimiter);
            strncpy(placemark->color, temp_string, sizeof(placemark->color) - 1);
            placemark->color[sizeof(placemark->color) - 1] = '\0';
        }
        // the '\n' at the end of a line has already been read as the delimiter of the last color field
    }

/*--------------CORRECTING FOLDER NAMES, adding short names (standart-provider)-----------------*/
    for(int i = 0; i < folder_quantity; i++)
    {
        memcpy(folder[i].name, folder[i].placemark_arr[0].name, sizeof(folder[i].name));
        folder[i].name[sizeof(folder[i].name) - 1] = 0;
        strncpy(folder[i].short_name, folder[i].name, sizeof(folder[i].short_name) - 1);
        folder[i].short_name[sizeof(folder[i].short_name) - 1] = 0;

        char tmp_char[10] = {'\\','/',':','*','?','\"','<','>','|'}; //exclude forbidden characters for file names in Windows
        u32 p = 0;
        while(folder[i].short_name[p] != '\0')
        {
            for(u32 q = 0; q < 9; q++)
            {
                if(folder[i].short_name[p] == tmp_char[q]) 
                {
                    folder[i].short_name[p] = '_';
                }
            }
            p++;
        }
        

        
#if 0
        //--reading the provider's name (located between consecutive characters '[' and '[') 
        //(The name of the standard is also written there, in this case it will be included in the shortname twice.)
        char* tmp_ptr[3];
        tmp_ptr[0] = folder[i].name;
        char subname[100];
        while(1)
        {
            tmp_ptr[0] = strchr(tmp_ptr[0], '[');

            if(tmp_ptr[0] != NULL)
            {
                tmp_ptr[0]++;
                tmp_ptr[1] = strchr(tmp_ptr[0], '[');
                tmp_ptr[2] = strchr(tmp_ptr[0], ']');
                if(tmp_ptr[1] < tmp_ptr[2]) break;
            }  
            else 
            {
                tmp_ptr[0] = folder[i].name;
                tmp_ptr[1] = folder[i].name + strlen(folder[i].name);
                break;
            }
        }
        int max_k = tmp_ptr[1] - tmp_ptr[0];
        for(int k = 0; k < max_k; k++)
        {
            subname[k] = tmp_ptr[0][k];
        }
        subname[max_k] = '\0';
        //---------------------------------------------------------------------------------
        if(strstr(folder[i].name, "GPS") != NULL) 
        {
            strcpy(folder[i].short_name, "GPS");
        }
        if(strstr(folder[i].name, "GSM") != NULL) 
        {
            strcpy(folder[i].short_name, "GSM_");
            strcat(folder[i].short_name, subname);
        }
        if(strstr(folder[i].name, "LTE") != NULL)  
        {
            strcpy(folder[i].short_name, "LTE_");
            strcat(folder[i].short_name, subname);
        }
        if((strstr(folder[i].name, "3G") != NULL) || (strstr(folder[i].name, "UMTS") != NULL))  
        {
            strcpy(folder[i].short_name, "UMTS_");
            strcat(folder[i].short_name, subname);
        }
#endif
    }

    fclose(file_stream);

    if(folder_quantity == 1)
        {
            for(u32 i = 0; i < str_max; i++)
            {
                    folder[1].placemark_arr[i].measnumber = folder[0].placemark_arr[i].measnumber;
                    folder[1].placemark_arr[i].timestamp = folder[0].placemark_arr[i].timestamp;
                    folder[1].placemark_arr[i].signallevel = folder[0].placemark_arr[i].signallevel;
                    folder[1].placemark_arr[i].latitude = folder[0].placemark_arr[i].latitude;
                    folder[1].placemark_arr[i].longitude = folder[0].placemark_arr[i].longitude;
            }
        }
	return folder_quantity;
}
