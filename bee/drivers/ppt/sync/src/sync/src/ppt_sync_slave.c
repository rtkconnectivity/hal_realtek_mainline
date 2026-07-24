/**
*****************************************************************************************
*     Copyright(c) 2020, Realtek Semiconductor Corporation. All rights reserved.
*****************************************************************************************
  * @file     ppt_sync_slave.c
  * @brief    Source file for sync protocol.
  * @details
  * @author   bill
  * @date     2022-03-15
  * @version  v0.1
  * *************************************************************************************
  */

/* Add Includes here */
#include <string.h>
#include "ppt_simple.h"
#include "ppt_sync_int.h"
#include "ppt_sync_slave.h"
//#include "trace.h"
#include "os_sync.h"
#include "os_timer.h"
#include "utils.h"

LOG_MODULE_REGISTER(ppt_sync_slave, PPT_SYNC_LOG_LEVEL);

#define SYNC_SLAVE_CCA_ENABLE                   1
#if SYNC_SLAVE_CCA_ENABLE == 0
//#define SYNC_SLAVE_FIXED_CHANNEL              3
#endif

#define SYNC_SLAVE_TX_MSG_NUM                   3

#define SYNC_RX_MSG_QUEUE_SIZE                  16
#define SYNC_RX_MSG_QUEUE_DATA_LEN              16

#define SYNC_SLAVE_TEST_RX_STOP                 0

#define SYNC_SLAVE_TIME_DEBUG_RX                -1
#define SYNC_SLAVE_TIME_DEBUG_REQ_ACK           -1
#define SYNC_SLAVE_TIME_DEBUG_ALL_ACK           -3
#define SYNC_SLAVE_TIME_DEBUG_SEQ_ACK           -2
#define SYNC_SLAVE_TIME_DEBUG_REFRESH_ACK       -4
#define SYNC_SLAVE_TIME_DEBUG_UPDATE_ACK        -5

typedef enum
{
    SYNC_SLAVE_PAIR_PHASE_WAIT_REQ,
    SYNC_SLAVE_PAIR_PHASE_SEND_RSP,
    SYNC_SLAVE_PAIR_PHASE_WAIT_CFM,
    SYNC_SLAVE_PAIR_PHASE_SEND_ACK
} sync_slave_pair_phase_t;

typedef enum
{
    SYNC_SLAVE_HB_STATE_IDLE,
    SYNC_SLAVE_HB_STATE_HB_RECEIVED
} sync_slave_hb_state_t;

struct
{
    sync_state_t state;
    sync_pair_state_t pair_state;
    uint8_t phase;
    uint16_t count;
    sync_acc_t acc_data;
    bool ack;
    uint8_t seq_tx;
    uint8_t seq_rx;
    uint32_t crc_rx;
    sync_msg_t *pmsg_sending[SYNC_SLAVE_TX_MSG_NUM];
    uint8_t pmsg_ptr[SYNC_SLAVE_TX_MSG_NUM];
    uint8_t pmsg_sending_idx;
    bool pmsg_switching;
    bool pmsg_last_empty;
    struct
    {
        uint16_t interval;
        sync_slave_hb_state_t state;
        bool slow;
        bool exit_pend;
        bool interval_changed;
    } hb;
    volatile bool busy; //!< tx or rx ongoing
    bool tx_trigger_pending;
#if SYNC_SUPPORT_MT1R
    struct
    {
        bool enable;
        uint16_t ce_cnt;
        volatile uint8_t count;
        int8_t slot_idx;
        uint8_t ack_field[3];
        uint8_t req_ack_field[3];
        uint8_t ack_rsp_idx;
        uint8_t ack_curr_idx;
        uint8_t seq_ack_rx;
        bool req_ack;
        uint32_t rx_clk;
    } quick;
    uint32_t ce_clk;
    bool rx_ok;
#endif
} sync_slave_ctx;

#if SYNC_SUPPORT_MT1R
typedef struct
{
    int8_t rssi;
    uint8_t len;
    uint8_t data[SYNC_RX_MSG_QUEUE_DATA_LEN];
} sync_rx_msg_t;

typedef struct
{
    sync_rx_msg_t buffer[SYNC_RX_MSG_QUEUE_SIZE];
    volatile uint8_t head;
    volatile uint8_t tail;
} sync_rx_msg_queue_t;

sync_rx_msg_queue_t sync_rx_msg_queue;
static inline uint8_t sync_slave_get_ack_field(void);
static void sync_slave_handle_rx_task(void *para1, uint32_t para2);
#endif

static void sync_slave_ppt_isr_handler(void);

int8_t sync_slave_cca_get(uint8_t start_idx, uint8_t num)
{
    APP_PRINT_INFO2("sync_slave_cca_get: start_idx=%d, num=%d",start_idx,num);
    uint8_t idx = 0;
    uint8_t count = 0;
    uint8_t bank = 0;
    int16_t rssi_min = 0;
    uint8_t rssi_min_idx = 0;
    PRO_CHANNEL_BANK_TYPE ch_bank = {.d16 = RD_PPT_REG(PRO_CHANNEL_BANK)};
    bank = ch_bank.bank;
    for (count = 0; count < num; count++)
    {
        idx = start_idx + count;
        sync_ctx.chan_cfg[idx].rssi = -128;
    }
    for (uint8_t loop = 0; loop < SYNC_CCA_TIMES; loop++)
    {
        for (count = 0; count < num; count++)
        {
            idx = start_idx + count;
            ppt_clear_psd_result();
            if (ch_bank.bank != sync_ctx.chan_cfg[idx].bank)
            {
                ch_bank.bank = sync_ctx.chan_cfg[idx].bank;
                WR_PPT_REG(PRO_CHANNEL_BANK, ch_bank.d16);
            }

            ppt_psd_mode_ext_t param =
            {
                {
                    .chann_start = sync_ctx.chan_cfg[idx].channel,
                    .chann_stop = sync_ctx.chan_cfg[idx].channel,
                    .chann_step = 1,
                    .mode = 0,
                    .timeout = 0x3ff
                }
            };
            ppt_set_psd_mode_ext(&param);
            ppt_enable_psd(NULL);
            int16_t rssi = ppt_get_psd_result(sync_ctx.chan_cfg[idx].channel);
            //APP_PRINT_TRACE3("sync: cca loop %d %d %d", loop, idx, rssi);
            if (rssi > sync_ctx.chan_cfg[idx].rssi)
            {
                sync_ctx.chan_cfg[idx].rssi = rssi;
            }
        }
    }

    if (ch_bank.bank != bank)
    {
        ch_bank.bank = bank;
        WR_PPT_REG(PRO_CHANNEL_BANK, ch_bank.d16);
    }

    for (count = 0; count < num; count++)
    {
        idx = start_idx + count;
        if (sync_ctx.chan_cfg[idx].rssi < rssi_min)
        {
            rssi_min = sync_ctx.chan_cfg[idx].rssi;
            rssi_min_idx = idx;
        }
    }

    if (rssi_min >= -85)
    {
        APP_PRINT_WARN3("sync: cca ret %d %d %d", rssi_min_idx, sync_ctx.chan_cfg[rssi_min_idx].freq,
                        rssi_min);
    }
    return rssi_min_idx;
}

static void sync_slave_close(bool quit_tx)
{
    ppt_kill_hw_fsm(PPT_FSM_PRX);
    if (quit_tx)
    {
        /* not all cases need it */
        ppt_force_fw_ack(0);
    }
}

static void sync_slave_init_tx_fifo(void)
{
    ppt_set_tx_fifo_rdptr(0, PPT_TX_FIFO_SIZE - 1);
    ppt_force_fw_ack(0);
    ppt_flush_tx_fifo(0);
}

static void sync_slave_free_sending_msg(void)
{
    for (uint8_t loop = 0; loop < SYNC_SLAVE_TX_MSG_NUM; loop++)
    {
        sync_msg_t *pmsg = sync_slave_ctx.pmsg_sending[loop];
        if (pmsg)
        {
            if (pmsg->send_cb)
            {
                pmsg->send_info.res = SYNC_SEND_RESULT_UNKNOWN;
                sync_pendcall(sync_msg_handle_tx_done, pmsg, 0);
            }
            else
            {
                sync_ctx.msg_count[pmsg->type] -= 1;
                plt_list_push(&sync_ctx.msg_free, pmsg);
            }
            sync_slave_ctx.pmsg_sending[loop] = NULL;
        }
    }
}

void sync_slave_handle_hopping(void);
static void sync_slave_handle_hopping_task(void *para1, uint32_t para2)
{
    if (sync_slave_ctx.state != SYNC_STATE_SYNC_SLOW)
    {
        APP_PRINT_WARN1("sync: slow hopping, state invalid %d!", sync_slave_ctx.state);
        return;
    }

#if SYNC_SLAVE_CCA_ENABLE
    int8_t chan_next = sync_slave_cca_get(0, sync_ctx.chan_num);
#else
    uint8_t chan_next = sync_rand() % sync_ctx.chan_num;
#endif
    //APP_PRINT_TRACE1("sync: slow hopping, cca change to %d!", chan_next);
    sync_channel_change(chan_next);
    /* restart rx */
    ppt_execute_instruction(PPT_HW_INSTRUCTION_PRX_ENABLE);
    sync_hw_timer_start(SYNC_SLAVE_HOPPING_SPEED, sync_slave_handle_hopping);
}

void sync_slave_handle_hopping(void)
{
    sync_slave_close(false);
    if (sync_slave_ctx.pair_state == SYNC_PAIR_STATE_PAIRING)
    {
        if (sync_slave_ctx.phase != SYNC_SLAVE_PAIR_PHASE_WAIT_REQ)
        {
            sync_slave_ctx.phase = SYNC_SLAVE_PAIR_PHASE_WAIT_REQ;
        }
    }
    sync_pendcall(sync_slave_handle_hopping_task, 0, 0);
}

static void sync_slave_handle_paired(void *para1, uint32_t para2)
{
    sync_bond_info_t bond_info;
    bond_info.acc = sync_slave_ctx.acc_data;
    sync_nvm_set_bond_info(&bond_info);

    if (sync_ctx.event_cb)
    {
        sync_ctx.event_cb(SYNC_EVENT_PAIRED);
    }
    if (sync_ctx.event_cb)
    {
        sync_ctx.event_cb(SYNC_EVENT_CONNECTED);
    }
}

