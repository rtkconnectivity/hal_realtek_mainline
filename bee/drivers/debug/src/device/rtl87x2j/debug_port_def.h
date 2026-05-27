/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

#ifndef RTL_DEBUG_PORT_DEF_H
#define RTL_DEBUG_PORT_DEF_H

#ifdef  __cplusplus
extern "C" {
#endif /* __cplusplus */

/*============================================================================*
 *                        Header Files
 *============================================================================*/
#include "utils/rtl_utils.h"
#include "pcc_reg.h"

/*============================================================================*
 *                          Debug Port Defines
 *============================================================================*/
#define DEBUG_PORT_SUPPORT_MULTI_PLATFORM            (0)
#define DEBUG_PORT_SUPPORT_QACTIVE_STATUS            (1)
#define DEBUG_PORT_SUPPORT_AON_DEBUG                 (1)

/*============================================================================*
 *                          Debug Port TYPE
 *============================================================================*/
typedef enum
{
    DBG_PIN_GROUP0,  //P0_0 P0_1 P0_2 P0_3
    DBG_PIN_GROUP1,  //P0_4 P0_5 P0_6 P0_7,
    DBG_PIN_GROUP2,  //P1_0 P1_1 P1_6 P1_7
    DBG_PIN_GROUP3,  //P2_0 P2_1 P2_2 P2_3
    DBG_PIN_GROUP4,  //P2_4 P2_5 P2_6 P2_7
    DBG_PIN_GROUP5,  //P3_0 P3_1 P3_2 P3_3
    DBG_PIN_GROUP6,  //P3_4 P3_5 P3_6 P4_0
    DBG_PIN_GROUP7,  //P4_1 P4_2 P4_3 P5_0
    DBG_PIN_GROUP8,  //P5_1 P5_2 P6_0 P6_1
    DBG_PIN_GROUP9,  //P6_2 P6_3 P6_4 P6_5
    DBG_PIN_GROUP10, //P6_6 P6_7 P7_0 P7_1
    DBG_PIN_GROUP11, //P7_2 P7_3 P7_4 P7_5
    DBG_PIN_GROUP_INVALID = 0xFF,
} T_PIN_GROUP;

typedef enum
{
    DIGI_DBG_SYSON,
    DIGI_DBG_BT,
    DIGI_DBG_PROC,
    DIGI_DBG_ADC,
    DIGI_DBG_RTC,
    DIGI_DBG_SPORT0,
    DIGI_DBG_SPORT1,
    DIGI_DBG_AUDIO,
    DIGI_DBG_CLK_RST_GEN,
    DIGI_DBG_FWREG_CTRL,
    DIGI_DBG_PCK600_VAON,
    DIGI_DBG_PCK600_VPON,
    DIGI_DBG_RAP_OUT,
    DIGI_DBG_FWREG_INVERSE,
    DIGI_DBG_PCK600_VCORE,
} T_DEBUG_MODE;

typedef enum
{
    PCK600_QACTIVE_CORE_DBG_TYPE = 0,
    PCK600_QACTIVE_CORE_LPDQ_OSC_QREQ,
    PCK600_QACTIVE_CORE_LPDQ_OSC_QACCEPT,
    PCK600_QACTIVE_CORE_LPDQ_OSC_QACTIVE_1,
    PCK600_QACTIVE_CORE_LPDQ_OSC_CG,
    PCK600_QACTIVE_CORE_LPDQ_PLL1_1,
    PCK600_QACTIVE_CORE_LPDQ_SDM_QREQ,
    PCK600_QACTIVE_CORE_LPDQ_SDM_QACCEPT,
    PCK600_QACTIVE_CORE_LPDQ_SDM_QACTIVE,
    PCK600_QACTIVE_CORE_LPDQ_XTAL_QREQ,
    PCK600_QACTIVE_CORE_LPDQ_XTAL_QACCEPT,
    PCK600_QACTIVE_CORE_LPDQ_XTAL_QACTIVE_1,
    PCK600_QACTIVE_CORE_LPDQ_XTAL_1,
    PCK600_QACTIVE_CORE_LPDQ_PLATFORM,
    PCK600_QACTIVE_CORE_LPDQ_PLATFORM_QACTIVE,
    PCK600_QACTIVE_CORE_LPDQ_OSC1,
    PCK600_QACTIVE_CORE_LPDQ_VCORE_QREQ,
    PCK600_QACTIVE_CORE_LPDQ_VCORE_QACCEPT,
    PCK600_QACTIVE_CORE_LPDQ_VCORE_QACTIVE_1,
    PCK600_QACTIVE_CORE_LPDQ_VCORE_1_QREQ,
    PCK600_QACTIVE_CORE_LPDQ_VCORE_1_QACCEPT,
    PCK600_QACTIVE_CORE_LPDQ_VCORE_1_QACTIVE,
    PCK600_QACTIVE_CORE_DEVICE2,
    PCK600_QACTIVE_CORE_DEVICE3,
    PCK600_QACTIVE_CORE_DEVICE4,
    PCK600_QACTIVE_CORE_DEVICE5,
    PCK600_QACTIVE_CORE_DEVICE6,
    PCK600_QACTIVE_CORE_DEVICE7,
    PCK600_QACTIVE_CORE_DEVICE_GROUP,
    PCK600_QACTIVE_CORE_DEVICE_POW,
    PCK600_QACTIVE_CORE_LPDQ_OSC,
    PCK600_QACTIVE_CORE_LPDQ_OSC10M,
    PCK600_QACTIVE_CORE_LPDQ_XTAL,
    PCK600_QACTIVE_CORE_DEVICE1,
    PCK600_QACTIVE_CORE_DEVICE0,
    PCK600_QACTIVE_CORE_LPCQ_BT,
    PCK600_QACTIVE_CORE_LPCQ_PLATFORM,
    PCK600_QACTIVE_CORE_CLKCTRL_PPU_VCORE,

    PCK600_QACTIVE_PON_DBG_TYPE = 0x100,
    PCK600_QACTIVE_PON_PPU_VCORE,
    PCK600_QACTIVE_PON_CLK_SOURCE,
    PCK600_QACTIVE_PON_VPON_DEVICE,
    PCK600_QACTIVE_PON_VPON_SCH_PLAN,
    PCK600_QACTIVE_PON_PPU_VCORE_PCSM,
    PCK600_QACTIVE_PON_LPDQ_PPU_VCORE,
    PCK600_QACTIVE_PON_LPDQ_OSC128K_VPON,
    PCK600_QACTIVE_PON_LPDQ_OSC10M_VPON,
    PCK600_QACTIVE_PON_LPDQ_OSC10M_VPON_QACTIVE,
    PCK600_QACTIVE_PON_LPDQ_OSC10M_VPON_HV,
    PCK600_QACTIVE_PON_LPCQ_PLATFORM,
    PCK600_QACTIVE_PON_CLKCTRL_PPU_VCORE,

    PCK600_QACTIVE_AON_DBG_TYPE = 0x1000,
    PCK600_QACTIVE_AON_PPU_VPON,
    PCK600_QACTIVE_AON_PPU_VPON_PCSM,
    PCK600_QACTIVE_AON_PPU_VPON_QACTIVE,
    PCK600_QACTIVE_AON_LPDQ_VPON,
    PCK600_QACTIVE_AON_LPDQ_VPON_QACTIVE_2,
    PCK600_QACTIVE_AON_LPDQ_VPON_QACTIVE_1,
    PCK600_QACTIVE_AON_LPDQ_VPON_QACTIVE_0,
    PCK600_QACTIVE_AON_LPDQ_OSC10M,
    PCK600_QACTIVE_AON_P2Q_OPMODE,
    PCK600_QACTIVE_AON_P2Q_MEMOFF,
    PCK600_QACTIVE_AON_P2Q_ON,
    PCK600_QACTIVE_AON_CLK_SOURCE_VAON
} PCK600_QACTIVE_GROUP_T;

typedef enum
{
    PCK600_QACTIVE_I2S        = 0,
    PCK600_QACTIVE_ADC        = 1,
    PCK600_QACTIVE_UART3      = 2,
    PCK600_QACTIVE_UART2      = 3,
    PCK600_QACTIVE_UART1      = 4,
    PCK600_QACTIVE_UART0      = 5,
    PCK600_QACTIVE_TMETER     = 6,
    PCK600_QACTIVE_TIMER1_CH8 = 7,
    PCK600_QACTIVE_TIMER1_CH7 = 8,
    PCK600_QACTIVE_TIMER1_CH6 = 9,
    PCK600_QACTIVE_TIMER1_CH5 = 10,
    PCK600_QACTIVE_TIMER1_CH4 = 11,
    PCK600_QACTIVE_TIMER1_CH3 = 12,
    PCK600_QACTIVE_TIMER1_CH2 = 13,
    PCK600_QACTIVE_TIMER1_CH1 = 14,
    PCK600_QACTIVE_TIMER1_CH0 = 15,
    PCK600_QACTIVE_TIMER0_CH1 = 16,
    PCK600_QACTIVE_TIMER0_CH0 = 17,
    PCK600_QACTIVE_SPI3W      = 18,
    PCK600_QACTIVE_SPI2       = 19,
    PCK600_QACTIVE_SPI1       = 20,
    PCK600_QACTIVE_SPI0       = 21,
    PCK600_QACTIVE_KEYSCAN    = 22,
    PCK600_QACTIVE_IR         = 23,
    PCK600_QACTIVE_I2C1       = 24,
    PCK600_QACTIVE_I2C0       = 25,
    PCK600_QACTIVE_GPIO       = 26,
    PCK600_QACTIVE_DMA        = 27,
    PCK600_QACTIVE_TIMER2_CH0 = 28,
    PCK600_QACTIVE_CAN        = 29,
    PCK600_QACTIVE_USB        = 30,

    PCK600_QACTIVE_RFC        = 31,
    PCK600_QACTIVE_PRO24G     = 32,
    PCK600_QACTIVE_PCC        = 33,
    PCK600_QACTIVE_MODEM      = 34,
    PCK600_QACTIVE_CAL80M     = 35,
    PCK600_QACTIVE_CAL32K     = 36,
    PCK600_QACTIVE_CAL10M     = 37,
    PCK600_QACTIVE_BTMAC      = 38,
    PCK600_QACTIVE_BT_VEN     = 39,
    PCK600_QACTIVE_AUDIO      = 40,
    PCK600_QACTIVE_AACK       = 41,
    PCK600_QACTIVE_AES        = 42,
    PCK600_QACTIVE_SHA256     = 43,
    PCK600_QACTIVE_CPU        = 44, ??
    PCK600_QACTIVE_SPIC0      = 45,
    PCK600_QACTIVE_RNG       = 46,
    PCK600_QACTIVE_VDREG     = 47,

    PCK600_QACTIVE_DEVICE_MAX,

} PCK600_QACTIVE_DEVICE_T;

/**
 *         DEBUG MUX            |          P4_0           |          P4_1           |          P4_2           |          MICBIAS        |          P2_1           |          P2_5           |          P2_6           |          P2_7           |
 * -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 * DEBUG_PCSM_STATE_A           | CLK_CTRL_OSC128K_PCSM   | CLK_CTRL_OSC128K_PCSM   | CLK_CTRL_OSC10_PCSM     | CLK_CTRL_OSC10_PCSM     | CLK_CTRL_OSC10_PCSM     | CLK_CTRL_OSC10_PCSM     | CLK_CTRL_OSC10_PCSM     | -                       |
 * DEBUG_PCSM_STATE_B           | PPU_VPON_PCSM           | PPU_VPON_PCSM           | PPU_VPON_PCSM           | PPU_VPON_PCSM           | PPU_VPON_PCSM           | PPU_VPON_PCSM           | PPU_VPON_PCSM           | PPU_VPON_PCSM           |
 * DEBUG_PCSM_STATE_C           | CLK_CTRL_OSC10_PON_PCSM | CLK_CTRL_OSC10_PON_PCSM | CLK_CTRL_OSC10_PON_PCSM | CLK_CTRL_XTAL40_PCSM    | CLK_CTRL_XTAL40_PCSM    | CLK_CTRL_XTAL40_PCSM    | CLK_CTRL_XTAL40_PCSM    | -                       |
 * DEBUG_PCSM_STATE_D           | CLK_CTRL_OSC80_PCSM     | CLK_CTRL_OSC80_PCSM     | CLK_CTRL_OSC80_PCSM     | CLK_CTRL_OSC80_PCSM     | CLK_CTRL_OSC80_CG_PCSM  | CLK_CTRL_OSC80_CG_PCSM  | CLK_CTRL_OSC80_CG_PCSM  | CLK_CTRL_OSC80_CG_PCSM  |
 * DEBUG_PCSM_STATE_E           | PPU_VCORE_PCSM          | PPU_VCORE_PCSM          | PPU_VCORE_PCSM          | PPU_VCORE_PCSM          | PPU_VCORE_PCSM          | -                       | -                       | -                       |
 * DEBUG_PCSM_STATE_F           | CLK_CTRL_OSC10_PON_PCSM | CLK_CTRL_OSC10_PON_PCSM | CLK_CTRL_OSC10_PON_PCSM | CLK_CTRL_PLL1_PCSM      | CLK_CTRL_PLL1_PCSM      | CLK_CTRL_PLL1_PCSM      | CLK_CTRL_PLL1_PCSM      | CLK_CTRL_PLL1_PCSM      |
 * DEBUG_PCSM_STATE_G           | CLK_CTRL_OSC10_PON_PCSM | CLK_CTRL_OSC10_PON_PCSM | CLK_CTRL_OSC10_PON_PCSM | CLK_CTRL_CORE_OP1_PCSM  | CLK_CTRL_CORE_OP1_PCSM  | CLK_CTRL_CORE_OP1_PCSM  | CLK_CTRL_CORE_OP1_PCSM  | CLK_CTRL_CORE_OP1_PCSM  |
 * DEBUG_C_KOUT0X_SIMO          | AON_DBG_C_KOUT_SIMO_SEL | AON_DBG_C_KOUT_SIMO_SEL | AON_DBG_C_KOUT_SIMO_SEL | AON_DBG_C_KOUT_SIMO_SEL | AON_DBG_C_KOUT_SIMO_SEL | AON_DBG_C_KOUT_SIMO_SEL | AON_DBG_C_KOUT_SIMO_SEL | AON_DBG_C_KOUT_SIMO_SEL |
 * DEBUG_C_KOUT0X_MBIAS_0_7     | C_KOUT0X_MBIAS [7:0]    | C_KOUT0X_MBIAS [7:0]    | C_KOUT0X_MBIAS [7:0]    | C_KOUT0X_MBIAS [7:0]    | C_KOUT0X_MBIAS [7:0]    | C_KOUT0X_MBIAS [7:0]    | C_KOUT0X_MBIAS [7:0]    | C_KOUT0X_MBIAS [7:0]    |
 * DEBUG_C_KOUT0X_MBIAS_8_15    | C_KOUT0X_MBIAS [15:8]   | C_KOUT0X_MBIAS [15:8]   | C_KOUT0X_MBIAS [15:8]   | C_KOUT0X_MBIAS [15:8]   | C_KOUT0X_MBIAS [15:8]   | C_KOUT0X_MBIAS [15:8]   | C_KOUT0X_MBIAS [15:8]   | C_KOUT0X_MBIAS [15:8]   |
 * DEBUG_C_KOUT0X_OSC128K10M    | C_KOUT0X_OSC128K10M [0] | C_KOUT0X_OSC128K10M [1] | OSC_DBG_CLK             | XTAL_CLK_OK             | C_KOUT0X_OSC80M [0]     | -                       | -                       | -                       |
 * DEBUG_POS_OUTPUT_FLAG        | AON_DBG_C_KOUT_POS_SEL  | AON_DBG_C_KOUT_POS_SEL  | AON_DBG_C_KOUT_POS_SEL  | AON_DBG_C_KOUT_POS_SEL  | AON_DBG_C_KOUT_POS_SEL  | AON_DBG_C_KOUT_POS_SEL  | AON_DBG_C_KOUT_POS_SEL  | AON_DBG_C_KOUT_POS_SEL  |
 * DEBUG_LPC_OUTPUT_FLAG        | LPC_DEB_SEL             | LPC_DEB_SEL             | LPC_DEB_SEL             | LPC_DEB_SEL             | LPC_DEB_SEL             | LPC_DEB_SEL             | LPC_DEB_SEL             | LPC_DEB_SEL             |
 * DEBUG_SCH_PLAN_OUTPUT_FLAG   | AON_DBG_SCH_PLAN_SEL    | AON_DBG_SCH_PLAN_SEL    | AON_DBG_SCH_PLAN_SEL    | AON_DBG_SCH_PLAN_SEL    | AON_DBG_SCH_PLAN_SEL    | AON_DBG_SCH_PLAN_SEL    | AON_DBG_SCH_PLAN_SEL    | AON_DBG_SCH_PLAN_SEL    |
 * DEBUG_POS_PCK600_OUTPUT_FLAG | AON_DBG_PCK600_SEL      | AON_DBG_PCK600_SEL      | AON_DBG_PCK600_SEL      | AON_DBG_PCK600_SEL      | AON_DBG_PCK600_SEL      | AON_DBG_PCK600_SEL      | AON_DBG_PCK600_SEL      | AON_DBG_PCK600_SEL      |
 * DEBUG_PMU_WDT_OUTPUT_FLAG    | SIMO_WDT_REST_FLAG      | ENLDO_FLAG              | ENSIMO_FLAG_OUTPUT      | -                       | -                       | -                       | -                       | -                       |
 * ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
typedef enum
{
    DEBUG_PCSM_STATE_A                       = ((0 << 7)),
    DEBUG_PCSM_STATE_B                       = ((1 << 7)),
    DEBUG_PCSM_STATE_C                       = ((2 << 7)),
    DEBUG_PCSM_STATE_D                       = ((3 << 7)),
    DEBUG_PCSM_STATE_E                       = ((4 << 7)),
    DEBUG_PCSM_STATE_F                       = ((5 << 7)),
    DEBUG_PCSM_STATE_G                       = ((6 << 7)),
    DEBUG_C_KOUT0X_SIMO_0_7                  = ((7 << 7) | 0),
    DEBUG_C_KOUT0X_SIMO_8_15                 = ((7 << 7) | 1),
    DEBUG_C_KOUT1X_SIMO_0_7                  = ((7 << 7) | 2),
    DEBUG_C_KOUT1X_SIMO_8_15                 = ((7 << 7) | 3),
    DEBUG_C_KOUT2X_SIMO_0_7                  = ((7 << 7) | 4),
    DEBUG_C_KOUT2X_SIMO_8_15                 = ((7 << 7) | 5),
    DEBUG_C_KOUT3X_SIMO_0_7                  = ((7 << 7) | 6),
    DEBUG_C_KOUT3X_SIMO_8_15                 = ((7 << 7) | 7),
    DEBUG_C_KOUT4X_SIMO_0_7                  = ((7 << 7) | 8),
    DEBUG_C_KOUT4X_SIMO_8_15                 = ((7 << 7) | 9),
    DEBUG_C_KOUT5X_SIMO_0_7                  = ((7 << 7) | 10),
    DEBUG_C_KOUT5X_SIMO_8_15                 = ((7 << 7) | 11),
    DEBUG_C_KOUT0X_MBIAS_0_7                 = ((8 << 7)),
    DEBUG_C_KOUT0X_MBIAS_8_15                = ((9 << 7)),
    DEBUG_C_KOUT0X_OSC128K10M                = ((10 << 7)),
    DEBUG_POS_OUTPUT_TO_BUCK_BOOST_HV18      = ((11 << 7) | 0),
    DEBUG_POS_OUTPUT_TO_BUCK_BOOST_VDDPA     = ((11 << 7) | 1),
    DEBUG_POS_OUTPUT_TO_LDO_ANA              = ((11 << 7) | 2),
    DEBUG_POS_OUTPUT_TO_LDO_PA               = ((11 << 7) | 3),
    DEBUG_LPC_OUTPUT_TO_GROUP1               = ((12 << 7) | 0),
    DEBUG_LPC_OUTPUT_TO_GROUP2               = ((12 << 7) | 1),
    DEBUG_SCHPLAN_OUTPUT_TO_CM33_CLOCK       = ((13 << 7) | 0),
    DEBUG_SCHPLAN_OUTPUT_TO_CM33_POWER       = ((13 << 7) | 1),
    DEBUG_SCHPLAN_OUTPUT_TO_BZ_CLOCK         = ((13 << 7) | 2),
    DEBUG_SCHPLAN_OUTPUT_TO_BZ_POWER         = ((13 << 7) | 3),
    DEBUG_SCHPLAN_OUTPUT_TO_DEBUG_GROUP      = ((13 << 7) | 4),
    DEBUG_PCK600_OUTPUT_TO_VPON_PPU          = ((14 << 7) | 0),
    DEBUG_PCK600_OUTPUT_TO_VCORE_PPU         = ((14 << 7) | 1),
    DEBUG_PCK600_OUTPUT_TO_WAKEUP_REASON     = ((14 << 7) | 2),
    DEBUG_PCK600_OUTPUT_TO_CLOCK_CTRL_LVL321 = ((14 << 7) | 3),
    DEBUG_PCK600_OUTPUT_TO_CLOCK_CTRL_LVL0   = ((14 << 7) | 4),
    DEBUG_PMU_WDT_OUTPUT                     = ((15 << 7)),
} AON_DEBUG_PORT;

#ifdef  __cplusplus
}
#endif /* __cplusplus */

#endif /* RTL_DEBUG_PORT_DEF_H */
