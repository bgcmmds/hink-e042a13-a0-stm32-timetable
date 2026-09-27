cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME timetable)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# ── 调试工具开关 ──────────────────────────────────────────────────────────
# 关闭时（默认）debug/ 目录下的代码不参与编译，正式固件不含诊断代码。
# 需要排查屏问题时：cmake --preset Debug -DENABLE_DEBUG_TOOLS=ON
option(ENABLE_DEBUG_TOOLS "编译 debug/ 下的诊断/自检工具" OFF)
if(ENABLE_DEBUG_TOOLS)
    message("Debug tools: ON  (debug/ 目录参与编译)")
else()
    message("Debug tools: OFF (正式固件，不含诊断代码)")
endif()

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    # ── 应用代码（用户自己写的，按分层排列）──
    #   font / font_cn   字库数据（不依赖任何东西）
    #   textlayout       文本测量与折行（纯计算，不碰显存）
    #   epd              屏驱动（依赖 SPI + GPIO）
    #   timetable        课表业务（只用 epd 的画图接口）
    #   my_courses       课表数据（换成你自己的课表）
    #   log              串口打点
    Core/Src/font.c
    Core/Src/font_cn.c
    Core/Src/textlayout.c
    Core/Src/epd.c
    Core/Src/timetable.c
    Core/Src/my_courses.c
    Core/Src/log.c
)

# ── 调试工具（默认不编译，见文件顶部的 ENABLE_DEBUG_TOOLS）──
if(ENABLE_DEBUG_TOOLS)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        debug/epd_test.c
    )
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE debug)
endif()

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