static void sync_slave_handle_connected(void *para1, uint32_t para2)
{
    if (sync_ctx.event_cb)
    {
        sync_ctx.event_cb(SYNC_EVENT_CONNECTED);
    }
}

static void sync_slave_handle_disconnected(void *para1, uint32_t para2)
{
    if (sync_ctx.event_cb)
    {
        sync_ctx.event_cb(SYNC_EVENT_CONNECT_LOST);
    }
}

static void sync_slave_handle_exception(void)
{
    sync_slave_stop();
    sync_pendcall(sync_slave_handle_disconnected, 0, 0);
}

#ifdef SYNC_DEBUG
static void sync_slave_handle_sync_lost(void *para1, uint32_t para2)
{
    if (sync_ctx.event_cb)
    {
        sync_ctx.event_cb(SYNC_EVENT_SYNC_LOST);
    }
}

static void sync_slave_handle_sync_reconnected(void *para1, uint32_t para2)
{
    if (sync_ctx.event_cb)
    {
        sync_ctx.event_cb(SYNC_EVENT_SYNC_RECONNECTED);
    }
}
#endif

void sync_slave_handle_sync_fast_to(void)
{
    sync_slave_close(true);
    sync_slave_free_sending_msg();
#if SYNC_SUPPORT_POWER_CONTROL
    if (sync_ctx.tx_power_adaptive)
    {
        sync_power_ctrl_stop();
    }
#endif
#if SYNC_SUPPORT_MT1R
    sync_hw_timer1_stop();
    ppt_reg_handler(sync_slave_ppt_isr_handler);
#endif
    sync_slave_ctx.pair_state = SYNC_PAIR_STATE_IDLE;
    sync_slave_ctx.state = SYNC_STATE_IDLE;
    if (sync_ctx.rte_req_cb)
    {
        sync_ctx.rte_req_cb(false);
    }
    APP_PRINT_ERROR0("sync: disconnected!");
    sync_pendcall(sync_slave_handle_disconnected, 0, 0);
}

static void sync_slave_handle_sync_lost_to_task(void *para1, uint32_t para2)
{
    if (sync_slave_ctx.state != SYNC_STATE_SYNC_FAST)
    {
        APP_PRINT_WARN1("sync: lost, state invalid %d!", sync_slave_ctx.state);
        return;
    }

    uint8_t chan_num, chan_start;
    chan_num = sync_ctx.chan_num / sync_ctx.chan_group_num;
    chan_start = (sync_ctx.chan_cur / chan_num) * chan_num;
#if SYNC_SLAVE_CCA_ENABLE
    int8_t chan_next = sync_slave_cca_get(chan_start, chan_num);
#else
    uint8_t chan_next = chan_start + sync_rand() % chan_num;
#endif
    APP_PRINT_INFO2("sync: lost, cca change to %d[%d]!", chan_next, sync_ctx.chan_cfg[chan_next].freq);
    sync_channel_change(chan_next);
    /* restart rx */
    ppt_execute_instruction(PPT_HW_INSTRUCTION_PRX_ENABLE);
    sync_hw_timer_start(SYNC_SYNC_FAST_PERIOD, sync_slave_handle_sync_fast_to);
}

void sync_slave_handle_sync_lost_to(void)
{
    sync_slave_ctx.state = SYNC_STATE_SYNC_FAST;
#ifdef SYNC_SLAVE_FIXED_CHANNEL
    APP_PRINT_INFO0("sync: lost!");
#ifdef SYNC_DEBUG
    sync_pendcall(sync_slave_handle_sync_lost, 0, 0);
#endif
#if SYNC_SUPPORT_SYNC_FAST_DISABLE
    if (SYNC_SYNC_FAST_PERIOD == SYNC_SYNC_FAST_PERIOD_DISABLE)
    {
        sync_slave_handle_sync_fast_to();
    }
    else
#endif
    {
        sync_hw_timer_start(SYNC_SYNC_FAST_PERIOD, sync_slave_handle_sync_fast_to);
    }
#else
#if SYNC_SUPPORT_SYNC_FAST_DISABLE
    if (SYNC_SYNC_FAST_PERIOD == SYNC_SYNC_FAST_PERIOD_DISABLE)
    {
        APP_PRINT_INFO0("sync: lost!");
#ifdef SYNC_DEBUG
        sync_pendcall(sync_slave_handle_sync_lost, 0, 0);
#endif
        sync_slave_handle_sync_fast_to();
    }
    else
#endif
    {
        /* stop rx */
        sync_slave_close(false);
        sync_pendcall(sync_slave_handle_sync_lost_to_task, 0, 0);
    }
#endif
}

void sync_slave_handle_pair_cfm_to(void)
{
    sync_slave_ctx.phase = SYNC_SLAVE_PAIR_PHASE_WAIT_REQ;
    APP_PRINT_INFO0("sync: back to pair!");

    ppt_force_fw_ack(0);
    sync_acc_t acc = {.addr = SYNC_ACC_ADDR_BR};
    sync_acc_set(acc);
    sync_ppt_set_tifs(true);

    sync_set_header(0, 0);
    sync_pdu_t pdu;
    pdu.opcode = SYNC_OPCODE_PAIR_RSP;
    pdu.pair_rsp.acc = sync_slave_ctx.acc_data;
    ppt_push_tx_data(sizeof(sync_opcode_t) + sizeof(sync_pair_rsp_t), (uint8_t *)&pdu);

#ifndef SYNC_SLAVE_FIXED_CHANNEL
    sync_slave_handle_hopping();
#endif
}

PPT_API_SECTION void sync_slave_hb_exit(void)
{
    sync_slave_ctx.hb.slow = false;
    sync_slave_ctx.hb.state = SYNC_SLAVE_HB_STATE_IDLE;
    sync_slave_ctx.hb.exit_pend = false;
    sync_hw_timer_start(SYNC_SYNC_LOST_PERIOD, sync_slave_handle_sync_lost_to);
    APP_PRINT_INFO0("sync: hb exit");
    if (sync_ctx.hb_cb)
    {
        sync_hb_cb_param_t param = {};
        param.enter = false;
        sync_ctx.hb_cb(&param);
    }
}

PPT_ISR_SECTION void sync_slave_handle_tx_trigger(void)
{
    if (sync_slave_ctx.pair_state != SYNC_PAIR_STATE_PAIRED
        || sync_slave_ctx.state != SYNC_STATE_SYNC)
    {
        return;
    }

    if (sync_slave_ctx.hb.slow)
    {
        if (sync_slave_ctx.pmsg_sending[sync_slave_ctx.pmsg_sending_idx] == NULL)
        {
            if (sync_slave_ctx.busy == false)
            {
                sync_msg_t *pmsg = (sync_msg_t *)plt_list_pop(&sync_ctx.msg_send);
                ppt_update_tx_fifo(0, pmsg->data, pmsg->len);
                sync_slave_ctx.pmsg_sending[sync_slave_ctx.pmsg_sending_idx] = pmsg;
            }
            else
            {
                /* trigger latter after tx */
                sync_slave_ctx.tx_trigger_pending = true;
            }
        }
    }
}

PPT_ISR_SECTION void sync_slave_handle_ack(bool rx, sync_header_t rx_header)
{
    /* mark busy when send pkt immediately */
    sync_slave_ctx.busy = true;

    /* the old pkt is acked, just skip */
    if (sync_slave_ctx.pmsg_switching)
    {
        return;
    }

    bool retrans = false;
    uint8_t pmsg_sending_prev_idx = (sync_slave_ctx.pmsg_sending_idx + SYNC_SLAVE_TX_MSG_NUM - 1) %
                                    SYNC_SLAVE_TX_MSG_NUM;
    sync_msg_t *pmsg_sending_prev = sync_slave_ctx.pmsg_sending[pmsg_sending_prev_idx];
    if (pmsg_sending_prev && (rx == false || rx_header.ack == false))
    {
        switch (pmsg_sending_prev->type)
        {
        case SYNC_MSG_TYPE_FINITE_RETRANS:
            if (sync_ctx.msg_retrans_count < sync_ctx.msg_finite_retrans_num)
            {
                retrans = true;
            }
            break;
        case SYNC_MSG_TYPE_INFINITE_RETRANS:
            retrans = true;
            break;
        case SYNC_MSG_TYPE_DYNAMIC_RETRANS:
            if (sync_ctx.msg_retrans_count < sync_ctx.msg_dynamic_retrans_num_lower ||
                (sync_ctx.msg_send.count == 0 &&
                 (sync_ctx.msg_dynamic_retrans_num_upper == SYNC_MSG_RETRANS_NUM_INFINITE ||
                  sync_ctx.msg_retrans_count < sync_ctx.msg_dynamic_retrans_num_upper)))
            {
                retrans = true;
            }
            break;
        default:
            break;
        }
    }

    if (retrans)
    {
        sync_ctx.msg_retrans_count += 1;
        sync_set_header(rx, sync_slave_ctx.seq_tx);
        sync_slave_ctx.pmsg_switching = false;
    }
    else
    {
#if SYNC_SUPPORT_MT1R
        if (sync_slave_ctx.quick.enable)
        {
            ppt_lock(0, true);
            ppt_trigger_fw_ack_lock(0, false);
            sync_slave_ctx.seq_tx += 1;
            sync_slave_set_header(rx, sync_slave_ctx.seq_tx);
            WR_PPT_REG_FIELD(PRO_HS_LENGTH, hs_field_length_4_0, SYNC_SUFFIX_LEN);
            uint8_t ack_field = sync_slave_get_ack_field();
            WR_PPT_REG(PRO_HS_0_LOWER, ack_field);
#if TIME_DEBUG
            if (ack_field == 0x7f)
            {
                time_debug_level_high(SYNC_SLAVE_TIME_DEBUG_ALL_ACK);
                time_debug_level_low(SYNC_SLAVE_TIME_DEBUG_ALL_ACK);
            }
#endif
            ppt_lock(0, false);
        }
        else
#endif
        {
            uint8_t rd_ptr = ppt_trigger_fw_ack(0);
            sync_slave_ctx.pmsg_ptr[sync_slave_ctx.pmsg_sending_idx] = rd_ptr;
            sync_slave_ctx.pmsg_switching = true;
            sync_slave_ctx.pmsg_last_empty = pmsg_sending_prev == NULL;
            sync_slave_ctx.seq_tx += 1;
            sync_set_header(rx, sync_slave_ctx.seq_tx);
        }

        if (pmsg_sending_prev)
        {
            sync_msg_t *pmsg = pmsg_sending_prev;
            sync_slave_ctx.pmsg_sending[pmsg_sending_prev_idx] = NULL;
            if (pmsg->send_cb)
            {
                pmsg->send_info.res = rx ? (rx_header.ack ? SYNC_SEND_RESULT_ACKED : SYNC_SEND_RESULT_NACKED) :
                                      SYNC_SEND_RESULT_UNKNOWN;
                pmsg->send_info.retrans_count = sync_ctx.msg_retrans_count;
                sync_pendcall(sync_msg_handle_tx_done, pmsg, 0);
            }
            else
            {
                sync_ctx.msg_count[pmsg->type] -= 1;
                plt_list_push(&sync_ctx.msg_free, pmsg);
            }
        }

        sync_ctx.msg_retrans_count = 0;
        uint8_t pmsg_sending_next_idx = (sync_slave_ctx.pmsg_sending_idx + 1) % SYNC_SLAVE_TX_MSG_NUM;
        if (sync_ctx.msg_send.count == 0)
        {
            sync_pdu_t pdu;
            pdu.opcode = SYNC_OPCODE_EMPTY;
            DBG_DIRECT("ppt slave send empty data");
            ppt_push_tx_data(sizeof(sync_opcode_t), (uint8_t *)&pdu);
            sync_slave_ctx.pmsg_sending[pmsg_sending_next_idx] = NULL;
        }
        else
        {
            sync_msg_t *pmsg = (sync_msg_t *)plt_list_pop(&sync_ctx.msg_send);
            DBG_DIRECT("ppt slave send pmsg data");
            ppt_push_tx_fifo(0, pmsg->data, pmsg->len);
            sync_slave_ctx.pmsg_sending[pmsg_sending_next_idx] = pmsg;
        }
    }

#if SYNC_SUPPORT_POWER_CONTROL
    if (sync_ctx.tx_power_adaptive)
    {
        sync_ctx.power_ctrl_count_fail += !(rx && rx_header.ack);
    }
#endif
}

