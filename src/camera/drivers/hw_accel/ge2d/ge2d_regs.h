// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_HW_ACCEL_GE2D_GE2D_REGS_H_
#define SRC_CAMERA_DRIVERS_HW_ACCEL_GE2D_GE2D_REGS_H_

#include <zircon/types.h>

#include <hwreg/bitfields.h>

namespace ge2d {

// Register addresses are actually 0x800 * 4 bytes lower than listed in the T931 datasheet.
class Status0 : public hwreg::RegisterBase<Status0, uint32_t> {
 public:
  DEF_BIT(0, busy);
  DEF_BIT(1, command_valid);
  DEF_BIT(2, buffer_command_valid);
  DEF_BIT(3, dpcmd_ready);
  DEF_BIT(4, pdpcmd_ready);
  DEF_BIT(5, read_src2_cmd_ready);
  DEF_BIT(6, read_src1_cmd_ready);

  static auto Get() { return hwreg::RegisterAddr<Status0>(0xa4 * 4); };
};

class Status1 : public hwreg::RegisterBase<Status1, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<Status1>(0xa5 * 4); };
};

class GenCtrl0 : public hwreg::RegisterBase<GenCtrl0, uint32_t> {
 public:
  DEF_FIELD(25, 24, dst1_8b_mode_sel);
  DEF_BIT(11, x_yc_ratio);
  DEF_BIT(10, y_yc_ratio);
  DEF_BIT(0, src1_separate_enable);  // True for NV12/NV21 SRC1 input.

  static auto Get() { return hwreg::RegisterAddr<GenCtrl0>(0xa0 * 4); };
};

class GenCtrl1 : public hwreg::RegisterBase<GenCtrl1, uint32_t> {
 public:
  DEF_BIT(31, soft_reset);
  DEF_BIT(25, interrupt_on_idling);
  DEF_BIT(24, interrupt_on_completed);
  DEF_FIELD(7, 0, global_alpha);

  static auto Get() { return hwreg::RegisterAddr<GenCtrl1>(0xa1 * 4); };
};

class GenCtrl2 : public hwreg::RegisterBase<GenCtrl2, uint32_t> {
 public:
  enum ColorMap {
    kColorMap16CbCr = 9,
    kColorMap24NV12 = 14,
    kColorMap32RGBA8888 = 0,
  };
  enum Format { kFormat8Bit = 0, kFormat16Bit = 1, kFormat24Bit = 2, kFormat32Bit = 3 };
  DEF_BIT(31, alpha_conversion_mode0);
  DEF_BIT(30, color_conversion_mode);
  DEF_BIT(29, src1_global_alpha_en);
  DEF_BIT(28, dst1_color_round_mode);
  DEF_BIT(27, src2_color_expand_mode);
  DEF_BIT(26, src2_alpha_expand_mode);
  DEF_BIT(25, src1_color_expand_mode);
  DEF_BIT(24, src1_alpha_expand_mode);
  DEF_BIT(23, dst_little_endian);
  DEF_FIELD(22, 19, dst1_color_map);
  DEF_BIT(18, alu_mult_mode);
  DEF_FIELD(17, 16, dst1_format);
  DEF_BIT(15, src2_little_endian);
  DEF_FIELD(14, 11, src2_color_map);
  DEF_BIT(10, alpha_conversion_mode1);
  DEF_FIELD(9, 8, src2_format);
  DEF_BIT(7, src1_little_endian);
  DEF_FIELD(6, 3, src1_color_map);
  DEF_BIT(2, src1_deepcolor);
  DEF_FIELD(1, 0, src1_format);

  static auto Get() { return hwreg::RegisterAddr<GenCtrl2>(0xa2 * 4); };
};

class CmdCtrl : public hwreg::RegisterBase<CmdCtrl, uint32_t> {
 public:
  DEF_BIT(0, cmd_wr);

  static auto Get() { return hwreg::RegisterAddr<CmdCtrl>(0xa3 * 4); };
};

class Src1FmtCtrl : public hwreg::RegisterBase<Src1FmtCtrl, uint32_t> {
 public:
  DEF_BIT(19, horizontal_repeat);
  DEF_BIT(18, horizontal_enable);
  DEF_BIT(17, vertical_repeat);
  DEF_BIT(16, vertical_enable);
  DEF_FIELD(15, 8, x_chroma_phase);
  DEF_FIELD(7, 0, y_chroma_phase);

  static auto Get() { return hwreg::RegisterAddr<Src1FmtCtrl>(0xae * 4); };
};

