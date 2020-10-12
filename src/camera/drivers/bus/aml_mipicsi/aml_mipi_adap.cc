// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <math.h>
#include <stdint.h>
#include <zircon/types.h>

#include <ddk/debug.h>

#include "src/camera/drivers/bus/aml_mipicsi/aml_mipi.h"
#include "src/camera/drivers/bus/aml_mipicsi/aml_mipi_regs.h"

// NOTE: A lot of magic numbers, they come from vendor
//       source code.

namespace camera {

namespace {

constexpr uint32_t kFrontEnd0Size = 0x400;
constexpr uint32_t kReaderSize = 0x100;
constexpr uint32_t kPixelSize = 0x100;
constexpr uint32_t kAlignSize = 0x200;
constexpr uint32_t kSize1Mb = 0x100000;
constexpr uint32_t kDdrModeSize = 48 * kSize1Mb;

}  // namespace

uint32_t AmlMipiDevice::AdapGetDepth(const mipi_adap_info_t* info) {
  uint32_t depth = 0;
  switch (info->format) {
    case MIPI_IMAGE_FORMAT_AM_RAW6:
      depth = 6;
      break;
    case MIPI_IMAGE_FORMAT_AM_RAW7:
      depth = 7;
      break;
    case MIPI_IMAGE_FORMAT_AM_RAW8:
      depth = 8;
      break;
    case MIPI_IMAGE_FORMAT_AM_RAW10:
      depth = 10;
      break;
    case MIPI_IMAGE_FORMAT_AM_RAW12:
      depth = 12;
      break;
    case MIPI_IMAGE_FORMAT_AM_RAW14:
      depth = 14;
      break;
    default:
      zxlogf(ERROR, "%s, unsupported data format.", __func__);
      break;
  }
  return depth;
}

zx_status_t AmlMipiDevice::InitBuffer(const mipi_adap_info_t* /*info*/, size_t size) {
  // Create a VMO for the ring buffer.
  zx_status_t status =
      zx_vmo_create_contiguous(bti_.get(), size, 0, ring_buffer_vmo_.reset_and_get_address());
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to allocate ring buffer vmo - %d", __func__, status);
    return status;
  }
  // Pin the ring buffer.
  status = pinned_ring_buffer_.Pin(ring_buffer_vmo_, bti_, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to pin ring buffer vmo - %d", __func__, status);
    return status;
  }
  // Validate the pinned buffer.
  if (pinned_ring_buffer_.region_count() != 1) {
    zxlogf(ERROR, "%s buffer is not contiguous", __func__);
    return ZX_ERR_NO_MEMORY;
  }
  return ZX_OK;
}

/*
 *======================== ADAPTER FRONTEND INTERFACE========================
 * Frontend is the HW block which configures if the data goes
 * to the memory or takes the direct path.
 * Register information 8.1.2 (page 312)
 */