PPT_ISR_SECTION void sync_slave_handle_rx(bool rx, sync_header_t rx_header, sync_pdu_t *rx_pdu,
                                          uint16_t rx_len, int8_t rssi, uint32_t rx_crc)
{
    //APP_PRINT_ERROR5("sync: prx rx %d, header 0x%x, seq rx %d, len %d %b", rx, rx_header.value, sync_slave_ctx.seq_rx, rx_len, TRACE_BINARY(rx_len, rx_pdu));
    if (rx)
    {
        /* rx fail may due to interference, so can't exit hb */
        sync_slave_ctx.hb.state = SYNC_SLAVE_HB_STATE_IDLE;
        bool seq_check = false;
#if SYNC_SUPPORT_MT1R
        if (sync_slave_ctx.quick.enable)
        {
            if (sync_slave_ctx.seq_rx != rx_header.m.seq)
            {
                seq_check = true;
                sync_slave_ctx.seq_rx = rx_header.m.seq;
            }
        }
        else
#endif
        {
            if (sync_slave_ctx.seq_rx != rx_header.seq)
            {
                seq_check = true;
                sync_slave_ctx.seq_rx = rx_header.seq;
            }
        }
        if (seq_check)
        {
            sync_slave_ctx.crc_rx = rx_crc;
            if (rx_len > 0)
            {
                if (rx_pdu->opcode >= SYNC_OPCODE_APP_START)
                {
#if SYNC_SUPPORT_MT1R
                    if (sync_slave_ctx.quick.enable)
                    {
                        if (sync_rx_msg_queue.tail + 1 == sync_rx_msg_queue.head)
                        {
                            APP_PRINT_ERROR0("sync: rx full");
                        }
                        else
                        {
                            if (rx_len <= SYNC_RX_MSG_QUEUE_DATA_LEN)
                            {
                                uint8_t idx = sync_rx_msg_queue.tail & (SYNC_RX_MSG_QUEUE_SIZE - 1);
                                sync_rx_msg_queue.buffer[idx].rssi = rssi;
                                sync_rx_msg_queue.buffer[idx].len = rx_len;
                                memcpy(sync_rx_msg_queue.buffer[idx].data, (uint8_t *)rx_pdu, rx_len);
                                sync_rx_msg_queue.tail += 1;
                                sync_pendcall(sync_slave_handle_rx_task, 0, 0);
                            }
                            else
                            {
                                APP_PRINT_ERROR1("sync: rx length exceed %d", rx_len);
                            }
                        }
                    }
                    else
#endif
                    {
                        if (sync_ctx.msg_receive_cb)
                        {
                            sync_receive_info_t info = {};
                            info.rssi = rssi;
                            sync_ctx.msg_receive_cb((uint8_t *)rx_pdu, rx_len, &info);
                        }
                    }
                }
                else
                {
                    bool handle = false;
                    switch (rx_pdu->opcode)
                    {
                    case SYNC_OPCODE_EMPTY:
                        if (rx_len == 1)
                        {
                            handle = true;
                        }
                        break;
                    case SYNC_OPCODE_HEARTBEAT:
                        if (rx_len == sizeof(sync_opcode_t) + sizeof(sync_heartbeat_t))
                        {
                            handle = true;
                            if (rx_pdu->heartbeat.interval != 0)
                            {
                                if (sync_slave_ctx.hb.interval != rx_pdu->heartbeat.interval)
                                {
                                    sync_slave_ctx.hb.interval_changed = true;
                                    sync_slave_ctx.hb.interval = rx_pdu->heartbeat.interval;
                                }
                                sync_slave_ctx.hb.state = SYNC_SLAVE_HB_STATE_HB_RECEIVED;
                            }
                            else
                            {
                                APP_PRINT_ERROR0("sync: hb interval invalid");
                            }
                        }
                        break;
                    default:
                        break;
                    }
                    if (handle == false)
                    {
                        APP_PRINT_WARN3("sync: rx unknown opcode %d, len %d, data %b", rx_pdu->opcode, rx_len,
                                        TRACE_BINARY(rx_len, rx_pdu));
                    }
                }
            }
        }
    }
}

#if SYNC_SUPPORT_MT1R
static void sync_slave_handle_sync_lost_to_task_quick(void *para1, uint32_t para2)
{
    if (sync_slave_ctx.state != SYNC_STATE_SYNC_FAST)
    {
        APP_PRINT_WARN1("sync: lost, state invalid %d!", sync_slave_ctx.state);
        return;
    }
    uint8_t chan_num, chan_start;
    chan_num = sync_ctx.chan_num / sync_ctx.chan_group_num;
    chan_start = (sync_ctx.chan_cur / chan_num) * chan_num;
#if SYNC_SLAVE_CCA_ENABLE
    int8_t chan_next = sync_slave_cca_get(chan_start, chan_num);
#else
    uint8_t chan_next = chan_start + sync_rand() % chan_num;
#endif
    APP_PRINT_INFO2("sync: lost, cca change to %d[%d]!", chan_next, sync_ctx.chan_cfg[chan_next].freq);
    sync_channel_change(chan_next);

    /* restart rx */
    ppt_execute_instruction(PPT_HW_INSTRUCTION_PRX_ENABLE);
    sync_slave_ctx.count = 0;
}

void sync_slave_handle_sync_lost_to_quick(void)
{
    sync_slave_ctx.state = SYNC_STATE_SYNC_FAST;
    sync_slave_ctx.count = 0;

    APP_PRINT_INFO0("sync: lost!");
#ifdef SYNC_DEBUG
    sync_pendcall(sync_slave_handle_sync_lost, 0, 0);
#endif

#if SYNC_SUPPORT_SYNC_FAST_DISABLE
    if (SYNC_SYNC_FAST_PERIOD == SYNC_SYNC_FAST_PERIOD_DISABLE)
    {
        sync_hw_timer_stop();
        sync_hw_timer1_stop();
        sync_slave_handle_sync_fast_to();
        return;
    }
#endif

#ifndef SYNC_SLAVE_FIXED_CHANNEL
    {
        /* stop rx */
        sync_slave_close(false);
        sync_pendcall(sync_slave_handle_sync_lost_to_task_quick, 0, 0);
    }
#endif
}

void sync_rx_msg_queue_clear(void)
{
    sync_rx_msg_queue.head = 0;
    sync_rx_msg_queue.tail = 0;
}

static void sync_slave_handle_rx_task(void *para1, uint32_t para2)
{
    while (sync_rx_msg_queue.tail != sync_rx_msg_queue.head)
    {
        uint8_t idx = sync_rx_msg_queue.head & (SYNC_RX_MSG_QUEUE_SIZE - 1);
        if (sync_ctx.msg_receive_cb)
        {
            sync_receive_info_t info = {};
            info.rssi = sync_rx_msg_queue.buffer[idx].rssi;
            sync_ctx.msg_receive_cb(sync_rx_msg_queue.buffer[idx].data, sync_rx_msg_queue.buffer[idx].len,
                                    &info);
        }
        sync_rx_msg_queue.head += 1;
    }
}

static void sync_slave_close_quick(void)
{
    PRO_MISR_TYPE reg_misr;
    reg_misr.d16 = RD_PPT_REG(PRO_MISR);
    bool clear = reg_misr.tx_int == false && reg_misr.rx_int == false;
    ppt_execute_instruction(PPT_HW_INSTRUCTION_PRX_DISABLE);
    ppt_clear_radio();
    do
    {
        reg_misr.d16 = RD_PPT_REG(PRO_MISR);
    }
    while (reg_misr.kill_prx_int == 0);
    WR_PPT_REG(PRO_MISR, BIT5);
    if (clear)
    {
        ppt_flush_trx_int();
    }
}


PPT_ISR_SECTION static inline void sync_slave_init_ack_field(void)
{
    sync_slave_ctx.quick.seq_ack_rx = 1;
    sync_slave_ctx.quick.ack_rsp_idx = 2;
    sync_slave_ctx.quick.ack_curr_idx = 2;
    sync_slave_ctx.quick.ack_field[2] = 0xff;
}

