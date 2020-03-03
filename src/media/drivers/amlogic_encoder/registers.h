// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_ENCODER_REGISTERS_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_ENCODER_REGISTERS_H_

#include <lib/mmio/mmio.h>

#include <cstdint>

#include <ddk/mmio-buffer.h>
#include <hwreg/bitfields.h>

template <class RegType>
class TypedRegisterAddr;

// Typed registers can be used only with a specific type of RegisterIo.
template <class MmioType, class DerivedType, class IntType, class PrinterState = void>
class TypedRegisterBase : public hwreg::RegisterBase<DerivedType, IntType, PrinterState> {
 public:
  using SelfType = DerivedType;
  using ValueType = IntType;
  using Mmio = MmioType;
  using AddrType = TypedRegisterAddr<SelfType>;
  SelfType& ReadFrom(MmioType* reg_io) {
    return hwreg::RegisterBase<DerivedType, IntType, PrinterState>::ReadFrom(
        static_cast<ddk::MmioBuffer*>(reg_io));
  }
  SelfType& WriteTo(MmioType* reg_io) {
    return hwreg::RegisterBase<DerivedType, IntType, PrinterState>::WriteTo(
        static_cast<ddk::MmioBuffer*>(reg_io));
  }
};

template <class RegType>
class TypedRegisterAddr : public hwreg::RegisterAddr<RegType> {
 public:
  TypedRegisterAddr(uint32_t reg_addr) : hwreg::RegisterAddr<RegType>(reg_addr) {}

  RegType ReadFrom(typename RegType::Mmio* reg_io) {
    RegType reg;
    reg.set_reg_addr(hwreg::RegisterAddr<RegType>::addr());
    reg.ReadFrom(reg_io);
    return reg;
  }
};

// Cbus does a lot of things, but mainly seems to handle audio and video
// processing.
class CbusRegisterIo : public ddk::MmioBuffer {
 public:
  CbusRegisterIo(MmioBuffer&& other) : ddk::MmioBuffer(std::move(other)) {}
};

// The DOS bus mainly seems to handle video decoding/encoding.
class DosRegisterIo : public ddk::MmioBuffer {
 public:
  DosRegisterIo(MmioBuffer&& other) : ddk::MmioBuffer(std::move(other)) {}
};

// Aobus communicates with the always-on power management processor.
class AoRegisterIo : public ddk::MmioBuffer {
 public:
  AoRegisterIo(MmioBuffer&& other) : ddk::MmioBuffer(std::move(other)) {}
};

// Hiubus mainly seems to handle clock control and gating.
class HiuRegisterIo : public ddk::MmioBuffer {
 public:
  HiuRegisterIo(MmioBuffer&& other) : ddk::MmioBuffer(std::move(other)) {}
};

#define DEFINE_REGISTER(name, type, address)                           \
  class name : public TypedRegisterBase<type, name, uint32_t> {        \
   public:                                                             \
    static auto Get() { return TypedRegisterAddr<name>((address)*4); } \
  };

#define REGISTER_BEGIN(name, type, address)                     \
  class name : public TypedRegisterBase<type, name, uint32_t> { \
   public:                                                      \
    static auto Get() { return AddrType((address)*4); }

#define REGISTER_END \
  }                  \
  ;

REGISTER_BEGIN(AoRtiGenPwrSleep0, AoRegisterIo, 0x3a)
DEF_BIT(1, dos_hcodec_d1_pwr_off);
DEF_BIT(0, dos_hcodec_pwr_off);
REGISTER_END

REGISTER_BEGIN(AoRtiGenPwrIso0, AoRegisterIo, 0x3b)
DEF_BIT(5, dos_hcodec_iso_out_en);
DEF_BIT(4, dos_hcodec_iso_in_en);
REGISTER_END

REGISTER_BEGIN(DosSwReset1, DosRegisterIo, 0x3f07)
enum { kAll = 0xffffffff, kNone = 0 };
DEF_BIT(17, hcodec_qdct);
DEF_BIT(16, hcodec_vlc);
DEF_BIT(14, hcodec_afifo);
DEF_BIT(13, hcodec_ddr);
DEF_BIT(12, hcodec_ccpu);
DEF_BIT(11, hcodec_mcpu);
DEF_BIT(10, hcodec_psc);
DEF_BIT(9, hcodec_pic_dc);
DEF_BIT(8, hcodec_dblk);
DEF_BIT(7, hcodec_mc);
DEF_BIT(6, hcodec_iqidct);
DEF_BIT(5, hcodec_vififo);
DEF_BIT(4, hcodec_vld_part);
DEF_BIT(3, hcodec_vld);
DEF_BIT(2, hcodec_assist);
REGISTER_END

REGISTER_BEGIN(DosGclkEn0, DosRegisterIo, 0x3f01)
DEF_FIELD(27, 12, hcodec_en);
REGISTER_END

