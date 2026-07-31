/**
*****************************************************************************************
*     Copyright(c) 2020, Realtek Semiconductor Corporation. All rights reserved.
*****************************************************************************************
  * @file     ppt_sync.c
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
#include "ppt_sync_slave.h"
//#include "trace.h"
#include "os_mem.h"
#include "os_sync.h"
#include "rtl876x.h"
#include "rtl_rcc.h"
#include "rtl_nvic.h"
#include "rtl_enh_tim.h"
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include "vector_table.h"

LOG_MODULE_REGISTER(ppt_sync, PPT_SYNC_LOG_LEVEL);

#define SYNC_VERSION                            "v1.17.1"
#define SYNC_FTL_OFFSET                         0
#define SYNC_HW_TIMER                           ENH_TIM1
#define SYNC_HW_TIMER_IRQN                      Enhanced_Timer1_IRQn
#define SYNC_HW_TIMER_VECTORN                   Enhanced_Timer1_VECTORn //Enhanced_Timer1_Handler
#define SYNC_HW_TIMER1                          ENH_TIM2
#define SYNC_HW_TIMER1_IRQN                     Enhanced_Timer2_IRQn
#define SYNC_HW_TIMER1_VECTORN                  Enhanced_Timer2_VECTORn
#define SYNC_HW_TIMER_IP                        NVIC_GetPriority(BTMAC_IRQn)
#define SYNC_HW_TIMER_CLOCK                     40
#define SYNC_HW_TIMER_SHIFT_COMPENSATE          2
#define SYNC_HW_TIMER_SHIFT_TOLERANCE           5
#define SYNC_HW_FRTIMER                         ENH_TIM0
#define SYNC_HW_FRTIMER_IRQN                    Enhanced_Timer0_IRQn
#define SYNC_HW_FRTIMER_VECTORN                 Enhanced_Timer0_VECTORn

#define SYNC_TIME_DEBUG_TIMER1                  -1

sync_ctx_t sync_ctx =
{
    .conn_interval = SYNC_SYNC_SPEED_DV,
    .conn_interval_high = SYNC_SYNC_SPEED_HIGH_DV,
    .acc_br.addr = SYNC_ACC_ADDR_BR_DV,
    .tifs = {SYNC_TIFS0_DV, SYNC_TIFS1_DV},
};

typedef struct
{
    sync_bond_info_t bond_info;
} sync_nvm_bond_info_t;

typedef struct
{
    sync_nvm_bond_info_t nvm_bond_info;
} sync_nvm_t;

#define FAIL_MSG                "fail (err %d)\n"
#define SYNC_NVM_BOND_INFO_F    "pptsync/bondinfo"

struct direct_immediate_value {
	size_t len;
	void *dest;
	uint8_t fetched;
};

static int sync_direct_loader_immediate_value(const char *name, size_t len,
					 settings_read_cb read_cb, void *cb_arg,
					 void *param)
{
	const char *next;
	size_t name_len;
	int rc;
	struct direct_immediate_value *one_value =
					(struct direct_immediate_value *)param;

	name_len = settings_name_next(name, &next);

	if (name_len == 0) {
		if (len == one_value->len) {
			rc = read_cb(cb_arg, one_value->dest, len);
			if (rc >= 0) {
				one_value->fetched = 1;
				//printk("immediate load: OK.\n");
				return 0;
			}

			printk(FAIL_MSG, rc);
			return rc;
		}
		return -EINVAL;
	}

	/* other keys aren't served by the callback
	 * Return success in order to skip them
	 * and keep storage processing.
	 */
	return 0;
}

int sync_load_immediate_value(const char *name, void *dest, size_t len)
{
	int rc;
	struct direct_immediate_value dov;

	dov.fetched = 0;
	dov.len = len;
	dov.dest = dest;

	rc = settings_load_subtree_direct(name, sync_direct_loader_immediate_value,
					  (void *)&dov);
	if (rc == 0) {
		if (!dov.fetched) {
			rc = -ENOENT;
		}
	}

	return rc;
}

sync_err_code_t sync_nvm_get_bond_info(sync_bond_info_t *bond_info)
{
    sync_nvm_bond_info_t nvm_bond_info;
    if (0 != sync_load_immediate_value(SYNC_NVM_BOND_INFO_F, &nvm_bond_info, sizeof(sync_nvm_bond_info_t)))
    {
        return SYNC_ERR_CODE_NOT_FOUND;
    }

    *bond_info = nvm_bond_info.bond_info;
    return SYNC_ERR_CODE_SUCCESS;
}

sync_err_code_t sync_nvm_set_bond_info(sync_bond_info_t *bond_info)
{
    sync_nvm_bond_info_t nvm_bond_info = {};
    nvm_bond_info.bond_info = *bond_info;
    if (0 != settings_save_one(SYNC_NVM_BOND_INFO_F, &nvm_bond_info, sizeof(sync_nvm_bond_info_t)))
    {
        return SYNC_ERR_CODE_UNKNOWN;
    }
    return SYNC_ERR_CODE_SUCCESS;
}

sync_err_code_t sync_nvm_clear_bond_info(void)
{
    if(0 != settings_delete(SYNC_NVM_BOND_INFO_F))
    {
        return SYNC_ERR_CODE_UNKNOWN;
    }

    return SYNC_ERR_CODE_SUCCESS;
}

void sync_ppt_init(void)
{
    ppt_init();

    {
        ppt_phy_param_t param =
        {
            .bank = 0,
            .channel = 0,
            .rx_phy = PPT_PHY_TYPE_BLE_2M
        };
        ppt_set_phy_param(&param);
    }

    {
        ppt_phy_entry_param_t param =
        {
            .preamble_len = 1,
            .tx_phy = PPT_PHY_TYPE_BLE_2M
        };
        ppt_set_phy_entry_param(0, &param);
    }

    {
        ppt_white_entry_param_t param =
        {
            .len = 7,
            .value = {0x91, 0x53}
        };
        ppt_set_white_entry_param(0, &param);
    }

    {
        ppt_pkt_format_t param =
        {
            .addr_len = 4,
            .hp_len = SYNC_HEADER_LEN,
            .length_len = SYNC_LENGTH_LEN,
            .hs_len = 0,
            .header_order = PPT_FIELD_BIT_ORDER_LSB,
            .payload_order = PPT_FIELD_BIT_ORDER_LSB
        };
        ppt_set_pkt_format(&param);
    }

    {
        ppt_crc_param_t param =
        {
            .len = SYNC_CRC_LEN >> 3,
                                .include_addr = false
        };
        ppt_set_crc_param(&param);
    }

    {
        ppt_crc_entry_param_t param =
        {
            .value = {SYNC_CRC_POLY, SYNC_CRC_INIT}
        };
        ppt_set_crc_entry_param(0, &param);
    }

    PRO_RESET_CTRL_REG_TYPE reset_ctrl = {.d16 = RD_PPT_REG(PRO_RESET_CTRL_REG)};
    reset_ctrl.fw_rst_enable = 1;
    WR_PPT_REG(PRO_RESET_CTRL_REG, reset_ctrl.d16);

    /* enable length check */
    WR_PPT_REG_FIELD(PRO_RX_MATCH_CONTROL, use_maxlength_compare, 1);
    WR_PPT_REG_FIELD(PRO_MAX_LENGTH, max_length, SYNC_MSG_MAX_LEN);

    /* set rf time parameters */
    sync_ppt_set_rf_time();

#if SYNC_SUPPORT_MT1R
    {
        ppt_timing_latch_t param =
        {
            .tx_on = true,
            .acc_hit = true
        };
        ppt_set_timing_latch(&param);
    }
#endif
}

