#ifdef ARCH_RP2040
.cpu cortex-m0
#elifdef ARCH_RP2350
.cpu cortex-m33
#endif
.thumb
.syntax unified

.extern __vidram_start__
.extern __vidram_length__

.equ TIMER_ALARM0_OFFSET, 0x10
.equ TIMER_TIMERAWL_OFFSET, 0x28
#ifdef ARCH_RP2040
.equ TIMER_INTE, 0x38
.equ TIMER_INTR, 0x34
#elifdef ARCH_RP2350
.equ TIMER_INTR, 0x3c
.equ TIMER_INTE, 0x40
#endif


.thumb_func
.global vga_clr_screen
.align 4
vga_clr_screen:
        push    {lr}

#if 0
        @ movs    r0, #0                  @ PIO BLOCK 0
        @ movs    r1, #7                  @ sm 0, 1, 2
        @ movs    r2, #0                  @ enabled=false
        @ bl      set_multiple_sm_enabled

        @ ldr     r0, =0x502000d8         @ hsync
        @ ldr     r1, =0xe001             @ set pins, 0
        @ str     r1, [r0]

        @ ldr     r0, =0x502000f0         @ vsync
        @ str     r1, [r0]

        @ ldr     r0, =0x50200108
        @ ldr     r1, =0xe020             @ set x, 0
        @ str     r1, [r0]
        @ ldr     r1, =0xa001             @ mov pins, x
        @ str     r1, [r0]
#endif
        ldr     r0, =0x20035000
        ldr     r3, =(640 * 100)
        add     r0, r0, r3
        movs    r1, #0
        ldr     r2, =307200
        sub     r2, r2, r3

        .loop_clear:
                str     r1, [r0]
                adds    r0, r0, #4
                subs    r2, r2, #4
                cmp     r2, #0
                bne     .loop_clear
        pop     {pc}

.thumb_func
.global vga_start
.align 4
vga_start:
        push    {lr}

        movs    r0, #0                  @ PIO BLOCK 0
        movs    r1, #7                  @ sm 0, 1, 2
        movs    r2, #1                  @ enabled=true
        bl      set_multiple_sm_enabled

        movs    r0, #1                  @ start DMA channel
        ldr     r1, =0x50000450
        str     r0, [r1]

        pop     {pc}

/**
 * Setup the HSYNC signal
 *   r0 - pin
 */
.thumb_func
.global hsync_gen_init
.align 4
hsync_gen_init:
        push    {r4, lr}
        mov     r4, r0
        @@@ According to squarewave example from SDK -
        @@@ pp. 313 (rp2040) / 877 (rp2350) in the datasheet.
        @@@
        @@@ The sequence of operations is taken straight from the SDK.

        @ 0) Disable State Machine!!!
        movs    r0, #0
        movs    r1, #0
        movs    r2, #0
        bl      set_sm_enabled

        @ 1) Load program
        ldr     r0, =hsync_gen
        movs    r1, #9                  @ Total of 9 words
        movs    r2, #0                  @ PIO0
        bl      load_pio_prog

        @ 2) Set state machine exec ctrl with wrap (SM0_EXECCTRL); (WRAP_TARGET << 7) | (WRAP << 12)
        movs    r0, #0
        movs    r1, #0
        ldr     r2, =0x8080             @ bottom wrap at 1, top wrap at 8
        bl      setup_pio_execctrl

        @ 3) Set state machine pin ctrl
        movs    r0, #1                  @ one pin affected
        lsls    r0, #26                 @ SET_COUNT_BITS
        mov     r1, r4
        lsls    r1, #5                  @ SET_BASE_BITS
        orrs    r2, r0, r1              @ control bits

        movs    r0, #0                  @ PIO BLOCK 0
        movs    r1, #0                  @ State Machine 0
        bl      setup_pio_pinctrl

        @ 4) Set clock divisor
        movs    r0, #0                  @ PIO BLOCK 0
        movs    r1, #0                  @ State Machine 0
        movs    r2, #5                  @ 125 MHz / 5 = 25 MHz
        lsls    r2, #16
        bl      setup_pio_clkdiv

        @ 5) Init GPIO
        mov     r0, r4                  @ GPIO
        movs    r1, #6                  @ Funcsel: PIO0
        bl      GPIO_function_select

        @ 6) Set consecutive pindirs to output
        movs    r0, #0
        movs    r1, #0
        mov     r2, r4
        movs    r3, #1
        bl      set_consecutive_pindirs_out

        @ 7) PIO State Machine init - load and jump to first instruction
        movs    r0, #0
        movs    r1, #0
        bl      clear_pio_sm_fifo

        movs    r0, #0
        movs    r1, #0
        movs    r2, #0                  @ address begin
        bl      clear_internal_and_jump

        @ 8) Set state machine running
        movs    r0, #0
        movs    r1, #0
        ldr     r2, =0x28f              @ 655 = 640 pixels + 16 frontporch - 1
        bl      sm_put

        movs    r0, #0
        movs    r1, #0
        movs    r2, #1
        @ bl      set_sm_enabled

        pop     {r4, pc}


