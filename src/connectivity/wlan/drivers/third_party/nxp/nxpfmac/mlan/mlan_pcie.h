/** @file mlan_pcie.h
 *
 *  @brief This file contains definitions for PCIE interface.
 *  driver.
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
    02/01/2012: initial version
********************************************************/

#ifndef _MLAN_PCIE_H_
#define _MLAN_PCIE_H_
/** Tx DATA */
#define ADMA_TX_DATA 0
/** Rx DATA */
#define ADMA_RX_DATA 1
/** EVENT */
#define ADMA_EVENT 2
/** CMD */
#define ADMA_CMD 3
/** CMD RESP */
#define ADMA_CMDRESP 4
/** ADMA direction */
#define ADMA_HOST_TO_DEVICE 0
/** ADMA direction Rx */
#define ADMA_DEVICE_TO_HOST 1
/** Direct Program mode */
#define DMA_MODE_DIRECT 0
/** Single descriptor mode */
#define DMA_MODE_SINGLE_DESC 1
/** dual discriptor mode */
#define DMA_MODE_DUAL_DESC 2
/** descriptor mode:  ring mode */
#define DESC_MODE_RING 0
/** descriptor mode: chain mode */
#define DESC_MODE_CHAIN 1
/** DMA size start bit */
#define DMA_SIZE_BIT 16
/** DMA size mask */
#define DMA_SIZE_MASK 0xffff0000
/** Descriptor mode */
#define DESC_MODE_MASK 0x0004
/** DMA MODE MASK */
#define DMA_MODE_MASK 0x0003
/** Dest Num Descriptor start bits */
#define DST_NUM_DESC_BIT 12
/** Destination Num of Descriptor mask */
#define DST_NUM_DESC_MASK 0xf000
/** Src Num Descriptor start bits */
#define SRC_NUM_DESC_BIT 8
/** Destination Num of Descriptor mask */
#define SRC_NUM_DESC_MASK 0x0f00
/** Virtual Q priority mask */
#define Q_PRIO_WEIGHT_MASK 0x00f0
/** DMA cfg register offset*/
#define ADMA_DMA_CFG 0x0000
/** source base low */
#define ADMA_SRC_LOW 0x0004
/** source base high */
#define ADMA_SRC_HIGH 0x0008
/** destination base low */
#define ADMA_DST_LOW 0x000C
/** destination base high */
#define ADMA_DST_HIGH 0x0010
/** source rd/wr pointer */
#define ADMA_SRC_RW_PTR 0x0014
/** destination rd/wr pointer */
#define ADMA_DST_RW_PTR 0x0018
/** interrupt direction mapping reg, for each virtual Q, used for
 * dual-descriptor only, only valid for Q0 */
#define ADMA_INT_MAPPING 0x001C
/** destination interrupt to device */
#define DEST_INT_TO_DEVICE MBIT(0)
/** destination interrupt to host */
#define DEST_INT_TO_HOST MBIT(1)
/** interrupt pending status for each virtual Q, only valid for Q0 */
#define ADMA_INT_PENDING 0x0020
/** Default ADMA INT mask, We only enable dma done */
#define DEF_ADMA_INT_MASK MBIT(0)
/** source interrupt status mask reg */
#define ADMA_SRC_INT_STATUS_MASK 0x0024
/** source interrupt mask reg */
#define ADMA_SRC_INT_MASK 0x0028
/** source interrupt status reg */
#define ADMA_SRC_INT_STATUS 0x002C
/** destination interrupt status mask reg */
#define ADMA_DST_INT_STATUS_MASK 0x0030
/** destination interrupt mask reg */
#define ADMA_DST_INT_MASK 0x0034
/** destination interrupt status reg */
#define ADMA_DST_INT_STATUS 0x0038
/** DMA cfg2 register */
#define ADMA_DMA_CFG2 0x003C
/** ADMA_MSI_LEGACY_DST_DMA_DONE_INT_BYPASS_EN */
#define ADMA_MSI_LEGACY_DST_DMA_DONE_INT_BYPASS_EN MBIT(22)
/** ADMA_MSI_LEGACY_SRC_DMA_DONE_INT_BYPASS_EN */
#define ADMA_MSI_LEGACY_SRC_DMA_DONE_INT_BYPASS_EN MBIT(21)
/* If this bit is set, MSIX trigger event will be from DST, other wise MSIX
 * trigger event will be from SRC */