void sync_ppt_set_tifs(bool init)
{
    uint8_t sync_tifs = 0;
    if (sync_ctx.tifs[0] != sync_ctx.tifs[1])
    {
        sync_tifs = init ? sync_ctx.tifs[0] : (SYNC_SYNC_SPEED >= 500 ? sync_ctx.tifs[0] :
                                               sync_ctx.tifs[1]);
    }
    else
    {
        sync_tifs = sync_ctx.tifs[0];
    }

    ppt_set_tifs(sync_tifs);
}

void sync_ppt_set_rf_time(void)
{
    PRO_2M_TRX_ON_DELAY_TYPE trx_on_delay = {.d16 = RD_PPT_REG(PRO_2M_TRX_ON_DELAY)};
    trx_on_delay.pro_txon_delay_2m = 54;
    trx_on_delay.pro_rxon_delay_2m = 40;
    WR_PPT_REG(PRO_2M_TRX_ON_DELAY, trx_on_delay.d16);
    PRO_RX_TIMEOUT_DELAY_TYPE rx_to = {.d16 = RD_PPT_REG(PRO_RX_TIMEOUT_DELAY)};
    rx_to.pro_txon_early_2m = 1;
    rx_to.pro_rx_timeout_delay_us = 55;
    WR_PPT_REG(PRO_RX_TIMEOUT_DELAY, rx_to.d16);
}

#if SYNC_SUPPORT_MT1R
void sync_ppt_set_rf_time_mt1r(void)
{
    PRO_2M_TRX_ON_DELAY_TYPE trx_on_delay = {.d16 = RD_PPT_REG(PRO_2M_TRX_ON_DELAY)};
    trx_on_delay.pro_rxon_delay_2m = 40;
    trx_on_delay.pro_txon_delay_2m = 40;
    WR_PPT_REG(PRO_2M_TRX_ON_DELAY, trx_on_delay.d16);

    PRO_RX_TIMEOUT_DELAY_TYPE rx_to = {.d16 = RD_PPT_REG(PRO_RX_TIMEOUT_DELAY)};
    rx_to.pro_txon_early_2m = 0;
    rx_to.pro_rx_timeout_delay_us = 90;
    WR_PPT_REG(PRO_RX_TIMEOUT_DELAY, rx_to.d16);
}
#endif

bool sync_channel_set(uint8_t chan_num, uint8_t group_num, uint16_t freq[])
{
    if (chan_num % group_num != 0)
    {
        return false;
    }
    sync_ctx.chan_num = chan_num;
    sync_ctx.chan_group_num = group_num;
    if (sync_ctx.chan_cfg)
    {
        os_mem_free(sync_ctx.chan_cfg);
    }
    sync_ctx.chan_cfg = (sync_channel_cfg_t *)os_mem_alloc(RAM_TYPE_DATA_ON,
                                                           sizeof(sync_channel_cfg_t) * sync_ctx.chan_num);
    if (sync_ctx.chan_cfg == NULL)
    {
        return false;
    }
    for (uint8_t loop = 0; loop < sync_ctx.chan_num; loop++)
    {
        sync_ctx.chan_cfg[loop].freq = freq[loop];
        ppt_get_bank_channel(freq[loop], &sync_ctx.chan_cfg[loop].bank,
                             &sync_ctx.chan_cfg[loop].channel);
    }
    sync_ctx.chan_cur = 0;
    return true;
}

void sync_channel_change(uint8_t idx)
{
    if (idx >= sync_ctx.chan_num)
    {
        APP_PRINT_ERROR2("sync_channel_change: fail set to %d, total %d", idx, sync_ctx.chan_num);
        return;
    }
    uint8_t old_idx = sync_ctx.chan_cur;
    sync_ctx.chan_cur = idx;
    PRO_CHANNEL_BANK_TYPE ch_bank;
    ch_bank.bank = sync_ctx.chan_cfg[idx].bank;
    ch_bank.pro_channel = sync_ctx.chan_cfg[idx].channel;
    WR_PPT_REG(PRO_CHANNEL_BANK, ch_bank.d16);

#if SYNC_FRAME_BLE_COMPATIBLE
    uint32_t white_init = ppt_get_ble_white_init(ppt_get_ble_logic_chan_via_freq(
                                                     sync_ctx.chan_cfg[idx].freq));
#else
    uint32_t white_init = ppt_get_ble_white_init(idx);
#endif
    WR_PPT_REG(PRO_WHITE_INIT0_LOWER, white_init & 0xffff);
    WR_PPT_REG(PRO_WHITE_INIT0_UPPER, (white_init >> 16) & 0xffff);
    sync_ctx.chan_used_count = 0;
    sync_ctx.chan_miss_count = 0;

#ifdef SYNC_DEBUG
    sync_ctx.chan_cfg[idx].choose_times += 1;
    if (sync_ctx.chan_cfg[old_idx].fail_burst_count)
    {
        if (sync_ctx.chan_cfg[old_idx].fail_burst_count > sync_ctx.chan_cfg[old_idx].fail_burst_max)
        {
            sync_ctx.chan_cfg[old_idx].fail_burst_max = sync_ctx.chan_cfg[old_idx].fail_burst_count;
        }
    }
    sync_ctx.chan_cfg[old_idx].fail_burst_count = 0;
#endif
}

void sync_channel_change_next(void)
{
    sync_channel_change((sync_ctx.chan_cur + 1) % sync_ctx.chan_num);
}

void sync_channel_change_next_in_group(void)
{
    uint8_t chan_min, chan_max, chan_num, chan_next;
    chan_num = sync_ctx.chan_num / sync_ctx.chan_group_num;
    chan_min = (sync_ctx.chan_cur / chan_num) * chan_num;
    chan_max = chan_min + chan_num - 1;
    chan_next = sync_ctx.chan_cur >= chan_max ? chan_min : sync_ctx.chan_cur + 1;
    sync_channel_change(chan_next);
}

#ifdef SYNC_DEBUG
void sync_channel_statistic(bool ok)
{
    uint8_t idx = sync_ctx.chan_cur;
    if (sync_ctx.chan_cfg[idx].choose_times == 0)
    {
        sync_ctx.chan_cfg[idx].choose_times += 1;
    }
    sync_ctx.chan_cfg[idx].used_count += 1;
    if (ok == false)
    {
        sync_ctx.chan_cfg[idx].fail_count += 1;
        sync_ctx.chan_cfg[idx].fail_burst_count += 1;
    }
    else
    {
        if (sync_ctx.chan_cfg[idx].fail_burst_count)
        {
            if (sync_ctx.chan_cfg[idx].fail_burst_count > sync_ctx.chan_cfg[idx].fail_burst_max)
            {
                sync_ctx.chan_cfg[idx].fail_burst_max = sync_ctx.chan_cfg[idx].fail_burst_count;
            }
        }
        sync_ctx.chan_cfg[idx].fail_burst_count = 0;
    }
}

void sync_channel_clear_statistic(void)
{
    for (uint8_t idx = 0; idx < sync_ctx.chan_num; idx++)
    {
        sync_ctx.chan_cfg[idx].choose_times = 0;
        sync_ctx.chan_cfg[idx].used_count = 0;
        sync_ctx.chan_cfg[idx].fail_count = 0;
        sync_ctx.chan_cfg[idx].fail_burst_count = 0;
        sync_ctx.chan_cfg[idx].fail_burst_max = 0;
    }
}

void sync_channel_print_statistic(void)
{
    for (uint8_t idx = 0; idx < sync_ctx.chan_num; idx++)
    {
        APP_PRINT_INFO5("sync: channel statistic %d, choose %d, used %d, miss %d, burst max %d", idx,
                        sync_ctx.chan_cfg[idx].choose_times,
                        sync_ctx.chan_cfg[idx].used_count, sync_ctx.chan_cfg[idx].fail_count,
                        sync_ctx.chan_cfg[idx].fail_burst_max);
    }
}
#endif

