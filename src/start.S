#ifdef ARCH_RP2040
.cpu cortex-m0
#elifdef ARCH_RP2350
.cpu cortex-m33
#endif
.thumb

.syntax unified

.extern __stack_end__
.extern __bss_start__

/**
 * Because the stack operations in the Cortex-M23 and Cortex-M33 processors are based on
 * full descending stack (the SP’s value decrements before storing the data during a stack PUSH
 * operation), the initial SP value should be set to the first address location after the last allocated
 * address of the stack memory. For example, if you have a stack memory range from
 * 0x20007C00 to 0x20007FFF (1 K bytes), the initial stack value should be set to 0x20008000.
 */

.section .vectors, "ax"
.align 2
.global vector_table
.thumb_func
vector_table:
.word __stack_end__
.word reset             @
.word isr_nmi           @
.word isr_hardfault     @
.word isr_memmanage     @ Memory Protection Violation
.word isr_busfault      @ Fault related to instruction execution
.word isr_usagefault    @
.word isr_invalid       @ Reserved, should never fire
.word isr_invalid       @ Reserved, should never fire
.word isr_invalid       @ Reserved, should never fire
.word isr_invalid       @ Reserved, should never fire
.word isr_svcall        @
.word isr_invalid       @ Reserved, should never fire
.word isr_invalid       @ Reserved, should never fire
.word isr_pendsv
.word isr_systick
#ifdef ARCH_RP2040
irq_0_31: .fill 32, 4, 0
#elifdef ARCH_RP2350
irq_0_51: .fill 52, 4, 0
#endif

.macro decl_isr_bkpt name
.weak \name
.type \name,%function
.thumb_func
\name:
    bkpt #0
.endm

decl_isr_bkpt isr_nmi
decl_isr_bkpt isr_hardfault
decl_isr_bkpt isr_memmanage
decl_isr_bkpt isr_busfault
decl_isr_bkpt isr_usagefault
decl_isr_bkpt isr_invalid


.section .reset, "ax"

.thumb_func
.global reset
.align 4
reset:
        ldr     r2, PPB_BASE
        ldr     r1, VTOR_OFFSET
        add     r1, r1, r2
        ldr     r0, =vector_table
        str     r0, [r1]

        ldr     r1, =__stack_end__
        mov     sp, r1

clear_bss:
        ldr     r1, =__bss_start__
        ldr     r2, =0x1000            @ size of bss
        add     r2, r1, r2
        movs    r0, #0

        .clear_loop:
                strb    r0, [r1]
                adds    r1, #1
                cmp     r1, r2
                bne     .clear_loop

platform_entry:
        ldr     r1, =main
        blx     r1
        mov     r0, r0
        bkpt    #0                     @ should not return


.align 4
PPB_BASE:               .word 0xe0000000
VTOR_OFFSET:            .word 0xed08
