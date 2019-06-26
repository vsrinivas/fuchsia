// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ovl.h"

#include <fbl/alloc_checker.h>
#include <math.h>

#include "registers-ovl.h"

namespace mt8167s_display {

namespace {
constexpr uint32_t kDefaultBackgroundColor = 0xFF000000;  // alpha/red/green/blue
constexpr int kIdleTimeout = 200000;                      // uSec
}  // namespace

zx_status_t Ovl::Init(zx_device_t* parent) {
  if (initialized_) {
    return ZX_OK;
  }

  zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev_);
  if (status != ZX_OK) {
    return status;
  }

  // Map Ovl mmio
  mmio_buffer_t mmio;
  status = pdev_map_mmio_buffer(&pdev_, MMIO_DISP_OVL, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map OVL mmio\n");
    return status;
  }
  fbl::AllocChecker ac;
  ovl_mmio_ = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, mmio);
  if (!ac.check()) {
    DISP_ERROR("Could not mapp Overlay MMIO\n");
    return ZX_ERR_NO_MEMORY;
  }

  // Get BTI from parent
  status = pdev_get_bti(&pdev_, 0, bti_.reset_and_get_address());
  if (status != ZX_OK) {
    DISP_ERROR("Could not get BTI handle\n");
    return status;
  }

  // Ovl is ready to be used
  initialized_ = true;
  return ZX_OK;
}

void Ovl::Reset() {
  ZX_DEBUG_ASSERT(initialized_);
  ovl_mmio_->Write32(1, OVL_RST);
  ovl_mmio_->Write32(0, OVL_RST);
  Stop();
  active_layers_ = 0;
  layer_handle_[0] = 0;
  layer_handle_[1] = 0;
  layer_handle_[2] = 0;
  layer_handle_[3] = 0;
}

bool Ovl::IsSupportedFormat(zx_pixel_format_t format) {
  if ((format == ZX_PIXEL_FORMAT_RGB_565) || (format == ZX_PIXEL_FORMAT_ARGB_8888) ||
      (format == ZX_PIXEL_FORMAT_RGB_x888)) {
    return true;
  } else {
    return false;
  }
}

uint32_t Ovl::GetFormat(zx_pixel_format_t format) {
  switch (format) {
    case ZX_PIXEL_FORMAT_RGB_565:
      return RGB565;
    case ZX_PIXEL_FORMAT_ARGB_8888:
      return BGRA8888;
    case ZX_PIXEL_FORMAT_RGB_x888:
      return RGB888;
  }

  return 0;
}

bool Ovl::ByteSwapNeeded(zx_pixel_format_t format) {
  switch (format) {
    case ZX_PIXEL_FORMAT_ARGB_8888:
    case ZX_PIXEL_FORMAT_RGB_x888:
      return false;
    case ZX_PIXEL_FORMAT_RGB_565:
      return true;
    default:
      ZX_DEBUG_ASSERT(false);
  }

  return false;
}

uint32_t Ovl::GetBytesPerPixel(zx_pixel_format_t format) {
  switch (format) {
    case ZX_PIXEL_FORMAT_RGB_565:
      return 2;
    case ZX_PIXEL_FORMAT_ARGB_8888:
    case ZX_PIXEL_FORMAT_RGB_x888:
      return 4;
    default:
      ZX_DEBUG_ASSERT(false);
  }

  return 0;
}

void Ovl::Start() {
  ZX_DEBUG_ASSERT(initialized_);
  // Enable the overlay engine and interrupts
  ovl_mmio_->Write32(INT_FRAME_COMPLETE, OVL_INTEN);
  ovl_mmio_->Write32(0x1, OVL_EN);
  ovl_mmio_->ModifyBits32(0x1, 0, 1, OVL_DATAPATH_CON);
}

