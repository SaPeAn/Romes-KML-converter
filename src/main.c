
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <locale.h>
#include <direct.h>
#include "open_file.h"
#include "save_file.h"
#include "kml_struct.h"
#include "main.h"
#include "instruction.h"

void clean_stdin(void)
{
	int c;
	do {
	c = getchar();
	} while (c != '\n' && c != EOF);
}

void printkmldata(folder_t* folder, int folder_quantity, u32 start_plm, u32 end_plm)
{
    printf("\n");
    for(int i = 0; i < folder_quantity; i++)
    {
        printf("Folder %d, %s, %d точек\n", i, folder[i].short_name, folder[i].placemark_quantity);
        for(u32 j = start_plm; j < end_plm; j++)
        {
            printf("    № %d, имя: %s, время: %d мс, уровень: %f, шир: %f, дол: %f, выс: %f, цвет: %s\n", folder[i].placemark_arr[j].measnumber, 
                folder[i].placemark_arr[j].name, folder[i].placemark_arr[j].timestamp, folder[i].placemark_arr[j].signallevel, 
                folder[i].placemark_arr[j].latitude, folder[i].placemark_arr[j].longitude, folder[i].placemark_arr[j].altitude,
                folder[i].placemark_arr[j].color);
        }
    }
}

void printcoveragetable(folder_t* folder, int folder_quantity, init_t settings)
{
    if(settings.covercalctype == separate)
    {
        for(int l = 0; l < folder_quantity; l++)
        {
            if(strstr(folder[l].name, "GPS Data") != NULL) continue;
            printf("\n%s\n", folder[l].short_name);
            for(u32 i = 0; i < folder[l].reg_quantity; i ++)
            {
                printf("Участок № %d; Покрытие - %d; %f км; начало %fс.ш. %fв.д.; конец %fс.ш. %fв.д.\n", i, folder[l].covreg[i].coverfl, folder[l].covreg[i].distance, folder[l].covreg[i].startlat, 
                    folder[l].covreg[i].startlong, folder[l].covreg[i].endlat, folder[l].covreg[i].endlong);
            }
        }
    }
    if(settings.covercalctype == total)
    {
        printf("\n%s\n", "TOTAL COVERAGE");
        int avg = folder_quantity;   /* the averaged coverage lives in the reserved slot now */
        for(u32 i = 0; i < folder[avg].reg_quantity; i ++)
        {
            printf("Участок № %d; Покрытие - %d, %f км; начало %fс.ш. %fв.д.; конец %fс.ш. %fв.д.\n", i, folder[avg].covreg[i].coverfl, folder[avg].covreg[i].distance, folder[avg].covreg[i].startlat, 
                folder[avg].covreg[i].startlong, folder[avg].covreg[i].endlat, folder[avg].covreg[i].endlong);
        }
    }
}

init_t initialization(void)
{
    static const char init_string[300] = "[OPTIONS]\nfilflag=1\nGSMlevel=-85.0\nUMTSlevel=-90.0\nLTElevel=-83.5\ndefaultlvl=-85.0\n"
                               "calctype=TOT\navertype=SMA\naverdepth=1\nmaxskipdistance=0.00\n";
    init_t settings;
    char str_1[100];
    char str_2[100];
    _mkdir("Output_CSV_files");      /* was system("mkdir ..."): a whole shell per folder */
    _mkdir("Output_KML_files");
    system("cls");

    FILE* instr = fopen("Инструкция.txt", "w");
    if(instr == NULL)
    {
        printf("Ошибка сохранения файла Инструкция.txt\n");
            perror("Инструкция.txt");
            clean_stdin();
            getchar();
            exit(1);
    }
    fputs(instruction_window, instr);   /* text, not a format string */
    fputs(instruction_console, instr);
    fputs(instruction_terms, instr);
    fputs(instruction_contact, instr);
    fclose(instr);

    FILE* set_file = fopen("settings.ini", "r");
    if(set_file == NULL)
    {
        set_file = fopen("settings.ini", "w");
        if(set_file == NULL) {
            printf("Ошибка создания файла settings.ini\n");
            perror("settings.ini");
            clean_stdin();
            getchar();
            exit(1);
        }
        fputs(init_string, set_file);   /* text, not a format string */
        fclose(set_file);
    } 
    set_file = fopen("settings.ini", "r");
    if(set_file == NULL)
    {
        printf("Ошибка открытия файла settings.ini\n");
            perror("settings.ini");
            clean_stdin();
            getchar();
            exit(1);
    }

    int test = fscanf(set_file,"[OPTIONS]\nfilflag=%d\nGSMlevel=%lf\nUMTSlevel=%lf\nLTElevel=%lf\ndefaultlvl=%lf\ncalctype=%99s\navertype=%99s\naverdepth=%d\nmaxskipdistance=%lf\n", 
        &settings.fillinflag, &settings.GSMcoveragelvl, &settings.UMTScoveragelvl, &settings.LTEcoveragelvl, &settings.defaultcovlvl, str_1, str_2, &settings.avgdepth, &settings.maxskipdist);
    if(test != 9)
    {
        printf("Ошибка чтения файла настроек settings.ini.\nИсправьте данные в файле или удалите его и перезапустите программу!\n");
        clean_stdin();
        getchar();
        exit(1);
    }   
    if((strstr(str_1, "TOT") != NULL) || (strstr(str_1, "TOTAL") != NULL) ) {
        settings.covercalctype = total;
    }
    else {
        if((strstr(str_1, "SEP") != NULL) || (strstr(str_1, "SEPARATE") != NULL)) {
            settings.covercalctype = separate;}
        else {
            printf("Ошибка чтения settings.ini,\n расчет зон покрытия по умолчанию - общий\n");
            settings.covercalctype = total;
        }
    }
    if(strstr(str_2, "SMA") != NULL) {
        settings.avgtype = sma;}
    else {
        if((strstr(str_2, "MED") != NULL) || (strstr(str_2, "MEDIAN") != NULL)) {
            settings.avgtype = median;}
        else {
            printf("Ошибка чтения settings.ini,\n расчет среднего уровня по умолчанию - медиана\n");
            settings.avgtype = median;
        }
    }
    fclose(set_file);
    if(settings.avgdepth > 100) {settings.avgdepth = 100;}
    if(settings.avgdepth < 1) {settings.avgdepth = 1;}   /* 1 - без усреднения */
    if(settings.maxskipdist < 0.0) {settings.maxskipdist = 0.0;}
    if((settings.fillinflag < 0) || (settings.fillinflag > 1)) {settings.fillinflag = 1;}
    return settings;
}

