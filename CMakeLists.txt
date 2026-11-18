cmake_minimum_required(VERSION 3.25)

# CUDA is optional - detect before project() to set languages correctly
option(ENABLE_CUDA "Enable CUDA GPU acceleration" ON)

if(ENABLE_CUDA)
    include(CheckLanguage)
    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        project(CUDAMediaUpscaler LANGUAGES CXX CUDA)
        set(CMAKE_CUDA_STANDARD 17)
        set(CMAKE_CUDA_STANDARD_REQUIRED ON)
        add_compile_definitions(HAS_CUDA)
        message(STATUS "CUDA enabled: ${CMAKE_CUDA_COMPILER}")
    else()
        project(CUDAMediaUpscaler LANGUAGES CXX)
        message(WARNING "CUDA compiler not found - building CPU-only")
        set(ENABLE_CUDA OFF)
    endif()
else()
    project(CUDAMediaUpscaler LANGUAGES CXX)
    message(STATUS "CUDA disabled by user - building CPU-only")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type default
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O2)
    endif()
endif()

# ============================================================
# Dependencies
# ============================================================

# Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
qt_standard_project_setup()
message(STATUS "Qt6: ${Qt6_VERSION}")

# CUDA Toolkit (optional)
if(ENABLE_CUDA)
    find_package(CUDAToolkit REQUIRED)
    message(STATUS "CUDA Toolkit: ${CUDAToolkit_VERSION}")
endif()

# OpenMP (optional, for CPU fallback parallelism)
find_package(OpenMP)

# ============================================================
# Source files
# ============================================================

# Core engine
set(CORE_SOURCES
    src/core/upscaler.cpp
    src/core/cpu_backend.cpp
)
set(CORE_HEADERS
    src/core/types.hpp
    src/core/upscaler.hpp
    src/core/cpu_backend.hpp
)

if(ENABLE_CUDA)
    list(APPEND CORE_SOURCES src/core/cuda_kernels.cu)
    list(APPEND CORE_HEADERS src/core/cuda_kernels.cuh)
endif()

# Media processors
set(MEDIA_SOURCES
    src/media/image_io.cpp
    src/media/video_processor.cpp
    src/media/gif_processor.cpp
)
set(MEDIA_HEADERS
    src/media/image_io.hpp
    src/media/video_processor.hpp
    src/media/gif_processor.hpp
    src/media/temp_dir.hpp
    src/media/subprocess.hpp
)

# UI (Qt MOC requires special handling)
set(UI_SOURCES
    src/ui/mainwindow.cpp
    src/ui/processingworker.cpp
    src/ui/theme.cpp
)
set(UI_HEADERS
    src/ui/mainwindow.hpp
    src/ui/processingworker.hpp
    src/ui/theme.hpp
)

# ============================================================
# Main executable
# ============================================================

qt_add_executable(cuda-media-upscaler
    src/main.cpp
    ${CORE_SOURCES}
    ${CORE_HEADERS}
    ${MEDIA_SOURCES}
    ${MEDIA_HEADERS}
    ${UI_SOURCES}
    ${UI_HEADERS}
)

target_include_directories(cuda-media-upscaler PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link Qt6
target_link_libraries(cuda-media-upscaler PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

# Link CUDA
if(ENABLE_CUDA)
    target_link_libraries(cuda-media-upscaler PRIVATE
        CUDA::cudart
    )
    # Set CUDA architectures (common desktop GPUs)
    set_target_properties(cuda-media-upscaler PROPERTIES
        CUDA_ARCHITECTURES "75;80;86;89;90"
    )
    # Suppress -Wpedantic for CUDA-generated code (nvcc line directives trigger it)
    set_source_files_properties(src/core/cuda_kernels.cu PROPERTIES
        COMPILE_OPTIONS "-Wno-pedantic"
    )
endif()

# Link OpenMP
if(OpenMP_CXX_FOUND)
    target_link_libraries(cuda-media-upscaler PRIVATE OpenMP::OpenMP_CXX)
endif()

# Platform-specific
if(UNIX AND NOT APPLE)
    target_link_libraries(cuda-media-upscaler PRIVATE pthread)
endif()

# ============================================================
# Install
# ============================================================

install(TARGETS cuda-media-upscaler
    RUNTIME DESTINATION bin
)

install(FILES cuda-media-upscaler.desktop
    DESTINATION share/applications
)

# ============================================================
# Summary
# ============================================================

message(STATUS "")
message(STATUS "=== Build Configuration ===")
message(STATUS "  Build type:    ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard:  ${CMAKE_CXX_STANDARD}")
message(STATUS "  CUDA:          ${ENABLE_CUDA}")
message(STATUS "  OpenMP:        ${OpenMP_CXX_FOUND}")
message(STATUS "  Qt6:           ${Qt6_VERSION}")
if(ENABLE_CUDA)
    message(STATUS "  CUDA Toolkit:  ${CUDAToolkit_VERSION}")
endif()
message(STATUS "===========================")
message(STATUS "")
