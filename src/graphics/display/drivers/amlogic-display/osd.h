// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_OSD_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_OSD_H_
#include <fuchsia/hardware/display/controller/cpp/banjo.h>
#include <lib/device-protocol/pdev.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/bti.h>
#include <lib/zx/handle.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/pmt.h>
#include <threads.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <cstdint>
#include <optional>

#include <fbl/auto_lock.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>

#include "src/graphics/display/drivers/amlogic-display/common.h"

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
  // This should be the last element to make sure the entire config
  // was written
  IDX_RDMA_CFG_STAMP_HIGH,
  IDX_RDMA_CFG_STAMP_LOW,
  IDX_MAX,
};
static_assert(IDX_RDMA_CFG_STAMP_HIGH == (IDX_MAX - 2), "Invalid RDMA Index Table");
static_assert(IDX_RDMA_CFG_STAMP_LOW == (IDX_MAX - 1), "Invalid RDMA Index Table");

// Table size for non-AFBC RDMA
constexpr size_t kTableSize = (IDX_MAX * sizeof(RdmaTable));
// Single element table for AFBC RDMA
constexpr size_t kAfbcTableSize = sizeof(RdmaTable);

// Non-AFBC RDMA Region size
constexpr size_t kRdmaRegionSize = ZX_PAGE_SIZE;

// AFBC RDMA Region Size
constexpr size_t kAfbcRdmaRegionSize = ZX_PAGE_SIZE;

// Arbitrarily limit table size to maximum 16
constexpr uint32_t kNumberOfTables = std::min(16ul, (kRdmaRegionSize / (kTableSize)));
// We should have space for at least 3 tables. If RDMA table has grown too large and cannot
// fit more than 3 tables within a PAGE_SIZE, we need to either:
// - Re-evaluate why RDMA table has grown so large
// - Create a larger RDMA table allocation
static_assert(kNumberOfTables >= 3, "RDMA table is too large");

// This value indicates an available entry into the RDMA table
constexpr uint64_t kRdmaTableReady = UINT64_MAX - 1;
// An entry is marked as unavailable temporarily when there are unapplied configs. This will ensure
// new configs are added to end of table since RDMA requires physical contiguous entries
constexpr uint64_t kRdmaTableUnavailable = UINT64_MAX;

// RDMA Channel 1 is used to track the application of image layers from queued configs to the
// display hardware
constexpr uint8_t kRdmaChannel = 1;
// RDMA Channel 7 will be dedicated to AFBC Trigger
constexpr uint8_t kAfbcRdmaChannel = 7;

enum class GammaChannel {
  kRed,
  kGreen,
  kBlue,
};

struct RdmaChannelContainer {
  zx_paddr_t phys_offset;  // offset into physical address
  uint8_t* virt_offset;    // offset into virtual address (vmar buf)
};

/*
 * RDMA Operation Design (non-AFBC):
 * Allocate kRdmaRegionSize of physical contiguous memory. This region will include
 * kNumberOfTables of RDMA Tables.
 * RDMA Tables will get populated with <reg><val> pairs. The last element will be a unique
 * stamp for a given configuration. The stamp is used to verify how far the RDMA channel was able
 * to write at vsync time.
 *  _______________
 * |<reg><val>     |
 * |<reg><val>     |
 * |...            |
 * |<Config Stamp> |
 * |_______________|
 * |<reg><val>     |
 * |<reg><val>     |
 * |...            |
 * |<Config Stamp> |
 * |_______________|
 * .
 * .
 * .
 * |<reg><val>     |
 * |<reg><val>     |
 * |...            |
 * |<Config Stamp> |
 * |_______________|
 * |<reg><val>     |
 * |<reg><val>     |
 * |...            |
 * |<Config Stamp> |
 * |_______________|
 *
 * The physical and virtual addresses of the above tables are stored in rdma_chnl_container_
 *
 * Each table contains a specific configuration to be applied at Vsync time. RDMA is programmed
 * to read from [start_index_used_ end_index_used_] inclusive. If more than one configuration is
 * applied within a vsync period, the new configuration will get added at the
 * next sequential index and RDMA end_index_used_ will get updated.
 *
 * rdma_usage_table_ is used to keep track of tables being used by RDMA. rdma_usage_table_ may
 * contain three possible values:
 * kRdmaTableReady: This index may be used by RDMA
 * kRdmaTableUnavailable: This index is unavailble
 * <config stamp>: This index includes a valid config. The stored value corresponds to the first
 *                 image handle that is contained in the config (we currently assume 1 image per
 *                 config).
 *
 * The client of the Osd instance is expected to call Osd::GetLastConfigStampApplied() on every
 * vsync interrupt to obtain the most recently applied config. This method checks if a previously
 * scheduled RDMA (via Osd::FlipOnVsync) has completed, and if so,  checks how far the RDMA was able
 * to write by comparing the "Config Stamp" in a scratch register to rdma_usage_table_. If RDMA did
 * not apply all the configs, it will re-schedule a new RDMA transaction.
 */
class Osd {
 public:
  Osd(bool supports_afbc, uint32_t fb_width, uint32_t fb_height, uint32_t display_width,
      uint32_t display_height, inspect::Node* parent_node);

