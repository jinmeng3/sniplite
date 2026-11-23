@echo off
setlocal
cd /d "%~dp0"
if exist "%~dp0w64devkit\bin\gcc.exe" set "PATH=%~dp0w64devkit\bin;%PATH%"
where git >nul 2>nul || (echo git is required to fetch the pinned CrispEmbed source & exit /b 1)
where cmake >nul 2>nul || (echo CMake is required to build the CPU GGML runtime & exit /b 1)
where ninja >nul 2>nul || (echo Ninja is required to build the CPU GGML runtime & exit /b 1)
set "SRC=build\crispembed-cpu-src"
set "OUT=build\crispembed-cpu-build"
set "RUNTIME=build\cpu-runtime"
if not exist "%SRC%\CMakeLists.txt" (
  git clone --depth 1 --branch v0.17.19 --recurse-submodules https://github.com/CrispStrobe/CrispEmbed.git "%SRC%" || exit /b 1
)
for /f "delims=" %%v in ('git -C "%SRC%" describe --tags --exact-match 2^>nul') do set "SRC_VERSION=%%v"
if not "%SRC_VERSION%"=="v0.17.19" (echo Expected CrispEmbed source tag v0.17.19 in %SRC% & exit /b 1)
git -C "%SRC%" submodule update --init --recursive || exit /b 1
cmake -S "%SRC%" -B "%OUT%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DGGML_VULKAN=OFF -DGGML_CUDA=OFF -DGGML_METAL=OFF -DGGML_NATIVE=OFF -DCRISPEMBED_NATIVE=OFF -DCRISPEMBED_BUILD_SHARED=ON -DGGML_SSE42=OFF -DGGML_AVX=OFF -DGGML_AVX2=OFF -DGGML_BMI2=OFF -DGGML_FMA=OFF -DGGML_F16C=OFF || exit /b 1
cmake --build "%OUT%" --target ggml ggml-base ggml-cpu --parallel 8 || exit /b 1
if not exist "%RUNTIME%" mkdir "%RUNTIME%" || exit /b 1
for %%f in (ggml.dll ggml-base.dll ggml-cpu.dll) do copy /y "%OUT%\bin\%%f" "%RUNTIME%\%%f" >nul || exit /b 1
echo CPU GGML runtime staged in %RUNTIME%.
set "OUT_AVX2=build\crispembed-cpu-avx2-build"
set "RUNTIME_AVX2=build\cpu-runtime-avx2"
cmake -S "%SRC%" -B "%OUT_AVX2%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DGGML_VULKAN=OFF -DGGML_CUDA=OFF -DGGML_METAL=OFF -DGGML_NATIVE=OFF -DCRISPEMBED_NATIVE=OFF -DCRISPEMBED_BUILD_SHARED=ON -DGGML_SSE42=OFF -DGGML_AVX=OFF -DGGML_AVX2=ON -DGGML_BMI2=OFF -DGGML_FMA=ON -DGGML_F16C=ON || exit /b 1
cmake --build "%OUT_AVX2%" --target ggml ggml-base ggml-cpu --parallel 8 || exit /b 1
if not exist "%RUNTIME_AVX2%" mkdir "%RUNTIME_AVX2%" || exit /b 1
for %%f in (ggml.dll ggml-base.dll ggml-cpu.dll) do copy /y "%OUT_AVX2%\bin\%%f" "%RUNTIME_AVX2%\%%f" >nul || exit /b 1
echo AVX2 CPU GGML runtime staged in %RUNTIME_AVX2%.