void sync_acc_set(sync_acc_t acc)
{
    {
        ppt_tx_addr_t param;
        LE_DWORD2EXTRN(param.tx_addr, acc.addr);
        ppt_set_tx_addr(0, &param);
    }

    {
        ppt_rx_addr_t param =
        {
            .enable = true
        };
        LE_DWORD2EXTRN(param.rx_addr, acc.addr);
        ppt_set_rx_addr(0, &param);
    }

#if SYNC_FRAME_BLE_COMPATIBLE == 0
    uint8_t rx_addr[4];
    LE_DWORD2EXTRN(rx_addr, acc.addr);
    ppt_crc_eng_param_t crc_param =
    {
        .poly = SYNC_CRC_POLY,
        .init = SYNC_CRC_INIT,
        .length = SYNC_CRC_LEN,
        .bit_mode = 0,
        .input_endian = 0,
        .output_endian = 0
    };
    uint32_t init = ppt_crc_eng_cal(&crc_param, rx_addr, 4);
    {
        ppt_crc_entry_param_t param =
        {
            .value = {SYNC_CRC_POLY, SYNC_CRC_INIT}
        };
        param.value.init = init;
        ppt_set_crc_entry_param(0, &param);
    }
#endif
}

int sync_acc_check(sync_acc_t acc)
{
    uint32_t acc_addr = acc.addr;
    uint8_t count = 0;
    uint8_t transition = 0;
    bool last_bit = false;

    uint8_t addr0 = acc_addr & 0xff;
    if (addr0 == 0x55 || addr0 == 0xaa)
    {
        return __LINE__;
    }

    uint8_t addr1 = (acc_addr >> 8) & 0xff;
    if (addr1 == 0x55 || addr1 == 0xaa)
    {
        return __LINE__;
    }

    uint8_t loop = 1;
    for (; loop < 4; loop++)
    {
        uint8_t value = (acc_addr >> (8 * loop)) & 0xff;
        if (value != (acc_addr & 0xff))
        {
            break;
        }
    }
    if (loop >= 4)
    {
        return __LINE__;
    }

    for (loop = 0; loop < 32; loop++)
    {
        bool curr_bit = acc_addr & BIT(31 - loop);
        if (loop != 0)
        {
            if (curr_bit != last_bit)
            {
                transition += 1;
                count = 0;
            }
            else
            {
                count += 1;
                if (count >= 6)
                {
                    return __LINE__;
                }
            }

            if (loop == 5 && transition < 2)
            {
                return __LINE__;
            }
        }
        last_bit = curr_bit;
    }

    if (transition > 24)
    {
        return __LINE__;
    }

    return 0;
}

sync_acc_t sync_acc_gen_data(void)
{
    sync_acc_t acc;
#if SYNC_FRAME_BLE_COMPATIBLE
    acc.addr = SYNC_ACC_ADDR_BR + 1
               return acc;
#else
    while (1)
    {
        acc.addr = sync_rand();
        if (acc.addr != SYNC_ACC_ADDR_BR && 0 == sync_acc_check(acc))
        {
            APP_PRINT_INFO1("sync: acc gen 0x%08x", acc.addr);
            return acc;
        }
    }
#endif
}

void sync_acc_set_br(sync_acc_t acc_br)
{
    sync_ctx.acc_br = acc_br;
    APP_PRINT_INFO2("sync: acc set br 0x%08x (%d)", acc_br.addr, sync_acc_check(acc_br));
}

void sync_hw_timer_handler(void);
void sync_hw_timer_init(void)
{
    RCC_PeriphClockCmd(APBPeriph_ENHTIMER, APBPeriph_ENHTIMER_CLOCK, ENABLE);

    ENHTIM_InitTypeDef TIM_InitStruct;
    ENHTIM_StructInit(&TIM_InitStruct);
    TIM_InitStruct.ENHTIM_Mode                  = ENHTIM_MODE_PWM_MANUAL;
    ENHTIM_Init(SYNC_HW_TIMER, &TIM_InitStruct);

    NVIC_InitTypeDef NVIC_InitStruct;
    NVIC_InitStruct.NVIC_IRQChannel = SYNC_HW_TIMER_IRQN;
    NVIC_InitStruct.NVIC_IRQChannelPriority = SYNC_HW_TIMER_IP;
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStruct);
    RamVectorTableUpdate(SYNC_HW_TIMER_VECTORN, sync_hw_timer_handler);

    ENHTIM_ClearINTPendingBit(SYNC_HW_TIMER, ENHTIM_INT_TIM);
    ENHTIM_INTConfig(SYNC_HW_TIMER, ENHTIM_INT_TIM, ENABLE);
}

static PPT_ISR_SECTION void sync_hw_timer_disable(void)
{
    uint32_t lock = sync_enter_critical();
    ENHTIM_Cmd(SYNC_HW_TIMER, DISABLE);
    ENHTIM_ClearINTPendingBit(SYNC_HW_TIMER, ENHTIM_INT_TIM);
    NVIC_ClearPendingIRQ(SYNC_HW_TIMER_IRQN);
    sync_exit_critical(lock);
    sync_ctx.hw_timer_periodic = false;
    sync_hw_timer_impr_ip(false);
}

PPT_ISR_SECTION void sync_hw_timer_start(uint32_t period_us, sync_hw_timer_cb_t cb)
{
    sync_hw_timer_disable();
    sync_ctx.hw_timer_cb = cb;
#if SYNC_SUPPORT_MT1R
    sync_ctx.hw_timer_period = period_us;
    sync_ctx.hw_timer_shift_state = SYNC_HW_TIMER_SHIFT_STATE_IDLE;
#endif
    SYNC_HW_TIMER->ENHTIM_MAX_CNT = period_us * SYNC_HW_TIMER_CLOCK;
    ENHTIM_Cmd(SYNC_HW_TIMER, ENABLE);
    time_debug_level_low(TIME_DEBUG_CHAN_ISR_GPIO);
    time_debug_level_high(TIME_DEBUG_CHAN_ISR_GPIO);
}

PPT_ISR_SECTION void sync_hw_timer_restart(void)
{
    time_debug_level_low(TIME_DEBUG_CHAN_ISR_GPIO);
    time_debug_level_high(TIME_DEBUG_CHAN_ISR_GPIO);
    sync_hw_timer_disable();
    ENHTIM_Cmd(SYNC_HW_TIMER, ENABLE);
}

PPT_ISR_SECTION void sync_hw_timer_stop(void)
{
    time_debug_level_low(TIME_DEBUG_CHAN_ISR_GPIO);
    sync_hw_timer_disable();
    sync_ctx.hw_timer_cb = NULL;
#if SYNC_SUPPORT_MT1R
    sync_ctx.hw_timer_shift_state = SYNC_HW_TIMER_SHIFT_STATE_IDLE;
#endif
}

PPT_ISR_SECTION uint32_t sync_hw_timer_get_left(void)
{
    uint32_t curr = SYNC_HW_TIMER->ENHTIM_CUR_CNT / SYNC_HW_TIMER_CLOCK;
    return curr;
}

PPT_ISR_SECTION bool sync_hw_timer_check_pending(void)
{
    return NVIC_GetPendingIRQ(SYNC_HW_TIMER_IRQN);
}

PPT_ISR_SECTION void sync_hw_timer_set_periodic(bool enable)
{
    sync_ctx.hw_timer_periodic = enable;
}

PPT_ISR_SECTION void sync_hw_timer_impr_ip(bool enable)
{
    NVIC_SetPriority(SYNC_HW_TIMER_IRQN, SYNC_HW_TIMER_IP - enable);
}

