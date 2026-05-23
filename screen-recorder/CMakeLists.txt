cmake_minimum_required(VERSION 3.16)
project(ScreenRecorder VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Static linking for MinGW
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")

# Optimization flags for i5-5300U (Broadwell, x86-64)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -flto -march=x86-64 -mtune=generic")
set(CMAKE_C_FLAGS_RELEASE "-O3 -flto -march=x86-64 -mtune=generic")

# Enable LTO for release builds
include(CheckIPOSupported)
check_ipo_supported(RESULT lpo_supported OUTPUT lpo_output)
if(lpo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
else()
    message(WARNING "LTO not supported: ${lpo_output}")
endif()

# Windows-specific definitions
add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -DUNICODE -D_UNICODE)

# FFmpeg paths - adjust these to your installation
# Option 1: Use environment variable FFMPEG_DIR
# Option 2: Use system-wide installation in C:/msys64/mingw64
# Option 3: Set manually via -DFFMPEG_DIR=C:/path/to/ffmpeg

set(FFMPEG_DIR "$ENV{FFMPEG_DIR}" CACHE PATH "FFmpeg installation directory")

# Find FFmpeg components
find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h
    HINTS ${FFMPEG_DIR}/include /usr/include /usr/local/include C:/msys64/mingw64/include)
find_path(AVFORMAT_INCLUDE_DIR libavformat/avformat.h
    HINTS ${FFMPEG_DIR}/include /usr/include /usr/local/include C:/msys64/mingw64/include)
find_path(AVUTIL_INCLUDE_DIR libavutil/avutil.h
    HINTS ${FFMPEG_DIR}/include /usr/include /usr/local/include C:/msys64/mingw64/include)
find_path(SWSCALE_INCLUDE_DIR libswscale/swscale.h
    HINTS ${FFMPEG_DIR}/include /usr/include /usr/local/include C:/msys64/mingw64/include)

find_library(AVCODEC_LIBRARY avcodec
    HINTS ${FFMPEG_DIR}/lib /usr/lib /usr/local/lib C:/msys64/mingw64/lib)
find_library(AVFORMAT_LIBRARY avformat
    HINTS ${FFMPEG_DIR}/lib /usr/lib /usr/local/lib C:/msys64/mingw64/lib)
find_library(AVUTIL_LIBRARY avutil
    HINTS ${FFMPEG_DIR}/lib /usr/lib /usr/local/lib C:/msys64/mingw64/lib)
find_library(SWSCALE_LIBRARY swscale
    HINTS ${FFMPEG_DIR}/lib /usr/lib /usr/local/lib C:/msys64/mingw64/lib)
find_library(SWRESAMPLE_LIBRARY swresample
    HINTS ${FFMPEG_DIR}/lib /usr/lib /usr/local/lib C:/msys64/mingw64/lib)

# Check for QSV support (optional)
find_library(MFX_LIBRARY mfx
    HINTS ${FFMPEG_DIR}/lib /usr/lib /usr/local/lib C:/msys64/mingw64/lib)

# Validate FFmpeg installation
if(NOT AVCODEC_INCLUDE_DIR OR NOT AVFORMAT_INCLUDE_DIR OR NOT AVUTIL_INCLUDE_DIR)
    message(FATAL_ERROR 
        "FFmpeg headers not found. Please install FFmpeg development packages:\n"
        "  MSYS2: pacman -S mingw-w64-x86_64-ffmpeg\n"
        "  Or set FFMPEG_DIR to your FFmpeg installation")
endif()

if(NOT AVCODEC_LIBRARY OR NOT AVFORMAT_LIBRARY OR NOT AVUTIL_LIBRARY)
    message(FATAL_ERROR 
        "FFmpeg libraries not found. Please ensure static FFmpeg libraries are available:\n"
        "  Required: avcodec.a, avformat.a, avutil.a, swscale.a")
endif()

# Report QSV availability
if(MFX_LIBRARY)
    message(STATUS "Intel MFX/QSV library found - QSV encoding enabled")
    add_definitions(-DHAVE_QSV)
    set(HAVE_QSV TRUE)
else()
    message(WARNING "Intel MFX/QSV library not found - will fallback to libx264 only")
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/capture.cpp
    src/encoder.cpp
    src/muxer.cpp
    src/tray.cpp
)

set(HEADERS
    src/utils.h
    src/queue.h
    src/tray.h
    src/capture.h
    src/encoder.h
    src/muxer.h
)

# Create executable
add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${AVCODEC_INCLUDE_DIR}
    ${AVFORMAT_INCLUDE_DIR}
    ${AVUTIL_INCLUDE_DIR}
    ${SWSCALE_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link libraries (order matters for static linking)
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${AVFORMAT_LIBRARY}
    ${AVCODEC_LIBRARY}
    ${AVUTIL_LIBRARY}
    ${SWSCALE_LIBRARY}
    ${SWRESAMPLE_LIBRARY}
    
    # Windows system libraries
    ws2_32      # Winsock (required by FFmpeg)
    bcrypt      # Cryptography (required by FFmpeg)
    secur32     # Security (required by FFmpeg)
    strmiids    # DirectShow (for DXGI)
    ole32       # COM (for DXGI)
    uuid        # UUID support
    d3d11       # Direct3D 11
    dxgi        # DXGI
    gdi32       # GDI (fallback capture)
    user32      # User interface
    shell32     # Shell functions
    comctl32    # Common controls
    
    # Threading
    pthread     # POSIX threads (MinGW)
)

# Add QSV library if available
if(HAVE_QSV)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${MFX_LIBRARY})
endif()

# Static linking flags for final executable
set_target_properties(${PROJECT_NAME} PROPERTIES
    LINK_FLAGS "-static -Wl,-Bstatic -Wl,--whole-archive -lpthread -Wl,--no-whole-archive"
)

# Output directory
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Installation
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# Print configuration summary
message(STATUS "")
message(STATUS "=== Screen Recorder Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "FFmpeg include: ${AVCODEC_INCLUDE_DIR}")
message(STATUS "FFmpeg libs: ${AVCODEC_LIBRARY}")
message(STATUS "QSV support: ${HAVE_QSV}")
message(STATUS "Output: ${CMAKE_BINARY_DIR}/bin/${PROJECT_NAME}.exe")
message(STATUS "=====================================")
