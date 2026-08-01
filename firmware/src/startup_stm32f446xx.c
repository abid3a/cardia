/* startup_stm32f446xx.c -- reset vector and exception table, written in C.
 *
 * Why C and not the usual .s file
 * -------------------------------
 * The vendor startup file is assembly for historical reasons, not technical
 * ones. Everything it does -- lay out a table of addresses, copy one block of
 * memory, zero another -- is expressible in C, and in C it is reviewable by
 * anyone rather than only by people fluent in ARM assembler directives. The one
 * genuine constraint is that this code runs before the C runtime is set up, so
 * it must not touch any initialised global before the copy loop has run. That
 * constraint is satisfied by construction here: the only objects referenced
 * before the copy are linker-provided symbols and hardware registers.
 *
 * Boot sequence on this part: the core loads SP from the word at 0x08000000 and
 * PC from 0x08000004, then starts executing. Both come from g_vector_table
 * below, which the linker script places first in flash.
 */

#include <stdint.h>

#include "stm32f446_regs.h"
#include "system.h"

/* --- symbols defined by the linker script -------------------------------- */
extern uint32_t _sidata;  /* .data image, load address in flash */
extern uint32_t _sdata;   /* .data start, in RAM */
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;
extern uint32_t _estack;

int main(void);

void Reset_Handler(void);
void Default_Handler(void);

#define CARDIA_ISR __attribute__((weak, alias("Default_Handler")))

/* --- core exceptions ----------------------------------------------------- */
void NMI_Handler(void)          CARDIA_ISR;
void HardFault_Handler(void)    CARDIA_ISR;
void MemManage_Handler(void)    CARDIA_ISR;
void BusFault_Handler(void)     CARDIA_ISR;
void UsageFault_Handler(void)   CARDIA_ISR;
void SVC_Handler(void)          CARDIA_ISR;
void DebugMon_Handler(void)     CARDIA_ISR;
void PendSV_Handler(void)       CARDIA_ISR;
void SysTick_Handler(void)      CARDIA_ISR;

