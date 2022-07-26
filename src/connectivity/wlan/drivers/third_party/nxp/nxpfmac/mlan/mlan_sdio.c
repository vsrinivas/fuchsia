/** @file mlan_sdio.c
 *
 *  @brief This file contains SDIO specific code
 *
 *
 *  Copyright 2008-2021 NXP
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *  this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *  this list of conditions and the following disclaimer in the documentation
 *  and/or other materials provided with the distribution.
 *
 *  3. Neither the name of the copyright holder nor the names of its
 *  contributors may be used to endorse or promote products derived from this
 *  software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ASIS AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/********************************************************
Change log:
    10/27/2008: initial version
********************************************************/

#include "mlan.h"
#ifdef STA_SUPPORT
#include "mlan_join.h"
#endif
#include "mlan_util.h"
#include "mlan_fw.h"
#include "mlan_main.h"
#include "mlan_init.h"
#include "mlan_wmm.h"
#include "mlan_11n.h"
#include "mlan_sdio.h"

/********************************************************
		Local Variables
********************************************************/
#ifdef SD8887
static const struct _mlan_sdio_card_reg mlan_reg_sd8887 = {
	.start_rd_port = 0,
	.start_wr_port = 0,
	.base_0_reg = 0x6C,
	.base_1_reg = 0x6D,
	.poll_reg = 0x5C,
	.host_int_enable = UP_LD_HOST_INT_MASK | DN_LD_HOST_INT_MASK |
		CMD_PORT_UPLD_INT_MASK | CMD_PORT_DNLD_INT_MASK,
	.host_int_status = DN_LD_HOST_INT_STATUS | UP_LD_HOST_INT_STATUS |
		DN_LD_CMD_PORT_HOST_INT_STATUS | UP_LD_CMD_PORT_HOST_INT_STATUS,
	.status_reg_0 = 0x90,
	.status_reg_1 = 0x91,
	.sdio_int_mask = 0xff,
	.data_port_mask = 0xffffffff,
	.max_mp_regs = 196,
	.rd_bitmap_l = 0x10,
	.rd_bitmap_u = 0x11,
	.rd_bitmap_1l = 0x12,
	.rd_bitmap_1u = 0x13,
	.wr_bitmap_l = 0x14,
	.wr_bitmap_u = 0x15,
	.wr_bitmap_1l = 0x16,
	.wr_bitmap_1u = 0x17,
	.rd_len_p0_l = 0x18,
	.rd_len_p0_u = 0x19,
	.card_config_2_1_reg = 0xD9,
	.cmd_config_0 = 0xC4,
	.cmd_config_1 = 0xC5,
	.cmd_config_2 = 0xC6,
	.cmd_config_3 = 0xC7,
	.cmd_rd_len_0 = 0xC0,
	.cmd_rd_len_1 = 0xC1,
	.cmd_rd_len_2 = 0xC2,
	.cmd_rd_len_3 = 0xC3,
	.io_port_0_reg = 0xE4,
	.io_port_1_reg = 0xE5,
	.io_port_2_reg = 0xE6,
	.host_int_rsr_reg = 0x04,
	.host_int_mask_reg = 0x08,
	.host_int_status_reg = 0x0C,
	.host_restart_reg = 0x58,
	.card_to_host_event_reg = 0x5C,
	.host_interrupt_mask_reg = 0x60,
	.card_interrupt_status_reg = 0x64,
	.card_interrupt_rsr_reg = 0x68,
	.card_revision_reg = 0xC8,
	.card_ocr_0_reg = 0xD4,
	.card_ocr_1_reg = 0xD5,
	.card_ocr_3_reg = 0xD6,
	.card_config_reg = 0xD7,
	.card_misc_cfg_reg = 0xD8,
	.debug_0_reg = 0xDC,
	.debug_1_reg = 0xDD,
	.debug_2_reg = 0xDE,
	.debug_3_reg = 0xDF,
	.fw_reset_reg = 0x0B6,
	.fw_reset_val = 1,
	.winner_check_reg = 0x90,
};

static const struct _mlan_card_info mlan_card_info_sd8887 = {
	.max_tx_buf_size = MLAN_TX_DATA_BUF_SIZE_2K,
	.v16_fw_api = 0,
	.supp_ps_handshake = 0,
	.default_11n_tx_bf_cap = DEFAULT_11N_TX_BF_CAP_1X1,
};
#endif

#ifdef SD8801
static const struct _mlan_sdio_card_reg mlan_reg_sd8801 = {
	.start_rd_port = 1,
	.start_wr_port = 1,
	.base_0_reg = 0x40,
	.base_1_reg = 0x41,
	.poll_reg = 0x30,
	.host_int_enable = UP_LD_HOST_INT_MASK | DN_LD_HOST_INT_MASK,
	.host_int_status = DN_LD_HOST_INT_STATUS | UP_LD_HOST_INT_STATUS,
	.status_reg_0 = 0x60,
	.status_reg_1 = 0x61,
	.sdio_int_mask = 0x3f,
	.data_port_mask = 0x0000fffe,
	.max_mp_regs = 64,
	.rd_bitmap_l = 0x4,
	.rd_bitmap_u = 0x5,
	.wr_bitmap_l = 0x6,
	.wr_bitmap_u = 0x7,
	.rd_len_p0_l = 0x8,
	.rd_len_p0_u = 0x9,
	.io_port_0_reg = 0x78,
	.io_port_1_reg = 0x79,
	.io_port_2_reg = 0x7A,
	.host_int_rsr_reg = 0x01,
	.host_int_mask_reg = 0x02,
	.host_int_status_reg = 0x03,
	.card_misc_cfg_reg = 0x6c,
	.fw_reset_reg = 0x64,
	.fw_reset_val = 0,
};

static const struct _mlan_card_info mlan_card_info_sd8801 = {
	.max_tx_buf_size = MLAN_TX_DATA_BUF_SIZE_2K,
	.v14_fw_api = 1,
	.v16_fw_api = 0,
	.supp_ps_handshake = 0,
	.default_11n_tx_bf_cap = DEFAULT_11N_TX_BF_CAP_1X1,
};
#endif

#ifdef SD8897
static const struct _mlan_sdio_card_reg mlan_reg_sd8897 = {
	.start_rd_port = 0,
	.start_wr_port = 0,
	.base_0_reg = 0x60,
	.base_1_reg = 0x61,
	.poll_reg = 0x50,
	.host_int_enable = UP_LD_HOST_INT_MASK | DN_LD_HOST_INT_MASK |
		CMD_PORT_UPLD_INT_MASK | CMD_PORT_DNLD_INT_MASK,
	.host_int_status = DN_LD_HOST_INT_STATUS | UP_LD_HOST_INT_STATUS |
		DN_LD_CMD_PORT_HOST_INT_STATUS | UP_LD_CMD_PORT_HOST_INT_STATUS,
	.status_reg_0 = 0xC0,
	.status_reg_1 = 0xC1,
	.sdio_int_mask = 0xff,
	.data_port_mask = 0xffffffff,
	.max_mp_regs = 184,
	.rd_bitmap_l = 0x04,
	.rd_bitmap_u = 0x05,
	.rd_bitmap_1l = 0x06,
	.rd_bitmap_1u = 0x07,
	.wr_bitmap_l = 0x08,
	.wr_bitmap_u = 0x09,
	.wr_bitmap_1l = 0x0A,
	.wr_bitmap_1u = 0x0B,
	.rd_len_p0_l = 0x0C,
	.rd_len_p0_u = 0x0D,
	.card_config_2_1_reg = 0xCD,
	.cmd_config_0 = 0xB8,
	.cmd_config_1 = 0xB9,
	.cmd_config_2 = 0xBA,
	.cmd_config_3 = 0xBB,
	.cmd_rd_len_0 = 0xB4,
	.cmd_rd_len_1 = 0xB5,
	.cmd_rd_len_2 = 0xB6,
	.cmd_rd_len_3 = 0xB7,
	.io_port_0_reg = 0xD8,
	.io_port_1_reg = 0xD9,
	.io_port_2_reg = 0xDA,
	.host_int_rsr_reg = 0x01,
	.host_int_mask_reg = 0x02,
	.host_int_status_reg = 0x03,
	.host_restart_reg = 0x4C,
	.card_to_host_event_reg = 0x50,
	.host_interrupt_mask_reg = 0x54,
	.card_interrupt_status_reg = 0x58,
	.card_interrupt_rsr_reg = 0x5C,
	.card_revision_reg = 0xBC,
	.card_ocr_0_reg = 0xC8,
	.card_ocr_1_reg = 0xC9,
	.card_ocr_3_reg = 0xCA,
	.card_config_reg = 0xCB,
	.card_misc_cfg_reg = 0xCC,
	.debug_0_reg = 0xD0,
	.debug_1_reg = 0xD1,
	.debug_2_reg = 0xD2,
	.debug_3_reg = 0xD3,
	.fw_reset_reg = 0x0E8,
	.fw_reset_val = 1,
	.winner_check_reg = 0xC0,
};

static const struct _mlan_card_info mlan_card_info_sd8897 = {
	.max_tx_buf_size = MLAN_TX_DATA_BUF_SIZE_4K,
	.v16_fw_api = 0,
	.supp_ps_handshake = 0,
	.default_11n_tx_bf_cap = DEFAULT_11N_TX_BF_CAP_2X2,
};
#endif

#if defined(SD8977) || defined(SD8997) || defined(SD8987) || defined(SD9098) || defined(SD9097) || defined(SD8978) || defined(SD9177)
static const struct _mlan_sdio_card_reg mlan_reg_sd8977_sd8997 = {
	.start_rd_port = 0,
	.start_wr_port = 0,
	.base_0_reg = 0xf8,
	.base_1_reg = 0xf9,
	.poll_reg = 0x5C,
	.host_int_enable = UP_LD_HOST_INT_MASK | DN_LD_HOST_INT_MASK |
		CMD_PORT_UPLD_INT_MASK | CMD_PORT_DNLD_INT_MASK,
	.host_int_status = DN_LD_HOST_INT_STATUS | UP_LD_HOST_INT_STATUS |
		DN_LD_CMD_PORT_HOST_INT_STATUS | UP_LD_CMD_PORT_HOST_INT_STATUS,
	.status_reg_0 = 0xe8,
	.status_reg_1 = 0xe9,
	.sdio_int_mask = 0xff,
	.data_port_mask = 0xffffffff,
	.max_mp_regs = 196,
	.rd_bitmap_l = 0x10,
	.rd_bitmap_u = 0x11,
	.rd_bitmap_1l = 0x12,
	.rd_bitmap_1u = 0x13,
	.wr_bitmap_l = 0x14,
	.wr_bitmap_u = 0x15,
	.wr_bitmap_1l = 0x16,
	.wr_bitmap_1u = 0x17,
	.rd_len_p0_l = 0x18,
	.rd_len_p0_u = 0x19,
	.card_config_2_1_reg = 0xD9,
	.cmd_config_0 = 0xC4,
	.cmd_config_1 = 0xC5,
	.cmd_config_2 = 0xC6,
	.cmd_config_3 = 0xC7,
	.cmd_rd_len_0 = 0xC0,
	.cmd_rd_len_1 = 0xC1,
	.cmd_rd_len_2 = 0xC2,
	.cmd_rd_len_3 = 0xC3,
	.io_port_0_reg = 0xE4,
	.io_port_1_reg = 0xE5,
	.io_port_2_reg = 0xE6,
	.host_int_rsr_reg = 0x04,
	.host_int_mask_reg = 0x08,
	.host_int_status_reg = 0x0C,
	.host_restart_reg = 0x58,
	.card_to_host_event_reg = 0x5C,
	.host_interrupt_mask_reg = 0x60,
	.card_interrupt_status_reg = 0x64,
	.card_interrupt_rsr_reg = 0x68,
	.card_revision_reg = 0xC8,
	.card_ocr_0_reg = 0xD4,
	.card_ocr_1_reg = 0xD5,
	.card_ocr_3_reg = 0xD6,
	.card_config_reg = 0xD7,
	.card_misc_cfg_reg = 0xD8,
	.debug_0_reg = 0xDC,
	.debug_1_reg = 0xDD,
	.debug_2_reg = 0xDE,
	.debug_3_reg = 0xDF,
	.fw_reset_reg = 0x0EE,
	.fw_reset_val = 0x99,
	.fw_dnld_offset_0_reg = 0xEC,
	.fw_dnld_offset_1_reg = 0xED,
	.fw_dnld_offset_2_reg = 0xEE,
	.fw_dnld_offset_3_reg = 0xEF,
	.fw_dnld_status_0_reg = 0xE8,
	.fw_dnld_status_1_reg = 0xE9,
	.winner_check_reg = 0xFC,
};
#endif

#ifdef SD8997
static const struct _mlan_card_info mlan_card_info_sd8997 = {
	.max_tx_buf_size = MLAN_TX_DATA_BUF_SIZE_4K,
	.v16_fw_api = 1,
	.supp_ps_handshake = 0,
	.default_11n_tx_bf_cap = DEFAULT_11N_TX_BF_CAP_2X2,
};
#endif

#ifdef SD9097
static const struct _mlan_card_info mlan_card_info_sd9097 = {
	.max_tx_buf_size = MLAN_TX_DATA_BUF_SIZE_4K,
	.v16_fw_api = 1,
	.v17_fw_api = 1,
	.supp_ps_handshake = 0,
	.default_11n_tx_bf_cap = DEFAULT_11N_TX_BF_CAP_2X2,
};
#endif
#ifdef SD9098
static const struct _mlan_card_info mlan_card_info_sd9098 = {
	.max_tx_buf_size = MLAN_TX_DATA_BUF_SIZE_4K,
	.v16_fw_api = 1,
	.v17_fw_api = 1,
	.supp_ps_handshake = 0,
	.default_11n_tx_bf_cap = DEFAULT_11N_TX_BF_CAP_2X2,
};
#endif
#ifdef SD9177
static const struct _mlan_card_info mlan_card_info_sd9177 = {
	.max_tx_buf_size = MLAN_TX_DATA_BUF_SIZE_4K,
	.v16_fw_api = 1,
	.v17_fw_api = 1,
	.supp_ps_handshake = 0,
	.default_11n_tx_bf_cap = DEFAULT_11N_TX_BF_CAP_1X1,
};
#endif

#if defined(SD8977) || defined(SD8978)
static const struct _mlan_card_info mlan_card_info_sd8977 = {
	.max_tx_buf_size = MLAN_TX_DATA_BUF_SIZE_2K,
	.v16_fw_api = 1,
	.supp_ps_handshake = 0,
	.default_11n_tx_bf_cap = DEFAULT_11N_TX_BF_CAP_1X1,
};
#endif

#ifdef SD8987
static const struct _mlan_card_info mlan_card_info_sd8987 = {
	.max_tx_buf_size = MLAN_TX_DATA_BUF_SIZE_2K,
	.v16_fw_api = 1,
	.supp_ps_handshake = 0,
	.default_11n_tx_bf_cap = DEFAULT_11N_TX_BF_CAP_1X1,
};
#endif

/********************************************************
		Global Variables
********************************************************/

/********************************************************
		Local Functions
********************************************************/

