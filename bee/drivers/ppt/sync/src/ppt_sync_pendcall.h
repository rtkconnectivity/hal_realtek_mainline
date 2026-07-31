/**
*****************************************************************************************
*     Copyright(c) 2020, Realtek Semiconductor Corporation. All rights reserved.
*****************************************************************************************
  * @file     ppt_sync_pendcall.h
  * @brief    Head file for sync protocol.
  * @details  data structs and external functions declaration.
  * @author   bill
  * @date     2022-03-15
  * @version  v0.1
  * *************************************************************************************
  */

/* Define to prevent recursive inclusion */
#ifndef _PPT_SYNC_PENDCALL_H_
#define _PPT_SYNC_PENDCALL_H_

/* Add Includes here */
#include <stdint.h>

#ifdef  __cplusplus
extern "C" {
#endif      /* __cplusplus */

/** @addtogroup PPT_SYNC_Driver
  * @{
  */

/** @defgroup PPT_SYNC_Driver_Exported_Macros Exported Macros
  * @brief
  * @{
  */

/** @} End of PPT_SYNC_Driver_Exported_Macros */

/** @defgroup PPT_SYNC_Driver_Exported_Types Exported Types
  * @brief
  * @{
  */

typedef void (*sync_pendcall_t)(void *para1, uint32_t para2);

/** @} End of PPT_SYNC_Driver_Exported_Types */

/** @defgroup PPT_SYNC_Driver_Exported_Functions Exported Functions
  * @brief
  * @{
  */

/**
  * @brief pend the function to execute 
  * @param[in] func: pend call
  * @param[in] para1: pointer parameter
  * @param[in] para2: normal parameter
  * @return none
  */
void sync_pendcall(sync_pendcall_t func, void *para1, uint32_t para2);

/** @} End of PPT_SYNC_Driver_Exported_Functions */

/** @} End of PPT_SYNC_Driver */

#ifdef  __cplusplus
}
#endif      /*  __cplusplus */

#endif