PPT_ISR_SECTION void sync_hw_timer_handler(void)
{
    time_debug_level_low(TIME_DEBUG_CHAN_ISR_GPIO);
    time_debug_level_high(TIME_DEBUG_CHAN_TIMING);
    sync_hw_timer_cb_t tmp_cb = sync_ctx.hw_timer_cb;

#if SYNC_SUPPORT_MT1R
    uint32_t period = (sync_ctx.hw_timer_periodic &&
                       (sync_ctx.hw_timer_shift_state == SYNC_HW_TIMER_SHIFT_STATE_SHIFTING)) ?
                      sync_ctx.hw_timer_shift_period - SYNC_HW_TIMER_SHIFT_COMPENSATE : sync_ctx.hw_timer_period;
    uint32_t left = sync_hw_timer_get_left();
    sync_ctx.hw_timer_pend_time = left == 0 ? 0 : period - left;
#if SYNC_TRACE
    if (sync_ctx.hw_timer_pend_time > sync_ctx.hw_timer_pend_time_max)
    {
        sync_ctx.hw_timer_pend_time_max = sync_ctx.hw_timer_pend_time;
    }
#endif
#endif

    if (sync_ctx.hw_timer_periodic == false)
    {
        sync_ctx.hw_timer_cb = NULL;
        /* Disable */
        ENHTIM_Cmd(SYNC_HW_TIMER, DISABLE);
    }
    else
    {
#if SYNC_SUPPORT_MT1R
        if (sync_ctx.hw_timer_shift_state == SYNC_HW_TIMER_SHIFT_STATE_PENDING)
        {
            /* in case already pend too long, shift next time */
            if (sync_ctx.hw_timer_pend_time < SYNC_HW_TIMER_SHIFT_TOLERANCE)
            {
                sync_ctx.hw_timer_shift_state = SYNC_HW_TIMER_SHIFT_STATE_SHIFTING;
                sync_ctx.hw_timer_shift_period = sync_ctx.hw_timer_period + sync_ctx.hw_timer_shift_time;
                ENHTIM_Cmd(SYNC_HW_TIMER, DISABLE);
                SYNC_HW_TIMER->ENHTIM_MAX_CNT = (sync_ctx.hw_timer_shift_period - SYNC_HW_TIMER_SHIFT_COMPENSATE) *
                                                SYNC_HW_TIMER_CLOCK;
                ENHTIM_Cmd(SYNC_HW_TIMER, ENABLE);
                time_debug_level_high(TIME_DEBUG_CHAN_ISR_GPIO);
                time_debug_level_low(TIME_DEBUG_CHAN_ISR_GPIO);
            }
        }
        else if (sync_ctx.hw_timer_shift_state == SYNC_HW_TIMER_SHIFT_STATE_SHIFTING)
        {
            uint32_t new_period = sync_ctx.hw_timer_period;
            if (sync_ctx.hw_timer_pend_time < SYNC_HW_TIMER_SHIFT_TOLERANCE)
            {
                sync_ctx.hw_timer_shift_state = SYNC_HW_TIMER_SHIFT_STATE_IDLE;
            }
            else
            {
                if (sync_ctx.hw_timer_pend_time + 20 < sync_ctx.hw_timer_period)
                {
                    /* re-enter shift state */
                    new_period = sync_ctx.hw_timer_period - sync_ctx.hw_timer_pend_time;
                    sync_ctx.hw_timer_shift_time = sync_ctx.hw_timer_pend_time;
                    sync_ctx.hw_timer_shift_period = new_period;
                    new_period -= SYNC_HW_TIMER_SHIFT_COMPENSATE;
                    time_debug_level_high(TIME_DEBUG_CHAN_ISR_GPIO);
                    time_debug_level_low(TIME_DEBUG_CHAN_ISR_GPIO);
                }
                else
                {
                    /* can't handle this case */
                    sync_ctx.hw_timer_shift_state = SYNC_HW_TIMER_SHIFT_STATE_IDLE;
                }
            }
            ENHTIM_Cmd(SYNC_HW_TIMER, DISABLE);
            SYNC_HW_TIMER->ENHTIM_MAX_CNT = new_period * SYNC_HW_TIMER_CLOCK;
            ENHTIM_Cmd(SYNC_HW_TIMER, ENABLE);
        }
#endif
        time_debug_level_high(TIME_DEBUG_CHAN_ISR_GPIO);
    }
    /* Clear interrupt */
    ENHTIM_ClearINTPendingBit(SYNC_HW_TIMER, ENHTIM_INT_TIM);
    NVIC_ClearPendingIRQ(SYNC_HW_TIMER_IRQN);
    if (tmp_cb)
    {
        //APP_PRINT_INFO1("sync_hw_timer_handler: callback 0x%x", tmp_cb);
        tmp_cb();
    }
    time_debug_level_low(TIME_DEBUG_CHAN_TIMING);
}

#if SYNC_SUPPORT_MT1R
PPT_ISR_SECTION void sync_hw_timer_shift(int32_t time)
{
    bool shift_pend = false;
    bool timer_pend = false;
    uint32_t left = 0;
    int32_t new_period = 0;
    if (sync_ctx.hw_timer_shift_state != SYNC_HW_TIMER_SHIFT_STATE_IDLE)
    {
        if (SYNC_ISR_DEBUG)
        {
            APP_PRINT_WARN1("sync_isr: timer shift state error %d", sync_ctx.hw_timer_shift_state);
        }
        return;
    }
    if (time == 0 || (time < 0 && sync_ctx.hw_timer_period <= -time))
    {
        if (SYNC_ISR_DEBUG)
        {
            APP_PRINT_WARN2("sync_isr: timer shift time error, period %d, shift %d", sync_ctx.hw_timer_period,
                            time);
        }
        return;
    }

    left = sync_hw_timer_get_left();
    timer_pend = sync_hw_timer_check_pending();
    if (timer_pend)
    {
        shift_pend = true;
    }
    else
    {
        new_period = left + time;
        if (left < 10 || (left + 2) >= sync_ctx.hw_timer_period || new_period < 20)
        {
            shift_pend = true;
        }
    }

    if (shift_pend)
    {
        sync_ctx.hw_timer_shift_state = SYNC_HW_TIMER_SHIFT_STATE_PENDING;
        sync_ctx.hw_timer_shift_time = time;
    }
    else
    {
        time_debug_level_low(TIME_DEBUG_CHAN_ISR_GPIO);
        sync_ctx.hw_timer_shift_state = SYNC_HW_TIMER_SHIFT_STATE_SHIFTING;
        sync_ctx.hw_timer_shift_period = new_period;
        sync_ctx.hw_timer_shift_time = time;
        ENHTIM_Cmd(SYNC_HW_TIMER, DISABLE);
        ENHTIM_ClearINTPendingBit(SYNC_HW_TIMER, ENHTIM_INT_TIM);
        NVIC_ClearPendingIRQ(SYNC_HW_TIMER_IRQN);
        SYNC_HW_TIMER->ENHTIM_MAX_CNT = (new_period - SYNC_HW_TIMER_SHIFT_COMPENSATE) * SYNC_HW_TIMER_CLOCK;
        ENHTIM_Cmd(SYNC_HW_TIMER, ENABLE);
        time_debug_level_high(TIME_DEBUG_CHAN_ISR_GPIO);
        time_debug_level_low(TIME_DEBUG_CHAN_ISR_GPIO);
        time_debug_level_high(TIME_DEBUG_CHAN_ISR_GPIO);
    }
    //APP_PRINT_INFO4("sync_isr: shift %d, left %d, pending %d, after state %d", time, left, timer_pend, sync_ctx.hw_timer_shift_state);
}