#define ADMA_MSIX_INT_SRC_DST_SEL MBIT(20)
/** Enable MSI/Legacy for this Queue */
#define ADMA_MSI_LEGACY_ENABLE MBIT(19)
/** Enable MSIX for this queue */
#define ADMA_MSIX_ENABLE MBIT(18)
/** ADMA_DST_DMA_DONE_INT_BYPASS_EN */
#define ADMA_DST_DMA_DONE_INT_BYPASS_EN MBIT(17)
/** SRC_DMA_DONE_INT_BYPASS_EN */
#define ADMA_SRC_DMA_DONE_INT_BYPASS_EN MBIT(16)
/* Destination Read Pointer Memory Copy Enable */
#define ADMA_DST_RPTR_MEM_COPY_EN MBIT(11)
/* Source Read Pointer Memory Copy Enable */
#define ADMA_SRC_RPTR_MEM_COPY_EN MBIT(10)
/** Destination address is host */
#define ADMA_DST_ADDR_IS_HOST MBIT(2)
/** Source address is host */
#define ADMA_SRC_ADDR_IS_HOST MBIT(1)

/** DMA cfg3 register */
#define ADMA_DMA_CFG3 0x0040
/** ADMA Queue pointer clear */
#define ADMA_Q_PTR_CLR  MBIT(0)
/** source rd ptr address low */
#define ADMA_SRC_RD_PTR_LOW 0x0044
/** source rd ptr address high */
#define ADMA_SRC_RD_PTR_HIGH 0x0048
/** destination rd ptr address low */
#define ADMA_DST_RD_PTR_LOW 0x004C
/** destination rd ptr address high */
#define ADMA_DST_RD_PTR_HIGH 0x0050
/** source active interrupt mask */
#define ADMA_SRC_ACTV_INT_MASK 0x0054
/** destination active interrupt mask */
#define ADMA_DST_ACTV_INT_MASK 0x0058
/** Read pointer start from bit 16 */
#define ADMA_RPTR_START 16
/** write pointer start from bit 0 */
#define ADMA_WPTR_START 0
/** Tx/Rx Read/Write pointer's mask */
#define TXRX_RW_PTR_MASK (ADMA_MAX_TXRX_BD - 1)
/** Tx/Rx Read/Write pointer's rollover indicate bit */
#define TXRX_RW_PTR_ROLLOVER_IND ADMA_MAX_TXRX_BD
/** Start of packet flag */
#define ADMA_BD_FLAG_SOP MBIT(0)
/** End of packet flag */
#define ADMA_BD_FLAG_EOP MBIT(1)
/** interrupt enable flag */
#define ADMA_BD_FLAG_INT_EN MBIT(2)
/** Source address is host side flag */
#define ADMA_BD_FLAG_SRC_HOST MBIT(3)
/** Destination address is host side flag */
#define ADMA_BD_FLAG_DST_HOST MBIT(4)
/** ADMA MIN PKT SIZE */
#define ADMA_MIN_PKT_SIZE 128
/** ADMA dual descriptor mode requir 8 bytes alignment in buf size */
#define ADMA_ALIGN_SIZE 8
/** ADMA RW_PTR wrap mask */
#define ADMA_RW_PTR_WRAP_MASK 0x00001FFF
/** ADMA MSIX DOORBEEL DATA */
#define ADMA_MSIX_DOORBELL_DATA 0x0064
/** MSIX VECTOR MASK: BIT 0-10 */
#define ADMA_MSIX_VECTOR_MASK 0x3f
/** PF mask: BIT 24-28 */
#define ADMA_MSIX_PF_MASK 0x1f000000
/** PF start bit */
#define ADMA_MSIX_PF_BIT 24

