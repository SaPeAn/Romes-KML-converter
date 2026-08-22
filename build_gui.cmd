@echo off
rem Builds the windowed version. Pure WinAPI - no external libraries, no dlls to ship.
rem The console version is built separately:
rem     gcc -std=c99 src\*.c -o RomesKML_converter.exe

windres src\gui\app.rc -O coff -o src\gui\app.res
if errorlevel 1 (echo RESOURCE STEP FAILED & exit /b 1)

gcc -DGUI_BUILD -std=gnu99 -O2 ^
    src/open_file.c src/kml_struct.c src/save_file.c ^
    src/gui/app_core.c src/gui/html_report.c src/gui/map_view.c src/gui/gui_main.c src/gui/app.res ^
    -o RomesCov.exe ^
    -lcomctl32 -lcomdlg32 -lgdi32 -luser32 -lole32 -lshell32 -mwindows

if errorlevel 1 (echo BUILD FAILED & exit /b 1)
echo Done: RomesCov.exe