void sync_hw_timer1_handler(void);
void sync_hw_timer1_init(void)
{
    RCC_PeriphClockCmd(APBPeriph_ENHTIMER, APBPeriph_ENHTIMER_CLOCK, ENABLE);
    ENHTIM_InitTypeDef TIM_InitStruct;
    ENHTIM_StructInit(&TIM_InitStruct);
    TIM_InitStruct.ENHTIM_Mode                  = ENHTIM_MODE_PWM_MANUAL;
    ENHTIM_Init(SYNC_HW_TIMER1, &TIM_InitStruct);

    NVIC_InitTypeDef NVIC_InitStruct;
    NVIC_InitStruct.NVIC_IRQChannel = SYNC_HW_TIMER1_IRQN;
    NVIC_InitStruct.NVIC_IRQChannelPriority = SYNC_HW_TIMER_IP;
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStruct);

    ENHTIM_ClearINTPendingBit(SYNC_HW_TIMER1, ENHTIM_INT_TIM);
    ENHTIM_INTConfig(SYNC_HW_TIMER1, ENHTIM_INT_TIM, ENABLE);
    RamVectorTableUpdate(SYNC_HW_TIMER1_VECTORN, sync_hw_timer1_handler);
}

static PPT_ISR_SECTION void sync_hw_timer1_disable(void)
{
    uint32_t lock = sync_enter_critical();
    ENHTIM_Cmd(SYNC_HW_TIMER1, DISABLE);
    ENHTIM_ClearINTPendingBit(SYNC_HW_TIMER1, ENHTIM_INT_TIM);
    NVIC_ClearPendingIRQ(SYNC_HW_TIMER1_IRQN);
    sync_exit_critical(lock);
}

PPT_ISR_SECTION void sync_hw_timer1_start(uint32_t period_us, sync_hw_timer_cb_t cb)
{
    sync_hw_timer1_disable();
    sync_ctx.hw_timer1_cb = cb;
    SYNC_HW_TIMER1->ENHTIM_MAX_CNT = period_us * SYNC_HW_TIMER_CLOCK;
    ENHTIM_Cmd(SYNC_HW_TIMER1, ENABLE);
    time_debug_level_low(SYNC_TIME_DEBUG_TIMER1);
    time_debug_level_high(SYNC_TIME_DEBUG_TIMER1);
}

PPT_ISR_SECTION void sync_hw_timer1_stop(void)
{
    if (sync_ctx.hw_timer1_cb)
    {
        time_debug_level_low(SYNC_TIME_DEBUG_TIMER1);
        sync_hw_timer1_disable();
        sync_ctx.hw_timer1_cb = NULL;
    }
}

PPT_ISR_SECTION void sync_hw_timer1_handler(void)
{
    time_debug_level_low(SYNC_TIME_DEBUG_TIMER1);
    sync_hw_timer_cb_t tmp_cb = sync_ctx.hw_timer1_cb;
    sync_ctx.hw_timer1_cb = NULL;
    ENHTIM_Cmd(SYNC_HW_TIMER1, DISABLE);

    ENHTIM_ClearINTPendingBit(SYNC_HW_TIMER1, ENHTIM_INT_TIM);
    NVIC_ClearPendingIRQ(SYNC_HW_TIMER1_IRQN);

    if (tmp_cb)
    {
        //APP_PRINT_INFO1("sync_hw_timer1_handler: callback 0x%x", tmp_cb);
        tmp_cb();
    }
}
#endif

void sync_hw_frtimer_init(void)
{
    RCC_PeriphClockCmd(APBPeriph_ENHTIMER, APBPeriph_ENHTIMER_CLOCK, ENABLE);
    ENHTIM_InitTypeDef TIM_InitStruct;
    ENHTIM_StructInit(&TIM_InitStruct);
    TIM_InitStruct.ENHTIM_ClockDiv              = ENHTIM_CLOCK_DIVIDER_1;
    TIM_InitStruct.ENHTIM_Mode                  = ENHTIM_MODE_FreeRun;
    TIM_InitStruct.ENHTIM_LatchCountEn[1]       = ENABLE;
    TIM_InitStruct.ENHTIM_LatchCountEn[2]       = ENABLE;
    ENHTIM_Init(SYNC_HW_FRTIMER, &TIM_InitStruct);
    sync_hw_frtimer_start();
}

void sync_hw_frtimer_start(void)
{
    ENHTIM_Cmd(SYNC_HW_FRTIMER, ENABLE);
}

void sync_hw_frtimer_stop(void)
{
    ENHTIM_Cmd(SYNC_HW_FRTIMER, DISABLE);
}

PPT_API_SECTION uint32_t sync_hw_frtimer_get_count(void)
{
    return ENHTIM_GetCurrentCount(SYNC_HW_FRTIMER);
}

PPT_API_SECTION int32_t sync_hw_frtimer_time_diff(uint32_t time1, uint32_t time2)
{
    int32_t diff_us;
    uint32_t abs = time1 >= time2 ? time1 - time2 : time2 - time1;
    if (abs > (SYNC_HW_FRTIMER_MAX_VALUE >> 1))
    {
        if (time1 >= time2)
        {
            diff_us = time1 - time2 - SYNC_HW_FRTIMER_MAX_VALUE - 1;
        }
        else
        {
            diff_us = SYNC_HW_FRTIMER_MAX_VALUE + 1 - time2 + time1;
        }
    }
    else
    {
        diff_us = time1 - time2;
    }

    return diff_us / SYNC_HW_FRTIMER_CLOCK;
}

static void sync_sw_timer_cb(void *timer)
{
    uint32_t timer_id = 0;
    os_timer_id_get(&timer, &timer_id);
    if (timer_id)
    {
        sync_sw_timer_t *sync_timer = (sync_sw_timer_t *)timer_id;
        //uint32_t lock = sync_enter_critical();
        sync_sw_timer_cb_t cb = sync_timer->cb;
        if (cb)
        {
            cb(sync_timer);
        }
        //sync_exit_critical(lock);
    }
}

void sync_sw_timer_start(sync_sw_timer_t *sync_timer, uint32_t interval_ms, sync_sw_timer_cb_t cb)
{
    sync_timer->cb = cb;
    if (sync_timer->timer == NULL)
    {
        os_timer_create(&sync_timer->timer, "sync", (uint32_t)sync_timer, interval_ms, true,
                        sync_sw_timer_cb);
        os_timer_start(&sync_timer->timer);
    }
    else
    {
        os_timer_restart(&sync_timer->timer, interval_ms);
    }
}

void sync_sw_timer_stop(sync_sw_timer_t *sync_timer)
{
    if (sync_timer->timer)
    {
        os_timer_delete(&sync_timer->timer);
        sync_timer->timer = NULL;
    }
    sync_timer->cb = NULL;
}

PPT_API_SECTION uint64_t sync_native_time_read(void)
{
    uint32_t clk;
    uint16_t cnt;
    do
    {
        clk = BB_read_native_clock();
        cnt = BB_read_native_counter();
    }
    while (clk != BB_read_native_clock());
    uint64_t time = (((uint64_t)(clk & 0xfffffffe) * 625) >> 1) + 624 - cnt;
    return time;
}

void sync_event_cb_reg(sync_event_cb_t cb)
{
    sync_ctx.event_cb = cb;
}

void sync_msg_reg_receive_cb(sync_msg_receive_cb_t cb)
{
    sync_ctx.msg_receive_cb = cb;
}

void sync_msg_set_quota(uint8_t msg_quota[SYNC_MSG_TYPE_NUM])
{
    memcpy(sync_ctx.msg_quota, msg_quota, SYNC_MSG_TYPE_NUM);
}

bool sync_msg_set_quota_by_type(sync_msg_type_t type, uint8_t quota)
{
    if (type >= SYNC_MSG_TYPE_NUM)
    {
        return false;
    }

    sync_ctx.msg_quota[type] = quota;
    return true;
}