#if defined(PCIE9098) || defined(PCIE9097)
/** PCIE9098 dev_id/vendor id reg */
#define PCIE9098_DEV_ID_REG 0x0000
/** PCIE revision ID register */
#define PCIE9098_REV_ID_REG 0x0008
/** PCIE IP revision register */
#define PCIE9098_IP_REV_REG 0x1000
/** PCIE CPU interrupt events */
#define PCIE9098_CPU_INT_EVENT 0x1C20
/** PCIE CPU interrupt status */
#define PCIE9098_CPU_INT_STATUS 0x1C24
/** PCIe CPU Interrupt Status Mask */
#define PCIE9098_CPU_INT2ARM_ISM 0x1C28
/** PCIE host interrupt status */
#define PCIE9098_HOST_INT_STATUS 0x1C44
/** PCIE host interrupt mask */
#define PCIE9098_HOST_INT_MASK 0x1C48
/** PCIE host interrupt clear select*/
#define PCIE9098_HOST_INT_CLR_SEL 0x1C4C
/** PCIE host interrupt status mask */
#define PCIE9098_HOST_INT_STATUS_MASK 0x1C50
/** PCIE host interrupt status */
#define PCIE9097_B0_HOST_INT_STATUS 0x3C44
/** PCIE host interrupt mask */
#define PCIE9097_B0_HOST_INT_MASK 0x3C48
/** PCIE host interrupt clear select*/
#define PCIE9097_B0_HOST_INT_CLR_SEL 0x3C4C
/** PCIE host interrupt status mask */
#define PCIE9097_B0_HOST_INT_STATUS_MASK 0x3C50
/** PCIE host interrupt select*/
#define PCIE9098_HOST_INT_SEL 0x1C58
/** PCIE data exchange register 0 */
#define PCIE9098_SCRATCH_0_REG 0x1C60
/** PCIE data exchange register 1 */
#define PCIE9098_SCRATCH_1_REG 0x1C64
/** PCIE data exchange register 2 */
#define PCIE9098_SCRATCH_2_REG 0x1C68
/** PCIE data exchange register 3 */
#define PCIE9098_SCRATCH_3_REG 0x1C6C
/** PCIE data exchange register 4 */
#define PCIE9098_SCRATCH_4_REG 0x1C70
/** PCIE data exchange register 5 */
#define PCIE9098_SCRATCH_5_REG 0x1C74
/** PCIE data exchange register 6 */
#define PCIE9098_SCRATCH_6_REG 0x1C78
/** PCIE data exchange register 7 */
#define PCIE9098_SCRATCH_7_REG 0x1C7C
/** PCIE data exchange register 8 */
#define PCIE9098_SCRATCH_8_REG 0x1C80
/** PCIE data exchange register 9 */
#define PCIE9098_SCRATCH_9_REG 0x1C84
/** PCIE data exchange register 10 */
#define PCIE9098_SCRATCH_10_REG 0x1C88
/** PCIE data exchange register 11 */
#define PCIE9098_SCRATCH_11_REG 0x1C8C
/** PCIE data exchange register 12 */
#define PCIE9098_SCRATCH_12_REG 0x1C90
/** PCIE data exchange register 13 */
#define PCIE9098_SCRATCH_13_REG 0x1C94
/** PCIE data exchange register 14 */
#define PCIE9098_SCRATCH_14_REG 0x1C98
/** PCIE data exchange register 15 */
#define PCIE9098_SCRATCH_15_REG 0x1C9C
/** ADMA CHAN0_Q0 start address, Tx Data */
#define ADMA_CHAN0_Q0 0x10000
/** ADMA CHAN1_Q0 start address, Rx Data */
#define ADMA_CHAN1_Q0 0x10800
/** ADMA CHAN1_Q1 start address, Rx Event */
#define ADMA_CHAN1_Q1 0x10880
/** ADMA CHAN2_Q0 start address, Tx Command */
#define ADMA_CHAN2_Q0 0x11000
/** ADMA CHAN2_Q1 start address, Command Resp */
#define ADMA_CHAN2_Q1 0x11080
/** CH0-Q0' src rd/wr ptr */
#define ADMA_SRC_PTR_CH0_Q0 (ADMA_CHAN0_Q0 + ADMA_SRC_RW_PTR)
/** CH1-Q1' dest rd/wr ptr */
#define ADMA_DST_PTR_CH1_Q0 (ADMA_CHAN1_Q0 + ADMA_DST_RW_PTR)
/** CH1-Q1' dest rd/wr ptr */
#define ADMA_DST_PTR_CH1_Q1 (ADMA_CHAN1_Q1 + ADMA_DST_RW_PTR)
/* TX buffer description read pointer */
#define PCIE9098_TXBD_RDPTR ADMA_SRC_PTR_CH0_Q0
/* TX buffer description write pointer */
#define PCIE9098_TXBD_WRPTR ADMA_SRC_PTR_CH0_Q0
/* RX buffer description read pointer */
#define PCIE9098_RXBD_RDPTR ADMA_DST_PTR_CH1_Q0
/* RX buffer description write pointer */
#define PCIE9098_RXBD_WRPTR ADMA_DST_PTR_CH1_Q0
/* Event buffer description read pointer */
#define PCIE9098_EVTBD_RDPTR ADMA_DST_PTR_CH1_Q1
/* Event buffer description write pointer */
#define PCIE9098_EVTBD_WRPTR ADMA_DST_PTR_CH1_Q1
/* Driver ready signature write pointer */
#define PCIE9098_DRV_READY PCIE9098_SCRATCH_12_REG

