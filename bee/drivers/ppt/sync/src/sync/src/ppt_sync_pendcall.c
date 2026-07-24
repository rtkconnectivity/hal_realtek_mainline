/**
*****************************************************************************************
*     Copyright(c) 2020, Realtek Semiconductor Corporation. All rights reserved.
*****************************************************************************************
  * @file     ppt_sync_pendcall.c
  * @brief    Source file for sync protocol.
  * @details
  * @author   bill
  * @date     2022-03-15
  * @version  v0.1
  * *************************************************************************************
  */

/* Add Includes here */
#include "ppt_sync_pendcall.h"
#include "zephyr/kernel.h"
#include "os_mem.h"

typedef struct {
    struct k_work work;
    sync_pendcall_t func;
    void *para1;
    uint32_t para2;
} sync_work_t;

static void sync_work_cb(struct k_work *item)
{
    sync_work_t *psync_work = CONTAINER_OF(item, sync_work_t, work);
    psync_work->func(psync_work->para1, psync_work->para2);
    os_mem_free(psync_work);
}

void sync_pendcall(sync_pendcall_t func, void *para1, uint32_t para2)
{
    sync_work_t *psync_work = (sync_work_t *)os_mem_alloc(RAM_TYPE_DATA_ON, sizeof(sync_work_t));
    k_work_init(&psync_work->work, sync_work_cb);
    psync_work->func = func;
    psync_work->para1 = para1;
    psync_work->para2 = para2;
    k_work_submit(&psync_work->work);
}