void sync_msg_set_finite_retrans(uint8_t msg_retrans_num)
{
    sync_ctx.msg_finite_retrans_num = msg_retrans_num;
}

void sync_msg_set_dynamic_retrans(uint8_t msg_retrans_num_lower, uint8_t msg_retrans_num_upper)
{
    if (msg_retrans_num_lower <= msg_retrans_num_upper)
    {
        sync_ctx.msg_dynamic_retrans_num_lower = msg_retrans_num_lower;
        sync_ctx.msg_dynamic_retrans_num_upper = msg_retrans_num_upper;
    }
}

void sync_msg_init(void)
{
    sync_ctx.msg_quota_total = 0;
    for (uint8_t loop = 0; loop < SYNC_MSG_TYPE_NUM; loop++)
    {
        sync_ctx.msg_quota_total += sync_ctx.msg_quota[loop];
    }

    if (sync_ctx.msg_quota_total)
    {
        sync_ctx.msg_quota_total += 1;
        if (sync_ctx.pmsg_buff == NULL)
        {
            sync_msg_t *pmsg = (sync_msg_t *)os_mem_alloc(RAM_TYPE_BUFFER_ON,
                                                          sync_ctx.msg_quota_total * sizeof(sync_msg_t));
            for (uint8_t loop = 0; loop < sync_ctx.msg_quota_total; loop++)
            {
                plt_list_push(&sync_ctx.msg_free, &pmsg[loop]);
            }
            sync_ctx.pmsg_buff = pmsg;
        }
    }
}

void sync_msg_reset(void)
{
#if 1
    /* print message queue state for debug */
    APP_PRINT_INFO4("sync: msg reset, total %d free %d sending %d, buff addr 0x%x",
                    sync_ctx.msg_quota_total,
                    sync_ctx.msg_free.count, sync_ctx.msg_send.count, sync_ctx.pmsg_buff);
    for (uint8_t loop = 0; loop < SYNC_MSG_TYPE_NUM; loop++)
    {
        APP_PRINT_INFO3("sync: msg reset, type %d quota %d count %d", loop, sync_ctx.msg_quota[loop],
                        sync_ctx.msg_count[loop]);
    }

    {
        uint8_t msg_count = 0;
        sync_msg_t *pmsg = (sync_msg_t *)sync_ctx.msg_send.pfirst;
        while (pmsg)
        {
            msg_count += 1;
            APP_PRINT_INFO3("sync: msg reset, send queue count %d ptr 0x%x next 0x%x", msg_count, pmsg,
                            pmsg->pnext);
            pmsg = (sync_msg_t *)pmsg->pnext;
        }
    }

    {
        uint8_t msg_count = 0;
        sync_msg_t *pmsg = (sync_msg_t *)sync_ctx.msg_free.pfirst;
        while (pmsg)
        {
            msg_count += 1;
            APP_PRINT_INFO3("sync: msg reset, free queue count %d ptr 0x%x next 0x%x", msg_count, pmsg,
                            pmsg->pnext);
            pmsg = (sync_msg_t *)pmsg->pnext;
        }
    }
#endif

    for (uint8_t loop = 0; loop < SYNC_MSG_TYPE_NUM; loop++)
    {
        sync_ctx.msg_count[loop] = 0;
    }
    sync_ctx.msg_send.pfirst = NULL;
    sync_ctx.msg_send.plast = NULL;
    sync_ctx.msg_send.count = 0;
    sync_ctx.msg_free.pfirst = NULL;
    sync_ctx.msg_free.plast = NULL;
    sync_ctx.msg_free.count = 0;
    sync_msg_t *pmsg = sync_ctx.pmsg_buff;
    for (uint8_t loop = 0; loop < sync_ctx.msg_quota_total; loop++)
    {
        plt_list_push(&sync_ctx.msg_free, &pmsg[loop]);
    }
}

PPT_ISR_SECTION void sync_msg_handle_tx_done(void *para1, uint32_t para2)
{
    sync_msg_t *pmsg = (sync_msg_t *)para1;
    uint32_t lock = sync_enter_critical();
    sync_ctx.msg_count[pmsg->type] -= 1;
    sync_exit_critical(lock);
    if (pmsg->send_cb)
    {
        pmsg->send_cb(pmsg->type, pmsg->data, pmsg->len, &pmsg->send_info);
    }
    lock = sync_enter_critical();
    plt_list_push(&sync_ctx.msg_free, pmsg);
    sync_exit_critical(lock);
}

PPT_API_SECTION sync_err_code_t sync_msg_send(sync_msg_type_t type, uint8_t *data, uint16_t len,
                                              sync_msg_send_cb_t send_cb)
{
    if (type >= SYNC_MSG_TYPE_NUM || data == NULL)
    {
        return SYNC_ERR_CODE_INVALID_PARAM;
    }

    if (len == 0 || len > SYNC_MSG_MAX_LEN)
    {
        return SYNC_ERR_CODE_INVALID_LENGTH;
    }

    if (sync_ctx.msg_count[type] >= sync_ctx.msg_quota[type])
    {
        return SYNC_ERR_CODE_FULL_QUEUE;
    }

    uint32_t lock = sync_enter_critical();
    sync_msg_t *pmsg = (sync_msg_t *)plt_list_pop(&sync_ctx.msg_free);
    sync_exit_critical(lock);
    if (pmsg == NULL)
    {
        return SYNC_ERR_CODE_UNKNOWN;
    }

    pmsg->type = type;
    memcpy(pmsg->data, data, len);
    pmsg->len = len;
    pmsg->send_cb = send_cb;

    lock = sync_enter_critical();
    plt_list_push(&sync_ctx.msg_send, (void *)pmsg);
    sync_ctx.msg_count[type] += 1;
    uint32_t list_size = sync_ctx.msg_send.count;
    if (list_size == 1)
    {
#if SYNC_ROLE_MASTER_SUPPORT
        if (sync_ctx.role == SYNC_ROLE_MASTER)
        {
            sync_master_handle_tx_trigger();
        }
#endif
#if SYNC_ROLE_SLAVE_SUPPORT
        if (sync_ctx.role == SYNC_ROLE_SLAVE)
        {
            sync_slave_handle_tx_trigger();
        }
#endif
    }
    sync_exit_critical(lock);

    return SYNC_ERR_CODE_SUCCESS;
}

uint8_t sync_msg_get_available_number(sync_msg_type_t type)
{
    if (type >= SYNC_MSG_TYPE_NUM && type != SYNC_MSG_TYPE_ALL)
    {
        return 0;
    }
    uint8_t count = 0;
    if (type == SYNC_MSG_TYPE_ALL)
    {
        for (uint8_t loop = 0; loop < SYNC_MSG_TYPE_NUM; loop++)
        {
            count += sync_ctx.msg_quota[loop] - sync_ctx.msg_count[loop];
        }
    }
    else
    {
        count = sync_ctx.msg_quota[type] - sync_ctx.msg_count[type];
    }
    return count;
}

