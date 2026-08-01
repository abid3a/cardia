/* stm32f446_regs.h -- hand-written register map for the peripherals Cardia uses.
 *
 * Why this file exists instead of `#include "stm32f446xx.h"`
 * ---------------------------------------------------------
 * ST's device header is ~30k lines describing every peripheral on the part,
 * pulled in through a CMSIS pack that has to be vendored or submoduled. Cardia
 * touches eleven peripherals. Writing those eleven out by hand costs a few
 * hundred lines, removes a large binary-ish dependency from the tree, and --
 * the part that actually matters -- means every base address, offset and bit
 * position in this firmware was read out of the reference manual and typed
 * deliberately rather than inherited. A wrong bit here is a bug I own, not a
 * mystery in a vendor header.
 *
 * It also keeps the repository buildable from a bare clone with nothing but
 * arm-none-eabi-gcc, which is the same property that makes the host-side parity
 * test possible.
 *
 * Sources, and the only two documents needed to check anything below:
 *   RM0390 rev 6  -- STM32F446xx reference manual (peripherals, bit fields)
 *   PM0214 rev 10 -- Cortex-M4 programming manual (SCB, NVIC, DWT, FPU)
 *   DS10693       -- STM32F446xE datasheet (electrical limits, e.g. ADCCLK max)
 *
 * Conventions: peripherals are `volatile uint32_t` struct overlays at a fixed
 * base; reserved words are named and present so every offset is verifiable by
 * counting. Bit masks are `_Msk`, single bits are the bare name.
 */

#ifndef CARDIA_STM32F446_REGS_H
#define CARDIA_STM32F446_REGS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * Memory map (RM0390 section 2.3)
 * ======================================================================== */

#define PERIPH_BASE       0x40000000UL
#define APB1PERIPH_BASE   (PERIPH_BASE + 0x00000000UL)
#define APB2PERIPH_BASE   (PERIPH_BASE + 0x00010000UL)
#define AHB1PERIPH_BASE   (PERIPH_BASE + 0x00020000UL)

#define TIM2_BASE         (APB1PERIPH_BASE + 0x0000UL)
#define USART2_BASE       (APB1PERIPH_BASE + 0x4400UL)
#define PWR_BASE          (APB1PERIPH_BASE + 0x7000UL)

#define ADC1_BASE         (APB2PERIPH_BASE + 0x2000UL)
#define ADC_COMMON_BASE   (APB2PERIPH_BASE + 0x2300UL)

#define GPIOA_BASE        (AHB1PERIPH_BASE + 0x0000UL)
#define GPIOB_BASE        (AHB1PERIPH_BASE + 0x0400UL)
#define GPIOC_BASE        (AHB1PERIPH_BASE + 0x0800UL)
#define RCC_BASE          (AHB1PERIPH_BASE + 0x3800UL)
#define FLASH_R_BASE      (AHB1PERIPH_BASE + 0x3C00UL)
#define DMA1_BASE         (AHB1PERIPH_BASE + 0x6000UL)
#define DMA2_BASE         (AHB1PERIPH_BASE + 0x6400UL)

/* Cortex-M4 private peripheral bus (PM0214 sections 4.1, 4.3, 11.2) */
#define SCS_BASE          0xE000E000UL
#define NVIC_BASE         (SCS_BASE + 0x0100UL)
#define SCB_BASE          (SCS_BASE + 0x0D00UL)
#define DWT_BASE          0xE0001000UL
#define COREDEBUG_BASE    0xE000EDF0UL

/* ==========================================================================
 * RCC -- reset and clock control (RM0390 section 6.3)
 * ======================================================================== */