/** interrupt bit define for ADMA CHAN0 Q0, For Tx DATA */
#define ADMA_INT_CHAN0_Q0 MBIT(0)
/** interrupt bit define for ADMA CHAN1 Q0, For Rx Data */
#define AMDA_INT_CHAN1_Q0 MBIT(16)
/** interrupt bit define for ADMA CHAN1 Q1, For Rx Event */
#define AMDA_INT_CHAN1_Q1 MBIT(17)
/** interrupt bit define for ADMA CHAN2 Q0, For Tx Command */
#define AMDA_INT_CHAN2_Q0 MBIT(24)
/** interrupt bit define for ADMA CHAN2 Q1, For Rx Command Resp */
#define AMDA_INT_CHAN2_Q1 MBIT(25)

/** interrupt vector number for ADMA CHAN0 Q0, For Tx DATA */
#define ADMA_VECTOR_CHAN0_Q0 0
/** interrupt vector number for ADMA CHAN1 Q0, For Rx Data */
#define AMDA_VECTOR_CHAN1_Q0 16
/** interrupt vector number for ADMA CHAN1 Q1, For Rx Event */
#define AMDA_VECTOR_CHAN1_Q1 17
/** interrupt vector number for ADMA CHAN2 Q0, For Tx Command */
#define AMDA_VECTOR_CHAN2_Q0 24
/** interrupt vector number for ADMA CHAN2 Q1, For Rx Command Resp */
#define AMDA_VECTOR_CHAN2_Q1 25

/** Data sent interrupt for host */
#define PCIE9098_HOST_INTR_DNLD_DONE ADMA_INT_CHAN0_Q0
/** Data receive interrupt for host */
#define PCIE9098_HOST_INTR_UPLD_RDY AMDA_INT_CHAN1_Q0
/** Command sent interrupt for host */
#define PCIE9098_HOST_INTR_CMD_DONE AMDA_INT_CHAN2_Q1
/** Event ready interrupt for host */
#define PCIE9098_HOST_INTR_EVENT_RDY AMDA_INT_CHAN1_Q1
/** CMD sent interrupt for host     */
#define PCIE9098_HOST_INTR_CMD_DNLD MBIT(7)

/** Interrupt mask for host */
#define PCIE9098_HOST_INTR_MASK                                                \
	(PCIE9098_HOST_INTR_DNLD_DONE | PCIE9098_HOST_INTR_UPLD_RDY |          \
	 PCIE9098_HOST_INTR_CMD_DONE | PCIE9098_HOST_INTR_CMD_DNLD |           \
	 PCIE9098_HOST_INTR_EVENT_RDY)

/** Interrupt select mask for host */
#define PCIE9098_HOST_INTR_SEL_MASK                                            \
	(PCIE9098_HOST_INTR_DNLD_DONE | PCIE9098_HOST_INTR_UPLD_RDY |          \
	 PCIE9098_HOST_INTR_CMD_DONE | PCIE9098_HOST_INTR_EVENT_RDY)
#endif

#if defined(PCIE8997) || defined(PCIE8897)
/* PCIE INTERNAL REGISTERS */
/** PCIE data exchange register 0 */
#define PCIE_SCRATCH_0_REG 0x0C10
/** PCIE data exchange register 1 */
#define PCIE_SCRATCH_1_REG 0x0C14
/** PCIE CPU interrupt events */
#define PCIE_CPU_INT_EVENT 0x0C18
/** PCIE CPU interrupt status */
#define PCIE_CPU_INT_STATUS 0x0C1C