uint8_t sync_msg_flush(sync_msg_type_t type)
{
    uint8_t count = 0;
    if (type >= SYNC_MSG_TYPE_NUM && type != SYNC_MSG_TYPE_ALL)
    {
        return 0;
    }
    uint32_t lock = sync_enter_critical();
    sync_msg_t *pmsg_prev = NULL;
    sync_msg_t *pmsg = (sync_msg_t *)sync_ctx.msg_send.pfirst;
    while (pmsg)
    {
        if (type == SYNC_MSG_TYPE_ALL || type == pmsg->type)
        {
            plt_list_delete(&sync_ctx.msg_send, pmsg_prev, pmsg);
            sync_msg_t *pmsg_next = (sync_msg_t *)pmsg->pnext;
            if (pmsg->send_cb)
            {
                pmsg->send_info.res = SYNC_SEND_RESULT_FLUSHED;
                sync_pendcall(sync_msg_handle_tx_done, pmsg, 0);
            }
            else
            {
                sync_ctx.msg_count[pmsg->type] -= 1;
                plt_list_push(&sync_ctx.msg_free, pmsg);
            }
            pmsg = pmsg_next;
            count += 1;
        }
        else
        {
            pmsg_prev = pmsg;
            pmsg = (sync_msg_t *)pmsg->pnext;
        }
    }
#if SYNC_ROLE_MASTER_SUPPORT
    if (sync_ctx.role == SYNC_ROLE_MASTER)
    {
        sync_master_msg_flush(type);
    }
#endif
    sync_exit_critical(lock);
    return count;
}

PPT_API_SECTION uint32_t sync_rand(void)
{
    return ((uint32_t)rand() & 0xffffff) + ((uint32_t)rand() << 24);
}

PPT_API_SECTION uint32_t sync_enter_critical(void)
{
    uint32_t flag = __get_PRIMASK();
    __disable_irq();
    return flag;
}

PPT_API_SECTION void sync_exit_critical(uint32_t flag)
{
    if (flag == 0)
    {
        __enable_irq();
    }
}

void sync_init(sync_role_t role)
{
    APP_PRINT_INFO1("sync: version "SYNC_VERSION", role %d", role);

    uint32_t random_seed_value = platform_random(0xffffffff);
    srand(random_seed_value);

    uint16_t freq[] = SYNC_CHANNEL_FREQUENCY;
    if (SYNC_CHANNEL_NUM_DV != sizeof(freq) / sizeof(freq[0]))
    {
        APP_PRINT_ERROR2("sync: channel wrong %d %d", SYNC_CHANNEL_NUM_DV, sizeof(freq) / sizeof(freq[0]));
        return;
    }
    sync_channel_set(SYNC_CHANNEL_NUM_DV, SYNC_CHANNEL_GROUP_NUM_DV, freq);

    sync_ctx.crc_len = SYNC_CRC_LEN_DV;
    sync_ctx.crc_poly = SYNC_CRC_POLY_DV;
    sync_ctx.crc_init = SYNC_CRC_INIT_DV;

    sync_ctx.conn_interval = SYNC_SYNC_SPEED_DV;
    sync_ctx.slave_hopping_speed = SYNC_SLAVE_HOPPING_SPEED_DV;

    sync_ctx.tx_power_adaptive = false;
    sync_ctx.tx_power_dbm_max = 8;
    sync_ctx.tx_power_dbm_min = 8;

    sync_ctx.msg_quota[SYNC_MSG_TYPE_ONESHOT] = 2;
    sync_ctx.msg_quota[SYNC_MSG_TYPE_FINITE_RETRANS] = 2;
    sync_ctx.msg_quota[SYNC_MSG_TYPE_DYNAMIC_RETRANS] = 2;
    sync_ctx.msg_quota[SYNC_MSG_TYPE_INFINITE_RETRANS] = 4;
    sync_ctx.msg_finite_retrans_num = 2;
    sync_ctx.msg_dynamic_retrans_num_lower = 0;
    sync_ctx.msg_dynamic_retrans_num_upper = SYNC_MSG_RETRANS_NUM_INFINITE;

    sync_ctx.log_isr = 0;
    sync_ctx.pair_rssi_th = -128;
    sync_ctx.hb_to_times = SYNC_HB_TO_TIMES_DV;
    sync_ctx.cca_times = SYNC_CCA_TIMES_DV;

    sync_ctx.role = role;

#if SYNC_ROLE_MASTER_SUPPORT
    if (sync_ctx.role == SYNC_ROLE_MASTER)
    {
        sync_master_init();
    }
#endif
#if SYNC_ROLE_SLAVE_SUPPORT
    if (sync_ctx.role == SYNC_ROLE_SLAVE)
    {
        sync_slave_init();
    }
#endif
}

void sync_enable(void)
{
    sync_msg_init();

#if SYNC_ROLE_MASTER_SUPPORT
    if (sync_ctx.role == SYNC_ROLE_MASTER)
    {
        sync_master_enable();
    }
#endif
#if SYNC_ROLE_SLAVE_SUPPORT
    if (sync_ctx.role == SYNC_ROLE_SLAVE)
    {
        sync_slave_enable();
    }
#endif
}

sync_err_code_t sync_pair(void)
{
#ifdef SYNC_ROLE_MASTER_SUPPORT
    if (sync_ctx.role == SYNC_ROLE_MASTER)
    {
        return sync_master_pair();
    }
#endif
#ifdef SYNC_ROLE_SLAVE_SUPPORT
    if (sync_ctx.role == SYNC_ROLE_SLAVE)
    {
        return sync_slave_pair();
    }
#endif
    return SYNC_ERR_CODE_UNKNOWN;
}

sync_err_code_t sync_connect(sync_bond_info_t *bond_info)
{
#if SYNC_ROLE_MASTER_SUPPORT
    if (sync_ctx.role == SYNC_ROLE_MASTER)
    {
        return sync_master_connect(bond_info);
    }
#endif
#if SYNC_ROLE_SLAVE_SUPPORT
    if (sync_ctx.role == SYNC_ROLE_SLAVE)
    {
        return sync_slave_connect(bond_info);
    }
#endif
    return SYNC_ERR_CODE_UNKNOWN;
}

void sync_stop(void)
{
#if SYNC_ROLE_MASTER_SUPPORT
    if (sync_ctx.role == SYNC_ROLE_MASTER)
    {
        sync_master_stop();
    }
#endif
#if SYNC_ROLE_SLAVE_SUPPORT
    if (sync_ctx.role == SYNC_ROLE_SLAVE)
    {
        sync_slave_stop();
    }
#endif
}

void sync_deinit(void)
{
    if (sync_ctx.chan_cfg)
    {
        os_mem_free(sync_ctx.chan_cfg);
        sync_ctx.chan_cfg = NULL;
    }

#if SYNC_ROLE_MASTER_SUPPORT
    if (sync_ctx.role == SYNC_ROLE_MASTER)
    {
        sync_master_deinit();
    }
#endif
#if SYNC_ROLE_SLAVE_SUPPORT
    if (sync_ctx.role == SYNC_ROLE_SLAVE)
    {
        sync_slave_deinit();
    }
#endif
}

void sync_dlps_init(void)
{
#if SYNC_ROLE_MASTER_SUPPORT
    if (sync_ctx.role == SYNC_ROLE_MASTER)
    {
        return sync_master_dlps_init();
    }
#endif
#if SYNC_ROLE_SLAVE_SUPPORT
    if (sync_ctx.role == SYNC_ROLE_SLAVE)
    {
        return sync_slave_dlps_init();
    }
#endif
}

void sync_crc_set(uint8_t crc_len, uint32_t crc_poly, uint32_t crc_init)
{
    sync_ctx.crc_len = crc_len;
    sync_ctx.crc_poly = crc_poly;
    sync_ctx.crc_init = crc_init;
    WR_PPT_REG_FIELD(PRO_PACKET_CONTROL, crc_field_length_2_0, sync_ctx.crc_len >> 3);
}

