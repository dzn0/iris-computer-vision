@echo off
setlocal enabledelayedexpansion

set "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
set "DEPS_URL=https://github.com/dzn0/iris-computer-vision/releases/download/deps/deps.zip"
set "DEPS_MARKER=IrisCore\rsc\onnxruntime.dll"
set "DEPS_ZIP=%TEMP%\iris_deps.zip"
set "DEPS_DIR=IrisCore\rsc"

if not exist "!MSBUILD!" (
    echo Erro: MSBuild nao encontrado em:
    echo   !MSBUILD!
    exit /b 1
)

REM ── [1] Bootstrap: baixar DLLs + modelo se ausentes ─────────────────────────
if not exist "!DEPS_MARKER!" (
    echo.
    echo  [*] Runtime deps ausentes. Baixando deps.zip ^(~69 MB^)...
    echo.

    powershell -NoProfile -Command "$ErrorActionPreference='Stop'; try { $wc=New-Object System.Net.WebClient; $wc.DownloadFile('%DEPS_URL%','%DEPS_ZIP%'); Expand-Archive -Path '%DEPS_ZIP%' -DestinationPath '%DEPS_DIR%' -Force; Remove-Item '%DEPS_ZIP%' -Force; Write-Host ' [+] Deps extraidas.' } catch { Write-Host ' [-] Falha:' $_.Exception.Message; exit 1 }"

    if !errorlevel! neq 0 (
        echo.
        echo  [-] Nao foi possivel baixar as dependencias automaticamente.
        echo      Baixe manualmente: !DEPS_URL!
        echo      Extraia em: !DEPS_DIR!\
        exit /b 1
    )
    echo.
)

REM ── [2] Compilar Driver ──────────────────────────────────────────────────────
echo  Compilando Driver Release^|x64...
"!MSBUILD!" "Driver\Driver.vcxproj" /p:Configuration=Release /p:Platform=x64 /v:minimal
if !errorlevel! neq 0 (
    echo  [-] Erro na compilacao do Driver.
    exit /b 1
)

copy /Y "Loader\rsc\Driver.sys" "IrisCore\rsc\Driver.sys" >nul
if !errorlevel! neq 0 (
    echo  [-] Erro ao copiar Driver.sys
    exit /b 1
)
echo  [+] Driver.sys atualizado

REM ── [3] Compilar Iris ────────────────────────────────────────────────────────
echo.
echo  Compilando Iris Release^|x64...
"!MSBUILD!" "Iris\Iris.vcxproj" /p:Configuration=Release /p:Platform=x64 /v:minimal
if !errorlevel! neq 0 (
    echo  [-] Erro na compilacao do Iris.
    exit /b 1
)

REM ── [4] Compilar IrisCore ────────────────────────────────────────────────────
echo.
echo  Compilando IrisCore Release^|x64...
"!MSBUILD!" "IrisCore\IrisCore.vcxproj" /p:Configuration=Release /p:Platform=x64 /v:minimal
if !errorlevel! neq 0 (
    echo  [-] Erro na compilacao do IrisCore.
    exit /b 1
)

echo.
echo  [+] Build concluido com sucesso!
echo      Iris.exe   -> x64\
echo      IrisCore   -> x64\IrisCore.exe