/* --- STM32F446 external interrupts (RM0390 Table 38) ---------------------- */
void WWDG_IRQHandler(void)                   CARDIA_ISR;
void PVD_IRQHandler(void)                    CARDIA_ISR;
void TAMP_STAMP_IRQHandler(void)             CARDIA_ISR;
void RTC_WKUP_IRQHandler(void)               CARDIA_ISR;
void FLASH_IRQHandler(void)                  CARDIA_ISR;
void RCC_IRQHandler(void)                    CARDIA_ISR;
void EXTI0_IRQHandler(void)                  CARDIA_ISR;
void EXTI1_IRQHandler(void)                  CARDIA_ISR;
void EXTI2_IRQHandler(void)                  CARDIA_ISR;
void EXTI3_IRQHandler(void)                  CARDIA_ISR;
void EXTI4_IRQHandler(void)                  CARDIA_ISR;
void DMA1_Stream0_IRQHandler(void)           CARDIA_ISR;
void DMA1_Stream1_IRQHandler(void)           CARDIA_ISR;
void DMA1_Stream2_IRQHandler(void)           CARDIA_ISR;
void DMA1_Stream3_IRQHandler(void)           CARDIA_ISR;
void DMA1_Stream4_IRQHandler(void)           CARDIA_ISR;
void DMA1_Stream5_IRQHandler(void)           CARDIA_ISR;
void DMA1_Stream6_IRQHandler(void)           CARDIA_ISR;
void ADC_IRQHandler(void)                    CARDIA_ISR;
void CAN1_TX_IRQHandler(void)                CARDIA_ISR;
void CAN1_RX0_IRQHandler(void)               CARDIA_ISR;
void CAN1_RX1_IRQHandler(void)               CARDIA_ISR;
void CAN1_SCE_IRQHandler(void)               CARDIA_ISR;
void EXTI9_5_IRQHandler(void)                CARDIA_ISR;
void TIM1_BRK_TIM9_IRQHandler(void)          CARDIA_ISR;
void TIM1_UP_TIM10_IRQHandler(void)          CARDIA_ISR;
void TIM1_TRG_COM_TIM11_IRQHandler(void)     CARDIA_ISR;
void TIM1_CC_IRQHandler(void)                CARDIA_ISR;
void TIM2_IRQHandler(void)                   CARDIA_ISR;
void TIM3_IRQHandler(void)                   CARDIA_ISR;
void TIM4_IRQHandler(void)                   CARDIA_ISR;
void I2C1_EV_IRQHandler(void)                CARDIA_ISR;
void I2C1_ER_IRQHandler(void)                CARDIA_ISR;
void I2C2_EV_IRQHandler(void)                CARDIA_ISR;
void I2C2_ER_IRQHandler(void)                CARDIA_ISR;
void SPI1_IRQHandler(void)                   CARDIA_ISR;
void SPI2_IRQHandler(void)                   CARDIA_ISR;
void USART1_IRQHandler(void)                 CARDIA_ISR;
void USART2_IRQHandler(void)                 CARDIA_ISR;
void USART3_IRQHandler(void)                 CARDIA_ISR;
void EXTI15_10_IRQHandler(void)              CARDIA_ISR;
void RTC_Alarm_IRQHandler(void)              CARDIA_ISR;
void OTG_FS_WKUP_IRQHandler(void)            CARDIA_ISR;
void TIM8_BRK_TIM12_IRQHandler(void)         CARDIA_ISR;
void TIM8_UP_TIM13_IRQHandler(void)          CARDIA_ISR;
void TIM8_TRG_COM_TIM14_IRQHandler(void)     CARDIA_ISR;
void TIM8_CC_IRQHandler(void)                CARDIA_ISR;
void DMA1_Stream7_IRQHandler(void)           CARDIA_ISR;
void FMC_IRQHandler(void)                    CARDIA_ISR;
void SDIO_IRQHandler(void)                   CARDIA_ISR;
void TIM5_IRQHandler(void)                   CARDIA_ISR;
void SPI3_IRQHandler(void)                   CARDIA_ISR;
void UART4_IRQHandler(void)                  CARDIA_ISR;
void UART5_IRQHandler(void)                  CARDIA_ISR;
void TIM6_DAC_IRQHandler(void)               CARDIA_ISR;
void TIM7_IRQHandler(void)                   CARDIA_ISR;
void DMA2_Stream0_IRQHandler(void)           CARDIA_ISR;   /* ADC1 ping-pong */
void DMA2_Stream1_IRQHandler(void)           CARDIA_ISR;
void DMA2_Stream2_IRQHandler(void)           CARDIA_ISR;
void DMA2_Stream3_IRQHandler(void)           CARDIA_ISR;
void DMA2_Stream4_IRQHandler(void)           CARDIA_ISR;
void CAN2_TX_IRQHandler(void)                CARDIA_ISR;
void CAN2_RX0_IRQHandler(void)               CARDIA_ISR;
void CAN2_RX1_IRQHandler(void)               CARDIA_ISR;
void CAN2_SCE_IRQHandler(void)               CARDIA_ISR;
void OTG_FS_IRQHandler(void)                 CARDIA_ISR;
void DMA2_Stream5_IRQHandler(void)           CARDIA_ISR;
void DMA2_Stream6_IRQHandler(void)           CARDIA_ISR;
void DMA2_Stream7_IRQHandler(void)           CARDIA_ISR;
void USART6_IRQHandler(void)                 CARDIA_ISR;
void I2C3_EV_IRQHandler(void)                CARDIA_ISR;
void I2C3_ER_IRQHandler(void)                CARDIA_ISR;
void OTG_HS_EP1_OUT_IRQHandler(void)         CARDIA_ISR;
void OTG_HS_EP1_IN_IRQHandler(void)          CARDIA_ISR;
void OTG_HS_WKUP_IRQHandler(void)            CARDIA_ISR;
void OTG_HS_IRQHandler(void)                 CARDIA_ISR;
void DCMI_IRQHandler(void)                   CARDIA_ISR;
void FPU_IRQHandler(void)                    CARDIA_ISR;
void SPI4_IRQHandler(void)                   CARDIA_ISR;
void SAI1_IRQHandler(void)                   CARDIA_ISR;
void SAI2_IRQHandler(void)                   CARDIA_ISR;
void QUADSPI_IRQHandler(void)                CARDIA_ISR;
void CEC_IRQHandler(void)                    CARDIA_ISR;
void SPDIF_RX_IRQHandler(void)               CARDIA_ISR;
void FMPI2C1_IRQHandler(void)                CARDIA_ISR;
void FMPI2C1_ER_IRQHandler(void)             CARDIA_ISR;

