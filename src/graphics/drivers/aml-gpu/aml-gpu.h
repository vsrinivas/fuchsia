// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_GRAPHICS_DRIVERS_AML_GPU_AML_GPU_H_
#define SRC_GRAPHICS_DRIVERS_AML_GPU_AML_GPU_H_

#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>

#include <memory>
#include <optional>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/sysmem.h>

#define GPU_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define GPU_INFO(fmt, ...) zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)

constexpr uint32_t kPwrKey = 0x14;
constexpr uint32_t kPwrOverride1 = 0x16;

constexpr uint32_t kClkEnabledBitShift = 8;

static inline uint32_t CalculateClockMux(bool enabled, uint32_t base, uint32_t divisor) {
  return (enabled << kClkEnabledBitShift) | (base << 9) | (divisor - 1);
}

constexpr uint32_t kClockMuxMask = 0xfff;
constexpr uint32_t kMaxGpuClkFreq = 6;
constexpr uint32_t kFinalMuxBitShift = 31;

enum {
  MMIO_GPU,
  MMIO_HIU,
  MMIO_PRESET,
};

typedef struct {
  // Byte offsets of the reset registers in the reset mmio region.
  uint32_t reset0_level_offset;
  uint32_t reset0_mask_offset;
  uint32_t reset2_level_offset;
  uint32_t reset2_mask_offset;
  // Offset of the Mali control register in the hiubus, in units of dwords.
  uint32_t hhi_clock_cntl_offset;
  // THe index into gpu_clk_freq that will be used upon booting.
  uint32_t initial_clock_index;
  uint32_t gpu_clk_freq[kMaxGpuClkFreq];
} aml_gpu_block_t;

typedef struct aml_hiu_dev aml_hiu_dev_t;
typedef struct aml_pll_dev aml_pll_dev_t;
namespace aml_gpu {
class TestAmlGpu;

class AmlGpu;
using DdkDeviceType = ddk::Device<AmlGpu, ddk::Messageable, ddk::GetProtocolable>;

class AmlGpu final : public DdkDeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_GPU_THERMAL> {
 public:
  AmlGpu(zx_device_t* parent);

  ~AmlGpu();

  zx_status_t Bind();

  void DdkRelease() { delete this; }
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);

  zx_status_t SetFrequencySource(uint32_t clk_source, fidl_txn_t* txn);

 private:
  friend class TestAmlGpu;

  zx_status_t Gp0Init();
  void InitClock();
  void SetClkFreqSource(int32_t clk_source);
  void SetInitialClkFreqSource(int32_t clk_source);

  ddk::PDev pdev_;

  std::optional<ddk::MmioBuffer> hiu_buffer_;
  std::optional<ddk::MmioBuffer> preset_buffer_;
  std::optional<ddk::MmioBuffer> gpu_buffer_;

  aml_gpu_block_t* gpu_block_;
  std::unique_ptr<aml_hiu_dev_t> hiu_dev_;
  std::unique_ptr<aml_pll_dev_t> gp0_pll_dev_;
  int32_t current_clk_source_ = -1;
};
}  // namespace aml_gpu

#endif  // SRC_GRAPHICS_DRIVERS_AML_GPU_AML_GPU_H_
