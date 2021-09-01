// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_GRAPHICS_DRIVERS_AML_GPU_AML_GPU_H_
#define SRC_GRAPHICS_DRIVERS_AML_GPU_AML_GPU_H_

#include <fidl/fuchsia.hardware.gpu.clock/cpp/wire.h>
#include <fidl/fuchsia.hardware.registers/cpp/wire.h>
#include <fuchsia/hardware/gpu/mali/cpp/banjo.h>
#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <fuchsia/hardware/registers/cpp/banjo.h>
#include <fuchsia/hardware/sysmem/cpp/banjo.h>
#include <lib/device-protocol/pdev.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/mmio/mmio.h>

#include <memory>
#include <optional>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <soc/aml-common/aml-registers.h>

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
constexpr uint32_t kClockInputs = 8;

enum {
  MMIO_GPU,
  MMIO_HIU,
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
  // Map from the clock index to the mux source to use.
  uint32_t gpu_clk_freq[kMaxGpuClkFreq];
  // Map from the mux source to the frequency in Hz.
  uint32_t input_freq_map[kClockInputs];
} aml_gpu_block_t;

typedef struct aml_hiu_dev aml_hiu_dev_t;
typedef struct aml_pll_dev aml_pll_dev_t;
namespace aml_gpu {
class TestAmlGpu;

class AmlGpu;
using DdkDeviceType =
    ddk::Device<AmlGpu, ddk::Messageable<fuchsia_hardware_gpu_clock::Clock>::Mixin,
                ddk::GetProtocolable>;

class AmlGpu final : public DdkDeviceType,
                     public ddk::ArmMaliProtocol<AmlGpu>,
                     public ddk::EmptyProtocol<ZX_PROTOCOL_GPU_THERMAL> {
 public:
  AmlGpu(zx_device_t* parent);

  ~AmlGpu();

  zx_status_t Bind();

  void DdkRelease() { delete this; }
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);

  // ArmMaliProtocol implementation.
  void ArmMaliGetProperties(mali_properties_t* out_properties);
  zx_status_t ArmMaliEnterProtectedMode();
  zx_status_t ArmMaliStartExitProtectedMode();
  zx_status_t ArmMaliFinishExitProtectedMode();

  void SetFrequencySource(SetFrequencySourceRequestView request,
                          SetFrequencySourceCompleter::Sync& completer) override;

 private:
  friend class TestAmlGpu;

  zx_status_t Gp0Init();
  void InitClock();
  void SetClkFreqSource(int32_t clk_source);
  void SetInitialClkFreqSource(int32_t clk_source);
  zx_status_t ProcessMetadata(std::vector<uint8_t> metadata);
  zx_status_t SetProtected(uint32_t protection_mode);

  void UpdateClockProperties();

  ddk::PDev pdev_;
  mali_properties_t properties_{};

  std::optional<ddk::MmioBuffer> hiu_buffer_;
  std::optional<ddk::MmioBuffer> gpu_buffer_;

  fidl::WireSyncClient<fuchsia_hardware_registers::Device> reset_register_;
  // Resource used to perform SMC calls. Only needed on SM1.
  zx::resource secure_monitor_;

  aml_gpu_block_t* gpu_block_;
  std::unique_ptr<aml_hiu_dev_t> hiu_dev_;
  std::unique_ptr<aml_pll_dev_t> gp0_pll_dev_;
  int32_t current_clk_source_ = -1;
  // /dev/diagnostics/class/gpu-thermal/000.inspect
  inspect::Inspector inspector_;
  // bootstrap/driver_manager:root/aml-gpu
  inspect::Node root_;

  inspect::UintProperty current_clk_source_property_;
  inspect::UintProperty current_clk_mux_source_property_;
  inspect::UintProperty current_clk_freq_hz_property_;
  inspect::IntProperty current_protected_mode_property_;
};
}  // namespace aml_gpu

#endif  // SRC_GRAPHICS_DRIVERS_AML_GPU_AML_GPU_H_