/**
 * Setup the VSYNC signal
 *   r0 - pin
 */
.thumb_func
.global vsync_gen_init
.align 4
vsync_gen_init:
        push    {r4, lr}
        mov     r4, r0


        @ 0) Disable State Machine!!!
        movs    r0, #0
        movs    r1, #1
        movs    r2, #0
        bl      set_sm_enabled

        @ 1) Load program
        ldr     r0, =vsync_gen
        movs    r1, #14                 @ Total of 14 words
        movs    r2, #0                  @ PIO0
        bl      load_pio_prog

        @ 2) Set state machine exec ctrl with wrap (SM0_EXECCTRL); (WRAP_TARGET << 7) | (WRAP << 12)
        movs    r0, #0
        movs    r1, #1
        ldr     r2, =0x16500            @ bottom wrap at 1, top wrap at 14 + OFFSET
        @ add info about SIDE_EN
        movs    r3, #1
        lsls    r3, r3, #30             @ SIDE_EN bit
        orrs    r2, r2, r3
        bl      setup_pio_execctrl

        @ 3) Set state machine pin ctrl
        @ add info about SET_COUNT
        movs    r0, #1                  @ one pin affected
        lsls    r0, #26                 @ SET_COUNT_BITS

        @ add info about SET_BASE
        mov     r1, r4
        lsls    r1, #5                  @ SET_BASE_BITS
        orrs    r2, r0, r1              @ control bits

        @ add info about SIDESET_COUNT
        movs    r0, #2                  @ two pins affected (inclusive the enable bit)
        lsls    r0, #29                 @ SIDESET_COUNT_BITS
        orrs    r2, r2, r0

        @ add info about SIDESET_BASE
        mov     r0, r4                  @ GPIO pin on which the VSYNC will appear
        lsls    r0, #10                 @ SIDESET_BASE_BITS
        orrs    r2, r2, r0              @ control bits

        movs    r0, #0                  @ PIO BLOCK 0
        movs    r1, #1                  @ State Machine 1
        bl      setup_pio_pinctrl

        @ 4) Set clock divisor
        movs    r0, #0                  @ PIO BLOCK 0
        movs    r1, #1                  @ State Machine 1
        movs    r2, #5                  @ 125 MHz / 5 = 25 MHz
        lsls    r2, #16
        bl      setup_pio_clkdiv

        @ 5) Init GPIO
        mov     r0, r4
        movs    r1, #6                  @ Funcsel: PIO0
        bl      GPIO_function_select

        @ 6) Set consecutive pindirs to output
        movs    r0, #0
        movs    r1, #1
        mov     r2, r4
        movs    r3, #1
        bl      set_consecutive_pindirs_out

        @ 7) PIO State Machine init - load and jump to first instruction
        movs    r0, #0
        movs    r1, #1
        bl      clear_pio_sm_fifo

        movs    r0, #0
        movs    r1, #1
        movs    r2, #9                  @ address begin
        bl      clear_internal_and_jump

        @ 8) Set state machine running
        movs    r0, #0
        movs    r1, #1
        ldr     r2, =0x1df              @ 479 = 480 pixels - 1
        bl      sm_put

        pop     {r4, pc}