class GenCtrl3 : public hwreg::RegisterBase<GenCtrl3, uint32_t> {
 public:
  enum DiscardMode {
    kDiscardModeNone = 0,
    kDiscardModeEven = 2,
    kDiscardModeOdd = 3,
  };
  DEF_FIELD(22, 19, dst2_color_map);
  DEF_FIELD(17, 16, dst2_format);
  DEF_FIELD(13, 12, dst2_x_discard_mode);
  DEF_FIELD(11, 10, dst2_y_discard_mode);
  DEF_BIT(8, dst2_enable);
  DEF_BIT(0, dst1_enable);

  static auto Get() { return hwreg::RegisterAddr<GenCtrl3>(0xe8 * 4); };
};

class Src1DefColor : public hwreg::RegisterBase<Src1DefColor, uint32_t> {
 public:
  DEF_FIELD(31, 24, y_or_r);
  DEF_FIELD(23, 16, cb_or_g);
  DEF_FIELD(15, 8, cr_or_b);
  DEF_FIELD(7, 0, alpha);

  static auto Get() { return hwreg::RegisterAddr<Src1DefColor>(0xa6 * 4); };
};

class Src1ClipXStartEnd : public hwreg::RegisterBase<Src1ClipXStartEnd, uint32_t> {
 public:
  DEF_BIT(31, start_extra);
  DEF_FIELD(28, 16, start);
  DEF_BIT(15, end_extra);
  DEF_FIELD(12, 0, end);

  static auto Get() { return hwreg::RegisterAddr<Src1ClipXStartEnd>(0xa7 * 4); };
};

class Src1ClipYStartEnd : public hwreg::RegisterBase<Src1ClipYStartEnd, uint32_t> {
 public:
  DEF_FIELD(28, 16, start);
  DEF_FIELD(12, 0, end);

  static auto Get() { return hwreg::RegisterAddr<Src1ClipYStartEnd>(0xa8 * 4); };
};

class Src1XStartEnd : public hwreg::RegisterBase<Src1XStartEnd, uint32_t> {
 public:
  DEF_FIELD(31, 30, start_extra);
  DEF_FIELD(29, 16, start);
  DEF_FIELD(15, 14, end_extra);
  DEF_FIELD(13, 0, end);

  static auto Get() { return hwreg::RegisterAddr<Src1XStartEnd>(0xaa * 4); };
};

class Src1YStartEnd : public hwreg::RegisterBase<Src1YStartEnd, uint32_t> {
 public:
  DEF_FIELD(31, 30, start_extra);
  DEF_FIELD(29, 16, start);
  DEF_FIELD(15, 14, end_extra);
  DEF_FIELD(13, 0, end);

  static auto Get() { return hwreg::RegisterAddr<Src1YStartEnd>(0xab * 4); };
};

class Src2ClipXStartEnd : public hwreg::RegisterBase<Src2ClipXStartEnd, uint32_t> {
 public:
  DEF_FIELD(28, 16, start);
  DEF_FIELD(12, 0, end);

  static auto Get() { return hwreg::RegisterAddr<Src2ClipXStartEnd>(0xb0 * 4); };
};

class Src2ClipYStartEnd : public hwreg::RegisterBase<Src2ClipYStartEnd, uint32_t> {
 public:
  DEF_FIELD(28, 16, start);
  DEF_FIELD(12, 0, end);

  static auto Get() { return hwreg::RegisterAddr<Src2ClipYStartEnd>(0xb1 * 4); };
};

class Src2XStartEnd : public hwreg::RegisterBase<Src2XStartEnd, uint32_t> {
 public:
  DEF_FIELD(28, 16, start);
  DEF_FIELD(12, 0, end);

  static auto Get() { return hwreg::RegisterAddr<Src2XStartEnd>(0xb2 * 4); };
};

class Src2YStartEnd : public hwreg::RegisterBase<Src2YStartEnd, uint32_t> {
 public:
  DEF_FIELD(28, 16, start);
  DEF_FIELD(12, 0, end);

  static auto Get() { return hwreg::RegisterAddr<Src2YStartEnd>(0xb3 * 4); };
};

class DstClipXStartEnd : public hwreg::RegisterBase<DstClipXStartEnd, uint32_t> {
 public:
  DEF_FIELD(28, 16, start);
  DEF_FIELD(12, 0, end);

  static auto Get() { return hwreg::RegisterAddr<DstClipXStartEnd>(0xb4 * 4); };
};

class DstClipYStartEnd : public hwreg::RegisterBase<DstClipYStartEnd, uint32_t> {
 public:
  DEF_FIELD(28, 16, start);
  DEF_FIELD(12, 0, end);