typedef struct {
    volatile uint32_t CR;            /* 0x00 clock control */
    volatile uint32_t PLLCFGR;       /* 0x04 PLL configuration */
    volatile uint32_t CFGR;          /* 0x08 clock configuration */
    volatile uint32_t CIR;           /* 0x0C clock interrupt */
    volatile uint32_t AHB1RSTR;      /* 0x10 */
    volatile uint32_t AHB2RSTR;      /* 0x14 */
    volatile uint32_t AHB3RSTR;      /* 0x18 */
    uint32_t          RESERVED0;     /* 0x1C */
    volatile uint32_t APB1RSTR;      /* 0x20 */
    volatile uint32_t APB2RSTR;      /* 0x24 */
    uint32_t          RESERVED1[2];  /* 0x28 0x2C */
    volatile uint32_t AHB1ENR;       /* 0x30 */
    volatile uint32_t AHB2ENR;       /* 0x34 */
    volatile uint32_t AHB3ENR;       /* 0x38 */
    uint32_t          RESERVED2;     /* 0x3C */
    volatile uint32_t APB1ENR;       /* 0x40 */
    volatile uint32_t APB2ENR;       /* 0x44 */
    uint32_t          RESERVED3[2];  /* 0x48 0x4C */
    volatile uint32_t AHB1LPENR;     /* 0x50 */
    volatile uint32_t AHB2LPENR;     /* 0x54 */
    volatile uint32_t AHB3LPENR;     /* 0x58 */
    uint32_t          RESERVED4;     /* 0x5C */
    volatile uint32_t APB1LPENR;     /* 0x60 */
    volatile uint32_t APB2LPENR;     /* 0x64 */
    uint32_t          RESERVED5[2];  /* 0x68 0x6C */
    volatile uint32_t BDCR;          /* 0x70 */
    volatile uint32_t CSR;           /* 0x74 */
    uint32_t          RESERVED6[2];  /* 0x78 0x7C */
    volatile uint32_t SSCGR;         /* 0x80 */
    volatile uint32_t PLLI2SCFGR;    /* 0x84 */
    volatile uint32_t PLLSAICFGR;    /* 0x88 */
    volatile uint32_t DCKCFGR;       /* 0x8C */
    volatile uint32_t CKGATENR;      /* 0x90 */
    volatile uint32_t DCKCFGR2;      /* 0x94 */
} RCC_TypeDef;

#define RCC ((RCC_TypeDef *)RCC_BASE)

/* RCC_CR */
#define RCC_CR_HSION            (1UL << 0)
#define RCC_CR_HSIRDY           (1UL << 1)
#define RCC_CR_HSEON            (1UL << 16)
#define RCC_CR_HSERDY           (1UL << 17)
#define RCC_CR_HSEBYP           (1UL << 18)
#define RCC_CR_CSSON            (1UL << 19)
#define RCC_CR_PLLON            (1UL << 24)
#define RCC_CR_PLLRDY           (1UL << 25)

/* RCC_PLLCFGR */
#define RCC_PLLCFGR_PLLM_Pos    0
#define RCC_PLLCFGR_PLLM_Msk    (0x3FUL << RCC_PLLCFGR_PLLM_Pos)
#define RCC_PLLCFGR_PLLN_Pos    6
#define RCC_PLLCFGR_PLLN_Msk    (0x1FFUL << RCC_PLLCFGR_PLLN_Pos)
#define RCC_PLLCFGR_PLLP_Pos    16
#define RCC_PLLCFGR_PLLP_Msk    (0x3UL << RCC_PLLCFGR_PLLP_Pos)
#define RCC_PLLCFGR_PLLSRC_HSE  (1UL << 22)
#define RCC_PLLCFGR_PLLQ_Pos    24
#define RCC_PLLCFGR_PLLQ_Msk    (0xFUL << RCC_PLLCFGR_PLLQ_Pos)