PPT_ISR_SECTION static inline uint8_t sync_slave_get_ack_field(void)
{
    return sync_slave_ctx.quick.ack_field[sync_slave_ctx.quick.ack_rsp_idx];
}

PPT_ISR_SECTION static inline void sync_slave_refresh_ack_field(void)
{
    uint8_t num = (3 - sync_slave_ctx.quick.ack_rsp_idx + sync_slave_ctx.quick.ack_curr_idx) % 3 + 1;
    if (num > 1)
    {
        time_debug_level_high(SYNC_SLAVE_TIME_DEBUG_REFRESH_ACK);
        time_debug_level_low(SYNC_SLAVE_TIME_DEBUG_REFRESH_ACK);
        if (num > 2)
        {
            time_debug_level_high(SYNC_SLAVE_TIME_DEBUG_REFRESH_ACK);
            time_debug_level_low(SYNC_SLAVE_TIME_DEBUG_REFRESH_ACK);
        }
    }

    if (num < 3)
    {
        sync_slave_ctx.quick.ack_curr_idx = (sync_slave_ctx.quick.ack_curr_idx + 1) % 3;
    }
    else
    {
        uint8_t ack = sync_slave_ctx.quick.ack_field[sync_slave_ctx.quick.ack_curr_idx];
        if (ack)
        {
            uint8_t idx = (sync_slave_ctx.quick.ack_curr_idx + 2) % 3;
            sync_slave_ctx.quick.ack_field[idx] = ack;
        }
    }
    sync_slave_ctx.quick.ack_field[sync_slave_ctx.quick.ack_curr_idx] = 0;
    if (SYNC_ISR_DEBUG)
    {
        APP_PRINT_WARN7("sync_isr: ack refresh ce %d, seq %d, rsp %d, curr %d, 0x%x 0x%x 0x%x",
                        sync_slave_ctx.quick.ce_cnt, sync_slave_ctx.quick.seq_ack_rx,
                        sync_slave_ctx.quick.ack_rsp_idx, sync_slave_ctx.quick.ack_curr_idx,
                        sync_slave_ctx.quick.ack_field[0], sync_slave_ctx.quick.ack_field[1],
                        sync_slave_ctx.quick.ack_field[2]);
    }

}

PPT_ISR_SECTION static inline bool sync_slave_update_ack_field(uint8_t slot_idx, uint8_t seq_ack,
                                                               bool req_ack)
{
    bool ack = true;
    uint32_t lock = sync_enter_critical();
    if (req_ack)
    {
        if (sync_slave_ctx.quick.seq_ack_rx != seq_ack)
        {
            sync_slave_ctx.quick.ack_rsp_idx = (sync_slave_ctx.quick.ack_rsp_idx + 1) % 3;
        }
        else
        {
            ack = false;
            uint8_t num = (3 - sync_slave_ctx.quick.ack_rsp_idx + sync_slave_ctx.quick.ack_curr_idx) % 3 + 1;
            if (num == 3)
            {
                time_debug_level_high(SYNC_SLAVE_TIME_DEBUG_UPDATE_ACK);
                time_debug_level_low(SYNC_SLAVE_TIME_DEBUG_UPDATE_ACK);
                uint8_t ack = sync_slave_ctx.quick.ack_field[sync_slave_ctx.quick.ack_curr_idx];
                sync_slave_ctx.quick.ack_curr_idx = (sync_slave_ctx.quick.ack_curr_idx + 2) % 3;
                sync_slave_ctx.quick.ack_field[sync_slave_ctx.quick.ack_curr_idx] = ack;
            }
        }
    }
    else
    {
        sync_slave_ctx.quick.ack_field[sync_slave_ctx.quick.ack_curr_idx] |= BIT(slot_idx);
        sync_slave_ctx.quick.ack_rsp_idx = sync_slave_ctx.quick.ack_curr_idx;
    }
    sync_slave_ctx.quick.seq_ack_rx = seq_ack;
    sync_exit_critical(lock);
    if (sync_slave_ctx.quick.seq_ack_rx & 0x1)
    {
        time_debug_level_high(SYNC_SLAVE_TIME_DEBUG_SEQ_ACK);
    }
    else
    {
        time_debug_level_low(SYNC_SLAVE_TIME_DEBUG_SEQ_ACK);
    }
    return ack;
}

PPT_ISR_SECTION void sync_slave_handle_sync_early_delay(void)
{
    WR_PPT_REG_FIELD(PRO_TRX_CONTROL, ack_enable, 1);
}

PPT_ISR_SECTION void sync_slave_handle_sync_early_quick(void)
{
    sync_slave_ctx.quick.count = (sync_slave_ctx.quick.count + 1) & 0x7;
    if (sync_slave_ctx.quick.count == 0)
    {
        time_debug_level_low(TIME_DEBUG_CHAN_TIMING);
        sync_slave_ctx.quick.ce_cnt += 1;
        sync_slave_refresh_ack_field();
        WR_PPT_REG_FIELD(PRO_TRX_CONTROL, ack_enable, 0);

        time_debug_level_high(TIME_DEBUG_CHAN_TIMING);
        sync_slave_ctx.rx_ok = false;
        sync_slave_ctx.ce_clk = sync_hw_frtimer_get_count();
    }
    else if (sync_slave_ctx.quick.count == 7)
    {
        if (sync_slave_ctx.rx_ok == false)
        {
            if (sync_slave_ctx.state == SYNC_STATE_SYNC)
            {
                sync_slave_ctx.count += 1;
                uint32_t hb_time = sync_slave_ctx.count * SYNC_SYNC_SPEED;
                if (sync_slave_ctx.hb.slow)
                {
                    uint32_t hb_interval = SYNC_HB_TO_TIMES * sync_slave_ctx.hb.interval * 1000;
                    if (hb_time >= hb_interval + SYNC_SYNC_LOST_PERIOD)
                    {
                        sync_slave_handle_sync_lost_to_quick();
                    }
                    else if (hb_time >= hb_interval)
                    {
                        sync_hw_timer_shift(-5);
                    }
                }
                else
                {
                    if (hb_time >= SYNC_SYNC_LOST_PERIOD)
                    {
                        sync_slave_handle_sync_lost_to_quick();
                    }
                    else if (sync_slave_ctx.count >= 2)
                    {
                        sync_hw_timer_shift(-5);
                    }
                }
            }
            else if (sync_slave_ctx.state == SYNC_STATE_SYNC_FAST)
            {
                sync_slave_ctx.count += 1;
                if (sync_slave_ctx.count >= SYNC_SYNC_FAST_PERIOD / SYNC_SYNC_SPEED)
                {
                    sync_hw_timer_stop();
                    sync_hw_timer1_stop();
                    sync_slave_handle_sync_fast_to();
                }
            }
        }
        else
        {
            sync_slave_ctx.count = 0;
        }
    }
    else
    {
        if (sync_slave_ctx.quick.count == 6)
        {
            /* delay to start ack in case of ack at slot 5 */
            sync_hw_timer1_start(30, sync_slave_handle_sync_early_delay);
        }
    }
}