  zx_status_t Init(ddk::PDev& pdev);
  void HwInit();

  // Disable the OSD and set the latest stamp to |config_stamp|.
  // If the driver disables (pauses) the OSD because the client sets an empty
  // config, the |config_stamp| should be the client-provided stamp; otherwise
  // it should use the invalid stamp value indicating that the OSD has been
  // invalidated.
  void Disable(config_stamp_t config_stamp = {.value = INVALID_CONFIG_STAMP_VALUE});
  void Enable();

  // Schedules the given |config| to be applied by the RDMA engine when the next VSYNC interrupt
  // occurs.
  void FlipOnVsync(uint8_t idx, const display_config_t* config, const config_stamp_t* config_stamp);

  // Returns the image handle that was most recently processed by the RDMA engine. If RDMA is
  // determined to be in progress and incomplete, then the previously applied image is returned. If
  // RDMA is determined to be complete at the time of a call, then the RDMA engine registers are
  // updated accordingly.
  //
  // This function is used by the vsync thread to determine the latest applied config.
  config_stamp_t GetLastConfigStampApplied();

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
  void SetRdmaTableValue(uint32_t table_index, uint32_t idx, uint32_t val);
  void FlushRdmaTable(uint32_t table_index);
  int GetNextAvailableRdmaTableIndex() __TA_EXCLUDES(rdma_lock_);

  // The following functions move the current RDMA state machine forward. If TryResolvePendingRdma
  // determines that RDMA has completed, it
  // - records the image handle of the most recently applied config based on scratch register
  //   content,
  // - updates the RDMA usage table and reschedules RDMA for remaining configs that the RDMA
  //   engine has not applied,
  // - writes to the RDMA control registers to clear and/or reschedule the RDMA interrupts.
  //
  // This method must be called when RDMA is active.
  void TryResolvePendingRdma() __TA_REQUIRES(rdma_lock_);
  void ProcessRdmaUsageTable() __TA_REQUIRES(rdma_lock_);

  void SetAfbcRdmaTableValue(uint32_t val) const;
  void FlushAfbcRdmaTable() const;
  int RdmaIrqThread() __TA_EXCLUDES(rdma_lock_);
  void EnableGamma();
  void DisableGamma();
  zx_status_t ConfigAfbc();
  zx_status_t SetGamma(GammaChannel channel, const float* data);
  void SetColorCorrection(uint32_t rdma_table_idx, const display_config_t* config);
  zx_status_t WaitForGammaAddressReady();
  zx_status_t WaitForGammaWriteReady();

  // Like Dump() but logs critical sections that require holding |rdma_lock_|.
  void DumpLocked() __TA_REQUIRES(rdma_lock_);
  void DumpRdmaRegisters();
  void DumpNonRdmaRegisters();
  void DumpRdmaState() __TA_REQUIRES(rdma_lock_);

  std::optional<fdf::MmioBuffer> vpu_mmio_;
  zx::bti bti_;

  // RDMA IRQ handle and thread used for diagnostic purposes.
  zx::interrupt rdma_irq_;
  thrd_t rdma_irq_thread_;

  fbl::Mutex rdma_lock_;

  uint64_t rdma_usage_table_[kNumberOfTables] TA_GUARDED(rdma_lock_);
  size_t start_index_used_ TA_GUARDED(rdma_lock_) = 0;
  size_t end_index_used_ TA_GUARDED(rdma_lock_) = 0;

  bool rdma_active_ TA_GUARDED(rdma_lock_) = false;
  config_stamp_t latest_applied_config_ TA_GUARDED(rdma_lock_) = {
      .value = INVALID_CONFIG_STAMP_VALUE};

  RdmaChannelContainer rdma_chnl_container_[kNumberOfTables];

  // use a single vmo for all channels
  zx::vmo rdma_vmo_;
  zx::pmt rdma_pmt_;
  zx_paddr_t rdma_phys_;
  uint8_t* rdma_vbuf_;

  // Container that holds AFBC specific trigger register
  RdmaChannelContainer afbc_rdma_chnl_container_;
  zx::vmo afbc_rdma_vmo_;
  zx_handle_t afbc_rdma_pmt_;
  zx_paddr_t afbc_rdma_phys_;
  uint8_t* afbc_rdma_vbuf_;

  const bool supports_afbc_;

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

  // Diagnostic variables:

  inspect::Node inspect_node_;
  inspect::UintProperty rdma_allocation_failures_;
  inspect::UintProperty rdma_irq_count_;
  inspect::UintProperty rdma_begin_count_;
  inspect::UintProperty rdma_pending_in_vsync_count_;
  inspect::UintProperty last_rdma_pending_in_vsync_interval_ns_;
  inspect::UintProperty last_rdma_pending_in_vsync_timestamp_ns_prop_;

  // Unused inspect properties (but still queried by lapis/detect for metrics and triage reporting).
  inspect::UintProperty rdma_stall_count_;
  inspect::UintProperty last_rdma_stall_timestamp_ns_;

  zx::time last_rdma_pending_in_vsync_timestamp_;
};

}  // namespace amlogic_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_OSD_H_