bool sync_time_set(sync_time_param_t type, uint32_t time)
{
    bool ret = true;
    switch (type)
    {
    case SYNC_TIME_PARAM_CONNECT_INTERVAL:
#if SYNC_SUPPORT_MT1R
        sync_ctx.mt1r.flag = time == 125;
        sync_ctx.conn_interval = sync_ctx.mt1r.flag ? 250 : time;
#else
        sync_ctx.conn_interval = time;
#endif
        break;
    case SYNC_TIME_PARAM_CONNECT_INTERVAL_HIGH:
        sync_ctx.conn_interval_high = time;
        break;
    case SYNC_TIME_PARAM_SYNC_LOST_PERIOD:
        sync_ctx.sync_lost_period = time;
        break;
    case SYNC_TIME_PARAM_SYNC_FAST_PERIOD:
        sync_ctx.sync_fast_period = time;
        break;
    case SYNC_TIME_PARAM_HB_TO_TIMES:
        sync_ctx.hb_to_times = time;
        break;
    case SYNC_TIME_PARAM_CHANNEL_ASSESS_TIMES:
        sync_ctx.cca_times = time;
        break;
    case SYNC_TIME_PARAM_SLAVE_HOPPING_SPEED:
        sync_ctx.slave_hopping_speed = time;
        break;
    default:
        ret = false;
        break;
    }
    return ret;
}

bool sync_time_get(sync_time_param_t type, uint32_t *time)
{
    bool ret = true;
    switch (type)
    {
    case SYNC_TIME_PARAM_CONNECT_INTERVAL:
#if SYNC_SUPPORT_MT1R
        *time = sync_ctx.mt1r.flag ? 125 : sync_ctx.conn_interval;
#else
        *time = sync_ctx.conn_interval;
#endif
        break;
    case SYNC_TIME_PARAM_CONNECT_INTERVAL_HIGH:
        *time = sync_ctx.conn_interval_high;
        break;
    default:
        ret = false;
        break;
    }
    return ret;
}

bool sync_log_set(uint8_t level, bool on)
{
    sync_ctx.log_isr = on;
    return true;
}

#if SYNC_SUPPORT_POWER_CONTROL
static const int8_t sync_power_level[] = {-40, -20, -10, 0, 4, 8, 10, 14, 18, 20};
static uint8_t sync_power_ctrl_get_idx(int8_t power_dbm)
{
    uint8_t table_size = sizeof(sync_power_level) / sizeof(sync_power_level[0]);
    if (sync_power_level[table_size - 1] < power_dbm)
    {
        return table_size - 1;
    }
    for (int loop = table_size - 1; loop >= 0; loop--)
    {
        if (sync_power_level[loop] >= power_dbm && (loop == 0 || sync_power_level[loop - 1] < power_dbm))
        {
            return loop;
        }
    }
    return table_size - 1;
}

static void sync_power_ctrl_adjust(sync_sw_timer_t *sync_timer)
{
    if (sync_ctx.tx_power_levels <= 1)
    {
        return;
    }

    uint8_t adjust = 0;

    uint32_t count_fail = sync_ctx.power_ctrl_count_fail;
    uint32_t count = sync_ctx.power_ctrl_count;
    uint16_t old_per = sync_ctx.power_ctrl_per;
    if (count && count_fail < count)
    {
        uint16_t per = count_fail * 1000 / count;
        sync_ctx.power_ctrl_per = (old_per * 8 + per * 2) / 10;
        if (sync_ctx.power_ctrl_per > 100)
        {
            adjust = 2;
        }
        else if (sync_ctx.power_ctrl_per < 10)
        {
            adjust = 1;
        }
    }
    //APP_PRINT_INFO5("sync: power ctrl count %d, count_fail %d, per %d -> %d, adjust %d", count, count_fail, old_per, sync_ctx.power_ctrl_per, adjust);
    sync_ctx.power_ctrl_count = 0;
    sync_ctx.power_ctrl_count_fail = 0;

    if (adjust == 1)
    {
        if (sync_ctx.tx_power_idx_cur > sync_ctx.tx_power_idx_min)
        {
            sync_ctx.tx_power_idx_cur -= 1;
            ppt_set_tx_power_dbm(sync_power_level[sync_ctx.tx_power_idx_cur]);
            APP_PRINT_INFO3("sync: power ctrl %d decrease from %d to %d", sync_ctx.power_ctrl_per,
                            sync_power_level[sync_ctx.tx_power_idx_cur + 1], sync_power_level[sync_ctx.tx_power_idx_cur]);
            sync_ctx.power_ctrl_per = 0;
        }

    }
    else if (adjust == 2)
    {
        if (sync_ctx.tx_power_idx_cur < sync_ctx.tx_power_idx_max)
        {
            sync_ctx.tx_power_idx_cur += 1;
            ppt_set_tx_power_dbm(sync_power_level[sync_ctx.tx_power_idx_cur]);
            APP_PRINT_INFO3("sync: power ctrl %d increase from %d to %d", sync_ctx.power_ctrl_per,
                            sync_power_level[sync_ctx.tx_power_idx_cur - 1], sync_power_level[sync_ctx.tx_power_idx_cur]);
            sync_ctx.power_ctrl_per = 0;
        }
    }
}

void sync_power_ctrl_init(void)
{
    sync_ctx.tx_power_idx_max = sync_power_ctrl_get_idx(sync_ctx.tx_power_dbm_max);
    sync_ctx.tx_power_idx_min = sync_power_ctrl_get_idx(sync_ctx.tx_power_dbm_min);
    sync_ctx.tx_power_levels =  sync_ctx.tx_power_idx_min - sync_ctx.tx_power_idx_max + 1;
    if (sync_ctx.tx_power_levels > 1)
    {
        sync_ctx.tx_power_idx_cur = sync_ctx.tx_power_idx_max;
        ppt_set_tx_power_dbm(sync_power_level[sync_ctx.tx_power_idx_cur]);
    }
    else
    {
        ppt_set_tx_power_dbm(sync_ctx.tx_power_dbm_max);
    }
}

void sync_power_ctrl_start(void)
{
    if (sync_ctx.tx_power_levels <= 1)
    {
        return;
    }

    sync_ctx.power_ctrl_count = 0;
    sync_ctx.power_ctrl_count_fail = 0;
    sync_ctx.power_ctrl_per = 0;
    sync_sw_timer_start(&sync_ctx.power_ctrl_timer, SYNC_SUPPORT_POWER_CONTROL_PERIOD,
                        sync_power_ctrl_adjust);
}

void sync_power_ctrl_stop(void)
{
    if (sync_ctx.tx_power_levels <= 1)
    {
        return;
    }

    sync_sw_timer_stop(&sync_ctx.power_ctrl_timer);
}
#endif

bool sync_tx_power_set(bool dynamic, int8_t tx_power_dbm_max, int8_t tx_power_dbm_min)
{
    if (tx_power_dbm_max < tx_power_dbm_min)
    {
        return false;
    }

    sync_ctx.tx_power_adaptive = dynamic;
    sync_ctx.tx_power_dbm_max = tx_power_dbm_max;
    sync_ctx.tx_power_dbm_min = tx_power_dbm_min;
    return true;
}

bool sync_pair_rssi_set(int8_t rssi)
{
    sync_ctx.pair_rssi_th = rssi;
    return true;
}

bool sync_tifs_set(uint8_t level, uint8_t tifs)
{
    if (level >= sizeof(sync_ctx.tifs) / sizeof(sync_ctx.tifs[0]))
    {
        return false;
    }
    sync_ctx.tifs[level] = tifs;
    return true;
}

void sync_check_feature(void)
{
#if SYNC_SUPPORT_POWER_CONTROL
    if (sync_ctx.tx_power_adaptive)
    {
        sync_power_ctrl_init();
    }
    else
#endif
    {
        ppt_set_tx_power_dbm(sync_ctx.tx_power_dbm_max);
    }
}

void sync_hb_reg_cb(sync_hb_cb_t cb)
{
    sync_ctx.hb_cb = cb;
}

void sync_rte_req_reg_cb(sync_rte_req_cb_t cb)
{
    sync_ctx.rte_req_cb = cb;
}