void Ovl::Stop() {
  ZX_DEBUG_ASSERT(initialized_);
  int timeout = kIdleTimeout;

  // Disable sources of interrupt and display the overlay engine first
  ovl_mmio_->Write32(0x0, OVL_INTEN);
  ovl_mmio_->Write32(0x0, OVL_EN);
  ovl_mmio_->Write32(0x0, OVL_INTSTA);

  // Wait for all operations to finish and the state machine is idle
  while (!IsIdle() && timeout--) {
    zx_nanosleep(zx_deadline_after(ZX_USEC(1)));
  }
  if (timeout <= 0) {
    DISP_ERROR("Ovl could not stop\n");
    PrintStatusRegisters();
    ZX_ASSERT(false);
  }

  // Now that we are idle, we can disable other parts of the engine
  ovl_mmio_->Write32(0, OVL_DATAPATH_CON);
  ovl_mmio_->Write32(0, OVL_RDMAx_CTRL(0));
  ovl_mmio_->Write32(0, OVL_RDMAx_CTRL(1));
  ovl_mmio_->Write32(0, OVL_RDMAx_CTRL(2));
  ovl_mmio_->Write32(0, OVL_RDMAx_CTRL(3));
  ovl_mmio_->Write32(0, OVL_SRC_CON);
}

