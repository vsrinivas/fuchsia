// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_CRG_UDC_CRG_UDC_REGS_H_
#define SRC_DEVICES_USB_DRIVERS_CRG_UDC_CRG_UDC_REGS_H_

#include <hwreg/bitfields.h>
#include <usb/usb.h>

// clang-format off
// transfer
#define TRB_TRANSFER_LEN_MASK           0x0001FFFF
#define TRB_TRANSFER_LEN_SHIFT          0
#define TRB_TD_SIZE_MASK                0x0000001F
#define TRB_TD_SIZE_SHIFT               17
#define TRB_INTR_TARGET_MASK            0x000003FF
#define TRB_INTR_TARGET_SHIFT           22

#define TRB_CYCLE_BIT_MASK              0x00000001
#define TRB_CYCLE_BIT_SHIFT             0
#define TRB_LINK_TOGGLE_CYCLE_MASK      0x00000001
#define TRB_LINK_TOGGLE_CYCLE_SHIFT     1
#define TRB_INTR_ON_SHORT_PKT_MASK      0x00000001
#define TRB_INTR_ON_SHORT_PKT_SHIFT     2
#define TRB_NO_SNOOP_MASK               0x00000001
#define TRB_NO_SNOOP_SHIFT              3
#define TRB_CHAIN_BIT_MASK              0x00000001
#define TRB_CHAIN_BIT_SHIFT             4
#define TRB_INTR_ON_COMPLETION_MASK     0x00000001
#define TRB_INTR_ON_COMPLETION_SHIFT    5

#define TRB_APPEND_ZLP_MASK             0x00000001
#define TRB_APPEND_ZLP_SHIFT            7

#define TRB_BLOCK_EVENT_INT_MASK        0x00000001
#define TRB_BLOCK_EVENT_INT_SHIFT       9
#define TRB_TYPE_MASK                   0x0000003F
#define TRB_TYPE_SHIFT                  10
#define DATA_STAGE_TRB_DIR_MASK         0x00000001
#define DATA_STAGE_TRB_DIR_SHIFT        16
#define TRB_SETUP_TAG_MASK              0x00000003
#define TRB_SETUP_TAG_SHIFT             17
#define STATUS_STAGE_TRB_STALL_MASK     0x00000001
#define STATUS_STAGE_TRB_STALL_SHIFT    19
#define STATUS_STAGE_TRB_SET_ADDR_MASK  0x00000001
#define STATUS_STAGE_TRB_SET_ADDR_SHIFT 20

#define ISOC_TRB_FRAME_ID_MASK          0x000007FF
#define ISOC_TRB_FRAME_ID_SHIFT         20
#define ISOC_TRB_SIA_MASK               0x00000001
#define ISOC_TRB_SIA_SHIFT              31

// event
#define EVE_TRB_TRAN_LEN_MASK           0x0001FFFF
#define EVE_TRB_TRAN_LEN_SHIFT          0
#define EVE_TRB_COMPL_CODE_MASK         0x000000FF
#define EVE_TRB_COMPL_CODE_SHIFT        24
#define EVE_TRB_CYCLE_BIT_MASK          0x00000001
#define EVE_TRB_CYCLE_BIT_SHIFT         0
#define EVE_TRB_TYPE_MASK               0x0000003F
#define EVE_TRB_TYPE_SHIFT              10
#define EVE_TRB_ENDPOINT_ID_MASK        0x0000001F
#define EVE_TRB_ENDPOINT_ID_SHIFT       16
#define EVE_TRB_SETUP_TAG_MASK          0x00000003
#define EVE_TRB_SETUP_TAG_SHIFT         21

// endpoint context
#define EP_CX_LOGICAL_EP_NUM_MASK       0x0000000F
#define EP_CX_LOGICAL_EP_NUM_SHIFT      3
#define EP_CX_INTERVAL_MASK             0x000000FF
#define EP_CX_INTERVAL_SHIFT            16
#define EP_CX_EP_TYPE_MASK              0x00000007
#define EP_CX_EP_TYPE_SHIFT             3
#define EP_CX_MAX_BURST_SIZE_MASK       0x000000FF
#define EP_CX_MAX_BURST_SIZE_SHIFT      8
#define EP_CX_MAX_PACKET_SIZE_MASK      0x0000FFFF
#define EP_CX_MAX_PACKET_SIZE_SHIFT     16
#define EP_CX_DEQ_CYC_STATE_MASK        0x00000001
#define EP_CX_DEQ_CYC_STATE_SHIFT       0
#define EP_CX_TR_DQPT_LO_MASK           0xFFFFFFF0
#define EP_CX_TR_DQPT_LO_SHIFT          4
// clang-format on

