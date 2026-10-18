#ifdef ARCH_RP2040
.cpu cortex-m0
#elifdef ARCH_RP2350
.cpu cortex-m33
#endif
.thumb

.syntax unified

#ifdef ARCH_RP2040
.equ BIT_DMA, 2
.equ BIT_IO_BANK0, 5
.equ BIT_PADS_BANK0, 8
.equ BIT_PIO0, 10
.equ BIT_PIO0, 11
.equ BIT_SPI0, 16
.equ BIT_SPI1, 17
.equ BIT_PLL_SYS, 12
.equ BIT_IO_UART0, 22
#elifdef ARCH_RP2350
.equ BIT_DMA, 2
.equ BIT_IO_BANK0, 6
.equ BIT_PADS_BANK0, 9
.equ BIT_PIO0, 11
.equ BIT_PIO1, 12
.equ BIT_PIO2, 13
.equ BIT_PLL_SYS, 14
.equ BIT_SPI0, 18
.equ BIT_SPI1, 19
.equ BIT_IO_UART0, 26
#endif


.equ RESET_DONE_OFFSET, 0x08

/**
 * Resets GPIO, PADS, UART and PIO
 *
 */
.thumb_func
.global reset_subsys
.align 4
reset_subsys:
        push    {lr}

        movs    r0, BIT_DMA
        bl      hw_reset

        movs    r0, BIT_PIO0
        bl      hw_reset

        movs    r0, BIT_PIO1
        bl      hw_reset

        movs    r0, BIT_IO_UART0
        bl      hw_reset

        movs    r0, BIT_SPI0
        bl      hw_reset

        movs    r0, BIT_SPI1
        bl      hw_reset

        movs    r0, BIT_IO_BANK0
        bl      hw_reset

        movs    r0, BIT_PADS_BANK0
        bl      hw_reset

        pop     {pc}


/**
 * Resets PLL
 *
 */
.thumb_func
.global reset_pll
.align 4
reset_pll:
        push    {lr}

        movs    r0, BIT_PLL_SYS
        bl      hw_reset

        pop     {pc}



/**
 * Resets peripherial specified as a bit whose number is in r0.
 */
.thumb_func
.align 4
hw_reset:
        movs    r3, #1
        lsls    r3, r0

        ldr     r2, RESETS_BASE
        ldr     r1, ATOMIC_BITMASK_CLR         @ Atomic register access
        add     r1, r1, r2
        str     r3, [r1]

        adds    r2, r2, RESET_DONE_OFFSET

        .reset:
                ldr     r1, [r2]
                tst     r1, r3
                beq     .reset

        bx      lr


#ifdef ARCH_RP2040
.align 4
RESETS_BASE:            .word 0x4000c000

#elifdef ARCH_RP2350
.align 4
RESETS_BASE:            .word 0x40020000

#endif

ATOMIC_BITMASK_CLR:     .word 0x3000