/**
 * Setup the RGB analog signal
 *   r0 - pin
 */
.thumb_func
.global rgb_gen_init
.align 4
rgb_gen_init:
        push    {r4, lr}
        mov     r4, r0

        @ 0) Disable State Machine!!!
        movs    r0, #0
        movs    r1, #2
        movs    r2, #0
        bl      set_sm_enabled

        @ 1) Load program
        ldr     r0, =rgb_gen
        movs    r1, #9                  @ Total of 9 words
        movs    r2, #0                  @ PIO0
        bl      load_pio_prog

        @ 2) Set state machine exec ctrl with wrap (SM0_EXECCTRL); (WRAP_TARGET << 7) | (WRAP << 12)
        movs    r0, #0
        movs    r1, #2
        ldr     r2, =0x1fc80            @ bottom wrap at 25, top wrap at 31
        bl      setup_pio_execctrl

        @ 3) Set state machine pin ctrl
        movs    r0, #5                  @ five pins affected
        lsls    r0, #26                 @ SET_COUNT_BITS
        mov     r1, r4
        lsls    r1, #5                  @ SET_BASE_BITS
        orrs    r2, r0, r1              @ control bits
        ldr     r0, =0x600000           @ OUT_COUNT_BITS
        orrs    r0, r0, r4              @ OUT_BASE_BITS
        orrs    r2, r2, r0

        movs    r0, #0                  @ PIO BLOCK 0
        movs    r1, #2                  @ State Machine 2
        bl      setup_pio_pinctrl

        @ 5) Init GPIO
        mov     r0, r4                  @ GPIO n
        movs    r1, #6                  @ Funcsel: PIO0
        bl      GPIO_function_select

        mov     r0, r4                  @ GPIO n + 1
        adds    r0, r0, #1
        movs    r1, #6                  @ Funcsel: PIO0
        bl      GPIO_function_select

        mov     r0, r4                  @ GPIO n + 2
        adds    r0, r0, #2
        movs    r1, #6                  @ Funcsel: PIO0
        bl      GPIO_function_select

        mov     r0, r4                  @ GPIO n + 3
        adds    r0, r0, #3
        movs    r1, #6                  @ Funcsel: PIO0
        bl      GPIO_function_select

        mov     r0, r4                  @ GPIO n + 4
        adds    r0, r0, #4
        movs    r1, #6                  @ Funcsel: PIO0
        bl      GPIO_function_select

        mov     r0, r4                  @ GPIO n + 5
        adds    r0, r0, #5
        movs    r1, #6                  @ Funcsel: PIO0
        bl      GPIO_function_select


        @ 6) Set consecutive pindirs to output
        movs    r0, #0
        movs    r1, #2
        mov     r2, r4
        movs    r3, #6
        bl      set_consecutive_pindirs_out

        @ 7) PIO State Machine init - load and jump to first instruction
        movs    r0, #0
        movs    r1, #2
        bl      clear_pio_sm_fifo

        movs    r0, #0
        movs    r1, #2
        movs    r2, #23                 @ address begin
        bl      clear_internal_and_jump

        @ 8) Set state machine running
        movs    r0, #0
        movs    r1, #2
        ldr     r2, =0x27f              @ 639 = 640 pixels - 1
        bl      sm_put

        pop     {r4, pc}

