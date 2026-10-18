set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-none-eabi-gcc CACHE INTERNAL "")
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc CACHE INTERNAL "")
set(CMAKE_OBJCOPY arm-none-eabi-objcopy CACHE INTERNAL "")

set(CMAKE_C_FLAGS "-Wno-unused-variable -Wno-error=unused-parameter -std=c23 -mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -ffreestanding -mthumb -g -O0" CACHE INTERNAL "")
set(CMAKE_ASM_FLAGS "-Wa,-warn,-fatal-warnings -mcpu=cortex-m33 -mthumb -g" CACHE INTERNAL "")
set(CMAKE_EXE_LINKER_FLAGS "" CACHE INTERNAL "")

set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_ASM_COMPILER_FORCED TRUE)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

#include_directories("/Applications/ArmGNUToolchain/14.2.rel1/arm-none-eabi/include")
#set(CMAKE_FIND_ROOT_PATH "/Applications/ArmGNUToolchain/14.2.rel1/arm-none-eabi/arm-none-eabi/include")

SET(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
