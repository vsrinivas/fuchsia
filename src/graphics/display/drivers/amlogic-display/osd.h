// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_OSD_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_OSD_H_

#include <lib/device-protocol/platform-device.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/bti.h>
#include <lib/zx/interrupt.h>
#include <threads.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <cstdint>
#include <optional>

#include <ddk/protocol/platform/device.h>
#include <ddktl/protocol/display/controller.h>
#include <fbl/mutex.h>

#include "common.h"

namespace amlogic_display {

struct RdmaTable {
  uint32_t reg;
  uint32_t val;
};

/*
 * This is the RDMA table index. Each index points to a specific VPU register.
 * RDMA engine will be programmed to update all those registers at vsync time.
 * Since all the fields will be updated at vsync time, we need to make sure all
 * the fields are updated with a valid value when FlipOnVsync is called.
 */
enum {
  IDX_BLK0_CFG_W0,
  IDX_CTRL_STAT,
  IDX_CTRL_STAT2,
  IDX_MATRIX_COEF00_01,
  IDX_MATRIX_COEF02_10,
  IDX_MATRIX_COEF11_12,
  IDX_MATRIX_COEF20_21,
  IDX_MATRIX_COEF22,
  IDX_MATRIX_OFFSET0_1,
  IDX_MATRIX_OFFSET2,
  IDX_MATRIX_PRE_OFFSET0_1,
  IDX_MATRIX_PRE_OFFSET2,
  IDX_MATRIX_EN_CTRL,
  IDX_GAMMA_EN,
  IDX_BLK2_CFG_W4,
  IDX_MALI_UNPACK_CTRL,
  IDX_PATH_MISC_CTRL,
  IDX_AFBC_HEAD_BUF_ADDR_LOW,
  IDX_AFBC_HEAD_BUF_ADDR_HIGH,
  IDX_AFBC_SURFACE_CFG,
  IDX_MAX,
};

enum class GammaChannel {
  kRed,
  kGreen,
  kBlue,
};

struct RdmaChannelContainer {
  zx_paddr_t phys_offset;  // offset into physical address
  uint8_t* virt_offset;    // offset into virtual address (vmar buf)
  bool active;             // indicated whether channel is being used or not
};

constexpr uint32_t kRdmaTableMaxSize = IDX_MAX;

// RDMA Channels used by OSD. Three channels should be more than enough
constexpr uint8_t kMaxRdmaChannels = 3;
constexpr uint8_t kMaxRetries = 100;
// spread channels 512B apart (make sure it's greater than a cache line size)
constexpr size_t kChannelBaseOffset = 512;

// RDMA Channel 7 will be dedicated to AFBC Trigger
static constexpr uint8_t kAfbcRdmaChannel = 7;

class Osd {
 public:
  Osd(uint32_t fb_width, uint32_t fb_height, uint32_t display_width, uint32_t display_height,
      inspect::Node* parent_node)
      : fb_width_(fb_width),
        fb_height_(fb_height),
        display_width_(display_width),
        display_height_(display_height),
        inspect_node_(parent_node->CreateChild("osd")),
        rdma_allocation_failures_(inspect_node_.CreateUint("rdma_allocation_failures", 0)) {}

  zx_status_t Init(zx_device_t* parent);
  void HwInit();
  void Disable();
  void Enable();

  // This function will apply configuration when VSYNC interrupt occurs using RDMA
  void FlipOnVsync(uint8_t idx, const display_config_t* config);
  void Dump();
  void Release();

  // This function converts a float into Signed fixed point 3.10 format
  // [12][11:10][9:0] = [sign][integer][fraction]
  static uint32_t FloatToFixed3_10(float f);
  // This function converts a float into Signed fixed point 2.10 format
  // [11][10][9:0] = [sign][integer][fraction]
  static uint32_t FloatToFixed2_10(float f);
  static constexpr size_t kGammaTableSize = 256;

  void SetMinimumRgb(uint8_t minimum_rgb);

 private:
  void DefaultSetup();
  // this function sets up scaling based on framebuffer and actual display
  // dimensions. The scaling IP and registers and undocumented.
  void EnableScaling(bool enable);
  void StopRdma();
  zx_status_t SetupRdma();
  void ResetRdmaTable();
  void SetRdmaTableValue(uint32_t channel, uint32_t idx, uint32_t val);
  void FlushRdmaTable(uint32_t channel);
  int GetNextAvailableRdmaChannel();
  void SetAfbcRdmaTableValue(uint32_t val) const;
  void FlushAfbcRdmaTable() const;
  int RdmaThread();
  void EnableGamma();
  void DisableGamma();
  zx_status_t ConfigAfbc();
  zx_status_t SetGamma(GammaChannel channel, const float* data);
  zx_status_t WaitForGammaAddressReady();
  zx_status_t WaitForGammaWriteReady();
  std::optional<ddk::MmioBuffer> vpu_mmio_;
  pdev_protocol_t pdev_ = {nullptr, nullptr};
  zx::bti bti_;

  // RDMA IRQ handle and thread
  zx::interrupt rdma_irq_;
  thrd_t rdma_thread_;

  fbl::Mutex rdma_lock_;

  // use a single vmo for all channels
  zx::vmo rdma_vmo_;
  zx_handle_t rdma_pmt_;
  zx_paddr_t rdma_phys_;
  uint8_t* rdma_vbuf_;

  // Container that holds channel specific properties
  RdmaChannelContainer rdma_chnl_container_[kMaxRdmaChannels];

  // Container that holds AFBC specific trigger register
  RdmaChannelContainer afbc_rdma_chnl_container_;
  zx::vmo afbc_rdma_vmo_;
  zx_handle_t afbc_rdma_pmt_;
  zx_paddr_t afbc_rdma_phys_;
  uint8_t* afbc_rdma_vbuf_;

  // Framebuffer dimension
  uint32_t fb_width_;
  uint32_t fb_height_;
  // Actual display dimension
  uint32_t display_width_;
  uint32_t display_height_;

  // This flag is set when the driver enables gamma correction.
  // If this flag is not set, we should not disable gamma in the absence
  // of a gamma table since that might have been provided by earlier boot stages.
  bool osd_enabled_gamma_ = false;

  bool initialized_ = false;

  inspect::Node inspect_node_;
  inspect::UintProperty rdma_allocation_failures_;
};

}  // namespace amlogic_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_OSD_H_
