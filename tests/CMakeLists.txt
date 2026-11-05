add_subdirectory(userspaceVi)

add_library(tests OBJECT
        tests.c
)

target_include_directories(tests PUBLIC ${CMAKE_SOURCE_DIR}/include)