PPT_ISR_SECTION static void sync_slave_ppt_isr_handler_quick(void)
{
    time_debug_level_high(TIME_DEBUG_CHAN_ISR);
    PRO_MISR_TYPE reg_misr;
    reg_misr.d16 = RD_PPT_REG(PRO_MISR);
    //APP_PRINT_INFO1("sync_isr: prx isr 0x%04x", reg_misr.d16);

    if (reg_misr.rx_int)
    {
        do
        {
            /* pop rx stack */
            //PRO_LENGTH_INCLUDE_ADDON_TYPE len_inc_addon = {.d16 = RD_PPT_REG(PRO_LENGTH_INCLUDE_ADDON)};
            PRO_RX_STACK_TYPE rx_stack = {.d16 = RD_PPT_REG(PRO_RX_STACK)};
            if (rx_stack.is_max_length)
            {
                if (SYNC_ISR_DEBUG)
                {
                    APP_PRINT_WARN0("sync_isr: prx rx max length");
                }
                PRO_RX_HS_UPPER_TYPE rx_hs_upper = {.d16 = RD_PPT_REG(PRO_RX_HS_UPPER)}; //read 0x62 to pop rx stack
                break;
            }

            if (rx_stack.rx_time_out || rx_stack.rx_abort_rd)
            {
                PRO_RX_HS_UPPER_TYPE rx_hs_upper = {.d16 = RD_PPT_REG(PRO_RX_HS_UPPER)}; //read 0x62 to pop rx stack
                break;
            }

            PRO_RX_HP_TYPE hp = {.d16 = RD_PPT_REG(PRO_RX_HP)};
            bool rx = !(rx_stack.rx_time_out || rx_stack.rx_hit == false || rx_stack.is_crc_error);
            sync_slave_ctx.rx_ok = rx;
            sync_slave_ctx.busy = sync_slave_ctx.rx_ok;

            PRO_LENGTH_TYPE length = {.d16 = RD_PPT_REG(PRO_LENGTH)};
            PRO_ACCHIT_CLK_LOWER_TYPE acchit_clk_lower = {.d16 = RD_PPT_REG(PRO_ACCHIT_CLK_LOWER)};
            PRO_ACCHIT_CLK_UPPER_TYPE acchit_clk_upper = {.d16 = RD_PPT_REG(PRO_ACCHIT_CLK_UPPER)};
            uint32_t acchit_clk = (acchit_clk_upper.acchit_clk_31_16 << 16) + acchit_clk_lower.acchit_clk_15_0;
            sync_slave_ctx.quick.rx_clk = acchit_clk;
            int32_t clk_diff = 0;
            /* determine slot only when rx success */
            bool ack_slot_flag = false;
            sync_slave_ctx.quick.slot_idx = -1;
            if (rx_stack.rx_time_out == false && rx_stack.rx_hit)
            {
                time_debug_level_high(SYNC_SLAVE_TIME_DEBUG_RX);
                uint32_t lock = sync_enter_critical();
                do
                {
                    //if (sync_slave_ctx.quick.count == 6 || sync_slave_ctx.quick.count == 7)
                    {
                        clk_diff = sync_hw_frtimer_time_diff(acchit_clk, sync_slave_ctx.ce_clk);
                        //APP_PRINT_ERROR3("DIFF %d %x %x", clk_diff, sync_slave_ctx.ce_clk, acchit_clk);
                        /* the acchit time is at least 30us */
                        if (clk_diff < 30)
                        {
                            break;
                        }
                        clk_diff -= 30;
                        sync_slave_ctx.quick.slot_idx = clk_diff / 125;
                        /* ack slot */
                        if (clk_diff < 750 || clk_diff >= 875)
                        {
                            break;
                        }
                        ack_slot_flag = true;
                    }
                }
                while (0);
                sync_exit_critical(lock);
                time_debug_level_low(SYNC_SLAVE_TIME_DEBUG_RX);
#if TIME_DEBUG
                if (length.d16 != 1)
                {
                    time_debug_level_high(SYNC_SLAVE_TIME_DEBUG_RX);
                    time_debug_level_low(SYNC_SLAVE_TIME_DEBUG_RX);
                }
#endif
            }

#if SYNC_SLAVE_TEST_RX_STOP
            bool switch_ptx = false;
#endif
            sync_header_t rx_header;
            rx_header.value = hp.hp;
            if (rx)
            {
                /* check ack status */
                if (sync_slave_ctx.quick.slot_idx >= 0)
                {
                    bool ack = sync_slave_update_ack_field(sync_slave_ctx.quick.slot_idx, rx_header.m.ack,
                                                           rx_header.m.req_ack);
                    rx_header.ack = ack;
                }

                if (sync_slave_ctx.quick.slot_idx < 6 && ((sync_slave_ctx.quick.slot_idx & 0x1) == 0) &&
                    rx_header.m.req_ack)
                {
                    time_debug_level_high(SYNC_SLAVE_TIME_DEBUG_REQ_ACK);
                    time_debug_level_low(SYNC_SLAVE_TIME_DEBUG_REQ_ACK);

                    //time_debug_level_high(TIME_DEBUG_CHAN_THREAD);
                    ppt_kill_hw_fsm(PPT_FSM_PRX);
                    //time_debug_level_low(TIME_DEBUG_CHAN_THREAD);
                    ppt_execute_instruction(PPT_HW_INSTRUCTION_PTX_ENABLE);
                    sync_slave_handle_ack(rx, rx_header);
                    sync_slave_ctx.quick.req_ack = true;
#if SYNC_SLAVE_TEST_RX_STOP
                    switch_ptx = true;
#endif
                }
            }

            /* only ack slot ptx pkt is used */
            if (rx && ack_slot_flag)
            {
                sync_slave_handle_ack(rx, rx_header);

                int32_t slot_shift = clk_diff - 750;
                slot_shift -= 48;
                if (ABS(slot_shift) > 2)
                {
                    uint32_t lock = sync_enter_critical();
                    sync_hw_timer_shift(slot_shift);
                    sync_exit_critical(lock);
                }
            }

            PRO_RX_CRC_LOWER_TYPE rx_crc_lower = {.d16 = RD_PPT_REG(PRO_RX_CRC_LOWER)};
            PRO_RX_CRC_UPPER_TYPE rx_crc_upper = {.d16 = RD_PPT_REG(PRO_RX_CRC_UPPER)};
            uint32_t rx_crc = (rx_crc_upper.d16 << 16) + rx_crc_lower.d16;
            rx_crc >>= 32 - SYNC_CRC_LEN;
            //PRO_RX_HS_LOWER_TYPE rx_hs_lower = {.d16 = RD_PPT_REG(PRO_RX_HS_LOWER)};
            PRO_RSSI_TYPE rssi = {.d16 = RD_PPT_REG(PRO_RSSI)};
            PRO_RX_HS_UPPER_TYPE rx_hs_upper = {.d16 = RD_PPT_REG(PRO_RX_HS_UPPER)}; //read 0x62 to pop rx stack
            //PRO_INT_CTRL_TYPE int_ctrl = {.d16 = RD_PPT_REG(PRO_INT_CTRL)};
            if (SYNC_ISR_DEBUG)
            {
                APP_PRINT_INFO6("sync_isr: prx rx stack 0x%04x, ce %d, clk %d, slot %d, hp 0x%02x, len %d",
                                rx_stack.d16,
                                sync_slave_ctx.quick.ce_cnt, clk_diff, sync_slave_ctx.quick.slot_idx, hp.hp, length.d16);
            }

            /* clear rx fifo */
            if (ack_slot_flag || rx == false)
            {
                uint16_t rx_len = rx ? length.d16 + SYNC_OVERHEAD_LEN : 0;
                if (rx_len & 0x1)
                {
                    rx_len += 1;
                }
                ACL_RX_FIFO_S rx_fifo_ptr = {.d32 = RD_U32_BZDMA_REG(BZDMA_REG_ACL_RXFIFO_PTR)};
                uint16_t fifo_len = BZDMA_ACL_RXFIFO_GET_LEFT(rx_fifo_ptr);
                while (fifo_len > rx_len)
                {
                    uint16_t pop_len = MIN(fifo_len - rx_len, PPT_RX_BUFFER_SIZE);
                    uint8_t *rx_buffer = ppt_get_rx_buffer(pop_len);
                    ppt_pop_rx_fifo(0, rx_buffer, pop_len);
                    fifo_len -= pop_len;
                }
            }

            if (rx)
            {
                uint16_t rx_len = length.d16 + SYNC_OVERHEAD_LEN;
                uint8_t *rx_buffer = ppt_get_rx_buffer(rx_len);
                ppt_pop_rx_fifo(0, rx_buffer, rx_len);
#if SYNC_SLAVE_TEST_RX_STOP
                uint32_t rand = sync_rand() % 100;
                if (rand < 30)
                {
                    time_debug_level_high(TIME_DEBUG_CHAN_THREAD);
                    ppt_kill_hw_fsm(switch_ptx ? PPT_FSM_PTX : PPT_FSM_PRX);
                    time_debug_level_low(TIME_DEBUG_CHAN_THREAD);
                    time_debug_level_high(TIME_DEBUG_CHAN_THREAD);
                    ppt_execute_instruction(PPT_HW_INSTRUCTION_PRX_ENABLE);
                    time_debug_level_low(TIME_DEBUG_CHAN_THREAD);
                    WR_PPT_REG_FIELD(PRO_HS_LENGTH, hs_field_length_4_0, 0);
                }
                else
#endif
                {
                    sync_pdu_t *pdu = (sync_pdu_t *)(rx_buffer + SYNC_OVERHEAD_LEN);
                    uint16_t buffer_head = rx_buffer[0] + (rx_buffer[1] << 8);
                    uint8_t buffer_len = ((buffer_head >> SYNC_HEADER_LEN_MT1R) & (0xffff >> (16 - SYNC_LENGTH_LEN)));
                    if (length.d16 != buffer_len)
                    {
                        APP_PRINT_INFO3("sync_isr: rx fifo corrupt, count %d, rx len %d, buffer len %d",
                                        sync_slave_ctx.quick.count, length.d16, buffer_len);
                        break;
                    }
                    if (length.d16 == sizeof(sync_opcode_t) + sizeof(sync_conn_req_t) &&
                        pdu->opcode == SYNC_OPCODE_CONN_REQ)
                    {
                        APP_PRINT_ERROR0("sync: state out of sync");
                        sync_slave_handle_exception();
                        break;
                    }

                    sync_header_t rx_header;
                    rx_header.value = hp.hp;
                    int8_t rssi_dbm = ppt_get_rssi(rssi.rssi);
                    sync_slave_handle_rx(true, rx_header, pdu, length.d16, rssi_dbm, rx_crc);
                }
            }
            else
            {
                if (ack_slot_flag)
                {
                    sync_slave_close(false);
                    ppt_execute_instruction(PPT_HW_INSTRUCTION_PRX_ENABLE);
                }
            }
        }
        while (0);
    }

    if (reg_misr.tx_int)
    {
        sync_slave_ctx.busy = false;
        sync_slave_ctx.count = 0;
        WR_PPT_REG_FIELD(PRO_HS_LENGTH, hs_field_length_4_0, 0);
        /* pop tx stack */
        PRO_TX_STACK_TYPE tx_stack = {.d16 = RD_PPT_REG(PRO_TX_STACK)};
        PRO_TX_HS_LOWER_TYPE hs_lower = {.d16 = RD_PPT_REG(PRO_TX_HS_LOWER)};
        PRO_TX_HS_UPPER_TYPE hs_upper = {.d16 = RD_PPT_REG(PRO_TX_HS_UPPER)};
        PRO_TX_CLK_LOWER_TYPE tx_clk_lower = {.d16 = RD_PPT_REG(PRO_TX_CLK_LOWER)};
        PRO_TX_CLK_UPPER_TYPE tx_clk_upper = {.d16 = RD_PPT_REG(PRO_TX_CLK_UPPER)};
        PRO_TX_LENGTH_TYPE length = {.d16 = RD_PPT_REG(PRO_TX_LENGTH)}; // Read 0x6a to pop stack

        if (sync_slave_ctx.quick.req_ack)
        {
            sync_slave_ctx.quick.req_ack = false;
            ppt_execute_instruction(PPT_HW_INSTRUCTION_PRX_ENABLE);
        }

        uint32_t tx_clk = (tx_clk_upper.tx_clk_31_16 << 16) + tx_clk_lower.tx_clk_15_0;
        uint32_t tx_delay = sync_hw_frtimer_time_elapse(tx_clk, sync_slave_ctx.quick.rx_clk);


        uint32_t hs = (hs_upper.d16 << 16) + hs_lower.d16;
        if (SYNC_ISR_DEBUG)
        {
            APP_PRINT_INFO8("sync_isr: prx tx stack 0x%04x(ptr %d), ce %d slot %d, delay %d hs 0x%x(0x%x), len %d",
                            tx_stack.d16,
                            tx_stack.tx_ptr, sync_slave_ctx.quick.ce_cnt, sync_slave_ctx.quick.slot_idx,
                            tx_delay, hs, sync_slave_get_ack_field(), length.tx_length);
        }
        if (hs != sync_slave_get_ack_field())
        {
            if (SYNC_ISR_DEBUG)
            {
                APP_PRINT_WARN0("sync_isr: prx tx header fail");
            }
        }

        sync_msg_t *pmsg = sync_slave_ctx.pmsg_sending[sync_slave_ctx.pmsg_sending_idx];
        //APP_PRINT_INFO3("sync: hb sending idx %d 0x%x, hb state %d", sync_slave_ctx.pmsg_sending_idx, pmsg,
        //                sync_slave_ctx.hb.state);
        bool slow = false;
        if (pmsg == NULL)
        {
            if (sync_slave_ctx.hb.state == SYNC_SLAVE_HB_STATE_HB_RECEIVED)
            {
                slow = true;
            }
        }

        if (slow)
        {
            if (sync_slave_ctx.hb.slow == false || sync_slave_ctx.hb.interval_changed)
            {
                sync_slave_ctx.hb.slow = true;
                sync_slave_ctx.hb.interval_changed = false;
                APP_PRINT_INFO1("sync: hb enter %dms", sync_slave_ctx.hb.interval);
                if (sync_ctx.hb_cb)
                {
                    sync_hb_cb_param_t param = {};
                    param.enter = true;
                    param.interval = sync_slave_ctx.hb.interval;
                    sync_ctx.hb_cb(&param);
                }
            }
        }
        else
        {
            if (sync_slave_ctx.hb.slow)
            {
                sync_slave_ctx.hb.slow = false;
                sync_slave_ctx.hb.state = SYNC_SLAVE_HB_STATE_IDLE;
                APP_PRINT_INFO0("sync: hb exit");
                if (sync_ctx.hb_cb)
                {
                    sync_hb_cb_param_t param = {};
                    param.enter = false;
                    sync_ctx.hb_cb(&param);
                }
            }
        }
        sync_slave_ctx.pmsg_sending_idx = (sync_slave_ctx.pmsg_sending_idx + 1) % SYNC_SLAVE_TX_MSG_NUM;

        if (sync_slave_ctx.state == SYNC_STATE_SYNC && sync_slave_ctx.rx_ok)
        {
            //sync_hw_timer_set_periodic(false);
            //sync_slave_handle_sync_early();
        }
    }

    time_debug_level_low(TIME_DEBUG_CHAN_ISR);
}