REGISTER_BEGIN(DosGenCtrl0, DosRegisterIo, 0x3f02)
DEF_BIT(0, hcodec_auto_clock_gate);
REGISTER_END

REGISTER_BEGIN(DosMemPdHcodec, DosRegisterIo, 0x3f32)
REGISTER_END

REGISTER_BEGIN(HcodecAssistMmcCtrl1, DosRegisterIo, 0x1002)
enum { kCtrl = 0x32 };
REGISTER_END

REGISTER_BEGIN(HcodecMfdInReg1Ctrl, DosRegisterIo, 0x1009)
DEF_BIT(19, nr_enable);
DEF_FIELD(18, 16, ifmt_extra);
DEF_FIELD(15, 13, r2y_mode);
DEF_BIT(12, r2y_en);
DEF_BIT(9, interp_en);
DEF_BIT(8, y_size);
DEF_BIT(6, dsample_en);
DEF_FIELD(5, 4, oformat);
DEF_FIELD(3, 0, iformat);
REGISTER_END

REGISTER_BEGIN(HcodecMfdInReg3Canv, DosRegisterIo, 0x100b)
DEF_FIELD(31, 30, canv_idx1_bppy);
DEF_FIELD(29, 28, canv_idx0_bppy);
DEF_FIELD(27, 26, canv_idx1_bppx);
DEF_FIELD(25, 24, canv_idx0_bppx);
DEF_FIELD(23, 0, input);
REGISTER_END

REGISTER_BEGIN(HcodecMfdInReg4Lnr0, DosRegisterIo, 0x100c)
DEF_FIELD(31, 16, linear_bytes4p);
DEF_FIELD(15, 0, linear_bytesperline);
REGISTER_END

DEFINE_REGISTER(HcodecMfdInReg5Lnr1, DosRegisterIo, 0x100d)

REGISTER_BEGIN(HcodecMfdInReg8Dmbl, DosRegisterIo, 0x1010)
DEF_FIELD(31, 14, picsize_x);
DEF_FIELD(13, 0, picsize_y);
REGISTER_END

REGISTER_BEGIN(HcodecMfdInReg9Endn, DosRegisterIo, 0x1011)
DEF_FIELD(23, 21, field21);
DEF_FIELD(20, 18, field18);
DEF_FIELD(17, 15, field15);
DEF_FIELD(14, 12, field12);
DEF_FIELD(11, 9, field9);
DEF_FIELD(8, 6, field6);
DEF_FIELD(5, 3, field3);
DEF_FIELD(2, 0, field0);
REGISTER_END

REGISTER_BEGIN(HcodecMfdInReg0D, DosRegisterIo, 0x1015)
DEF_FIELD(31, 26, y_snr_gau_alp0_max);
DEF_FIELD(25, 20, y_snr_gau_alp0_min);
DEF_FIELD(19, 14, y_snr_gau_bld_rate);
DEF_FIELD(13, 6, y_snr_gau_bld_ofst);
DEF_FIELD(5, 2, y_snr_gau_bld_core);
DEF_BIT(1, y_snr_err_norm);
DEF_BIT(0, cfg_y_snr_en);
REGISTER_END

REGISTER_BEGIN(HcodecMfdInReg0E, DosRegisterIo, 0x1016)
DEF_FIELD(31, 19, y_tnr_deghost_os);
DEF_FIELD(18, 13, y_tnr_alpha_max);
DEF_FIELD(12, 7, y_tnr_alpha_min);
DEF_FIELD(6, 3, y_tnr_mot_sad_margin);
DEF_BIT(2, y_tnr_txt_mode);
DEF_BIT(1, y_tnr_mc_en);
DEF_BIT(0, cfg_y_tnr_en);
REGISTER_END

REGISTER_BEGIN(HcodecMfdInReg0F, DosRegisterIo, 0x1017)
DEF_FIELD(31, 24, y_tnr_mot_frcsad_lock);
DEF_FIELD(23, 16, y_tnr_mot_dismot_ofst);
DEF_FIELD(15, 8, y_tnr_mot_distxt_ofst);
DEF_FIELD(7, 4, y_tnr_mot_distxt_rate);
DEF_FIELD(3, 0, y_tnr_mot_cortxt_rate);
REGISTER_END

REGISTER_BEGIN(HcodecMfdInReg10, DosRegisterIo, 0x1018)
DEF_FIELD(31, 24, y_tnr_mot2alp_dis_ofst);
DEF_FIELD(23, 16, y_tnr_mot2alp_dis_gain);
DEF_FIELD(15, 8, y_tnr_mot2alp_nrm_gain);
DEF_FIELD(7, 0, y_tnr_mot2alp_frc_gain);
REGISTER_END

