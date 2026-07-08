cmake_minimum_required(VERSION 3.30)
set(CMAKE_TOOLCHAIN_FILE pico_toolchain.cmake)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_definitions(VGA_640x480)
if (DEFINED ARCH_RP2040)
    add_compile_definitions(ARCH_RP2040=1)
    set(LINKER_SCRIPT "linker_rp2040.ld")

elseif (DEFINED ARCH_RP2350)
    add_compile_definitions(ARCH_RP2350=1)
    set(LINKER_SCRIPT "linker_rp2350.ld")

else ()
    message(FATAL_ERROR "Target architecture should be defined, add -DARCH_RP2040 or -DARCH_RP2350")
endif ()

project(os LANGUAGES C ASM)

include_directories(${CMAKE_SOURCE_DIR} include tests)
link_directories(${CMAKE_SOURCE_DIR} include tests)

add_subdirectory(src/drivers)
add_subdirectory(src/kernel)
add_subdirectory(src/fs)
add_subdirectory(src/kstdlib)
add_subdirectory(src/userspaceVi) # TEMPORARILY FOR DEVELOPMENT

add_executable(os
        src/start.S
        src/config.c
        src/main.c
        $<TARGET_OBJECTS:drivers>
        $<TARGET_OBJECTS:kernel>
        $<TARGET_OBJECTS:fs>
        $<TARGET_OBJECTS:vi>
        $<TARGET_OBJECTS:klibc>
        src/archive_loader.S # PUT THIS ARCHIVE LOADER ALWAYS AT THE END (raw files copied)!!!
)

target_link_options(os PRIVATE
        -T${LINKER_SCRIPT}
        -nostdlib
)

set_target_properties(os PROPERTIES
        LINK_DEPENDS ${CMAKE_SOURCE_DIR}/${LINKER_SCRIPT}
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
        OUTPUT_NAME "kernel_debug.elf"
)