zx_status_t Ovl::Config(uint8_t layer, OvlConfig& cfg) {
  ZX_DEBUG_ASSERT(initialized_);
  ZX_DEBUG_ASSERT(layer < kMaxLayer);
  // Overlay does not support scaling.
  ZX_DEBUG_ASSERT(cfg.src_frame.height == cfg.dest_frame.height &&
                  cfg.src_frame.width == cfg.dest_frame.width);

  // Configure ROI first. ROI abbreviation is not clarified in docs. Assuming it means
  // Region Of Interest. Also, ROI does not seem to be "layer" related. So it might mean the
  // final screen after all the layers have been mixed.
  ovl_mmio_->Write32(height_ << 16 | width_, OVL_ROI_SIZE);
  ovl_mmio_->Write32(kDefaultBackgroundColor, OVL_ROI_BGCLR);

  // Enable Layer
  // We cannot simply enable a layer and/or its RDMA channel source. This could
  // only be done at IDLE time. Config should only be called after calling the stop function
  // which places the engine in Idle.
  if (IsIdle()) {
    ovl_mmio_->Write32(ovl_mmio_->Read32(OVL_SRC_CON) | SRC_CON_ENABLE_LAYER(layer), OVL_SRC_CON);
    ovl_mmio_->ModifyBits32(1, 0, 1, OVL_RDMAx_CTRL(layer));
  } else {
    // We are not Idle! Let's dump all registers and crash
    PrintStatusRegisters();
    ZX_ASSERT(false);
  }

  // Make sure we support the input format
  if (!IsSupportedFormat(cfg.format)) {
    DISP_ERROR("Unsupported format: 0x%x\n", cfg.format);
    ZX_ASSERT(false);
  }

  // Setup various OVL CON register <layer specific>
  uint32_t regVal = 0;
  if (cfg.alpha_mode != ALPHA_DISABLE) {
    regVal = Lx_CON_AEN;  // enable alpha blending
    if (!isnan(cfg.alpha_val)) {
      // Apply alpha value
      regVal |= Lx_CON_ALPHA(static_cast<uint8_t>(round(cfg.alpha_val * 255)));
    } else {
      // Enable per-pixel only, therefore set multiplier to 1
      regVal |= Lx_CON_ALPHA(0xFF);
    }
  }

  if (ByteSwapNeeded(cfg.format)) {
    regVal |= Lx_CON_BYTE_SWAP;
  }
  regVal |= Lx_CON_CLRFMT(GetFormat(cfg.format));

  // enable horizontal and veritical flip
  if (cfg.transform == FRAME_TRANSFORM_ROT_180) {
    regVal |= Lx_CON_HFE | Lx_CON_VFE;
  } else if (cfg.transform == FRAME_TRANSFORM_REFLECT_X) {
    regVal |= Lx_CON_HFE;
  } else if (cfg.transform == FRAME_TRANSFORM_REFLECT_Y) {
    regVal |= Lx_CON_VFE;
  }

  ovl_mmio_->Write32(regVal, OVL_Lx_CON(layer));

  // write the height and width of source buffer for this layer
  // Since scaling is not support in OVL, it doesn't matter where we get
  // the height and width from. Picking source height and width
  ovl_mmio_->Write32(cfg.src_frame.height << 16 | cfg.src_frame.width, OVL_Lx_SRC_SIZE(layer));

  // Set destination frame to be display on display
  uint32_t x_pos;
  uint32_t y_pos;
  uint32_t offset;

  if (cfg.transform == FRAME_TRANSFORM_ROT_180) {
    // flipping in both x and y
    x_pos = width_ - cfg.dest_frame.width - cfg.dest_frame.x_pos;
    y_pos = height_ - cfg.dest_frame.height - cfg.dest_frame.y_pos;
    offset = (cfg.dest_frame.width + cfg.src_frame.x_pos) * GetBytesPerPixel(cfg.format) +
             (cfg.dest_frame.height + cfg.src_frame.y_pos - 1) * cfg.pitch - 1;
  } else if (cfg.transform == FRAME_TRANSFORM_REFLECT_X) {
    x_pos = width_ - cfg.dest_frame.width - cfg.dest_frame.x_pos;
    y_pos = cfg.dest_frame.y_pos;
    offset = (cfg.dest_frame.width + cfg.src_frame.x_pos) * GetBytesPerPixel(cfg.format) +
             cfg.src_frame.y_pos * cfg.pitch - 1;
  } else if (cfg.transform == FRAME_TRANSFORM_REFLECT_Y) {
    x_pos = cfg.dest_frame.x_pos;
    y_pos = height_ - cfg.dest_frame.height - cfg.dest_frame.y_pos;
    offset = cfg.src_frame.x_pos * GetBytesPerPixel(cfg.format) +
             (cfg.dest_frame.height + cfg.src_frame.y_pos - 1) * cfg.pitch;
  } else {
    // No flipping/rotation
    x_pos = cfg.dest_frame.x_pos;
    y_pos = cfg.dest_frame.y_pos;
    offset = cfg.src_frame.x_pos * GetBytesPerPixel(cfg.format) + cfg.src_frame.y_pos * cfg.pitch;
  }
  ovl_mmio_->Write32((y_pos << 16 | x_pos), OVL_Lx_OFFSET(layer));

  // set the physical address of the buffer for this layer based on source offset
  ovl_mmio_->Write32(static_cast<uint32_t>(cfg.paddr) + offset, OVL_Lx_ADDR(layer));

  // setup Lx_PITCH_PITCH register
  regVal = 0;
  regVal |= Lx_PITCH_PITCH(cfg.pitch);
  ovl_mmio_->Write32(regVal, OVL_Lx_PITCH(layer));

  // Setup magical register with undocumented magic value
  ovl_mmio_->Write32(0x6070, OVL_RDMAx_MEM_GMC_SETTING(layer));
  active_layers_ |= static_cast<uint8_t>((1 << layer));
  layer_handle_[layer] = cfg.handle;
  return ZX_OK;
}