.thumb_func
.global setup_vga_dma
.align 4
setup_vga_dma:
        @ CH0
        ldr     r0, CH0_READ_ADDR
        ldr     r1, VIDRAM_START
        str     r1, [r0]

        adds    r0, r0, #4              @ CH0_WRITE_ADDR
        ldr     r1, =0x50200018         @ rgb pio tx fifo
        str     r1, [r0]

        adds    r0, r0, #4              @ CH0_TRANSFER_COUNT
        ldr     r1, =__vidram_length__
        str     r1, [r0]

        adds    r0, r0, #8              @ CH0_AL1_CTRL
        ldr     r1, =0x42011
        str     r1, [r0]

        @ CH1
        ldr     r0, CH1_READ_ADDR
        ldr     r1, =VIDRAM_START
        str     r1, [r0]

        adds    r0, r0, #4              @ CH1_WRITE_ADDR
        ldr     r1, CH0_READ_ADDR
        str     r1, [r0]

        adds    r0, r0, #4              @ CH1_TRANSFER_COUNT
        movs    r1, #1
        str     r1, [r0]

        adds    r0, r0, #8              @ CH1_AL1_CTRL
        ldr     r1, =0x7e0009
        str     r1, [r0]

        bx      lr

.align 4
VIDRAM_START:   .word __vidram_start__
CH0_READ_ADDR:  .word 0x50000000
CH1_READ_ADDR:  .word 0x50000040

.thumb_func
.global vga_reset_cursor_timer
.align 4
vga_reset_cursor_timer:
        ldr     r3, TIMER_BASE
        adds    r3, r3, TIMER_INTR
        movs    r2, #1
        str     r2, [r3]                                @ reset interrupt for alarm0

        ldr     r3, TIMER_BASE
        adds    r3, r3, TIMER_INTE
        movs    r2, #1                                  @ enable timer interrupt for alarm0 i.e. (1 << 0)
        str     r2, [r3]

        ldr     r3, TIMER_BASE
        ldr     r1, [r3, TIMER_TIMERAWL_OFFSET]         @ get TIMERAWL
        ldr     r0, =cursor_blink_time
        ldr     r0, [r0]
        add     r1, r1, r0                              @ TIMERAWL + desired alarm
        str     r1, [r3, TIMER_ALARM0_OFFSET]

        bx      lr

.thumb_func
.global vga_cursor_blinker_handler
.align 4
vga_cursor_blinker_handler:
        cpsid   i
        push    {lr}

        @ 1) xor cursor pointer
        bl      vga_xor_cursor

        @ 2) reset
        bl      vga_reset_cursor_timer

        cpsie   i
        pop     {pc}

/**
 * Starts the cursor blinking.
 *   r0 - blinking delay
 */
.thumb_func
.global vga_set_cursor_blink
.align 4
vga_set_cursor_blink:
        push    {r4, lr}
        mov     r4, r0
        ldr     r1, =cursor_blink_time
        str     r0, [r1]

        movs    r0, #0
        ldr     r1, =vga_cursor_blinker_handler
        bl      set_isr

        ldr     r0, TIMER_BASE
        movs    r1, #1                                  @ enable timer interrupt for alarm0 i.e. (1 << 0)
        str     r1, [r0, TIMER_INTE]

        ldr     r1, [r0, TIMER_TIMERAWL_OFFSET]         @ get TIMERAWL
        add     r1, r1, r4                              @ TIMERAWL + desired alarm
        str     r1, [r0, TIMER_ALARM0_OFFSET]

        pop     {r4, pc}

.thumb_func
.global vga_set_cursor_off
.align 4
vga_set_cursor_off:
        ldr     r0, TIMER_BASE
        movs    r1, #0                                  @ disable timer interrupt for alarm0 i.e. (0 << 0)
        str     r1, [r0, TIMER_INTE]

        bx      lr

#ifdef ARCH_RP2040
.align 4
TIMER_BASE:             .word 0x40054000
#elifdef ARCH_RP2350
.align 4
TIMER_BASE:             .word 0x400b0000
#endif

#ifdef VGA_640x480
/**
 * Generating Hsync signal. Total of 9 instructions.
 */
.align 4
hsync_gen:
        .word 0x80a0    @ 0:            pull   block
                        @       .wrap_target
        .word 0xa027    @ 1:            mov    x, osr
        .word 0x0042    @ 2:activeporch:jmp    x--, activeporch
        .word 0xff00    @ 3: pulse:     set    pins, 0 [31]
        .word 0xff00    @ 4:            set    pins, 0 [31]
        .word 0xff00    @ 5:            set    pins, 0 [31]
        .word 0xff01    @ 6: backporch: set    pins, 1 [31]
        .word 0xec01    @ 7:            set    pins, 1 [12]
        .word 0xc100    @ 8:            irq    0[1]
                        @       .wrap