PPT_ISR_SECTION void sync_slave_handle_sync_early_first_quick(void)
{
    sync_hw_timer_start(125, sync_slave_handle_sync_early_quick);
    sync_hw_timer_set_periodic(true);
    sync_hw_timer_impr_ip(true);
    ppt_reg_handler(sync_slave_ppt_isr_handler_quick);
    sync_slave_ctx.quick.enable = true;
    sync_slave_ctx.quick.count = 7;
    sync_slave_ctx.quick.ce_cnt -= 1;
    sync_slave_init_ack_field();
    WR_PPT_REG_FIELD(PRO_FIELD_LENGTH, hp_field_length_3_0, SYNC_HEADER_LEN_MT1R);
    sync_slave_handle_sync_early_quick();
}
#endif

PPT_ISR_SECTION static void sync_slave_ppt_isr_handler(void)
{
    time_debug_level_high(TIME_DEBUG_CHAN_ISR);
    PRO_MISR_TYPE reg_misr;
    reg_misr.d16 = RD_PPT_REG(PRO_MISR);
    //APP_PRINT_INFO1("sync_isr: prx isr 0x%04x", reg_misr.d16);

    if (reg_misr.rx_int)
    {
        do
        {
            /* pop rx stack */
            //PRO_LENGTH_INCLUDE_ADDON_TYPE len_inc_addon = {.d16 = RD_PPT_REG(PRO_LENGTH_INCLUDE_ADDON)};
            PRO_RX_STACK_TYPE rx_stack = {.d16 = RD_PPT_REG(PRO_RX_STACK)};
            if (rx_stack.is_max_length)
            {
                APP_PRINT_WARN0("sync_isr: prx rx max length");
                PRO_RX_HS_UPPER_TYPE rx_hs_upper = {.d16 = RD_PPT_REG(PRO_RX_HS_UPPER)}; //read 0x62 to pop rx stack
                break;
            }

            PRO_RX_HP_TYPE hp = {.d16 = RD_PPT_REG(PRO_RX_HP)};
            bool rx = !(rx_stack.rx_time_out || rx_stack.rx_hit == false || rx_stack.is_crc_error);
            /* no need ack when rx fail */
            if (rx)
            {
                if (sync_slave_ctx.pair_state == SYNC_PAIR_STATE_PAIRED &&
                    sync_slave_ctx.state != SYNC_STATE_SYNC_SLOW)
                {
                    sync_header_t rx_header;
                    rx_header.value = hp.hp;
                    sync_slave_handle_ack(rx, rx_header);

                    /* exit hb state if acked when pending */
                    if (sync_slave_ctx.hb.slow && sync_slave_ctx.hb.exit_pend)
                    {
                        if (rx_header.ack)
                        {
                            sync_slave_hb_exit();
                        }
                    }
                }
            }

#if SYNC_SUPPORT_MT1R
            PRO_ACCHIT_CLK_LOWER_TYPE acchit_clk_lower = {.d16 = RD_PPT_REG(PRO_ACCHIT_CLK_LOWER)};
            PRO_ACCHIT_CLK_UPPER_TYPE acchit_clk_upper = {.d16 = RD_PPT_REG(PRO_ACCHIT_CLK_UPPER)};
#endif
            PRO_RX_CRC_LOWER_TYPE rx_crc_lower = {.d16 = RD_PPT_REG(PRO_RX_CRC_LOWER)};
            PRO_RX_CRC_UPPER_TYPE rx_crc_upper = {.d16 = RD_PPT_REG(PRO_RX_CRC_UPPER)};
            uint32_t rx_crc = (rx_crc_upper.d16 << 16) + rx_crc_lower.d16;
            rx_crc >>= 32 - SYNC_CRC_LEN;
            //PRO_RX_HS_LOWER_TYPE rx_hs_lower = {.d16 = RD_PPT_REG(PRO_RX_HS_LOWER)};
            PRO_RSSI_TYPE rssi = {.d16 = RD_PPT_REG(PRO_RSSI)};
            PRO_LENGTH_TYPE length = {.d16 = RD_PPT_REG(PRO_LENGTH)};
            PRO_RX_HS_UPPER_TYPE rx_hs_upper = {.d16 = RD_PPT_REG(PRO_RX_HS_UPPER)}; //read 0x62 to pop rx stack
            //PRO_INT_CTRL_TYPE int_ctrl = {.d16 = RD_PPT_REG(PRO_INT_CTRL)};
            if (SYNC_ISR_DEBUG)
            {
                APP_PRINT_INFO4("sync_isr: prx rx stack 0x%04x, rssi %d, hp 0x%02x, len %d", rx_stack.d16,
                                ppt_get_rssi(rssi.rssi), hp.hp, length.d16);
            }

            if (rx)
            {
                uint16_t rx_len = length.d16 + SYNC_OVERHEAD_LEN;
                uint8_t *rx_buffer = ppt_get_rx_buffer(rx_len);
                ppt_pop_rx_fifo(0, rx_buffer, rx_len);
                sync_pdu_t *pdu = (sync_pdu_t *)(rx_buffer + SYNC_OVERHEAD_LEN);

                if (sync_slave_ctx.pair_state == SYNC_PAIR_STATE_PAIRED)
                {
                    bool handle_rx = true;
                    if (sync_slave_ctx.state == SYNC_STATE_SYNC)
                    {
#if SYNC_SUPPORT_MT1R
                        if (sync_slave_ctx.quick.enable == false)
#endif
                        {
                            sync_hw_timer_restart();
                        }
                    }
                    else if (sync_slave_ctx.state == SYNC_STATE_SYNC_SLOW)
                    {
                        handle_rx = false;
                        if (length.d16 == sizeof(sync_opcode_t) + sizeof(sync_conn_req_t)  &&
                            pdu->opcode == SYNC_OPCODE_CONN_REQ)
                        {
#if SYNC_SUPPORT_MT1R
                            sync_ctx.mt1r.flag = pdu->conn_req.interval == 125;
                            sync_ctx.conn_interval = sync_ctx.mt1r.flag ? 250 : pdu->conn_req.interval;
                            if (sync_ctx.mt1r.flag)
                            {
                                sync_slave_ctx.ce_clk = (acchit_clk_upper.acchit_clk_31_16 << 16) +
                                                        acchit_clk_lower.acchit_clk_15_0;
                            }
#else
                            sync_ctx.conn_interval = pdu->conn_req.interval;
#endif
                        }
                        else
                        {
                            sync_slave_close(false);
                            ppt_execute_instruction(PPT_HW_INSTRUCTION_PRX_ENABLE);
                        }
                    }
                    else if (sync_slave_ctx.state == SYNC_STATE_SYNC_FAST)
                    {
                        APP_PRINT_INFO2("sync: reconnected on %d[%d]!", sync_ctx.chan_cur,
                                        sync_ctx.chan_cfg[sync_ctx.chan_cur].freq);
                        sync_slave_ctx.state = SYNC_STATE_SYNC;
                        sync_hw_timer_start(SYNC_SYNC_LOST_PERIOD, sync_slave_handle_sync_lost_to);
#ifdef SYNC_DEBUG
                        sync_pendcall(sync_slave_handle_sync_reconnected, 0, 0);
#endif
                    }

                    if (handle_rx)
                    {
                        if (length.d16 == sizeof(sync_opcode_t) + sizeof(sync_conn_req_t) &&
                            pdu->opcode == SYNC_OPCODE_CONN_REQ)
                        {
                            APP_PRINT_ERROR0("sync: state out of sync");
                            sync_slave_handle_exception();
                            break;
                        }

                        sync_header_t rx_header;
                        rx_header.value = hp.hp;
                        int8_t rssi_dbm = ppt_get_rssi(rssi.rssi);
                        sync_slave_handle_rx(true, rx_header, pdu, length.d16,
                                             rssi_dbm, rx_crc);
                    }
                }
                else if (sync_slave_ctx.pair_state == SYNC_PAIR_STATE_PAIRING)
                {
                    bool retry = true;
                    if (sync_slave_ctx.phase == SYNC_SLAVE_PAIR_PHASE_WAIT_REQ)
                    {
                        if (length.d16 == sizeof(sync_opcode_t) + sizeof(sync_pair_req_t) &&
                            pdu->opcode == SYNC_OPCODE_PAIR_REQ)
                        {
                            int8_t rssi_dbm = ppt_get_rssi(rssi.rssi);
                            if (rssi_dbm < sync_ctx.pair_rssi_th)
                            {
                                APP_PRINT_ERROR2("sync: pair rssi check fail, %d < %d", rssi_dbm, sync_ctx.pair_rssi_th);
                            }
                            else
                            {
                                retry = false;
                                sync_slave_ctx.phase = SYNC_SLAVE_PAIR_PHASE_SEND_RSP;
#if SYNC_SUPPORT_MT1R
                                sync_ctx.mt1r.flag = pdu->pair_req.interval == 125;
                                sync_ctx.conn_interval = sync_ctx.mt1r.flag ? 250 : pdu->pair_req.interval;
                                if (sync_ctx.mt1r.flag)
                                {
                                    sync_slave_ctx.ce_clk = (acchit_clk_upper.acchit_clk_31_16 << 16) +
                                                            acchit_clk_lower.acchit_clk_15_0;
                                }
#else
                                sync_ctx.conn_interval = pdu->pair_req.interval;
#endif
                            }
                        }
                    }
                    else if (sync_slave_ctx.phase == SYNC_SLAVE_PAIR_PHASE_WAIT_CFM)
                    {
                        if (length.d16 == sizeof(sync_opcode_t) && pdu->opcode == SYNC_OPCODE_PAIR_CONF)
                        {
                            retry = false;
                            sync_slave_ctx.phase = SYNC_SLAVE_PAIR_PHASE_SEND_ACK;
                        }
                    }

                    if (retry)
                    {
                        sync_slave_close(false);
                        ppt_execute_instruction(PPT_HW_INSTRUCTION_PRX_ENABLE);
                    }
                }
            }
            else
            {
                sync_slave_close(false);
                ppt_execute_instruction(PPT_HW_INSTRUCTION_PRX_ENABLE);
            }
        }
        while (0);
    }

    if (reg_misr.tx_int)
    {
        sync_slave_ctx.busy = false;
        /* pop tx stack */
        PRO_TX_STACK_TYPE tx_stack = {.d16 = RD_PPT_REG(PRO_TX_STACK)};
        //PRO_TX_HS_LOWER_TYPE hs_lower = {.d16 = RD_PPT_REG(PRO_TX_HS_LOWER)};
        //PRO_TX_HS_UPPER_TYPE hs_upper = {.d16 = RD_PPT_REG(PRO_TX_HS_UPPER)};
        PRO_TX_LENGTH_TYPE length = {.d16 = RD_PPT_REG(PRO_TX_LENGTH)}; // Read 0x6a to pop stack
        if (SYNC_ISR_DEBUG)
        {
            APP_PRINT_INFO3("sync_isr: prx tx stack 0x%04x(ptr %d), len %d", tx_stack.d16, tx_stack.tx_ptr,
                            length.tx_length);
        }

#if SYNC_SUPPORT_POWER_CONTROL
        if (sync_ctx.tx_power_adaptive)
        {
            sync_ctx.power_ctrl_count += 1;
        }
#endif

        if (sync_slave_ctx.pair_state == SYNC_PAIR_STATE_PAIRING)
        {
            if (sync_slave_ctx.phase == SYNC_SLAVE_PAIR_PHASE_SEND_RSP)
            {
                APP_PRINT_INFO0("sync: pairing!");
                sync_slave_ctx.phase = SYNC_SLAVE_PAIR_PHASE_WAIT_CFM;
                sync_hw_timer_start(SYNC_PAIR_CFM_PERIOD, sync_slave_handle_pair_cfm_to);

                sync_acc_set(sync_slave_ctx.acc_data);
                ppt_trigger_fw_ack(0);
                sync_ppt_set_tifs(false);

                sync_slave_ctx.seq_tx = 0;
                /* ack is checked by master */
                sync_set_header(1, 0);
                sync_pdu_t pdu;
                pdu.opcode = SYNC_OPCODE_EMPTY;
                ppt_push_tx_data(sizeof(sync_opcode_t), (uint8_t *)&pdu);
            }
            else if (sync_slave_ctx.phase == SYNC_SLAVE_PAIR_PHASE_SEND_ACK)
            {
                /* ack later in sync_slave_handle_ack */
                //ppt_trigger_fw_ack(0);

                sync_slave_ctx.seq_tx = 1;
                sync_set_header(0, 0);
                sync_pdu_t pdu;
                pdu.opcode = SYNC_OPCODE_EMPTY;
                ppt_push_tx_data(sizeof(sync_opcode_t), (uint8_t *)&pdu);

                sync_slave_ctx.pair_state = SYNC_PAIR_STATE_PAIRED;
                sync_slave_ctx.state = SYNC_STATE_SYNC;
                APP_PRINT_INFO3("sync: paired 0x%08x on %d[%d]!", sync_slave_ctx.acc_data.addr, sync_ctx.chan_cur,
                                sync_ctx.chan_cfg[sync_ctx.chan_cur].freq);
#if SYNC_SUPPORT_MT1R
                if (sync_ctx.mt1r.flag)
                {
                    uint32_t clk = sync_hw_frtimer_get_count();
                    uint32_t total_time = SYNC_PAIR_CFM_PERIOD + SYNC_CONNECT_DELAY;
                    uint32_t delay = sync_hw_frtimer_time_elapse(clk, sync_slave_ctx.ce_clk);
                    delay += 29;
                    delay = total_time > delay ? total_time - delay : 30;
                    sync_hw_timer_start(delay, sync_slave_handle_sync_early_first_quick);
                    sync_hw_timer1_init();
                    sync_ppt_set_rf_time_mt1r();
                    sync_ctx.conn_interval = 1000;
                    APP_PRINT_ERROR0("sync: quick enter");
                }
                else
#endif
                {
                    sync_hw_timer_start(SYNC_SYNC_LOST_PERIOD, sync_slave_handle_sync_lost_to);
                }
                sync_pendcall(sync_slave_handle_paired, 0, 0);
                sync_channel_print_statistic();
                sync_channel_clear_statistic();
#if SYNC_SUPPORT_POWER_CONTROL
                if (sync_ctx.tx_power_adaptive)
                {
                    sync_power_ctrl_start();
                }
#endif
            }
        }
        else if (sync_slave_ctx.pair_state == SYNC_PAIR_STATE_PAIRED)
        {
            if (sync_slave_ctx.state == SYNC_STATE_SYNC_SLOW)
            {
                /* ack later in sync_slave_handle_ack */
                //ppt_trigger_fw_ack(0);
                sync_ppt_set_tifs(false);

                sync_slave_ctx.seq_tx = 0;
                sync_set_header(0, 0);
                sync_pdu_t pdu;
                pdu.opcode = SYNC_OPCODE_EMPTY;
                ppt_push_tx_data(sizeof(sync_opcode_t), (uint8_t *)&pdu);

                APP_PRINT_INFO2("sync: connected on %d[%d]!", sync_ctx.chan_cur,
                                sync_ctx.chan_cfg[sync_ctx.chan_cur].freq);
                sync_slave_ctx.state = SYNC_STATE_SYNC;
#if SYNC_SUPPORT_MT1R
                if (sync_ctx.mt1r.flag)
                {
                    uint32_t clk = sync_hw_frtimer_get_count();
                    uint32_t delay = sync_hw_frtimer_time_elapse(clk, sync_slave_ctx.ce_clk);
                    delay += 29;
                    delay = SYNC_CONNECT_DELAY > delay ? SYNC_CONNECT_DELAY - delay : 30;
                    sync_hw_timer_start(delay, sync_slave_handle_sync_early_first_quick);
                    sync_hw_timer1_init();
                    sync_ppt_set_rf_time_mt1r();
                    sync_ctx.conn_interval = 1000;
                    APP_PRINT_ERROR0("sync: quick enter");
                }
                else
#endif
                {
                    sync_hw_timer_start(SYNC_SYNC_LOST_PERIOD, sync_slave_handle_sync_lost_to);
                }
                sync_pendcall(sync_slave_handle_connected, 0, 0);
                sync_channel_print_statistic();
                sync_channel_clear_statistic();
#if SYNC_SUPPORT_POWER_CONTROL
                if (sync_ctx.tx_power_adaptive)
                {
                    sync_power_ctrl_start();
                }
#endif
            }
            else
            {
                uint8_t msg_idx = sync_slave_ctx.pmsg_sending_idx;
                sync_msg_t *pmsg = sync_slave_ctx.pmsg_sending[msg_idx];
                //APP_PRINT_INFO3("sync: hb sending idx %d 0x%x, hb state %d", sync_slave_ctx.pmsg_sending_idx, pmsg,
                //                sync_slave_ctx.hb.state);
                bool slow = false;
                /* check if msg updated timely */
                if (sync_slave_ctx.pmsg_ptr[msg_idx] != tx_stack.tx_ptr
                    || (pmsg == NULL && sizeof(sync_opcode_t) != length.tx_length)
                    || (pmsg != NULL && pmsg->len != length.tx_length))
                {
                    APP_PRINT_WARN3("sync: pkt 0x%x not switched timely, tx ptr %d, pkt ptr %d", pmsg, tx_stack.tx_ptr,
                                    sync_slave_ctx.pmsg_ptr[msg_idx]);
                    /* old pkt may also trigger enter hb */
                    if (sync_slave_ctx.pmsg_last_empty && sync_slave_ctx.hb.state == SYNC_SLAVE_HB_STATE_HB_RECEIVED)
                    {
                        slow = true;
                    }
                }
                else
                {
                    if (sync_slave_ctx.pmsg_switching)
                    {
                        sync_slave_ctx.pmsg_switching = false;
                        /* switch when new pkt is transmitted */
                        sync_slave_ctx.pmsg_sending_idx = (sync_slave_ctx.pmsg_sending_idx + 1) % SYNC_SLAVE_TX_MSG_NUM;
                    }

                    if (pmsg == NULL)
                    {
                        if (sync_slave_ctx.hb.state == SYNC_SLAVE_HB_STATE_HB_RECEIVED)
                        {
                            slow = true;
                        }
                    }
                }

                if (slow)
                {
                    sync_hw_timer_start(SYNC_HB_TO_TIMES * sync_slave_ctx.hb.interval * 1000 + SYNC_SYNC_LOST_PERIOD,
                                        sync_slave_handle_sync_lost_to);
                    if (sync_slave_ctx.hb.slow == false || sync_slave_ctx.hb.interval_changed)
                    {
                        sync_slave_ctx.hb.slow = true;
                        sync_slave_ctx.hb.interval_changed = false;
                        APP_PRINT_INFO1("sync: hb enter %dms", sync_slave_ctx.hb.interval);
                        if (sync_ctx.hb_cb)
                        {
                            sync_hb_cb_param_t param = {};
                            param.enter = true;
                            param.interval = sync_slave_ctx.hb.interval;
                            sync_ctx.hb_cb(&param);
                        }
                    }
                }
                else
                {
                    if (sync_slave_ctx.hb.slow)
                    {
                        if (sync_slave_ctx.hb.state != SYNC_SLAVE_HB_STATE_HB_RECEIVED)
                        {
                            sync_slave_hb_exit();
                        }
                        else
                        {
                            /* exit only when acked if tx a message */
                            sync_slave_ctx.hb.exit_pend = true;
                        }
                    }
                }

                /* re-trigger at last in case the state is changed */
                if (sync_slave_ctx.tx_trigger_pending)
                {
                    sync_slave_ctx.tx_trigger_pending = false;
                    sync_slave_handle_tx_trigger();
                }
            }
        }
    }

    if (reg_misr.kill_prx_int)
    {
        ppt_clear_radio();
        WR_PPT_REG(PRO_MISR, BIT5);
        ppt_flush_rx_fifo();
        ppt_ctx->fsm = PPT_FSM_STANDBY;
        ppt_ctx->sync_flag = false;
    }

    if (reg_misr.reset_trig_int)
    {
        ppt_clear_radio();
        WR_PPT_REG(PRO_MISR, BIT6);
        ppt_flush_rx_fifo();
        ppt_ctx->fsm = PPT_FSM_STANDBY;
        ppt_ctx->sync_flag = false;
    }
    time_debug_level_low(TIME_DEBUG_CHAN_ISR);
}