REGISTER_BEGIN(HcodecMfdInReg11, DosRegisterIo, 0x1019)
DEF_FIELD(31, 14, y_bld_beta_max);
DEF_FIELD(13, 8, y_bld_beta_min);
DEF_FIELD(7, 0, y_bld_beta2alp_rate);
REGISTER_END

REGISTER_BEGIN(HcodecMfdInReg12, DosRegisterIo, 0x101a)
DEF_FIELD(31, 26, c_snr_gau_alp0_max);
DEF_FIELD(25, 20, c_snr_gau_alp0_min);
DEF_FIELD(19, 14, c_snr_gau_bld_rate);
DEF_FIELD(13, 6, c_snr_gau_bld_ofst);
DEF_FIELD(5, 2, c_snr_gau_bld_core);
DEF_BIT(1, c_snr_err_norm);
DEF_BIT(0, cfg_c_snr_en);
REGISTER_END

REGISTER_BEGIN(HcodecMfdInReg13, DosRegisterIo, 0x101b)
DEF_FIELD(31, 19, c_tnr_deghost_os);
DEF_FIELD(18, 13, c_tnr_alpha_max);
DEF_FIELD(12, 7, c_tnr_alpha_min);
DEF_FIELD(6, 3, c_tnr_mot_sad_margin);
DEF_BIT(2, c_tnr_txt_mode);
DEF_BIT(1, c_tnr_mc_en);
DEF_BIT(0, cfg_c_tnr_en);
REGISTER_END

REGISTER_BEGIN(HcodecMfdInReg14, DosRegisterIo, 0x101c)
DEF_FIELD(31, 24, c_tnr_mot_frcsad_lock);
DEF_FIELD(23, 16, c_tnr_mot_dismot_ofst);
DEF_FIELD(15, 8, c_tnr_mot_distxt_ofst);
DEF_FIELD(7, 4, c_tnr_mot_distxt_rate);
DEF_FIELD(3, 0, c_tnr_mot_cortxt_rate);
REGISTER_END

REGISTER_BEGIN(HcodecMfdInReg15, DosRegisterIo, 0x101d)
DEF_FIELD(31, 24, c_tnr_mot2alp_dis_ofst);
DEF_FIELD(23, 16, c_tnr_mot2alp_dis_gain);
DEF_FIELD(15, 8, c_tnr_mot2alp_nrm_gain);
DEF_FIELD(7, 0, c_tnr_mot2alp_frc_gain);
REGISTER_END

REGISTER_BEGIN(HcodecMfdInReg16, DosRegisterIo, 0x101e)
DEF_FIELD(31, 14, c_bld_beta_max);
DEF_FIELD(13, 8, c_bld_beta_min);
DEF_FIELD(7, 0, c_bld_beta2alp_rate);
REGISTER_END

DEFINE_REGISTER(HcodecAssistAmr1Int0, DosRegisterIo, 0x1025)
DEFINE_REGISTER(HcodecAssistAmr1Int1, DosRegisterIo, 0x1026)
DEFINE_REGISTER(HcodecAssistAmr1Int3, DosRegisterIo, 0x1028)

DEFINE_REGISTER(HcodecIrqMboxClear, DosRegisterIo, 0x1079)
DEFINE_REGISTER(HcodecIrqMboxMask, DosRegisterIo, 0x107a)

DEFINE_REGISTER(HcodecMpsr, DosRegisterIo, 0x1301)
DEFINE_REGISTER(HcodecCpsr, DosRegisterIo, 0x1321)

REGISTER_BEGIN(HcodecImemDmaCtrl, DosRegisterIo, 0x1340)
enum { kCtrl = 0x7 };
DEF_FIELD(18, 16, ctrl);
DEF_BIT(15, ready);
REGISTER_END

DEFINE_REGISTER(HcodecImemDmaAdr, DosRegisterIo, 0x1341)
DEFINE_REGISTER(HcodecImemDmaCount, DosRegisterIo, 0x1342)

REGISTER_BEGIN(HcodecHdecMcOmemAuto, DosRegisterIo, 0x1930)
DEF_BIT(31, use_omem_mb_xy);
DEF_FIELD(30, 16, omem_max_mb_x);
REGISTER_END

DEFINE_REGISTER(HcodecAnc0CanvasAddr, DosRegisterIo, 0x1990)

DEFINE_REGISTER(HcodecDbkRCanvasAddr, DosRegisterIo, 0x19b0)
DEFINE_REGISTER(HcodecDbkWCanvasAddr, DosRegisterIo, 0x19b1)
DEFINE_REGISTER(HcodecRecCanvasAddr, DosRegisterIo, 0x19b2)
DEFINE_REGISTER(HcodecCurrCanvasCtrl, DosRegisterIo, 0x19b3)