/**
 * Generating Vsync signal. Total of 14 instructions.
 *  .side_set 1 opt
 */
.align 4
vsync_gen:
        .word 0x80a0    @ 9:            pull   block
                        @       .wrap_target
        .word 0xa027    @ 10:           mov    x, osr
        .word 0x20c0    @ 11: active:   wait   1 irq, 0
        .word 0xc001    @ 12:           irq    1
        .word 0x004b    @ 13:           jmp    x--, active
        .word 0xe049    @ 14:           set    y, 9
        .word 0x20c0    @ 15:frontporch:wait   1 irq, 0
        .word 0x008f    @ 16:           jmp    y--, frontporch
        .word 0xe000    @ 17:syncpulse: set    pins, 0
        .word 0x20c0    @ 18:           wait   1 irq, 0
        .word 0x20c0    @ 19:           wait   1 irq, 0
        .word 0xe05f    @ 20:           set    y, 31
        .word 0x38c0    @ 21:backporch  wait   1 irq, 0        side 1
        .word 0x0095    @ 22:           jmp    y--, backporch
                        @       .wrap

/**
 * Generating RGB signals. Total of 8 instructions. Running at 125[MHz]
 */
.align 4
rgb_gen:

        .word 0x80a0    @ 23:           pull   block
        .word 0xa047    @ 24:           mov    y, osr
                        @       .wrap_target
        @ .word 0xe000    @ 25:           set    pins, 0
        .word 0xe020    @ 25:           set    x, 0
        .word 0xa001    @ 26:           mov    pins, x
        .word 0xa022    @ 27:           mov    x, y
        .word 0x22c1    @ 28:           wait   1 irq, 1 [2]
        @ .word 0xa042
        @ .word 0xe203
        .word 0x80a0    @ 29: colorout: pull   block
        .word 0x6206    @ 30:           out    pins, 6  [2]      @ for 5-bit mode
        @ .word 0x6203    @ 29:           out    pins, 3 [2]       @ for 3-bit mode
        .word 0x005d    @ 31:           jmp    x--, colorout
                        @       .wrap

/*
 * TODO: fix and check in future
 */
#elifdef VGA_800x600
/**
 * Generating Hsync signal. Total of 10 instructions.
 */
.align 4
hsync_gen:
        .word 0x80a0    @               pull   block
                        @     .wrap_target
        .word 0xa027    @               mov    x, osr
        .word 0x0042    @  activeporch: jmp    x--, 2
        .word 0xe023    @               set    x, 3
        .word 0xff00    @  pulse:       set    pins, 0 [31]
        .word 0x0044    @               jmp    x--, pulse
        .word 0xff01    @  backporch:   set    pins, 1 [31]
        .word 0xff01    @               set    pins, 1 [31]
        .word 0xf401    @               set    pins, 1 [20]
        .word 0xc100    @               irq    0 [1]
                        @     .wrap

/**
 * Generating Vsync signal. Total of 12 instructions.
 *  .side_set 1 opt
 */
.align 4
vsync_gen:
        .wrod 0x80a0    @               pull   block
                        @       .wrap_target
        .word 0xa027    @               mov    x, osr
        .word 0x20c0    @  active:      wait   1 irq, 0
        .word 0xc001    @               irq    1
        .word 0x0042    @               jmp    x--, active
        .word 0x20c0    @  frontporch:  wait   1 irq, 0
        .word 0xe043    @               set    y, 3
        .word 0x30c0    @  syncpulse:   wait   1 irq, 0        side 0
        .word 0x0087    @               jmp    y--, syncpulse
        .word 0xe057    @               set    y, 23
        .word 0x38c0    @  backporch:   wait   1 irq, 0        side 1
        .word 0x008a    @               jmp    y--, 10
                        @       .wrap

#endif

.section .vidram, "wa"
.incbin "images/logo_flash.raw"

.section .data
cursor_blink_time:      .word 0
