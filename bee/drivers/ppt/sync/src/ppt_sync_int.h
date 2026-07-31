/**
*****************************************************************************************
*     Copyright(c) 2020, Realtek Semiconductor Corporation. All rights reserved.
*****************************************************************************************
* @file     ppt_sync_int.h
* @brief    Head file for sync protocol.
* @details  data structs and external functions declaration.
* @author   bill
* @date     2022-03-15
* @version  v0.1
* *************************************************************************************
*/

/* Define to prevent recursive inclusion */
#ifndef _PPT_SYNC_INT_H_
#define _PPT_SYNC_INT_H_

/* Add Includes here */
#include <stdbool.h>
#include <stdint.h>
#include "platform_macros.h"
#include "platform_list.h"
#include "ppt_sync.h"
#include "ppt_sync_pendcall.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** @addtogroup PPT_SYNC_Driver
 * @{
 */
#define PPT_SYNC_LOG_LEVEL CONFIG_PPT_DRIVER_LOG_LEVEL

/** @defgroup PPT_SYNC_Driver_Exported_Macros Exported Macros
 * @brief
 * @{
 */
#ifdef CONFIG_REALTEK_USING_PPT_SYNC_MASTER
#define SYNC_ROLE_MASTER_SUPPORT 1
#else
#define SYNC_ROLE_MASTER_SUPPORT 0
#endif

#ifdef CONFIG_REALTEK_USING_PPT_SYNC_SLAVE
#define SYNC_ROLE_SLAVE_SUPPORT 1
#else
#define SYNC_ROLE_SLAVE_SUPPORT 0
#endif
#define SYNC_CHANNEL_FREQUENCY                                                                     \
    {2432, 2447, 2462, 2477, 2407, 2422, 2437, 2452, 2442, 2457, 2472, 2413}
#define SYNC_CHANNEL_NUM_DV 12
#define SYNC_CHANNEL_GROUP_NUM_DV 3
#define SYNC_HEADER_LEN 8     // bit
#define SYNC_HEADER_SEQ_LEN 7 // bit
#define SYNC_LENGTH_LEN 7     // bit
#define SYNC_MSG_MAX_LEN 127  // byte
#define SYNC_CRC_LEN_DV 8     // bit
#define SYNC_CRC_POLY_DV 0x07
#define SYNC_CRC_INIT_DV 0xff
#define SYNC_OVERHEAD_LEN 2 // byte
#define SYNC_ACC_ADDR_BR_DV 0x8EBE89D6
#define SYNC_TIFS0_DV 40                   // us
#define SYNC_TIFS1_DV 40                   // us
#define SYNC_PAIR_SPEED_DV 1000            // us
#define SYNC_PAIR_PERIOD 1000000           // us
#define SYNC_PAIR_CFM_PERIOD_DV 8          // times
#define SYNC_SYNC_SLOW_SPEED_DV 1000       // us
#define SYNC_SYNC_SLOW_PERIOD 1000000      // us
#define SYNC_SYNC_SPEED_DV 1000            // us
#define SYNC_SYNC_SPEED_HIGH_DV 250        // us
#define SYNC_SYNC_SPEED_HIGH_MIN 250       // us
#define SYNC_SYNC_LOST_PERIOD_DV 8         // times
#define SYNC_SYNC_FAST_SPEED_DV 1000       // us
#define SYNC_SYNC_FAST_PERIOD_DV 3         // times
#define SYNC_HB_TO_TIMES_DV 3              // times
#define SYNC_CCA_TIMES_DV 3                // times
#define SYNC_SLAVE_HOPPING_SPEED_DV 200000 // us

#define SYNC_PARAM_CONFIG 1
#if SYNC_PARAM_CONFIG
#define SYNC_CRC_LEN sync_ctx.crc_len
#define SYNC_CRC_POLY sync_ctx.crc_poly
#define SYNC_CRC_INIT sync_ctx.crc_init
#define SYNC_PAIR_SPEED sync_ctx.conn_interval
#define SYNC_SYNC_SLOW_SPEED sync_ctx.conn_interval
#define SYNC_SYNC_SPEED sync_ctx.conn_interval
#define SYNC_SYNC_SPEED_HIGH                                                                       \
    (sync_ctx.conn_interval_high <= sync_ctx.conn_interval ? sync_ctx.conn_interval_high           \
                                                           : sync_ctx.conn_interval)
#define SYNC_SYNC_FAST_SPEED sync_ctx.conn_interval
#define SYNC_CHANNEL_NUM sync_ctx.chan_num
#define SYNC_CHANNEL_GROUP_NUM sync_ctx.chan_group_num
#define SYNC_ACC_ADDR_BR sync_ctx.acc_br.addr
#define SYNC_HB_TO_TIMES sync_ctx.hb_to_times
#define SYNC_CCA_TIMES sync_ctx.cca_times
#define SYNC_SLAVE_HOPPING_SPEED sync_ctx.slave_hopping_speed
#define SYNC_ISR_DEBUG sync_ctx.log_isr
#else
#define SYNC_CRC_LEN SYNC_CRC_LEN_DV
#define SYNC_CRC_POLY SYNC_CRC_POLY_DV
#define SYNC_CRC_INIT SYNC_CRC_INIT_DV
#define SYNC_PAIR_SPEED SYNC_PAIR_SPEED_DV
#define SYNC_SYNC_SLOW_SPEED SYNC_SYNC_SLOW_SPEED_DV
#define SYNC_SYNC_SPEED SYNC_SYNC_SPEED_DV
#define SYNC_SYNC_SPEED_HIGH SYNC_SYNC_SPEED_DV
#define SYNC_SYNC_FAST_SPEED SYNC_SYNC_FAST_SPEED_DV
#define SYNC_CHANNEL_NUM SYNC_CHANNEL_NUM_DV
#define SYNC_CHANNEL_GROUP_NUM SYNC_CHANNEL_GROUP_NUM_DV
#define SYNC_ACC_ADDR_BR SYNC_ACC_ADDR_BR_DV
#define SYNC_HB_TO_TIMES SYNC_HB_TO_TIMES_DV
#define SYNC_CCA_TIMES SYNC_CCA_TIMES_DV
#define SYNC_SLAVE_HOPPING_SPEED SYNC_SLAVE_HOPPING_SPEED_DV
#define SYNC_ISR_DEBUG 0
#endif

#define SYNC_PAIR_CFM_PERIOD (SYNC_SYNC_SPEED * SYNC_PAIR_CFM_PERIOD_DV)
#define SYNC_SYNC_LOST_PERIOD                                                                      \
    (sync_ctx.sync_lost_period ? sync_ctx.sync_lost_period                                         \
                               : SYNC_SYNC_SPEED * SYNC_SYNC_LOST_PERIOD_DV)
#define SYNC_SYNC_FAST_PERIOD                                                                      \
    (sync_ctx.sync_fast_period                                                                     \
         ? sync_ctx.sync_fast_period                                                               \
         : (SYNC_SYNC_LOST_PERIOD + SYNC_SYNC_FAST_PERIOD_DV * SYNC_SYNC_FAST_SPEED *              \
                                        SYNC_CHANNEL_NUM / SYNC_CHANNEL_GROUP_NUM))

#define SYNC_SUPPORT_POWER_CONTROL 1
#define SYNC_SUPPORT_POWER_CONTROL_PERIOD 1000 // ms

#define SYNC_SUPPORT_MT1R 1
#define SYNC_SUFFIX_LEN 7 // bit
#define SYNC_CONNECT_DELAY 300
#define SYNC_HEADER_LEN_MT1R 6 // bit

#define SYNC_SUPPORT_SYNC_FAST_DISABLE 1

#define SYNC_FRAME_BLE_COMPATIBLE 0
#if SYNC_FRAME_BLE_COMPATIBLE
#undef SYNC_CHANNEL_FREQUENCY
#define SYNC_CHANNEL_FREQUENCY                                                                     \
    {2402, 2406, 2410, 2414, 2418, 2422, 2426, 2430, 2434, 2438, 2442, 2446}
#undef SYNC_HEADER_LEN
#define SYNC_HEADER_LEN 8 // bit
#undef SYNC_LENGTH_LEN
#define SYNC_LENGTH_LEN 8 // bit
#undef SYNC_CRC_LEN
#define SYNC_CRC_LEN 24
#undef SYNC_CRC_POLY
#define SYNC_CRC_POLY 0x00065b
#undef SYNC_CRC_INIT
#define SYNC_CRC_INIT 0x555555
#undef SYNC_ACC_ADDR_BR_DV
#define SYNC_ACC_ADDR_BR_DV 0x8E89BED6
#endif
/** @} End of PPT_SYNC_Driver_Exported_Macros */

/** @defgroup PPT_SYNC_Driver_Exported_Types Exported Types
 * @brief
 * @{
 */

#pragma pack(1)

typedef union {
    uint8_t value;
    struct {
        uint8_t ack : 1;
        uint8_t seq : SYNC_HEADER_SEQ_LEN;
    };
#if SYNC_SUPPORT_MT1R
    struct {
        uint8_t ack : 1;
        uint8_t seq : 4;
        uint8_t req_ack : 1;
    } m;
    struct {
        uint8_t ack : 1;
        uint8_t seq : 4;
        uint8_t rfu : 1;
    } s;
#endif
} sync_header_t;

typedef enum {
    SYNC_OPCODE_EMPTY,
    SYNC_OPCODE_PAIR_REQ,
    SYNC_OPCODE_PAIR_RSP,
    SYNC_OPCODE_PAIR_CONF,
    SYNC_OPCODE_CONN_REQ,
    SYNC_OPCODE_CONN_RSP,
    SYNC_OPCODE_HEARTBEAT,
    SYNC_OPCODE_APP_START = SYNC_OPCODE_APP_START_VALUE,
} sync_opcode_t;

typedef struct {
    uint16_t interval;
} sync_pair_req_t;

typedef struct {
    sync_acc_t acc;
} sync_pair_rsp_t;

typedef struct {
    uint16_t interval;
} sync_conn_req_t;

typedef struct {
    uint16_t interval;
} sync_heartbeat_t;

typedef struct {
#if SYNC_FRAME_BLE_COMPATIBLE
    uint8_t addr[5];
#endif
    uint32_t seq;
    // uint8_t padding[10];
} sync_app_test_t;

typedef struct {
#if SYNC_FRAME_BLE_COMPATIBLE
    uint8_t addr[5];
#endif
    uint32_t seq;
    // uint8_t padding[22];
} sync_app_test1_t;

typedef struct {
    sync_opcode_t opcode;
    union {
        sync_pair_req_t pair_req;
        sync_pair_rsp_t pair_rsp;
        sync_conn_req_t conn_req;
        sync_heartbeat_t heartbeat;
        sync_app_test_t app_test;
    };
} sync_pdu_t;

#pragma pack()

typedef struct {
    void *pnext;
    sync_msg_send_cb_t send_cb;
    sync_msg_type_t type;
    uint16_t len;
    uint8_t data[SYNC_MSG_MAX_LEN];
    sync_send_info_t send_info;
} sync_msg_t;

typedef struct {
    uint16_t freq;
    uint8_t bank;
    uint8_t channel;
    int16_t rssi;
#ifdef SYNC_DEBUG
    uint32_t choose_times;
    uint32_t used_count;
    uint32_t fail_count;
    uint32_t fail_burst_count;
    uint32_t fail_burst_max;
#endif
} sync_channel_cfg_t;

struct _sync_sw_timer_t;
typedef void (*sync_sw_timer_cb_t)(struct _sync_sw_timer_t *p_timer);
typedef struct _sync_sw_timer_t {
    void *timer;
    sync_sw_timer_cb_t cb;
} sync_sw_timer_t;

typedef void (*sync_hw_timer_cb_t)(void);

#if SYNC_SUPPORT_MT1R
typedef enum {
    SYNC_HW_TIMER_SHIFT_STATE_IDLE,
    SYNC_HW_TIMER_SHIFT_STATE_PENDING,
    SYNC_HW_TIMER_SHIFT_STATE_SHIFTING
} sync_hw_timer_shift_state_t;
#endif

typedef struct {
    sync_role_t role;
    uint8_t crc_len; // bit
    uint32_t crc_poly;
    uint32_t crc_init;
    uint8_t chan_num;
    uint8_t chan_group_num;
    uint8_t chan_cur;
    sync_channel_cfg_t *chan_cfg;
    uint32_t chan_used_count;
    uint32_t chan_miss_count;
    sync_hw_timer_cb_t hw_timer_cb;
    bool hw_timer_periodic;
    sync_event_cb_t event_cb;
    uint8_t msg_quota[SYNC_MSG_TYPE_NUM];
    uint8_t msg_finite_retrans_num;
    uint8_t msg_dynamic_retrans_num_lower;
    uint8_t msg_dynamic_retrans_num_upper;
    uint8_t msg_retrans_count;
    sync_msg_receive_cb_t msg_receive_cb;
    uint8_t msg_quota_total;
    uint8_t msg_count[SYNC_MSG_TYPE_NUM];
    plt_list_t msg_free;
    plt_list_t msg_send;
    sync_msg_t *pmsg_buff;
    uint32_t conn_interval;      //!< us
    uint32_t conn_interval_high; //!< us
    uint32_t sync_lost_period;   //!< us
    uint32_t sync_fast_period;   //!< us
    bool log_isr;
    bool tx_power_adaptive;
    int8_t tx_power_dbm_max;
    int8_t tx_power_dbm_min;
#if SYNC_SUPPORT_POWER_CONTROL
    int8_t tx_power_idx_cur;
    int8_t tx_power_idx_max;
    int8_t tx_power_idx_min;
    uint8_t tx_power_levels;
    sync_sw_timer_t power_ctrl_timer;
    uint32_t power_ctrl_count;
    uint32_t power_ctrl_count_fail;
    uint16_t power_ctrl_per;
#endif
    int8_t pair_rssi_th;
    sync_acc_t acc_br;
    uint8_t tifs[2];
    sync_hb_cb_t hb_cb;
    uint8_t hb_to_times;
    uint8_t cca_times;
    uint32_t slave_hopping_speed;
    sync_rte_req_cb_t rte_req_cb;
#if SYNC_SUPPORT_MT1R
    struct {
        bool flag;
    } mt1r;
    uint32_t hw_timer_period;
    int32_t hw_timer_shift_time;
    uint32_t hw_timer_shift_period;
    uint32_t hw_timer_pend_time;
    sync_hw_timer_shift_state_t hw_timer_shift_state;
    sync_hw_timer_cb_t hw_timer1_cb;
#endif
} sync_ctx_t;

extern sync_ctx_t sync_ctx;

/** @} End of PPT_SYNC_Driver_Exported_Types */

/** @defgroup PPT_SYNC_Driver_Exported_Functions Exported Functions
 * @brief
 * @{
 */

/**
 * @brief format diagnose output with raw data
 * @param[in] data: raw data
 * @param[in] data_len: raw data length
 * @param[in] fmt: format string
 * @return none
 */
void sync_ppt_init(void);
void sync_ppt_set_rf_time(void);
void sync_ppt_set_tifs(bool init);
void sync_channel_init(void);
void sync_channel_change(uint8_t idx);
void sync_channel_change_next(void);
void sync_channel_change_next_in_group(void);
#ifdef SYNC_DEBUG
void sync_channel_statistic(bool ok);
void sync_channel_clear_statistic(void);
void sync_channel_print_statistic(void);
#else
#define sync_channel_statistic(...)
#define sync_channel_clear_statistic(...)
#define sync_channel_print_statistic(...)
#endif
void sync_acc_set(sync_acc_t acc);
sync_acc_t sync_acc_gen_data(void);

void sync_hw_timer_init(void);
void sync_hw_timer_start(uint32_t period_us, sync_hw_timer_cb_t cb);
void sync_hw_timer_restart(void);
void sync_hw_timer_stop(void);
uint32_t sync_hw_timer_get_left(void);
bool sync_hw_timer_check_pending(void);
void sync_hw_timer_set_periodic(bool enable);
void sync_hw_timer_impr_ip(bool enable);

void sync_hw_timer_shift(int32_t time);
void sync_hw_timer1_init(void);
void sync_hw_timer1_start(uint32_t period_us, sync_hw_timer_cb_t cb);
void sync_hw_timer1_stop(void);

void sync_hw_frtimer_init(void);
void sync_hw_frtimer_start(void);
void sync_hw_frtimer_stop(void);
uint32_t sync_hw_frtimer_get_count(void);
int32_t sync_hw_frtimer_time_diff(uint32_t time1, uint32_t time2);
#define SYNC_HW_FRTIMER_MAX_VALUE 0xFFFFFFFE
#define SYNC_HW_FRTIMER_CLOCK 40
#define sync_hw_frtimer_time_elapse(late, early)                                                   \
    (((late) >= (early) ? (late) - (early) : (SYNC_HW_FRTIMER_MAX_VALUE - (early) + (late) + 1)) / \
     SYNC_HW_FRTIMER_CLOCK)

void sync_sw_timer_start(sync_sw_timer_t *sync_timer, uint32_t interval_ms, sync_sw_timer_cb_t cb);
void sync_sw_timer_stop(sync_sw_timer_t *sync_timer);

uint64_t sync_native_time_read(void);

void sync_msg_init(void);
void sync_msg_handle_tx_done(void *para1, uint32_t para2);

void sync_check_feature(void);

void sync_master_handle_tx_trigger(void);
void sync_master_msg_flush(sync_msg_type_t type);

void sync_slave_handle_tx_trigger(void);

#if SYNC_SUPPORT_POWER_CONTROL
void sync_power_ctrl_init(void);
void sync_power_ctrl_start(void);
void sync_power_ctrl_stop(void);
#endif

#define sync_set_header(ack_tmp, seq_tmp)                                                          \
    do {                                                                                           \
        sync_header_t header = {0};                                                                \
        header.ack = ack_tmp;                                                                      \
        header.seq = seq_tmp;                                                                      \
        PRO_HP_0_TYPE hp = {0};                                                                    \
        hp.hp_0_7_0 = header.value;                                                                \
        WR_PPT_REG(PRO_HP_0 + PRO_ENTRY_FIELD_BASE(0), hp.d16);                                    \
    } while (0)

#if SYNC_SUPPORT_MT1R
#define sync_master_set_header(ack_tmp, seq_tmp, req_ack_tmp)                                      \
    do {                                                                                           \
        sync_header_t header = {0};                                                                \
        header.m.ack = ack_tmp;                                                                    \
        header.m.seq = seq_tmp;                                                                    \
        header.m.req_ack = req_ack_tmp;                                                            \
        PRO_HP_0_TYPE hp = {0};                                                                    \
        hp.hp_0_7_0 = header.value;                                                                \
        WR_PPT_REG(PRO_HP_0 + PRO_ENTRY_FIELD_BASE(0), hp.d16);                                    \
    } while (0)
#define sync_slave_set_header(ack_tmp, seq_tmp)                                                    \
    do {                                                                                           \
        sync_header_t header = {0};                                                                \
        header.s.ack = ack_tmp;                                                                    \
        header.s.seq = seq_tmp;                                                                    \
        header.s.rfu = 0;                                                                          \
        PRO_HP_0_TYPE hp = {0};                                                                    \
        hp.hp_0_7_0 = header.value;                                                                \
        WR_PPT_REG(PRO_HP_0 + PRO_ENTRY_FIELD_BASE(0), hp.d16);                                    \
    } while (0)
void sync_ppt_set_rf_time_mt1r(void);
#endif

uint32_t sync_rand(void);

#include "os_timer.h"
#define os_timer_pendcall(func, para1, para2, wait) os_timer_pend_function_call(func, para1, para2)
/** @} End of PPT_SYNC_Driver_Exported_Functions */

/** @} End of PPT_SYNC_Driver */

#ifdef __cplusplus
}
#endif /*  __cplusplus */

#endif
