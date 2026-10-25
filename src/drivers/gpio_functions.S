#ifdef ARCH_RP2040
.cpu cortex-m0
#elifdef ARCH_RP2350
.cpu cortex-m33
#endif
.thumb
.syntax unified

#ifdef ARCH_RP2040
.equ GPIO_OE, 0x20
.equ GPIO_OE_SET, 0x24
.equ GPIO_OE_CLR, 0x28
.equ GPIO_IN, 0x04
.equ GPIO_OUT, 0x10
.equ GPIO_OUT_SET, 0x14
.equ GPIO_OUT_CLR, 0x18
.equ GPIO_OUT_XOR, 0x1c

#elifdef ARCH_RP2350
.equ GPIO_OE, 0x30
.equ GPIO_OE_SET, 0x38
.equ GPIO_OE_CLR, 0x40
.equ GPIO_IN, 0x04
.equ GPIO_OUT, 0x10
.equ GPIO_OUT_SET, 0x18
.equ GPIO_OUT_CLR, 0x20
.equ GPIO_OUT_XOR, 0x28
#endif


/**
 * Connects selected GPIO pin to selected peripherial by using GPIO CTRL register.
 * r0 - GPIO Pin
 * r1 - GPIO Function (cf. RP2040 Datasheet 1.4.3)
 * */
.thumb_func
.global GPIO_function_select
.align 4
#ifdef ARCH_RP2040
GPIO_function_select:
        ldr     r3, IO_BANK0_BASE

        movs    r2, #8
        muls    r2, r2, r0              // calculate offset for GPIO_N_CTRL (minus 0x04)
        adds    r2, #0x04               // GPIO0_CTRL offset
        add     r3, r3, r2              // add calculated offset

        str     r1, [r3]                // write specfied function

        bx      lr


#elifdef ARCH_RP2350
GPIO_function_select:
        push    {r4}
        // calculate offset for PADS_GPIO_n
        ldr     r4, PADS_BANK0_BASE
        movs    r3, #4
        muls    r3, r3, r0
        adds    r3, #0x04
        add     r3, r3, r4
        ldr     r2, =pads_offset
        str     r3, [r2]

        // setup pads
        ldr     r2, [r3]                // read current state of pad
        movs    r4, #0x40               // PADS_BANK0_GPIO0_IE_BITS
        eors    r2, r2, r4
        movs    r4, #0xc0               // IE_BITS, OD_BITS
        ands    r2, r2, r4
        str     r2, [r3]

        // function select
        ldr     r3, IO_BANK0_BASE
        movs    r2, #8
        muls    r2, r2, r0              // calculate offset for GPIO_N_CTRL (minus 0x04)
        adds    r2, #0x04               // GPIO0_CTRL offset
        add     r3, r3, r2              // add calculated offset
        str     r1, [r3]                // write specfied function


        ldr     r4, =pads_offset
        ldr     r4, [r4]
        ldr     r3, =0x3000             // atomic register clear
        adds    r4, r4, r3
        movs    r1, #1
        lsls    r1, r1, #8
        str     r1, [r4]

        pop     {r4}
        bx      lr
#endif


/**
 * Sets up pullup on specified GPIO
 *  r0 - GPIO Pin
 *  r1 - pull mask (up/down) -> down: 0, up: true
 * */
.thumb_func
.global set_pin_pull
.align 4
set_pin_pull:
        ldr     r3, PADS_BANK0_BASE
        movs    r2, #0x04
        muls    r0, r0, r2              // calculate pin offset
        add     r0, r0, #0x04           // add offset start (GPIO0)
        add     r0, r0, r3

        movs    r2, #1
        cmp     r1, #0
        ite     eq
        lslseq  r2, r2, #2              // pull down
        lslsne  r2, r2, #3              // pull up

        ldr     r3, [r0]
        orrs    r3, r3, r2
        str     r3, [r0]

        bx      lr


////////////////////////////////////////////////////////////////////////////////
////
////          PIN INPUTS
////
////////////////////////////////////////////////////////////////////////////////

/**
 * Configs specified pin to work as input.
 * r0 - GPIO Pin
 * r1 - pull mask (up/down) -> down: 0, up: true
 * */