enum EncoderStatus {
  kIdle = 0,
  kSequence,
  kPicture,
  kIdr,
  kNonIdr,
  kMbHeader,
  kSequenceDone,
  kPictureDone,
  kIdrDone,
  kNonIdrDone,
  kMbHeaderDone,
  kMbDataDone,
  kNonIdrIntra,
  kNonIdrInter,
  kError = 0xff,
};

// in scratch registers
DEFINE_REGISTER(HcodecEncoderStatus, DosRegisterIo, 0x1ac0)
DEFINE_REGISTER(HcodecMemOffsetReg, DosRegisterIo, 0x1ac1)
DEFINE_REGISTER(HcodecIdrPicId, DosRegisterIo, 0x1ac5)
DEFINE_REGISTER(HcodecFrameNumber, DosRegisterIo, 0x1ac6)
DEFINE_REGISTER(HcodecPicOrderCntLsb, DosRegisterIo, 0x1ac7)
DEFINE_REGISTER(HcodecLog2MaxPicOrderCntLsb, DosRegisterIo, 0x1ac8)
DEFINE_REGISTER(HcodecLog2MaxFrameNum, DosRegisterIo, 0x1ac9)
DEFINE_REGISTER(HcodecAnc0BufferId, DosRegisterIo, 0x1aca)
DEFINE_REGISTER(HcodecQpPicture, DosRegisterIo, 0x1acb)

REGISTER_BEGIN(HcodecIeMeMbType, DosRegisterIo, 0x1acd)
enum MbType {
  kDefault = 0,
  kI4MB = 0x9,
  kAuto = 0xffff00ff,
};
REGISTER_END

REGISTER_BEGIN(HcodecIeMeMode, DosRegisterIo, 0x1ace)
DEF_FIELD(4, 0, ie_pippeline_block);
DEF_BIT(5, me_half_pixel_in_m8);
DEF_BIT(6, me_step2_sub_pixel_in_m8);
REGISTER_END

DEFINE_REGISTER(HcodecIeRefSel, DosRegisterIo, 0x1acf)

// For CBR
DEFINE_REGISTER(HcodecEncCbrTableAddr, DosRegisterIo, 0x1ac3)
DEFINE_REGISTER(HcodecEncCbrMbSizeAddr, DosRegisterIo, 0x1ac4)

REGISTER_BEGIN(HcodecEncCbrCtl, DosRegisterIo, 0x1ad0)
static constexpr uint32_t kTableSize = 0x800;
static constexpr uint32_t kShortShift = 12;
static constexpr uint32_t kLongMbNum = 2;
static constexpr uint32_t kStartTableId = 8;
static constexpr uint32_t kLongThreshold = 4;
DEF_FIELD(31, 28, init_qp_table_idx);
DEF_FIELD(27, 24, short_term_adjust_shift);
DEF_FIELD(23, 16, long_term_mb_number);
DEF_FIELD(15, 0, long_term_adjust_threshold);
REGISTER_END

DEFINE_REGISTER(HcodecEncCbrTargetSize, DosRegisterIo, 0x1ad1)

REGISTER_BEGIN(HcodecEncCbrRegionSize, DosRegisterIo, 0x1ad3)
static constexpr uint32_t kBlockWidth = 16;
static constexpr uint32_t kBlockHeight = 9;
DEF_FIELD(31, 16, block_w);
DEF_FIELD(15, 0, block_h);
REGISTER_END

DEFINE_REGISTER(HcodecInfoDumpStartAddr, DosRegisterIo, 0x1ad2)

REGISTER_BEGIN(HcodecFixedSliceCfg, DosRegisterIo, 0x1ad5)
DEF_FIELD(31, 0, num_rows_per_slice);
REGISTER_END

DEFINE_REGISTER(HcodecIeDataFeedBuffInfo, DosRegisterIo, 0x1ad8)

REGISTER_BEGIN(HcodecVlcConfig, DosRegisterIo, 0x1d01)
DEF_BIT(0, pop_coeff_even_all_zero);
REGISTER_END

DEFINE_REGISTER(HcodecVlcVbStartPtr, DosRegisterIo, 0x1d10)
DEFINE_REGISTER(HcodecVlcVbEndPtr, DosRegisterIo, 0x1d11)
DEFINE_REGISTER(HcodecVlcVbWrPtr, DosRegisterIo, 0x1d12)
DEFINE_REGISTER(HcodecVlcVbSwRdPtr, DosRegisterIo, 0x1d14)

REGISTER_BEGIN(HcodecVlcVbControl, DosRegisterIo, 0x1d16)
DEF_BIT(14, bit_14);
DEF_FIELD(5, 3, bits_5_3);
DEF_BIT(1, bit_1);
DEF_BIT(0, bit_0);
REGISTER_END

REGISTER_BEGIN(HcodecVlcVbMemCtl, DosRegisterIo, 0x1d17)
DEF_BIT(31, bit_31);
DEF_FIELD(30, 24, bits_30_24);
DEF_FIELD(23, 16, bits_23_16);
DEF_FIELD(1, 0, bits_1_0);
REGISTER_END

