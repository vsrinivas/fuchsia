// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_HDMITX_VPU_REGS_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_HDMITX_VPU_REGS_H_

#define READ32_VPU_REG(a) vpu_mmio_->Read32(a)
#define WRITE32_VPU_REG(a, v) vpu_mmio_->Write32(v, a)

// Offsets
#define VPU_VENC_VIDEO_TST_EN (0x1b70 << 2)
#define VPU_VENC_VIDEO_TST_MDSEL (0x1b71 << 2)
#define VPU_VENC_VIDEO_TST_Y (0x1b72 << 2)
#define VPU_VENC_VIDEO_TST_CB (0x1b73 << 2)
#define VPU_VENC_VIDEO_TST_CR (0x1b74 << 2)
#define VPU_VENC_VIDEO_TST_CLRBAR_STRT (0x1b75 << 2)
#define VPU_VENC_VIDEO_TST_CLRBAR_WIDTH (0x1b76 << 2)

#define VPU_ENCP_VIDEO_EN (0x1b80 << 2)
#define VPU_ENCI_VIDEO_EN (0x1b57 << 2)
#define VPU_ENCP_VIDEO_MODE (0x1b8d << 2)
#define VPU_ENCP_VIDEO_MODE_ADV (0x1b8e << 2)
#define VPU_ENCP_VIDEO_MAX_PXCNT (0x1b97 << 2)
#define VPU_ENCP_VIDEO_HAVON_END (0x1ba3 << 2)
#define VPU_ENCP_VIDEO_HAVON_BEGIN (0x1ba4 << 2)
#define VPU_ENCP_VIDEO_VAVON_ELINE (0x1baf << 2)
#define VPU_ENCP_VIDEO_VAVON_BLINE (0x1ba6 << 2)
#define VPU_ENCP_VIDEO_HSO_BEGIN (0x1ba7 << 2)
#define VPU_ENCP_VIDEO_HSO_END (0x1ba8 << 2)
#define VPU_ENCP_VIDEO_VSO_BEGIN (0x1ba9 << 2)
#define VPU_ENCP_VIDEO_VSO_END (0x1baa << 2)
#define VPU_ENCP_VIDEO_VSO_BLINE (0x1bab << 2)
#define VPU_ENCP_VIDEO_VSO_ELINE (0x1bac << 2)
#define VPU_ENCP_VIDEO_MAX_LNCNT (0x1bae << 2)
#define VPU_ENCP_DVI_HSO_BEGIN (0x1c30 << 2)
#define VPU_ENCP_DVI_HSO_END (0x1c31 << 2)
#define VPU_ENCP_DVI_VSO_BLINE_EVN (0x1c32 << 2)
#define VPU_ENCP_DVI_VSO_ELINE_EVN (0x1c34 << 2)
#define VPU_ENCP_DVI_VSO_BEGIN_EVN (0x1c36 << 2)
#define VPU_ENCP_DVI_VSO_END_EVN (0x1c38 << 2)
#define VPU_ENCP_DE_H_BEGIN (0x1c3a << 2)
#define VPU_ENCP_DE_H_END (0x1c3b << 2)
#define VPU_ENCP_DE_V_BEGIN_EVEN (0x1c3c << 2)
#define VPU_ENCP_DE_V_END_EVEN (0x1c3d << 2)
#define VPU_VPU_VIU_VENC_MUX_CTRL (0x271a << 2)
#define VPU_HDMI_SETTING (0x271b << 2)
#define VPU_HDMI_FMT_CTRL (0x2743 << 2)
#define VPU_HDMI_DITH_CNTL (0x27fc << 2)

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_HDMITX_VPU_REGS_H_