/* --- the vector table ----------------------------------------------------
 * 16 core entries followed by 97 IRQ slots. Slots the part does not implement
 * are NULL rather than a handler: they can never be taken, and leaving them
 * empty keeps the table's index arithmetic honest -- entry n + 16 is IRQ n, no
 * exceptions.
 *
 * KEEP() in the linker script prevents --gc-sections from collecting this;
 * nothing in the program references it, so without that it would vanish and the
 * chip would boot to an unmapped address.
 */
__attribute__((section(".isr_vector"), used))
const void *g_vector_table[] = {
    (const void *)&_estack,               /*   0 initial stack pointer      */
    (const void *)Reset_Handler,          /*   1 reset                      */
    (const void *)NMI_Handler,            /*   2 */
    (const void *)HardFault_Handler,      /*   3 */
    (const void *)MemManage_Handler,      /*   4 */
    (const void *)BusFault_Handler,       /*   5 */
    (const void *)UsageFault_Handler,     /*   6 */
    (const void *)0,                      /*   7 reserved */
    (const void *)0,                      /*   8 reserved */
    (const void *)0,                      /*   9 reserved */
    (const void *)0,                      /*  10 reserved */
    (const void *)SVC_Handler,            /*  11 */
    (const void *)DebugMon_Handler,       /*  12 */
    (const void *)0,                      /*  13 reserved */
    (const void *)PendSV_Handler,         /*  14 */
    (const void *)SysTick_Handler,        /*  15 */

    (const void *)WWDG_IRQHandler,               /* IRQ  0 */
    (const void *)PVD_IRQHandler,                /* IRQ  1 */
    (const void *)TAMP_STAMP_IRQHandler,         /* IRQ  2 */
    (const void *)RTC_WKUP_IRQHandler,           /* IRQ  3 */
    (const void *)FLASH_IRQHandler,              /* IRQ  4 */
    (const void *)RCC_IRQHandler,                /* IRQ  5 */
    (const void *)EXTI0_IRQHandler,              /* IRQ  6 */
    (const void *)EXTI1_IRQHandler,              /* IRQ  7 */
    (const void *)EXTI2_IRQHandler,              /* IRQ  8 */
    (const void *)EXTI3_IRQHandler,              /* IRQ  9 */
    (const void *)EXTI4_IRQHandler,              /* IRQ 10 */
    (const void *)DMA1_Stream0_IRQHandler,       /* IRQ 11 */
    (const void *)DMA1_Stream1_IRQHandler,       /* IRQ 12 */
    (const void *)DMA1_Stream2_IRQHandler,       /* IRQ 13 */
    (const void *)DMA1_Stream3_IRQHandler,       /* IRQ 14 */
    (const void *)DMA1_Stream4_IRQHandler,       /* IRQ 15 */
    (const void *)DMA1_Stream5_IRQHandler,       /* IRQ 16 */
    (const void *)DMA1_Stream6_IRQHandler,       /* IRQ 17 */
    (const void *)ADC_IRQHandler,                /* IRQ 18 */
    (const void *)CAN1_TX_IRQHandler,            /* IRQ 19 */
    (const void *)CAN1_RX0_IRQHandler,           /* IRQ 20 */
    (const void *)CAN1_RX1_IRQHandler,           /* IRQ 21 */
    (const void *)CAN1_SCE_IRQHandler,           /* IRQ 22 */
    (const void *)EXTI9_5_IRQHandler,            /* IRQ 23 */
    (const void *)TIM1_BRK_TIM9_IRQHandler,      /* IRQ 24 */
    (const void *)TIM1_UP_TIM10_IRQHandler,      /* IRQ 25 */
    (const void *)TIM1_TRG_COM_TIM11_IRQHandler, /* IRQ 26 */
    (const void *)TIM1_CC_IRQHandler,            /* IRQ 27 */
    (const void *)TIM2_IRQHandler,               /* IRQ 28 */
    (const void *)TIM3_IRQHandler,               /* IRQ 29 */
    (const void *)TIM4_IRQHandler,               /* IRQ 30 */
    (const void *)I2C1_EV_IRQHandler,            /* IRQ 31 */
    (const void *)I2C1_ER_IRQHandler,            /* IRQ 32 */
    (const void *)I2C2_EV_IRQHandler,            /* IRQ 33 */
    (const void *)I2C2_ER_IRQHandler,            /* IRQ 34 */
    (const void *)SPI1_IRQHandler,               /* IRQ 35 */
    (const void *)SPI2_IRQHandler,               /* IRQ 36 */
    (const void *)USART1_IRQHandler,             /* IRQ 37 */
    (const void *)USART2_IRQHandler,             /* IRQ 38 */
    (const void *)USART3_IRQHandler,             /* IRQ 39 */
    (const void *)EXTI15_10_IRQHandler,          /* IRQ 40 */
    (const void *)RTC_Alarm_IRQHandler,          /* IRQ 41 */
    (const void *)OTG_FS_WKUP_IRQHandler,        /* IRQ 42 */
    (const void *)TIM8_BRK_TIM12_IRQHandler,     /* IRQ 43 */
    (const void *)TIM8_UP_TIM13_IRQHandler,      /* IRQ 44 */
    (const void *)TIM8_TRG_COM_TIM14_IRQHandler, /* IRQ 45 */
    (const void *)TIM8_CC_IRQHandler,            /* IRQ 46 */
    (const void *)DMA1_Stream7_IRQHandler,       /* IRQ 47 */
    (const void *)FMC_IRQHandler,                /* IRQ 48 */
    (const void *)SDIO_IRQHandler,               /* IRQ 49 */
    (const void *)TIM5_IRQHandler,               /* IRQ 50 */
    (const void *)SPI3_IRQHandler,               /* IRQ 51 */
    (const void *)UART4_IRQHandler,              /* IRQ 52 */
    (const void *)UART5_IRQHandler,              /* IRQ 53 */
    (const void *)TIM6_DAC_IRQHandler,           /* IRQ 54 */
    (const void *)TIM7_IRQHandler,               /* IRQ 55 */
    (const void *)DMA2_Stream0_IRQHandler,       /* IRQ 56 <- ADC1 ping-pong */
    (const void *)DMA2_Stream1_IRQHandler,       /* IRQ 57 */
    (const void *)DMA2_Stream2_IRQHandler,       /* IRQ 58 */
    (const void *)DMA2_Stream3_IRQHandler,       /* IRQ 59 */
    (const void *)DMA2_Stream4_IRQHandler,       /* IRQ 60 */
    (const void *)0,                             /* IRQ 61 reserved */
    (const void *)0,                             /* IRQ 62 reserved */
    (const void *)CAN2_TX_IRQHandler,            /* IRQ 63 */
    (const void *)CAN2_RX0_IRQHandler,           /* IRQ 64 */
    (const void *)CAN2_RX1_IRQHandler,           /* IRQ 65 */
    (const void *)CAN2_SCE_IRQHandler,           /* IRQ 66 */
    (const void *)OTG_FS_IRQHandler,             /* IRQ 67 */
    (const void *)DMA2_Stream5_IRQHandler,       /* IRQ 68 */
    (const void *)DMA2_Stream6_IRQHandler,       /* IRQ 69 */
    (const void *)DMA2_Stream7_IRQHandler,       /* IRQ 70 */
    (const void *)USART6_IRQHandler,             /* IRQ 71 */
    (const void *)I2C3_EV_IRQHandler,            /* IRQ 72 */
    (const void *)I2C3_ER_IRQHandler,            /* IRQ 73 */
    (const void *)OTG_HS_EP1_OUT_IRQHandler,     /* IRQ 74 */
    (const void *)OTG_HS_EP1_IN_IRQHandler,      /* IRQ 75 */
    (const void *)OTG_HS_WKUP_IRQHandler,        /* IRQ 76 */
    (const void *)OTG_HS_IRQHandler,             /* IRQ 77 */
    (const void *)DCMI_IRQHandler,               /* IRQ 78 */
    (const void *)0,                             /* IRQ 79 reserved */
    (const void *)0,                             /* IRQ 80 reserved */
    (const void *)FPU_IRQHandler,                /* IRQ 81 */
    (const void *)0,                             /* IRQ 82 reserved */
    (const void *)0,                             /* IRQ 83 reserved */
    (const void *)SPI4_IRQHandler,               /* IRQ 84 */
    (const void *)0,                             /* IRQ 85 reserved */
    (const void *)SAI1_IRQHandler,               /* IRQ 86 */
    (const void *)0,                             /* IRQ 87 reserved */
    (const void *)0,                             /* IRQ 88 reserved */
    (const void *)0,                             /* IRQ 89 reserved */
    (const void *)SAI2_IRQHandler,               /* IRQ 90 */
    (const void *)QUADSPI_IRQHandler,            /* IRQ 91 */
    (const void *)CEC_IRQHandler,                /* IRQ 92 */
    (const void *)SPDIF_RX_IRQHandler,           /* IRQ 93 */
    (const void *)FMPI2C1_IRQHandler,            /* IRQ 94 */
    (const void *)FMPI2C1_ER_IRQHandler,         /* IRQ 95 */
    (const void *)0,                             /* IRQ 96 reserved (padding) */
};