/**
 *  @brief This function initialize the SDIO port
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_sdio_init_ioport(mlan_adapter *pmadapter)
{
	t_u32 reg;
	pmlan_callbacks pcb = &pmadapter->callbacks;
	t_u8 host_int_rsr_reg = pmadapter->pcard_sd->reg->host_int_rsr_reg;
	t_u8 host_int_rsr_mask = pmadapter->pcard_sd->reg->sdio_int_mask;
	t_u8 card_misc_cfg_reg = pmadapter->pcard_sd->reg->card_misc_cfg_reg;
	t_u8 card_config_2_1_reg =
		pmadapter->pcard_sd->reg->card_config_2_1_reg;
	t_u8 cmd_config_0 = pmadapter->pcard_sd->reg->cmd_config_0;
	t_u8 cmd_config_1 = pmadapter->pcard_sd->reg->cmd_config_1;

	ENTER();
	if (pmadapter->pcard_sd->supports_sdio_new_mode) {
		pmadapter->pcard_sd->ioport = MEM_PORT;
	} else {
		if (MLAN_STATUS_SUCCESS ==
		    pcb->moal_read_reg(pmadapter->pmoal_handle,
				       pmadapter->pcard_sd->reg->io_port_0_reg,
				       &reg))
			pmadapter->pcard_sd->ioport |= (reg & 0xff);
		else {
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
		if (MLAN_STATUS_SUCCESS ==
		    pcb->moal_read_reg(pmadapter->pmoal_handle,
				       pmadapter->pcard_sd->reg->io_port_1_reg,
				       &reg))
			pmadapter->pcard_sd->ioport |= ((reg & 0xff) << 8);
		else {
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
		if (MLAN_STATUS_SUCCESS ==
		    pcb->moal_read_reg(pmadapter->pmoal_handle,
				       pmadapter->pcard_sd->reg->io_port_2_reg,
				       &reg))
			pmadapter->pcard_sd->ioport |= ((reg & 0xff) << 16);
		else {
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
	}
	PRINTM(MINFO, "SDIO FUNC1 IO port: 0x%x\n",
	       pmadapter->pcard_sd->ioport);

	if (pmadapter->pcard_sd->supports_sdio_new_mode) {
		/* enable sdio cmd53 new mode */
		if (MLAN_STATUS_SUCCESS ==
		    pcb->moal_read_reg(pmadapter->pmoal_handle,
				       card_config_2_1_reg, &reg)) {
			pcb->moal_write_reg(pmadapter->pmoal_handle,
					    card_config_2_1_reg,
					    reg | CMD53_NEW_MODE);
		} else {
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}

		/* configure cmd port  */
		/* enable reading rx length from the register  */
		if (MLAN_STATUS_SUCCESS ==
		    pcb->moal_read_reg(pmadapter->pmoal_handle, cmd_config_0,
				       &reg)) {
			pcb->moal_write_reg(pmadapter->pmoal_handle,
					    cmd_config_0,
					    reg | CMD_PORT_RD_LEN_EN);
		} else {
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
		/* enable Dnld/Upld ready auto reset for cmd port
		 * after cmd53 is completed */
		if (MLAN_STATUS_SUCCESS ==
		    pcb->moal_read_reg(pmadapter->pmoal_handle, cmd_config_1,
				       &reg)) {
			pcb->moal_write_reg(pmadapter->pmoal_handle,
					    cmd_config_1,
					    reg | CMD_PORT_AUTO_EN);
		} else {
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
	}
#if defined(SD8977) || defined(SD8978)
	if (IS_SD8977(pmadapter->card_type) || IS_SD8978(pmadapter->card_type)) {
		if ((pmadapter->init_para.int_mode == INT_MODE_GPIO) &&
		    (pmadapter->init_para.gpio_pin == GPIO_INT_NEW_MODE)) {
			PRINTM(MMSG, "Enable GPIO-1 int mode\n");
			pcb->moal_write_reg(pmadapter->pmoal_handle,
					    SCRATCH_REG_32,
					    ENABLE_GPIO_1_INT_MODE);
		}
	}
#endif
	/* Set Host interrupt reset to read to clear */
	if (MLAN_STATUS_SUCCESS == pcb->moal_read_reg(pmadapter->pmoal_handle,
						      host_int_rsr_reg, &reg)) {
		pcb->moal_write_reg(pmadapter->pmoal_handle, host_int_rsr_reg,
				    reg | host_int_rsr_mask);
	} else {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	/* Dnld/Upld ready set to auto reset */
	if (MLAN_STATUS_SUCCESS == pcb->moal_read_reg(pmadapter->pmoal_handle,
						      card_misc_cfg_reg,
						      &reg)) {
		pcb->moal_write_reg(pmadapter->pmoal_handle, card_misc_cfg_reg,
				    reg | AUTO_RE_ENABLE_INT);
	} else {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function sends data to the card.
 *
 *  @param pmadapter A pointer to mlan_adapter structure
 *  @param pmbuf     A pointer to mlan_buffer (pmbuf->data_len should include
 * SDIO header)
 *  @param port      Port
 *  @return          MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_write_data_sync(mlan_adapter *pmadapter, mlan_buffer *pmbuf, t_u32 port)
{
	t_u32 i = 0;
	pmlan_callbacks pcb = &pmadapter->callbacks;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	do {
		ret = pcb->moal_write_data_sync(pmadapter->pmoal_handle, pmbuf,
						port, 0);
		if (ret != MLAN_STATUS_SUCCESS) {
			i++;
			PRINTM(MERROR,
			       "host_to_card, write iomem (%d) failed: %d\n", i,
			       ret);
			if (MLAN_STATUS_SUCCESS !=
			    pcb->moal_write_reg(pmadapter->pmoal_handle,
						HOST_TO_CARD_EVENT_REG,
						HOST_TERM_CMD53)) {
				PRINTM(MERROR, "write CFG reg failed\n");
			}
			ret = MLAN_STATUS_FAILURE;
			if (i > MAX_WRITE_IOMEM_RETRY) {
				pmbuf->status_code = MLAN_ERROR_DATA_TX_FAIL;
				goto exit;
			}
		}
	} while (ret == MLAN_STATUS_FAILURE);
exit:
	LEAVE();
	return ret;
}

/**
 *  @brief This function gets available SDIO port for reading cmd/data
 *
 *  @param pmadapter  A pointer to mlan_adapter structure
 *  @param pport      A pointer to port number
 *  @return           MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_get_rd_port(mlan_adapter *pmadapter, t_u8 *pport)
{
	t_u32 rd_bitmap = pmadapter->pcard_sd->mp_rd_bitmap;
	const mlan_sdio_card_reg *reg = pmadapter->pcard_sd->reg;
	t_u8 max_ports = pmadapter->pcard_sd->max_ports;
	t_bool new_mode = pmadapter->pcard_sd->supports_sdio_new_mode;

	ENTER();

	PRINTM(MIF_D, "wlan_get_rd_port: mp_rd_bitmap=0x%08x\n", rd_bitmap);
	if (new_mode) {
		if (!(rd_bitmap & reg->data_port_mask)) {
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
	} else {
		if (!(rd_bitmap & (CTRL_PORT_MASK | reg->data_port_mask))) {
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
	}
	if (!new_mode && (pmadapter->pcard_sd->mp_rd_bitmap & CTRL_PORT_MASK)) {
		pmadapter->pcard_sd->mp_rd_bitmap &= (t_u32)(~CTRL_PORT_MASK);
		*pport = CTRL_PORT;
		PRINTM(MIF_D, "wlan_get_rd_port: port=%d mp_rd_bitmap=0x%08x\n",
		       *pport, pmadapter->pcard_sd->mp_rd_bitmap);
	} else {
		if (pmadapter->pcard_sd->mp_rd_bitmap &
		    (1 << pmadapter->pcard_sd->curr_rd_port)) {
			pmadapter->pcard_sd->mp_rd_bitmap &=
				(t_u32)(~
					(1 << pmadapter->pcard_sd->
					 curr_rd_port));
			*pport = pmadapter->pcard_sd->curr_rd_port;

			/* hw rx wraps round only after port (MAX_PORT-1) */
			if (++pmadapter->pcard_sd->curr_rd_port == max_ports)
				pmadapter->pcard_sd->curr_rd_port =
					reg->start_rd_port;
		} else {
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}

		PRINTM(MIF_D, "port=%d mp_rd_bitmap=0x%08x -> 0x%08x\n", *pport,
		       rd_bitmap, pmadapter->pcard_sd->mp_rd_bitmap);
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function gets available SDIO port for writing data
 *
 *  @param pmadapter  A pointer to mlan_adapter structure
 *  @param pport      A pointer to port number
 *  @return           MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_get_wr_port_data(mlan_adapter *pmadapter, t_u8 *pport)
{
	t_u32 wr_bitmap = pmadapter->pcard_sd->mp_wr_bitmap;
	const mlan_sdio_card_reg *reg = pmadapter->pcard_sd->reg;
	t_bool new_mode = pmadapter->pcard_sd->supports_sdio_new_mode;

	ENTER();

	PRINTM(MIF_D, "wlan_get_wr_port_data: mp_wr_bitmap=0x%08x\n",
	       wr_bitmap);

	if (!(wr_bitmap & pmadapter->pcard_sd->mp_data_port_mask)) {
		pmadapter->data_sent = MTRUE;
		LEAVE();
		return MLAN_STATUS_RESOURCE;
	}

	if (pmadapter->pcard_sd->mp_wr_bitmap &
	    (1 << pmadapter->pcard_sd->curr_wr_port)) {
		pmadapter->pcard_sd->mp_wr_bitmap &=
			(t_u32)(~(1 << pmadapter->pcard_sd->curr_wr_port));
		*pport = pmadapter->pcard_sd->curr_wr_port;
		if (++pmadapter->pcard_sd->curr_wr_port ==
		    pmadapter->pcard_sd->mp_end_port)
			pmadapter->pcard_sd->curr_wr_port = reg->start_wr_port;
	} else {
		pmadapter->data_sent = MTRUE;
		LEAVE();
		return MLAN_STATUS_RESOURCE;
	}
	if ((!new_mode) && (*pport == CTRL_PORT)) {
		PRINTM(MERROR,
		       "Invalid data port=%d cur port=%d mp_wr_bitmap=0x%08x -> 0x%08x\n",
		       *pport, pmadapter->pcard_sd->curr_wr_port, wr_bitmap,
		       pmadapter->pcard_sd->mp_wr_bitmap);
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	PRINTM(MIF_D, "port=%d mp_wr_bitmap=0x%08x -> 0x%08x\n", *pport,
	       wr_bitmap, pmadapter->pcard_sd->mp_wr_bitmap);
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function polls the card status register.
 *
 *  @param pmadapter  A pointer to mlan_adapter structure
 *  @param bits       the bit mask
 *  @return           MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_sdio_poll_card_status(mlan_adapter *pmadapter, t_u8 bits)
{
	pmlan_callbacks pcb = &pmadapter->callbacks;
	t_u32 tries;
	t_u32 cs = 0;

	ENTER();

	for (tries = 0; tries < 10000; tries++) {
		if (pcb->moal_read_reg(pmadapter->pmoal_handle,
				       pmadapter->pcard_sd->reg->poll_reg,
				       &cs) != MLAN_STATUS_SUCCESS)
			break;
		else if ((cs & bits) == bits) {
			LEAVE();
			return MLAN_STATUS_SUCCESS;
		}
		wlan_udelay(pmadapter, 10);
	}

	PRINTM(MERROR,
	       "wlan_sdio_poll_card_status failed, tries = %d, cs = 0x%x\n",
	       tries, cs);
	LEAVE();
	return MLAN_STATUS_FAILURE;
}

/**
 *  @brief This function reads firmware status registers
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param dat          A pointer to keep returned data
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_sdio_read_fw_status(mlan_adapter *pmadapter, t_u16 *dat)
{
	pmlan_callbacks pcb = &pmadapter->callbacks;
	t_u32 fws0 = 0, fws1 = 0;

	ENTER();
	if (MLAN_STATUS_SUCCESS !=
	    pcb->moal_read_reg(pmadapter->pmoal_handle,
			       pmadapter->pcard_sd->reg->status_reg_0, &fws0)) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	if (MLAN_STATUS_SUCCESS !=
	    pcb->moal_read_reg(pmadapter->pmoal_handle,
			       pmadapter->pcard_sd->reg->status_reg_1, &fws1)) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	*dat = (t_u16)((fws1 << 8) | fws0);
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function reads firmware dnld offset registers
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param dat          A pointer to keep returned data
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_sdio_read_fw_dnld_offset(mlan_adapter *pmadapter, t_u32 *dat)
{
	pmlan_callbacks pcb = &pmadapter->callbacks;
	const mlan_sdio_card_reg *reg = pmadapter->pcard_sd->reg;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u32 fw_dnld_offset_0 = 0;
	t_u32 fw_dnld_offset_1 = 0;
	t_u32 fw_dnld_offset_2 = 0;
	t_u32 fw_dnld_offset_3 = 0;

	ENTER();

	ret = pcb->moal_read_reg(pmadapter->pmoal_handle,
				 reg->fw_dnld_offset_0_reg, &fw_dnld_offset_0);
	if (ret != MLAN_STATUS_SUCCESS) {
		PRINTM(MERROR,
		       "Dev fw_dnld_offset_0 reg read failed: reg(0x%04X)=0x%x. Terminating download\n",
		       reg->fw_dnld_offset_0_reg, fw_dnld_offset_0);
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	ret = pcb->moal_read_reg(pmadapter->pmoal_handle,
				 reg->fw_dnld_offset_1_reg, &fw_dnld_offset_1);
	if (ret != MLAN_STATUS_SUCCESS) {
		PRINTM(MERROR,
		       "Dev fw_dnld_offset_1 reg read failed: reg(0x%04X)=0x%x. Terminating download\n",
		       reg->fw_dnld_offset_1_reg, fw_dnld_offset_1);
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	ret = pcb->moal_read_reg(pmadapter->pmoal_handle,
				 reg->fw_dnld_offset_2_reg, &fw_dnld_offset_2);
	if (ret != MLAN_STATUS_SUCCESS) {
		PRINTM(MERROR,
		       "Dev fw_dnld_offset_2 reg read failed: reg(0x%04X)=0x%x. Terminating download\n",
		       reg->fw_dnld_offset_2_reg, fw_dnld_offset_2);
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	ret = pcb->moal_read_reg(pmadapter->pmoal_handle,
				 reg->fw_dnld_offset_3_reg, &fw_dnld_offset_3);
	if (ret != MLAN_STATUS_SUCCESS) {
		PRINTM(MERROR,
		       "Dev fw_dnld_offset_3 reg read failed: reg(0x%04X)=0x%x. Terminating download\n",
		       reg->fw_dnld_offset_3_reg, fw_dnld_offset_3);
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	*dat = (t_u32)(((fw_dnld_offset_3 & 0xff) << 24) |
		       ((fw_dnld_offset_2 & 0xff) << 16) |
		       ((fw_dnld_offset_1 & 0xff) << 8) |
		       (fw_dnld_offset_0 & 0xff));

done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function reads firmware dnld status registers
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param dat          A pointer to keep returned data
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_sdio_read_fw_dnld_status(mlan_adapter *pmadapter, t_u16 *dat)
{
	pmlan_callbacks pcb = &pmadapter->callbacks;
	const mlan_sdio_card_reg *reg = pmadapter->pcard_sd->reg;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u32 fw_dnld_status_0 = 0;
	t_u32 fw_dnld_status_1 = 0;

	ENTER();

	ret = pcb->moal_read_reg(pmadapter->pmoal_handle,
				 reg->fw_dnld_status_0_reg, &fw_dnld_status_0);
	if (ret != MLAN_STATUS_SUCCESS) {
		PRINTM(MERROR,
		       "Dev fw_dnld_status_0 reg read failed: reg(0x%04X)=0x%x. Terminating download\n",
		       reg->fw_dnld_status_0_reg, fw_dnld_status_0);
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	ret = pcb->moal_read_reg(pmadapter->pmoal_handle,
				 reg->fw_dnld_status_1_reg, &fw_dnld_status_1);
	if (ret != MLAN_STATUS_SUCCESS) {
		PRINTM(MERROR,
		       "Dev fw_dnld_status_1 reg read failed: reg(0x%04X)=0x%x. Terminating download\n",
		       reg->fw_dnld_status_1_reg, fw_dnld_status_1);
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	*dat = (t_u16)(((fw_dnld_status_1 & 0xff) << 8) |
		       (fw_dnld_status_0 & 0xff));

done:
	LEAVE();
	return ret;
}

/**  @brief This function disables the host interrupts mask.
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param mask         the interrupt mask
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_sdio_disable_host_int_mask(pmlan_adapter pmadapter, t_u8 mask)
{
	t_u32 host_int_mask = 0;
	pmlan_callbacks pcb = &pmadapter->callbacks;

	ENTER();

	/* Read back the host_int_mask register */
	if (MLAN_STATUS_SUCCESS !=
	    pcb->moal_read_reg(pmadapter->pmoal_handle,
			       pmadapter->pcard_sd->reg->host_int_mask_reg,
			       &host_int_mask)) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	/* Update with the mask and write back to the register */
	host_int_mask &= ~mask;

	if (MLAN_STATUS_SUCCESS !=
	    pcb->moal_write_reg(pmadapter->pmoal_handle,
				pmadapter->pcard_sd->reg->host_int_mask_reg,
				host_int_mask)) {
		PRINTM(MWARN, "Disable host interrupt failed\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function enables the host interrupts mask
 *
 *  @param pmadapter A pointer to mlan_adapter structure
 *  @param mask    the interrupt mask
 *  @return        MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_sdio_enable_host_int_mask(pmlan_adapter pmadapter, t_u8 mask)
{
	pmlan_callbacks pcb = &pmadapter->callbacks;

	ENTER();

	/* Simply write the mask to the register */
	if (MLAN_STATUS_SUCCESS !=
	    pcb->moal_write_reg(pmadapter->pmoal_handle,
				pmadapter->pcard_sd->reg->host_int_mask_reg,
				mask)) {
		PRINTM(MWARN, "Enable host interrupt failed\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function reads data from the card.
 *
 *  @param pmadapter A pointer to mlan_adapter structure
 *  @param type     A pointer to keep type as data or command
 *  @param nb       A pointer to keep the data/cmd length returned in buffer
 *  @param pmbuf    A pointer to the SDIO data/cmd buffer
 *  @param npayload the length of data/cmd buffer
 *  @param ioport   the SDIO ioport
 *  @return         MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_sdio_card_to_host(mlan_adapter *pmadapter, t_u32 *type,
		       t_u32 *nb, pmlan_buffer pmbuf,
		       t_u32 npayload, t_u32 ioport)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_callbacks pcb = &pmadapter->callbacks;
	t_u32 i = 0;

	ENTER();

	if (!pmbuf) {
		PRINTM(MWARN, "pmbuf is NULL!\n");
		ret = MLAN_STATUS_FAILURE;
		goto exit;
	}
	do {
		ret = pcb->moal_read_data_sync(pmadapter->pmoal_handle, pmbuf,
					       ioport, 0);

		if (ret != MLAN_STATUS_SUCCESS) {
			PRINTM(MERROR,
			       "wlan: cmd53 read failed: %d ioport=0x%x retry=%d\n",
			       ret, ioport, i);
			i++;
			if (MLAN_STATUS_SUCCESS !=
			    pcb->moal_write_reg(pmadapter->pmoal_handle,
						HOST_TO_CARD_EVENT_REG,
						HOST_TERM_CMD53)) {
				PRINTM(MERROR, "Set Term cmd53 failed\n");
			}
			if (i > MAX_WRITE_IOMEM_RETRY) {
				pmbuf->status_code = MLAN_ERROR_DATA_RX_FAIL;
				ret = MLAN_STATUS_FAILURE;
				goto exit;
			}
		}
	} while (ret == MLAN_STATUS_FAILURE);
	*nb = wlan_le16_to_cpu(*(t_u16 *)(pmbuf->pbuf + pmbuf->data_offset));
	if (*nb > npayload) {
		PRINTM(MERROR, "invalid packet, *nb=%d, npayload=%d\n", *nb,
		       npayload);
		pmbuf->status_code = MLAN_ERROR_PKT_SIZE_INVALID;
		ret = MLAN_STATUS_FAILURE;
		goto exit;
	}

	DBG_HEXDUMP(MIF_D, "SDIO Blk Rd", pmbuf->pbuf + pmbuf->data_offset,
		    MIN(*nb, MAX_DATA_DUMP_LEN));

	*type = wlan_le16_to_cpu(*(t_u16 *)
				 (pmbuf->pbuf + pmbuf->data_offset + 2));

exit:
	LEAVE();
	return ret;
}

/**
 *  @brief  This function downloads FW blocks to device
 *
 *  @param pmadapter	A pointer to mlan_adapter
 *  @param firmware     A pointer to firmware image
 *  @param firmwarelen  firmware len
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_sdio_prog_fw_w_helper(pmlan_adapter pmadapter, t_u8 *fw, t_u32 fw_len)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_callbacks pcb = &pmadapter->callbacks;
	t_u8 *firmware = fw;
	t_u32 firmwarelen = fw_len;
	t_u32 offset = 0;
	t_u32 base0, base1;
	t_void *tmpfwbuf = MNULL;
	t_u32 tmpfwbufsz;
	t_u8 *fwbuf;
	mlan_buffer mbuf;
	t_u16 len = 0;
	t_u32 txlen = 0, tx_blocks = 0, tries = 0;
	t_u32 i = 0;
	const mlan_sdio_card_reg *reg = pmadapter->pcard_sd->reg;
	t_u32 read_base_0_reg = reg->base_0_reg;
	t_u32 read_base_1_reg = reg->base_1_reg;
#if defined(SD9098)
	t_u32 rev_id_reg = 0;
	t_u32 revision_id = 0;
#endif
	t_u8 check_fw_status = MFALSE;
	t_u16 fw_dnld_status = 0;
	t_u32 fw_dnld_offset = 0;
	t_u8 mic_retry = 0;

	ENTER();

	if (!firmware && !pcb->moal_get_fw_data) {
		PRINTM(MMSG, "No firmware image found! Terminating download\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	PRINTM(MINFO, "WLAN: Downloading FW image (%d bytes)\n", firmwarelen);

	tmpfwbufsz = ALIGN_SZ(WLAN_UPLD_SIZE, DMA_ALIGNMENT);
	ret = pcb->moal_malloc(pmadapter->pmoal_handle, tmpfwbufsz,
			       MLAN_MEM_DEF | MLAN_MEM_DMA, (t_u8 **)&tmpfwbuf);
	if ((ret != MLAN_STATUS_SUCCESS) || !tmpfwbuf) {
		PRINTM(MERROR,
		       "Unable to allocate buffer for firmware. Terminating download\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	memset(pmadapter, tmpfwbuf, 0, tmpfwbufsz);
	/* Ensure 8-byte aligned firmware buffer */
	fwbuf = (t_u8 *)ALIGN_ADDR(tmpfwbuf, DMA_ALIGNMENT);
#if defined(SD9098)
	if (IS_SD9098(pmadapter->card_type)) {
		rev_id_reg = pmadapter->pcard_sd->reg->card_revision_reg;
		ret = pcb->moal_read_reg(pmadapter->pmoal_handle, rev_id_reg,
					 &revision_id);
		if (ret != MLAN_STATUS_SUCCESS) {
			PRINTM(MERROR,
			       "Card Revision register read failed:"
			       "card_revision_reg=0x%x\n", rev_id_reg);
			goto done;
		}
		/* Skyhawk A0, need to check both CRC and MIC error */
		if (revision_id >= CHIP_9098_REV_A0)
			check_fw_status = MTRUE;
	}
#endif
#if defined(SD9097)
	if (IS_SD9097(pmadapter->card_type))
		check_fw_status = MTRUE;
#endif
#if defined(SD9177)
	if (IS_SD9177(pmadapter->card_type))
		check_fw_status = MTRUE;
#endif
	/* Perform firmware data transfer */
	do {
		/* The host polls for the DN_LD_CARD_RDY and CARD_IO_READY bits
		 */
		ret = wlan_sdio_poll_card_status(pmadapter,
						 CARD_IO_READY |
						 DN_LD_CARD_RDY);
		if (ret != MLAN_STATUS_SUCCESS) {
			PRINTM(MFATAL,
			       "WLAN: FW download with helper poll status timeout @ %d\n",
			       offset);
			goto done;
		}

		/* More data */
		if (firmwarelen && offset >= firmwarelen)
			break;

		for (tries = 0; tries < MAX_POLL_TRIES; tries++) {
			ret = pcb->moal_read_reg(pmadapter->pmoal_handle,
						 read_base_0_reg, &base0);
			if (ret != MLAN_STATUS_SUCCESS) {
				PRINTM(MERROR,
				       "Dev BASE0 register read failed:"
				       " base0=0x%04X(%d). Terminating download\n",
				       base0, base0);
				goto done;
			}
			ret = pcb->moal_read_reg(pmadapter->pmoal_handle,
						 read_base_1_reg, &base1);
			if (ret != MLAN_STATUS_SUCCESS) {
				PRINTM(MERROR,
				       "Dev BASE1 register read failed:"
				       " base1=0x%04X(%d). Terminating download\n",
				       base1, base1);
				goto done;
			}
			len = (t_u16)(((base1 & 0xff) << 8) | (base0 & 0xff));

			if (len)
				break;
			wlan_udelay(pmadapter, 10);
		}

		if (!len)
			break;
		else if (len > WLAN_UPLD_SIZE) {
			PRINTM(MFATAL,
			       "WLAN: FW download failure @ %d, invalid length %d\n",
			       offset, len);
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}

		/* Ignore CRC check before download the 1st packet */
		if (offset == 0 && (len & MBIT(0)))
			len &= ~MBIT(0);

		txlen = len;

		if (len & MBIT(0)) {
			/* New fw download process, check CRC and MIC error */
			if (check_fw_status) {
				/* Get offset from fw dnld offset Register */
				ret = wlan_sdio_read_fw_dnld_offset(pmadapter,
								    &fw_dnld_offset);
				if (ret != MLAN_STATUS_SUCCESS) {
					PRINTM(MFATAL,
					       "WLAN: FW download with helper read fw dnld offset failed @ %d\n",
					       offset);
					goto done;
				}
				/* Get CRC MIC error from fw dnld status
				 * Register */
				ret = wlan_sdio_read_fw_dnld_status(pmadapter,
								    &fw_dnld_status);
				if (ret != MLAN_STATUS_SUCCESS) {
					PRINTM(MFATAL,
					       "WLAN: FW download with helper read fw dnld status failed @ %d\n",
					       offset);
					goto done;
				}
				PRINTM(MERROR,
				       "WLAN: FW download error: status=0x%x offset = 0x%x fw offset = 0x%x\n",
				       fw_dnld_status, offset, fw_dnld_offset);
			}
			i++;
			if (i > MAX_WRITE_IOMEM_RETRY) {
				PRINTM(MFATAL,
				       "WLAN: FW download failure @ %d, over max retry count\n",
				       offset);
				ret = MLAN_STATUS_FAILURE;
				goto done;
			}

			PRINTM(MERROR,
			       "WLAN: FW CRC error indicated by the helper:"
			       " len = 0x%04X, txlen = %d\n", len, txlen);
			len &= ~MBIT(0);
			if (fw_dnld_status & (MBIT(6) | MBIT(7))) {
				offset = 0;
				mic_retry++;
				if (mic_retry > MAX_FW_RETRY) {
					PRINTM(MFATAL,
					       "WLAN: FW download failure @ %d, over max mic retry count\n",
					       offset);
					ret = MLAN_STATUS_FAILURE;
					goto done;
				}
			}
			PRINTM(MERROR, "WLAN: retry: %d, offset %d\n", i,
			       offset);
			DBG_HEXDUMP(MERROR, "WLAN: FW block:", fwbuf, len);
			/* Setting this to 0 to resend from same offset */
			txlen = 0;
		} else {
			i = 0;

			/* Set blocksize to transfer - checking
			 * for last block */
			if (firmwarelen && firmwarelen - offset < txlen)
				txlen = firmwarelen - offset;
			PRINTM(MINFO, ".");

			tx_blocks = (txlen + MLAN_SDIO_BLOCK_SIZE_FW_DNLD - 1) /
				MLAN_SDIO_BLOCK_SIZE_FW_DNLD;

			/* Copy payload to buffer */
			if (firmware)
				memmove(pmadapter, fwbuf, &firmware[offset],
					txlen);
			else
				pcb->moal_get_fw_data(pmadapter->pmoal_handle,
						      offset, txlen, fwbuf);
		}

		/* Send data */
		memset(pmadapter, &mbuf, 0, sizeof(mlan_buffer));
		mbuf.pbuf = (t_u8 *)fwbuf;
		mbuf.data_len = tx_blocks * MLAN_SDIO_BLOCK_SIZE_FW_DNLD;

		ret = pcb->moal_write_data_sync(pmadapter->pmoal_handle, &mbuf,
						pmadapter->pcard_sd->ioport, 0);
		if (ret != MLAN_STATUS_SUCCESS) {
			PRINTM(MERROR,
			       "WLAN: FW download, write iomem (%d) failed @ %d\n",
			       i, offset);
			if (pcb->moal_write_reg(pmadapter->pmoal_handle,
						HOST_TO_CARD_EVENT_REG,
						HOST_TERM_CMD53) !=
			    MLAN_STATUS_SUCCESS) {
				PRINTM(MERROR, "write CFG reg failed\n");
			}
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}

		offset += txlen;
	} while (MTRUE);

	PRINTM(MMSG, "Wlan: FW download over, firmwarelen=%d downloaded %d\n",
	       firmwarelen, offset);

	ret = MLAN_STATUS_SUCCESS;
done:
	if (tmpfwbuf)
		pcb->moal_mfree(pmadapter->pmoal_handle, (t_u8 *)tmpfwbuf);

	LEAVE();
	return ret;
}

/**
 *  @brief This function disables the host interrupts.
 *
 *  @param pmadapter A pointer to mlan_adapter structure
 *  @return          MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_disable_sdio_host_int(pmlan_adapter pmadapter)
{
	mlan_status ret;

	ENTER();
	ret = wlan_sdio_disable_host_int_mask(pmadapter, HIM_DISABLE);
	LEAVE();
	return ret;
}

/**
 *  @brief This function decodes the rx packet &
 *  calls corresponding handlers according to the packet type
 *
 *  @param pmadapter A pointer to mlan_adapter structure
 *  @param pmbuf      A pointer to the SDIO data/cmd buffer
 *  @param upld_typ  Type of rx packet
 *  @param lock_flag  flag for spin_lock.
 *  @return          MLAN_STATUS_SUCCESS
 */
static mlan_status
wlan_decode_rx_packet(mlan_adapter *pmadapter,
		      mlan_buffer *pmbuf, t_u32 upld_typ, t_u8 lock_flag)
{
	t_u8 *cmd_buf;
	t_u32 event;
	t_u32 in_ts_sec, in_ts_usec;
	pmlan_callbacks pcb = &pmadapter->callbacks;

	ENTER();

	switch (upld_typ) {
	case MLAN_TYPE_SPA_DATA:
		PRINTM(MINFO, "--- Rx: SPA Data packet ---\n");
		pmbuf->data_len = pmadapter->upld_len;
		if (pmadapter->rx_work_flag) {
			pmbuf->buf_type = MLAN_BUF_TYPE_SPA_DATA;
			if (lock_flag)
				pmadapter->callbacks.moal_spin_lock(pmadapter->
								    pmoal_handle,
								    pmadapter->
								    rx_data_queue.
								    plock);
			util_enqueue_list_tail(pmadapter->pmoal_handle,
					       &pmadapter->rx_data_queue,
					       (pmlan_linked_list)pmbuf, MNULL,
					       MNULL);
			pmadapter->rx_pkts_queued++;
			if (lock_flag)
				pmadapter->callbacks.
					moal_spin_unlock(pmadapter->
							 pmoal_handle,
							 pmadapter->
							 rx_data_queue.plock);
		} else {
			wlan_decode_spa_buffer(pmadapter,
					       pmbuf->pbuf + pmbuf->data_offset,
					       pmbuf->data_len);
			wlan_free_mlan_buffer(pmadapter, pmbuf);
		}
		pmadapter->data_received = MTRUE;
		break;
	case MLAN_TYPE_DATA:
		PRINTM(MINFO, "--- Rx: Data packet ---\n");
		if (pmadapter->upld_len > pmbuf->data_len) {
			PRINTM(MERROR,
			       "SDIO: Drop packet upld_len=%d data_len=%d \n",
			       pmadapter->upld_len, pmbuf->data_len);
			wlan_free_mlan_buffer(pmadapter, pmbuf);
			break;
		}
		pmbuf->data_len = (pmadapter->upld_len - SDIO_INTF_HEADER_LEN);
		pmbuf->data_offset += SDIO_INTF_HEADER_LEN;
		if (pmadapter->rx_work_flag) {
			//rx_trace 5
			if (pmadapter->tp_state_on) {
				pmadapter->callbacks.
					moal_tp_accounting(pmadapter->
							   pmoal_handle, pmbuf,
							   5 /*RX_DROP_P1 */ );
				pcb->moal_get_system_time(pmadapter->
							  pmoal_handle,
							  &in_ts_sec,
							  &in_ts_usec);
				pmbuf->in_ts_sec = in_ts_sec;
				pmbuf->in_ts_usec = in_ts_usec;
			}
			if (pmadapter->tp_state_drop_point ==
			    5 /*RX_DROP_P1 */ ) {
				pmadapter->ops.data_complete(pmadapter, pmbuf,
							     MLAN_STATUS_SUCCESS);
			} else {
				if (lock_flag)
					pmadapter->callbacks.
						moal_spin_lock(pmadapter->
							       pmoal_handle,
							       pmadapter->
							       rx_data_queue.
							       plock);
				util_enqueue_list_tail(pmadapter->pmoal_handle,
						       &pmadapter->
						       rx_data_queue,
						       (pmlan_linked_list)pmbuf,
						       MNULL, MNULL);
				pmadapter->rx_pkts_queued++;
				if (pmadapter->tp_state_on)
					pmadapter->callbacks.
						moal_tp_accounting_rx_param
						(pmadapter->pmoal_handle, 1,
						 pmadapter->rx_pkts_queued);
				if (lock_flag)
					pmadapter->callbacks.
						moal_spin_unlock(pmadapter->
								 pmoal_handle,
								 pmadapter->
								 rx_data_queue.
								 plock);
			}
		} else {
			wlan_handle_rx_packet(pmadapter, pmbuf);
		}
		pmadapter->data_received = MTRUE;
		break;

	case MLAN_TYPE_CMD:
		PRINTM(MINFO, "--- Rx: Cmd Response ---\n");
		/* take care of curr_cmd = NULL case */
		if (!pmadapter->curr_cmd) {
			cmd_buf = pmadapter->upld_buf;
			if (pmadapter->ps_state == PS_STATE_SLEEP_CFM) {
				wlan_process_sleep_confirm_resp(pmadapter,
								pmbuf->pbuf +
								pmbuf->
								data_offset +
								SDIO_INTF_HEADER_LEN,
								pmadapter->
								upld_len -
								SDIO_INTF_HEADER_LEN);
			}
			pmadapter->upld_len -= SDIO_INTF_HEADER_LEN;
			memcpy_ext(pmadapter, cmd_buf,
				   pmbuf->pbuf + pmbuf->data_offset +
				   SDIO_INTF_HEADER_LEN,
				   pmadapter->upld_len - SDIO_INTF_HEADER_LEN,
				   MRVDRV_SIZE_OF_CMD_BUFFER);
			wlan_free_mlan_buffer(pmadapter, pmbuf);
		} else {
			pmadapter->cmd_resp_received = MTRUE;
			pmadapter->upld_len -= SDIO_INTF_HEADER_LEN;
			pmbuf->data_len = pmadapter->upld_len;
			pmbuf->data_offset += SDIO_INTF_HEADER_LEN;
			pmadapter->curr_cmd->respbuf = pmbuf;
			if (pmadapter->upld_len >= MRVDRV_SIZE_OF_CMD_BUFFER) {
				PRINTM(MMSG, "Invalid CmdResp len=%d\n",
				       pmadapter->upld_len);
				DBG_HEXDUMP(MERROR, "Invalid CmdResp",
					    pmbuf->pbuf + pmbuf->data_offset,
					    MAX_DATA_DUMP_LEN);
			}
		}
		break;

	case MLAN_TYPE_EVENT:
		PRINTM(MINFO, "--- Rx: Event ---\n");
		event = *(t_u32 *)&pmbuf->pbuf[pmbuf->data_offset +
					       SDIO_INTF_HEADER_LEN];
		pmadapter->event_cause = wlan_le32_to_cpu(event);
		if ((pmadapter->upld_len > MLAN_EVENT_HEADER_LEN) &&
		    ((pmadapter->upld_len - MLAN_EVENT_HEADER_LEN) <
		     MAX_EVENT_SIZE)) {
			memcpy_ext(pmadapter, pmadapter->event_body,
				   pmbuf->pbuf + pmbuf->data_offset +
				   MLAN_EVENT_HEADER_LEN,
				   pmadapter->upld_len - MLAN_EVENT_HEADER_LEN,
				   MAX_EVENT_SIZE);
		}

		/* event cause has been saved to adapter->event_cause */
		pmadapter->event_received = MTRUE;
		pmbuf->data_len = pmadapter->upld_len;
		pmadapter->pmlan_buffer_event = pmbuf;

		/* remove SDIO header */
		pmbuf->data_offset += SDIO_INTF_HEADER_LEN;
		pmbuf->data_len -= SDIO_INTF_HEADER_LEN;
		break;

	default:
		PRINTM(MERROR, "SDIO unknown upload type = 0x%x\n", upld_typ);
		wlan_free_mlan_buffer(pmadapter, pmbuf);
		break;
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function receives single packet
 *
 *  @param pmadapter A pointer to mlan_adapter structure
 *  @return          MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_receive_single_packet(mlan_adapter *pmadapter)
{
	mlan_buffer *pmbuf;
	t_u8 port;
	t_u16 rx_len;
	t_u32 pkt_type = 0;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();
	pmbuf = pmadapter->pcard_sd->mpa_rx.mbuf_arr[0];
	port = pmadapter->pcard_sd->mpa_rx.start_port;
	rx_len = pmadapter->pcard_sd->mpa_rx.len_arr[0];
	if (MLAN_STATUS_SUCCESS !=
	    wlan_sdio_card_to_host(pmadapter, &pkt_type,
				   (t_u32 *)&pmadapter->upld_len, pmbuf, rx_len,
				   pmadapter->pcard_sd->ioport + port)) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	if (pkt_type != MLAN_TYPE_DATA && pkt_type != MLAN_TYPE_SPA_DATA) {
		PRINTM(MERROR,
		       "receive a wrong pkt from DATA PORT: type=%d, len=%dd\n",
		       pkt_type, pmbuf->data_len);
		pmbuf->status_code = MLAN_ERROR_DATA_RX_FAIL;
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	pmadapter->pcard_sd->mpa_rx_count[0]++;
	wlan_decode_rx_packet(pmadapter, pmbuf, pkt_type, MTRUE);
done:
	if (ret != MLAN_STATUS_SUCCESS)
		wlan_free_mlan_buffer(pmadapter, pmbuf);
	MP_RX_AGGR_BUF_RESET(pmadapter);
	LEAVE();
	return ret;
}

/**
 *  @brief This function receives data from the card in aggregate mode.
 *
 *  @param pmadapter A pointer to mlan_adapter structure
 *  @return          MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_receive_mp_aggr_buf(mlan_adapter *pmadapter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_callbacks pcb = &pmadapter->callbacks;
	mlan_buffer mbuf_aggr;
	mlan_buffer *mbuf_deaggr;
	t_u32 pind = 0;
	t_u32 pkt_len, pkt_type = 0;
	t_u8 *curr_ptr;
	t_u32 cmd53_port = 0;
	t_u32 i = 0;
	t_u32 port_count = 0;
	t_bool new_mode = pmadapter->pcard_sd->supports_sdio_new_mode;

	/* do aggr RX now */
	PRINTM(MINFO, "do_rx_aggr: num of packets: %d\n",
	       pmadapter->pcard_sd->mpa_rx.pkt_cnt);

	memset(pmadapter, &mbuf_aggr, 0, sizeof(mlan_buffer));

	if (pmadapter->pcard_sd->mpa_rx.pkt_cnt == 1)
		return wlan_receive_single_packet(pmadapter);
	if (!pmadapter->pcard_sd->mpa_rx.buf) {
		mbuf_aggr.data_len = pmadapter->pcard_sd->mpa_rx.buf_len;
		mbuf_aggr.pnext = mbuf_aggr.pprev = &mbuf_aggr;
		mbuf_aggr.use_count = 0;
		for (pind = 0; pind < pmadapter->pcard_sd->mpa_rx.pkt_cnt;
		     pind++) {
			pmadapter->pcard_sd->mpa_rx.mbuf_arr[pind]->data_len =
				pmadapter->pcard_sd->mpa_rx.len_arr[pind];
			wlan_link_buf_to_aggr(&mbuf_aggr,
					      pmadapter->pcard_sd->mpa_rx.
					      mbuf_arr[pind]);
		}
	} else {
		mbuf_aggr.pbuf = (t_u8 *)pmadapter->pcard_sd->mpa_rx.buf;
		mbuf_aggr.data_len = pmadapter->pcard_sd->mpa_rx.buf_len;
	}

	if (new_mode) {
		port_count = bitcount(pmadapter->pcard_sd->mpa_rx.ports) - 1;
		/* port_count = pmadapter->mpa_rx.pkt_cnt - 1; */
		cmd53_port = (pmadapter->pcard_sd->ioport | SDIO_MPA_ADDR_BASE |
			      (port_count << 8)) +
			pmadapter->pcard_sd->mpa_rx.start_port;
	} else {
		cmd53_port = (pmadapter->pcard_sd->ioport | SDIO_MPA_ADDR_BASE |
			      (pmadapter->pcard_sd->mpa_rx.ports << 4)) +
			pmadapter->pcard_sd->mpa_rx.start_port;
	}
	do {
		ret = pcb->moal_read_data_sync(pmadapter->pmoal_handle,
					       &mbuf_aggr, cmd53_port, 0);
		if (ret != MLAN_STATUS_SUCCESS) {
			PRINTM(MERROR,
			       "wlan: sdio mp cmd53 read failed: %d ioport=0x%x retry=%d\n",
			       ret, cmd53_port, i);
			i++;
			if (MLAN_STATUS_SUCCESS !=
			    pcb->moal_write_reg(pmadapter->pmoal_handle,
						HOST_TO_CARD_EVENT_REG,
						HOST_TERM_CMD53)) {
				PRINTM(MERROR, "Set Term cmd53 failed\n");
			}
			if (i > MAX_WRITE_IOMEM_RETRY) {
				ret = MLAN_STATUS_FAILURE;
				goto done;
			}
		}
	} while (ret == MLAN_STATUS_FAILURE);
	if (pmadapter->rx_work_flag)
		pmadapter->callbacks.moal_spin_lock(pmadapter->pmoal_handle,
						    pmadapter->rx_data_queue.
						    plock);
	if (!pmadapter->pcard_sd->mpa_rx.buf &&
	    pmadapter->pcard_sd->mpa_rx.pkt_cnt > 1) {
		for (pind = 0; pind < pmadapter->pcard_sd->mpa_rx.pkt_cnt;
		     pind++) {
			mbuf_deaggr =
				pmadapter->pcard_sd->mpa_rx.mbuf_arr[pind];
			pkt_len =
				wlan_le16_to_cpu(*(t_u16 *)
						 (mbuf_deaggr->pbuf +
						  mbuf_deaggr->data_offset));
			pkt_type =
				wlan_le16_to_cpu(*(t_u16 *)
						 (mbuf_deaggr->pbuf +
						  mbuf_deaggr->data_offset +
						  2));
			pmadapter->upld_len = pkt_len;
			wlan_decode_rx_packet(pmadapter, mbuf_deaggr, pkt_type,
					      MFALSE);
		}
	} else {
		DBG_HEXDUMP(MIF_D, "SDIO MP-A Blk Rd",
			    pmadapter->pcard_sd->mpa_rx.buf,
			    MIN(pmadapter->pcard_sd->mpa_rx.buf_len,
				MAX_DATA_DUMP_LEN));

		curr_ptr = pmadapter->pcard_sd->mpa_rx.buf;

		for (pind = 0; pind < pmadapter->pcard_sd->mpa_rx.pkt_cnt;
		     pind++) {
			/* get curr PKT len & type */
			pkt_len = wlan_le16_to_cpu(*(t_u16 *)&curr_ptr[0]);
			pkt_type = wlan_le16_to_cpu(*(t_u16 *)&curr_ptr[2]);

			PRINTM(MINFO, "RX: [%d] pktlen: %d pkt_type: 0x%x\n",
			       pind, pkt_len, pkt_type);

			/* copy pkt to deaggr buf */
			mbuf_deaggr =
				pmadapter->pcard_sd->mpa_rx.mbuf_arr[pind];
			if ((pkt_type == MLAN_TYPE_DATA
			     || pkt_type == MLAN_TYPE_SPA_DATA) &&
			    (pkt_len <=
			     pmadapter->pcard_sd->mpa_rx.len_arr[pind])) {
				memcpy_ext(pmadapter,
					   mbuf_deaggr->pbuf +
					   mbuf_deaggr->data_offset,
					   curr_ptr, pkt_len, pkt_len);
				pmadapter->upld_len = pkt_len;
				/* Process de-aggr packet */
				wlan_decode_rx_packet(pmadapter, mbuf_deaggr,
						      pkt_type, MFALSE);
			} else {
				PRINTM(MERROR,
				       "Wrong aggr packet: type=%d, len=%d, max_len=%d\n",
				       pkt_type, pkt_len,
				       pmadapter->pcard_sd->mpa_rx.
				       len_arr[pind]);
				wlan_free_mlan_buffer(pmadapter, mbuf_deaggr);
			}
			curr_ptr += pmadapter->pcard_sd->mpa_rx.len_arr[pind];
		}
	}
	if (pmadapter->rx_work_flag)
		pmadapter->callbacks.moal_spin_unlock(pmadapter->pmoal_handle,
						      pmadapter->rx_data_queue.
						      plock);
	pmadapter->pcard_sd->mpa_rx_count[pmadapter->pcard_sd->mpa_rx.pkt_cnt -
					  1]++;
	MP_RX_AGGR_BUF_RESET(pmadapter);
done:
	return ret;
}

/**
 *  @brief This function receives data from the card in aggregate mode.
 *
 *  @param pmadapter A pointer to mlan_adapter structure
 *  @param pmbuf      A pointer to the SDIO data/cmd buffer
 *  @param port      Current port on which packet needs to be rxed
 *  @param rx_len    Length of received packet
 *  @return          MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_sdio_card_to_host_mp_aggr(mlan_adapter *pmadapter,
			       mlan_buffer *pmbuf, t_u8 port, t_u16 rx_len)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_s32 f_do_rx_aggr = 0;
	t_s32 f_do_rx_cur = 0;
	t_s32 f_aggr_cur = 0;
	t_s32 f_post_aggr_cur = 0;
	t_u32 pind = 0;
	t_u32 pkt_type = 0;
	const mlan_sdio_card_reg *reg = pmadapter->pcard_sd->reg;
	t_bool new_mode = pmadapter->pcard_sd->supports_sdio_new_mode;

	ENTER();
	if (!new_mode && (port == CTRL_PORT)) {
		/* Read the command response or event without aggr */
		PRINTM(MINFO,
		       "card_2_host_mp_aggr: No aggr for control port\n");

		f_do_rx_cur = 1;
		goto rx_curr_single;
	}

	if (!pmadapter->pcard_sd->mpa_rx.enabled) {
		PRINTM(MINFO,
		       "card_2_host_mp_aggr: rx aggregation disabled !\n");

		f_do_rx_cur = 1;
		goto rx_curr_single;
	}
	if ((new_mode &&
	     (pmadapter->pcard_sd->mp_rd_bitmap & reg->data_port_mask))
	    || (!new_mode &&
		(pmadapter->pcard_sd->
		 mp_rd_bitmap & (~((t_u32)CTRL_PORT_MASK))))
		) {
		/* Some more data RX pending */
		PRINTM(MINFO, "card_2_host_mp_aggr: Not last packet\n");

		if (MP_RX_AGGR_IN_PROGRESS(pmadapter)) {
			if (MP_RX_AGGR_BUF_HAS_ROOM(pmadapter, rx_len)) {
				f_aggr_cur = 1;
			} else {
				/* No room in Aggr buf, do rx aggr now */
				f_do_rx_aggr = 1;
				f_post_aggr_cur = 1;
			}
		} else {
			/* Rx aggr not in progress */
			f_aggr_cur = 1;
		}

	} else {
		/* No more data RX pending */
		PRINTM(MINFO, "card_2_host_mp_aggr: Last packet\n");

		if (MP_RX_AGGR_IN_PROGRESS(pmadapter)) {
			f_do_rx_aggr = 1;
			if (MP_RX_AGGR_BUF_HAS_ROOM(pmadapter, rx_len)) {
				f_aggr_cur = 1;
			} else {
				/* No room in Aggr buf, do rx aggr now */
				f_do_rx_cur = 1;
			}
		} else {
			f_do_rx_cur = 1;
		}
	}

	if (f_aggr_cur) {
		PRINTM(MINFO, "Current packet aggregation.\n");
		/* Curr pkt can be aggregated */
		if (new_mode)
			MP_RX_AGGR_SETUP(pmadapter, pmbuf, port, rx_len);
		else
			MP_RX_AGGR_SETUP_NONEWMODE(pmadapter, pmbuf, port,
						   rx_len);
		if (MP_RX_AGGR_PKT_LIMIT_REACHED(pmadapter) ||
		    ((new_mode && MP_RX_AGGR_PORT_LIMIT_REACHED(pmadapter))
		     || (!new_mode &&
			 MP_RX_AGGR_PORT_LIMIT_REACHED_NONEWMODE(pmadapter))
		    )) {
			PRINTM(MINFO,
			       "card_2_host_mp_aggr: Aggregation Packet limit reached\n");
			/* No more pkts allowed in Aggr buf, rx it */
			f_do_rx_aggr = 1;
		}
	}

	if (f_do_rx_aggr) {
		/* do aggr RX now */
		if (MLAN_STATUS_SUCCESS != wlan_receive_mp_aggr_buf(pmadapter)) {
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
	}
rx_curr_single:
	if (f_do_rx_cur) {
		PRINTM(MINFO, "RX: f_do_rx_cur: port: %d rx_len: %d\n", port,
		       rx_len);

		if (MLAN_STATUS_SUCCESS !=
		    wlan_sdio_card_to_host(pmadapter, &pkt_type,
					   (t_u32 *)&pmadapter->upld_len, pmbuf,
					   rx_len,
					   pmadapter->pcard_sd->ioport +
					   port)) {
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
		if (!new_mode &&
		    ((port == CTRL_PORT) &&
		     ((pkt_type != MLAN_TYPE_EVENT) &&
		      (pkt_type != MLAN_TYPE_CMD)))) {
			PRINTM(MERROR,
			       "Wrong pkt from CTRL PORT: type=%d, len=%dd\n",
			       pkt_type, pmbuf->data_len);
			pmbuf->status_code = MLAN_ERROR_DATA_RX_FAIL;
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
		if (new_mode || (port != CTRL_PORT)
			) {
			if (pkt_type != MLAN_TYPE_DATA
			    && pkt_type != MLAN_TYPE_SPA_DATA) {
				PRINTM(MERROR,
				       "receive a wrong pkt from DATA PORT: type=%d, len=%dd\n",
				       pkt_type, pmbuf->data_len);
				pmbuf->status_code = MLAN_ERROR_DATA_RX_FAIL;
				ret = MLAN_STATUS_FAILURE;
				goto done;
			}
		}

		if (new_mode || (port != CTRL_PORT)
			)
			pmadapter->pcard_sd->mpa_rx_count[0]++;

		wlan_decode_rx_packet(pmadapter, pmbuf, pkt_type, MTRUE);
	}
	if (f_post_aggr_cur) {
		PRINTM(MINFO, "Current packet aggregation.\n");
		/* Curr pkt can be aggregated */
		if (new_mode)
			MP_RX_AGGR_SETUP(pmadapter, pmbuf, port, rx_len);
		else
			MP_RX_AGGR_SETUP_NONEWMODE(pmadapter, pmbuf, port,
						   rx_len);
	}
done:
	if (ret == MLAN_STATUS_FAILURE) {
		if (MP_RX_AGGR_IN_PROGRESS(pmadapter)) {
			/* MP-A transfer failed - cleanup */
			for (pind = 0;
			     pind < pmadapter->pcard_sd->mpa_rx.pkt_cnt;
			     pind++) {
				wlan_free_mlan_buffer(pmadapter,
						      pmadapter->pcard_sd->
						      mpa_rx.mbuf_arr[pind]);
			}
			MP_RX_AGGR_BUF_RESET(pmadapter);
		}

		if (f_do_rx_cur) {
			/* Single Transfer pending */
			/* Free curr buff also */
			wlan_free_mlan_buffer(pmadapter, pmbuf);
		}
	}

	LEAVE();
	return ret;
}

/**
 *  @brief This function sends aggr buf
 *
 *  @param pmadapter A pointer to mlan_adapter structure
 *  @return          MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_send_mp_aggr_buf(mlan_adapter *pmadapter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u32 cmd53_port = 0;
	t_u32 port_count = 0;
	mlan_buffer mbuf_aggr;
	t_u8 i = 0;
	t_u8 mp_aggr_pkt_limit = pmadapter->pcard_sd->mp_aggr_pkt_limit;
	t_bool new_mode = pmadapter->pcard_sd->supports_sdio_new_mode;

	ENTER();

	if (!pmadapter->pcard_sd->mpa_tx.pkt_cnt) {
		LEAVE();
		return ret;
	}
	PRINTM(MINFO,
	       "host_2_card_mp_aggr: Send aggregation buffer."
	       "%d %d\n",
	       pmadapter->pcard_sd->mpa_tx.start_port,
	       pmadapter->pcard_sd->mpa_tx.ports);

	memset(pmadapter, &mbuf_aggr, 0, sizeof(mlan_buffer));

	if (!pmadapter->pcard_sd->mpa_tx.buf &&
	    pmadapter->pcard_sd->mpa_tx.pkt_cnt > 1) {
		mbuf_aggr.data_len = pmadapter->pcard_sd->mpa_tx.buf_len;
		mbuf_aggr.pnext = mbuf_aggr.pprev = &mbuf_aggr;
		mbuf_aggr.use_count = 0;
		for (i = 0; i < pmadapter->pcard_sd->mpa_tx.pkt_cnt; i++)
			wlan_link_buf_to_aggr(&mbuf_aggr,
					      pmadapter->pcard_sd->mpa_tx.
					      mbuf_arr[i]);
	} else {
		mbuf_aggr.pbuf = (t_u8 *)pmadapter->pcard_sd->mpa_tx.buf;
		mbuf_aggr.data_len = pmadapter->pcard_sd->mpa_tx.buf_len;
	}

	if (new_mode) {
		port_count = bitcount(pmadapter->pcard_sd->mpa_tx.ports) - 1;
		cmd53_port = (pmadapter->pcard_sd->ioport | SDIO_MPA_ADDR_BASE |
			      (port_count << 8)) +
			pmadapter->pcard_sd->mpa_tx.start_port;
	} else {
		cmd53_port = (pmadapter->pcard_sd->ioport | SDIO_MPA_ADDR_BASE |
			      (pmadapter->pcard_sd->mpa_tx.ports << 4)) +
			pmadapter->pcard_sd->mpa_tx.start_port;
	}
	if (pmadapter->pcard_sd->mpa_tx.pkt_cnt == 1)
		cmd53_port = pmadapter->pcard_sd->ioport +
			pmadapter->pcard_sd->mpa_tx.start_port;
	/** only one packet */
	if (!pmadapter->pcard_sd->mpa_tx.buf &&
	    pmadapter->pcard_sd->mpa_tx.pkt_cnt == 1)
		ret = wlan_write_data_sync(pmadapter,
					   pmadapter->pcard_sd->mpa_tx.
					   mbuf_arr[0], cmd53_port);
	else
		ret = wlan_write_data_sync(pmadapter, &mbuf_aggr, cmd53_port);
	if (!pmadapter->pcard_sd->mpa_tx.buf) {
		/** free mlan buffer */
		for (i = 0; i < pmadapter->pcard_sd->mpa_tx.pkt_cnt; i++) {
			wlan_write_data_complete(pmadapter,
						 pmadapter->pcard_sd->mpa_tx.
						 mbuf_arr[i],
						 MLAN_STATUS_SUCCESS);
		}
	}
	if (!(pmadapter->pcard_sd->mp_wr_bitmap &
	      (1 << pmadapter->pcard_sd->curr_wr_port)) &&
	    (pmadapter->pcard_sd->mpa_tx.pkt_cnt < mp_aggr_pkt_limit))
		pmadapter->pcard_sd->mpa_sent_no_ports++;
	pmadapter->pcard_sd->mpa_tx_count[pmadapter->pcard_sd->mpa_tx.pkt_cnt -
					  1]++;
	pmadapter->pcard_sd->last_mp_wr_bitmap[pmadapter->pcard_sd->
					       last_mp_index] =
		pmadapter->pcard_sd->mp_wr_bitmap;
	pmadapter->pcard_sd->last_mp_wr_ports[pmadapter->pcard_sd->
					      last_mp_index] = cmd53_port;
	pmadapter->pcard_sd->last_mp_wr_len[pmadapter->pcard_sd->
					    last_mp_index] =
		pmadapter->pcard_sd->mpa_tx.buf_len;
	pmadapter->pcard_sd->last_curr_wr_port[pmadapter->pcard_sd->
					       last_mp_index] =
		pmadapter->pcard_sd->curr_wr_port;
	memcpy_ext(pmadapter,
		   (t_u8 *)&pmadapter->pcard_sd->last_mp_wr_info[pmadapter->
								 pcard_sd->
								 last_mp_index *
								 mp_aggr_pkt_limit],
		   (t_u8 *)pmadapter->pcard_sd->mpa_tx.mp_wr_info,
		   mp_aggr_pkt_limit * sizeof(t_u16),
		   mp_aggr_pkt_limit * sizeof(t_u16));
	pmadapter->pcard_sd->last_mp_index++;
	if (pmadapter->pcard_sd->last_mp_index >= SDIO_MP_DBG_NUM)
		pmadapter->pcard_sd->last_mp_index = 0;
	MP_TX_AGGR_BUF_RESET(pmadapter);
	LEAVE();
	return ret;
}

/**
 *  @brief This function sends data to the card in SDIO aggregated mode.
 *
 *  @param pmadapter A pointer to mlan_adapter structure
 *  @param mbuf      A pointer to the SDIO data/cmd buffer
 *  @param port	     current port for aggregation
 *  @param next_pkt_len Length of next packet used for multiport aggregation
 *  @return         MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_host_to_card_mp_aggr(mlan_adapter *pmadapter,
			  mlan_buffer *mbuf, t_u8 port, t_u32 next_pkt_len)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_s32 f_send_aggr_buf = 0;
	t_s32 f_send_cur_buf = 0;
	t_s32 f_precopy_cur_buf = 0;
	t_s32 f_postcopy_cur_buf = 0;
	t_u8 aggr_sg = 0;
	t_u8 mp_aggr_pkt_limit = pmadapter->pcard_sd->mp_aggr_pkt_limit;
	t_bool new_mode = pmadapter->pcard_sd->supports_sdio_new_mode;

	ENTER();

	PRINTM(MIF_D, "host_2_card_mp_aggr: next_pkt_len: %d curr_port:%d\n",
	       next_pkt_len, port);

	if (!pmadapter->pcard_sd->mpa_tx.enabled) {
		PRINTM(MINFO,
		       "host_2_card_mp_aggr: tx aggregation disabled !\n");
		f_send_cur_buf = 1;
		goto tx_curr_single;
	}

	if (next_pkt_len) {
		/* More pkt in TX queue */
		PRINTM(MINFO, "host_2_card_mp_aggr: More packets in Queue.\n");

		if (MP_TX_AGGR_IN_PROGRESS(pmadapter)) {
			if (MP_TX_AGGR_BUF_HAS_ROOM(pmadapter, mbuf,
						    mbuf->data_len)) {
				f_precopy_cur_buf = 1;

				if (!(pmadapter->pcard_sd->mp_wr_bitmap &
				      (1
				       << pmadapter->pcard_sd->curr_wr_port)) ||
				    !MP_TX_AGGR_BUF_HAS_ROOM(pmadapter, mbuf,
							     mbuf->data_len +
							     next_pkt_len)) {
					f_send_aggr_buf = 1;
				}
			} else {
				/* No room in Aggr buf, send it */
				f_send_aggr_buf = 1;

				if (!(pmadapter->pcard_sd->mp_wr_bitmap &
				      (1
				       << pmadapter->pcard_sd->curr_wr_port))) {
					f_send_cur_buf = 1;
				} else {
					f_postcopy_cur_buf = 1;
				}
			}
		} else {
			if (MP_TX_AGGR_BUF_HAS_ROOM(pmadapter, mbuf,
						    mbuf->data_len) &&
			    (pmadapter->pcard_sd->mp_wr_bitmap &
			     (1 << pmadapter->pcard_sd->curr_wr_port)))
				f_precopy_cur_buf = 1;
			else
				f_send_cur_buf = 1;
		}
	} else {
		/* Last pkt in TX queue */
		PRINTM(MINFO,
		       "host_2_card_mp_aggr: Last packet in Tx Queue.\n");

		if (MP_TX_AGGR_IN_PROGRESS(pmadapter)) {
			/* some packs in Aggr buf already */
			f_send_aggr_buf = 1;

			if (MP_TX_AGGR_BUF_HAS_ROOM(pmadapter, mbuf,
						    mbuf->data_len)) {
				f_precopy_cur_buf = 1;
			} else {
				/* No room in Aggr buf, send it */
				f_send_cur_buf = 1;
			}
		} else {
			f_send_cur_buf = 1;
		}
		pmadapter->pcard_sd->mpa_sent_last_pkt++;
	}

	if (f_precopy_cur_buf) {
		PRINTM(MINFO, "host_2_card_mp_aggr: Precopy current buffer\n");
		if (pmadapter->pcard_sd->mpa_buf)
			memcpy_ext(pmadapter,
				   pmadapter->pcard_sd->mpa_buf +
				   (pmadapter->pcard_sd->last_mp_index *
				    mp_aggr_pkt_limit +
				    pmadapter->pcard_sd->mpa_tx.pkt_cnt) *
				   MLAN_SDIO_BLOCK_SIZE,
				   mbuf->pbuf + mbuf->data_offset,
				   MLAN_SDIO_BLOCK_SIZE, MLAN_SDIO_BLOCK_SIZE);
		if (!pmadapter->pcard_sd->mpa_tx.buf) {
			if (new_mode)
				MP_TX_AGGR_BUF_PUT_SG(pmadapter, mbuf, port);
			else
				MP_TX_AGGR_BUF_PUT_SG_NONEWMODE(pmadapter, mbuf,
								port);
			aggr_sg = MTRUE;
		} else {
			if (new_mode)
				MP_TX_AGGR_BUF_PUT(pmadapter, mbuf, port);
			else
				MP_TX_AGGR_BUF_PUT_NONEWMODE(pmadapter, mbuf,
							     port);
		}
		if (MP_TX_AGGR_PKT_LIMIT_REACHED(pmadapter)
		    || (!new_mode && MP_TX_AGGR_PORT_LIMIT_REACHED(pmadapter))
			) {
			PRINTM(MIF_D,
			       "host_2_card_mp_aggr: Aggregation Pkt limit reached\n");
			/* No more pkts allowed in Aggr buf, send it */
			f_send_aggr_buf = 1;
		}
	}

	if (f_send_aggr_buf)
		ret = wlan_send_mp_aggr_buf(pmadapter);

tx_curr_single:
	if (f_send_cur_buf) {
		PRINTM(MINFO, "host_2_card_mp_aggr: writing to port #%d\n",
		       port);
		ret = wlan_write_data_sync(pmadapter, mbuf,
					   pmadapter->pcard_sd->ioport + port);
		if (!(pmadapter->pcard_sd->mp_wr_bitmap &
		      (1 << pmadapter->pcard_sd->curr_wr_port)))
			pmadapter->pcard_sd->mpa_sent_no_ports++;
		pmadapter->pcard_sd->last_mp_wr_bitmap[pmadapter->pcard_sd->
						       last_mp_index] =
			pmadapter->pcard_sd->mp_wr_bitmap;
		pmadapter->pcard_sd->last_mp_wr_ports[pmadapter->pcard_sd->
						      last_mp_index] =
			pmadapter->pcard_sd->ioport + port;
		pmadapter->pcard_sd->last_mp_wr_len[pmadapter->pcard_sd->
						    last_mp_index] =
			mbuf->data_len;
		memset(pmadapter,
		       (t_u8 *)&pmadapter->pcard_sd->last_mp_wr_info[pmadapter->
								     pcard_sd->
								     last_mp_index
								     *
								     mp_aggr_pkt_limit],
		       0, sizeof(t_u16) * mp_aggr_pkt_limit);
		pmadapter->pcard_sd->last_mp_wr_info[pmadapter->pcard_sd->
						     last_mp_index *
						     mp_aggr_pkt_limit] =
			*(t_u16 *)(mbuf->pbuf + mbuf->data_offset);
		pmadapter->pcard_sd->last_curr_wr_port[pmadapter->pcard_sd->
						       last_mp_index] =
			pmadapter->pcard_sd->curr_wr_port;
		if (pmadapter->pcard_sd->mpa_buf)
			memcpy_ext(pmadapter,
				   pmadapter->pcard_sd->mpa_buf +
				   (pmadapter->pcard_sd->last_mp_index *
				    mp_aggr_pkt_limit *
				    MLAN_SDIO_BLOCK_SIZE),
				   mbuf->pbuf + mbuf->data_offset,
				   MLAN_SDIO_BLOCK_SIZE, MLAN_SDIO_BLOCK_SIZE);
		pmadapter->pcard_sd->last_mp_index++;
		if (pmadapter->pcard_sd->last_mp_index >= SDIO_MP_DBG_NUM)
			pmadapter->pcard_sd->last_mp_index = 0;
		pmadapter->pcard_sd->mpa_tx_count[0]++;
	}
	if (f_postcopy_cur_buf) {
		PRINTM(MINFO, "host_2_card_mp_aggr: Postcopy current buffer\n");
		if (pmadapter->pcard_sd->mpa_buf)
			memcpy_ext(pmadapter,
				   pmadapter->pcard_sd->mpa_buf +
				   (pmadapter->pcard_sd->last_mp_index *
				    mp_aggr_pkt_limit +
				    pmadapter->pcard_sd->mpa_tx.pkt_cnt) *
				   MLAN_SDIO_BLOCK_SIZE,
				   mbuf->pbuf + mbuf->data_offset,
				   MLAN_SDIO_BLOCK_SIZE, MLAN_SDIO_BLOCK_SIZE);
		if (!pmadapter->pcard_sd->mpa_tx.buf) {
			if (new_mode)
				MP_TX_AGGR_BUF_PUT_SG(pmadapter, mbuf, port);
			else
				MP_TX_AGGR_BUF_PUT_SG_NONEWMODE(pmadapter, mbuf,
								port);
			aggr_sg = MTRUE;
		} else {
			if (new_mode)
				MP_TX_AGGR_BUF_PUT(pmadapter, mbuf, port);
			else
				MP_TX_AGGR_BUF_PUT_NONEWMODE(pmadapter, mbuf,
							     port);
		}
	}
	/* Always return PENDING in SG mode */
	if (aggr_sg)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/********************************************************
		Global functions
********************************************************/

/**
 *  @brief This function checks if the interface is ready to download
 *  or not while other download interface is present
 *
 *  @param pmadapter  A pointer to mlan_adapter structure
 *  @param val        Winner status (0: winner)
 *  @return           MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 *
 */
static mlan_status
wlan_sdio_check_winner_status(mlan_adapter *pmadapter, t_u32 *val)
{
	t_u32 winner = 0;
	pmlan_callbacks pcb;
	t_u8 card_winner_check_reg = pmadapter->pcard_sd->reg->winner_check_reg;

	ENTER();

#ifdef SD8801
	if (IS_SD8801(pmadapter->card_type)) {
		*val = 0;
		return MLAN_STATUS_SUCCESS;
	}
#endif
	pcb = &pmadapter->callbacks;

	if (MLAN_STATUS_SUCCESS != pcb->moal_read_reg(pmadapter->pmoal_handle,
						      card_winner_check_reg,
						      &winner)) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	*val = winner;

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function checks if the firmware is ready to accept
 *  command or not.
 *
 *  @param pmadapter  A pointer to mlan_adapter structure
 *  @param pollnum    Maximum polling number
 *  @return           MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_sdio_check_fw_status(mlan_adapter *pmadapter, t_u32 pollnum)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 firmwarestat = 0;
	t_u32 tries;

	ENTER();

	/* Wait for firmware initialization event */
	for (tries = 0; tries < pollnum; tries++) {
		ret = wlan_sdio_read_fw_status(pmadapter, &firmwarestat);
		if (MLAN_STATUS_SUCCESS != ret)
			continue;
		if (firmwarestat == SDIO_FIRMWARE_READY) {
			ret = MLAN_STATUS_SUCCESS;
			break;
		} else {
			wlan_mdelay(pmadapter, 100);
			ret = MLAN_STATUS_FAILURE;
		}
	}

	if (ret != MLAN_STATUS_SUCCESS) {
		if (pollnum > 1)
			PRINTM(MERROR,
			       "Fail to poll firmware status: firmwarestat=0x%x\n",
			       firmwarestat);
		goto done;
	}

done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function enables the host interrupts.
 *
 *  @param pmadapter A pointer to mlan_adapter structure
 *  @return          MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_enable_sdio_host_int(pmlan_adapter pmadapter)
{
	mlan_status ret;
	t_u8 mask = pmadapter->pcard_sd->reg->host_int_enable;

	ENTER();
	ret = wlan_sdio_enable_host_int_mask(pmadapter, mask);
	LEAVE();
	return ret;
}

/**
 *  @brief  This function downloads firmware to card
 *
 *  @param pmadapter	A pointer to mlan_adapter
 *  @param pmfw			A pointer to firmware image
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_sdio_dnld_fw(pmlan_adapter pmadapter, pmlan_fw_image pmfw)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u32 poll_num = 1;
	t_u32 winner = 0;

	ENTER();

/*when using GPIO wakeup, don't run the below code.
 *if using GPIO wakeup, host will do handshake with FW
 *to check if FW wake up and pull up SDIO line, then reload driver.
 *So when using GPIO wakeup, don't need driver to do check wakeup status again.
 *when using SDIO interface wakeup, run the below code;
 *if using SDIO interface wakeup, driver need to do check wakeup status with FW.
 */

	/* Card specific probing */
	ret = wlan_sdio_probe(pmadapter);
	if (ret == MLAN_STATUS_FAILURE) {
		PRINTM(MERROR, "WLAN SDIO probe failed\n", ret);
		LEAVE();
		return ret;
	}

	/* Check if firmware is already running */
	ret = wlan_sdio_check_fw_status(pmadapter, poll_num);
	if (ret == MLAN_STATUS_SUCCESS) {
#if defined(SDIO)
		if (pmfw->fw_reload == FW_RELOAD_SDIO_INBAND_RESET) {
			PRINTM(MMSG, "Try reset fw in mlan\n");
			ret = wlan_reset_fw(pmadapter);
			if (ret == MLAN_STATUS_FAILURE) {
				PRINTM(MERROR, "FW reset failure!");
				LEAVE();
				return ret;
			}
		} else {
#endif
			PRINTM(MMSG,
			       "WLAN FW already running! Skip FW download\n");
#if defined(SDIO)
			pmadapter->ops.wakeup_card(pmadapter, MFALSE);
#endif
			goto done;
#if defined(SDIO)
		}
#endif
	}
	poll_num = MAX_FIRMWARE_POLL_TRIES;
	/* Check if other interface is downloading */
	ret = wlan_sdio_check_winner_status(pmadapter, &winner);
	if (ret == MLAN_STATUS_FAILURE) {
		PRINTM(MFATAL, "WLAN read winner status failed!\n");
		goto done;
	}
	if (winner) {
		PRINTM(MMSG,
		       "WLAN is not the winner (0x%x). Skip FW download\n",
		       winner);
		poll_num = MAX_MULTI_INTERFACE_POLL_TRIES;
		goto poll_fw;
	}

	/* Download the firmware image via helper */
	ret = wlan_sdio_prog_fw_w_helper(pmadapter, pmfw->pfw_buf,
					 pmfw->fw_len);
	if (ret != MLAN_STATUS_SUCCESS) {
		PRINTM(MERROR, "wlan_dnld_fw fail ret=0x%x\n", ret);
		LEAVE();
		return ret;
	}

poll_fw:
	/* Check if the firmware is downloaded successfully or not */
	ret = wlan_sdio_check_fw_status(pmadapter, poll_num);
	if (ret != MLAN_STATUS_SUCCESS) {
		PRINTM(MFATAL, "FW failed to be active in time!\n");
		ret = MLAN_STATUS_FAILURE;
		LEAVE();
		return ret;
	}
#ifdef SD9177
	if (IS_SD9177(pmadapter->card_type))
		wlan_mdelay(pmadapter, 1000);
#endif
done:

	/* re-enable host interrupt for mlan after fw dnld is successful */
	wlan_enable_sdio_host_int(pmadapter);

	LEAVE();
	return ret;
}

/**
 *  @brief This function probes the driver
 *
 *  @param pmadapter  A pointer to mlan_adapter structure
 *  @return           MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_sdio_probe(pmlan_adapter pmadapter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u32 sdio_ireg = 0;
	pmlan_callbacks pcb = &pmadapter->callbacks;

	ENTER();
	/*
	 * Read the HOST_INT_STATUS_REG for ACK the first interrupt got
	 * from the bootloader. If we don't do this we get a interrupt
	 * as soon as we register the irq.
	 */
	pcb->moal_read_reg(pmadapter->pmoal_handle,
			   pmadapter->pcard_sd->reg->host_int_status_reg,
			   &sdio_ireg);

	/* Disable host interrupt mask register for SDIO */
	ret = wlan_disable_sdio_host_int(pmadapter);
	if (ret != MLAN_STATUS_SUCCESS) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	/* Get SDIO ioport */
	ret = wlan_sdio_init_ioport(pmadapter);
	LEAVE();
	return ret;
}

/**
 *  @brief This function get sdio device from card type
 *
 *  @param pmadapter  A pointer to mlan_adapter structure
 *  @return           MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_get_sdio_device(pmlan_adapter pmadapter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 card_type = pmadapter->card_type;

	ENTER();

	ret = pmadapter->callbacks.moal_malloc(pmadapter->pmoal_handle,
					       sizeof(mlan_sdio_card),
					       MLAN_MEM_DEF,
					       (t_u8 **)&pmadapter->pcard_sd);
	if (ret != MLAN_STATUS_SUCCESS || !pmadapter->pcard_sd) {
		PRINTM(MERROR, "Failed to allocate pcard_sd\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	pmadapter->pcard_sd->max_ports = MAX_PORT;
	pmadapter->pcard_sd->mp_aggr_pkt_limit = SDIO_MP_AGGR_DEF_PKT_LIMIT;
	pmadapter->pcard_sd->supports_sdio_new_mode = MTRUE;
	pmadapter->pcard_sd->mp_tx_aggr_buf_size = SDIO_MP_AGGR_BUF_SIZE_MAX;
	pmadapter->pcard_sd->mp_rx_aggr_buf_size = SDIO_MP_AGGR_BUF_SIZE_MAX;

	switch (card_type) {
#ifdef SD8801
	case CARD_TYPE_SD8801:
		pmadapter->pcard_sd->reg = &mlan_reg_sd8801;
		pmadapter->pcard_info = &mlan_card_info_sd8801;
		pmadapter->pcard_sd->max_ports = MAX_PORT_16;
		pmadapter->pcard_sd->mp_aggr_pkt_limit =
			SDIO_MP_AGGR_DEF_PKT_LIMIT_8;
		pmadapter->pcard_sd->supports_sdio_new_mode = MFALSE;
		pmadapter->pcard_sd->mp_tx_aggr_buf_size =
			SDIO_MP_AGGR_BUF_SIZE_32K;
		pmadapter->pcard_sd->mp_rx_aggr_buf_size =
			SDIO_MP_AGGR_BUF_SIZE_32K;
		break;
#endif
#ifdef SD8887
	case CARD_TYPE_SD8887:
		pmadapter->pcard_sd->reg = &mlan_reg_sd8887;
		pmadapter->pcard_info = &mlan_card_info_sd8887;
		break;
#endif
#ifdef SD8897
	case CARD_TYPE_SD8897:
		pmadapter->pcard_sd->reg = &mlan_reg_sd8897;
		pmadapter->pcard_info = &mlan_card_info_sd8897;
		break;
#endif
#if defined(SD8977) || defined(SD8978)
	case CARD_TYPE_SD8977:
	case CARD_TYPE_SD8978:
		pmadapter->pcard_sd->reg = &mlan_reg_sd8977_sd8997;
		pmadapter->pcard_info = &mlan_card_info_sd8977;
		break;
#endif
#ifdef SD8997
	case CARD_TYPE_SD8997:
		pmadapter->pcard_sd->reg = &mlan_reg_sd8977_sd8997;
		pmadapter->pcard_info = &mlan_card_info_sd8997;
		break;
#endif
#ifdef SD8987
	case CARD_TYPE_SD8987:
		pmadapter->pcard_sd->reg = &mlan_reg_sd8977_sd8997;
		pmadapter->pcard_info = &mlan_card_info_sd8987;
		break;
#endif
#ifdef SD9098
	case CARD_TYPE_SD9098:
		pmadapter->pcard_sd->reg = &mlan_reg_sd8977_sd8997;
		pmadapter->pcard_info = &mlan_card_info_sd9098;
		break;
#endif
#ifdef SD9097
	case CARD_TYPE_SD9097:
		pmadapter->pcard_sd->reg = &mlan_reg_sd8977_sd8997;
		pmadapter->pcard_info = &mlan_card_info_sd9097;
		break;
#endif
#ifdef SD9177
	case CARD_TYPE_SD9177:
		pmadapter->pcard_sd->reg = &mlan_reg_sd8977_sd8997;
		pmadapter->pcard_info = &mlan_card_info_sd9177;
		break;
#endif
	default:
		PRINTM(MERROR, "can't get right card type \n");
		ret = MLAN_STATUS_FAILURE;
		break;
	}

	LEAVE();
	return ret;
}

/**
 *  @brief This function dump the mp registers when issue happened
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @return             N/A
 */
void
wlan_dump_mp_registers(pmlan_adapter pmadapter)
{
	t_u32 mp_wr_bitmap;
	t_bool new_mode = pmadapter->pcard_sd->supports_sdio_new_mode;
	t_u32 mp_rd_bitmap;
	t_u16 rx_len = 0;
	const mlan_sdio_card_reg *reg = pmadapter->pcard_sd->reg;
	t_u8 cmd_rd_len_0 = reg->cmd_rd_len_0;
	t_u8 cmd_rd_len_1 = reg->cmd_rd_len_1;
	t_u8 host_int_status_reg = reg->host_int_status_reg;
	t_u32 sdio_ireg = 0;

	mp_wr_bitmap = (t_u32)pmadapter->pcard_sd->mp_regs[reg->wr_bitmap_l];
	mp_wr_bitmap |= ((t_u32)pmadapter->pcard_sd->mp_regs[reg->wr_bitmap_u])
		<< 8;
	if (new_mode) {
		mp_wr_bitmap |=
			((t_u32)pmadapter->pcard_sd->mp_regs[reg->wr_bitmap_1l])
			<< 16;
		mp_wr_bitmap |=
			((t_u32)pmadapter->pcard_sd->mp_regs[reg->wr_bitmap_1u])
			<< 24;
	}
	PRINTM(MMSG, "wlan: mp_data_port_mask = 0x%x\n",
	       pmadapter->pcard_sd->mp_data_port_mask);
	PRINTM(MMSG, "wlan: HW wr_bitmap=0x%08x Host: wr_bitmap=0x%08x\n",
	       mp_wr_bitmap, pmadapter->pcard_sd->mp_wr_bitmap);
	mp_rd_bitmap = (t_u32)pmadapter->pcard_sd->mp_regs[reg->rd_bitmap_l];
	mp_rd_bitmap |= ((t_u32)pmadapter->pcard_sd->mp_regs[reg->rd_bitmap_u])
		<< 8;
	if (new_mode) {
		mp_rd_bitmap |=
			((t_u32)pmadapter->pcard_sd->mp_regs[reg->rd_bitmap_1l])
			<< 16;
		mp_rd_bitmap |=
			((t_u32)pmadapter->pcard_sd->mp_regs[reg->rd_bitmap_1u])
			<< 24;
	}
	PRINTM(MMSG, "wlan: HW rd_bitmap=0x%08x Host: rd_bitmap=0x%08x\n",
	       mp_rd_bitmap, pmadapter->pcard_sd->mp_rd_bitmap);

	if (new_mode) {
		rx_len = ((t_u16)pmadapter->pcard_sd->mp_regs[cmd_rd_len_1])
			<< 8;
		rx_len |= (t_u16)pmadapter->pcard_sd->mp_regs[cmd_rd_len_0];
		PRINTM(MMSG, "wlan: cmd rx buffer rx_len = %d\n", rx_len);
	}
	PRINTM(MMSG, "wlan: HW sdio_ireg = 0x%x\n",
	       pmadapter->pcard_sd->mp_regs[host_int_status_reg]);
	sdio_ireg = pmadapter->pcard_sd->mp_regs[host_int_status_reg];

	if (new_mode && rx_len)
		sdio_ireg |= UP_LD_CMD_PORT_HOST_INT_STATUS;

	if (!(pmadapter->pcard_sd->mp_wr_bitmap &
	      pmadapter->pcard_sd->mp_data_port_mask)) {
		if (mp_wr_bitmap & pmadapter->pcard_sd->mp_data_port_mask)
			sdio_ireg |= DN_LD_HOST_INT_STATUS;
	}

	if ((!pmadapter->pcard_sd->mp_rd_bitmap) && mp_rd_bitmap)
		sdio_ireg |= UP_LD_HOST_INT_STATUS;

	pmadapter->pcard_sd->mp_regs[host_int_status_reg] = sdio_ireg;
	PRINTM(MMSG, "wlan: recovered sdio_ireg=0x%x\n", sdio_ireg);
	return;
}

/**
 *  @brief This function gets interrupt status.
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @return             MLAN_STATUS_SUCCESS
 */
static mlan_status
wlan_sdio_interrupt(t_u16 msg_id, pmlan_adapter pmadapter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_callbacks pcb = &pmadapter->callbacks;
	mlan_buffer mbuf;
	t_u32 sdio_ireg = 0;
	t_u8 offset = 0;
	t_u8 i = 0;
	int max_mp_regs = pmadapter->pcard_sd->reg->max_mp_regs;
	t_u8 host_int_status_reg =
		pmadapter->pcard_sd->reg->host_int_status_reg;

	ENTER();

	while (max_mp_regs) {
		memset(pmadapter, &mbuf, 0, sizeof(mlan_buffer));
		mbuf.pbuf = pmadapter->pcard_sd->mp_regs + offset;
		mbuf.data_len = MIN(max_mp_regs, MLAN_SDIO_BLOCK_SIZE);
		do {
			ret = pcb->moal_read_data_sync(pmadapter->pmoal_handle,
						       &mbuf,
						       (REG_PORT +
							offset) |
						       MLAN_SDIO_BYTE_MODE_MASK,
						       0);
			if (ret != MLAN_STATUS_SUCCESS) {
				PRINTM(MERROR,
				       "wlan: cmd53 read regs failed: %d port=%x retry=%d\n",
				       ret, REG_PORT + offset, i);
				i++;
				pcb->moal_write_reg(pmadapter->pmoal_handle,
						    HOST_TO_CARD_EVENT_REG,
						    HOST_TERM_CMD53);
				if (i > MAX_WRITE_IOMEM_RETRY) {
					PRINTM(MERROR,
					       "wlan: Fail to read mp_regs\n");
					pmadapter->dbg.num_int_read_failure++;
					goto done;
				}
			}
		} while (ret == MLAN_STATUS_FAILURE);
		offset += mbuf.data_len;
		max_mp_regs -= mbuf.data_len;
	}
	if (i > 0)
		wlan_dump_mp_registers(pmadapter);

	DBG_HEXDUMP(MIF_D, "SDIO MP Registers", pmadapter->pcard_sd->mp_regs,
		    max_mp_regs);
	sdio_ireg = pmadapter->pcard_sd->mp_regs[host_int_status_reg];
	pmadapter->dbg.last_int_status = pmadapter->ireg | sdio_ireg;
	if (sdio_ireg) {
		/*
		 * DN_LD_HOST_INT_STATUS and/or UP_LD_HOST_INT_STATUS
		 * DN_LD_CMD_PORT_HOST_INT_STATUS and/or
		 * UP_LD_CMD_PORT_HOST_INT_STATUS
		 * Clear the interrupt status register
		 */
		PRINTM(MINTR, "wlan_interrupt: sdio_ireg = 0x%x\n", sdio_ireg);
		pmadapter->pcard_sd->num_of_irq++;
		pcb->moal_spin_lock(pmadapter->pmoal_handle,
				    pmadapter->pint_lock);
		pmadapter->ireg |= sdio_ireg;
		pcb->moal_spin_unlock(pmadapter->pmoal_handle,
				      pmadapter->pint_lock);
		if (!pmadapter->pps_uapsd_mode &&
		    pmadapter->ps_state == PS_STATE_SLEEP) {
			pmadapter->pm_wakeup_fw_try = MFALSE;
			pmadapter->ps_state = PS_STATE_AWAKE;
			pmadapter->pm_wakeup_card_req = MFALSE;
		}
	} else {
		PRINTM(MMSG, "wlan_interrupt: sdio_ireg = 0x%x\n", sdio_ireg);
	}
done:
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function try to read the packet when fail to alloc rx buffer
 *
 *  @param pmadapter A pointer to mlan_adapter structure
 *  @param port      Current port on which packet needs to be rxed
 *  @param rx_len    Length of received packet
 *  @return          MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_sdio_card_to_host_recovery(mlan_adapter *pmadapter,
				t_u8 port, t_u16 rx_len)
{
	mlan_buffer mbuf;
	t_u32 pkt_type = 0;
	mlan_status ret = MLAN_STATUS_FAILURE;
	ENTER();
	if (!pmadapter->pcard_sd->supports_sdio_new_mode)
		goto done;
	if (MP_RX_AGGR_IN_PROGRESS(pmadapter)) {
		PRINTM(MDATA, "Recovery:do Rx Aggr\n");
		/* do aggr RX now */
		wlan_receive_mp_aggr_buf(pmadapter);
	}
	memset(pmadapter, &mbuf, 0, sizeof(mlan_buffer));
	mbuf.pbuf = pmadapter->pcard_sd->rx_buf;
	mbuf.data_len = rx_len;

	PRINTM(MDATA, "Recovery: Try read port=%d rx_len=%d\n", port, rx_len);
	if (MLAN_STATUS_SUCCESS !=
	    wlan_sdio_card_to_host(pmadapter, &pkt_type,
				   (t_u32 *)&pmadapter->upld_len, &mbuf, rx_len,
				   pmadapter->pcard_sd->ioport + port)) {
		PRINTM(MERROR, "Recovery: Fail to do cmd53\n");
	}
	if (pkt_type != MLAN_TYPE_DATA && pkt_type != MLAN_TYPE_SPA_DATA) {
		PRINTM(MERROR,
		       "Recovery: Receive a wrong pkt: type=%d, len=%d\n",
		       pkt_type, pmadapter->upld_len);
		goto done;
	}
	if (pkt_type == MLAN_TYPE_DATA) {
		// TODO fill the hole in Rx reorder table
		PRINTM(MDATA, "Recovery: Drop Data packet\n");
		pmadapter->dbg.num_pkt_dropped++;
	} else if (pkt_type == MLAN_TYPE_SPA_DATA) {
		PRINTM(MDATA, "Recovery: SPA Data packet len=%d\n",
		       pmadapter->upld_len);
		wlan_decode_spa_buffer(pmadapter, pmadapter->pcard_sd->rx_buf,
				       pmadapter->upld_len);
		pmadapter->data_received = MTRUE;
	}
	PRINTM(MMSG, "wlan: Success handle rx port=%d, rx_len=%d \n", port,
	       rx_len);
	ret = MLAN_STATUS_SUCCESS;
done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function checks the interrupt status and handle it accordingly.
 *
 *  @param pmadapter A pointer to mlan_adapter structure
 *  @return          MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_process_sdio_int_status(mlan_adapter *pmadapter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_callbacks pcb = &pmadapter->callbacks;
	t_u8 sdio_ireg;
	mlan_buffer *pmbuf = MNULL;

	t_u8 port = 0;
	t_u32 len_reg_l, len_reg_u;
	t_u32 rx_blocks;
	t_u8 bit_count = 0;
	t_u32 ps_state = pmadapter->ps_state;
	t_u16 rx_len;
	t_u32 upld_typ = 0;
	t_u32 cr = 0;
	const mlan_sdio_card_reg *reg = pmadapter->pcard_sd->reg;
	t_u8 rd_len_p0_l = reg->rd_len_p0_l;
	t_u8 rd_len_p0_u = reg->rd_len_p0_u;
	t_u8 cmd_rd_len_0 = reg->cmd_rd_len_0;
	t_u8 cmd_rd_len_1 = reg->cmd_rd_len_1;
	t_bool new_mode = pmadapter->pcard_sd->supports_sdio_new_mode;

	ENTER();

	pcb->moal_spin_lock(pmadapter->pmoal_handle, pmadapter->pint_lock);
	sdio_ireg = (t_u8)pmadapter->ireg;
	pmadapter->ireg = 0;
	pcb->moal_spin_unlock(pmadapter->pmoal_handle, pmadapter->pint_lock);

	if (!sdio_ireg)
		goto done;

	if (new_mode) {
		/* check the command port */
		if (sdio_ireg & DN_LD_CMD_PORT_HOST_INT_STATUS) {
			if (pmadapter->cmd_sent)
				pmadapter->cmd_sent = MFALSE;

			PRINTM(MINFO, "cmd_sent=%d\n", pmadapter->cmd_sent);
		}

		if (sdio_ireg & UP_LD_CMD_PORT_HOST_INT_STATUS) {
			/* read the len of control packet */
			rx_len = ((t_u16)pmadapter->pcard_sd->
				  mp_regs[cmd_rd_len_1])
				<< 8;
			rx_len |=
				(t_u16)pmadapter->pcard_sd->
				mp_regs[cmd_rd_len_0];
			PRINTM(MINFO, "RX: cmd port rx_len=%u\n", rx_len);
			rx_blocks = (rx_len + MLAN_SDIO_BLOCK_SIZE - 1) /
				MLAN_SDIO_BLOCK_SIZE;
			if (rx_len <= SDIO_INTF_HEADER_LEN ||
			    (rx_blocks * MLAN_SDIO_BLOCK_SIZE) >
			    ALLOC_BUF_SIZE) {
				PRINTM(MERROR, "invalid rx_len=%d\n", rx_len);
				ret = MLAN_STATUS_FAILURE;
				goto done;
			}
			rx_len = (t_u16)(rx_blocks * MLAN_SDIO_BLOCK_SIZE);
			pmbuf = wlan_alloc_mlan_buffer(pmadapter, rx_len, 0,
						       MOAL_MALLOC_BUFFER);
			if (pmbuf == MNULL) {
				PRINTM(MERROR,
				       "Failed to allocate 'mlan_buffer'\n");
				ret = MLAN_STATUS_FAILURE;
				goto done;
			}
			PRINTM(MINFO, "cmd rx buffer rx_len = %d\n", rx_len);

			/* Transfer data from card */
			if (MLAN_STATUS_SUCCESS !=
			    wlan_sdio_card_to_host(pmadapter, &upld_typ,
						   (t_u32 *)&pmadapter->
						   upld_len, pmbuf, rx_len,
						   pmadapter->pcard_sd->
						   ioport | CMD_PORT_SLCT)) {
				pmadapter->dbg.
					num_cmdevt_card_to_host_failure++;
				PRINTM(MERROR,
				       "Card-to-host cmd failed: int status=0x%x\n",
				       sdio_ireg);
				wlan_free_mlan_buffer(pmadapter, pmbuf);
				ret = MLAN_STATUS_FAILURE;
				goto term_cmd53;
			}

			if ((upld_typ != MLAN_TYPE_CMD) &&
			    (upld_typ != MLAN_TYPE_EVENT))
				PRINTM(MERROR,
				       "receive a wrong packet from CMD PORT. type =0x%x\n",
				       upld_typ);

			wlan_decode_rx_packet(pmadapter, pmbuf, upld_typ,
					      MFALSE);

			/* We might receive data/sleep_cfm at the same time */
			/* reset data_receive flag to avoid ps_state change */
			if ((ps_state == PS_STATE_SLEEP_CFM) &&
			    (pmadapter->ps_state == PS_STATE_SLEEP))
				pmadapter->data_received = MFALSE;
		}
	}

	if (sdio_ireg & DN_LD_HOST_INT_STATUS) {
		if (pmadapter->pcard_sd->mp_wr_bitmap &
		    pmadapter->pcard_sd->mp_data_port_mask)
			pmadapter->pcard_sd->mp_invalid_update++;
		pmadapter->pcard_sd->mp_wr_bitmap =
			(t_u32)pmadapter->pcard_sd->mp_regs[reg->wr_bitmap_l];
		pmadapter->pcard_sd->mp_wr_bitmap |=
			((t_u32)pmadapter->pcard_sd->mp_regs[reg->wr_bitmap_u])
			<< 8;
		if (new_mode) {
			pmadapter->pcard_sd->mp_wr_bitmap |=
				((t_u32)pmadapter->pcard_sd->
				 mp_regs[reg->wr_bitmap_1l])
				<< 16;
			pmadapter->pcard_sd->mp_wr_bitmap |=
				((t_u32)pmadapter->pcard_sd->
				 mp_regs[reg->wr_bitmap_1u])
				<< 24;
		}
		bit_count = bitcount(pmadapter->pcard_sd->mp_wr_bitmap &
				     pmadapter->pcard_sd->mp_data_port_mask);
		if (bit_count) {
			pmadapter->pcard_sd->mp_update[bit_count - 1]++;
			if (pmadapter->pcard_sd->mp_update[bit_count - 1] ==
			    0xffffffff)
				memset(pmadapter,
				       pmadapter->pcard_sd->mp_update, 0,
				       sizeof(pmadapter->pcard_sd->mp_update));
		}

		pmadapter->pcard_sd->last_recv_wr_bitmap =
			pmadapter->pcard_sd->mp_wr_bitmap;
		PRINTM(MINTR, "DNLD: wr_bitmap=0x%08x\n",
		       pmadapter->pcard_sd->mp_wr_bitmap);
		if (pmadapter->data_sent &&
		    (pmadapter->pcard_sd->mp_wr_bitmap &
		     (1 << pmadapter->pcard_sd->curr_wr_port))) {
			pmadapter->callbacks.
				moal_tp_accounting_rx_param(pmadapter->
							    pmoal_handle, 3, 0);
			PRINTM(MINFO, " <--- Tx DONE Interrupt --->\n");
			pmadapter->data_sent = MFALSE;
		}
	}
	if ((!new_mode) && (pmadapter->cmd_sent == MTRUE)) {
		/* Check if firmware has attach buffer at command port and update just that in wr_bit_map. */
		pmadapter->pcard_sd->mp_wr_bitmap |=
			(t_u32)pmadapter->pcard_sd->
			mp_regs[reg->wr_bitmap_l] & CTRL_PORT_MASK;
		if (pmadapter->pcard_sd->mp_wr_bitmap & CTRL_PORT_MASK)
			pmadapter->cmd_sent = MFALSE;
	}

	if (sdio_ireg & UP_LD_HOST_INT_STATUS) {
		pmadapter->pcard_sd->mp_rd_bitmap =
			(t_u32)pmadapter->pcard_sd->mp_regs[reg->rd_bitmap_l];
		pmadapter->pcard_sd->mp_rd_bitmap |=
			((t_u32)pmadapter->pcard_sd->mp_regs[reg->rd_bitmap_u])
			<< 8;
		if (new_mode) {
			pmadapter->pcard_sd->mp_rd_bitmap |=
				((t_u32)pmadapter->pcard_sd->
				 mp_regs[reg->rd_bitmap_1l])
				<< 16;
			pmadapter->pcard_sd->mp_rd_bitmap |=
				((t_u32)pmadapter->pcard_sd->
				 mp_regs[reg->rd_bitmap_1u])
				<< 24;
		}
		pmadapter->pcard_sd->last_recv_rd_bitmap =
			pmadapter->pcard_sd->mp_rd_bitmap;

		PRINTM(MINTR, "UPLD: rd_bitmap=0x%08x\n",
		       pmadapter->pcard_sd->mp_rd_bitmap);
		pmadapter->callbacks.moal_tp_accounting_rx_param(pmadapter->
								 pmoal_handle,
								 0, 0);

		while (MTRUE) {
			ret = wlan_get_rd_port(pmadapter, &port);
			if (ret != MLAN_STATUS_SUCCESS) {
				PRINTM(MINFO,
				       "no more rd_port to be handled\n");
				break;
			}
			len_reg_l = rd_len_p0_l + (port << 1);
			len_reg_u = rd_len_p0_u + (port << 1);
			rx_len = ((t_u16)pmadapter->pcard_sd->
				  mp_regs[len_reg_u])
				<< 8;
			rx_len |=
				(t_u16)pmadapter->pcard_sd->mp_regs[len_reg_l];
			PRINTM(MINFO, "RX: port=%d rx_len=%u\n", port, rx_len);
			rx_blocks = (rx_len + MLAN_SDIO_BLOCK_SIZE - 1) /
				MLAN_SDIO_BLOCK_SIZE;
			if (rx_len <= SDIO_INTF_HEADER_LEN ||
			    (rx_blocks * MLAN_SDIO_BLOCK_SIZE) >
			    pmadapter->pcard_sd->mpa_rx.buf_size) {
				PRINTM(MERROR, "invalid rx_len=%d\n", rx_len);
				ret = MLAN_STATUS_FAILURE;
				goto done;
			}
			rx_len = (t_u16)(rx_blocks * MLAN_SDIO_BLOCK_SIZE);

			if (!new_mode && (port == CTRL_PORT))
				pmbuf = wlan_alloc_mlan_buffer(pmadapter,
							       rx_len, 0,
							       MOAL_MALLOC_BUFFER);
			else
				pmbuf = wlan_alloc_mlan_buffer(pmadapter,
							       rx_len,
							       MLAN_RX_HEADER_LEN,
							       MOAL_ALLOC_MLAN_BUFFER);
			if (pmbuf == MNULL) {
				PRINTM(MERROR,
				       "Failed to allocate 'mlan_buffer'\n");
				pmadapter->dbg.num_alloc_buffer_failure++;
				if (MLAN_STATUS_SUCCESS ==
				    wlan_sdio_card_to_host_recovery(pmadapter,
								    port,
								    rx_len))
					continue;
				ret = MLAN_STATUS_FAILURE;
				goto done;
			}
			PRINTM(MINFO, "rx_len = %d\n", rx_len);
			if (MLAN_STATUS_SUCCESS !=
			    wlan_sdio_card_to_host_mp_aggr(pmadapter, pmbuf,
							   port, rx_len)) {
				if ((!new_mode) && (port == CTRL_PORT))
					pmadapter->dbg.
						num_cmdevt_card_to_host_failure++;
				else
					pmadapter->dbg.
						num_rx_card_to_host_failure++;

				PRINTM(MERROR,
				       "Card to host failed: int status=0x%x\n",
				       sdio_ireg);
				ret = MLAN_STATUS_FAILURE;
				goto term_cmd53;
			}
		}
		/* We might receive data/sleep_cfm at the same time */
		/* reset data_receive flag to avoid ps_state change */
		if ((ps_state == PS_STATE_SLEEP_CFM) &&
		    (pmadapter->ps_state == PS_STATE_SLEEP))
			pmadapter->data_received = MFALSE;
	}

	ret = MLAN_STATUS_SUCCESS;
	goto done;

term_cmd53:
	/* terminate cmd53 */
	if (MLAN_STATUS_SUCCESS != pcb->moal_read_reg(pmadapter->pmoal_handle,
						      HOST_TO_CARD_EVENT_REG,
						      &cr))
		PRINTM(MERROR, "read CFG reg failed\n");
	PRINTM(MINFO, "Config Reg val = %d\n", cr);
	if (MLAN_STATUS_SUCCESS != pcb->moal_write_reg(pmadapter->pmoal_handle,
						       HOST_TO_CARD_EVENT_REG,
						       (cr | HOST_TERM_CMD53)))
		PRINTM(MERROR, "write CFG reg failed\n");
	PRINTM(MINFO, "write success\n");
	if (MLAN_STATUS_SUCCESS != pcb->moal_read_reg(pmadapter->pmoal_handle,
						      HOST_TO_CARD_EVENT_REG,
						      &cr))
		PRINTM(MERROR, "read CFG reg failed\n");
	PRINTM(MINFO, "Config reg val =%x\n", cr);

done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function sends data to the card.
 *
 *  @param pmadapter A pointer to mlan_adapter structure
 *  @param type      data or command
 *  @param pmbuf     A pointer to mlan_buffer (pmbuf->data_len should include
 * SDIO header)
 *  @param tx_param  A pointer to mlan_tx_param
 *  @return          MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_sdio_host_to_card(mlan_adapter *pmadapter, t_u8 type,
		       mlan_buffer *pmbuf, mlan_tx_param *tx_param)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u32 buf_block_len;
	t_u32 blksz;
	t_u8 port = 0;
	t_u32 cmd53_port = 0;
	t_u8 *payload = pmbuf->pbuf + pmbuf->data_offset;
	t_bool new_mode = pmadapter->pcard_sd->supports_sdio_new_mode;

	ENTER();

	/* Allocate buffer and copy payload */
	blksz = MLAN_SDIO_BLOCK_SIZE;
	buf_block_len = (pmbuf->data_len + blksz - 1) / blksz;
	*(t_u16 *)&payload[0] = wlan_cpu_to_le16((t_u16)pmbuf->data_len);
	*(t_u16 *)&payload[2] = wlan_cpu_to_le16(type);

	/*
	 * This is SDIO specific header
	 *  t_u16 length,
	 *  t_u16 type (MLAN_TYPE_DATA = 0,
	 *    MLAN_TYPE_CMD = 1, MLAN_TYPE_EVENT = 3)
	 */
	if (type == MLAN_TYPE_DATA) {
		ret = wlan_get_wr_port_data(pmadapter, &port);
		if (ret != MLAN_STATUS_SUCCESS) {
			PRINTM(MERROR,
			       "no wr_port available: wr_bitmap=0x%08x curr_wr_port=%d\n",
			       pmadapter->pcard_sd->mp_wr_bitmap,
			       pmadapter->pcard_sd->curr_wr_port);
			goto exit;
		}
		/* Transfer data to card */
		pmbuf->data_len = buf_block_len * blksz;

		if (tx_param)
			ret = wlan_host_to_card_mp_aggr(pmadapter, pmbuf, port,
							tx_param->next_pkt_len);
		else
			ret = wlan_host_to_card_mp_aggr(pmadapter, pmbuf, port,
							0);
	} else {
		/*Type must be MLAN_TYPE_CMD */
		pmadapter->cmd_sent = MTRUE;
		if (!new_mode)
			pmadapter->pcard_sd->mp_wr_bitmap &=
				(t_u32)(~(1 << CTRL_PORT));
		if (pmbuf->data_len <= SDIO_INTF_HEADER_LEN ||
		    pmbuf->data_len > WLAN_UPLD_SIZE)
			PRINTM(MWARN,
			       "wlan_sdio_host_to_card(): Error: payload=%p, nb=%d\n",
			       payload, pmbuf->data_len);
		/* Transfer data to card */
		pmbuf->data_len = buf_block_len * blksz;
		if (new_mode)
			cmd53_port =
				(pmadapter->pcard_sd->ioport) | CMD_PORT_SLCT;
		else
			cmd53_port = pmadapter->pcard_sd->ioport + CTRL_PORT;
		ret = wlan_write_data_sync(pmadapter, pmbuf, cmd53_port);
	}

	if (ret == MLAN_STATUS_FAILURE) {
		PRINTM(MERROR, "Error: host_to_card failed: 0x%X\n", ret);
		if (type == MLAN_TYPE_CMD)
			pmadapter->cmd_sent = MFALSE;
		if (type == MLAN_TYPE_DATA)
			pmadapter->data_sent = MFALSE;
	} else {
		if (type == MLAN_TYPE_DATA) {
			if (!(pmadapter->pcard_sd->mp_wr_bitmap &
			      (1 << pmadapter->pcard_sd->curr_wr_port)))
				pmadapter->data_sent = MTRUE;
			else
				pmadapter->data_sent = MFALSE;
		}
		DBG_HEXDUMP(MIF_D, "SDIO Blk Wr",
			    pmbuf->pbuf + pmbuf->data_offset,
			    MIN(pmbuf->data_len, MAX_DATA_DUMP_LEN));
	}
exit:
	LEAVE();
	return ret;
}

#if (defined(SD9098) || defined(SD9097) || defined(SD9177))
/**
 *  @brief This function sends vdll data to the card.
 *
 *  @param pmadapter   A pointer to mlan_adapter structure
 *  @param pmbuf     A pointer to mlan_buffer (pmbuf->data_len should include
 * SDIO header)
 *  @return          MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_sdio_send_vdll(mlan_adapter *pmadapter, mlan_buffer *pmbuf)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u32 buf_block_len;
	t_u32 blksz;
	t_u8 *payload = pmbuf->pbuf + pmbuf->data_offset;
	t_u32 cmd53_port = 0;
	ENTER();
	blksz = MLAN_SDIO_BLOCK_SIZE;
	buf_block_len = (pmbuf->data_len + blksz - 1) / blksz;

	*(t_u16 *)&payload[0] = wlan_cpu_to_le16((t_u16)pmbuf->data_len);
	*(t_u16 *)&payload[2] = wlan_cpu_to_le16(MLAN_TYPE_VDLL);

	pmbuf->data_len = buf_block_len * blksz;

	if (pmbuf->data_len > MRVDRV_SIZE_OF_CMD_BUFFER) {
		PRINTM(MERROR, "VDLL block is too big: %d\n", pmbuf->data_len);
		return MLAN_STATUS_FAILURE;
	}
	cmd53_port = (pmadapter->pcard_sd->ioport) | CMD_PORT_SLCT;
	pmadapter->cmd_sent = MTRUE;
	ret = wlan_write_data_sync(pmadapter, pmbuf, cmd53_port);
	if (ret == MLAN_STATUS_FAILURE)
		PRINTM(MERROR, "Send Vdll: host_to_card failed: 0x%X\n", ret);
	else
		DBG_HEXDUMP(MIF_D, "SDIO Blk Wr",
			    pmbuf->pbuf + pmbuf->data_offset,
			    MIN(pmbuf->data_len, MAX_DATA_DUMP_LEN));
	LEAVE();
	return ret;
}
#endif

/**
 *  @brief This function sends data to the card.
 *
 *  @param pmpriv    A pointer to mlan_private structure
 *  @param type      data or command
 *  @param pmbuf     A pointer to mlan_buffer (pmbuf->data_len should include
 * SDIO header)
 *  @param tx_param  A pointer to mlan_tx_param
 *  @return          MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_sdio_host_to_card_ext(pmlan_private pmpriv, t_u8 type,
			   mlan_buffer *pmbuf, mlan_tx_param *tx_param)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_adapter *pmadapter = pmpriv->adapter;

#if (defined(SD9098) || defined(SD9097) || defined(SD9177))
	if (type == MLAN_TYPE_VDLL)
		return wlan_sdio_send_vdll(pmadapter, pmbuf);
#endif
	ret = wlan_sdio_host_to_card(pmadapter, type, pmbuf, tx_param);

	if (type == MLAN_TYPE_DATA && ret == MLAN_STATUS_FAILURE)
		pmadapter->data_sent = MFALSE;

	LEAVE();
	return ret;
}

/**
 *  @brief Deaggregate single port aggregation packet
 *
 *  @param pmadapter  A pointer to mlan_adapter structure
 *  @param buf	A pointer to aggregated data packet
 *  @param len
 *
 *  @return		N/A
 */
void
wlan_decode_spa_buffer(mlan_adapter *pmadapter, t_u8 *buf, t_u32 len)
{
	int total_pkt_len;
	t_u8 block_num = 0;
	t_u16 block_size = 0;
	t_u8 *data;
	t_u32 pkt_len;
	mlan_buffer *mbuf_deaggr = MNULL;

	ENTER();

	data = (t_u8 *)buf;
	total_pkt_len = len;
	if (total_pkt_len < pmadapter->pcard_sd->sdio_rx_block_size) {
		PRINTM(MERROR, "Invalid sp aggr packet size=%d\n",
		       total_pkt_len);
		goto done;
	}
	while (total_pkt_len >= (OFFSET_OF_SDIO_HEADER + SDIO_INTF_HEADER_LEN)) {
		block_num = *(data + OFFSET_OF_BLOCK_NUMBER);
		block_size =
			pmadapter->pcard_sd->sdio_rx_block_size * block_num;
		if (block_size > total_pkt_len) {
			PRINTM(MERROR,
			       "Error in pkt, block_num=%d, pkt_len=%d\n",
			       block_num, total_pkt_len);
			break;
		}
		pkt_len =
			wlan_le16_to_cpu(*(t_u16 *)
					 (data + OFFSET_OF_SDIO_HEADER));
		if ((pkt_len + OFFSET_OF_SDIO_HEADER) > block_size) {
			PRINTM(MERROR,
			       "Error in pkt, pkt_len=%d, block_size=%d\n",
			       pkt_len, block_size);
			break;
		}
		mbuf_deaggr =
			wlan_alloc_mlan_buffer(pmadapter,
					       pkt_len - SDIO_INTF_HEADER_LEN,
					       MLAN_RX_HEADER_LEN,
					       MOAL_ALLOC_MLAN_BUFFER);
		if (mbuf_deaggr == MNULL) {
			PRINTM(MERROR, "Error allocating daggr mlan_buffer\n");
			break;
		}
		memcpy_ext(pmadapter,
			   mbuf_deaggr->pbuf + mbuf_deaggr->data_offset,
			   data + OFFSET_OF_SDIO_HEADER + SDIO_INTF_HEADER_LEN,
			   pkt_len - SDIO_INTF_HEADER_LEN,
			   pkt_len - SDIO_INTF_HEADER_LEN);
		mbuf_deaggr->data_len = pkt_len - SDIO_INTF_HEADER_LEN;
		wlan_handle_rx_packet(pmadapter, mbuf_deaggr);
		data += block_size;
		total_pkt_len -= block_size;
		if (total_pkt_len < pmadapter->pcard_sd->sdio_rx_block_size)
			break;
	}
done:
	LEAVE();
	return;
}

/**
 *  @brief This function deaggr rx pkt
 *
 *  @param pmadapter A pointer to mlan_adapter structure
 *  @param pmbuf     A pointer to the SDIO mpa data
 *  @return          N/A
 */
t_void
wlan_sdio_deaggr_rx_pkt(pmlan_adapter pmadapter, mlan_buffer *pmbuf)
{
	if (pmbuf->buf_type == MLAN_BUF_TYPE_SPA_DATA) {
		wlan_decode_spa_buffer(pmadapter,
				       pmbuf->pbuf + pmbuf->data_offset,
				       pmbuf->data_len);
		wlan_free_mlan_buffer(pmadapter, pmbuf);
	} else
		wlan_handle_rx_packet(pmadapter, pmbuf);
}

/**
 *  @brief This function allocates buffer for the SDIO aggregation buffer
 *          related members of adapter structure
 *
 *  @param pmadapter       A pointer to mlan_adapter structure
 *  @param mpa_tx_buf_size Tx buffer size to allocate
 *  @param mpa_rx_buf_size Rx buffer size to allocate
 *
 *  @return        MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_alloc_sdio_mpa_buffers(mlan_adapter *pmadapter,
			    t_u32 mpa_tx_buf_size, t_u32 mpa_rx_buf_size)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_callbacks pcb = &pmadapter->callbacks;
	t_u8 mp_aggr_pkt_limit = pmadapter->pcard_sd->mp_aggr_pkt_limit;

	ENTER();

	if ((pmadapter->pcard_sd->max_segs < mp_aggr_pkt_limit) ||
	    (pmadapter->pcard_sd->max_seg_size <
	     pmadapter->pcard_sd->max_sp_tx_size)) {
		ret = pcb->moal_malloc(pmadapter->pmoal_handle,
				       mpa_tx_buf_size + DMA_ALIGNMENT,
				       MLAN_MEM_DEF | MLAN_MEM_DMA,
				       (t_u8 **)&pmadapter->pcard_sd->mpa_tx.
				       head_ptr);
		if (ret != MLAN_STATUS_SUCCESS ||
		    !pmadapter->pcard_sd->mpa_tx.head_ptr) {
			PRINTM(MERROR,
			       "Could not allocate buffer for SDIO MP TX aggr\n");
			ret = MLAN_STATUS_FAILURE;
			goto error;
		}
		pmadapter->pcard_sd->mpa_tx.buf =
			(t_u8 *)ALIGN_ADDR(pmadapter->pcard_sd->mpa_tx.head_ptr,
					   DMA_ALIGNMENT);
	} else {
		PRINTM(MMSG, "wlan: Enable TX SG mode\n");
		pmadapter->pcard_sd->mpa_tx.head_ptr = MNULL;
		pmadapter->pcard_sd->mpa_tx.buf = MNULL;
	}
	pmadapter->pcard_sd->mpa_tx.buf_size = mpa_tx_buf_size;

	if ((pmadapter->pcard_sd->max_segs < mp_aggr_pkt_limit) ||
	    (pmadapter->pcard_sd->max_seg_size <
	     pmadapter->pcard_sd->max_sp_rx_size)) {
		ret = pcb->moal_malloc(pmadapter->pmoal_handle,
				       mpa_rx_buf_size + DMA_ALIGNMENT,
				       MLAN_MEM_DEF | MLAN_MEM_DMA,
				       (t_u8 **)&pmadapter->pcard_sd->mpa_rx.
				       head_ptr);
		if (ret != MLAN_STATUS_SUCCESS ||
		    !pmadapter->pcard_sd->mpa_rx.head_ptr) {
			PRINTM(MERROR,
			       "Could not allocate buffer for SDIO MP RX aggr\n");
			ret = MLAN_STATUS_FAILURE;
			goto error;
		}
		pmadapter->pcard_sd->mpa_rx.buf =
			(t_u8 *)ALIGN_ADDR(pmadapter->pcard_sd->mpa_rx.head_ptr,
					   DMA_ALIGNMENT);
	} else {
		PRINTM(MMSG, "wlan: Enable RX SG mode\n");
		pmadapter->pcard_sd->mpa_rx.head_ptr = MNULL;
		pmadapter->pcard_sd->mpa_rx.buf = MNULL;
	}
	pmadapter->pcard_sd->mpa_rx.buf_size = mpa_rx_buf_size;
error:
	if (ret != MLAN_STATUS_SUCCESS)
		wlan_free_sdio_mpa_buffers(pmadapter);

	LEAVE();
	return ret;
}

/**
 *  @brief This function frees buffers for the SDIO aggregation
 *
 *  @param pmadapter       A pointer to mlan_adapter structure
 *
 *  @return        MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_free_sdio_mpa_buffers(mlan_adapter *pmadapter)
{
	pmlan_callbacks pcb = &pmadapter->callbacks;

	ENTER();

	if (pmadapter->pcard_sd->mpa_tx.buf) {
		pcb->moal_mfree(pmadapter->pmoal_handle,
				(t_u8 *)pmadapter->pcard_sd->mpa_tx.head_ptr);
		pmadapter->pcard_sd->mpa_tx.head_ptr = MNULL;
		pmadapter->pcard_sd->mpa_tx.buf = MNULL;
		pmadapter->pcard_sd->mpa_tx.buf_size = 0;
	}

	if (pmadapter->pcard_sd->mpa_rx.buf) {
		pcb->moal_mfree(pmadapter->pmoal_handle,
				(t_u8 *)pmadapter->pcard_sd->mpa_rx.head_ptr);
		pmadapter->pcard_sd->mpa_rx.head_ptr = MNULL;
		pmadapter->pcard_sd->mpa_rx.buf = MNULL;
		pmadapter->pcard_sd->mpa_rx.buf_size = 0;
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function re-allocate rx mpa buffer
 *
 *  @param pmadapter       A pointer to mlan_adapter structure
 *
 *  @return        MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_re_alloc_sdio_rx_mpa_buffer(mlan_adapter *pmadapter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_callbacks pcb = &pmadapter->callbacks;
	t_u8 mp_aggr_pkt_limit = pmadapter->pcard_sd->mp_aggr_pkt_limit;
	t_u32 mpa_rx_buf_size = pmadapter->pcard_sd->mp_tx_aggr_buf_size;

	if (pmadapter->pcard_sd->mpa_rx.buf) {
		pcb->moal_mfree(pmadapter->pmoal_handle,
				(t_u8 *)pmadapter->pcard_sd->mpa_rx.head_ptr);
		pmadapter->pcard_sd->mpa_rx.head_ptr = MNULL;
		pmadapter->pcard_sd->mpa_rx.buf = MNULL;
		pmadapter->pcard_sd->mpa_rx.buf_size = 0;
	}
	if (pmadapter->pcard_sd->sdio_rx_aggr_enable) {
		mpa_rx_buf_size = MAX(mpa_rx_buf_size, SDIO_CMD53_MAX_SIZE);
		/** reallocate rx buffer for recover when single port rx
		 * aggregation enabled */
		if (pmadapter->pcard_sd->rx_buffer) {
			pcb->moal_mfree(pmadapter->pmoal_handle,
					(t_u8 *)pmadapter->pcard_sd->rx_buffer);
			pmadapter->pcard_sd->rx_buffer = MNULL;
			pmadapter->pcard_sd->rx_buf = MNULL;
		}
		ret = pmadapter->callbacks.moal_malloc(pmadapter->pmoal_handle,
						       SDIO_CMD53_MAX_SIZE +
						       DMA_ALIGNMENT,
						       MLAN_MEM_DEF |
						       MLAN_MEM_DMA,
						       (t_u8 **)&pmadapter->
						       pcard_sd->rx_buffer);

		if (ret != MLAN_STATUS_SUCCESS ||
		    !pmadapter->pcard_sd->rx_buffer) {
			PRINTM(MERROR, "Failed to allocate receive buffer\n");
			ret = MLAN_STATUS_FAILURE;
			goto error;
		}
		pmadapter->pcard_sd->rx_buf =
			(t_u8 *)ALIGN_ADDR(pmadapter->pcard_sd->rx_buffer,
					   DMA_ALIGNMENT);
	}
	if ((pmadapter->pcard_sd->max_segs < mp_aggr_pkt_limit) ||
	    (pmadapter->pcard_sd->max_seg_size <
	     pmadapter->pcard_sd->max_sp_rx_size)) {
		ret = pcb->moal_malloc(pmadapter->pmoal_handle,
				       mpa_rx_buf_size + DMA_ALIGNMENT,
				       MLAN_MEM_DEF | MLAN_MEM_DMA,
				       (t_u8 **)&pmadapter->pcard_sd->mpa_rx.
				       head_ptr);
		if (ret != MLAN_STATUS_SUCCESS ||
		    !pmadapter->pcard_sd->mpa_rx.head_ptr) {
			PRINTM(MERROR,
			       "Could not allocate buffer for SDIO MP RX aggr\n");
			ret = MLAN_STATUS_FAILURE;
			goto error;
		}
		pmadapter->pcard_sd->mpa_rx.buf =
			(t_u8 *)ALIGN_ADDR(pmadapter->pcard_sd->mpa_rx.head_ptr,
					   DMA_ALIGNMENT);
	} else {
		PRINTM(MMSG, "wlan: Enable RX SG mode\n");
		pmadapter->pcard_sd->mpa_rx.head_ptr = MNULL;
		pmadapter->pcard_sd->mpa_rx.buf = MNULL;
	}
	pmadapter->pcard_sd->mpa_rx.buf_size = mpa_rx_buf_size;
	PRINTM(MMSG, "mpa_rx_buf_size=%d\n", mpa_rx_buf_size);
error:
	return ret;
}

/**
 *  @brief This function wakes up the card.
 *
 *  @param pmadapter		A pointer to mlan_adapter structure
 *  @param timeout          set timeout flag
 *
 *  @return			MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_pm_sdio_wakeup_card(pmlan_adapter pmadapter, t_u8 timeout)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u32 age_ts_usec;
	pmlan_callbacks pcb = &pmadapter->callbacks;

	ENTER();
	PRINTM(MEVENT, "Wakeup device...\n");
	pmadapter->callbacks.moal_get_system_time(pmadapter->pmoal_handle,
						  &pmadapter->pm_wakeup_in_secs,
						  &age_ts_usec);

	if (timeout) {
		pmadapter->callbacks.moal_start_timer(pmadapter->pmoal_handle,
						      pmadapter->
						      pwakeup_fw_timer, MFALSE,
						      MRVDRV_TIMER_3S);
		pmadapter->wakeup_fw_timer_is_set = MTRUE;
	}

	ret = pcb->moal_write_reg(pmadapter->pmoal_handle,
				  HOST_TO_CARD_EVENT_REG, HOST_POWER_UP);

	LEAVE();
	return ret;
}

/**
 *  @brief This function resets the PM setting of the card.
 *
 *  @param pmadapter		A pointer to mlan_adapter structure
 *
 *  @return			MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_pm_sdio_reset_card(pmlan_adapter pmadapter)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_callbacks pcb = &pmadapter->callbacks;

	ENTER();

	ret = pcb->moal_write_reg(pmadapter->pmoal_handle,
				  HOST_TO_CARD_EVENT_REG, 0);

	LEAVE();
	return ret;
}

/**
 *  @brief  This function issues commands to initialize firmware
 *
 *  @param priv     A pointer to mlan_private structure
 *
 *  @return         MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_set_sdio_gpio_int(pmlan_private priv)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_adapter pmadapter = MNULL;
	HostCmd_DS_SDIO_GPIO_INT_CONFIG sdio_int_cfg;

	if (!priv) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	pmadapter = priv->adapter;

	ENTER();

	if (pmadapter->pcard_sd->int_mode == INT_MODE_GPIO) {
		if (pmadapter->pcard_sd->gpio_pin != GPIO_INT_NEW_MODE) {
			PRINTM(MINFO,
			       "SDIO_GPIO_INT_CONFIG: interrupt mode is GPIO\n");
			sdio_int_cfg.action = HostCmd_ACT_GEN_SET;
			sdio_int_cfg.gpio_pin = pmadapter->pcard_sd->gpio_pin;
			sdio_int_cfg.gpio_int_edge = INT_FALLING_EDGE;
			sdio_int_cfg.gpio_pulse_width = DELAY_1_US;
			ret = wlan_prepare_cmd(priv,
					       HostCmd_CMD_SDIO_GPIO_INT_CONFIG,
					       HostCmd_ACT_GEN_SET, 0, MNULL,
					       &sdio_int_cfg);

			if (ret) {
				PRINTM(MERROR,
				       "SDIO_GPIO_INT_CONFIG: send command fail\n");
				ret = MLAN_STATUS_FAILURE;
			}
		}
	} else {
		PRINTM(MINFO, "SDIO_GPIO_INT_CONFIG: interrupt mode is SDIO\n");
	}

	LEAVE();
	return ret;
}

/**
 *  @brief This function prepares command of SDIO GPIO interrupt
 *
 *  @param pmpriv   A pointer to mlan_private structure
 *  @param cmd      A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   The action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_cmd_sdio_gpio_int(pmlan_private pmpriv,
		       HostCmd_DS_COMMAND *cmd,
		       t_u16 cmd_action, t_void *pdata_buf)
{
	HostCmd_DS_SDIO_GPIO_INT_CONFIG *psdio_gpio_int =
		&cmd->params.sdio_gpio_int;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_SDIO_GPIO_INT_CONFIG);
	cmd->size = wlan_cpu_to_le16((sizeof(HostCmd_DS_SDIO_GPIO_INT_CONFIG)) +
				     S_DS_GEN);

	memset(pmpriv->adapter, psdio_gpio_int, 0,
	       sizeof(HostCmd_DS_SDIO_GPIO_INT_CONFIG));
	if (cmd_action == HostCmd_ACT_GEN_SET) {
		memcpy_ext(pmpriv->adapter, psdio_gpio_int, pdata_buf,
			   sizeof(HostCmd_DS_SDIO_GPIO_INT_CONFIG),
			   sizeof(HostCmd_DS_SDIO_GPIO_INT_CONFIG));
		psdio_gpio_int->action =
			wlan_cpu_to_le16(psdio_gpio_int->action);
		psdio_gpio_int->gpio_pin =
			wlan_cpu_to_le16(psdio_gpio_int->gpio_pin);
		psdio_gpio_int->gpio_int_edge =
			wlan_cpu_to_le16(psdio_gpio_int->gpio_int_edge);
		psdio_gpio_int->gpio_pulse_width =
			wlan_cpu_to_le16(psdio_gpio_int->gpio_pulse_width);
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

mlan_status
wlan_reset_fw(pmlan_adapter pmadapter)
{
	t_u32 tries = 0;
	t_u32 value = 1;
	t_u32 reset_reg = pmadapter->pcard_sd->reg->fw_reset_reg;
	t_u8 reset_val = pmadapter->pcard_sd->reg->fw_reset_val;
	pmlan_callbacks pcb = &pmadapter->callbacks;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();
	wlan_pm_sdio_wakeup_card(pmadapter, MFALSE);

	/** wait SOC fully wake up */
	for (tries = 0; tries < MAX_POLL_TRIES; ++tries) {
		if (MLAN_STATUS_SUCCESS ==
		    pcb->moal_write_reg(pmadapter->pmoal_handle, reset_reg,
					0xba)) {
			pcb->moal_read_reg(pmadapter->pmoal_handle, reset_reg,
					   &value);
			if (value == 0xba) {
				PRINTM(MMSG, "FW wake up\n");
				break;
			}
		}
		pcb->moal_udelay(pmadapter->pmoal_handle, 1000);
	}
	/* Write register to notify FW */
	if (MLAN_STATUS_FAILURE == pcb->moal_write_reg(pmadapter->pmoal_handle,
						       reset_reg, reset_val)) {
		PRINTM(MERROR, "Failed to write register.\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
#if defined(SD8997) || defined(SD8977) || defined(SD8987) || defined(SD9098) || defined(SD9097) || defined(SD8978) || defined(SD9177)
	if (MFALSE
#ifdef SD8997
	    || IS_SD8997(pmadapter->card_type)
#endif
#ifdef SD8977
	    || IS_SD8977(pmadapter->card_type)
#endif
#ifdef SD8978
	    || IS_SD8978(pmadapter->card_type)
#endif
#ifdef SD8987
	    || IS_SD8987(pmadapter->card_type)
#endif
#ifdef SD9098
	    || IS_SD9098(pmadapter->card_type)
#endif
#ifdef SD9097
	    || IS_SD9097(pmadapter->card_type)
#endif
#ifdef SD9177
	    || IS_SD9177(pmadapter->card_type)
#endif
		) {
		pcb->moal_read_reg(pmadapter->pmoal_handle,
				   HOST_TO_CARD_EVENT_REG, &value);
		pcb->moal_write_reg(pmadapter->pmoal_handle,
				    HOST_TO_CARD_EVENT_REG,
				    value | HOST_POWER_UP);
	}
#endif
	/* Poll register around 100 ms */
	for (tries = 0; tries < MAX_POLL_TRIES; ++tries) {
		pcb->moal_read_reg(pmadapter->pmoal_handle, reset_reg, &value);
		if (value == 0)
			/* FW is ready */
			break;
		pcb->moal_udelay(pmadapter->pmoal_handle, 1000);
	}

	if (value) {
		PRINTM(MERROR, "Failed to poll FW reset register %X=0x%x\n",
		       reset_reg, value);
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	PRINTM(MMSG, "FW Reset success\n");
	ret = wlan_sdio_probe(pmadapter);
done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function handle event/data/cmd complete
 *
 *  @param pmadapter A pointer to mlan_adapter structure
 *  @param pmbuf     A pointer to the mlan_buffer
 *  @return          N/A
 */
static mlan_status
wlan_sdio_data_evt_complete(pmlan_adapter pmadapter,
			    mlan_buffer *pmbuf, mlan_status status)
{
	ENTER();

	wlan_free_mlan_buffer(pmadapter, pmbuf);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handle receive packet
 *
 *  @param pmadapter A pointer to mlan_adapter structure
 *  @param pmbuf     A pointer to the mlan_buffer
 *  @return
 */
static mlan_status
wlan_sdio_handle_rx_packet(mlan_adapter *pmadapter, pmlan_buffer pmbuf)
{
	ENTER();

	wlan_sdio_deaggr_rx_pkt(pmadapter, pmbuf);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

mlan_adapter_operations mlan_sdio_ops = {
	.dnld_fw = wlan_sdio_dnld_fw,
	.interrupt = wlan_sdio_interrupt,
	.process_int_status = wlan_process_sdio_int_status,
	.host_to_card = wlan_sdio_host_to_card_ext,
	.wakeup_card = wlan_pm_sdio_wakeup_card,
	.reset_card = wlan_pm_sdio_reset_card,
	.event_complete = wlan_sdio_data_evt_complete,
	.data_complete = wlan_sdio_data_evt_complete,
	.cmdrsp_complete = wlan_sdio_data_evt_complete,
	.handle_rx_packet = wlan_sdio_handle_rx_packet,
	.disable_host_int = wlan_disable_sdio_host_int,
	.enable_host_int = wlan_enable_sdio_host_int,

	.intf_header_len = SDIO_INTF_HEADER_LEN,
};
