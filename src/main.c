
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <locale.h>
#include <direct.h>
#include "open_file.h"
#include "save_file.h"
#include "kml_struct.h"
#include "main.h"

static const char instruction[5000] = "\n"
"    Утилита RomesCov предназначена для обработки исходных kml-файлов из ПО R&S Romes\n"
"    содержащих результаты измерения уровней сигналов сетей связи на автомобильных дорогах.\n"
"\n"    
"    Для корректной работы с утилитой в исходном kml-файле помимо параметров мощности по каждому оператору и стандарту связи,\n"
"    должны содержаться данные по GPS: latitude, longitude (добавляются как дополнительные параметры при экспорте в ПО Romes).\n"
"\n"    
"\n"                   
"                   ИНСТРУКЦИЯ\n"
"\n"    
"    После первого запуска утилиты в папке с исполняемым файлом создаются дополнительные элементы:\n"
"\n"    
"    - папка Output_CSV_files - для сохранения результатов обработки исходных данных в формате csv\n"
"    - папка Output_KML_files - для сохранения результатов обработки исходных данных в формате kml\n"
"    - файл settings.ini с настройками утилиты, изначально заданы значения параметров по умолчению, \n"
"      которые можно впоследствии изменить, при перезапуске программа будет брать настройки из файла.\n"
"    - Инструкция.\n"
"\n"   
"    Параметры работы утилиты в файле settings.ini:\n"
"    filflag=1                 - практически всегда должен быть равен \"1\", параметр используется для добавления отсутствующих в исходном kml-файле данных по уровням (особенности конвертации ПО Romes v.21.2)\n"
"    GSMlevel=-85.0            - пороговый уровень сигнала, dBm, для определения покрытия стандартом GSM\n"
"    UMTSlevel=-90.0           - пороговый уровень сигнала, dBm, для определения покрытия стандартом UMTS\n"
"    LTElevel=-83.5            - пороговый уровень сигнала, dBm, для определения покрытия стандартом LTE\n"
"    defaultlvl=-85.0          - пороговый уровень сигнала, dBm, для определения покрытия в случае, если утилита не смогла распознать стандарт связи (добавлен на всякий случай)\n"
"    calctype=TOT              - тип формирования таблиц покрытия: \"SEP\" - несколько таблиц, каждая одного стандарта связи одного оператора; \"TOT\" - одна общая таблица покрытия автодороги\n"
"    avertype=MED              - тип расчета среднего уровня для формирования таблиц покрытия: \"SMA\" - простое среднее, \"MED\" - медианное среднее\n"
"    averdepth=20              - количество точек для расчета среднего (от 2 до 100);\n"
"    maxskipdistance=0.15      - окно сглаживания покрытия по расстоянию, км, учитывается при формировании общей таблицы покрытия (когда calctype=TOT)\n"
"\n"
"    В утилите реализованы два сценария работы: \n"
"    1. конвертация и обработка данных из исходного kml-файла\n"
"    2. обработка данных из csv-файла предварительно созданного утилитой из исходного kml-файла. \n"
"\n"    
"    Исходный kml-файл необходимо поместить в папку с утилитой\n"
"\n"
"    В первом случае поэтапно происходят следующие действия:\n"
"\n"    
"    - преобразование и сохранение в общую csv-таблицу всех необходимых данных из исходного kml-файла;\n"
"    - формирование и сохранение csv-таблиц содержащих результаты расчета не покрытых связью зон (таблицы покрытия, файлы с меткой \"Coverage_\" в названии);\n"
"    - сохранение дополнительных csv-таблиц для просмотра в Excel графиков иллюстрирующих усреднение уровня при формировании таблиц покрытия \n"
"      (вспомогательные данные, могут быть использованы для настройки параметров усреднения, файл с меткой \"_coverchart\" в названии);\n"
"    - преобразование обратно в формат kml исходных данных из общей csv-таблицы с разбивкой на отдельные файлы оператор-стандарт (из файлов убрана лишняя информация,\n"
"      что позволяет быстрее открывать их в ПО GoogleEarth для проверки, в последствии в ПО GoogleEarth можно объединить их обратно в один kml-файл).\n"
"\n"      
"    Во втором случае происходит все то же самое только не из исходного kml-файла, а из предварительно сформированной общей csv-таблицы,\n"
"    что дает возможность редактировать исходные данные в общей csv-таблице перед дальнейшей обработкой.\n"
"\n"
"    Например, если требуется построить общую таблицу покрытия для одного оператора (с учетом всех стандартов связи этого оператора)\n"
"    то нужно сделать следующее:\n"
"    - удалить из csv-файла все лишнее, оставить только данные относящиеся к стандартам связи выбранного оператора и сохранить измененный csv-файл\n"
"    - убедиться, что в настройках утилиты (файл settings.ini) параметр calctype=TOT (формирование общей таблицы покрытия), если нет, прописать calctype=TOT\n"
"    - запустить утилиту и открыть измененный csv-файл\n"
"\n"      
"    При выборе типа формирования таблиц покрытия - общая таблица покрытия (calctype=TOT) - в папку Output_KML_files будут добавлены два kml-файла,\n"
"    один с \"сырыми\" (файл с меткой _rawcov.kml в названии), другой со сглаженными по расстоянию (окно maxskipdistance) данными (файл с меткой _avgcov.kml в названии).\n"
"    В файлах отображено общее покрытие автодороги сотовой связью.\n"
"    С их помощью можно подобрать наиболее подходящее значение окна сглаживания (maxskipdistance) для конкретных исходных данных\n"
"\n\n"
"Вопросы, замечаия, предложения - p.a.samsonov@ya.ru";

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
                               "calctype=TOT\navertype=MED\naverdepth=2\nmaxskipdistance=0.1\n";
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
    fputs(instruction, instr);          /* text, not a format string */
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
    if(settings.avgdepth < 2) {settings.avgdepth = 2;}
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