/** PCIe CPU Interrupt Status Mask */
#define PCIE_CPU_INT2ARM_ISM 0x0C28
/** PCIE host interrupt status */
#define PCIE_HOST_INT_STATUS 0x0C30
/** PCIE host interrupt mask */
#define PCIE_HOST_INT_MASK 0x0C34
/** PCIE host interrupt status mask */
#define PCIE_HOST_INT_STATUS_MASK 0x0C3C
/** PCIE data exchange register 2 */
#define PCIE_SCRATCH_2_REG 0x0C40
/** PCIE data exchange register 3 */
#define PCIE_SCRATCH_3_REG 0x0C44

#define PCIE_IP_REV_REG 0x0C48

/** PCIE data exchange register 4 */
#define PCIE_SCRATCH_4_REG 0x0CD0
/** PCIE data exchange register 5 */
#define PCIE_SCRATCH_5_REG 0x0CD4
/** PCIE data exchange register 6 */
#define PCIE_SCRATCH_6_REG 0x0CD8
/** PCIE data exchange register 7 */
#define PCIE_SCRATCH_7_REG 0x0CDC
/** PCIE data exchange register 8 */
#define PCIE_SCRATCH_8_REG 0x0CE0
/** PCIE data exchange register 9 */
#define PCIE_SCRATCH_9_REG 0x0CE4
/** PCIE data exchange register 10 */
#define PCIE_SCRATCH_10_REG 0x0CE8
/** PCIE data exchange register 11 */
#define PCIE_SCRATCH_11_REG 0x0CEC
/** PCIE data exchange register 12 */
#define PCIE_SCRATCH_12_REG 0x0CF0
#endif

#ifdef PCIE8997
/* PCIE read data pointer for queue 0 and 1 */
#define PCIE8997_RD_DATA_PTR_Q0_Q1 0xC1A4	/* 0x8000C1A4 */
/* PCIE read data pointer for queue 2 and 3 */
#define PCIE8997_RD_DATA_PTR_Q2_Q3 0xC1A8	/* 0x8000C1A8 */
/* PCIE write data pointer for queue 0 and 1 */
#define PCIE8997_WR_DATA_PTR_Q0_Q1 0xC174	/* 0x8000C174 */
/* PCIE write data pointer for queue 2 and 3 */
#define PCIE8997_WR_DATA_PTR_Q2_Q3 0xC178	/* 0x8000C178 */
#endif
#ifdef PCIE8897
/* PCIE read data pointer for queue 0 and 1 */
#define PCIE8897_RD_DATA_PTR_Q0_Q1 0xC08C	/* 0x8000C08C */
/* PCIE read data pointer for queue 2 and 3 */
#define PCIE8897_RD_DATA_PTR_Q2_Q3 0xC090	/* 0x8000C090 */
/* PCIE write data pointer for queue 0 and 1 */
#define PCIE8897_WR_DATA_PTR_Q0_Q1 0xC05C	/* 0x8000C05C */
/* PCIE write data pointer for queue 2 and 3 */
#define PCIE8897_WR_DATA_PTR_Q2_Q3 0xC060	/* 0x8000C060 */
#endif

/** Download ready interrupt for CPU */
#define CPU_INTR_DNLD_RDY MBIT(0)
/** Command ready interrupt for CPU */
#define CPU_INTR_DOOR_BELL MBIT(1)
/** Confirmation that sleep confirm message has been processed.
 Device will enter sleep after receiving this interrupt */
#define CPU_INTR_SLEEP_CFM_DONE MBIT(2)
/** Reset interrupt for CPU */
#define CPU_INTR_RESET MBIT(3)
/** Set Event Done interupt to the FW*/
#define CPU_INTR_EVENT_DONE MBIT(5)

#if defined(PCIE8997) || defined(PCIE8897)
/** Data sent interrupt for host */
#define HOST_INTR_DNLD_DONE MBIT(0)
/** Data receive interrupt for host */
#define HOST_INTR_UPLD_RDY MBIT(1)
/** Command sent interrupt for host */
#define HOST_INTR_CMD_DONE MBIT(2)
/** Event ready interrupt for host */
#define HOST_INTR_EVENT_RDY MBIT(3)
/** Interrupt mask for host */
#define HOST_INTR_MASK                                                         \
	(HOST_INTR_DNLD_DONE | HOST_INTR_UPLD_RDY | HOST_INTR_CMD_DONE |       \
	 HOST_INTR_EVENT_RDY)

