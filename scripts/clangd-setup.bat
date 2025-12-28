@ECHO OFF

set CurrDirName=%cd%

(
    echo CompileFlags:
    echo   Add:
    echo     - -I/lib/llvm-18/include/
    echo     - -I%CurrDirName%\include
    echo     - -I%CurrDirName%\deps\quickjs
    echo     - -std=c++20
) > .clangd