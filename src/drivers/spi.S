#ifdef ARCH_RP2040
.cpu cortex-m0
#elifdef ARCH_RP2350
.cpu cortex-m33
#endif
.thumb
.syntax unified

.equ SSPCR0, 0x00
.equ SSPCR1, 0x04
.equ SSPDR, 0x08
.equ SSPSR, 0x0c
.equ SSPCPSR, 0x10
.equ SSPDMACR, 0x24

.macro spi_determine_address
        cmp     r0, #0
        ite     eq
        ldreq   r0, SPI0_BASE
        ldrne   r0, SPI1_BASE
.endm

/**
 * Setup the SPI_TX. Final frequency according to datasheet: cf. 12.3.4.6.1. Bit rate generation pp. 1051
 *   r0 - spi hw block number
 *   r1 - prescaller in 2-254 (LSB=0!!!)
 *   r2 - post-divider  0-255
 *   r3 - SPI operating mode (0-3)
 */
.thumb_func
.global spi_init
.align 4
spi_init:
        push    {r4-r7, lr}

        mov     r5, r1
        mov     r6, r2
        mov     r7, r3
        bl      spi_set_baudrate

        spi_determine_address
        mov     r4, r0

        bl      spi_disable

        adds    r0, r4, SSPCR0
        movs    r1, #7                  // 8 bit data, CPOL=0, CPHA=0, Motorola; TODO: CPOL, CPHA depend on r3
        bl      atomic_set

        adds    r0, r4, SSPDMACR
        movs    r1, #3                  // according to SDK spi_init - always enable
        bl      atomic_set

        mov     r0, r4
        bl      spi_enable

        pop     {r4-r7, pc}

/**
 * Setup the SPI_TX. Final frequency according to datasheet: cf. 12.3.4.6.1. Bit rate generation pp. 1051
 *   r0 - spi hw block number
 *   r1 - prescaller in 2-254 (LSB=0!!!)
 *   r2 - post-divider  0-255
 */
.thumb_func
.global spi_set_baudrate
.align 4
spi_set_baudrate:
        push    {r4-r6, lr}
        mov     r4, r1
        mov     r5, r2

        spi_determine_address
        mov     r6, r0

        bl      spi_disable

        // 1) set prescaller
        adds    r0, r6, SSPCPSR
        ldr     r2, ATOMIC_BITMASK_CLR
        mov     r1, #0xff
        str     r1, [r0, r2]            // clear current prescaller
        ldr     r2, ATOMIC_BITMASK_SET
        str     r4, [r0, r2]            // load prescaller


        // 2) set post-divider
        adds    r0, r6, SSPCR0
        ldr     r2, ATOMIC_BITMASK_CLR
        mov     r1, #0xff
        lsls    r1, r1, #8              // SCR = post-divider
        str     r1, [r0, r2]            // clear current SCR value
        lsls    r1, r5, #8
        ldr     r2, ATOMIC_BITMASK_SET
        str     r1, [r0, r2]            // store SCR

        mov     r0, r6
        bl      spi_enable

        pop     {r4-r6, pc}


/**
 * Enables spi hw block
 *  - r0 - address of a block
 */
.thumb_func
.align 4
spi_enable:
        push    {lr}

        adds    r0, r0, SSPCR1
        movs    r1, #2                  // SSE_ENABLE BIT
        bl      atomic_set

        pop     {pc}

/**
 * Disables spi hw block
 *  - r0 - address of a block
 */
.thumb_func
.align 4
spi_disable:
        push    {lr}

        adds    r0, r0, SSPCR1
        movs    r1, #2                  // SSE_ENABLE BIT
        bl      atomic_clr

        pop     {pc}


/**
 * Transfers data through selected SPI block
 *  - r0 - spi block number (0, 1)
 *  - r1 - data to be send
 */
.thumb_func
.global spi_tx
.align 4
spi_tx:
        push    {lr}

        spi_determine_address

        // as the SSP device may still work (it works much slower than the cpu clock)
        // we must wait for the availability of the fifo
        .tnf_tx_loop:
                ldr     r2, [r0, SSPSR]
                tst     r2, #2                  // bit 2 (TNF): 0 full; 1 not full
                beq     .tnf_tx_loop

        str     r1, [r0, SSPDR]

        .rne_tx_loop:
                ldr     r2, [r0, SSPSR]
                tst     r2, #4                  // RNE: wait for rx fifo not empty
                beq     .rne_tx_loop
        ldr     r0, [r0, SSPDR]                 // drain rx fifo, return value in r0

        pop     {pc}


/**
 * Transfers data through selected SPI block
 *  - r0 - spi block number (0, 1)
 *
 * Returns:
 *  Received value in r0
 */
.thumb_func
.global spi_rx
.align 4
spi_rx:
        push    {lr}
        mov     r1, #0xff
        bl      spi_tx

        pop     {pc}


.align 4
#ifdef ARCH_RP2040
SPI0_BASE:      .word 0x4003c000
SPI1_BASE:      .word 0x40040000
#elifdef ARCH_RP2350
SPI0_BASE:      .word 0x40080000
SPI1_BASE:      .word 0x40088000
#endif

ATOMIC_BITMASK_SET:     .word 0x2000
ATOMIC_BITMASK_CLR:     .word 0x3000