void sync_slave_init(void)
{
    sync_ppt_init();
    ppt_reg_handler(sync_slave_ppt_isr_handler);
    sync_hw_timer_init();
#if defined(SYNC_DEBUG) || SYNC_SUPPORT_MT1R
    sync_hw_frtimer_init();
#endif
    sync_ctx.role = SYNC_ROLE_SLAVE;
    memset(&sync_slave_ctx, 0, sizeof(sync_slave_ctx));
}

void sync_slave_check_feature(void)
{
    sync_check_feature();
    if (sync_ctx.rte_req_cb)
    {
        sync_ctx.rte_req_cb(true);
    }
}

void sync_slave_enable(void)
{

}

sync_err_code_t sync_slave_pair(void)
{
    if (sync_slave_ctx.state != SYNC_STATE_IDLE || sync_slave_ctx.pair_state != SYNC_PAIR_STATE_IDLE)
    {
        APP_PRINT_INFO2("sync: fail to start pair, state %d, pair state %d!", sync_slave_ctx.state,
                        sync_slave_ctx.pair_state);
        return SYNC_ERR_CODE_INVALID_STATE;
    }

    memset(&sync_slave_ctx, 0, sizeof(sync_slave_ctx));
    sync_slave_ctx.state = SYNC_STATE_SYNC_SLOW;
    sync_slave_ctx.pair_state = SYNC_PAIR_STATE_PAIRING;
    sync_slave_ctx.phase = SYNC_SLAVE_PAIR_PHASE_WAIT_REQ;
    sync_slave_ctx.seq_rx = 0xff;
    sync_slave_init_tx_fifo();
    sync_ppt_set_tifs(true);
    sync_slave_check_feature();
#if SYNC_SUPPORT_MT1R
    WR_PPT_REG_FIELD(PRO_FIELD_LENGTH, hp_field_length_3_0, SYNC_HEADER_LEN);
    WR_PPT_REG_FIELD(PRO_HS_LENGTH, hs_field_length_4_0, 0);
    sync_ppt_set_rf_time();
#endif
#ifdef SYNC_SLAVE_FIXED_CHANNEL
    uint8_t chan_idx = SYNC_SLAVE_FIXED_CHANNEL;
#else
#if SYNC_SLAVE_CCA_ENABLE
    int8_t chan_idx = sync_slave_cca_get(0, sync_ctx.chan_num);
#else
    uint8_t chan_idx = sync_rand() % sync_ctx.chan_num;
#endif
    sync_hw_timer_start(SYNC_SLAVE_HOPPING_SPEED, sync_slave_handle_hopping);
#endif

    APP_PRINT_INFO2("sync: start pair on %d[%d]!", chan_idx, sync_ctx.chan_cfg[chan_idx].freq);
    sync_channel_change(chan_idx);

    sync_slave_ctx.acc_data = sync_acc_gen_data();
    sync_acc_t acc = {.addr = SYNC_ACC_ADDR_BR};
    sync_acc_set(acc);

    sync_set_header(0, 0);
    sync_pdu_t pdu;
    pdu.opcode = SYNC_OPCODE_PAIR_RSP;
    pdu.pair_rsp.acc = sync_slave_ctx.acc_data;
    ppt_push_tx_data(sizeof(sync_opcode_t) + sizeof(sync_pair_rsp_t), (uint8_t *)&pdu);

    {
        ppt_prx_mode_ext_t param =
        {
            {
                .ack_mode = true,
                .continuous_mode = true
            }
        };
        ppt_set_prx_mode_ext(&param);
    }

    ppt_execute_instruction(PPT_HW_INSTRUCTION_PRX_ENABLE);
    return SYNC_ERR_CODE_SUCCESS;
}