/* RCC_CFGR */
#define RCC_CFGR_SW_Pos         0
#define RCC_CFGR_SW_Msk         (0x3UL << RCC_CFGR_SW_Pos)
#define RCC_CFGR_SW_PLL         (0x2UL << RCC_CFGR_SW_Pos)
#define RCC_CFGR_SWS_Pos        2
#define RCC_CFGR_SWS_Msk        (0x3UL << RCC_CFGR_SWS_Pos)
#define RCC_CFGR_SWS_PLL        (0x2UL << RCC_CFGR_SWS_Pos)
#define RCC_CFGR_HPRE_Pos       4
#define RCC_CFGR_HPRE_Msk       (0xFUL << RCC_CFGR_HPRE_Pos)
#define RCC_CFGR_HPRE_DIV1      (0x0UL << RCC_CFGR_HPRE_Pos)
#define RCC_CFGR_PPRE1_Pos      10
#define RCC_CFGR_PPRE1_Msk      (0x7UL << RCC_CFGR_PPRE1_Pos)
#define RCC_CFGR_PPRE1_DIV4     (0x5UL << RCC_CFGR_PPRE1_Pos)
#define RCC_CFGR_PPRE2_Pos      13
#define RCC_CFGR_PPRE2_Msk      (0x7UL << RCC_CFGR_PPRE2_Pos)
#define RCC_CFGR_PPRE2_DIV2     (0x4UL << RCC_CFGR_PPRE2_Pos)

/* RCC_AHB1ENR */
#define RCC_AHB1ENR_GPIOAEN     (1UL << 0)
#define RCC_AHB1ENR_GPIOBEN     (1UL << 1)
#define RCC_AHB1ENR_GPIOCEN     (1UL << 2)
#define RCC_AHB1ENR_DMA1EN      (1UL << 21)
#define RCC_AHB1ENR_DMA2EN      (1UL << 22)

/* RCC_APB1ENR */
#define RCC_APB1ENR_TIM2EN      (1UL << 0)
#define RCC_APB1ENR_USART2EN    (1UL << 17)
#define RCC_APB1ENR_PWREN       (1UL << 28)

/* RCC_APB2ENR */
#define RCC_APB2ENR_ADC1EN      (1UL << 8)
#define RCC_APB2ENR_SYSCFGEN    (1UL << 14)

/* ==========================================================================
 * PWR -- power control (RM0390 section 5.4)
 *
 * Over-drive lives here, and it is mandatory above 168 MHz on this part.
 * ======================================================================== */

typedef struct {
    volatile uint32_t CR;   /* 0x00 */
    volatile uint32_t CSR;  /* 0x04 */
} PWR_TypeDef;

#define PWR ((PWR_TypeDef *)PWR_BASE)

#define PWR_CR_VOS_Pos          14
#define PWR_CR_VOS_Msk          (0x3UL << PWR_CR_VOS_Pos)
#define PWR_CR_VOS_SCALE1       (0x3UL << PWR_CR_VOS_Pos)  /* required >168 MHz */
#define PWR_CR_ODEN             (1UL << 16)
#define PWR_CR_ODSWEN           (1UL << 17)

#define PWR_CSR_VOSRDY          (1UL << 14)
#define PWR_CSR_ODRDY           (1UL << 16)
#define PWR_CSR_ODSWRDY         (1UL << 17)

/* ==========================================================================
 * FLASH interface (RM0390 section 3.4)
 * ======================================================================== */

typedef struct {
    volatile uint32_t ACR;      /* 0x00 access control */
    volatile uint32_t KEYR;     /* 0x04 */
    volatile uint32_t OPTKEYR;  /* 0x08 */
    volatile uint32_t SR;       /* 0x0C */
    volatile uint32_t CR;       /* 0x10 */
    volatile uint32_t OPTCR;    /* 0x14 */
} FLASH_TypeDef;

#define FLASH ((FLASH_TypeDef *)FLASH_R_BASE)

#define FLASH_ACR_LATENCY_Pos   0
#define FLASH_ACR_LATENCY_Msk   (0xFUL << FLASH_ACR_LATENCY_Pos)
#define FLASH_ACR_PRFTEN        (1UL << 8)
#define FLASH_ACR_ICEN          (1UL << 9)
#define FLASH_ACR_DCEN          (1UL << 10)

/* ==========================================================================
 * GPIO (RM0390 section 8.4)
 * ======================================================================== */

typedef struct {
    volatile uint32_t MODER;    /* 0x00 */
    volatile uint32_t OTYPER;   /* 0x04 */
    volatile uint32_t OSPEEDR;  /* 0x08 */
    volatile uint32_t PUPDR;    /* 0x0C */
    volatile uint32_t IDR;      /* 0x10 */
    volatile uint32_t ODR;      /* 0x14 */
    volatile uint32_t BSRR;     /* 0x18 */
    volatile uint32_t LCKR;     /* 0x1C */
    volatile uint32_t AFR[2];   /* 0x20 AFRL, 0x24 AFRH */
} GPIO_TypeDef;

