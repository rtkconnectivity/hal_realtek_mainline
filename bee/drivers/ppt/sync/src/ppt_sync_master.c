/**
*****************************************************************************************
*     Copyright(c) 2020, Realtek Semiconductor Corporation. All rights reserved.
*****************************************************************************************
  * @file     ppt_sync_master.c
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
#include "ppt_sync_master.h"
#include "os_sync.h"
#include "os_timer.h"
//#include "trace.h"

LOG_MODULE_REGISTER(ppt_sync_master, PPT_SYNC_LOG_LEVEL);

#define SYNC_MASTER_PAIR_TIMES          (SYNC_PAIR_PERIOD/SYNC_PAIR_SPEED)
#define SYNC_MASTER_PAIR_CFM_TIMES      (SYNC_PAIR_CFM_PERIOD/SYNC_SYNC_SPEED)
#define SYNC_MASTER_SYNC_SLOW_TIMES     (SYNC_SYNC_SLOW_PERIOD/SYNC_SYNC_SLOW_SPEED)
#define SYNC_MASTER_SYNC_LOST_TIMES     (SYNC_SYNC_LOST_PERIOD/SYNC_SYNC_SPEED)
#define SYNC_MASTER_SYNC_FAST_TIMES     (SYNC_SYNC_FAST_PERIOD/SYNC_SYNC_FAST_SPEED)
#define SYNC_MASTER_HB_SLEEP_MIN_PERIOD 3000 // us
#define SYNC_MASTER_HB_SLEEP_MIN_PERIOD_MT1R 10000 // us
#define SYNC_MASTER_HB_SLEEP_OH         1500 // us

#define SYNC_MASTER_TIME_DEBUG_TX       -1
#define SYNC_MASTER_TIME_DEBUG_ACK      -2
#define SYNC_MASTER_TIME_DEBUG_NACK     -2
#define SYNC_MASTER_TIME_DEBUG_CB       -1
#define SYNC_MASTER_TIME_DEBUG_SEQ_ACK  -3

typedef enum
{
    SYNC_MASTER_PAIR_PHASE_SEND_REQ,
    SYNC_MASTER_PAIR_PHASE_SEND_CFM
} sync_master_pair_phase_t;

typedef enum
{
    SYNC_MASTER_HB_STATE_IDLE,
    SYNC_MASTER_HB_STATE_EMPTY_SEND,
    SYNC_MASTER_HB_STATE_HB_SEND
} sync_master_hb_state_t;

typedef struct
{
    uint32_t speed; // us
    uint32_t period; // counter
} sync_master_hb_rhy_t;

sync_master_hb_rhy_t sync_master_hb_rhy[] =
{
    {0, 20}, //SYNC_SYNC_SPEED
    {8000, 10},
    {30000, SYNC_MASTER_HB_RHY_PERIOD_MAX}
};
#define SYNC_MASTER_HB_RHY_NUMBER    (sizeof(sync_master_hb_rhy)/sizeof(sync_master_hb_rhy_t))

#define SYNC_MASTER_MSG_SENDING_NUM                   3

typedef struct
{
    sync_msg_t *pmsg;
    uint8_t tx_count;
    bool flush;
} sync_master_msg_sending_info_t;

typedef struct
{
    sync_master_msg_sending_info_t info[SYNC_MASTER_MSG_SENDING_NUM];
    volatile uint8_t head;
    volatile uint8_t tail;
} sync_master_msg_sending_list_t;

struct
{
    sync_state_t state;
    sync_pair_state_t pair_state;
    uint8_t phase;
    uint16_t count;
    uint16_t count1;
    sync_acc_t acc_data;
    bool ack;
    uint8_t seq_tx;
    uint8_t seq_rx;
    uint32_t crc_rx;
    struct
    {
        uint16_t interval;
        sync_master_hb_state_t state;
        bool slow;
        uint8_t rhy_idx;
        bool rhy_changing;
        uint8_t rhy_changing_idx;
        uint32_t rhy_counter;
        uint32_t time_left;
        uint32_t sleep_clk;
        uint8_t unack_count;
        uint8_t rhy_idx_old;
#if SYNC_SUPPORT_MT1R
        uint32_t time_left1;
        uint32_t speed;
        uint16_t ce_cnt;
        uint64_t sleep_time;
        bool sleep;
        bool sleep_mac;
        bool sleep_cal;
        bool oversleep;
#endif
    } hb;
    volatile bool busy; //!< tx or rx ongoing
    bool rx;
    uint8_t tx_ptr;
    uint8_t tx_ptr_hw;
    bool tx_null;
    bool tx_old;
    uint8_t tx_fail_count;
    volatile bool handling_ack;
    sync_master_msg_sending_list_t msg_sending_list;
    sync_msg_t *volatile pmsg_pend_free;
    bool tx_align;
    volatile uint32_t curr_time_in_ce; //!< us
    bool new_event; //!< use a flag to indicate new while the curr_time_in_ce is changing
#if SYNC_SUPPORT_MT1R
    struct
    {
        bool enable;
        uint16_t ce_cnt;
        uint8_t count;
        uint8_t tx_count;
        sync_msg_t *msg_txed[7];
        uint8_t msg_ack;
        bool skip;
        bool req_ack;
        uint8_t seq_ack;
        uint32_t ce_clk;
    } quick;
    uint8_t tx_fifo_bitmap;
#endif
} sync_master_ctx;

struct
{
    sync_master_tx_scheme_t tx_scheme;
    bool tx_align_enable;
    bool early_high_prio_enable;
    bool retrans_at_ack_enable;
    bool tx_new_at_retrans_int_enable;
    sync_tx_prepare_cb_t tx_prepare_cb;
} sync_master_param;

#define sync_master_last_tx_is_valid()          (sync_master_ctx.tx_null == false && sync_master_ctx.tx_old == false)
#define sync_master_tx_fifo_is_empty(entry)     (ppt_get_tx_fifo_rdptr(entry) == ppt_get_tx_fifo_wrptr(entry))

void sync_master_start_sync_timer(void);
static void sync_master_ppt_isr_handler(void);

static void sync_master_init_tx_fifo(void)
{
    ppt_set_tx_fifo_rdptr(0, PPT_TX_FIFO_SIZE - 1);
    ppt_force_fw_ack(0);
    sync_master_ctx.tx_ptr = ppt_flush_tx_fifo(0);
}

static void sync_master_close(void)
{
    ppt_reset_hw_fsm();
    sync_master_ctx.tx_ptr = ppt_force_fw_ack(0);
}

void sync_master_msg_flush(sync_msg_type_t type)
{
    uint8_t head = sync_master_ctx.msg_sending_list.head;
    uint8_t tail = sync_master_ctx.msg_sending_list.tail;
    while (head != tail)
    {
        sync_msg_t *pmsg = sync_master_ctx.msg_sending_list.info[head].pmsg;
        if (pmsg)
        {
            if (type == SYNC_MSG_TYPE_ALL || type == pmsg->type)
            {
                sync_master_ctx.msg_sending_list.info[head].flush = true;
            }
        }
        head = (head + 1) % SYNC_MASTER_MSG_SENDING_NUM;
    }
}

static void sync_master_msg_sending_free(void)
{
#if SYNC_SUPPORT_MT1R
    if (sync_ctx.mt1r.flag)
    {
        for (uint8_t loop = 0; loop < 7; loop++)
        {
            sync_msg_t *pmsg = sync_master_ctx.quick.msg_txed[loop];
            if (pmsg == NULL)
            {
                continue;
            }
            sync_master_ctx.quick.msg_txed[loop] = NULL;
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
        }
    }
#endif
    uint8_t head = sync_master_ctx.msg_sending_list.head;
    uint8_t tail = sync_master_ctx.msg_sending_list.tail;
    while (head != tail)
    {
        sync_msg_t *pmsg = sync_master_ctx.msg_sending_list.info[head].pmsg;
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
        }
        head = (head + 1) % SYNC_MASTER_MSG_SENDING_NUM;
    }
    sync_master_ctx.msg_sending_list.head = 0;
    sync_master_ctx.msg_sending_list.tail = 0;

    /* free the pend msg at the early handler */
    if (sync_master_ctx.pmsg_pend_free)
    {
        sync_pendcall(sync_msg_handle_tx_done, sync_master_ctx.pmsg_pend_free, 0);
        sync_master_ctx.pmsg_pend_free = NULL;
    }
}

PPT_API_SECTION bool sync_master_msg_sending_is_full(void)
{
    uint8_t head = sync_master_ctx.msg_sending_list.head;
    uint8_t tail = sync_master_ctx.msg_sending_list.tail;
    uint8_t tail_next = (tail + 1) % SYNC_MASTER_MSG_SENDING_NUM;
    return tail_next == head;
}

PPT_API_SECTION bool sync_master_msg_sending_is_empty(void)
{
    uint8_t head = sync_master_ctx.msg_sending_list.head;
    uint8_t tail = sync_master_ctx.msg_sending_list.tail;
    return head == tail;
}

PPT_API_SECTION uint8_t sync_master_msg_sending_get_num(void)
{
    uint8_t head = sync_master_ctx.msg_sending_list.head;
    uint8_t tail = sync_master_ctx.msg_sending_list.tail;
    return tail >= head ? tail - head : tail + SYNC_MASTER_MSG_SENDING_NUM - head;
}

PPT_API_SECTION uint8_t sync_master_msg_sending_get_data_num(void)
{
    uint8_t head = sync_master_ctx.msg_sending_list.head;
    uint8_t tail = sync_master_ctx.msg_sending_list.tail;
    uint8_t count = 0;
    while (head != tail)
    {
        if (sync_master_ctx.msg_sending_list.info[head].pmsg)
        {
            count += 1;
        }
        head = (head + 1) % SYNC_MASTER_MSG_SENDING_NUM;
    }

    return count;
}

PPT_API_SECTION bool sync_master_msg_sending_push(sync_msg_t *pmsg)
{
    uint8_t head = sync_master_ctx.msg_sending_list.head;
    uint8_t tail = sync_master_ctx.msg_sending_list.tail;
    uint8_t tail_next = (tail + 1) % SYNC_MASTER_MSG_SENDING_NUM;
    if (tail_next == head)
    {
        APP_PRINT_WARN0("sync: fail push, sending fifo is full");
        return false;
    }
    memset(&sync_master_ctx.msg_sending_list.info[tail], 0, sizeof(sync_master_msg_sending_info_t));
    sync_master_ctx.msg_sending_list.info[tail].pmsg = pmsg;
    sync_master_ctx.msg_sending_list.tail = tail_next;
    return true;
}

PPT_API_SECTION sync_master_msg_sending_info_t *sync_master_msg_sending_peak(void)
{
    uint8_t head = sync_master_ctx.msg_sending_list.head;
    uint8_t tail = sync_master_ctx.msg_sending_list.tail;
    if (head == tail)
    {
        return NULL;
    }

    return &sync_master_ctx.msg_sending_list.info[head];
}

PPT_API_SECTION sync_master_msg_sending_info_t *sync_master_msg_sending_pop(void)
{
    sync_master_msg_sending_info_t *ret = NULL;
    uint8_t head = sync_master_ctx.msg_sending_list.head;
    uint8_t tail = sync_master_ctx.msg_sending_list.tail;
    if (head == tail)
    {
        APP_PRINT_WARN0("sync: fail pop, sending fifo is empty");
        return ret;
    }
    ret = &sync_master_ctx.msg_sending_list.info[head];
    sync_master_ctx.msg_sending_list.head = (head + 1) % SYNC_MASTER_MSG_SENDING_NUM;
    return ret;
}

static void sync_master_handle_pair_to(void *para1, uint32_t para2)
{
    if (sync_ctx.event_cb)
    {
        sync_ctx.event_cb(SYNC_EVENT_PAIR_TIMEOUT);
    }
}