// converts a USB endpoint address to 2 - 31 index
constexpr uint8_t CRG_UDC_ADDR_TO_INDEX(uint8_t addr) {
  // |  31 | 30  | ...... | 3  | 2  |   1    | 0 |
  // |IEP15|OEP15| ...... |IEP1|OEP1|reserved|EP0|
  //
  // OEP: Outbound EP (EP_IN from Host perspective)
  // IEP: Inbound EP (EP_OUT from Host perspective)
  return static_cast<uint8_t>((2 * (addr & 0xF)) + ((addr & USB_DIR_IN) ? 0 : 1));
}
constexpr bool CRG_UDC_EP_IS_OUT(uint8_t ep) { return ((ep / 2) == 0); }

// CRG UDC
constexpr uint32_t CTRL_REQ_QUEUE_DEPTH = 5;

constexpr uint32_t CRG_UDC_MAX_EPS = 32;
constexpr uint32_t CRG_UCCR_OFFSET = 0x2400;
constexpr uint32_t CRG_UICR_OFFSET = 0x2500;

constexpr uint32_t CRG_UDC_EVENT_RING_NUM = 1;
constexpr uint32_t CRG_UDC_EVENT_TRB_NUM = 256;
constexpr uint32_t CRG_CONTROL_EP_TD_RING_SIZE = 16;
constexpr uint32_t CRG_BULK_EP_TD_RING_SIZE = 32;
constexpr uint32_t CRG_ISOC_EP_TD_RING_SIZE = 32;
constexpr uint32_t CRG_INT_EP_TD_RING_SIZE = 8;

constexpr uint32_t CRG_U3DC_PORTSC_SPEED_FS = 0x1;
constexpr uint32_t CRG_U3DC_PORTSC_SPEED_LS = 0x2;
constexpr uint32_t CRG_U3DC_PORTSC_SPEED_HS = 0x3;
constexpr uint32_t CRG_U3DC_PORTSC_SPEED_SS = 0x4;
constexpr uint32_t CRG_U3DC_PORTSC_SPEED_SSP = 0x5;

constexpr uint32_t TRB_MAX_BUFFER_SIZE = 65536;
constexpr uint32_t CRGUDC_CONTROL_EP_TD_RING_SIZE = 16;
constexpr uint32_t CRGUDC_BULK_EP_TD_RING_SIZE = 16;
constexpr uint32_t CRGUDC_ISOC_EP_TD_RING_SIZE = 16;
constexpr uint32_t CRGUDC_INT_EP_TD_RING_SIZE = 16;

// TRB type
constexpr uint32_t TRB_TYPE_XFER_NORMAL = 1;
constexpr uint32_t TRB_TYPE_XFER_DATA_STAGE = 3;
constexpr uint32_t TRB_TYPE_XFER_STATUS_STAGE = 4;
constexpr uint32_t TRB_TYPE_XFER_DATA_ISOCH = 5;
constexpr uint32_t TRB_TYPE_LINK = 6;
constexpr uint32_t TRB_TYPE_EVT_TRANSFER = 32;
constexpr uint32_t TRB_TYPE_EVT_CMD_COMP = 33;
constexpr uint32_t TRB_TYPE_EVT_PORT_STATUS_CHANGE = 34;
constexpr uint32_t TRB_TYPE_EVT_MFINDEX_WRAP = 39;
constexpr uint32_t TRB_TYPE_EVT_SETUP_PKT = 40;

// EP type
constexpr uint32_t EP_TYPE_INVALID = 0;
constexpr uint32_t EP_TYPE_ISOCH_OUTBOUND = 1;
constexpr uint32_t EP_TYPE_BULK_OUTBOUND = 2;
constexpr uint32_t EP_TYPE_INTR_OUTBOUND = 3;
constexpr uint32_t EP_TYPE_INVALID2 = 4;
constexpr uint32_t EP_TYPE_ISOCH_INBOUND = 5;
constexpr uint32_t EP_TYPE_BULK_INBOUND = 6;
constexpr uint32_t EP_TYPE_INTR_INBOUND = 7;