DEFINE_REGISTER(HcodecVlcTotalBytes, DosRegisterIo, 0x1d1a)
DEFINE_REGISTER(HcodecVlcIntControl, DosRegisterIo, 0x1d30)
REGISTER_BEGIN(HcodecVlcPicSize, DosRegisterIo, 0x1d31)
DEF_FIELD(31, 16, pic_height);
DEF_FIELD(15, 0, pic_width);
REGISTER_END

REGISTER_BEGIN(HcodecVlcPicPosition, DosRegisterIo, 0x1d33)
DEF_FIELD(31, 16, pic_mb_nr);
DEF_FIELD(15, 8, pic_mby);
DEF_FIELD(7, 0, pic_mbx);
REGISTER_END

DEFINE_REGISTER(HcodecVlcHcmdConfig, DosRegisterIo, 0x1d54)

REGISTER_BEGIN(HcodecVlcAdvConfig, DosRegisterIo, 0x1d25)
DEF_BIT(10, early_mix_mc_hcmd);
DEF_BIT(9, update_top_left_mix);
DEF_BIT(8, p_top_left_mix);
DEF_BIT(7, mv_cal_mixed_type);
DEF_BIT(6, mc_hcmd_mixed_type);
DEF_BIT(5, use_separate_int_control);
DEF_BIT(4, hcmd_intra_use_q_info);
DEF_BIT(3, hcmd_left_use_prev_info);
DEF_BIT(2, hcmd_use_q_info);
DEF_BIT(1, use_q_delta_quant);
DEF_BIT(0, detect_I16_from_I4);
REGISTER_END

REGISTER_BEGIN(HcodecIgnoreConfig, DosRegisterIo, 0x1f02)
DEF_BIT(31, ignore_lac_coeff_en);
DEF_BIT(26, ignore_lac_coeff_else);
DEF_BIT(21, ignore_lac_coeff_2);
DEF_FIELD(17, 16, ignore_lac_coeff_1);
DEF_BIT(15, ignore_cac_coeff_en);
DEF_BIT(10, ignore_cac_coeff_else);
DEF_BIT(5, ignore_cac_coeff_2);
DEF_FIELD(1, 0, ignore_cac_coeff_1);
REGISTER_END

REGISTER_BEGIN(HcodecIgnoreConfig2, DosRegisterIo, 0x1f03)
DEF_BIT(31, ignore_t_lac_coeff_en);
DEF_BIT(26, ignore_t_lac_coeff_else);
DEF_FIELD(22, 21, ignore_t_lac_coeff_2);
DEF_FIELD(18, 16, ignore_t_lac_coeff_1);
DEF_BIT(15, ignore_cdc_coeff_en);
DEF_BIT(14, ignore_t_lac_coeff_else_le_3);
DEF_BIT(13, ignore_t_lac_coeff_else_le_4);
DEF_BIT(12, ignore_cdc_only_when_empty_cac_inter);
DEF_BIT(11, ignore_cdc_only_when_one_empty_inter);
DEF_FIELD(10, 9, ignore_cdc_range_max_inter);
DEF_FIELD(8, 7, ignore_cdc_abs_max_inter);
DEF_BIT(5, ignore_cdc_only_when_empty_cac_intra);
DEF_BIT(4, ignore_cdc_only_when_one_empty_intra);
DEF_BIT(2, ignore_cdc_range_max_intra);
DEF_BIT(0, ignore_cdc_abs_max_intra);
REGISTER_END

DEFINE_REGISTER(HcodecQdctMbStartPtr, DosRegisterIo, 0x1f10)
DEFINE_REGISTER(HcodecQdctMbEndPtr, DosRegisterIo, 0x1f11)
DEFINE_REGISTER(HcodecQdctMbWrPtr, DosRegisterIo, 0x1f12)
DEFINE_REGISTER(HcodecQdctMbRdPtr, DosRegisterIo, 0x1f13)

REGISTER_BEGIN(HcodecQdctMbControl, DosRegisterIo, 0x1f15)
DEF_BIT(29, ie_start_int_enable);
DEF_BIT(28, ignore_t_p8x8);
DEF_BIT(27, zero_mc_out_null_non_skipped_mb);
DEF_BIT(26, no_mc_out_null_non_skipped_mb);
DEF_BIT(25, mc_out_even_skipped_mb);
DEF_BIT(24, mc_out_wait_cbp_ready);
DEF_BIT(23, mc_out_wait_mb_type_ready);
DEF_BIT(20, ie_sub_enable);
DEF_BIT(19, i_pred_enable);
DEF_BIT(18, iq_enable);
DEF_BIT(17, idct_enable);
DEF_BIT(14, mb_pause_enable);
DEF_BIT(13, q_enable);
DEF_BIT(12, dct_enable);
DEF_BIT(10, mb_info_en);
DEF_BIT(9, mb_info_soft_reset);
DEF_BIT(3, endian);
DEF_BIT(1, mb_read_en);
DEF_BIT(0, soft_reset);
REGISTER_END