static void sync_master_handle_paired(void *para1, uint32_t para2)
{
    sync_bond_info_t bond_info;
    bond_info.acc = sync_master_ctx.acc_data;
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

static void sync_master_handle_connect_to(void *para1, uint32_t para2)
{
    if (sync_ctx.event_cb)
    {
        sync_ctx.event_cb(SYNC_EVENT_CONNECT_TIMEOUT);
    }
}

static void sync_master_handle_connected(void *para1, uint32_t para2)
{
    if (sync_ctx.event_cb)
    {
        sync_ctx.event_cb(SYNC_EVENT_CONNECTED);
    }
}

static void sync_master_handle_disconnected(void *para1, uint32_t para2)
{
    if (sync_ctx.event_cb)
    {
        sync_ctx.event_cb(SYNC_EVENT_CONNECT_LOST);
    }
}

#ifdef SYNC_DEBUG
static void sync_master_handle_sync_lost(void *para1, uint32_t para2)
{
    if (sync_ctx.event_cb)
    {
        sync_ctx.event_cb(SYNC_EVENT_SYNC_LOST);
    }
}

static void sync_master_handle_sync_reconnected(void *para1, uint32_t para2)
{
    if (sync_ctx.event_cb)
    {
        sync_ctx.event_cb(SYNC_EVENT_SYNC_RECONNECTED);
    }
}
#endif

static void sync_master_handle_exception(void)
{
    sync_master_stop();
    sync_pendcall(sync_master_handle_disconnected, 0, 0);
}

PPT_ISR_SECTION void sync_master_handle_hb_to(void)
{
    if (sync_master_hb_rhy[sync_master_ctx.hb.rhy_idx].period != SYNC_MASTER_HB_RHY_PERIOD_MAX
        && sync_master_ctx.hb.rhy_changing == false)
    {
        sync_master_ctx.hb.rhy_counter += 1;
        if (sync_master_ctx.hb.rhy_counter >= sync_master_hb_rhy[sync_master_ctx.hb.rhy_idx].period)
        {
            sync_master_ctx.hb.rhy_changing_idx = sync_master_ctx.hb.rhy_idx;
            sync_master_ctx.hb.rhy_idx += 1;
            sync_master_ctx.hb.rhy_changing = true;
            APP_PRINT_INFO1("sync: hb changing %dus", sync_master_hb_rhy[sync_master_ctx.hb.rhy_idx].speed);
        }
    }

    sync_set_header(true, sync_master_ctx.seq_tx);
    sync_pdu_t pdu;
    pdu.opcode = SYNC_OPCODE_HEARTBEAT;
    pdu.heartbeat.interval = sync_master_hb_rhy[sync_master_ctx.hb.rhy_idx].speed / 1000;
    ppt_push_tx_data(sizeof(sync_opcode_t) + sizeof(sync_heartbeat_t), (uint8_t *)&pdu);
    sync_master_msg_sending_push(NULL);
    sync_master_ctx.busy = true;
    ppt_execute_instruction(PPT_HW_INSTRUCTION_PTX_ENABLE);
}

PPT_ISR_SECTION void sync_master_hb_exit(bool real)
{
    if (sync_master_ctx.hb.slow)
    {
        sync_master_ctx.hb.slow = false;
        APP_PRINT_INFO0("sync: hb exit");
        if (sync_ctx.hb_cb)
        {
            sync_hb_cb_param_t param = {};
            param.enter = false;
            sync_ctx.hb_cb(&param);
        }
#if SYNC_SUPPORT_MT1R
        if (sync_master_ctx.quick.enable)
        {
            if (sync_ctx.rte_req_cb)
            {
                sync_ctx.rte_req_cb(true);
            }
        }
        else
#endif
        {
            sync_master_ctx.curr_time_in_ce = 0;
            sync_master_start_sync_timer();
            if (SYNC_SYNC_SPEED_HIGH <= SYNC_SYNC_SPEED_HIGH_MIN)
            {
                if (sync_ctx.rte_req_cb)
                {
                    sync_ctx.rte_req_cb(true);
                }
            }
        }
    }
    sync_master_ctx.hb.rhy_idx_old = real ? 0 : sync_master_ctx.hb.rhy_idx;
    sync_master_ctx.hb.state = SYNC_MASTER_HB_STATE_IDLE;
    sync_master_ctx.hb.rhy_changing = false;
    sync_master_ctx.hb.rhy_idx = 0;
    sync_master_ctx.hb.rhy_counter = 0;
    sync_master_ctx.hb.unack_count = 0;
}
#include "rtl_pinmux.h"
PPT_ISR_SECTION void sync_master_push_data(void)
{
    uint32_t lock = sync_enter_critical();
    time_debug_level_high(TIME_DEBUG_CHAN_THREAD);
    time_debug_level_low(TIME_DEBUG_CHAN_THREAD);
    if (sync_master_msg_sending_is_full())
    {
        sync_exit_critical(lock);
        return;
    }
    sync_msg_t *pmsg = NULL;
    if (sync_ctx.msg_send.count == 0
#if SYNC_SUPPORT_MT1R
        || sync_master_ctx.quick.req_ack
#endif
       )
    {
        sync_master_ctx.hb.state = SYNC_MASTER_HB_STATE_EMPTY_SEND;
        sync_pdu_t pdu;
        pdu.opcode = SYNC_OPCODE_EMPTY;
        ppt_push_tx_data(sizeof(sync_opcode_t), (uint8_t *)&pdu);
    }
    else
    {
        /* clear hb state & counter in case of tx empty ahead of this */
        sync_master_ctx.hb.state = SYNC_MASTER_HB_STATE_IDLE;
        sync_master_ctx.hb.rhy_counter = 0;
        pmsg = (sync_msg_t *)plt_list_pop(&sync_ctx.msg_send);
        Pad_Config(P3_0, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_DOWN, PAD_OUT_ENABLE,
                   PAD_OUT_LOW);
        Pad_Config(P3_0, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_DOWN, PAD_OUT_ENABLE,
                   PAD_OUT_HIGH);
        ppt_push_tx_fifo(0, pmsg->data, pmsg->len);
        Pad_Config(P3_0, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_DOWN, PAD_OUT_ENABLE,
                   PAD_OUT_LOW);
    }
    sync_master_msg_sending_push(pmsg);

    /* update at last in case of tx fail */
#if SYNC_SUPPORT_MT1R
    if (sync_master_ctx.quick.enable)
    {
        sync_master_set_header(sync_master_ctx.quick.seq_ack, sync_master_ctx.seq_tx,
                               sync_master_ctx.quick.req_ack);
    }
    else
#endif
    {
        sync_set_header(sync_master_ctx.rx, sync_master_ctx.seq_tx);
    }
    time_debug_level_high(TIME_DEBUG_CHAN_THREAD);
    time_debug_level_low(TIME_DEBUG_CHAN_THREAD);
    sync_exit_critical(lock);
}

PPT_API_SECTION static void sync_master_tx_restart(void)
{
    sync_master_push_data();
    sync_master_ctx.busy = true;
    ppt_execute_instruction(PPT_HW_INSTRUCTION_PTX_ENABLE);
    sync_master_ctx.curr_time_in_ce = 0;
    sync_master_start_sync_timer();
}

PPT_ISR_SECTION void sync_master_handle_tx_trigger(void)
{
    if (sync_master_ctx.pair_state != SYNC_PAIR_STATE_PAIRED
        || sync_master_ctx.state != SYNC_STATE_SYNC)
    {
        return;
    }

#if SYNC_SUPPORT_MT1R
    if (sync_master_ctx.quick.enable)
    {
        if (sync_master_ctx.busy == false)
        {
            if (sync_master_ctx.hb.slow)
            {
                void sync_master_hb_sleep_resume(void);
                sync_master_hb_sleep_resume();
                sync_master_push_data();
                sync_master_hb_exit(true);
            }
        }
    }
    else
#endif
    {
        if (sync_master_ctx.busy == false)
        {
            if (sync_master_ctx.hb.slow)
            {
                /* quit hb to send msg */
                sync_master_push_data();
                sync_master_ctx.busy = true;
                ppt_execute_instruction(PPT_HW_INSTRUCTION_PTX_ENABLE);
                sync_master_hb_exit(true);
                APP_PRINT_INFO0("sync: hb exit by tx trigger");
                return;
            }

            if (sync_master_param.tx_align_enable)
            {
                /* in case of retrans ongoing */
                if (sync_master_msg_sending_get_num() == 0)
                {
                    sync_master_tx_restart();
                }
            }
        }
        else
        {
            if (sync_master_param.tx_align_enable)
            {
                if (sync_master_msg_sending_get_data_num() == 0)
                {
                    sync_master_ctx.tx_align = true;
                }
            }
        }
    }
}

PPT_ISR_SECTION void sync_master_handle_ack(bool rx, sync_header_t rx_header)
{
    bool enter_critical = false;
    uint32_t lock = 0;
    /* update ack field first in case tx fail next time */
    sync_set_header(rx, sync_master_ctx.seq_tx);
    /* rx ack handling */
    sync_master_ctx.rx = rx;

    if (sync_master_ctx.tx_null)
    {
        /* empty msg send since no data pushed timely */
        uint8_t seg_vld = RD_U32_BZDMA_REG(BZDMA_REG_BLEONLY_ENTRY_SEGMENT_VALID(0));
        if (sync_master_ctx.tx_fail_count >= 3)//seg_vld == 0 ||
        {
            APP_PRINT_WARN2("sync: fail, tx null ptr %d data 0x%02x, stop here",
                            sync_master_ctx.tx_ptr_hw, seg_vld);
            sync_master_handle_exception();
            return;
        }
        else
        {
            /* the null pkt may exhaust one seq, so use new to make sure of normal pkt being handled */
            sync_master_ctx.seq_tx += 1;
            sync_set_header(rx, sync_master_ctx.seq_tx);
        }
    }
    else if (sync_master_ctx.tx_old)
    {
        /* this msg should have already been handled before, so no need handle again */
        uint8_t seg_vld = RD_U32_BZDMA_REG(BZDMA_REG_BLEONLY_ENTRY_SEGMENT_VALID(0));
        /* next pkt may be late */
        //if(seg_vld | (~(1 << sync_master_ctx.tx_ptr_hw)))
        /* tx_ptr_hw maybe wrong */
        //if(seg_vld & (1 << sync_master_ctx.tx_ptr_hw))
        if (sync_master_ctx.tx_fail_count >= 3)
        {
            APP_PRINT_WARN2("sync: fail, tx %d old due to valid data 0x%02x, stop here",
                            sync_master_ctx.tx_ptr_hw, seg_vld);
            sync_master_handle_exception();
            return;
        }
    }
    else
    {
        bool retrans = false;
        sync_master_msg_sending_info_t *pmsg_info = sync_master_msg_sending_peak();
        sync_msg_t *pmsg = pmsg_info ? pmsg_info->pmsg : NULL;
        if (pmsg_info && pmsg_info->flush)
        {

        }
        else if (pmsg && (rx == false || rx_header.ack == false))
        {
            switch (pmsg->type)
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
            /* header already updated above */
        }
        else
        {
            sync_master_ctx.tx_ptr = ppt_trigger_fw_ack(0);
            sync_master_ctx.seq_tx += 1;
            sync_master_msg_sending_pop();
            if (pmsg)
            {
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
        }
    }

    if (sync_master_param.retrans_at_ack_enable)
    {
        if (sync_master_tx_fifo_is_empty(0) == false)
        {
            if (enter_critical == false)
            {
                lock = sync_enter_critical();
                enter_critical = true;
                time_debug_level_low(TIME_DEBUG_CHAN_ISR);
                time_debug_level_high(TIME_DEBUG_CHAN_ISR);
            }
            if (sync_master_ctx.busy == false)
            {
                /* re-trigger right now */
                sync_master_ctx.busy = true;
                ppt_execute_instruction(PPT_HW_INSTRUCTION_PTX_ENABLE);
                /* mark as retransmit tx */
                sync_master_ctx.curr_time_in_ce += 1;
                sync_master_ctx.new_event = false;
            }
        }
    }

    if (sync_master_param.early_high_prio_enable)
    {
        if (sync_master_tx_fifo_is_empty(0))
        {
            if (enter_critical == false)
            {
                lock = sync_enter_critical();
                enter_critical = true;
                time_debug_level_low(TIME_DEBUG_CHAN_ISR);
                time_debug_level_high(TIME_DEBUG_CHAN_ISR);
            }

            if (sync_master_ctx.busy)
            {
                if (sync_master_ctx.curr_time_in_ce != 0)
                {
                    if (sync_master_param.tx_new_at_retrans_int_enable && sync_ctx.msg_send.count != 0)
                    {
                        sync_master_push_data();
                    }
                    else
                    {
                        /* cancel before rx handler */
                        ppt_execute_instruction(PPT_HW_INSTRUCTION_PTX_DISABLE);
                        sync_master_ctx.busy = false;
                    }
                }
                else
                {
                    sync_master_push_data();
                }
            }
        }
    }

    if (sync_master_param.tx_align_enable && sync_master_ctx.tx_align)
    {
        sync_master_ctx.tx_align = false;
        if (sync_master_tx_fifo_is_empty(0))
        {
            if (enter_critical == false)
            {
                lock = sync_enter_critical();
                enter_critical = true;
                time_debug_level_low(TIME_DEBUG_CHAN_ISR);
                time_debug_level_high(TIME_DEBUG_CHAN_ISR);
            }
            if (sync_master_ctx.busy == false)
            {
                sync_master_tx_restart();
            }
        }
    }

    /* update header in case tx at early */
    sync_set_header(rx, sync_master_ctx.seq_tx);
    time_debug_level_low(TIME_DEBUG_CHAN_ISR);
    time_debug_level_high(TIME_DEBUG_CHAN_ISR);
    sync_master_ctx.handling_ack = false;
    if (enter_critical)
    {
        sync_exit_critical(lock);
    }
#if SYNC_SUPPORT_POWER_CONTROL
    if (sync_ctx.tx_power_adaptive)
    {
        sync_ctx.power_ctrl_count_fail += !(rx && rx_header.ack);
    }
#endif
}

PPT_ISR_SECTION void sync_master_handle_rx(bool rx, sync_header_t rx_header, sync_pdu_t *rx_pdu,
                                           uint16_t rx_len, uint16_t rssi, uint32_t rx_crc)
{
    uint32_t lock = sync_enter_critical();
    time_debug_level_low(TIME_DEBUG_CHAN_ISR);
    if (sync_master_tx_fifo_is_empty(0))
    {
        /* hb handling */
        bool rx_empty = false;
        bool rx_ack = false;
        if (rx)
        {
            if (rx_len == sizeof(sync_opcode_t) && rx_pdu->opcode == SYNC_OPCODE_EMPTY)
            {
                rx_empty = true;
            }
            if (rx_header.ack)
            {
                rx_ack = true;
            }
        }
        //APP_PRINT_INFO4("sync: hb rhy idx %d counter %d, state %d, rx emtpy %d",
        //                sync_master_ctx.hb.rhy_idx,
        //                sync_master_ctx.hb.rhy_counter, sync_master_ctx.hb.state, rx_empty);
        if (SYNC_MASTER_HB_RHY_NUMBER <= 1 || sync_master_ctx.hb.state == SYNC_MASTER_HB_STATE_IDLE)
        {

        }
        else if (sync_master_ctx.hb.state == SYNC_MASTER_HB_STATE_EMPTY_SEND)
        {
            bool tx_hb = false;
            /* tx pend message */
            if (sync_ctx.msg_send.count != 0)
            {
                sync_master_hb_exit(true);
            }
            else if (rx_empty && rx_ack)
            {
                if (sync_master_hb_rhy[0].period != SYNC_MASTER_HB_RHY_PERIOD_MAX)
                {
                    if (sync_master_ctx.hb.rhy_idx_old != 0)
                    {
                        tx_hb = true;
                        sync_master_ctx.hb.rhy_idx = sync_master_ctx.hb.rhy_idx_old;
                        sync_master_ctx.hb.rhy_changing = true;
                        sync_master_ctx.hb.rhy_changing_idx = 0;
                    }
                    else
                    {
                        sync_master_ctx.hb.rhy_counter += 1;
                        if (sync_master_ctx.hb.rhy_counter >= sync_master_hb_rhy[0].period)
                        {
                            tx_hb = true;
                            sync_master_ctx.hb.rhy_idx = 1;
                            sync_master_ctx.hb.rhy_changing = true;
                            sync_master_ctx.hb.rhy_changing_idx = 0;
                        }
                    }
                }
            }
            else
            {
                /* ignore if be disturbed */
                if (sync_master_ctx.rx)
                {
                    sync_master_ctx.hb.rhy_counter = 0;
                }
            }
            if (sync_master_ctx.hb.rhy_changing)
            {
                uint32_t speed = sync_master_ctx.hb.rhy_changing_idx == 0 ? SYNC_SYNC_SPEED :
                                 sync_master_hb_rhy[sync_master_ctx.hb.rhy_changing_idx].speed;
                sync_hw_timer_start(speed, sync_master_handle_hb_to);
                if (sync_master_param.early_high_prio_enable)
                {
                    /* cancel if already triggered */
                    if (sync_master_ctx.busy)
                    {
                        ppt_kill_hw_fsm(PPT_FSM_PTX);
                        sync_master_ctx.busy = false;
                    }
                }
                sync_master_ctx.hb.state = SYNC_MASTER_HB_STATE_HB_SEND;
                sync_master_ctx.hb.slow = true;
                APP_PRINT_INFO1("sync: hb changing %dus", sync_master_hb_rhy[sync_master_ctx.hb.rhy_idx].speed);
            }
        }
        else if (sync_master_ctx.hb.state == SYNC_MASTER_HB_STATE_HB_SEND)
        {
            bool hb_enter = false;
            bool hb_continue = false;
            /* tx pend message */
            if (sync_ctx.msg_send.count != 0)
            {
                sync_master_hb_exit(true);
            }
            /* rx empty ack message */
            else if (rx_ack && rx_empty)
            {
                sync_master_ctx.hb.unack_count = 0;
                hb_enter = true;
            }
            else
            {
                /* rx non-empty message */
                if (sync_master_ctx.rx && rx_empty == false)
                {
                    sync_master_hb_exit(true);
                }
                /* no rx or rx non-ack */
                else if (rx_ack == false)
                {
                    /* skip wakeup only when already in the hb state */
                    if (sync_master_ctx.hb.slow)
                    {
                        sync_master_ctx.hb.unack_count += 1;
                        if (sync_master_ctx.hb.unack_count >= SYNC_HB_TO_TIMES)
                        {
                            sync_master_hb_exit(false);
                        }
                        else
                        {
                            hb_continue = true;
                            sync_master_ctx.hb.rhy_counter -= 1;
                        }
                    }
                }
            }

            if (hb_enter || hb_continue)
            {
                if (hb_enter && sync_master_ctx.hb.rhy_changing)
                {
                    sync_master_ctx.hb.rhy_changing = false;
                    sync_master_ctx.hb.rhy_counter = 0;
                    APP_PRINT_INFO1("sync: hb enter %dus",
                                    sync_master_hb_rhy[sync_master_ctx.hb.rhy_idx].speed);
                    if (sync_ctx.hb_cb)
                    {
                        sync_hb_cb_param_t param = {};
                        param.enter = true;
                        param.interval = sync_master_hb_rhy[sync_master_ctx.hb.rhy_idx].speed;
                        sync_ctx.hb_cb(&param);
                    }
                }
                uint32_t speed = 0;
                if (sync_master_ctx.hb.rhy_changing)
                {
                    speed = sync_master_ctx.hb.rhy_changing_idx == 0 ? SYNC_SYNC_SPEED :
                            sync_master_hb_rhy[sync_master_ctx.hb.rhy_changing_idx].speed;
                }
                else
                {
                    speed = sync_master_hb_rhy[sync_master_ctx.hb.rhy_idx].speed;
                }
                sync_hw_timer_start(speed, sync_master_handle_hb_to);
                sync_master_ctx.hb.slow = true;
                if (speed > SYNC_SYNC_SPEED)
                {
                    if (sync_ctx.rte_req_cb)
                    {
                        sync_ctx.rte_req_cb(false);
                    }
                }
            }
        }
    }
    time_debug_level_high(TIME_DEBUG_CHAN_ISR);
    sync_exit_critical(lock);

    if (rx)
    {
        if (rx_header.seq != sync_master_ctx.seq_rx)// || rx_crc != sync_master_ctx.crc_rx
        {
            sync_master_ctx.seq_rx = rx_header.seq;
            sync_master_ctx.crc_rx = rx_crc;
            if (rx_len > 0)
            {
                if (rx_pdu->opcode >= SYNC_OPCODE_APP_START)
                {
                    if (sync_ctx.msg_receive_cb)
                    {
                        sync_receive_info_t info = {};
                        info.rssi = ppt_get_rssi(rssi);
                        sync_ctx.msg_receive_cb((uint8_t *)rx_pdu, rx_len, &info);
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

PPT_ISR_SECTION void sync_master_handle_sync_fast_early(void)
{
    sync_hw_timer_start(SYNC_SYNC_FAST_SPEED, sync_master_handle_sync_fast_early);
    if (sync_master_ctx.busy)
    {
        return;
    }
    sync_master_ctx.busy = true;
    ppt_execute_instruction(PPT_HW_INSTRUCTION_PTX_ENABLE);
}

PPT_ISR_SECTION void sync_master_handle_sync_slow_early(void)
{
    uint32_t speed = sync_master_ctx.pair_state == SYNC_PAIR_STATE_PAIRED ? SYNC_SYNC_SLOW_SPEED :
                     sync_master_ctx.phase == SYNC_MASTER_PAIR_PHASE_SEND_CFM ? SYNC_SYNC_SPEED : SYNC_PAIR_SPEED;
    sync_hw_timer_start(speed, sync_master_handle_sync_slow_early);
    if (sync_master_ctx.busy)
    {
        return;
    }
    sync_master_ctx.busy = true;
    ppt_execute_instruction(PPT_HW_INSTRUCTION_PTX_ENABLE);
}

PPT_ISR_SECTION void sync_master_handle_sync_early(void)
{
    sync_master_ctx.curr_time_in_ce += SYNC_SYNC_SPEED_HIGH;
    if (sync_master_ctx.busy)
    {
        APP_PRINT_WARN1("sync: busy at %d", sync_master_ctx.curr_time_in_ce);
        return;
    }

    if (sync_master_ctx.curr_time_in_ce >= SYNC_SYNC_SPEED)
    {
        sync_master_ctx.curr_time_in_ce = 0;
        time_debug_level_low(TIME_DEBUG_CHAN_TIMING);
        time_debug_level_high(TIME_DEBUG_CHAN_TIMING);
    }

    /* tx if necessary */
    sync_master_msg_sending_info_t *pmsg_info = sync_master_msg_sending_peak();
    bool tx_new = sync_master_ctx.curr_time_in_ce == 0
                  || (sync_master_param.tx_new_at_retrans_int_enable && sync_ctx.msg_send.count != 0);
    if (tx_new || (false == sync_master_tx_fifo_is_empty(0) && pmsg_info && pmsg_info->pmsg &&
                   pmsg_info->tx_count))
    {
        sync_master_ctx.busy = true;
        ppt_execute_instruction(PPT_HW_INSTRUCTION_PTX_ENABLE);
        sync_master_ctx.new_event = sync_master_ctx.curr_time_in_ce == 0;
        time_debug_level_low(TIME_DEBUG_CHAN_TIMING);

        if (sync_master_ctx.curr_time_in_ce == 0)
        {
            if (sync_master_param.early_high_prio_enable == false)
            {
                if (sync_master_param.tx_prepare_cb != NULL)
                {
                    time_debug_level_high(TIME_DEBUG_CHAN_TIMING);
                    /* in case of calling tx trigger */
                    sync_master_param.tx_prepare_cb();
                    time_debug_level_low(TIME_DEBUG_CHAN_TIMING);
                }
            }
        }

        if (tx_new)
        {
            if (sync_master_ctx.handling_ack == false)
            {
                /* quit dynamic msg if necessary */
                if (pmsg_info)
                {
                    time_debug_level_high(TIME_DEBUG_CHAN_TIMING);
                    time_debug_level_low(TIME_DEBUG_CHAN_TIMING);
                    sync_msg_t *pmsg = pmsg_info->pmsg;
                    if (pmsg && pmsg->type == SYNC_MSG_TYPE_DYNAMIC_RETRANS
                        && sync_ctx.msg_retrans_count > sync_ctx.msg_dynamic_retrans_num_lower
                        && sync_ctx.msg_send.count != 0)
                    {
                        sync_master_ctx.tx_ptr = ppt_trigger_fw_ack(0);
                        sync_master_ctx.seq_tx += 1;
                        pmsg->send_info.retrans_count = sync_ctx.msg_retrans_count;
                        sync_master_msg_sending_pop();
                        sync_master_push_data();
                        if (pmsg->send_cb)
                        {
                            pmsg->send_info.res = SYNC_SEND_RESULT_UNKNOWN;
                            /* the early handler can't call os api when at high priority */
                            if (sync_master_param.early_high_prio_enable)
                            {
                                if (sync_master_ctx.pmsg_pend_free)
                                {
                                    APP_PRINT_ERROR0("sync: pmsg_pend_free error");
                                    while (1);
                                }
                                sync_master_ctx.pmsg_pend_free = pmsg;
                            }
                            else
                            {
                                sync_pendcall(sync_msg_handle_tx_done, pmsg, 0);
                            }
                        }
                        else
                        {
                            sync_ctx.msg_count[pmsg->type] -= 1;
                            plt_list_push(&sync_ctx.msg_free, pmsg);
                        }
                    }
                }
                else
                {
                    if (sync_master_msg_sending_get_num() <= 1)
                    {
                        sync_master_push_data();
                    }
                }
            }
        }

        time_debug_level_high(TIME_DEBUG_CHAN_TIMING);
        //APP_PRINT_TRACE1("sync: early curr_time_in_ce %d", sync_master_ctx.curr_time_in_ce);
    }
}

PPT_ISR_SECTION void sync_master_handle_sync_fast_to(void)
{
    sync_master_ctx.count += 1;
    if (
#if SYNC_SUPPORT_SYNC_FAST_DISABLE
        SYNC_SYNC_FAST_PERIOD == SYNC_SYNC_FAST_PERIOD_DISABLE ||
#endif
        sync_master_ctx.count >= SYNC_MASTER_SYNC_FAST_TIMES)
    {
        sync_hw_timer_stop();
        sync_master_close();
        sync_master_msg_sending_free();
#if SYNC_SUPPORT_POWER_CONTROL
        if (sync_ctx.tx_power_adaptive)
        {
            sync_power_ctrl_stop();
        }
#endif
#if SYNC_SUPPORT_MT1R
        ppt_reg_handler(sync_master_ppt_isr_handler);
#endif
        sync_master_ctx.pair_state = SYNC_PAIR_STATE_IDLE;
        sync_master_ctx.state = SYNC_STATE_IDLE;
        APP_PRINT_INFO0("sync: disconnected!");
        sync_channel_print_statistic();
        sync_pendcall(sync_master_handle_disconnected, 0, 0);
        if (sync_ctx.rte_req_cb)
        {
            sync_ctx.rte_req_cb(false);
        }
    }
    else
    {
        sync_channel_change_next_in_group();
        if (sync_master_msg_sending_is_empty())
        {
            sync_master_push_data();
        }
    }
}

PPT_ISR_SECTION void sync_master_handle_sync_lost_to(void)
{
    sync_header_t rx_header = {};
    sync_master_handle_rx(false, rx_header, NULL, 0, 0, 0);

    sync_ctx.chan_miss_count += 1;
    if (sync_master_ctx.curr_time_in_ce == 0 || sync_master_ctx.new_event)
    {
        sync_master_ctx.count += 1;
    }
    if (sync_master_ctx.count >= SYNC_MASTER_SYNC_LOST_TIMES)
    {
        sync_hw_timer_stop();
        if (sync_master_param.early_high_prio_enable)
        {
            if (sync_master_ctx.busy)
            {
                ppt_kill_hw_fsm(PPT_FSM_PTX);
                sync_master_ctx.busy = false;
            }
        }
        APP_PRINT_INFO4("sync: lost at %d[%d], miss %d/%d!", sync_ctx.chan_cur,
                        sync_ctx.chan_cfg[sync_ctx.chan_cur].freq,
                        sync_ctx.chan_miss_count, sync_ctx.chan_used_count);
#ifdef SYNC_DEBUG
        sync_pendcall(sync_master_handle_sync_lost, 0, 0);
#endif
#if SYNC_SUPPORT_SYNC_FAST_DISABLE
        if (SYNC_SYNC_FAST_PERIOD == SYNC_SYNC_FAST_PERIOD_DISABLE)
        {
            sync_master_handle_sync_fast_to();
        }
        else
#endif
        {
            sync_master_ctx.state = SYNC_STATE_SYNC_FAST;
            sync_master_ctx.count = 0;
            sync_channel_change_next_in_group();
            if (sync_master_msg_sending_is_empty())
            {
                sync_master_push_data();
            }
            sync_master_ctx.busy = true;
            ppt_execute_instruction(PPT_HW_INSTRUCTION_PTX_ENABLE);
            sync_hw_timer_start(SYNC_SYNC_FAST_SPEED, sync_master_handle_sync_fast_early);
        }
    }
}

PPT_ISR_SECTION void sync_master_handle_sync_slow_to(void)
{
    sync_master_ctx.count += 1;
    if (sync_master_ctx.count < SYNC_MASTER_SYNC_SLOW_TIMES)
    {
        sync_channel_change_next();
    }
    else
    {
        sync_hw_timer_stop();
        sync_master_close();
        sync_master_ctx.pair_state = SYNC_PAIR_STATE_IDLE;
        sync_master_ctx.state = SYNC_STATE_IDLE;
        APP_PRINT_INFO0("sync: connect failed!");
        sync_pendcall(sync_master_handle_connect_to, 0, 0);
    }
}

PPT_ISR_SECTION void sync_master_handle_pair_req_to(void)
{
    sync_master_ctx.count += 1;
    if (sync_master_ctx.count < SYNC_MASTER_PAIR_TIMES)
    {
        sync_channel_change_next();
    }
    else
    {
        sync_hw_timer_stop();
        sync_master_close();
        sync_master_ctx.pair_state = SYNC_PAIR_STATE_IDLE;
        sync_master_ctx.state = SYNC_STATE_IDLE;
        APP_PRINT_INFO0("sync: pair failed!");
        sync_pendcall(sync_master_handle_pair_to, 0, 0);
    }
}

PPT_ISR_SECTION void sync_master_handle_pair_cfm_to(void)
{
    sync_master_ctx.count1 += 1;
    if (sync_master_ctx.count1 >= SYNC_MASTER_PAIR_CFM_TIMES)
    {
        sync_master_ctx.phase = SYNC_MASTER_PAIR_PHASE_SEND_REQ;
        APP_PRINT_ERROR0("sync: back to pair!");

        sync_master_ctx.tx_ptr = ppt_trigger_fw_ack(0);
        sync_ppt_set_tifs(true);
        sync_hw_timer_start(SYNC_PAIR_SPEED, sync_master_handle_sync_slow_early);
        sync_acc_t acc = {.addr = SYNC_ACC_ADDR_BR};
        sync_acc_set(acc);

        uint16_t interval = SYNC_SYNC_SPEED;
#if SYNC_SUPPORT_MT1R
        if (sync_ctx.mt1r.flag)
        {
            interval = 125;
            sync_ctx.conn_interval = 250;
        }
#endif
        sync_set_header(0, 0);
        sync_pdu_t pdu;
        pdu.opcode = SYNC_OPCODE_PAIR_REQ;
        pdu.pair_req.interval = interval;
        ppt_push_tx_data(sizeof(sync_opcode_t) + sizeof(sync_pair_req_t), (uint8_t *)&pdu);

        sync_master_handle_pair_req_to();
    }
}

PPT_API_SECTION void sync_master_start_sync_timer(void)
{
    sync_hw_timer_start(SYNC_SYNC_SPEED_HIGH, sync_master_handle_sync_early);
    sync_hw_timer_set_periodic(true);
    if (sync_master_param.early_high_prio_enable)
    {
        sync_hw_timer_impr_ip(true);
    }
}

#if SYNC_SUPPORT_MT1R
PPT_ISR_SECTION void sync_master_pop_data(void)
{
    sync_master_ctx.tx_ptr = ppt_trigger_fw_ack(0);
    sync_master_ctx.seq_tx += 1;
    sync_master_msg_sending_info_t *pmsg_info = sync_master_msg_sending_pop();
    sync_msg_t *pmsg = pmsg_info ? pmsg_info->pmsg : NULL;
    if (pmsg)
    {
        if (sync_master_ctx.quick.msg_txed[sync_master_ctx.quick.tx_count] != NULL)
        {
            APP_PRINT_ERROR2("sync: fail to free msg, slot %d, ptr 0x%0x", sync_master_ctx.quick.tx_count,
                             sync_master_ctx.quick.msg_txed[sync_master_ctx.quick.tx_count]);
            sync_pendcall(sync_msg_handle_tx_done,
                              sync_master_ctx.quick.msg_txed[sync_master_ctx.quick.tx_count], 0);
        }
        sync_master_ctx.quick.msg_txed[sync_master_ctx.quick.tx_count] = pmsg;
    }
}

PPT_ISR_SECTION void sync_master_handle_rx_quick(bool rx, sync_header_t rx_header,
                                                 sync_pdu_t *rx_pdu,
                                                 uint16_t rx_len, uint16_t rssi, uint32_t rx_crc)
{
    if (rx && rx_header.s.seq == sync_master_ctx.seq_rx)
    {
        if (SYNC_ISR_DEBUG)
        {
            APP_PRINT_WARN0("sync_isr: ptx rx old pkt");
        }
        rx = false;
    }

    sync_master_ctx.quick.req_ack = rx == false;
    sync_master_ctx.quick.seq_ack += rx;
    sync_master_ctx.rx = rx;
#if TIME_DEBUG
    if (sync_master_ctx.quick.seq_ack & 0x1)
    {
        time_debug_level_high(SYNC_MASTER_TIME_DEBUG_SEQ_ACK);
    }
    else
    {
        time_debug_level_low(SYNC_MASTER_TIME_DEBUG_SEQ_ACK);
    }
#endif
    sync_master_set_header(sync_master_ctx.quick.seq_ack, sync_master_ctx.seq_tx,
                           sync_master_ctx.quick.req_ack);
    WR_PPT_REG_FIELD(PRO_TRX_CONTROL, ack_enable, sync_master_ctx.quick.req_ack);
    if (sync_master_ctx.quick.req_ack)
    {
        time_debug_level_high(TIME_DEBUG_CHAN_THREAD);
        time_debug_level_low(TIME_DEBUG_CHAN_THREAD);
    }

    sync_master_pop_data();

    /* hb handling */
    if (sync_master_ctx.quick.tx_count == 6)
    {
        bool rx_empty = false;
        bool rx_ack = false;
        if (rx)
        {
            if (rx_len == sizeof(sync_opcode_t) && rx_pdu->opcode == SYNC_OPCODE_EMPTY)
            {
                rx_empty = true;
            }
            if (rx_header.ack)
            {
                rx_ack = true;
            }
        }
        //APP_PRINT_INFO4("sync: hb rhy idx %d counter %d, state %d, rx emtpy %d",
        //                sync_master_ctx.hb.rhy_idx,
        //                sync_master_ctx.hb.rhy_counter, sync_master_ctx.hb.state, rx_empty);
        if (SYNC_MASTER_HB_RHY_NUMBER <= 1 || sync_master_ctx.hb.state == SYNC_MASTER_HB_STATE_IDLE)
        {

        }
        else if (sync_master_ctx.hb.state == SYNC_MASTER_HB_STATE_EMPTY_SEND)
        {
            bool tx_hb = false;
            /* tx pend message */
            if (sync_ctx.msg_send.count != 0)
            {
                sync_master_ctx.hb.rhy_counter = 0;
            }
            else if (rx_empty && rx_ack)
            {
                if (sync_master_hb_rhy[0].period != SYNC_MASTER_HB_RHY_PERIOD_MAX)
                {
                    if (sync_master_ctx.hb.rhy_idx_old != 0)
                    {
                        tx_hb = true;
                        sync_master_ctx.hb.rhy_idx = sync_master_ctx.hb.rhy_idx_old;
                        sync_master_ctx.hb.rhy_changing = true;
                        sync_master_ctx.hb.rhy_changing_idx = 0;
                    }
                    else
                    {
                        sync_master_ctx.hb.rhy_counter += 1;
                        if (sync_master_ctx.hb.rhy_counter >= sync_master_hb_rhy[0].period)
                        {
                            tx_hb = true;
                            sync_master_ctx.hb.rhy_idx = 1;
                            sync_master_ctx.hb.rhy_changing = true;
                            sync_master_ctx.hb.rhy_changing_idx = 0;
                        }
                    }
                }
            }
            else
            {
                /* ignore if be disturbed */
                if (sync_master_ctx.rx)
                {
                    sync_master_ctx.hb.rhy_counter = 0;
                }
            }
            if (sync_master_ctx.hb.rhy_changing)
            {
                sync_master_ctx.hb.speed = sync_master_ctx.hb.rhy_changing_idx == 0 ? SYNC_SYNC_SPEED :
                                           sync_master_hb_rhy[sync_master_ctx.hb.rhy_changing_idx].speed;
                sync_master_ctx.hb.ce_cnt = sync_master_ctx.quick.ce_cnt + sync_master_ctx.hb.speed /
                                            SYNC_SYNC_SPEED;
                if (sync_master_param.early_high_prio_enable)
                {
                    /* cancel if already triggered */
                    if (sync_master_ctx.busy)
                    {
                        ppt_kill_hw_fsm(PPT_FSM_PTX);
                        sync_master_ctx.busy = false;
                    }
                }
                sync_master_ctx.hb.state = SYNC_MASTER_HB_STATE_HB_SEND;
                sync_master_ctx.hb.slow = true;
            }
        }
        else if (sync_master_ctx.hb.state == SYNC_MASTER_HB_STATE_HB_SEND)
        {
            bool hb_enter = false;
            bool hb_continue = false;
            /* tx pend message */
            if (sync_ctx.msg_send.count != 0)
            {
                sync_master_ctx.hb.slow = false;
                sync_master_hb_exit(true);
            }
            /* rx empty ack message */
            else if (rx_ack && rx_empty)
            {
                sync_master_ctx.hb.unack_count = 0;
                hb_enter = true;
            }
            else
            {
                /* rx non-empty message */
                if (sync_master_ctx.rx && rx_empty == false)
                {
                    sync_master_ctx.hb.slow = false;
                    sync_master_hb_exit(true);
                }
                /* no rx or rx non-ack */
                else if (rx_ack == false)
                {
                    /* skip wakeup only when already in the hb state */
                    if (sync_master_ctx.hb.slow)
                    {
                        sync_master_ctx.hb.unack_count += 1;
                        if (sync_master_ctx.hb.unack_count >= SYNC_HB_TO_TIMES)
                        {
                            sync_master_ctx.hb.slow = false;
                            sync_master_ctx.count = 0;
                            sync_master_hb_exit(false);
                        }
                        else
                        {
                            hb_continue = true;
                            sync_master_ctx.hb.rhy_counter -= 1;
                        }
                    }
                }
            }

            if (hb_enter || hb_continue)
            {
                if (hb_enter && sync_master_ctx.hb.rhy_changing)
                {
                    sync_master_ctx.hb.rhy_changing = false;
                    sync_master_ctx.hb.rhy_counter = 0;
                    APP_PRINT_INFO1("sync: hb enter %dus",
                                    sync_master_hb_rhy[sync_master_ctx.hb.rhy_idx].speed);
                    if (sync_ctx.hb_cb)
                    {
                        sync_hb_cb_param_t param = {};
                        param.enter = true;
                        param.interval = sync_master_hb_rhy[sync_master_ctx.hb.rhy_idx].speed;
                        sync_ctx.hb_cb(&param);
                    }
                }
                if (sync_master_ctx.hb.rhy_changing)
                {
                    sync_master_ctx.hb.speed = sync_master_ctx.hb.rhy_changing_idx == 0 ? SYNC_SYNC_SPEED :
                                               sync_master_hb_rhy[sync_master_ctx.hb.rhy_changing_idx].speed;
                }
                else
                {
                    sync_master_ctx.hb.speed = sync_master_hb_rhy[sync_master_ctx.hb.rhy_idx].speed;
                }
                sync_master_ctx.hb.ce_cnt = sync_master_ctx.quick.ce_cnt + sync_master_ctx.hb.speed /
                                            SYNC_SYNC_SPEED;
                sync_master_ctx.hb.slow = true;
                if (sync_ctx.rte_req_cb)
                {
                    sync_ctx.rte_req_cb(false);
                }
            }
        }
    }

    if (sync_master_ctx.hb.slow == false)
    {
        sync_master_push_data();
    }

    if (sync_master_ctx.quick.req_ack == false)
    {
        for (uint8_t loop = 0; loop < 7; loop++)
        {
            bool ack = rx && (sync_master_ctx.quick.msg_ack & BIT(loop));
#if TIME_DEBUG
            if (ack)
            {
                time_debug_level_high(SYNC_MASTER_TIME_DEBUG_ACK);
                time_debug_level_low(SYNC_MASTER_TIME_DEBUG_ACK);
            }
            else
            {
                time_debug_level_high(SYNC_MASTER_TIME_DEBUG_NACK);
                time_debug_level_low(SYNC_MASTER_TIME_DEBUG_NACK);
                time_debug_level_high(SYNC_MASTER_TIME_DEBUG_NACK);
                time_debug_level_low(SYNC_MASTER_TIME_DEBUG_NACK);
            }
#endif
            sync_msg_t *pmsg = sync_master_ctx.quick.msg_txed[loop];
            if (pmsg)
            {
                sync_master_ctx.quick.msg_txed[loop] = NULL;
                pmsg->send_info.res = ack ? SYNC_SEND_RESULT_ACKED : SYNC_SEND_RESULT_NACKED;
                sync_pendcall(sync_msg_handle_tx_done, pmsg, 0);
            }
        }
    }

    if (rx)
    {
        if (rx_header.s.seq != sync_master_ctx.seq_rx)// || rx_crc != sync_master_ctx.crc_rx
        {
            sync_master_ctx.seq_rx = rx_header.s.seq;
            sync_master_ctx.crc_rx = rx_crc;
            if (rx_len > 0)
            {
                if (rx_pdu->opcode >= SYNC_OPCODE_APP_START)
                {
                    if (sync_ctx.msg_receive_cb)
                    {
                        sync_receive_info_t info = {};
                        info.rssi = ppt_get_rssi(rssi);
                        sync_ctx.msg_receive_cb((uint8_t *)rx_pdu, rx_len, &info);
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

PPT_ISR_SECTION void sync_master_handle_sync_fast_to_quick(void)
{
    sync_header_t rx_header = {};
    sync_master_handle_rx_quick(false, rx_header, NULL, 0, 0, 0);

    if (sync_master_ctx.quick.tx_count < 6)
    {
        return;
    }

    sync_master_ctx.count += 1;
    if (
#if SYNC_SUPPORT_SYNC_FAST_DISABLE
        SYNC_SYNC_FAST_PERIOD == SYNC_SYNC_FAST_PERIOD_DISABLE ||
#endif
        sync_master_ctx.count >= SYNC_MASTER_SYNC_FAST_TIMES)
    {
        sync_hw_timer_stop();
        sync_master_close();
        sync_master_msg_sending_free();
#if SYNC_SUPPORT_POWER_CONTROL
        if (sync_ctx.tx_power_adaptive)
        {
            sync_power_ctrl_stop();
        }
#endif
        ppt_reg_handler(sync_master_ppt_isr_handler);
        sync_master_ctx.pair_state = SYNC_PAIR_STATE_IDLE;
        sync_master_ctx.state = SYNC_STATE_IDLE;
        APP_PRINT_INFO0("sync: disconnected!");
        sync_channel_print_statistic();
        sync_pendcall(sync_master_handle_disconnected, 0, 0);
        if (sync_ctx.rte_req_cb)
        {
            sync_ctx.rte_req_cb(false);
        }
    }
    else
    {
        sync_channel_change_next_in_group();
        if (sync_master_msg_sending_is_empty())
        {
            sync_master_push_data();
        }
    }
}

PPT_ISR_SECTION void sync_master_handle_sync_lost_to_quick(void)
{
    sync_header_t rx_header = {};
    sync_master_handle_rx_quick(false, rx_header, NULL, 0, 0, 0);

    if (sync_master_ctx.quick.tx_count < 6)
    {
        return;
    }

    sync_master_ctx.count += 1;
    if (sync_master_ctx.count >= SYNC_MASTER_SYNC_LOST_TIMES)
    {
        APP_PRINT_INFO0("sync: lost!");
#ifdef SYNC_DEBUG
        sync_pendcall(sync_master_handle_sync_lost, 0, 0);
#endif
#if SYNC_SUPPORT_SYNC_FAST_DISABLE
        if (SYNC_SYNC_FAST_PERIOD == SYNC_SYNC_FAST_PERIOD_DISABLE)
        {
            sync_master_handle_sync_fast_to_quick();
        }
        else
#endif
        {
            sync_master_ctx.state = SYNC_STATE_SYNC_FAST;
            sync_master_ctx.count = 0;
            sync_channel_change_next_in_group();
        }
    }
}

PPT_ISR_SECTION void sync_master_handle_sync_early_quick(void)
{
    sync_master_ctx.quick.count = (sync_master_ctx.quick.count + 1) & 0x7;
    if (sync_master_ctx.quick.skip)
    {
        sync_master_ctx.quick.skip = false;
        /* no need protect since it shall be idle currently */
        if (sync_master_ctx.quick.tx_count == 6)
        {
            sync_master_ctx.quick.tx_count = 0;
        }
        else
        {
            if (sync_master_ctx.quick.req_ack && (sync_master_ctx.quick.count & 0x1))
            {
                /* in case skip even slot */
                sync_master_ctx.quick.tx_count += 2;
            }
            else
            {
                sync_master_ctx.quick.tx_count += 1;
            }
        }
    }
    if (sync_master_ctx.quick.count == 0)
    {
        time_debug_level_low(TIME_DEBUG_CHAN_TIMING);
        sync_master_ctx.quick.ce_cnt += 1;
        WR_PPT_REG_FIELD(PRO_TRX_CONTROL, ack_enable, 0);
        WR_PPT_REG_FIELD(PRO_HS_LENGTH, hs_field_length_4_0, 0);
        sync_master_ctx.quick.ce_clk = sync_hw_frtimer_get_count();
    }
    else if (sync_master_ctx.quick.count == 7)
    {
        return;
    }
    else
    {
        if (sync_master_ctx.quick.count == 6)
        {
            WR_PPT_REG_FIELD(PRO_TRX_CONTROL, ack_enable, 1);
        }
    }

    if (sync_master_ctx.quick.req_ack)
    {
        if (sync_master_ctx.quick.count & 0x1)
        {
            return;
        }
        WR_PPT_REG_FIELD(PRO_TRX_CONTROL, ack_enable, 1);
    }

    if (sync_master_ctx.hb.slow)
    {
        sync_master_ctx.quick.tx_count = sync_master_ctx.quick.count;
        if (sync_master_ctx.hb.ce_cnt == sync_master_ctx.quick.ce_cnt || sync_master_ctx.hb.oversleep)
        {
            if (sync_master_ctx.quick.count == 6)
            {
                sync_master_handle_hb_to();
                sync_master_ctx.hb.ce_cnt += sync_master_ctx.hb.speed / SYNC_SYNC_SPEED;
                sync_master_ctx.hb.oversleep = false;
            }
            else if (sync_master_ctx.quick.count == 5)
            {
                if (sync_ctx.rte_req_cb)
                {
                    sync_ctx.rte_req_cb(true);
                }
            }
        }
    }
    else if (sync_ctx.hw_timer_pend_time <= 12 || sync_master_ctx.quick.count == 6)
    {
        sync_master_ctx.busy = true;
        ppt_execute_instruction(PPT_HW_INSTRUCTION_PTX_ENABLE);
    }
    else
    {
        sync_master_ctx.quick.skip = true;
        if (SYNC_ISR_DEBUG)
        {
            APP_PRINT_WARN2("sync_isr: skip count %d, timer pend %dus", sync_master_ctx.quick.count,
                            sync_ctx.hw_timer_pend_time);
        }
    }
    if (sync_master_ctx.quick.count == 0)
    {
        time_debug_level_high(TIME_DEBUG_CHAN_TIMING);
    }
}

PPT_ISR_SECTION static void sync_master_ppt_isr_handler_quick(void)
{
    time_debug_level_high(TIME_DEBUG_CHAN_ISR);
    PRO_MISR_TYPE reg_misr;
    reg_misr.d16 = RD_PPT_REG(PRO_MISR);
    //APP_PRINT_INFO1("sync_isr: ptx isr 0x%04x", reg_misr.d16);

    if (reg_misr.tx_int)
    {
        sync_master_ctx.busy = false;
        /* pop tx stack */
        PRO_TX_STACK_TYPE tx_stack = {.d16 = RD_PPT_REG(PRO_TX_STACK)};
        //PRO_TX_HS_LOWER_TYPE hs_lower = {.d16 = RD_PPT_REG(PRO_TX_HS_LOWER)};
        //PRO_TX_HS_UPPER_TYPE hs_upper = {.d16 = RD_PPT_REG(PRO_TX_HS_UPPER)};
        int32_t clk_diff = 0;
        if (sync_master_ctx.hb.slow == false)
        {
            PRO_TX_CLK_LOWER_TYPE tx_clk_lower = {.d16 = RD_PPT_REG(PRO_TX_CLK_LOWER)};
            PRO_TX_CLK_UPPER_TYPE tx_clk_upper = {.d16 = RD_PPT_REG(PRO_TX_CLK_UPPER)};
            uint32_t tx_clk = (tx_clk_upper.tx_clk_31_16 << 16) + tx_clk_lower.tx_clk_15_0;
            clk_diff = sync_hw_frtimer_time_diff(tx_clk, sync_master_ctx.quick.ce_clk);
            if (clk_diff >= 0)
            {
                uint8_t tx_count = clk_diff / 125;
                if (sync_master_ctx.quick.tx_count != tx_count)
                {
                    APP_PRINT_WARN2("sync_isr: ptx tx count fail, add %d cal %d", sync_master_ctx.quick.tx_count,
                                    tx_count);
                    sync_master_ctx.quick.tx_count = tx_count;
                }
            }
        }
        PRO_TX_LENGTH_TYPE length = {.d16 = RD_PPT_REG(PRO_TX_LENGTH)}; // Read 0x6a to pop stack
        sync_master_ctx.tx_fifo_bitmap = RD_U32_BZDMA_REG(BZDMA_REG_BLEONLY_ENTRY_SEGMENT_VALID((
                                                              uint8_t)tx_stack.tx_entry_1_0));
        if (SYNC_ISR_DEBUG)
        {
            APP_PRINT_INFO8("sync_isr: ptx tx stack 0x%04x, bitmap 0x%02x, rd ptr hw %d sw %d, len %d, ce %d, clk %d, tx %d",
                            tx_stack.d16, sync_master_ctx.tx_fifo_bitmap,
                            tx_stack.tx_ptr, sync_master_ctx.tx_ptr, length.tx_length, sync_master_ctx.quick.ce_cnt, clk_diff,
                            sync_master_ctx.quick.tx_count);
        }
        sync_master_ctx.tx_ptr_hw = tx_stack.tx_ptr;
        sync_master_ctx.tx_null = length.tx_length == 0;
        sync_master_ctx.tx_old = (sync_master_ctx.tx_null == false) &&
                                 (tx_stack.tx_ptr != sync_master_ctx.tx_ptr);

        /* workaround when only tx empty pkt */
        if (sync_master_ctx.tx_null
            && 0 == RD_U32_BZDMA_REG(BZDMA_REG_BLEONLY_ENTRY_SEGMENT_VALID((uint8_t)tx_stack.tx_entry_1_0))
            && tx_stack.tx_ptr != sync_master_ctx.tx_ptr
           )
        {
            APP_PRINT_WARN0("sync: fail, fifo error");
            sync_master_init_tx_fifo();
        }

        if (sync_master_ctx.tx_null || sync_master_ctx.tx_old)
        {
            APP_PRINT_WARN2("sync_isr: tx fail, null %d, old %d", sync_master_ctx.tx_null,
                            sync_master_ctx.tx_old);
            sync_master_ctx.tx_fail_count += 1;
        }
        else
        {
            sync_master_ctx.tx_fail_count = 0;
        }

        if (sync_master_ctx.quick.tx_count <= 5)
        {
            if (sync_master_ctx.quick.req_ack)
            {
                WR_PPT_REG_FIELD(PRO_HS_LENGTH, hs_field_length_4_0, SYNC_SUFFIX_LEN);
                sync_master_ctx.busy = true;
            }
            else
            {
                /* don't switch pkt when tx fail */
                if (false == sync_master_ctx.tx_null && false == sync_master_ctx.tx_old &&
                    sync_master_ctx.tx_fifo_bitmap)
                {
                    sync_master_pop_data();
                    sync_master_push_data();
                }
                /* tx_count is used for determine ack timing, so update at last */
                sync_master_ctx.quick.tx_count += 1;
            }
        }
        else
        {
            WR_PPT_REG_FIELD(PRO_HS_LENGTH, hs_field_length_4_0, SYNC_SUFFIX_LEN);
            sync_master_ctx.busy = true;
        }
    }

    if (reg_misr.rx_int)
    {
        sync_master_ctx.busy = false;
        ppt_clear_radio();
        WR_PPT_REG_FIELD(PRO_HS_LENGTH, hs_field_length_4_0, 0);
        /* pop rx stack */
        //PRO_LENGTH_INCLUDE_ADDON_TYPE len_inc_addon = {.d16 = RD_PPT_REG(PRO_LENGTH_INCLUDE_ADDON)};
        PRO_RX_STACK_TYPE rx_stack = {.d16 = RD_PPT_REG(PRO_RX_STACK)};
        PRO_RX_HP_TYPE hp = {.d16 = RD_PPT_REG(PRO_RX_HP)};
        if (rx_stack.is_max_length)
        {
            APP_PRINT_WARN0("sync_isr: ptx rx max length");
            rx_stack.rx_time_out = 1;
        }
        bool rx = !(rx_stack.rx_time_out || rx_stack.rx_hit == false || rx_stack.is_crc_error);
        //PRO_ACCHIT_CLK_LOWER_TYPE acchit_clk_lower = {.d16 = RD_PPT_REG(PRO_ACCHIT_CLK_LOWER)};
        //PRO_ACCHIT_CLK_UPPER_TYPE acchit_clk_upper = {.d16 = RD_PPT_REG(PRO_ACCHIT_CLK_UPPER)};
        PRO_LENGTH_TYPE length = {.d16 = RD_PPT_REG(PRO_LENGTH)};
        PRO_RSSI_TYPE rssi = {.d16 = RD_PPT_REG(PRO_RSSI)};
        PRO_RX_CRC_LOWER_TYPE rx_crc_lower = {.d16 = RD_PPT_REG(PRO_RX_CRC_LOWER)};
        PRO_RX_CRC_UPPER_TYPE rx_crc_upper = {.d16 = RD_PPT_REG(PRO_RX_CRC_UPPER)};
        uint32_t rx_crc = (rx_crc_upper.d16 << 16) + rx_crc_lower.d16;
        rx_crc >>= 32 - SYNC_CRC_LEN;
        PRO_ACCHIT_CLK_LOWER_TYPE acchit_clk_lower = {.d16 = RD_PPT_REG(PRO_ACCHIT_CLK_LOWER)};
        PRO_ACCHIT_CLK_UPPER_TYPE acchit_clk_upper = {.d16 = RD_PPT_REG(PRO_ACCHIT_CLK_UPPER)};
        uint32_t acchit_clk = (acchit_clk_upper.acchit_clk_31_16 << 16) + acchit_clk_lower.acchit_clk_15_0;
        PRO_RX_HS_LOWER_TYPE rx_hs_lower = {.d16 = RD_PPT_REG(PRO_RX_HS_LOWER)};
        PRO_RX_HS_UPPER_TYPE rx_hs_upper = {.d16 = RD_PPT_REG(PRO_RX_HS_UPPER)}; //read 0x62 to pop rx stack
        uint32_t hs = (rx_hs_upper.d16 << 16) + rx_hs_lower.d16;
        sync_master_ctx.quick.msg_ack = hs;
        //PRO_INT_CTRL_TYPE int_ctrl = {.d16 = RD_PPT_REG(PRO_INT_CTRL)};
        bool req_ack = sync_master_ctx.quick.req_ack;
        if (SYNC_ISR_DEBUG)
        {
            int32_t clk_diff = 0;
            clk_diff = sync_hw_frtimer_time_diff(acchit_clk, sync_master_ctx.quick.ce_clk);
            APP_PRINT_INFO6("sync_isr: ptx rx stack 0x%04x, ce %d, clk %d, hp 0x%02x, hs 0x%x, len %d",
                            rx_stack.d16,
                            sync_master_ctx.quick.ce_cnt, clk_diff, hp.hp, hs, length.d16);
        }

        if (rx)
        {
            uint16_t rx_len = length.d16 + SYNC_OVERHEAD_LEN + 2;
            uint8_t *rx_buffer = ppt_get_rx_buffer(rx_len);
            ppt_pop_rx_fifo(0, rx_buffer, rx_len);
            sync_pdu_t *pdu = (sync_pdu_t *)(rx_buffer + SYNC_OVERHEAD_LEN + 2);
            sync_header_t rx_header;
            rx_header.value = hp.hp;

            //APP_PRINT_INFO2("sync_isr: ptx rx len %d, %b", rx_len, TRACE_BINARY(rx_len, rx_buffer));

            if (sync_master_ctx.count != 0)
            {
                sync_master_ctx.count = 0;
            }
            sync_master_handle_rx_quick(true, rx_header, pdu, length.d16,
                                        rssi.rssi, rx_crc);
            if (sync_master_ctx.state == SYNC_STATE_SYNC_FAST)
            {
                sync_master_ctx.state = SYNC_STATE_SYNC;
                sync_master_ctx.count = 0;
                APP_PRINT_INFO2("sync: reconnected on %d[%d]!", sync_ctx.chan_cur,
                                sync_ctx.chan_cfg[sync_ctx.chan_cur].freq);
#ifdef SYNC_DEBUG
                sync_pendcall(sync_master_handle_sync_reconnected, 0, 0);
#endif
            }
        }
        else
        {
            if (sync_master_ctx.state == SYNC_STATE_SYNC)
            {
                sync_master_handle_sync_lost_to_quick();
            }
            else if (sync_master_ctx.state == SYNC_STATE_SYNC_FAST)
            {
                sync_master_handle_sync_fast_to_quick();
            }
        }
        /* tx_count is used for determine ack timing, so update at last */
        if (req_ack)
        {
            sync_master_ctx.quick.tx_count += 2;
            if (sync_master_ctx.quick.tx_count > 6)
            {
                sync_master_ctx.quick.tx_count = 0;
            }
        }
        else
        {
            sync_master_ctx.quick.tx_count = 0;
        }
    }

    time_debug_level_low(TIME_DEBUG_CHAN_ISR);
}

PPT_ISR_SECTION void sync_master_handle_sync_early_first_quick(void)
{
    sync_hw_timer_start(125, sync_master_handle_sync_early_quick);
    sync_hw_timer_set_periodic(true);
    sync_hw_timer_impr_ip(true);
    ppt_reg_handler(sync_master_ppt_isr_handler_quick);
    sync_master_ctx.quick.enable = true;
    sync_master_ctx.quick.count = 7;
    sync_master_ctx.quick.tx_count = 0;
    sync_master_ctx.quick.ce_cnt -= 1;
    WR_PPT_REG_FIELD(PRO_FIELD_LENGTH, hp_field_length_3_0, SYNC_HEADER_LEN_MT1R);
    sync_master_set_header(sync_master_ctx.quick.seq_ack, sync_master_ctx.seq_tx,
                           sync_master_ctx.quick.req_ack);
    sync_master_handle_sync_early_quick();
    sync_master_push_data();
}

PPT_ISR_SECTION void sync_master_handle_sync_early_hb_quick(void)
{
    time_debug_level_high(TIME_DEBUG_CHAN_DLPS);
    sync_hw_timer_start(125, sync_master_handle_sync_early_quick);
    sync_hw_timer_set_periodic(true);
    sync_hw_timer_impr_ip(true);
    time_debug_level_low(TIME_DEBUG_CHAN_DLPS);
    sync_master_ctx.hb.sleep_cal = false;
    uint8_t count = (sync_master_ctx.quick.count + 1) & 0x7;
    if (count != 0)
    {
        sync_master_ctx.quick.ce_clk = sync_hw_frtimer_get_count() - count * 125 * SYNC_HW_FRTIMER_CLOCK;
    }
    sync_master_handle_sync_early_quick();
    //APP_PRINT_INFO4("sync: dlps mac exit, sleep %d hw %d, ce %d, count %d!",
    //                sync_master_ctx.hb.time_left, sync_master_ctx.hb.time_left1, sync_master_ctx.quick.ce_cnt,
    //                sync_master_ctx.quick.count);
}

static void sync_master_dlps_mac_exit(void);
PPT_ISR_SECTION void sync_master_hb_sleep_resume(void)
{
    if (sync_master_ctx.hb.sleep)
    {
        if (sync_master_ctx.hb.sleep_mac)
        {
            RD_PPT_REG(PRO_MISR);
        }
        else
        {
            sync_master_dlps_mac_exit();
        }
    }
}
#endif

PPT_ISR_SECTION static void sync_master_ppt_isr_handler(void)
{
    time_debug_level_high(TIME_DEBUG_CHAN_ISR);
    PRO_MISR_TYPE reg_misr;
    reg_misr.d16 = RD_PPT_REG(PRO_MISR);
    //APP_PRINT_INFO1("sync_isr: ptx isr 0x%04x\r\n", reg_misr.d16);

    /* free the pend msg at the early handler */
    if (sync_master_ctx.pmsg_pend_free)
    {
        sync_pendcall(sync_msg_handle_tx_done, sync_master_ctx.pmsg_pend_free, 0);
        sync_master_ctx.pmsg_pend_free = NULL;
    }

    if (reg_misr.tx_int)
    {
        Pad_Config(P2_3, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_DOWN, PAD_OUT_ENABLE,
            PAD_OUT_HIGH);
        Pad_Config(P2_3, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_DOWN, PAD_OUT_ENABLE,
            PAD_OUT_LOW);
        /* pop tx stack */
        PRO_TX_STACK_TYPE tx_stack = {.d16 = RD_PPT_REG(PRO_TX_STACK)};
        //PRO_TX_HS_LOWER_TYPE hs_lower = {.d16 = RD_PPT_REG(PRO_TX_HS_LOWER)};
        //PRO_TX_HS_UPPER_TYPE hs_upper = {.d16 = RD_PPT_REG(PRO_TX_HS_UPPER)};
#if SYNC_SUPPORT_MT1R
        if (sync_ctx.mt1r.flag)
        {
            if ((sync_master_ctx.pair_state == SYNC_PAIR_STATE_PAIRING &&
                 sync_master_ctx.phase == SYNC_MASTER_PAIR_PHASE_SEND_REQ)
                || (sync_master_ctx.pair_state == SYNC_PAIR_STATE_PAIRED &&
                    sync_master_ctx.state == SYNC_STATE_SYNC_SLOW))
            {
                PRO_TX_CLK_LOWER_TYPE tx_clk_lower = {.d16 = RD_PPT_REG(PRO_TX_CLK_LOWER)};
                PRO_TX_CLK_UPPER_TYPE tx_clk_upper = {.d16 = RD_PPT_REG(PRO_TX_CLK_UPPER)};
                sync_master_ctx.quick.ce_clk = (tx_clk_upper.tx_clk_31_16 << 16) + tx_clk_lower.tx_clk_15_0;
            }
        }
#endif
        PRO_TX_LENGTH_TYPE length = {.d16 = RD_PPT_REG(PRO_TX_LENGTH)}; // Read 0x6a to pop stack
        if (SYNC_ISR_DEBUG)
        {
            APP_PRINT_INFO6("sync_isr: ptx tx stack 0x%04x, seq 0x%02x, bitmap 0x%02x, rd ptr hw %d sw %d, len %d",
                            tx_stack.d16, sync_master_ctx.seq_tx,
                            RD_U32_BZDMA_REG(BZDMA_REG_BLEONLY_ENTRY_SEGMENT_VALID((uint8_t)tx_stack.tx_entry_1_0)),
                            tx_stack.tx_ptr, sync_master_ctx.tx_ptr, length.tx_length);
        }
        sync_master_ctx.tx_ptr_hw = tx_stack.tx_ptr;
        sync_master_ctx.tx_null = length.tx_length == 0;
        sync_master_ctx.tx_old = tx_stack.tx_ptr != sync_master_ctx.tx_ptr;
        sync_header_t header;
        header.value = tx_stack.tx_hp;
        if (header.seq != (sync_master_ctx.seq_tx & ((1 << SYNC_HEADER_SEQ_LEN) - 1)))
        {
            sync_master_ctx.tx_old = true;
        }
        if (sync_master_ctx.tx_null || sync_master_ctx.tx_old)
        {
            APP_PRINT_WARN2("sync_isr: tx fail, null %d, old %d", sync_master_ctx.tx_null,
                            sync_master_ctx.tx_old);
            sync_master_ctx.tx_fail_count += 1;
        }
        else
        {
            sync_master_ctx.tx_fail_count = 0;
        }

#if SYNC_SUPPORT_POWER_CONTROL
        if (sync_ctx.tx_power_adaptive)
        {
            sync_ctx.power_ctrl_count += 1;
        }
#endif

        if (sync_master_ctx.state == SYNC_STATE_SYNC &&
            sync_master_ctx.pair_state == SYNC_PAIR_STATE_PAIRED
           )
        {
            sync_master_msg_sending_info_t *pmsg_info = sync_master_msg_sending_peak();
            if (pmsg_info)
            {
                pmsg_info->tx_count += 1;
                if (SYNC_ISR_DEBUG)
                {
                    sync_msg_type_t type = SYNC_MSG_TYPE_ALL;
                    uint8_t opcode = 0;
                    if (pmsg_info->pmsg)
                    {
                        type = pmsg_info->pmsg->type;
                        opcode = pmsg_info->pmsg->data[0];
                    }
                    else
                    {
                        uint8_t *tx_buffer = ppt_get_tx_buffer(1);
                        opcode = tx_buffer[0];
                    }

                    APP_PRINT_INFO2("sync_isr: tx msg type %d, op 0x%02x", type, opcode);
                }
            }

            if (sync_master_ctx.hb.slow == false)
            {
                if (sync_master_param.early_high_prio_enable)
                {
                    if (sync_master_ctx.curr_time_in_ce == 0)
                    {
                        if (sync_master_param.tx_prepare_cb != NULL)
                        {
                            sync_master_param.tx_prepare_cb();
                        }
                    }
                }
            }
        }
    }

    if (reg_misr.rx_int)
    {
        sync_master_ctx.handling_ack = true;
        sync_master_ctx.busy = false;

        /* pop rx stack */
        //PRO_LENGTH_INCLUDE_ADDON_TYPE len_inc_addon = {.d16 = RD_PPT_REG(PRO_LENGTH_INCLUDE_ADDON)};
        PRO_RX_STACK_TYPE rx_stack = {.d16 = RD_PPT_REG(PRO_RX_STACK)};
        PRO_RX_HP_TYPE hp = {.d16 = RD_PPT_REG(PRO_RX_HP)};
        if (rx_stack.is_max_length)
        {
            APP_PRINT_WARN0("sync_isr: ptx rx max length");
            rx_stack.rx_time_out = 1;
        }
        bool rx = !(rx_stack.rx_time_out || rx_stack.rx_hit == false || rx_stack.is_crc_error);
        if (sync_master_ctx.pair_state == SYNC_PAIR_STATE_PAIRED &&
            sync_master_ctx.state != SYNC_STATE_SYNC_SLOW)
        {
            sync_header_t rx_header;
            rx_header.value = hp.hp;
            sync_master_handle_ack(rx, rx_header);
        }

        //PRO_ACCHIT_CLK_LOWER_TYPE acchit_clk_lower = {.d16 = RD_PPT_REG(PRO_ACCHIT_CLK_LOWER)};
        //PRO_ACCHIT_CLK_UPPER_TYPE acchit_clk_upper = {.d16 = RD_PPT_REG(PRO_ACCHIT_CLK_UPPER)};
        PRO_RX_CRC_LOWER_TYPE rx_crc_lower = {.d16 = RD_PPT_REG(PRO_RX_CRC_LOWER)};
        PRO_RX_CRC_UPPER_TYPE rx_crc_upper = {.d16 = RD_PPT_REG(PRO_RX_CRC_UPPER)};
        uint32_t rx_crc = (rx_crc_upper.d16 << 16) + rx_crc_lower.d16;
        rx_crc >>= 32 - SYNC_CRC_LEN;
        PRO_RSSI_TYPE rssi = {.d16 = RD_PPT_REG(PRO_RSSI)};
        PRO_LENGTH_TYPE length = {.d16 = RD_PPT_REG(PRO_LENGTH)};
        //PRO_RX_HS_LOWER_TYPE rx_hs_lower = {.d16 = RD_PPT_REG(PRO_RX_HS_LOWER)};
        PRO_RX_HS_UPPER_TYPE rx_hs_upper = {.d16 = RD_PPT_REG(PRO_RX_HS_UPPER)}; //read 0x62 to pop rx stack
        //PRO_INT_CTRL_TYPE int_ctrl = {.d16 = RD_PPT_REG(PRO_INT_CTRL)};

        if (SYNC_ISR_DEBUG)
        {
            APP_PRINT_INFO4("sync_isr: ptx rx stack 0x%04x, rssi %d, hp 0x%02x, len %d", rx_stack.d16,
                            ppt_get_rssi(rssi.rssi), hp.hp, length.d16);
        }

        sync_channel_statistic(rx);
        sync_ctx.chan_used_count += 1;
        if (false == rx)
        {
            if (sync_master_ctx.pair_state == SYNC_PAIR_STATE_PAIRED)
            {
                if (sync_master_ctx.state == SYNC_STATE_SYNC)
                {
                    sync_master_handle_sync_lost_to();
                }
                else if (sync_master_ctx.state == SYNC_STATE_SYNC_SLOW)
                {
                    sync_master_handle_sync_slow_to();
                }
                else if (sync_master_ctx.state == SYNC_STATE_SYNC_FAST)
                {
                    sync_master_handle_sync_fast_to();
                }
            }
            else if (sync_master_ctx.pair_state == SYNC_PAIR_STATE_PAIRING)
            {
                if (sync_master_ctx.phase == SYNC_MASTER_PAIR_PHASE_SEND_REQ)
                {
                    sync_master_handle_pair_req_to();
                }
                else if (sync_master_ctx.phase == SYNC_MASTER_PAIR_PHASE_SEND_CFM)
                {
                    sync_master_handle_pair_cfm_to();
                }
            }
        }
        else
        {
            uint16_t rx_len = length.d16 + SYNC_OVERHEAD_LEN;
            uint8_t *rx_buffer = ppt_get_rx_buffer(rx_len);
            ppt_pop_rx_fifo(0, rx_buffer, rx_len);
            sync_pdu_t *pdu = (sync_pdu_t *)(rx_buffer + SYNC_OVERHEAD_LEN);
            sync_header_t rx_header;
            rx_header.value = hp.hp;

            //APP_PRINT_INFO2("sync_isr: ptx rx len %d, %b", rx_len, TRACE_BINARY(rx_len, rx_buffer));
            if (sync_master_ctx.pair_state == SYNC_PAIR_STATE_PAIRED)
            {
                bool handle_rx = true;
                if (sync_master_ctx.state == SYNC_STATE_SYNC)
                {
                    if (sync_master_ctx.count != 0)
                    {
                        sync_master_ctx.count = 0;
                    }
                }
                else if (sync_master_ctx.state == SYNC_STATE_SYNC_SLOW)
                {
                    handle_rx = false;
                    if (length.d16 == sizeof(sync_opcode_t) && pdu->opcode == SYNC_OPCODE_CONN_RSP)
                    {
                        sync_master_ctx.state = SYNC_STATE_SYNC;
                        sync_master_ctx.count = 0;
                        sync_master_ctx.tx_ptr = ppt_trigger_fw_ack(0);
                        sync_ppt_set_tifs(false);
                        APP_PRINT_INFO2("sync: connected on %d[%d]!", sync_ctx.chan_cur,
                                        sync_ctx.chan_cfg[sync_ctx.chan_cur].freq);
                        sync_master_ctx.rx = 1;
                        sync_master_ctx.seq_tx = 0;
                        sync_master_ctx.handling_ack = false;
#if SYNC_SUPPORT_MT1R
                        if (sync_ctx.mt1r.flag)
                        {
                            uint32_t clk = sync_hw_frtimer_get_count();
                            uint32_t delay = sync_hw_frtimer_time_elapse(clk, sync_master_ctx.quick.ce_clk);
                            delay = SYNC_CONNECT_DELAY > delay ? SYNC_CONNECT_DELAY - delay : 30;
                            sync_hw_timer_start(delay, sync_master_handle_sync_early_first_quick);
                            sync_ppt_set_rf_time_mt1r();
                            sync_ctx.conn_interval = 1000;
                            APP_PRINT_ERROR0("sync: quick enter");
                            if (sync_ctx.rte_req_cb)
                            {
                                sync_ctx.rte_req_cb(true);
                            }
                        }
                        else
#endif
                        {
                            sync_master_ctx.curr_time_in_ce = SYNC_SYNC_SPEED;
                            sync_master_start_sync_timer();
                            if (SYNC_SYNC_SPEED_HIGH <= SYNC_SYNC_SPEED_HIGH_MIN)
                            {
                                if (sync_ctx.rte_req_cb)
                                {
                                    sync_ctx.rte_req_cb(true);
                                }
                            }
                        }
                        sync_pendcall(sync_master_handle_connected, 0, 0);
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
                        sync_master_handle_sync_slow_to();
                    }
                }
                else if (sync_master_ctx.state == SYNC_STATE_SYNC_FAST)
                {
                    sync_master_ctx.curr_time_in_ce = SYNC_SYNC_SPEED;
                    sync_master_start_sync_timer();
                    sync_master_ctx.state = SYNC_STATE_SYNC;
                    sync_master_ctx.count = 0;
                    APP_PRINT_INFO2("sync: reconnected on %d[%d]!", sync_ctx.chan_cur,
                                    sync_ctx.chan_cfg[sync_ctx.chan_cur].freq);
#ifdef SYNC_DEBUG
                    sync_pendcall(sync_master_handle_sync_reconnected, 0, 0);
#endif
                }
                if (handle_rx)
                {
                    sync_master_handle_rx(true, rx_header, (sync_pdu_t *)(rx_buffer + SYNC_OVERHEAD_LEN), length.d16,
                                          rssi.rssi, rx_crc);
                }
            }
            else if (sync_master_ctx.pair_state == SYNC_PAIR_STATE_PAIRING)
            {
                if (sync_master_ctx.phase == SYNC_MASTER_PAIR_PHASE_SEND_REQ)
                {
                    bool retry = true;
                    do
                    {
                        int8_t rssi_dbm = ppt_get_rssi(rssi.rssi);
                        if (rssi_dbm < sync_ctx.pair_rssi_th)
                        {
                            APP_PRINT_ERROR2("sync: pair rssi check fail, %d < %d", rssi_dbm, sync_ctx.pair_rssi_th);
                            break;
                        }
                        if (length.d16 == sizeof(sync_opcode_t) + sizeof(sync_pair_rsp_t))
                        {
                            sync_pdu_t *pdu = (sync_pdu_t *)(rx_buffer + SYNC_OVERHEAD_LEN);
                            if (pdu->opcode == SYNC_OPCODE_PAIR_RSP)
                            {
                                retry = false;
                                sync_master_ctx.phase = SYNC_MASTER_PAIR_PHASE_SEND_CFM;
                                sync_master_ctx.count1 = 0;
                                sync_ppt_set_tifs(false);
                                sync_hw_timer_start(SYNC_SYNC_SPEED, sync_master_handle_sync_slow_early);

                                sync_master_ctx.acc_data = pdu->pair_rsp.acc;
                                sync_acc_set(sync_master_ctx.acc_data);

                                sync_master_ctx.tx_ptr = ppt_trigger_fw_ack(0);
                                sync_master_ctx.rx = 1;
                                sync_master_ctx.seq_tx = 0;
                                sync_set_header(1, 0);
                                sync_pdu_t pdu;
                                pdu.opcode = SYNC_OPCODE_PAIR_CONF;
                                ppt_push_tx_data(sizeof(sync_opcode_t), (uint8_t *)&pdu);

                                APP_PRINT_INFO3("sync: pairing 0x%08x on %d[%d]!", sync_master_ctx.acc_data.addr, sync_ctx.chan_cur,
                                                sync_ctx.chan_cfg[sync_ctx.chan_cur].freq);
                            }
                        }
                    }
                    while (0);
                    if (retry)
                    {
                        sync_master_handle_pair_req_to();
                    }
                }
                else if (sync_master_ctx.phase == SYNC_MASTER_PAIR_PHASE_SEND_CFM)
                {
                    if (rx_header.ack)
                    {
                        sync_master_ctx.pair_state = SYNC_PAIR_STATE_PAIRED;
                        sync_master_ctx.state = SYNC_STATE_SYNC;
                        sync_master_ctx.count = 0;
                        sync_master_ctx.tx_ptr = ppt_trigger_fw_ack(0);

                        sync_master_ctx.rx = 1;
                        sync_master_ctx.seq_tx = 0;
                        sync_master_ctx.handling_ack = false;
                        APP_PRINT_INFO3("sync: paired 0x%08x on %d[%d]!", sync_master_ctx.acc_data.addr, sync_ctx.chan_cur,
                                        sync_ctx.chan_cfg[sync_ctx.chan_cur].freq);
#if SYNC_SUPPORT_MT1R
                        if (sync_ctx.mt1r.flag)
                        {
                            uint32_t clk = sync_hw_frtimer_get_count();
                            uint32_t total_time = SYNC_PAIR_CFM_PERIOD + SYNC_CONNECT_DELAY;
                            uint32_t delay = sync_hw_frtimer_time_elapse(clk, sync_master_ctx.quick.ce_clk);
                            delay = total_time > delay ? total_time - delay : 30;
                            sync_hw_timer_start(delay, sync_master_handle_sync_early_first_quick);
                            sync_ppt_set_rf_time_mt1r();
                            sync_ctx.conn_interval = 1000;
                            APP_PRINT_ERROR0("sync: quick enter");
                            if (sync_ctx.rte_req_cb)
                            {
                                sync_ctx.rte_req_cb(true);
                            }
                        }
                        else
#endif
                        {
                            sync_master_ctx.curr_time_in_ce = SYNC_SYNC_SPEED;
                            sync_master_start_sync_timer();
                            if (SYNC_SYNC_SPEED_HIGH <= SYNC_SYNC_SPEED_HIGH_MIN)
                            {
                                if (sync_ctx.rte_req_cb)
                                {
                                    sync_ctx.rte_req_cb(true);
                                }
                            }
                        }
                        sync_pendcall(sync_master_handle_paired, 0, 0);
                        sync_channel_clear_statistic();
#if SYNC_SUPPORT_POWER_CONTROL
                        if (sync_ctx.tx_power_adaptive)
                        {
                            sync_power_ctrl_start();
                        }
#endif
                        sync_header_t rx_header;
                        rx_header.value = hp.hp;
                        sync_master_handle_rx(true, rx_header, (sync_pdu_t *)(rx_buffer + SYNC_OVERHEAD_LEN), length.d16,
                                              rssi.rssi, rx_crc);
                    }
                    else
                    {
                        sync_master_handle_pair_cfm_to();
                    }
                }
            }
        }
    }

    if (reg_misr.kill_ptx_int)
    {
        ppt_clear_radio();
        WR_PPT_REG(PRO_MISR, BIT4);
        ppt_ctx->fsm = PPT_FSM_STANDBY;
        ppt_ctx->sync_flag = false;
    }
    time_debug_level_low(TIME_DEBUG_CHAN_ISR);
}

void sync_master_init(void)
{
    sync_ppt_init();
    ppt_reg_handler(sync_master_ppt_isr_handler);
    sync_hw_timer_init();
#ifdef SYNC_DEBUG
    sync_hw_frtimer_init();
#endif
    sync_ctx.role = SYNC_ROLE_MASTER;
    memset(&sync_master_ctx, 0, sizeof(sync_master_ctx));
}

bool sync_master_set_hb_param(uint8_t level, uint32_t interval, uint32_t beat_times)
{
    if (level >= SYNC_MASTER_HB_RHY_NUMBER)
    {
        return false;
    }
    if (level != 0)
    {
        sync_master_hb_rhy[level].speed = interval;
    }
    if (level != SYNC_MASTER_HB_RHY_NUMBER - 1)
    {
        sync_master_hb_rhy[level].period = beat_times;
    }
    return true;
}

bool sync_master_wakeup_hb(void)
{
    uint32_t lock = sync_enter_critical();
    if (sync_master_ctx.state != SYNC_STATE_SYNC ||
        sync_master_ctx.pair_state != SYNC_PAIR_STATE_PAIRED)
    {
        sync_exit_critical(lock);
        return false;
    }

    if (sync_master_ctx.hb.slow == false)
    {
        sync_master_hb_exit(true);
        sync_exit_critical(lock);
        return false;
    }

    if (sync_master_ctx.busy == false)
    {
        /* reuse this function to activate */
        sync_master_handle_tx_trigger();
    }
    else
    {
        sync_master_hb_exit(true);
    }
    sync_exit_critical(lock);
    return true;
}

void sync_master_reg_tx_prepare_cb(void (*cb)(void))
{
    sync_master_param.tx_prepare_cb = cb;
}

void sync_master_set_tx_scheme(sync_master_tx_scheme_t tx_scheme)
{
    sync_master_param.tx_scheme = tx_scheme;
}

void sync_master_check_feature(void)
{
    sync_check_feature();

#if SYNC_SUPPORT_MT1R
    if (sync_ctx.mt1r.flag == false)
#endif
    {
        sync_master_param.tx_align_enable = sync_master_param.tx_scheme.tx_align == SYNC_FEATURE_USE_DEFAULT
                                            ? SYNC_SYNC_SPEED_HIGH > 1000 : sync_master_param.tx_scheme.tx_align == SYNC_FEATURE_ENABLE;
        sync_master_param.early_high_prio_enable = sync_master_param.tx_scheme.early_high_prio ==
                                                   SYNC_FEATURE_USE_DEFAULT ? SYNC_SYNC_SPEED_HIGH == SYNC_SYNC_SPEED_HIGH_MIN :
                                                   sync_master_param.tx_scheme.early_high_prio == SYNC_FEATURE_ENABLE;
        sync_master_param.retrans_at_ack_enable = sync_master_param.tx_scheme.retrans_at_ack ==
                                                  SYNC_FEATURE_USE_DEFAULT ? (SYNC_SYNC_SPEED_HIGH == SYNC_SYNC_SPEED) &&
                                                  (SYNC_SYNC_SPEED_HIGH > SYNC_SYNC_SPEED_HIGH_MIN) : sync_master_param.tx_scheme.retrans_at_ack ==
                                                  SYNC_FEATURE_ENABLE;
        sync_master_param.tx_new_at_retrans_int_enable = sync_master_param.tx_scheme.tx_new_at_retrans_int
                                                         == SYNC_FEATURE_USE_DEFAULT ? false : sync_master_param.tx_scheme.tx_new_at_retrans_int ==
                                                         SYNC_FEATURE_ENABLE;
    }
    uint32_t speed = SYNC_SYNC_SPEED;
    sync_time_get(SYNC_TIME_PARAM_CONNECT_INTERVAL, &speed);
    APP_PRINT_INFO7("sync: speed %dus high %dus, scheme %d-%d-%d-%d, tx cb 0x%x", speed,
                    SYNC_SYNC_SPEED_HIGH,
                    sync_master_param.tx_align_enable, sync_master_param.early_high_prio_enable,
                    sync_master_param.retrans_at_ack_enable, sync_master_param.tx_new_at_retrans_int_enable,
                    sync_master_param.tx_prepare_cb);
}

void sync_master_enable(void)
{

}

sync_err_code_t sync_master_pair(void)
{
    if (sync_master_ctx.state != SYNC_STATE_IDLE || sync_master_ctx.pair_state != SYNC_PAIR_STATE_IDLE)
    {
        APP_PRINT_INFO2("sync: fail to start pair, state %d, pair state %d!", sync_master_ctx.state,
                        sync_master_ctx.pair_state);
        return SYNC_ERR_CODE_INVALID_STATE;
    }

    memset(&sync_master_ctx, 0, sizeof(sync_master_ctx));
    sync_master_ctx.state = SYNC_STATE_SYNC_SLOW;
    sync_master_ctx.pair_state = SYNC_PAIR_STATE_PAIRING;
    sync_master_ctx.phase = SYNC_MASTER_PAIR_PHASE_SEND_REQ;
    sync_master_ctx.seq_rx = 0xff;
    sync_master_init_tx_fifo();
    sync_ppt_set_tifs(true);
    sync_master_check_feature();

    APP_PRINT_INFO0("sync: start pair!");
    sync_channel_change(0);
    sync_acc_t acc = {.addr = SYNC_ACC_ADDR_BR};
    sync_acc_set(acc);

    uint16_t interval = SYNC_SYNC_SPEED;
#if SYNC_SUPPORT_MT1R
    WR_PPT_REG_FIELD(PRO_FIELD_LENGTH, hp_field_length_3_0, SYNC_HEADER_LEN);
    WR_PPT_REG_FIELD(PRO_HS_LENGTH, hs_field_length_4_0, 0);
    sync_ppt_set_rf_time();
    if (sync_ctx.mt1r.flag)
    {
        interval = 125;
        sync_ctx.conn_interval = 250;
        sync_hw_frtimer_init();
    }
#endif

    sync_set_header(0, 0);
    sync_pdu_t pdu;
    pdu.opcode = SYNC_OPCODE_PAIR_REQ;
    pdu.pair_req.interval = interval;
    ppt_push_tx_data(sizeof(sync_opcode_t) + sizeof(sync_pair_req_t), (uint8_t *)&pdu);

    {
        ppt_ptx_mode_ext_t param =
        {
            {
                .ack_mode = true,
                .periodic_mode = false,
                .periodic_interval = SYNC_PAIR_SPEED / 125 - 1
            },
            .retransmit_times = 0xffff
        };
        ppt_set_ptx_mode_ext(&param);
    }
    sync_master_ctx.busy = true;
    ppt_execute_instruction(PPT_HW_INSTRUCTION_PTX_ENABLE);
    sync_hw_timer_start(SYNC_PAIR_SPEED, sync_master_handle_sync_slow_early);
    return SYNC_ERR_CODE_SUCCESS;
}

sync_err_code_t sync_master_connect(sync_bond_info_t *bond_info)
{
    if (bond_info == NULL)
    {
        return SYNC_ERR_CODE_NULL_POINTER;
    }
    if (sync_master_ctx.state != SYNC_STATE_IDLE)
    {
        APP_PRINT_INFO1("sync: fail to start connect, state %d!", sync_master_ctx.state);
        return SYNC_ERR_CODE_INVALID_STATE;
    }

    memset(&sync_master_ctx, 0, sizeof(sync_master_ctx));
    sync_master_ctx.state = SYNC_STATE_SYNC_SLOW;
    sync_master_ctx.pair_state = SYNC_PAIR_STATE_PAIRED;
    sync_master_ctx.acc_data = bond_info->acc;
    sync_master_ctx.seq_rx = 0xff;
    sync_master_init_tx_fifo();
    sync_ppt_set_tifs(true);
    sync_master_check_feature();

    APP_PRINT_INFO1("sync: start connect 0x%08x!", bond_info->acc.addr);
    sync_channel_change(0);
    sync_acc_set(sync_master_ctx.acc_data);

    uint16_t interval = SYNC_SYNC_SPEED;
#if SYNC_SUPPORT_MT1R
    WR_PPT_REG_FIELD(PRO_FIELD_LENGTH, hp_field_length_3_0, SYNC_HEADER_LEN);
    WR_PPT_REG_FIELD(PRO_HS_LENGTH, hs_field_length_4_0, 0);
    sync_ppt_set_rf_time();
    if (sync_ctx.mt1r.flag)
    {
        interval = 125;
        sync_ctx.conn_interval = 250;
        sync_hw_frtimer_init();
    }
#endif

    sync_set_header(0, 0);
    sync_pdu_t pdu;
    pdu.opcode = SYNC_OPCODE_CONN_REQ;
    pdu.conn_req.interval = interval;
    ppt_push_tx_data(sizeof(sync_opcode_t) + sizeof(sync_conn_req_t), (uint8_t *)&pdu);

    {
        ppt_ptx_mode_ext_t param =
        {
            {
                .ack_mode = true,
                .periodic_mode = false,
                .periodic_interval = SYNC_SYNC_SLOW_SPEED / 125 - 1
            },
            .retransmit_times = 0xffff
        };
        ppt_set_ptx_mode_ext(&param);
    }
    sync_master_ctx.busy = true;
    ppt_execute_instruction(PPT_HW_INSTRUCTION_PTX_ENABLE);
    sync_hw_timer_start(SYNC_SYNC_SLOW_SPEED, sync_master_handle_sync_slow_early);
    return SYNC_ERR_CODE_SUCCESS;
}

void sync_master_stop(void)
{
    if (sync_master_ctx.state == SYNC_STATE_IDLE)
    {
        return;
    }

    uint32_t lock = sync_enter_critical();
    sync_master_ctx.state = SYNC_STATE_IDLE;
    sync_hw_timer_stop();
    sync_master_close();
    sync_master_msg_sending_free();
#if SYNC_SUPPORT_POWER_CONTROL
    if (sync_ctx.tx_power_adaptive)
    {
        sync_power_ctrl_stop();
    }
#endif
#if SYNC_SUPPORT_MT1R
    ppt_reg_handler(sync_master_ppt_isr_handler);
#endif
    memset(&sync_master_ctx, 0, sizeof(sync_master_ctx));
    if (sync_ctx.rte_req_cb)
    {
        sync_ctx.rte_req_cb(false);
    }
    sync_exit_critical(lock);
    APP_PRINT_INFO0("sync: stopped!");
}

void sync_master_deinit(void)
{

}

PPT_API_SECTION static bool sync_master_dlps_check(uint32_t *wakeup_time_diff)
{
    //APP_PRINT_ERROR4("sync: dlps check, state %d, pair state %d, hb slow %d busy %d",
    //                 sync_master_ctx.state, sync_master_ctx.pair_state,
    //                 sync_master_ctx.hb.slow, sync_master_ctx.busy);

    if (sync_master_ctx.state == SYNC_STATE_IDLE &&
        sync_master_ctx.pair_state == SYNC_PAIR_STATE_IDLE)
    {
        return true;
    }

    if (sync_master_ctx.state == SYNC_STATE_SYNC &&
        sync_master_ctx.pair_state == SYNC_PAIR_STATE_PAIRED &&
        sync_master_ctx.hb.slow && sync_master_ctx.busy == false)
    {
#if SYNC_SUPPORT_MT1R
        if (sync_master_ctx.quick.enable)
        {
            if (sync_master_ctx.hb.sleep)
            {
                return true;
            }
            if (sync_master_ctx.hb.sleep_cal)
            {
                return false;
            }
            time_debug_level_high(TIME_DEBUG_CHAN_DLPS);
            sync_master_ctx.hb.time_left1 = sync_hw_timer_get_left();
            sync_master_ctx.hb.sleep_time = sync_native_time_read();
            time_debug_level_low(TIME_DEBUG_CHAN_DLPS);
            if (sync_hw_timer_check_pending())
            {
                return false;
            }
            sync_master_ctx.hb.time_left = sync_master_ctx.hb.ce_cnt >= sync_master_ctx.quick.ce_cnt ?
                                           sync_master_ctx.hb.ce_cnt - sync_master_ctx.quick.ce_cnt : 0x10000 - sync_master_ctx.quick.ce_cnt +
                                           sync_master_ctx.hb.ce_cnt;
            sync_master_ctx.hb.time_left *= 1000;
            sync_master_ctx.hb.time_left += 625 + sync_master_ctx.hb.time_left1 - sync_master_ctx.quick.count *
                                            125;
            //APP_PRINT_ERROR4("sync: dlps check, time left %dus, hw left %d, ce %d, count %d",
            //                 sync_master_ctx.hb.time_left, sync_master_ctx.hb.time_left1, sync_master_ctx.quick.ce_cnt,
            //                 sync_master_ctx.quick.count);
            if (sync_master_ctx.hb.time_left <= SYNC_MASTER_HB_SLEEP_MIN_PERIOD_MT1R)
            {
                return false;
            }
            *wakeup_time_diff = us_to_rtc_clk(sync_master_ctx.hb.time_left - SYNC_MASTER_HB_SLEEP_OH, ROUND);
            sync_hw_timer_stop();
            sync_master_ctx.hb.sleep = true;
            return true;
        }
        else
#endif
        {
            sync_master_ctx.hb.time_left = sync_hw_timer_get_left();
            //APP_PRINT_ERROR1("sync: dlps check, time left %dus", sync_master_ctx.hb.time_left);
            if (sync_master_ctx.hb.time_left <= SYNC_MASTER_HB_SLEEP_MIN_PERIOD)
            {
                return false;
            }
            sync_master_ctx.hb.sleep_clk = platform_rtc_get_counter();
            *wakeup_time_diff = us_to_rtc_clk(sync_master_ctx.hb.time_left, ROUND);
            time_debug_level_high(TIME_DEBUG_CHAN_DLPS);
            time_debug_level_low(TIME_DEBUG_CHAN_DLPS);
            return true;
        }
    }

    return false;
}

PPT_API_SECTION static void sync_master_dlps_mac_enter(void)
{
#if SYNC_SUPPORT_MT1R
    sync_master_ctx.hb.sleep_mac = true;
#endif
}

PPT_API_SECTION static void sync_master_dlps_mac_exit(void)
{
#if SYNC_SUPPORT_MT1R
    if (sync_master_ctx.state == SYNC_STATE_SYNC &&
        sync_master_ctx.pair_state == SYNC_PAIR_STATE_PAIRED &&
        sync_master_ctx.hb.slow && sync_master_ctx.busy == false)
    {
        if (sync_master_ctx.quick.enable)
        {
            time_debug_level_high(TIME_DEBUG_CHAN_DLPS);
            uint64_t wakeup_time = sync_native_time_read();
            time_debug_level_low(TIME_DEBUG_CHAN_DLPS);
            uint32_t sleep_us = wakeup_time - sync_master_ctx.hb.sleep_time;
            bool rte_req = sync_master_ctx.hb.time_left <= sleep_us + SYNC_MASTER_HB_SLEEP_MIN_PERIOD_MT1R;
            uint32_t sleep_counter = 0;
            sync_master_ctx.hb.oversleep = sync_master_ctx.hb.time_left <= sleep_us;
            sync_master_ctx.hb.time_left = sleep_us;
            sleep_us += 12;
            if (sleep_us >= sync_master_ctx.hb.time_left1)
            {
                sleep_us -= sync_master_ctx.hb.time_left1;
                sleep_counter += 1;
            }
            sleep_counter += sleep_us / 125 + sync_master_ctx.quick.count;
            sleep_us = sleep_us % 125;
            sync_master_ctx.hb.time_left1 = sleep_us;
            sync_master_ctx.quick.ce_cnt += sleep_counter >> 3;
            sync_master_ctx.quick.count = sleep_counter & 0x7;
            sync_master_ctx.hb.sleep = false;
            sync_master_ctx.hb.sleep_mac = false;
            sync_master_ctx.hb.sleep_cal = true;
            time_debug_level_high(TIME_DEBUG_CHAN_DLPS);
            if (sleep_us == 0)
            {
                sync_master_ctx.quick.count -= 1;
                sync_master_handle_sync_early_hb_quick();
            }
            else
            {
                if (sleep_us >= 110)
                {
                    sync_master_ctx.quick.count += 1;
                    if (sync_master_ctx.quick.count == 8)
                    {
                        sync_master_ctx.quick.count = 0;
                        sync_master_ctx.quick.ce_cnt += 1;
                    }
                    sync_hw_timer_start(250 - sleep_us, sync_master_handle_sync_early_hb_quick);
                }
                else
                {
                    sync_hw_timer_start(125 - sleep_us, sync_master_handle_sync_early_hb_quick);
                }
            }
            time_debug_level_low(TIME_DEBUG_CHAN_DLPS);
            if (rte_req)
            {
                if (sync_ctx.rte_req_cb)
                {
                    sync_ctx.rte_req_cb(true);
                }
            }
        }
    }
#endif
}

PPT_API_SECTION static void sync_master_dlps_enter(void)
{

}

PPT_API_SECTION static void sync_master_dlps_exit(void)
{
    //    Pad_Config(P1_0, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_DOWN, PAD_OUT_ENABLE,
    //               PAD_OUT_LOW);
    //    Pad_Config(P1_0, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_DOWN, PAD_OUT_ENABLE,
    //               PAD_OUT_HIGH);
    //APP_PRINT_ERROR4("sync: dlps exit, state %d, pair state %d, hb slow %d busy %d",
    //                 sync_master_ctx.state, sync_master_ctx.pair_state,
    //                 sync_master_ctx.hb.slow, sync_master_ctx.busy);
    uint32_t lock = sync_enter_critical();
    if (sync_master_ctx.state == SYNC_STATE_SYNC &&
        sync_master_ctx.pair_state == SYNC_PAIR_STATE_PAIRED &&
        sync_master_ctx.hb.slow && sync_master_ctx.busy == false)
    {
#if SYNC_SUPPORT_MT1R
        if (sync_master_ctx.quick.enable == false)
#endif
        {
            time_debug_level_high(TIME_DEBUG_CHAN_DLPS);
            time_debug_level_low(TIME_DEBUG_CHAN_DLPS);
            uint32_t last_sleep_clk = sync_master_ctx.hb.sleep_clk;
            uint32_t last_wakeup_clk = platform_rtc_get_counter();
            uint32_t sleep_us = rtc_clk_to_us(clk_cnt_diff(last_sleep_clk, last_wakeup_clk, BIT64(32)), ROUND);
            //APP_PRINT_INFO2("sync: dlps exit, sleep %dus, time left %dus", sleep_us,
            //                sync_master_ctx.hb.time_left);
            if (sleep_us >= sync_master_ctx.hb.time_left)
            {
                sync_hw_timer_stop();
                sync_master_handle_hb_to();
            }
            else
            {
                sync_hw_timer_start(sync_master_ctx.hb.time_left - sleep_us, sync_master_handle_hb_to);
            }
        }
    }
    sync_exit_critical(lock);
    //Pad_Config(P1_0, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_DOWN, PAD_OUT_ENABLE,
    //            PAD_OUT_LOW);
}

void sync_master_dlps_init(void)
{
    ppt_dlps_init();
    ppt_dlps_reg_mac_check_cb(sync_master_dlps_check);
    ppt_dlps_reg_mac_enter_cb(sync_master_dlps_mac_enter);
    ppt_dlps_reg_mac_exit_cb(sync_master_dlps_mac_exit);
    ppt_dlps_reg_pf_enter_cb(sync_master_dlps_enter);
    ppt_dlps_reg_pf_exit_cb(sync_master_dlps_exit);
}