constexpr uint32_t LOWER_32_BITS(uint64_t x) { return static_cast<uint32_t>(x); }
constexpr uint32_t UPPER_32_BITS(uint64_t x) { return static_cast<uint32_t>((x >> 16) >> 16); }

class CAPABILITY : public hwreg::RegisterBase<CAPABILITY, uint32_t> {
 public:
  DEF_FIELD(7, 0, version);
  DEF_FIELD(11, 8, ep_in_num);
  DEF_FIELD(15, 12, ep_out_num);
  DEF_FIELD(26, 16, max_int);
  DEF_BIT(27, gen1_support);
  DEF_BIT(1, gen2_support);
  DEF_BIT(2, isoch_support);
  static auto Get() { return hwreg::RegisterAddr<CAPABILITY>(CRG_UCCR_OFFSET + 0x0); }
};

class CONFIG0 : public hwreg::RegisterBase<CONFIG0, uint32_t> {
 public:
  DEF_FIELD(3, 0, max_speed);
  DEF_FIELD(7, 4, usb3_dis_count_limit);
  static auto Get() { return hwreg::RegisterAddr<CONFIG0>(CRG_UCCR_OFFSET + 0x10); }
};

class CONFIG1 : public hwreg::RegisterBase<CONFIG1, uint32_t> {
 public:
  DEF_BIT(0, csc_event_en);
  DEF_BIT(1, pec_event_en);
  DEF_BIT(3, ppc_event_en);
  DEF_BIT(4, prc_event_en);
  DEF_BIT(5, plc_event_en);
  DEF_BIT(6, cec_event_en);
  DEF_BIT(8, u3_entry_plc_en);
  DEF_BIT(9, l1_entry_plc_en);
  DEF_BIT(10, u3_resume_plc_en);
  DEF_BIT(11, l1_resume_plc_en);
  DEF_BIT(12, inactive_plc_en);
  DEF_BIT(13, usb3_resume_no_response_plc_en);
  DEF_BIT(14, usb2_resume_no_response_plc_en);
  DEF_BIT(16, setup_event_en);
  DEF_BIT(17, stopped_len_invalid_event_en);
  DEF_BIT(18, halted_len_invalid_event_en);
  DEF_BIT(19, disabled_len_invalid_event_en);
  DEF_BIT(20, disabled_event_en);
  static auto Get() { return hwreg::RegisterAddr<CONFIG1>(CRG_UCCR_OFFSET + 0x14); }
};

class COMMAND : public hwreg::RegisterBase<COMMAND, uint32_t> {
 public:
  DEF_BIT(0, start);
  DEF_BIT(1, soft_reset);
  DEF_BIT(2, interrupt_en);
  DEF_BIT(3, sys_err_en);
  DEF_BIT(10, ewe);  //  Enable MFINDEX Wrap Event whenever MFINDEX transitions from 3FFFh to 0
  DEF_BIT(11, keep_connect);
  static auto Get() { return hwreg::RegisterAddr<COMMAND>(CRG_UCCR_OFFSET + 0x20); }
};

class STATUS : public hwreg::RegisterBase<STATUS, uint32_t> {
 public:
  DEF_BIT(0, controller_halted);
  DEF_BIT(2, sys_err);
  DEF_BIT(3, eint);
  DEF_BIT(12, controller_idle);
  static auto Get() { return hwreg::RegisterAddr<STATUS>(CRG_UCCR_OFFSET + 0x24); }
};

// Device Context Base Address Pointer Low
class DCBAPLO : public hwreg::RegisterBase<DCBAPLO, uint32_t> {
 public:
  DEF_FIELD(31, 0, dcbap_lo);
  static auto Get() { return hwreg::RegisterAddr<DCBAPLO>(CRG_UCCR_OFFSET + 0x28); }
};

// Device Context Base Address Pointer High
class DCBAPHI : public hwreg::RegisterBase<DCBAPHI, uint32_t> {
 public:
  DEF_FIELD(31, 0, dcbap_hi);
  static auto Get() { return hwreg::RegisterAddr<DCBAPHI>(CRG_UCCR_OFFSET + 0x2c); }
};