  static auto Get() { return hwreg::RegisterAddr<DstClipYStartEnd>(0xb5 * 4); };
};

class DstXStartEnd : public hwreg::RegisterBase<DstXStartEnd, uint32_t> {
 public:
  DEF_FIELD(28, 16, start);
  DEF_FIELD(12, 0, end);

  static auto Get() { return hwreg::RegisterAddr<DstXStartEnd>(0xb6 * 4); };
};

class DstYStartEnd : public hwreg::RegisterBase<DstYStartEnd, uint32_t> {
 public:
  DEF_FIELD(28, 16, start);
  DEF_FIELD(12, 0, end);

  static auto Get() { return hwreg::RegisterAddr<DstYStartEnd>(0xb7 * 4); };
};

class Src1Canvas : public hwreg::RegisterBase<Src1Canvas, uint32_t> {
 public:
  DEF_FIELD(31, 24, y);
  DEF_FIELD(23, 16, u);
  DEF_FIELD(15, 8, v);

  static auto Get() { return hwreg::RegisterAddr<Src1Canvas>(0xa9 * 4); };
};

class Src2DstCanvas : public hwreg::RegisterBase<Src2DstCanvas, uint32_t> {
 public:
  // Src2 and Dst don't support multiplane formats.
  DEF_FIELD(23, 16, dst2);
  DEF_FIELD(15, 8, src2);
  DEF_FIELD(7, 0, dst1);

  static auto Get() { return hwreg::RegisterAddr<Src2DstCanvas>(0xb8 * 4); };
};

class VscStartPhaseStep : public hwreg::RegisterBase<VscStartPhaseStep, uint32_t> {
 public:
  DEF_FIELD(28, 0, phase_step);

  static auto Get() { return hwreg::RegisterAddr<VscStartPhaseStep>(0xb9 * 4); };
};

class VscIniCtrl : public hwreg::RegisterBase<VscIniCtrl, uint32_t> {
 public:
  DEF_FIELD(30, 29, vertical_repeat_p0);
  DEF_FIELD(23, 0, vertical_initial_phase);

  static auto Get() { return hwreg::RegisterAddr<VscIniCtrl>(0xbb * 4); };
};

class HscStartPhaseStep : public hwreg::RegisterBase<HscStartPhaseStep, uint32_t> {
 public:
  DEF_FIELD(28, 0, phase_step);

  static auto Get() { return hwreg::RegisterAddr<HscStartPhaseStep>(0xbc * 4); };
};

class HscPhaseSlope : public hwreg::RegisterBase<HscPhaseSlope, uint32_t> {
 public:
  DEF_FIELD(24, 0, slope);

  static auto Get() { return hwreg::RegisterAddr<HscPhaseSlope>(0xbd * 4); };
};

class HscIniCtrl : public hwreg::RegisterBase<HscIniCtrl, uint32_t> {
 public:
  DEF_FIELD(30, 29, horizontal_repeat_p0);
  DEF_FIELD(28, 24, horizontal_advance_num_upper);  // Not documented in datasheet
  DEF_FIELD(23, 0, horizontal_initial_phase);

  static auto Get() { return hwreg::RegisterAddr<HscIniCtrl>(0xbe * 4); };
};

class HscAdvCtrl : public hwreg::RegisterBase<HscAdvCtrl, uint32_t> {
 public:
  DEF_FIELD(31, 24, advance_num);
  DEF_FIELD(23, 0, advance_phase);
  static auto Get() { return hwreg::RegisterAddr<HscAdvCtrl>(0xbf * 4); };
};

class ScMiscCtrl : public hwreg::RegisterBase<ScMiscCtrl, uint32_t> {
 public:
  DEF_BIT(28, hsc_div_en);
  DEF_FIELD(27, 15, hsc_dividing_length);
  DEF_BIT(14, pre_hsc_enable);
  DEF_BIT(13, pre_vsc_enable);
  DEF_BIT(12, vsc_enable);
  DEF_BIT(11, hsc_enable);
  DEF_BIT(9, hsc_rpt_ctrl);
  DEF_BIT(8, vsc_rpt_ctrl);

  static auto Get() { return hwreg::RegisterAddr<ScMiscCtrl>(0xc0 * 4); };
};

class MatrixPreOffset : public hwreg::RegisterBase<MatrixPreOffset, uint32_t> {
 public:
  DEF_FIELD(28, 20, offset0);
  DEF_FIELD(18, 10, offset1);
  DEF_FIELD(8, 0, offset2);

  static auto Get() { return hwreg::RegisterAddr<MatrixPreOffset>(0xc5 * 4); };
};