#define GPIOA ((GPIO_TypeDef *)GPIOA_BASE)
#define GPIOB ((GPIO_TypeDef *)GPIOB_BASE)
#define GPIOC ((GPIO_TypeDef *)GPIOC_BASE)

/* ==========================================================================
 * ADC (RM0390 section 13.13)
 * ======================================================================== */

typedef struct {
    volatile uint32_t SR;       /* 0x00 status */
    volatile uint32_t CR1;      /* 0x04 */
    volatile uint32_t CR2;      /* 0x08 */
    volatile uint32_t SMPR1;    /* 0x0C sample time, channels 10..18 */
    volatile uint32_t SMPR2;    /* 0x10 sample time, channels 0..9 */
    volatile uint32_t JOFR[4];  /* 0x14..0x20 */
    volatile uint32_t HTR;      /* 0x24 */
    volatile uint32_t LTR;      /* 0x28 */
    volatile uint32_t SQR1;     /* 0x2C */
    volatile uint32_t SQR2;     /* 0x30 */
    volatile uint32_t SQR3;     /* 0x34 */
    volatile uint32_t JSQR;     /* 0x38 */
    volatile uint32_t JDR[4];   /* 0x3C..0x48 */
    volatile uint32_t DR;       /* 0x4C regular data -- the DMA source */
} ADC_TypeDef;

typedef struct {
    volatile uint32_t CSR;  /* 0x00 */
    volatile uint32_t CCR;  /* 0x04 common control: ADC prescaler lives here */
    volatile uint32_t CDR;  /* 0x08 */
} ADC_Common_TypeDef;

#define ADC1       ((ADC_TypeDef *)ADC1_BASE)
#define ADC_COMMON ((ADC_Common_TypeDef *)ADC_COMMON_BASE)

#define ADC_SR_EOC              (1UL << 1)
#define ADC_SR_OVR              (1UL << 5)

#define ADC_CR1_EOCIE           (1UL << 5)
#define ADC_CR1_SCAN            (1UL << 8)
#define ADC_CR1_RES_Pos         24
#define ADC_CR1_RES_Msk         (0x3UL << ADC_CR1_RES_Pos)
#define ADC_CR1_RES_12B         (0x0UL << ADC_CR1_RES_Pos)

#define ADC_CR2_ADON            (1UL << 0)
#define ADC_CR2_CONT            (1UL << 1)
#define ADC_CR2_DMA             (1UL << 8)
#define ADC_CR2_DDS             (1UL << 9)   /* keep issuing DMA requests */
#define ADC_CR2_EOCS            (1UL << 10)
#define ADC_CR2_ALIGN           (1UL << 11)
#define ADC_CR2_EXTSEL_Pos      24
#define ADC_CR2_EXTSEL_Msk      (0xFUL << ADC_CR2_EXTSEL_Pos)
/* RM0390 Table 74: regular-channel external trigger selection.
 * 0110 is TIM2_TRGO -- the update event Cardia's sample clock generates. */
#define ADC_CR2_EXTSEL_TIM2_TRGO (0x6UL << ADC_CR2_EXTSEL_Pos)
#define ADC_CR2_EXTEN_Pos       28
#define ADC_CR2_EXTEN_Msk       (0x3UL << ADC_CR2_EXTEN_Pos)
#define ADC_CR2_EXTEN_RISING    (0x1UL << ADC_CR2_EXTEN_Pos)
#define ADC_CR2_SWSTART         (1UL << 30)

#define ADC_SQR1_L_Pos          20
#define ADC_SQR1_L_Msk          (0xFUL << ADC_SQR1_L_Pos)

#define ADC_SMPR2_SMP0_Pos      0
#define ADC_SMPR2_SMP0_Msk      (0x7UL << ADC_SMPR2_SMP0_Pos)
#define ADC_SMP_84CYC           0x4UL