//  Port Status and Control Register
class PORTSC : public hwreg::RegisterBase<PORTSC, uint32_t> {
 public:
  DEF_BIT(0, ccs);
  DEF_BIT(3, pp);
  DEF_BIT(4, pr);
  DEF_FIELD(8, 5, pls);
  DEF_FIELD(13, 10, speed);
  DEF_BIT(16, lws);
  DEF_BIT(17, csc);
  DEF_BIT(20, ppc);
  DEF_BIT(21, prc);
  DEF_BIT(22, plc);
  DEF_BIT(23, cec);
  DEF_BIT(25, wce);
  DEF_BIT(26, wde);
  DEF_BIT(31, wpr);
  static auto Get() { return hwreg::RegisterAddr<PORTSC>(CRG_UCCR_OFFSET + 0x30); }
};

class U3PORTPMSC : public hwreg::RegisterBase<U3PORTPMSC, uint32_t> {
 public:
  DEF_FIELD(7, 0, u1_timeout);
  DEF_FIELD(15, 8, u2_timeout);
  DEF_BIT(16, fla);
  DEF_BIT(20, u1_initiate_en);
  DEF_BIT(21, u2_initiate_en);
  DEF_BIT(22, u1_accept_en);
  DEF_BIT(23, u2_accept_en);
  DEF_FIELD(31, 24, u12u2_timeout);
  static auto Get() { return hwreg::RegisterAddr<U3PORTPMSC>(CRG_UCCR_OFFSET + 0x34); }
};

class U2PORTPMSC : public hwreg::RegisterBase<U2PORTPMSC, uint32_t> {
 public:
  DEF_FIELD(3, 0, reject_threshold);
  DEF_FIELD(7, 4, deepsleep_threshold);
  DEF_BIT(8, lpm_en);
  DEF_BIT(9, reject_threshold_en);
  DEF_BIT(10, deepsleep_en);
  DEF_BIT(11, sleep_en);
  DEF_BIT(12, plm_force_ack);
  DEF_BIT(13, l1_auto_exit_en);
  DEF_FIELD(19, 16, hird_besl);
  DEF_BIT(20, rwe);
  DEF_FIELD(31, 28, test_mode);
  static auto Get() { return hwreg::RegisterAddr<U2PORTPMSC>(CRG_UCCR_OFFSET + 0x38); }
};

class U3PORTLI : public hwreg::RegisterBase<U3PORTLI, uint32_t> {
 public:
  DEF_FIELD(15, 0, link_err_count);
  static auto Get() { return hwreg::RegisterAddr<U3PORTLI>(CRG_UCCR_OFFSET + 0x3c); }
};

class DOORBELL : public hwreg::RegisterBase<DOORBELL, uint32_t> {
 public:
  DEF_FIELD(4, 0, db_target);
  static auto Get() { return hwreg::RegisterAddr<DOORBELL>(CRG_UCCR_OFFSET + 0x40); }
};

class MFINDEX : public hwreg::RegisterBase<MFINDEX, uint32_t> {
 public:
  DEF_BIT(0, sync_en);
  DEF_BIT(1, sync_interrupt_en);
  DEF_BIT(2, in_sync);
  DEF_BIT(3, sync_detected);
  DEF_FIELD(17, 4, mfindex);
  DEF_FIELD(30, 18, mfoffset);
  static auto Get() { return hwreg::RegisterAddr<MFINDEX>(CRG_UCCR_OFFSET + 0x44); }
};

class PTMCR : public hwreg::RegisterBase<PTMCR, uint32_t> {
 public:
  DEF_FIELD(13, 0, ptm_delay);
  static auto Get() { return hwreg::RegisterAddr<PTMCR>(CRG_UCCR_OFFSET + 0x48); }
};

class PTMSR : public hwreg::RegisterBase<PTMSR, uint32_t> {
 public:
  DEF_BIT(2, in_sync);
  DEF_FIELD(17, 4, mfindex);
  DEF_FIELD(30, 18, mfoffset);
  static auto Get() { return hwreg::RegisterAddr<PTMSR>(CRG_UCCR_OFFSET + 0x4c); }
};

class EPENABLED : public hwreg::RegisterBase<EPENABLED, uint32_t> {
 public:
  DEF_FIELD(31, 0, ep_enabled);
  static auto Get() { return hwreg::RegisterAddr<EPENABLED>(CRG_UCCR_OFFSET + 0x60); }
};

class EPRUN : public hwreg::RegisterBase<EPRUN, uint32_t> {
 public:
  DEF_FIELD(31, 2, ep_running);
  static auto Get() { return hwreg::RegisterAddr<EPRUN>(CRG_UCCR_OFFSET + 0x64); }
};