/*------------------------------------------MAIN-------------------------------------------------*/
int main(void)
{   
    setlocale(LC_CTYPE, "");   // console output in the system codepage
    init_t settings = initialization();
    int choice = 0;
    char file_name[200];
    folder_t folder[MAX_FOLDERS] = {0};
	int folder_quantity = 0;

    
#if 1
    printf("Параметры работы программы:\n");
    if(settings.fillinflag) printf("  Автозаполнение пустых точек - ВКЛЮЧЕНО\n"); else printf("  Автозаполнение пустых точек - ВЫКЛЮЧЕНО\n");
    printf("\n  Пороговый уровень сигнала\n  GSM - %.2f dBm\n  UMTS - %.2f dBm\n  LTE - %.2f dBm\n  default - %.2f dBm\n", 
        settings.GSMcoveragelvl,settings.UMTScoveragelvl, settings.LTEcoveragelvl, settings.defaultcovlvl);
    if(settings.covercalctype == total) printf("\n  Оформление результата анализа покрытия:\n  - одна общая таблица содержащая участки с полным отсутствием связи для всех стандартов всех операторов\n");
    else printf("\n  Оформление результата анализа покрытия:\n  - несколько таблиц, каждая содержит участки АД без связи для одного стандарта одного оператора\n");
    if(settings.avgtype == median) printf("\n  Способ нахождения среднего уровня сигнала - медиана\n");
    else printf("\n  Способ нахождения среднего уровня сигнала - скользящее среднее\n");
    printf("  Количество точек для рассчета среднего уровня - %d\n", settings.avgdepth);
    printf("  Окно сглаживания покрытия по расстоянию - %.3f км\n\n\n", settings.maxskipdist);
#endif 

#if 1
    while(1)
    {
        printf("Выберите действие:\n1 - открыть kml-файл\n2 - открыть csv-файл\n3 - выход\nВведите номер выбранного действия:\n");
        if(scanf("%d", &choice) != 1)   /* a letter used to leave the character in the buffer
                                           and the loop printed the error message forever */
        {
            clean_stdin();
            printf("Нужно ввести число. Попробуйте ещё раз\n");
            continue;
        }

        if( choice == 1) 
        {
            clean_stdin();
            printf("Введите имя файла:\n");
            scanf("%199s", file_name);
            folder_quantity = openkml(file_name, folder);
            savecsv(file_name, folder, folder_quantity, settings.fillinflag);
            foldermemfree(folder, folder_quantity);
            folder_quantity = opencsv(file_name, folder);
            printkmldata(folder, folder_quantity, 0, 0);
            getaverlvl(folder,folder_quantity, settings.avgdepth, settings.avgtype);
            createcovtab(folder, folder_quantity, settings);
            savekml(file_name,folder, folder_quantity);
            savecsvcovchart(file_name, folder, folder_quantity);
            savecsvcovtab(file_name, folder, folder_quantity, settings);
            if(settings.covercalctype == total) savekml_test(file_name, folder, folder_quantity);
            printcoveragetable(folder, folder_quantity, settings);
            break;
        } 

        if( choice == 2) 
        {
            clean_stdin();
            printf("Введите имя файла:\n");
            scanf("%199s", file_name);
            folder_quantity = opencsv(file_name, folder);
            printkmldata(folder, folder_quantity, 0, 0);
            savekml(file_name,folder, folder_quantity);
            getaverlvl(folder,folder_quantity, settings.avgdepth, settings.avgtype);
            createcovtab(folder, folder_quantity, settings);
            savecsvcovchart(file_name, folder, folder_quantity);
            savecsvcovtab(file_name, folder, folder_quantity, settings);
            if(settings.covercalctype == total) savekml_test(file_name, folder, folder_quantity);
            printcoveragetable(folder, folder_quantity, settings);
            break;
        } 
        if( choice == 3) 
        {
            break;
        } 
        printf("Что-то пошло не так, попробуйте снова\n");
    }
#endif    

    foldermemfree(folder, MAX_FOLDERS);   /* the reserved slot holds memory too */

    clean_stdin();
    printf("\nНажмите Enter для выхода.\n");
    getchar();
    return 0;
}