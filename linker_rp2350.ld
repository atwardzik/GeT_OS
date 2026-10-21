MEMORY
{
        code (rx)               : ORIGIN = 0x20000000, LENGTH = 65536
        heap (rwx)              : ORIGIN = 0x20010000, LENGTH = 136960
        stack (rw)              : ORIGIN = 0x20031700, LENGTH = 8K
        screen_buffer (rw)      : ORIGIN = 0x20033700, LENGTH = 6400
        vidram (w)              : ORIGIN = 0x20035000, LENGTH = 307200
        data (rw)               : ORIGIN = 0x20080000, LENGTH = 4K
        bss (rw)                : ORIGIN = 0x20081000, LENGTH = 4K
}

SECTIONS
{
        .text : {
                __text_start__ = .;
                *(.reset*)
                *(.text*)
                /* __cpio_init_start__ = .; */
                /* *(.cpio_init*) */        /* For debug purposes until proper unvolatile memory support exists */
                *(.rodata*)
                __text_end__ = .;
        } > code

        .vectors : {
                . = ALIGN(256);
                __vectors_start = .;
                *(.vectors*)
        } > code

        .vidram : {
                *(.vidram*)
        } > vidram

        .data : {
                __data_start__ = .;
                *(.data)
                *(.data*)
                __data_end__ = .;
        } > data

        .bss : {
                __bss_start__ = .;
                *(.bss)
                *(.bss*)
                *(COMMON*)
                __bss_end__ = .;
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
