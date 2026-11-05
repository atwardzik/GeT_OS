add_library(init OBJECT
        init.c
        archive_loader.c
        time.c
)

target_include_directories(init PUBLIC ${CMAKE_SOURCE_DIR}/include)