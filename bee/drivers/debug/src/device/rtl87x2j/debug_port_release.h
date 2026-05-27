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
typedef enum
{
    PCK600_QACTIVE_IDLE = 0,
    PCK600_QACTIVE_ACTIVE = 1,
    PCK600_QACTIVE_ERROR = 2,
} PCK600_QACTIVE_STATUS_T;

/*============================================================================*
 *                         Functions
 *============================================================================*/

uint64_t debug_port_pck600_qactive_get_device_mask(void);

PCK600_QACTIVE_STATUS_T debug_port_pck600_qactive_get_device_status(PCK600_QACTIVE_DEVICE_T device);

#ifdef __cplusplus
}
#endif

#endif /* _DEBUG_PORT_H_ */