#define ADC_CCR_ADCPRE_Pos      16
#define ADC_CCR_ADCPRE_Msk      (0x3UL << ADC_CCR_ADCPRE_Pos)
/* DS10693: f_ADC max is 36 MHz. PCLK2 is 90 MHz here, so /2 (45 MHz) is out of
 * spec and /4 (22.5 MHz) is the fastest legal divider. Getting this wrong is
 * the classic "conversions are subtly wrong at the top of the range" bug. */
#define ADC_CCR_ADCPRE_DIV4     (0x1UL << ADC_CCR_ADCPRE_Pos)

/* ==========================================================================
 * DMA (RM0390 section 9.5)
 * ======================================================================== */

typedef struct {
    volatile uint32_t CR;    /* 0x00 configuration */
    volatile uint32_t NDTR;  /* 0x04 number of data items */
    volatile uint32_t PAR;   /* 0x08 peripheral address */
    volatile uint32_t M0AR;  /* 0x0C memory 0 address */
    volatile uint32_t M1AR;  /* 0x10 memory 1 address (double-buffer target) */
    volatile uint32_t FCR;   /* 0x14 FIFO control */
} DMA_Stream_TypeDef;

typedef struct {
    volatile uint32_t LISR;   /* 0x00 interrupt status, streams 0..3 */
    volatile uint32_t HISR;   /* 0x04 interrupt status, streams 4..7 */
    volatile uint32_t LIFCR;  /* 0x08 interrupt flag clear, streams 0..3 */
    volatile uint32_t HIFCR;  /* 0x0C interrupt flag clear, streams 4..7 */
} DMA_TypeDef;

#define DMA2          ((DMA_TypeDef *)DMA2_BASE)
#define DMA2_Stream0  ((DMA_Stream_TypeDef *)(DMA2_BASE + 0x010UL))

#define DMA_SxCR_EN             (1UL << 0)
#define DMA_SxCR_DMEIE          (1UL << 1)
#define DMA_SxCR_TEIE           (1UL << 2)
#define DMA_SxCR_HTIE           (1UL << 3)
#define DMA_SxCR_TCIE           (1UL << 4)
#define DMA_SxCR_DIR_Pos        6
#define DMA_SxCR_DIR_Msk        (0x3UL << DMA_SxCR_DIR_Pos)
#define DMA_SxCR_DIR_P2M        (0x0UL << DMA_SxCR_DIR_Pos)
#define DMA_SxCR_CIRC           (1UL << 8)
#define DMA_SxCR_PINC           (1UL << 9)
#define DMA_SxCR_MINC           (1UL << 10)
#define DMA_SxCR_PSIZE_Pos      11
#define DMA_SxCR_PSIZE_Msk      (0x3UL << DMA_SxCR_PSIZE_Pos)
#define DMA_SxCR_PSIZE_HALF     (0x1UL << DMA_SxCR_PSIZE_Pos)
#define DMA_SxCR_MSIZE_Pos      13
#define DMA_SxCR_MSIZE_Msk      (0x3UL << DMA_SxCR_MSIZE_Pos)
#define DMA_SxCR_MSIZE_HALF     (0x1UL << DMA_SxCR_MSIZE_Pos)
#define DMA_SxCR_PL_Pos         16
#define DMA_SxCR_PL_Msk         (0x3UL << DMA_SxCR_PL_Pos)
#define DMA_SxCR_PL_HIGH        (0x2UL << DMA_SxCR_PL_Pos)
#define DMA_SxCR_DBM            (1UL << 18)  /* double buffer */
#define DMA_SxCR_CT             (1UL << 19)  /* current target: 0=M0AR 1=M1AR */
#define DMA_SxCR_CHSEL_Pos      25
#define DMA_SxCR_CHSEL_Msk      (0x7UL << DMA_SxCR_CHSEL_Pos)

#define DMA_SxFCR_DMDIS         (1UL << 2)   /* disable direct mode */