void Ovl::PrintRegisters() {
  zxlogf(INFO, "Dumping OVL Registers:\n");
  zxlogf(INFO, "######################\n\n");
  zxlogf(INFO, "OVL_STA = 0x%x\n", ovl_mmio_->Read32(OVL_STA));
  zxlogf(INFO, "OVL_INTEN = 0x%x\n", ovl_mmio_->Read32(OVL_INTEN));
  zxlogf(INFO, "OVL_INTSTA = 0x%x\n", ovl_mmio_->Read32(OVL_INTSTA));
  zxlogf(INFO, "OVL_EN = 0x%x\n", ovl_mmio_->Read32(OVL_EN));
  zxlogf(INFO, "OVL_TRIG = 0x%x\n", ovl_mmio_->Read32(OVL_TRIG));
  zxlogf(INFO, "OVL_RST = 0x%x\n", ovl_mmio_->Read32(OVL_RST));
  zxlogf(INFO, "OVL_ROI_SIZE = 0x%x\n", ovl_mmio_->Read32(OVL_ROI_SIZE));
  zxlogf(INFO, "OVL_DATAPATH_CON = 0x%x\n", ovl_mmio_->Read32(OVL_DATAPATH_CON));
  zxlogf(INFO, "OVL_ROI_BGCLR = 0x%x\n", ovl_mmio_->Read32(OVL_ROI_BGCLR));
  zxlogf(INFO, "OVL_SRC_CON = 0x%x\n", ovl_mmio_->Read32(OVL_SRC_CON));
  zxlogf(INFO, "OVL_Lx_CON0123 = 0x%x, 0x%x, 0x%x, 0x%x\n", ovl_mmio_->Read32(OVL_Lx_CON(0)),
         ovl_mmio_->Read32(OVL_Lx_CON(1)), ovl_mmio_->Read32(OVL_Lx_CON(2)),
         ovl_mmio_->Read32(OVL_Lx_CON(3)));
  zxlogf(INFO, "OVL_Lx_SRCKEY0123 = 0x%x, 0x%x, 0x%x, 0x%x\n", ovl_mmio_->Read32(OVL_Lx_SRCKEY(0)),
         ovl_mmio_->Read32(OVL_Lx_SRCKEY(1)), ovl_mmio_->Read32(OVL_Lx_SRCKEY(2)),
         ovl_mmio_->Read32(OVL_Lx_SRCKEY(3)));
  zxlogf(INFO, "OVL_Lx_SRC_SIZE0123 = 0x%x, 0x%x, 0x%x, 0x%x\n",
         ovl_mmio_->Read32(OVL_Lx_SRC_SIZE(0)), ovl_mmio_->Read32(OVL_Lx_SRC_SIZE(1)),
         ovl_mmio_->Read32(OVL_Lx_SRC_SIZE(2)), ovl_mmio_->Read32(OVL_Lx_SRC_SIZE(3)));
  zxlogf(INFO, "OVL_Lx_OFFSET0123 = 0x%x, 0x%x, 0x%x, 0x%x\n", ovl_mmio_->Read32(OVL_Lx_OFFSET(0)),
         ovl_mmio_->Read32(OVL_Lx_OFFSET(1)), ovl_mmio_->Read32(OVL_Lx_OFFSET(2)),
         ovl_mmio_->Read32(OVL_Lx_OFFSET(3)));
  zxlogf(INFO, "OVL_Lx_ADDR0123 = 0x%x, 0x%x, 0x%x, 0x%x\n", ovl_mmio_->Read32(OVL_Lx_ADDR(0)),
         ovl_mmio_->Read32(OVL_Lx_ADDR(1)), ovl_mmio_->Read32(OVL_Lx_ADDR(2)),
         ovl_mmio_->Read32(OVL_Lx_ADDR(3)));
  zxlogf(INFO, "OVL_Lx_PITCH0123 = 0x%x, 0x%x, 0x%x, 0x%x\n", ovl_mmio_->Read32(OVL_Lx_PITCH(0)),
         ovl_mmio_->Read32(OVL_Lx_PITCH(1)), ovl_mmio_->Read32(OVL_Lx_PITCH(2)),
         ovl_mmio_->Read32(OVL_Lx_PITCH(3)));
  zxlogf(INFO, "OVL_Lx_TILE0123 = 0x%x, 0x%x, 0x%x, 0x%x\n", ovl_mmio_->Read32(OVL_Lx_TILE(0)),
         ovl_mmio_->Read32(OVL_Lx_TILE(1)), ovl_mmio_->Read32(OVL_Lx_TILE(2)),
         ovl_mmio_->Read32(OVL_Lx_TILE(3)));
  zxlogf(INFO, "OVL_RDMAx_CTRL0123 = 0x%x, 0x%x, 0x%x, 0x%x\n",
         ovl_mmio_->Read32(OVL_RDMAx_CTRL(0)), ovl_mmio_->Read32(OVL_RDMAx_CTRL(1)),
         ovl_mmio_->Read32(OVL_RDMAx_CTRL(2)), ovl_mmio_->Read32(OVL_RDMAx_CTRL(3)));
  zxlogf(INFO, "OVL_RDMAx_MEM_GMC_SETTING0123 = 0x%x, 0x%x, 0x%x, 0x%x\n",
         ovl_mmio_->Read32(OVL_RDMAx_MEM_GMC_SETTING(0)),
         ovl_mmio_->Read32(OVL_RDMAx_MEM_GMC_SETTING(1)),
         ovl_mmio_->Read32(OVL_RDMAx_MEM_GMC_SETTING(2)),
         ovl_mmio_->Read32(OVL_RDMAx_MEM_GMC_SETTING(3)));
  zxlogf(INFO, "OVL_RDMAx_MEM_SLOW_CON0123 = 0x%x, 0x%x, 0x%x, 0x%x\n",
         ovl_mmio_->Read32(OVL_RDMAx_MEM_SLOW_CON(0)), ovl_mmio_->Read32(OVL_RDMAx_MEM_SLOW_CON(1)),
         ovl_mmio_->Read32(OVL_RDMAx_MEM_SLOW_CON(2)),
         ovl_mmio_->Read32(OVL_RDMAx_MEM_SLOW_CON(3)));
  zxlogf(INFO, "OVL_RDMAx_FIFO_CTRL0123 = 0x%x, 0x%x, 0x%x, 0x%x\n",
         ovl_mmio_->Read32(OVL_RDMAx_FIFO_CTRL(0)), ovl_mmio_->Read32(OVL_RDMAx_FIFO_CTRL(1)),
         ovl_mmio_->Read32(OVL_RDMAx_FIFO_CTRL(2)), ovl_mmio_->Read32(OVL_RDMAx_FIFO_CTRL(3)));
  zxlogf(INFO, "OVL_Lx_Y2R_PARA_R00123 = 0x%x, 0x%x, 0x%x, 0x%x\n",
         ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_R0(0)), ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_R0(1)),
         ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_R0(2)), ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_R0(3)));
  zxlogf(INFO, "OVL_Lx_Y2R_PARA_R10123 = 0x%x, 0x%x, 0x%x, 0x%x\n",
         ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_R1(0)), ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_R1(1)),
         ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_R1(2)), ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_R1(3)));
  zxlogf(INFO, "OVL_Lx_Y2R_PARA_G00123 = 0x%x, 0x%x, 0x%x, 0x%x\n",
         ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_G0(0)), ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_G0(1)),
         ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_G0(2)), ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_G0(3)));
  zxlogf(INFO, "OVL_Lx_Y2R_PARA_G10123 = 0x%x, 0x%x, 0x%x, 0x%x\n",
         ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_G1(0)), ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_G1(1)),
         ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_G1(2)), ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_G1(3)));
  zxlogf(INFO, "OVL_Lx_Y2R_PARA_B00123 = 0x%x, 0x%x, 0x%x, 0x%x\n",
         ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_B0(0)), ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_B0(1)),
         ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_B0(2)), ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_B0(3)));
  zxlogf(INFO, "OVL_Lx_Y2R_PARA_B10123 = 0x%x, 0x%x, 0x%x, 0x%x\n",
         ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_B1(0)), ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_B1(1)),
         ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_B1(2)), ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_B1(3)));
  zxlogf(
      INFO, "OVL_Lx_Y2R_PARA_YUV_A_00123 = 0x%x, 0x%x, 0x%x, 0x%x\n",
      ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_YUV_A_0(0)), ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_YUV_A_0(1)),
      ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_YUV_A_0(2)), ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_YUV_A_0(3)));
  zxlogf(
      INFO, "OVL_Lx_Y2R_PARA_YUV_A_10123 = 0x%x, 0x%x, 0x%x, 0x%x\n",
      ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_YUV_A_1(0)), ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_YUV_A_1(1)),
      ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_YUV_A_1(2)), ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_YUV_A_1(3)));
  zxlogf(
      INFO, "OVL_Lx_Y2R_PARA_RGB_A_00123 = 0x%x, 0x%x, 0x%x, 0x%x\n",
      ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_RGB_A_0(0)), ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_RGB_A_0(1)),
      ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_RGB_A_0(2)), ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_RGB_A_0(3)));
  zxlogf(
      INFO, "OVL_Lx_Y2R_PARA_RGB_A_10123 = 0x%x, 0x%x, 0x%x, 0x%x\n",
      ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_RGB_A_1(0)), ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_RGB_A_1(1)),
      ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_RGB_A_1(2)), ovl_mmio_->Read32(OVL_Lx_Y2R_PARA_RGB_A_1(3)));
  zxlogf(INFO, "OVL_DEBUG_MON_SEL = 0x%x\n", ovl_mmio_->Read32(OVL_DEBUG_MON_SEL));
  zxlogf(INFO, "OVL_RDMAx_MEM_GMC_S20123 = 0x%x, 0x%x, 0x%x, 0x%x\n",
         ovl_mmio_->Read32(OVL_RDMAx_MEM_GMC_S2(0)), ovl_mmio_->Read32(OVL_RDMAx_MEM_GMC_S2(1)),
         ovl_mmio_->Read32(OVL_RDMAx_MEM_GMC_S2(2)), ovl_mmio_->Read32(OVL_RDMAx_MEM_GMC_S2(3)));
  zxlogf(INFO, "OVL_DUMMY_REG = 0x%x\n", ovl_mmio_->Read32(OVL_DUMMY_REG));
  zxlogf(INFO, "OVL_SMI_DBG = 0x%x\n", ovl_mmio_->Read32(OVL_SMI_DBG));
  zxlogf(INFO, "OVL_GREQ_LAYER_CNT = 0x%x\n", ovl_mmio_->Read32(OVL_GREQ_LAYER_CNT));
  zxlogf(INFO, "OVL_FLOW_CTRL_DBG = 0x%x\n", ovl_mmio_->Read32(OVL_FLOW_CTRL_DBG));
  zxlogf(INFO, "OVL_ADDCON_DBG = 0x%x\n", ovl_mmio_->Read32(OVL_ADDCON_DBG));
  zxlogf(INFO, "OVL_RDMAx_DBG0123 = 0x%x, 0x%x, 0x%x, 0x%x\n", ovl_mmio_->Read32(OVL_RDMAx_DBG(0)),
         ovl_mmio_->Read32(OVL_RDMAx_DBG(1)), ovl_mmio_->Read32(OVL_RDMAx_DBG(2)),
         ovl_mmio_->Read32(OVL_RDMAx_DBG(3)));
  zxlogf(INFO, "OVL_Lx_CLR0123 = 0x%x, 0x%x, 0x%x, 0x%x\n", ovl_mmio_->Read32(OVL_Lx_CLR(0)),
         ovl_mmio_->Read32(OVL_Lx_CLR(1)), ovl_mmio_->Read32(OVL_Lx_CLR(2)),
         ovl_mmio_->Read32(OVL_Lx_CLR(3)));
  zxlogf(INFO, "######################\n\n");
}

}  // namespace mt8167s_display
