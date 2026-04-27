@echo off
setlocal enabledelayedexpansion

set "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"

if not exist "!MSBUILD!" (
    echo Erro: MSBuild nao encontrado
    exit /b 1
)

echo Compilando Driver Release^|x64...
"!MSBUILD!" "Driver\Driver.vcxproj" -p:Configuration=Release -p:Platform=x64 -v:minimal
if %errorlevel% neq 0 ( echo Erro na compilacao do Driver! & exit /b 1 )

copy /Y "Loader\rsc\Driver.sys" "IrisCore\rsc\Driver.sys" >nul
if %errorlevel% neq 0 ( echo Erro ao copiar Driver.sys! & exit /b 1 )
echo Driver.sys atualizado

echo.
echo Compilando Iris Release^|x64...
"!MSBUILD!" "Iris\Iris.vcxproj" -p:Configuration=Release -p:Platform=x64 -v:minimal
if %errorlevel% neq 0 ( echo Erro na compilacao do Iris! & exit /b 1 )

echo.
echo Compilando IrisCore Release^|x64...
"!MSBUILD!" "IrisCore\IrisCore.vcxproj" -p:Configuration=Release -p:Platform=x64 -v:minimal
if %errorlevel% neq 0 ( echo Erro na compilacao do IrisCore! & exit /b 1 )

echo.
echo Build concluido com sucesso
echo Saida em: x64\