/* --- reset --------------------------------------------------------------- */

/* The FPU must be switched on here and not one instruction later.
 *
 * The compiler is invoked with -mfloat-abi=hard, so it is free to emit VFP
 * instructions anywhere -- including in main's prologue, where VPUSH is used to
 * preserve callee-saved s16-s31 before the first floating-point statement is
 * reached. With CP10/CP11 still disabled, that VPUSH raises a UsageFault
 * (NOCP), and because it happens before anything has run there is nothing in
 * the log to explain it. The failure looks like "the board hard-faults at
 * startup for no reason", which is a genuinely nasty afternoon.
 *
 * So: enable the coprocessor before calling any C function that might touch a
 * float, and force the write to complete with DSB+ISB before the next
 * instruction is fetched -- the pipeline may already hold instructions decoded
 * against the old CPACR value.
 *
 * This function itself is marked no-float-abi-sensitive by construction: it
 * only moves integers, so it cannot trap on its own.
 */
static void fpu_enable(void)
{
    SCB->CPACR |= SCB_CPACR_FPU_FULL_ACCESS;
    cardia_dsb();
    cardia_isb();
}

void Reset_Handler(void)
{
    /* 1. Copy .data from its flash load address into RAM.
     *    Until this loop finishes, every initialised global holds garbage. */
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    /* 2. Zero .bss. The C standard promises this; nothing else provides it. */
    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0u;
    }

    /* 3. FPU on, before any float can be touched. See the note above. */
    fpu_enable();

    /* 4. Clock tree: 8 MHz HSE bypass -> 180 MHz SYSCLK. */
    SystemInit();

    /* 5. __libc_init_array is deliberately NOT called.
     *
     *    It exists to run C++ static constructors and any function tagged
     *    __attribute__((constructor)). This firmware is C, has neither, and
     *    calling it would pull in newlib initialisation for a program that
     *    never uses newlib's runtime. The .init_array section is still emitted
     *    by the linker script, so if that ever changes the only edit needed is
     *    to declare `extern void __libc_init_array(void);` and call it here. */

    (void)main();

    /* main() does not return. If it somehow does, stop rather than fall off the
     * end of flash and execute whatever follows. */
    for (;;) {
    }
}

/* Any unhandled interrupt lands here. An infinite loop rather than a return:
 * spinning leaves the machine in a state a debugger can attach to and read
 * (the stacked frame identifies the faulting instruction), whereas returning
 * from an unexpected exception would let the program limp on with a peripheral
 * screaming for attention and no evidence anything went wrong. */
void Default_Handler(void)
{
    for (;;) {
    }
}