.thumb_func
.global init_pin_input_with_pull
.align 4
init_pin_input_with_pull:
        push    {lr}
        ldr     r2, =in_pin
        str     r0, [r2]
        ldr     r2, =pull_mask
        str     r1, [r2]

        ldr     r0, in_pin
        movs    r1, #5
        bl      GPIO_function_select

        ldr     r0, in_pin
        ldr     r1, pull_mask

        ldr     r3, PADS_BANK0_BASE
        movs    r2, #0x04
        muls    r0, r0, r2                      // calculate pin offset
        adds    r0, r0, #0x04                   // add offset start (GPIO0)
        add     r0, r0, r3

        // OUTPUT DISABLE + INPUT ENABLE = 0xC0
        // PULL DOWN - BIT 2 ; PULL UP - BIT 3
        cmp     r1, #0
        bne     .pull_up
        .pull_down:
                movs    r1, 0xC4
                b       .store_config
        .pull_up:
                movs    r1, 0xC8
        .store_config:
                str     r1, [r0]

        pop     {pc}

.align 4
in_pin:             .word 0
pull_mask:          .word 0

/**
 * Initialises multiple consecutive pins
 * r0 - pin start
 * r1 - number of pins to be initialised
 * r2 - pull up/down -> down: 0 ; up: true
 * */
.thumb_func
.global init_multiple_pin_inputs
.align 4
init_multiple_pin_inputs:
        push    {r4-r7, lr}
        mov     r4, r0                  // pin start
        mov     r5, r1                  // counter
        mov     r6, r2                  // pull up/down

        .init_loop:
                cmp     r5, #0
                beq     .end_init

                mov     r0, r4
                mov     r1, r6
                bl      init_pin_input_with_pull

                adds    r4, #1
                subs    r5, #1
                b       .init_loop

        .end_init:
                pop     {r4-r7, pc}


////////////////////////////////////////////////////////////////////////////////
////
////          PIN OUTPUTS
////
////////////////////////////////////////////////////////////////////////////////

/**
 * Selects SIO and enables output for a pin.
 * r0 - pin
 * */
.thumb_func
.global init_pin_output
.align 4
init_pin_output:
        push    {lr}

        ldr     r1, =out_pin
        str     r0, [r1]

        ldr     r0, out_pin
        movs    r1, #5                          // 5 - SIO
        bl      GPIO_function_select

        ldr     r0, out_pin
        bl      output_enable_pin

        pop     {pc}

.align 4
out_pin:            .word 0

/**
 * Enables output to the pin specified in r0.
 * */
.thumb_func
.global output_enable_pin
.align 4
output_enable_pin:
        ldr     r1, SIO_BASE
        adds    r1, r1, GPIO_OE_SET     // atomic operation GPIO_OE |= data
        movs    r2, #1
        lsls    r2, r2, r0              // set enable on n-th pin
        str     r2, [r1]

        bx      lr


/**
 * Clears output enable on the pin specified in r0, by setting it to input.
 * */
.thumb_func
.global clear_output_enable_pin
.align 4
clear_output_enable_pin:
        ldr     r1, SIO_BASE
        adds    r1, r1, GPIO_OE_CLR     // clear output enable
        movs    r2, #0
        lsls    r2, r2, r0              // clear enable on n-th pin
        str     r2, [r1]

        bx      lr


/**
 * Sets boolean true to the pin specified in r0.
 * */
.thumb_func
.global set_pin
.align 4
set_pin:
        ldr     r2, SIO_BASE
        adds    r2, r2, GPIO_OUT_SET
        movs    r1, #1
        lsls    r1, r1, r0              // set on n-th pin
        str     r1, [r2]

        bx      lr


/**
 * Sets boolean false to the pin specified in r0.
 * */
.thumb_func
.global clr_pin
.align 4
clr_pin:
        ldr     r2, SIO_BASE
        adds    r2, r2, GPIO_OUT_CLR
        movs    r1, #1
        lsls    r1, r1, r0              // reset on n-th pin
        str     r1, [r2]

        bx      lr

/**
 * Sets boolean mask specified in r0
 * */