/* LISR / LIFCR bit positions for stream 0 (RM0390 Tables 9.5.1, 9.5.3). */
#define DMA_LISR_FEIF0          (1UL << 0)
#define DMA_LISR_DMEIF0         (1UL << 2)
#define DMA_LISR_TEIF0          (1UL << 3)
#define DMA_LISR_HTIF0          (1UL << 4)
#define DMA_LISR_TCIF0          (1UL << 5)

/* ==========================================================================
 * TIM2 -- 32-bit general-purpose timer (RM0390 section 17.4)
 * ======================================================================== */

typedef struct {
    volatile uint32_t CR1;    /* 0x00 */
    volatile uint32_t CR2;    /* 0x04 -- MMS (TRGO source) lives here */
    volatile uint32_t SMCR;   /* 0x08 */
    volatile uint32_t DIER;   /* 0x0C */
    volatile uint32_t SR;     /* 0x10 */
    volatile uint32_t EGR;    /* 0x14 */
    volatile uint32_t CCMR1;  /* 0x18 */
    volatile uint32_t CCMR2;  /* 0x1C */
    volatile uint32_t CCER;   /* 0x20 */
    volatile uint32_t CNT;    /* 0x24 */
    volatile uint32_t PSC;    /* 0x28 */
    volatile uint32_t ARR;    /* 0x2C */
    uint32_t          RESERVED0; /* 0x30 (RCR, advanced timers only) */
    volatile uint32_t CCR1;   /* 0x34 */
    volatile uint32_t CCR2;   /* 0x38 */
    volatile uint32_t CCR3;   /* 0x3C */
    volatile uint32_t CCR4;   /* 0x40 */
    uint32_t          RESERVED1; /* 0x44 (BDTR, advanced timers only) */
    volatile uint32_t DCR;    /* 0x48 */
    volatile uint32_t DMAR;   /* 0x4C */
    volatile uint32_t OR;     /* 0x50 */
} TIM_TypeDef;

#define TIM2 ((TIM_TypeDef *)TIM2_BASE)

#define TIM_CR1_CEN             (1UL << 0)
#define TIM_CR1_UDIS            (1UL << 1)
#define TIM_CR1_URS             (1UL << 2)
#define TIM_CR1_ARPE            (1UL << 7)

#define TIM_CR2_MMS_Pos         4
#define TIM_CR2_MMS_Msk         (0x7UL << TIM_CR2_MMS_Pos)
#define TIM_CR2_MMS_UPDATE      (0x2UL << TIM_CR2_MMS_Pos)  /* 010: TRGO = UEV */

#define TIM_EGR_UG              (1UL << 0)
#define TIM_SR_UIF              (1UL << 0)

/* ==========================================================================
 * USART (RM0390 section 30.6)
 * ======================================================================== */

typedef struct {
    volatile uint32_t SR;    /* 0x00 */
    volatile uint32_t DR;    /* 0x04 */
    volatile uint32_t BRR;   /* 0x08 */
    volatile uint32_t CR1;   /* 0x0C */
    volatile uint32_t CR2;   /* 0x10 */
    volatile uint32_t CR3;   /* 0x14 */
    volatile uint32_t GTPR;  /* 0x18 */
} USART_TypeDef;

#define USART2 ((USART_TypeDef *)USART2_BASE)

#define USART_SR_ORE            (1UL << 3)
#define USART_SR_RXNE           (1UL << 5)
#define USART_SR_TC             (1UL << 6)
#define USART_SR_TXE            (1UL << 7)

#define USART_CR1_RE            (1UL << 2)
#define USART_CR1_TE            (1UL << 3)
#define USART_CR1_RXNEIE        (1UL << 5)
#define USART_CR1_M             (1UL << 12)   /* 0 = 8 data bits */
#define USART_CR1_UE            (1UL << 13)
#define USART_CR1_OVER8         (1UL << 15)

#define USART_CR2_STOP_Pos      12
#define USART_CR2_STOP_Msk      (0x3UL << USART_CR2_STOP_Pos)
#define USART_CR2_STOP_1BIT     (0x0UL << USART_CR2_STOP_Pos)

/* ==========================================================================
 * Cortex-M4 core blocks (PM0214)
 * ======================================================================== */