/** Lower 32bits command address holding register */
#define REG_CMD_ADDR_LO PCIE_SCRATCH_0_REG
/** Upper 32bits command address holding register */
#define REG_CMD_ADDR_HI PCIE_SCRATCH_1_REG
/** Command length holding register */
#define REG_CMD_SIZE PCIE_SCRATCH_2_REG

/** Lower 32bits command response address holding register */
#define REG_CMDRSP_ADDR_LO PCIE_SCRATCH_4_REG
/** Upper 32bits command response address holding register */
#define REG_CMDRSP_ADDR_HI PCIE_SCRATCH_5_REG

/** TxBD's Read/Write pointer start from bit 16 */
#define TXBD_RW_PTR_START 16
/** RxBD's Read/Write pointer start from bit 0 */
#define RXBD_RW_PTR_STRAT 0

#define MLAN_BD_FLAG_SOP MBIT(0)
#define MLAN_BD_FLAG_EOP MBIT(1)
#define MLAN_BD_FLAG_XS_SOP MBIT(2)
#define MLAN_BD_FLAG_XS_EOP MBIT(3)

/* Event buffer description write pointer */
#define REG_EVTBD_WRPTR PCIE_SCRATCH_10_REG
/* Event buffer description read pointer */
#define REG_EVTBD_RDPTR PCIE_SCRATCH_11_REG
/* Driver ready signature write pointer */
#define REG_DRV_READY PCIE_SCRATCH_12_REG

/** Event Read/Write pointer mask */
#define EVT_RW_PTR_MASK 0x0f
/** Event Read/Write pointer rollover bit */
#define EVT_RW_PTR_ROLLOVER_IND MBIT(7)
#endif

/* Define PCIE block size for firmware download */
#define MLAN_PCIE_BLOCK_SIZE_FW_DNLD 256

/** Extra added macros **/
#define MLAN_EVENT_HEADER_LEN 8

/** Max interrupt status register read limit */
#define MAX_READ_REG_RETRY 10000

extern mlan_adapter_operations mlan_pcie_ops;

/* Get pcie device from card type */
mlan_status wlan_get_pcie_device(pmlan_adapter pmadapter);

/** Set PCIE host buffer configurations */
mlan_status wlan_set_pcie_buf_config(mlan_private *pmpriv);

/** Init write pointer */
mlan_status wlan_pcie_init_fw(pmlan_adapter pmadapter);

#if defined(PCIE8997) || defined(PCIE8897)
/** Prepare command PCIE host buffer config */
mlan_status wlan_cmd_pcie_host_buf_cfg(pmlan_private pmpriv,
				       pHostCmd_DS_COMMAND cmd,
				       t_u16 cmd_action, t_pvoid pdata_buf);
#endif

/** Wakeup PCIE card */
mlan_status wlan_pcie_wakeup(pmlan_adapter pmadapter);

/** Set DRV_READY register */
mlan_status wlan_set_drv_ready_reg(mlan_adapter *pmadapter, t_u32 val);
/** PCIE init */
mlan_status wlan_pcie_init(mlan_adapter *pmadapter);

/** Read interrupt status */
mlan_status wlan_process_msix_int(mlan_adapter *pmadapter);
/** Transfer data to card */
mlan_status wlan_pcie_host_to_card(pmlan_private pmpriv, t_u8 type,
				   mlan_buffer *mbuf, mlan_tx_param *tx_param);
/** Ring buffer allocation function */
mlan_status wlan_alloc_pcie_ring_buf(pmlan_adapter pmadapter);
/** Ring buffer deallocation function */
mlan_status wlan_free_pcie_ring_buf(pmlan_adapter pmadapter);
/** Ring buffer cleanup function, e.g. on deauth */
mlan_status wlan_clean_pcie_ring_buf(pmlan_adapter pmadapter);
mlan_status wlan_alloc_ssu_pcie_buf(pmlan_adapter pmadapter);
mlan_status wlan_free_ssu_pcie_buf(pmlan_adapter pmadapter);

#endif /* _MLAN_PCIE_H_ */