DEFINE_REGISTER(HcodecQdctMbBuff, DosRegisterIo, 0x1f17)
REGISTER_BEGIN(HcodecQdctQQuantI, DosRegisterIo, 0x1f1c)
DEF_FIELD(25, 22, i_pic_qp_c);
DEF_FIELD(21, 16, i_pic_qp);
DEF_FIELD(15, 12, i_pic_qp_c_mod6);
DEF_FIELD(11, 8, i_pic_qp_c_div6);
DEF_FIELD(7, 4, i_pic_qp_mod6);
DEF_FIELD(3, 0, i_pic_qp_div6);
REGISTER_END
REGISTER_BEGIN(HcodecQdctQQuantP, DosRegisterIo, 0x1f1d)
DEF_FIELD(25, 22, p_pic_qp_c);
DEF_FIELD(21, 16, p_pic_qp);
DEF_FIELD(15, 12, p_pic_qp_c_mod6);
DEF_FIELD(11, 8, p_pic_qp_c_div6);
DEF_FIELD(7, 4, p_pic_qp_mod6);
DEF_FIELD(3, 0, p_pic_qp_div6);
REGISTER_END

REGISTER_BEGIN(HcodecQdctAdvConfig, DosRegisterIo, 0x1f34)
DEF_BIT(29, mb_info_latch_no_I16_pred_mode);
DEF_BIT(28, ie_dma_mbxy_use_i_pred);
DEF_BIT(27, ie_dma_read_write_use_ip_idx);
DEF_BIT(26, ie_start_use_top_dma_count);
DEF_BIT(25, i_pred_top_dma_rd_mbbot);
DEF_BIT(24, i_pred_top_dma_wr_disable);
DEF_BIT(23, i_pred_mix);
DEF_BIT(22, me_ab_rd_when_intra_in_p);
DEF_BIT(21, force_mb_skip_run_when_intra);
DEF_BIT(20, mc_out_mixed_type);
DEF_BIT(19, ie_start_when_quant_not_full);
DEF_BIT(18, mb_info_state_mix);
DEF_BIT(17, mb_type_use_mix_result);
DEF_BIT(16, me_cb_ie_read_enable);
DEF_BIT(15, ie_cur_data_from_me);
DEF_BIT(14, rem_per_use_table);
DEF_BIT(13, q_latch_int_enable);
DEF_BIT(12, q_use_table);
DEF_BIT(11, q_start_wait);
DEF_BIT(10, LUMA_16_LEFT_use_cur);
DEF_BIT(9, DC_16_LEFT_SUM_use_cur);
DEF_BIT(8, c_ref_ie_sel_cur);
DEF_BIT(7, c_ipred_perfect_mode);
DEF_BIT(6, ref_ie_ul_sel);
DEF_BIT(5, mb_type_use_ie_result);
DEF_BIT(4, detect_I16_from_I4);
DEF_BIT(3, ie_not_wait_ref_busy);
DEF_BIT(2, ie_I16_enable);
DEF_FIELD(1, 0, ie_done_sel);
REGISTER_END

REGISTER_BEGIN(HcodecIeWeight, DosRegisterIo, 0x1f35)
static constexpr uint32_t kI4MbWeightOffset = 0x755;
static constexpr uint32_t kI16MbWeightOffset = 0x340;
DEF_FIELD(31, 16, i16_weight);
DEF_FIELD(15, 0, i4_weight);
REGISTER_END

REGISTER_BEGIN(HcodecQQuantControl, DosRegisterIo, 0x1f36)
DEF_FIELD(31, 23, quant_table_addr);
DEF_BIT(22, quant_table_addr_update);
REGISTER_END

DEFINE_REGISTER(HcodecQuantTableData, DosRegisterIo, 0x1f39)

REGISTER_BEGIN(HcodecSadControl0, DosRegisterIo, 0x1f3a)
DEF_FIELD(31, 16, ie_sad_offset_I16);
DEF_FIELD(15, 0, ie_sad_offset_I4);
REGISTER_END

REGISTER_BEGIN(HcodecSadControl1, DosRegisterIo, 0x1f3b)
DEF_FIELD(27, 24, ie_sad_shift_I16);
DEF_FIELD(23, 20, ie_sad_shift_I4);
DEF_FIELD(19, 16, me_sad_shift_INTER);
DEF_FIELD(15, 0, me_sad_offset_INTER);
REGISTER_END

