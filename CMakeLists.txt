cmake_minimum_required(VERSION 3.15)
project(ScreenRecorder VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows-specific settings
if(WIN32)
    # Static runtime library for standalone .exe
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    
    # Release build optimizations for Broadwell
    if(MSVC)
        # /MT - Static runtime
        # /O2 - Maximize speed
        # /arch:AVX2 - Enable AVX2 instructions (Broadwell supports AVX2)
        # /GL - Whole program optimization
        # /Gy - Function-level linking
        # /Oi - Intrinsic functions
        add_compile_options(
            /MT
            /O2
            /arch:AVX2
            /GL
            /Gy
            /Oi
            /W4
            /WX-
            /MP           # Multi-processor compilation
            /Zc:inline    # Remove unreferenced COMDATs
            /Qpar         # Auto-parallelization
        )
        
        # Linker options
        add_link_options(
            /LTCG         # Link-time code generation
            /OPT:REF      # Eliminate unreferenced data
            /OPT:ICF      # Identical COMDAT folding
            /INCREMENTAL:NO
        )
        
        # Release-specific flags
        add_compile_options($<$<CONFIG:Release>:/DNDEBUG>)
    endif()
endif()

# Find FFmpeg packages
# Set FFMPEG_ROOT environment variable or pass -DFFMPEG_ROOT=<path>
find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h PATHS $ENV{FFMPEG_ROOT}/include NO_DEFAULT_PATH)
find_path(AVFORMAT_INCLUDE_DIR libavformat/avformat.h PATHS $ENV{FFMPEG_ROOT}/include NO_DEFAULT_PATH)
find_path(AVUTIL_INCLUDE_DIR libavutil/avutil.h PATHS $ENV{FFMPEG_ROOT}/include NO_DEFAULT_PATH)
find_path(SWSCALE_INCLUDE_DIR libswscale/swscale.h PATHS $ENV{FFMPEG_ROOT}/include NO_DEFAULT_PATH)

find_library(AVCODEC_LIBRARY avcodec PATHS $ENV{FFMPEG_ROOT}/lib NO_DEFAULT_PATH)
find_library(AVFORMAT_LIBRARY avformat PATHS $ENV{FFMPEG_ROOT}/lib NO_DEFAULT_PATH)
find_library(AVUTIL_LIBRARY avutil PATHS $ENV{FFMPEG_ROOT}/lib NO_DEFAULT_PATH)
find_library(SWSCALE_LIBRARY swscale PATHS $ENV{FFMPEG_ROOT}/lib NO_DEFAULT_PATH)
find_library(SWRESAMPLE_LIBRARY swresample PATHS $ENV{FFMPEG_ROOT}/lib NO_DEFAULT_PATH)

# Check for QSV support
find_path(AVHWCONTEXT_INCLUDE_DIR libavutil/hwcontext_qsv.h PATHS $ENV{FFMPEG_ROOT}/include NO_DEFAULT_PATH)

if(NOT AVCODEC_INCLUDE_DIR OR NOT AVCODEC_LIBRARY)
    message(FATAL_ERROR 
        "FFmpeg not found! Please install FFmpeg with QSV support.\n"
        "Set FFMPEG_ROOT environment variable to FFmpeg installation directory.\n"
        "Download from: https://github.com/BtbN/FFmpeg-Builds/releases\n"
        "Choose: ffmpeg-master-latest-win64-gpl-shared.zip (or static for static linking)")
endif()

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${AVCODEC_INCLUDE_DIR}
    ${AVFORMAT_INCLUDE_DIR}
    ${AVUTIL_INCLUDE_DIR}
    ${SWSCALE_INCLUDE_DIR}
    ${AVHWCONTEXT_INCLUDE_DIR}
)

# Source files
set(SOURCES
    src/main.cpp
    src/capture_dxgi.cpp
    src/capture_gdi.cpp
    src/encoder_qsv.cpp
    src/muxer.cpp
)

# Header files
set(HEADERS
    include/ring_buffer.h
    include/capture_dxgi.h
    include/capture_gdi.h
    include/encoder_qsv.h
    include/muxer.h
    include/utils.h
)

# Create executable
add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS})

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${AVCODEC_LIBRARY}
    ${AVFORMAT_LIBRARY}
    ${AVUTIL_LIBRARY}
    ${SWSCALE_LIBRARY}
    ${SWRESAMPLE_LIBRARY}
    d3d11
    dxgi
    dwmapi
)

# Compile definitions for QSV support
target_compile_definitions(${PROJECT_NAME} PRIVATE
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    HAS_QSV_SUPPORT
)

# Check for AVX2 support (Broadwell has AVX2)
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("/arch:AVX2" COMPILER_SUPPORTS_AVX2)
if(COMPILER_SUPPORTS_AVX2)
    message(STATUS "AVX2 support enabled for Broadwell optimization")
endif()

# Installation rules
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
install(DIRECTORY ${FFMPEG_ROOT}/bin/ DESTINATION bin 
        FILES_MATCHING PATTERN "*.dll" 
        OPTIONAL)

# Print configuration summary
message(STATUS "")
message(STATUS "=== Screen Recorder Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "FFmpeg root: $ENV{FFMPEG_ROOT}")
message(STATUS "AVCodec: ${AVCODEC_LIBRARY}")
message(STATUS "AVFormat: ${AVFORMAT_LIBRARY}")
message(STATUS "QSV header found: ${AVHWCONTEXT_INCLUDE_DIR}")
message(STATUS "Output: ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.exe")
message(STATUS "")
