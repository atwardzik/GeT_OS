MEMORY
{
        code (rx)               : ORIGIN = 0x20000000, LENGTH = 65536
        heap (rwx)              : ORIGIN = 0x20010000, LENGTH = 28416
        stack (rw)              : ORIGIN = 0x20016f00, LENGTH = 8K
        screen_buffer (rw)      : ORIGIN = 0x20018f00, LENGTH = 6400
        vidram (w)              : ORIGIN = 0x2001A800, LENGTH = 153600
        data (rw)               : ORIGIN = 0x20040000, LENGTH = 4K
        bss (rw)                : ORIGIN = 0x20041000, LENGTH = 4K
}

SECTIONS
{
        .text : {
                __text_start = .;
                *(.reset*)
                *(.text*)
                *(.rodata*)
                __text_end = .;
        } > code

        .vectors : {
                . = ALIGN(256);
                __vectors_start = .;
                *(.vectors*)
        } > code

        .data : {
                __data_start = .;
                *(.data)
                *(.data*)
                __data_end = .;
        } > data

        .bss : {
                __bss_start = .;
                *(.bss)
                *(.bss*)
                *(COMMON*)
                __bss_end = .;
        } > bss

        __heap_start__                  = ORIGIN(heap);
        __heap_length__                 = LENGTH(heap);
        __heap_end__                    = ORIGIN(heap) + LENGTH(heap) - 1;

        __stack_start__                 = ORIGIN(stack);
        __stack_end__                   = ORIGIN(stack) + LENGTH(stack); /*Stack is FULL DESCENDING*/

        __screen_buffer_start__         = ORIGIN(screen_buffer);
        __screen_buffer_length__        = LENGTH(screen_buffer);

        __vidram_start__                = ORIGIN(vidram);
        __vidram_length__               = LENGTH(vidram);
}
