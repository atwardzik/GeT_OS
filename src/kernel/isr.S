#ifdef ARCH_RP2040
.cpu cortex-m0
#elifdef ARCH_RP2350
.cpu cortex-m33
.fpu fpv5-sp-d16
#endif
.thumb

.syntax unified

#include "syscall_codes.h"

.equ USER_DEFINED_IVT_OFFSET, 0x40

.extern __heap_start__
.extern __heap_end__

/**
 * Syscall        (svc #<syscall_number>)
 *  r0-r3       - arguments specified by syscalls
 */
.thumb_func
.global isr_svcall
.align 4
isr_svcall:
        sub     sp, sp, #32
        stmia   sp, {r4, r5, r7, lr}
        mov     r7, sp

        mrs     r4, CONTROL
        tst     r4, #1                  // 1 - unprivileged
        itte    eq
        mrseq   r4, msp
        addeq   r4, r4, #0x20
        mrsne   r4, psp

        adds    r4, r4, #0x18           // 0x18 from original sp - offset for return address
        ldr     r4, [r4]
        subs    r4, r4, #2              // - svc instruction is 2 bytes before, argument for svc is 2 byte before (little endian)
        ldrb    r4, [r4]


        cmp     r4, OS_INIT_SVC
        bne     .standard_syscalls
        .os_init_svc:
                bl      scheduler_get_current_process_stack     // gets a pointer to stack pointer
                mov     r12, r0
                ldr     r0, [r12]

                // cmp     r0, #0
                // itttt   eq
                // TODO: IMPLEMENT PANIC


                ldmia   r0!, {r2-r11}
                mov     lr, r2
                msr     CONTROL, r3
                isb                                             // executes an Instruction Synchronization
                                                                //  Barrier after CONTROL updates
                msr     psp, r0
                str     r0, [r12]
                ldr     r1, =2048
                sub     r0, r0, r1                              // FIXME: this will break when process with pid=1 gets more than 2KiB of stack
                msr     psplim, r0


                // set PENDSV to the lowest priority level
                // ldr     r2, NVIC_IPR_PEND_SV                  // FIXME: change to proper IPR address bits
                // ldr     r1, PPB_BASE
                // movs    r0, #0xff
                // str     r0, [r1, r2]

                // enable systick
                mov     r4, lr
                ldr     r0, =625000                             // systick every 5[ms]
                bl      systick_enable

                // enable exceptions so that they do not escalate to hardfault
                ldr     r0, PPB_BASE
                ldr     r1, SHCSR
                ldr     r2, [r0, r1]
                mov     r3, #1
                lsl     r3, r3, #18                             // USGFAULTENA
                orr     r2, r2, r3
                str     r2, [r0, r1]


                // setup kernel stack for process
                bl      scheduler_get_current_kernel_stack
                ldr     r0, [r0]
                msr     msp, r0
                mov     lr, r4
                ldr     r1, =1024
                sub     r0, r0, r1                              // FIXME: similary, this will break when pid=1 gets more than 1KiB of kernel stack
                msr     msplim, r0

                bx      lr                                      // start init (process with pid=1)

        .standard_syscalls:
                str     r0, [r7, #16]
                str     r1, [r7, #20]
                str     r2, [r7, #24]
                str     r3, [r7, #28]

                bl      set_kernel_mode_flag

                ldr     r0, [r7, #16]
                ldr     r1, [r7, #20]
                ldr     r2, [r7, #24]
                ldr     r3, [r7, #28]

        .enter_trampoline:
                ldr     r5, =syscall_table
                ldr     r4, [r5, r4, lsl #2]
                blx     r4

        .svcall_exit:
                mrs     r1, psp
                str     r0, [r1]                        // save return value on the stack
                                                        // upon returning from this exception
                                                        // the stack frame is restored, at which
                                                        // r0 is at offset 0
                bl      reset_kernel_mode_flag

                add     sp, sp, #32
                ldmia   r7, {r4, r5, r7, pc}

/**
 *
 */
.thumb_func
.global isr_pendsv
.align 4
isr_pendsv:
        b       save_usermode_and_context_switch



.thumb_func
.global systick_enable
.align 4
systick_enable:
        ldr     r1, PPB_BASE
        ldr     r2, SYST_RVR
        subs    r0, r0, #1
        str     r0, [r1, r2]

        ldr     r2, SYST_CVR
        movs    r0, #0
        str     r0, [r1, r2]

        ldr     r2, SYST_CSR
        movs    r0, #7                  // processor clock | systick exception | counter enabled
        str     r0, [r1, r2]


        bx      lr


/**
 *
 */
.thumb_func
.global isr_systick
.align 4
isr_systick:
        ldr     r0, PPB_BASE
        ldr     r1, ICSR
        ldr     r2, [r0, r1]
        movs    r3, #1
        lsls    r3, r3, #28
        orrs    r2, r2, r3
        str     r2, [r0, r1]

        bx      lr

/**
 *
 */
.thumb_func
.global isr_usagefault
.align 4
isr_usagefault:
        push    {lr}
        adr     r0, usagefault_msg
        bl      printk
        bl      sys_getpid
        mov     r1, #15         // SIGKILL
        bl      sys_kill
        bl      context_switch

        pop     {pc}


.align 4
usagefault_msg:
        .asciz "Stack Smashing Detected!\n"

/**
 * Writes N-th interrupt handler to the IVT. FIXME: currently only for irq 0-31
 *  r0 - interrupt number
 *  r1 - interrupt handler
 * */
.thumb_func
.global set_isr
.align 4
set_isr:
        ldr     r2, PPB_BASE
        ldr     r3, VTOR_OFFSET
        ldr     r2, [r2, r3]                    // read the address of IVT from VTOR hardware register

        movs    r3, #4
        muls    r3, r0, r3
        adds    r3, r3, USER_DEFINED_IVT_OFFSET
        str     r1, [r2, r3]                    // write interrupt handler

        //////// enable appropriate interrupt at the CPU by turning off and on again ////////

        ldr     r2, NVIC_ICPR0_OFFSET           // unset IRQ
        ldr     r3, NVIC_ISER0_OFFSET           // set IRQ

        cmp     r0, #32
        ittt    ge
        subsge  r0, r0, #32
        addsge  r2, r2, #4
        addsge  r3, r3, #4

        movs    r1, #1
        lsls    r0, r1, r0

        ldr     r1, PPB_BASE
        str     r0, [r1, r2]
        str     r0, [r1, r3]

        bx      lr


PPB_BASE:               .word 0xe0000000
SYST_CSR:               .word 0xe010
SYST_RVR:               .word 0xe014
ICSR:                   .word 0xed04
SHCSR:                  .word 0xed24
SYST_CVR:               .word 0xe018
VTOR_OFFSET:            .word 0xed08
NVIC_ICPR0_OFFSET:      .word 0xe280                    // interrupt clear-pending register
NVIC_ISER0_OFFSET:      .word 0xe100                    // interrupt set-enable register
NVIC_IPR_PEND_SV:       .word 0xe438                    // 14

no_init_message:        .asciz "No init method provided, aborting."