class CMDPARA0 : public hwreg::RegisterBase<CMDPARA0, uint32_t> {
 public:
  DEF_FIELD(31, 0, cmd_para0);
  static auto Get() { return hwreg::RegisterAddr<CMDPARA0>(CRG_UCCR_OFFSET + 0x70); }
};

class CMDPARA1 : public hwreg::RegisterBase<CMDPARA1, uint32_t> {
 public:
  DEF_FIELD(31, 0, cmd_para1);
  static auto Get() { return hwreg::RegisterAddr<CMDPARA1>(CRG_UCCR_OFFSET + 0x74); }
};

class CMDCTRL : public hwreg::RegisterBase<CMDCTRL, uint32_t> {
 public:
  DEF_BIT(0, cmd_active);
  DEF_BIT(1, cmd_ioc);
  DEF_FIELD(7, 4, cmd_type);
  DEF_FIELD(19, 16, cmd_status);
  static auto Get() { return hwreg::RegisterAddr<CMDCTRL>(CRG_UCCR_OFFSET + 0x78); }
};

// ODB Capability Register
class ODBCAP : public hwreg::RegisterBase<ODBCAP, uint32_t> {
 public:
  DEF_FIELD(10, 0, odb_ram_size);
  static auto Get() { return hwreg::RegisterAddr<ODBCAP>(CRG_UCCR_OFFSET + 0x80); }
};

class ODBCFG : public hwreg::RegisterBase<ODBCFG, uint32_t> {
 public:
  DEF_FIELD(9, 0, epn_offset);
  DEF_FIELD(12, 10, epn_size);
  DEF_FIELD(25, 16, epn_add1_offset);
  DEF_FIELD(28, 26, epn_add1_size);
  static auto Get() { return hwreg::RegisterAddr<ODBCFG>(CRG_UCCR_OFFSET + 0x90); }
};

class DEBUG0 : public hwreg::RegisterBase<DEBUG0, uint32_t> {
 public:
  DEF_FIELD(6, 0, dev_addr);
  DEF_FIELD(11, 8, nump_limit);
  static auto Get() { return hwreg::RegisterAddr<DEBUG0>(CRG_UCCR_OFFSET + 0xb0); }
};

class IMAN : public hwreg::RegisterBase<IMAN, uint32_t> {
 public:
  DEF_BIT(0, ip);
  DEF_BIT(1, ie);
  static auto Get() { return hwreg::RegisterAddr<IMAN>(CRG_UICR_OFFSET + 0x0); }
};

class IMOD : public hwreg::RegisterBase<IMOD, uint32_t> {
 public:
  DEF_FIELD(15, 0, imodi);
  DEF_FIELD(31, 16, imodc);
  static auto Get() { return hwreg::RegisterAddr<IMOD>(CRG_UICR_OFFSET + 0x4); }
};

class ERSTSZ : public hwreg::RegisterBase<ERSTSZ, uint32_t> {
 public:
  DEF_FIELD(15, 0, erstsz);
  static auto Get() { return hwreg::RegisterAddr<ERSTSZ>(CRG_UICR_OFFSET + 0x8); }
};

class ERSTBALO : public hwreg::RegisterBase<ERSTBALO, uint32_t> {
 public:
  DEF_FIELD(31, 0, erstba_lo);
  static auto Get() { return hwreg::RegisterAddr<ERSTBALO>(CRG_UICR_OFFSET + 0x10); }
};

class ERSTBAHI : public hwreg::RegisterBase<ERSTBAHI, uint32_t> {
 public:
  DEF_FIELD(31, 0, erstba_hi);
  static auto Get() { return hwreg::RegisterAddr<ERSTBAHI>(CRG_UICR_OFFSET + 0x14); }
};

class ERDPLO : public hwreg::RegisterBase<ERDPLO, uint32_t> {
 public:
  DEF_FIELD(31, 0, erdp_lo);
  static auto Get() { return hwreg::RegisterAddr<ERDPLO>(CRG_UICR_OFFSET + 0x18); }
};

class ERDPHI : public hwreg::RegisterBase<ERDPHI, uint32_t> {
 public:
  DEF_FIELD(31, 0, erdp_hi);
  static auto Get() { return hwreg::RegisterAddr<ERDPHI>(CRG_UICR_OFFSET + 0x1c); }
};

#endif  // SRC_DEVICES_USB_DRIVERS_CRG_UDC_CRG_UDC_REGS_H_