class MatrixCoef00_01 : public hwreg::RegisterBase<MatrixCoef00_01, uint32_t> {
 public:
  DEF_FIELD(28, 16, coef00);
  DEF_FIELD(12, 0, coef01);

  static auto Get() { return hwreg::RegisterAddr<MatrixCoef00_01>(0xc6 * 4); };
};

class MatrixCoef02_10 : public hwreg::RegisterBase<MatrixCoef02_10, uint32_t> {
 public:
  DEF_FIELD(28, 16, coef02);
  DEF_FIELD(12, 0, coef10);

  static auto Get() { return hwreg::RegisterAddr<MatrixCoef02_10>(0xc7 * 4); };
};

class MatrixCoef11_12 : public hwreg::RegisterBase<MatrixCoef11_12, uint32_t> {
 public:
  DEF_FIELD(28, 16, coef11);
  DEF_FIELD(12, 0, coef12);

  static auto Get() { return hwreg::RegisterAddr<MatrixCoef11_12>(0xc8 * 4); };
};

class MatrixCoef20_21 : public hwreg::RegisterBase<MatrixCoef20_21, uint32_t> {
 public:
  DEF_FIELD(28, 16, coef20);
  DEF_FIELD(12, 0, coef21);

  static auto Get() { return hwreg::RegisterAddr<MatrixCoef20_21>(0xc9 * 4); };
};

class MatrixCoef22Ctrl : public hwreg::RegisterBase<MatrixCoef22Ctrl, uint32_t> {
 public:
  DEF_FIELD(28, 16, coef22);
  DEF_BIT(6, saturation_enable);
  DEF_BIT(0, matrix_enable);

  static auto Get() { return hwreg::RegisterAddr<MatrixCoef22Ctrl>(0xca * 4); };
};

class MatrixOffset : public hwreg::RegisterBase<MatrixOffset, uint32_t> {
 public:
  DEF_FIELD(28, 20, offset0);
  DEF_FIELD(18, 10, offset1);
  DEF_FIELD(8, 0, offset2);

  static auto Get() { return hwreg::RegisterAddr<MatrixOffset>(0xcb * 4); };
};

class AluOpCtrl : public hwreg::RegisterBase<AluOpCtrl, uint32_t> {
 public:
  enum BlendingMode { kBlendingModeAdd = 0, kBlendingModeLogicOp = 5 };
  enum BlendingFactor {
    kBlendingFactorZero = 0b0000,
    kBlendingFactorOne = 0b0001,
    kBlendingFactorOneMinusSrcAlpha = 0b0111,
    kBlendingFactorOneMinusDstAlpha = 0b1001,
    kBlendingFactorOneMinusConstAlpha = 0b1101,
  };
  enum LogicOperation {
    kLogicOperationCopy = 0b0001,
    kLogicOperationSet = 0b0011,
  };

  enum ColorMult {
    kColorMultNone = 0,
    kColorMultNonPremult =
        1,                  // Also multiplies with global alpha - with SRC2 only supported on G12A+
    kColorMultPremult = 2,  // Also multiplies with global alpha
  };

  DEF_FIELD(28, 27, src2_cmult_ad);
  DEF_FIELD(26, 25, src1_color_mult);
  DEF_FIELD(24, 23, src2_color_mult);

  DEF_FIELD(22, 20, blending_mode);
  DEF_FIELD(19, 16, source_factor);
  DEF_FIELD(15, 12, logic_operation);
  DEF_FIELD(10, 8, alpha_blending_mode);
  DEF_FIELD(7, 4, alpha_source_factor);
  DEF_FIELD(3, 0, alpha_logic_operation);

  static auto Get() { return hwreg::RegisterAddr<AluOpCtrl>(0xcc * 4); };
};

class AluConstColor : public hwreg::RegisterBase<AluConstColor, uint32_t> {
 public:
  DEF_FIELD(31, 24, r);
  DEF_FIELD(23, 16, g);
  DEF_FIELD(15, 8, b);
  DEF_FIELD(7, 0, a);
  static auto Get() { return hwreg::RegisterAddr<AluConstColor>(0xcd * 4); };
};

class ScaleCoefIdx : public hwreg::RegisterBase<ScaleCoefIdx, uint32_t> {
 public:
  DEF_BIT(8, horizontal);

  static auto Get() { return hwreg::RegisterAddr<ScaleCoefIdx>(0xd4 * 4); };
};

class ScaleCoef : public hwreg::RegisterBase<ScaleCoef, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<ScaleCoef>(0xd5 * 4); };
};

}  // namespace ge2d

#endif  // SRC_CAMERA_DRIVERS_HW_ACCEL_GE2D_GE2D_REGS_H_
