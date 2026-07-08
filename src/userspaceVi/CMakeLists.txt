add_library(vi OBJECT

        file_editor.c
        visual_editor.c
        cmd_runner.c
        screen_controller.c
)

target_include_directories(vi PUBLIC ${CMAKE_SOURCE_DIR}/include)