typedef struct {
    volatile uint32_t ISER[8];
    uint32_t          RESERVED0[24];
    volatile uint32_t ICER[8];
    uint32_t          RESERVED1[24];
    volatile uint32_t ISPR[8];
    uint32_t          RESERVED2[24];
    volatile uint32_t ICPR[8];
    uint32_t          RESERVED3[24];
    volatile uint32_t IABR[8];
    uint32_t          RESERVED4[56];
    volatile uint8_t  IP[240];
} NVIC_TypeDef;

#define NVIC ((NVIC_TypeDef *)NVIC_BASE)

static inline void nvic_enable_irq(uint32_t irqn)
{
    NVIC->ISER[irqn >> 5] = 1UL << (irqn & 0x1FUL);
}

static inline void nvic_set_priority(uint32_t irqn, uint8_t priority)
{
    /* F4 implements the top 4 bits of the 8-bit priority field. */
    NVIC->IP[irqn] = (uint8_t)(priority << 4);
}

/* Interrupt numbers actually used (RM0390 Table 38). */
#define ADC_IRQn            18
#define TIM2_IRQn           28
#define USART2_IRQn         38
#define DMA2_Stream0_IRQn   56

/* System control block. Only CPACR is touched, but the neighbours are named so
 * the offsets are checkable. */
typedef struct {
    volatile uint32_t CPUID;  /* 0xE000ED00 */
    volatile uint32_t ICSR;
    volatile uint32_t VTOR;
    volatile uint32_t AIRCR;
    volatile uint32_t SCR;
    volatile uint32_t CCR;
    volatile uint8_t  SHP[12];
    volatile uint32_t SHCSR;
    volatile uint32_t CFSR;
    volatile uint32_t HFSR;
    volatile uint32_t DFSR;
    volatile uint32_t MMFAR;
    volatile uint32_t BFAR;
    volatile uint32_t AFSR;
    volatile uint32_t PFR[2];
    volatile uint32_t DFR;
    volatile uint32_t ADR;
    volatile uint32_t MMFR[4];
    volatile uint32_t ISAR[5];
    uint32_t          RESERVED0[5];
    volatile uint32_t CPACR;  /* 0xE000ED88 coprocessor access control */
} SCB_TypeDef;

#define SCB ((SCB_TypeDef *)SCB_BASE)

/* CP10/CP11 are the single- and double-precision FPU banks. 0b11 in each
 * two-bit field is "full access from privileged and unprivileged code". */
#define SCB_CPACR_FPU_FULL_ACCESS  (0xFUL << 20)

/* Data watchpoint and trace unit -- the free-running cycle counter used for
 * every latency number this firmware reports. */
typedef struct {
    volatile uint32_t CTRL;    /* 0x00 */
    volatile uint32_t CYCCNT;  /* 0x04 */
} DWT_TypeDef;

#define DWT ((DWT_TypeDef *)DWT_BASE)

#define DWT_CTRL_CYCCNTENA      (1UL << 0)

typedef struct {
    volatile uint32_t DHCSR;  /* 0xE000EDF0 */
    volatile uint32_t DCRSR;
    volatile uint32_t DCRDR;
    volatile uint32_t DEMCR;  /* 0xE000EDFC */
} CoreDebug_TypeDef;

#define CoreDebug ((CoreDebug_TypeDef *)COREDEBUG_BASE)

#define CoreDebug_DEMCR_TRCENA  (1UL << 24)

/* --- small helpers -------------------------------------------------------- */

static inline void cardia_dsb(void) { __asm volatile ("dsb 0xF" ::: "memory"); }
static inline void cardia_isb(void) { __asm volatile ("isb 0xF" ::: "memory"); }
static inline void cardia_wfi(void) { __asm volatile ("wfi"); }
static inline void cardia_irq_enable(void)  { __asm volatile ("cpsie i" ::: "memory"); }
static inline void cardia_irq_disable(void) { __asm volatile ("cpsid i" ::: "memory"); }

#ifdef __cplusplus
}
#endif

#endif /* CARDIA_STM32F446_REGS_H */