zx_status_t AmlMipiDevice::AdapFrontendInit(const mipi_adap_info_t* info) {
  // TODO(braval):    Add support for DOL_MODE
  auto frontend_reg = mipi_adap_mmio_->View(FRONTEND_BASE, kFrontEnd0Size);

  // release from reset
  frontend_reg.Write32(0x0, CSI2_CLK_RESET);
  // enable frontend module clock and disable auto clock gating
  frontend_reg.Write32(0x6, CSI2_CLK_RESET);

  if (info->mode == MIPI_MODES_DIR_MODE) {
    if (info->path == MIPI_PATH_PATH0) {
      // bit[0] 1:enable virtual channel 0
      frontend_reg.Write32(0x001f0001, CSI2_GEN_CTRL0);
    }
  } else if (info->mode == MIPI_MODES_DDR_MODE) {
    if (info->path == MIPI_PATH_PATH0) {
      frontend_reg.Write32(0x001f0011, CSI2_GEN_CTRL0);
    }
  } else {
    zxlogf(ERROR, "%s, unsupported mode.", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  // applicable only to Raw data, direct MEM path
  frontend_reg.Write32(0xffff0000, CSI2_X_START_END_MEM);
  frontend_reg.Write32(0xffff0000, CSI2_Y_START_END_MEM);

  if (info->mode == MIPI_MODES_DDR_MODE) {
    // config ddr_buf[0] address
    frontend_reg.ModifyBits32(static_cast<uint32_t>(pinned_ring_buffer_.region(0).phys_addr), 0, 32,
                              CSI2_DDR_START_PIX);
  } else if (info->mode == MIPI_MODES_DOL_MODE) {
    // TODO(braval):    Add support for DOL_MODE.
  }

  // set frame size
  if (info->mode == MIPI_MODES_DOL_MODE) {
    zxlogf(ERROR, "%s, unsupported mode.", __func__);
  } else {
    frontend_reg.Write32(0x00000780, CSI2_DDR_STRIDE_PIX);
  }

  // enable vs_rise_isp interrupt & enable ddr_wdone interrupt
  frontend_reg.Write32(0x5, CSI2_INTERRUPT_CTRL_STAT);
  return ZX_OK;
}

void AmlMipiDevice::AdapFrontEndStart(const mipi_adap_info_t* info) {
  uint32_t width = info->resolution.width;
  uint32_t depth, val;
  depth = AdapGetDepth(info);
  if (!depth) {
    zxlogf(ERROR, "%s, unsupported format ", __func__);
  }
  auto frontend_reg = mipi_adap_mmio_->View(FRONTEND_BASE, kFrontEnd0Size);

  frontend_reg.SetBits32(1 << 0, CSI2_GEN_CTRL0);
  // This register information is missing in the datasheet.
  // Best assumption is that theunit of values programmed in the register is
  // 128bit.
  val = static_cast<uint32_t>(ceil((width * depth) / static_cast<double>((8 * 16))));
  frontend_reg.ModifyBits32(val, 4, 28, CSI2_DDR_STRIDE_PIX);
}

/*
 *======================== ADAPTER READER INTERFACE==========================
 * Reader is the HW block which is configured to read the data from
 * the memory or direct oath. It also configures for multi-exposures.
 * Register information 8.1.2 (page 322)
 */

zx_status_t AmlMipiDevice::AdapReaderInit(const mipi_adap_info_t* info) {
  // TODO(braval):    Add support for DOL_MODE
  auto reader_reg = mipi_adap_mmio_->View(RD_BASE, kReaderSize);

  if (info->mode == MIPI_MODES_DIR_MODE) {
    reader_reg.Write32(0x02d00078, MIPI_ADAPT_DDR_RD0_CNTL1);
    reader_reg.Write32(0xb5000005, MIPI_ADAPT_DDR_RD0_CNTL0);
  } else if (info->mode == MIPI_MODES_DDR_MODE) {
    reader_reg.Write32(0x02d00078, MIPI_ADAPT_DDR_RD0_CNTL1);
    // ddr mode config frame address
    reader_reg.ModifyBits32(static_cast<uint32_t>(pinned_ring_buffer_.region(0).phys_addr), 0, 32,
                            MIPI_ADAPT_DDR_RD0_CNTL2);
    reader_reg.Write32(0x70000001, MIPI_ADAPT_DDR_RD0_CNTL0);
  } else {
    zxlogf(ERROR, "%s, unsupported mode.", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

void AmlMipiDevice::AdapReaderStart(const mipi_adap_info_t* info) {
  uint32_t height = info->resolution.height;
  uint32_t width = info->resolution.width;
  uint32_t val, depth;
  depth = AdapGetDepth(info);
  if (!depth) {
    zxlogf(ERROR, "%s, unsupported format ", __func__);
  }

  val = static_cast<uint32_t>(ceil((width * depth) / (8 * 16)));

  auto reader_reg = mipi_adap_mmio_->View(RD_BASE, kReaderSize);

  reader_reg.ModifyBits32(height, 16, 13, MIPI_ADAPT_DDR_RD0_CNTL1);
  reader_reg.ModifyBits32(val, 0, 10, MIPI_ADAPT_DDR_RD0_CNTL1);
  // TODO(braval):    Add support for DOL_MODE

  reader_reg.SetBits32(1 << 0, MIPI_ADAPT_DDR_RD0_CNTL0);
}

/*
 *======================== ADAPTER PIXEL INTERFACE===========================
 * Setting the width to 1280 and default mode to RAW12
 * Register information 8.1.2 (page 330)
 */

zx_status_t AmlMipiDevice::AdapPixelInit(const mipi_adap_info_t* info) {
  // TODO(braval):    Add support for  DOL_MODE
  auto pixel_reg = mipi_adap_mmio_->View(PIXEL_BASE, kPixelSize);

  if (info->mode == MIPI_MODES_DIR_MODE) {
    // default width 1280
    pixel_reg.Write32(0x8000a500, MIPI_ADAPT_PIXEL0_CNTL0);
    pixel_reg.Write32(0x80000808, MIPI_ADAPT_PIXEL0_CNTL1);
  } else if (info->mode == MIPI_MODES_DDR_MODE) {
    pixel_reg.Write32(0x0000a500, MIPI_ADAPT_PIXEL0_CNTL0);
    pixel_reg.Write32(0x80000008, MIPI_ADAPT_PIXEL0_CNTL1);
  } else {
    zxlogf(ERROR, "%s, unsupported mode.", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

void AmlMipiDevice::AdapPixelStart(const mipi_adap_info_t* info) {
  auto pixel_reg = mipi_adap_mmio_->View(PIXEL_BASE, kPixelSize);

  pixel_reg.ModifyBits32(info->format, 13, 3, MIPI_ADAPT_PIXEL0_CNTL0);
  pixel_reg.ModifyBits32(info->resolution.width, 0, 13, MIPI_ADAPT_PIXEL0_CNTL0);

  // TODO(braval):    Add support for DOL_MODE
  pixel_reg.SetBits32(1 << 31, MIPI_ADAPT_PIXEL0_CNTL1);
}

/*
 *======================== ADAPTER ALIGNMENT INTERFACE=======================
 * Register information 8.1.2 (page 333)
 */

zx_status_t AmlMipiDevice::AdapAlignInit(const mipi_adap_info_t* info) {
  // TODO(braval):    Add support for DDR_MODE & DOL_MODE
  auto align_reg = mipi_adap_mmio_->View(ALIGN_BASE, kAlignSize);

  if (info->mode == MIPI_MODES_DOL_MODE) {
    zxlogf(ERROR, "%s, unsupported mode.", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }
  // default width 1280, height 720
  align_reg.Write32(0x02f80528,
                    MIPI_ADAPT_ALIG_CNTL0);              // associate width and height
  align_reg.Write32(0x05000000, MIPI_ADAPT_ALIG_CNTL1);  // associate width
  align_reg.Write32(0x02d00000, MIPI_ADAPT_ALIG_CNTL2);  // associate height

  if (info->mode == MIPI_MODES_DIR_MODE) {
    align_reg.Write32(0x00fff011, MIPI_ADAPT_ALIG_CNTL6);
    align_reg.Write32(0xc350c000, MIPI_ADAPT_ALIG_CNTL7);
    align_reg.Write32(0x85231020, MIPI_ADAPT_ALIG_CNTL8);
  } else if (info->mode == MIPI_MODES_DDR_MODE) {
    align_reg.Write32(0x00fff001, MIPI_ADAPT_ALIG_CNTL6);
    align_reg.Write32(0x0, MIPI_ADAPT_ALIG_CNTL7);
    align_reg.Write32(0x80000020, MIPI_ADAPT_ALIG_CNTL8);
  } else {
    zxlogf(ERROR, "%s, unsupported mode.", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  align_reg.Write32(0x00082000, MIPI_ADAPT_IRQ_MASK0);
  return ZX_OK;
}

void AmlMipiDevice::AdapAlignStart(const mipi_adap_info_t* info) {
  auto align_reg = mipi_adap_mmio_->View(ALIGN_BASE, kAlignSize);

  uint32_t width, height, alig_width, alig_height, val;
  width = info->resolution.width;
  height = info->resolution.height;
  alig_width = width + 40;    // hblank > 32 cycles
  alig_height = height + 60;  // vblank > 48 lines
  val = width + 35;           // width < val < alig_width
  align_reg.ModifyBits32(alig_width, 0, 13, MIPI_ADAPT_ALIG_CNTL0);
  align_reg.ModifyBits32(alig_height, 16, 13, MIPI_ADAPT_ALIG_CNTL0);
  align_reg.ModifyBits32(width, 16, 13, MIPI_ADAPT_ALIG_CNTL1);
  align_reg.ModifyBits32(height, 16, 13, MIPI_ADAPT_ALIG_CNTL2);
  align_reg.ModifyBits32(val, 16, 13, MIPI_ADAPT_ALIG_CNTL8);
  align_reg.ModifyBits32(1, 31, 1, MIPI_ADAPT_ALIG_CNTL8);
}

/*
 *======================== ADAPTER INTERFACE==========================
 */

int AmlMipiDevice::AdapterIrqHandler() {
  zxlogf(TRACE, "%s start", __func__);
  zx_status_t status = ZX_OK;

  while (running_.load()) {
    status = adap_irq_.wait(nullptr);
    if (status != ZX_OK) {
      return status;
    }
  }
  // TODO(braval) : Add ISR implementation here.
  return status;
}

zx_status_t AmlMipiDevice::MipiAdapInit(const mipi_adap_info_t* info) {
  // TODO(braval):    Add support for DOL_MODE

  if (info->mode == MIPI_MODES_DDR_MODE) {
    zx_status_t status = InitBuffer(info, kDdrModeSize);
    if (status != ZX_OK) {
      return status;
    }
    // TODO(braval): Setup ring buffers phys. address.

    // Start thermal notification thread.
    auto start_thread = [](void* arg) -> int {
      return static_cast<AmlMipiDevice*>(arg)->AdapterIrqHandler();
    };

    running_.store(true);

    int rc = thrd_create_with_name(&irq_thread_, start_thread, this, "adapter_irq_thread");
    if (rc != thrd_success) {
      return ZX_ERR_INTERNAL;
    }
  }

  // Reset the Frontend
  auto frontend_reg = mipi_adap_mmio_->View(FRONTEND_BASE, kFrontEnd0Size);
  frontend_reg.Write32(1, CSI2_CLK_RESET);
  frontend_reg.Write32(0, CSI2_CLK_RESET);

  // default setting : 720p & RAW12
  zx_status_t status = AdapFrontendInit(info);
  if (status != ZX_OK) {
    return status;
  }

  status = AdapReaderInit(info);
  if (status != ZX_OK) {
    return status;
  }

  status = AdapPixelInit(info);
  if (status != ZX_OK) {
    return status;
  }

  status = AdapAlignInit(info);
  if (status != ZX_OK) {
    return status;
  }

  return status;
}

void AmlMipiDevice::MipiAdapStart(const mipi_adap_info_t* info) {
  AdapAlignStart(info);
  AdapPixelStart(info);
  AdapReaderStart(info);
  AdapFrontEndStart(info);
}

void AmlMipiDevice::MipiAdapReset() {
  auto frontend_reg = mipi_adap_mmio_->View(FRONTEND_BASE, kFrontEnd0Size);
  auto align_reg = mipi_adap_mmio_->View(ALIGN_BASE, kAlignSize);

  frontend_reg.Write32(0x0, CSI2_CLK_RESET);
  frontend_reg.Write32(0x6, CSI2_CLK_RESET);
  frontend_reg.Write32(0x001f0000, CSI2_GEN_CTRL0);
  align_reg.Write32(0xf0000000, MIPI_OTHER_CNTL0);
  align_reg.Write32(0x00000000, MIPI_OTHER_CNTL0);
}

}  // namespace camera
