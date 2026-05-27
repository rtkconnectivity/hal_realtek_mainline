/*
 * Copyright (c) 2026, Realtek Semiconductor Corporation
 *
 * SPDX-License-Identifier: LicenseRef-Realtek-5-Clause
 */

/*============================================================================*
 *                        Define to prevent recursive inclusion
 *============================================================================*/
#ifndef DEBUG_PORT_H
#define DEBUG_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================*
 *                        Header Files
 *============================================================================*/
#include "utils/rtl_utils.h"
#if defined (CONFIG_SOC_SERIES_RTL87X3J)
#include "debug/src/device/rtl87x3j/debug_port_def.h"
#elif defined (CONFIG_SOC_SERIES_RTL87X2J)
#include "debug/src/device/rtl87x2j/debug_port_def.h"
#endif

/*============================================================================*
 *                        Constants
 *============================================================================*/
#define PIN_GROUP_TABLE_SIZE        8

/**
  * @brife Debug Mode Select Group Struct
  * @notes User can select debug mode group for different source signals
  */
typedef union
{
    uint32_t d32;
    struct
    {
        uint32_t mode_sel0: 4;
        uint32_t mode_sel1: 4;
        uint32_t mode_sel2: 4;
        uint32_t mode_sel3: 4;
        uint32_t src_sel0: 4;
        uint32_t src_sel1: 4;
        uint32_t src_sel2: 4;
        uint32_t src_sel3: 4;
    } b;
} T_GROUP_CONFIG;

/**
  * @brife Debug Mode Select Platform Struct
  * @notes User can select debug port in different platform
  */
typedef enum
{
    DEBUG_PORT_IN_PLATFORM,         //Config debug port in platform.
#if (DEBUG_PORT_SUPPORT_MULTI_PLATFORM == 1)
    DEBUG_PORT_IN_SHARE_PLATFORM,   //Config debug port in share platform.
    DEBUG_PORT_IN_NETWORK,          //Config debug port in network.
#endif
} T_DEBUG_PORT_PLATFORM;

typedef enum
{
    PCK600_QACTIVE_IDLE = 0,
    PCK600_QACTIVE_ACTIVE = 1,
    PCK600_QACTIVE_ERROR = 2,
} PCK600_QACTIVE_STATUS_T;

/*============================================================================*
 *                         Functions
 *============================================================================*/
/**
  * @brief  set debug mode on specific pin
  * @param  pin_group_table: the pin group table pointer.
  *         This parameter can be combinated by user himself.
  *         For example pin_group_table can be following values:
  *         uint8_t debug_pin_group[8] =
  *         {
  *            DBG_PIN_GROUP10,
  *            DBG_PIN_GROUP11,
  *            DBG_PIN_GROUP8,
  *            DBG_PIN_GROUP7,
  *            DBG_PIN_GROUP5,
  *            DBG_PIN_GROUP0,
  *            DBG_PIN_GROUP1,
  *            DBG_PIN_GROUP2,
  *         };
  * @param  dbg_bitmap: the pin bitmap.
  * @retval None
  */
void debug_port_set_pin_bit_map(uint8_t *pin_group_table, uint32_t dbg_bitmap);

/**
  * @brief  Open the debug port mode on specific pins
  * @param  sel_group: select debug mode group from T_DEBUG_MODE
  * @retval None
  *
  * <b>Example usage</b>
  * \code{.c}
  *
  * const T_GROUP_CONFIG group_config =
  * {
  *     .b.mode_sel0 = DIGI_DBG_FWREG_CTRL,
  *     .b.mode_sel1 = DIGI_DBG_FWREG_CTRL,
  *     .b.mode_sel2 = DIGI_DBG_FWREG_CTRL,
  *     .b.mode_sel3 = DIGI_DBG_FWREG_CTRL,
  *     .b.src_sel0  = 0,
  *     .b.src_sel1  = 1,
  *     .b.src_sel2  = 2,
  *     .b.src_sel3  = 3,
  * };
  * debug_port_open_rom(group_config);
  * \endcode
  */
void debug_port_open(T_GROUP_CONFIG group_config);

#if (DEBUG_PORT_SUPPORT_QACTIVE_STATUS == 1)
/**
  * @brief  set debug mode on specific pin
  * @param  peripheral: can be /ref PINMUX_CORE_DEBUG_PORT.
  * @param  pin_group_table: the pin group table pointer.
  *         This parameter can be combinated by user himself.
  *         For example pin_group_table can be following values:
  *         uint8_t debug_pin_group[8] =
  *         {
  *            DBG_PIN_GROUP_INVALID,
  *            DBG_PIN_GROUP1,
  *            DBG_PIN_GROUP_INVALID,
  *            DBG_PIN_GROUP_INVALID,
  *            DBG_PIN_GROUP_INVALID,
  *            DBG_PIN_GROUP2,
  *            DBG_PIN_GROUP_INVALID,
  *            DBG_PIN_GROUP_INVALID,
  *         };
  * @retval None
    *
    * <b>Example usage</b>
  * \code{.c}
  *
  * void debug_port_sample(void)
  * {
  *     debug_port_pck600_qactive_output(PCK600_QACTIVE_CORE_DEVICE2, debug_pin_group);
  * }
  * \endcode
  */
void debug_port_pck600_qactive_output(uint32_t group, uint8_t *pin_group_table);

uint64_t debug_port_pck600_qactive_get_device_mask(void);

PCK600_QACTIVE_STATUS_T debug_port_pck600_qactive_get_device_status(PCK600_QACTIVE_DEVICE_T device);
#endif

#if (DEBUG_PORT_SUPPORT_AON_DEBUG == 1)
/**
  * @brief  set debug mode on specific pin
  * @param  debug_select: can be /ref AON_DEBUG_PORT.
  * @param  NewState: Newstate of the specified peripheral.
  *
  * @retval None
    *
    * <b>Example usage</b>
  * \code{.c}
  *
  * void debug_port_sample(void)
  * {
  *     debug_port_aon(DEBUG_PCK600_OUTPUT_TO_VPON_PPU, ENABLE);
  * }
  * \endcode
  */
void debug_port_aon_output(AON_DEBUG_PORT debug_select, FunctionalState NewState);
#endif

#if (DEBUG_PORT_SUPPORT_MULTI_PLATFORM == 1)
/**
  * @brief  Config debug port in which platform.
  * @param  platform: select platform from \ref T_DEBUG_PORT_PLATFORM.
  *
  * <b>Example usage</b>
  * \code{.c}
  *
  * debug_port_set_platform(DEBUG_PORT_IN_PLATFORM);
  * debug_port_open(group_config);
  * debug_port_set_pin_bit_map(pin_group, 0xffffffff);
  * \endcode
  */
void debug_port_set_platform(T_DEBUG_PORT_PLATFORM platform);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _DEBUG_PORT_H_ */