REGISTER_BEGIN(HcodecQdctVlcQuantCtl0, DosRegisterIo, 0x1f3c)
DEF_BIT(19, vlc_delta_quant_1);
DEF_FIELD(18, 13, vlc_quant_1);
DEF_BIT(6, vlc_delta_quant_0);
DEF_FIELD(5, 0, vlc_quant_0);
REGISTER_END

REGISTER_BEGIN(HcodecQdctVlcQuantCtl1, DosRegisterIo, 0x1f3d)
DEF_FIELD(11, 6, vlc_max_delta_q_neg);
DEF_FIELD(5, 0, vlc_max_delta_q_pos);
REGISTER_END

REGISTER_BEGIN(HcodecIeControl, DosRegisterIo, 0x1f40)
DEF_BIT(30, active_ul_block);
DEF_BIT(1, ie_enable);
DEF_BIT(0, ie_soft_reset);
REGISTER_END

REGISTER_BEGIN(HcodecSadControl, DosRegisterIo, 0x1f43)
DEF_BIT(3, ie_result_buff_enable);
DEF_BIT(2, ie_result_buff_soft_reset);
DEF_BIT(1, sad_enable);
DEF_BIT(0, sad_soft_reset);
REGISTER_END

DEFINE_REGISTER(HcodecIeResultBuffer, DosRegisterIo, 0x1f44)

REGISTER_BEGIN(HcodecMeSkipLine, DosRegisterIo, 0x1f4d)
DEF_FIELD(27, 24, step_3_skip_line);
DEF_FIELD(23, 18, step_2_skip_line);
DEF_FIELD(17, 12, step_1_skip_line);
DEF_FIELD(11, 6, step_0_skip_line);
REGISTER_END

REGISTER_BEGIN(HcodecMeSadEnough01, DosRegisterIo, 0x1f50)
DEF_FIELD(19, 12, me_sad_enough_1);
DEF_FIELD(11, 0, me_sad_enough_0);
REGISTER_END

REGISTER_BEGIN(HcodecMeSadEnough23, DosRegisterIo, 0x1f51)
DEF_FIELD(19, 12, adv_mv_8x8_enough);
DEF_FIELD(11, 0, me_sad_enough_2);
REGISTER_END

REGISTER_BEGIN(HcodecMeStep0CloseMv, DosRegisterIo, 0x1f52)
DEF_FIELD(21, 10, me_step0_big_sad);
DEF_FIELD(9, 5, me_step0_close_mv_y);
DEF_FIELD(4, 0, me_step0_close_mv_x);
REGISTER_END

REGISTER_BEGIN(HcodecMeFSkipSad, DosRegisterIo, 0x1f53)
DEF_FIELD(31, 24, force_skip_sad_3);
DEF_FIELD(23, 16, force_skip_sad_2);
DEF_FIELD(15, 8, force_skip_sad_1);
DEF_FIELD(7, 0, force_skip_sad_0);
REGISTER_END

REGISTER_BEGIN(HcodecMeFSkipWeight, DosRegisterIo, 0x1f54)
DEF_FIELD(31, 24, force_skip_weight_3);
DEF_FIELD(23, 16, force_skip_weight_2);
DEF_FIELD(15, 8, force_skip_weight_1);
DEF_FIELD(7, 0, force_skip_weight_0);
REGISTER_END

REGISTER_BEGIN(HcodecMeMvWeight01, DosRegisterIo, 0x1f56)
DEF_FIELD(31, 24, me_mv_step_weight_1);
DEF_FIELD(23, 16, me_mv_pre_weight_1);
DEF_FIELD(15, 8, me_mv_step_weight_0);
DEF_FIELD(7, 0, me_mv_pre_weight_0);
REGISTER_END

REGISTER_BEGIN(HcodecMeMvWeight23, DosRegisterIo, 0x1f57)
DEF_FIELD(31, 24, me_mv_step_weight_3);
DEF_FIELD(23, 16, me_mv_pre_weight_3);
DEF_FIELD(15, 8, me_mv_step_weight_2);
DEF_FIELD(7, 0, me_mv_pre_weight_2);
REGISTER_END

REGISTER_BEGIN(HcodecMeSadRangeInc, DosRegisterIo, 0x1f58)
DEF_FIELD(31, 24, me_sad_range_3);
DEF_FIELD(23, 16, me_sad_range_2);
DEF_FIELD(15, 8, me_sad_range_1);
DEF_FIELD(7, 0, me_sad_range_0);
REGISTER_END

REGISTER_BEGIN(HcodecMeWeight, DosRegisterIo, 0x1f60)
static constexpr uint32_t kMeWeightOffset = 0x340;
REGISTER_END

REGISTER_BEGIN(HcodecAdvMvCtl0, DosRegisterIo, 0x1f69)
DEF_BIT(31, adv_mv_large_16x8);
DEF_BIT(30, adv_mv_large_8x16);
DEF_FIELD(27, 16, adv_mv_8x8_weight);
DEF_FIELD(15, 0, adv_mv_4x4x4_weight);
REGISTER_END

