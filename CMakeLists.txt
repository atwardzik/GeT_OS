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

if (DEFINED STANDALONE)
    add_compile_definitions(STANDALONE=1)
endif ()

if (DEFINED TESTS)
    add_compile_definitions(TESTS=1)
endif ()

project(os LANGUAGES C ASM)

add_compile_options(
        "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=."
)

include_directories(${CMAKE_SOURCE_DIR} include tests)
link_directories(${CMAKE_SOURCE_DIR} include tests)

add_subdirectory(src/drivers)
add_subdirectory(src/kernel)
add_subdirectory(src/fs)
add_subdirectory(src/kstdlib)
add_subdirectory(src/init)

add_executable(os
        src/start.S
        src/config.c
        src/main.c
        $<TARGET_OBJECTS:drivers>
        $<TARGET_OBJECTS:kernel>
        $<TARGET_OBJECTS:fs>
        $<TARGET_OBJECTS:klibc>
        $<TARGET_OBJECTS:init>
)

if (DEFINED TESTS)
    add_subdirectory(tests)
    target_sources(os PRIVATE
            $<TARGET_OBJECTS:tests>
            $<TARGET_OBJECTS:vi> # TEMPORARILY FOR DEVELOPMENT
    )
endif ()

target_link_options(os PRIVATE
        -T${LINKER_SCRIPT}
        -nostdlib
)

if (DEFINED STANDALONE)
    set_target_properties(os PROPERTIES
            LINK_DEPENDS ${CMAKE_SOURCE_DIR}/${LINKER_SCRIPT}
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
            OUTPUT_NAME "kernel_debug_standalone.elf"
    )
elseif (NOT DEFINED STANDALONE)
    set_target_properties(os PROPERTIES
            LINK_DEPENDS ${CMAKE_SOURCE_DIR}/${LINKER_SCRIPT}
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
            OUTPUT_NAME "kernel_debug.elf"
    )
    add_custom_command(
            TARGET os
            POST_BUILD
            COMMAND ${CMAKE_OBJCOPY} -O binary
            "$<TARGET_FILE:os>"
            "${CMAKE_SOURCE_DIR}/bin/kernel.img"
    )
endif ()