.thumb_func
.global set_pin_mask
.align 4
set_pin_mask:
        ldr     r1, SIO_BASE
        adds    r1, r1, GPIO_OUT

        str     r0, [r1]

        bx      lr


/**
 * Returns in r0 current mask (logical values on pins)
 * */
.thumb_func
.global get_pin_mask
.align 4
get_pin_mask:
        ldr     r0, SIO_BASE
        adds    r0, r0, GPIO_IN

        ldr     r0, [r0]

        bx      lr

/**
 * Performs XOR on the value of the pin specified in r0.
 * */
.thumb_func
.global xor_pin
.align 4
xor_pin:
        ldr     r1, SIO_BASE
        adds    r1, r1, GPIO_OUT_XOR
        movs    r2, #1                  // XOR value => 1 ^ 1 = 0 ; 0 ^ 1 = 1
        lsls    r2, r2, r0              // set xor on n-th pin
        str     r2, [r1]

        bx      lr

/**
 * Set IRQ enabled
 *  r0 - pin for irq
 * */
.thumb_func
.global set_irq_pin_enabled_edge_low
.align 4
set_irq_pin_enabled_edge_low:
        push    {r4, r5}
        mov     r4, r0

        // determine bit for edge low -> (pin % 8) * 4 + 2
        movs    r2, #0x07
        ands    r1, r0, r2                      // equivalent to (pin % 8)

        movs    r2, #0x04
        muls    r1, r1, r2
        adds    r1, r1, #0x02

        movs    r2, #1
        lsls    r2, r2, r1

        mov     r5, r2

        // 3.1) acknowledge_irq - "clear stale events which might cause immediate spurious handler entry"
        ldr     r1, IO_BANK0_BASE

        lsrs    r0, r0, #3                      // pin / 8 = INTR_n
        movs    r2, #0x04
        muls    r0, r0, r2
        ldr     r3, INTR_OFFSET
        add     r0, r0, r3
        add     r0, r0, r1

        str     r5, [r0]                        // clear by setting INTR

        // 3.2) set irq enabled
        ldr     r0, IO_BANK0_BASE

        lsrs    r1, r4, #3                      // pin / 8 = INTE_n
        movs    r2, #0x04
        muls    r1, r1, r2

        ldr     r2, PROC0_INTE0_OFFSET
        add     r1, r1, r2
        add     r0, r0, r1

        str     r5, [r0]                        // set irq enabled - INTE


        pop     {r4, r5}
        bx      lr

/**
 * Clears interrupt on specified pin
 *  r0 - pin to clear interrupt from
 */
.thumb_func
.global clr_interrupt
.align 4
clr_interrupt:
        mov     r3, r0
        // 1) determine INTR_n
        ldr     r1, IO_BANK0_BASE
        lsrs    r0, r0, #3                              // pin / 8 = INTR_n
        movs    r2, #0x04
        muls    r0, r0, r2
        ldr     r2, INTR_OFFSET
        add     r0, r0, r2
        add     r0, r0, r1

        // determine bit for edge low -> (pin % 8) * 4 + 2
        movs    r2, #0x07
        ands    r1, r3, r2                              // equivalent to (pin % 8)

        movs    r2, #0x04
        muls    r1, r1, r2
        adds    r1, r1, #0x02

        movs    r2, #1
        lsls    r2, r2, r1

        // 2) reset interrupt

        str     r2, [r0]                                // clear by setting INTR

        bx      lr


#ifdef ARCH_RP2040
.align 4
IO_BANK0_BASE:          .word 0x40014000
PADS_BANK0_BASE:        .word 0x4001c000
SIO_BASE:               .word 0xd0000000
INTR_OFFSET:            .word 0x0f0
PROC0_INTE0_OFFSET:     .word 0x100

#elifdef ARCH_RP2350
.align 4
IO_BANK0_BASE:          .word 0x40028000
PADS_BANK0_BASE:        .word 0x40038000
SIO_BASE:               .word 0xd0000000
INTR_OFFSET:            .word 0x230
PROC0_INTE0_OFFSET:     .word 0x248

.section .data
.align 4
pads_offset: .word 0
#endif