REGISTER_BEGIN(HcodecAdvMvCtl1, DosRegisterIo, 0x1f6a)
DEF_FIELD(27, 16, adv_mv_16x16_weight);
DEF_BIT(15, adv_mv_large_16x16);
DEF_FIELD(11, 0, adv_mv_16x8_weight);
REGISTER_END

REGISTER_BEGIN(HcodecV3SkipControl, DosRegisterIo, 0x1f6c)
DEF_BIT(31, v3_skip_enable);
DEF_BIT(30, v3_step_1_weight_enable);
DEF_BIT(28, v3_mv_sad_weight_enable);
DEF_BIT(27, v3_ipred_type_enable);
DEF_FIELD(19, 12, v3_force_skip_sad_1);
DEF_FIELD(11, 0, v3_force_skip_sad_0);
REGISTER_END

REGISTER_BEGIN(HcodecV3SkipWeight, DosRegisterIo, 0x1f70)
DEF_FIELD(31, 16, v3_skip_weight_1);
DEF_FIELD(15, 0, v3_skip_weight_0);
REGISTER_END

REGISTER_BEGIN(HcodecV3L1SkipMaxSad, DosRegisterIo, 0x1f71)
DEF_FIELD(31, 16, v3_level_1_f_skip_max_sad);
DEF_FIELD(15, 0, v3_level_1_skip_max_sad);
REGISTER_END

REGISTER_BEGIN(HcodecV3L2SkipWeight, DosRegisterIo, 0x1f72)
DEF_FIELD(31, 16, v3_force_skip_sad_2);
DEF_FIELD(15, 0, v3_skip_weight_2);
REGISTER_END

DEFINE_REGISTER(HcodecV3MvSadTable, DosRegisterIo, 0x1f73)

REGISTER_BEGIN(HcodecV3FZeroCtl0, DosRegisterIo, 0x1f74)
DEF_FIELD(31, 16, v3_ie_f_zero_sad_I16);
DEF_FIELD(15, 0, v3_ie_f_zero_sad_I4);
REGISTER_END

REGISTER_BEGIN(HcodecV3FZeroCtl1, DosRegisterIo, 0x1f75)
DEF_BIT(25, v3_no_ver_when_top_zero_en);
DEF_BIT(24, v3_no_hor_when_left_zero_en);
DEF_FIELD(17, 16, type_hor_break);
DEF_FIELD(15, 0, v3_me_f_zero_sad);
REGISTER_END

REGISTER_BEGIN(HcodecV3IpredTypeWeight0, DosRegisterIo, 0x1f78)
DEF_FIELD(31, 24, C_ipred_weight_H);
DEF_FIELD(23, 16, C_ipred_weight_V);
DEF_FIELD(15, 8, I4_ipred_weight_else);
DEF_FIELD(7, 0, I4_ipred_weight_most);
REGISTER_END

REGISTER_BEGIN(HcodecV3IpredTypeWeight1, DosRegisterIo, 0x1f79)
DEF_FIELD(31, 24, I16_ipred_weight_DC);
DEF_FIELD(23, 16, I16_ipred_weight_H);
DEF_FIELD(15, 8, I16_ipred_weight_V);
DEF_FIELD(7, 0, C_ipred_weight_DC);
REGISTER_END

REGISTER_BEGIN(HcodecV3LeftSmallMaxSad, DosRegisterIo, 0x1f7a)
DEF_FIELD(31, 16, v3_left_small_max_me_sad);
DEF_FIELD(15, 0, v3_left_small_max_ie_sad);
REGISTER_END

REGISTER_BEGIN(HcodecV4ForceSkipCfg, DosRegisterIo, 0x1f7b)
DEF_FIELD(31, 26, v4_force_q_r_intra);
DEF_FIELD(25, 20, v4_force_q_r_inter);
DEF_BIT(19, v4_force_q_y_enable);
DEF_FIELD(18, 16, v4_force_qr_y);
DEF_FIELD(15, 12, v4_force_qp_y);
DEF_BIT(0, v4_force_skip_sad);
REGISTER_END

REGISTER_BEGIN(HhiVdecClkCntl, HiuRegisterIo, 0x78)
DEF_FIELD(27, 25, hcodec_clk_sel);
DEF_BIT(24, hcodec_clk_en);
DEF_FIELD(22, 16, hcodec_clk_div);
REGISTER_END

REGISTER_BEGIN(HhiGclkMpeg0, HiuRegisterIo, 0x50)
DEF_BIT(1, dos);
REGISTER_END

#undef REGISTER_BEGIN
#undef REGISTER_END
#undef DEFINE_REGISTER

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_ENCODER_REGISTERS_H_
