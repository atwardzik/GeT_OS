add_library(kernel OBJECT
        memory.c
        proc.c
        ctx.S
        isr.S
        time.c
        tty.c

        syscall_table.c
        resources.c
        signal.c
        network.c
        error.c
        loader.c
)

set_source_files_properties(memory.c PROPERTIES COMPILE_FLAGS -Wno-pointer-arith)
set_source_files_properties(proc.c PROPERTIES COMPILE_FLAGS -Wno-pointer-arith)
set_source_files_properties(proc.c PROPERTIES COMPILE_FLAGS -Wno-int-conversion)

target_include_directories(kernel PUBLIC ${CMAKE_SOURCE_DIR}/include/kernel)
target_include_directories(kernel PUBLIC ${CMAKE_SOURCE_DIR}/include)