sync_err_code_t sync_slave_connect(sync_bond_info_t *bond_info)
{
    if (bond_info == NULL)
    {
        return SYNC_ERR_CODE_NULL_POINTER;
    }

    if (sync_slave_ctx.state != SYNC_STATE_IDLE)
    {
        APP_PRINT_INFO1("sync: fail to start connect, state %d!", sync_slave_ctx.state);
        return SYNC_ERR_CODE_INVALID_STATE;
    }

    memset(&sync_slave_ctx, 0, sizeof(sync_slave_ctx));
    sync_slave_ctx.state = SYNC_STATE_SYNC_SLOW;
    sync_slave_ctx.pair_state = SYNC_PAIR_STATE_PAIRED;
    sync_slave_ctx.seq_rx = 0xff;
    sync_slave_init_tx_fifo();
    sync_ppt_set_tifs(true);
    sync_slave_check_feature();
#if SYNC_SUPPORT_MT1R
    WR_PPT_REG_FIELD(PRO_FIELD_LENGTH, hp_field_length_3_0, SYNC_HEADER_LEN);
    WR_PPT_REG_FIELD(PRO_HS_LENGTH, hs_field_length_4_0, 0);
    sync_ppt_set_rf_time();
#endif
#ifdef SYNC_SLAVE_FIXED_CHANNEL
    uint8_t chan_idx = SYNC_SLAVE_FIXED_CHANNEL;
#else
#if SYNC_SLAVE_CCA_ENABLE
    int8_t chan_idx = sync_slave_cca_get(0, sync_ctx.chan_num);
#else
    uint8_t chan_idx = sync_rand() % sync_ctx.chan_num;
#endif
    sync_hw_timer_start(SYNC_SLAVE_HOPPING_SPEED, sync_slave_handle_hopping);
#endif

    APP_PRINT_INFO3("sync: start connect 0x%08x on %d[%d]!", bond_info->acc.addr, chan_idx,
                    sync_ctx.chan_cfg[chan_idx].freq);
    sync_channel_change(chan_idx);

    sync_slave_ctx.acc_data = bond_info->acc;
    sync_acc_set(sync_slave_ctx.acc_data);

#if SYNC_SUPPORT_MT1R
    WR_PPT_REG_FIELD(PRO_FIELD_LENGTH, hp_field_length_3_0, SYNC_HEADER_LEN);
    WR_PPT_REG_FIELD(PRO_HS_LENGTH, hs_field_length_4_0, 0);
    sync_ppt_set_rf_time();
#endif

    sync_set_header(0, 0);
    sync_pdu_t pdu;
    pdu.opcode = SYNC_OPCODE_CONN_RSP;
    ppt_push_tx_data(sizeof(sync_opcode_t), (uint8_t *)&pdu);

    {
        ppt_prx_mode_ext_t param =
        {
            {
                .ack_mode = true,
                .continuous_mode = true
            }
        };
        ppt_set_prx_mode_ext(&param);
    }

    ppt_execute_instruction(PPT_HW_INSTRUCTION_PRX_ENABLE);
    return SYNC_ERR_CODE_SUCCESS;
}

void sync_slave_stop(void)
{
    if (sync_slave_ctx.state == SYNC_STATE_IDLE)
    {
        return;
    }

    uint32_t lock = sync_enter_critical();
    sync_hw_timer_stop();
#if SYNC_SUPPORT_MT1R
    sync_hw_timer1_stop();
    ppt_reg_handler(sync_slave_ppt_isr_handler);
#endif
    sync_slave_close(true);
    sync_slave_free_sending_msg();
#if SYNC_SUPPORT_POWER_CONTROL
    if (sync_ctx.tx_power_adaptive)
    {
        sync_power_ctrl_stop();
    }
#endif
    memset(&sync_slave_ctx, 0, sizeof(sync_slave_ctx));
    if (sync_ctx.rte_req_cb)
    {
        sync_ctx.rte_req_cb(false);
    }
    sync_exit_critical(lock);
    APP_PRINT_INFO0("sync: stopped!");
    return;
}

void sync_slave_deinit(void)
{

}

PPT_API_SECTION static bool sync_slave_dlps_check(uint32_t *wakeup_time_diff)
{
    if (sync_slave_ctx.state == SYNC_STATE_IDLE &&
        sync_slave_ctx.pair_state == SYNC_PAIR_STATE_IDLE)
    {
        return true;
    }

    return false;
}

void sync_slave_dlps_init(void)
{
    ppt_dlps_init();
    ppt_dlps_reg_mac_check_cb(sync_slave_dlps_check);
}
