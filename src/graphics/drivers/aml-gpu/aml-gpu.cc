// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-gpu.h"

#include <fidl/fuchsia.hardware.gpu.amlogic/cpp/wire.h>
#include <fuchsia/hardware/iommu/c/banjo.h>
#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/platform-defs.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/fidl-utils/bind.h>
#include <lib/trace/event.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/smc.h>

#include <bind/fuchsia/arm/platform/cpp/fidl.h>
#include <bind/fuchsia/platform/cpp/fidl.h>
#include <soc/aml-s905d2/s905d2-hiu.h>

#include "s905d2-gpu.h"
#include "s912-gpu.h"
#include "src/devices/tee/drivers/optee/tee-smc.h"
#include "src/graphics/drivers/aml-gpu/aml_gpu-bind.h"
#include "t931-gpu.h"

namespace aml_gpu {
AmlGpu::AmlGpu(zx_device_t* parent) : DdkDeviceType(parent) {}

AmlGpu::~AmlGpu() {}

void AmlGpu::SetClkFreqSource(int32_t clk_source) {
  if (current_clk_source_ == clk_source) {
    return;
  }

  GPU_INFO("Setting clock source to %d: %d\n", clk_source, gpu_block_->gpu_clk_freq[clk_source]);
  uint32_t current_clk_cntl = hiu_buffer_->Read32(4 * gpu_block_->hhi_clock_cntl_offset);
  uint32_t enabled_mux = current_clk_cntl & (1 << kFinalMuxBitShift);
  uint32_t new_mux = enabled_mux == 0;
  uint32_t mux_shift = new_mux ? 16 : 0;

  // clear existing values
  current_clk_cntl &= ~(kClockMuxMask << mux_shift);
  // set the divisor, enable & source for the unused mux
  current_clk_cntl |= CalculateClockMux(true, gpu_block_->gpu_clk_freq[clk_source], 1) << mux_shift;

  // Write the new values to the unused mux
  hiu_buffer_->Write32(current_clk_cntl, 4 * gpu_block_->hhi_clock_cntl_offset);
  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));

  // Toggle current mux selection
  current_clk_cntl ^= (1 << kFinalMuxBitShift);

  // Select the unused input mux
  hiu_buffer_->Write32(current_clk_cntl, 4 * gpu_block_->hhi_clock_cntl_offset);

  current_clk_source_ = clk_source;
  UpdateClockProperties();
}

void AmlGpu::SetInitialClkFreqSource(int32_t clk_source) {
  uint32_t current_clk_cntl = hiu_buffer_->Read32(4 * gpu_block_->hhi_clock_cntl_offset);
  uint32_t enabled_mux = (current_clk_cntl & (1 << kFinalMuxBitShift)) != 0;
  uint32_t mux_shift = enabled_mux ? 16 : 0;

  if (current_clk_cntl & (1 << (mux_shift + kClkEnabledBitShift))) {
    SetClkFreqSource(clk_source);
  } else {
    GPU_INFO("Setting initial clock source to %d: %d\n", clk_source,
             gpu_block_->gpu_clk_freq[clk_source]);
    // Switching the final dynamic mux from a disabled source to an enabled
    // source doesn't work. If the current clock source is disabled, then
    // enable it instead of switching.
    current_clk_cntl &= ~(kClockMuxMask << mux_shift);
    current_clk_cntl |= CalculateClockMux(true, gpu_block_->gpu_clk_freq[clk_source], 1)
                        << mux_shift;

    // Write the new values to the existing mux.
    hiu_buffer_->Write32(current_clk_cntl, 4 * gpu_block_->hhi_clock_cntl_offset);
    zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
    current_clk_source_ = clk_source;
    UpdateClockProperties();
  }
}

void AmlGpu::UpdateClockProperties() {
  current_clk_source_property_.Set(current_clk_source_);
  uint32_t clk_mux_source = gpu_block_->gpu_clk_freq[current_clk_source_];
  current_clk_mux_source_property_.Set(clk_mux_source);
  ZX_DEBUG_ASSERT(clk_mux_source < kClockInputs);
  uint32_t current_clk_freq_hz = gpu_block_->input_freq_map[clk_mux_source];
  current_clk_freq_hz_property_.Set(current_clk_freq_hz);
  TRACE_INSTANT("magma", "AmlGpu::UpdateClockProperties", TRACE_SCOPE_PROCESS, "current_clk_source",
                current_clk_source_, "clk_mux_source", clk_mux_source, "current_clk_freq_hz",
                current_clk_freq_hz);
}

zx_status_t AmlGpu::Gp0Init() {
  hiu_dev_ = std::make_unique<aml_hiu_dev_t>();
  gp0_pll_dev_ = std::make_unique<aml_pll_dev_t>();

  // HIU Init.
  zx_status_t status = s905d2_hiu_init(hiu_dev_.get());
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml_gp0_init: hiu_init failed: %d", status);
    return status;
  }

  status = s905d2_pll_init(hiu_dev_.get(), gp0_pll_dev_.get(), GP0_PLL);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml_gp0_init: pll_init failed: %d", status);
    return status;
  }

  status = s905d2_pll_set_rate(gp0_pll_dev_.get(), 846000000);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml_gp0_init: pll_set_rate failed: %d", status);
    return status;
  }
  status = s905d2_pll_ena(gp0_pll_dev_.get());
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml_gp0_init: pll_ena failed: %d", status);
    return status;
  }
  return ZX_OK;
}

void AmlGpu::InitClock() {
  {
    auto result = reset_register_.WriteRegister32(gpu_block_->reset0_mask_offset,
                                                  aml_registers::MALI_RESET0_MASK, 0);
    if ((result.status() != ZX_OK) || result->result.is_err()) {
      zxlogf(ERROR, "Reset0 Mask Clear failed\n");
    }
  }

  {
    auto result = reset_register_.WriteRegister32(gpu_block_->reset0_level_offset,
                                                  aml_registers::MALI_RESET0_MASK, 0);
    if ((result.status() != ZX_OK) || result->result.is_err()) {
      zxlogf(ERROR, "Reset0 Level Clear failed\n");
    }
  }

  {
    auto result = reset_register_.WriteRegister32(gpu_block_->reset2_mask_offset,
                                                  aml_registers::MALI_RESET2_MASK, 0);
    if ((result.status() != ZX_OK) || result->result.is_err()) {
      zxlogf(ERROR, "Reset2 Mask Clear failed\n");
    }
  }

  {
    auto result = reset_register_.WriteRegister32(gpu_block_->reset2_level_offset,
                                                  aml_registers::MALI_RESET2_MASK, 0);
    if ((result.status() != ZX_OK) || result->result.is_err()) {
      zxlogf(ERROR, "Reset2 Level Clear failed\n");
    }
  }

  SetInitialClkFreqSource(gpu_block_->initial_clock_index);

  {
    auto result = reset_register_.WriteRegister32(gpu_block_->reset0_level_offset,
                                                  aml_registers::MALI_RESET0_MASK,
                                                  aml_registers::MALI_RESET0_MASK);
    if ((result.status() != ZX_OK) || result->result.is_err()) {
      zxlogf(ERROR, "Reset2 Level Set failed\n");
    }
  }

  {
    auto result = reset_register_.WriteRegister32(gpu_block_->reset2_level_offset,
                                                  aml_registers::MALI_RESET2_MASK,
                                                  aml_registers::MALI_RESET2_MASK);
    if ((result.status() != ZX_OK) || result->result.is_err()) {
      zxlogf(ERROR, "Reset2 Level Set failed\n");
    }
  }

  gpu_buffer_->Write32(0x2968A819, 4 * kPwrKey);
  gpu_buffer_->Write32(0xfff | (0x20 << 16), 4 * kPwrOverride1);
}

zx_status_t AmlGpu::DdkGetProtocol(uint32_t proto_id, void* out_proto) {
  if (proto_id == ZX_PROTOCOL_ARM_MALI) {
    auto proto = static_cast<arm_mali_protocol_t*>(out_proto);
    proto->ctx = this;
    proto->ops = &arm_mali_protocol_ops_;
    return ZX_OK;
  } else if (proto_id == bind::fuchsia::platform::BIND_PROTOCOL_DEVICE) {
    pdev_protocol_t* gpu_proto = static_cast<pdev_protocol_t*>(out_proto);
    // Forward the underlying ops.
    pdev_.GetProto(gpu_proto);
    return ZX_OK;
  } else {
    zxlogf(ERROR, "Invalid protocol requested: %d", proto_id);
    return ZX_ERR_INVALID_ARGS;
  }
}

void AmlGpu::ArmMaliGetProperties(mali_properties_t* out_properties) {
  *out_properties = properties_;
}

// Match the definitions in the Amlogic OPTEE implementation.
#define DMC_DEV_ID_GPU 1

#define DMC_DEV_TYPE_NON_SECURE 0
#define DMC_DEV_TYPE_SECURE 1
#define DMC_DEV_TYPE_INACCESSIBLE 2

zx_status_t AmlGpu::SetProtected(uint32_t protection_mode) {
  if (!secure_monitor_)
    return ZX_ERR_NOT_SUPPORTED;

  // Call into the TEE to mark a particular hardware unit as able to access
  // protected memory or not.
  zx_smc_parameters_t params = {};
  zx_smc_result_t result = {};
  constexpr uint32_t kFuncIdConfigDeviceSecure = 14;
  params.func_id = tee_smc::CreateFunctionId(tee_smc::kFastCall, tee_smc::kSmc32CallConv,
                                             tee_smc::kTrustedOsService, kFuncIdConfigDeviceSecure);
  params.arg1 = DMC_DEV_ID_GPU;
  params.arg2 = protection_mode;
  zx_status_t status = zx_smc_call(secure_monitor_.get(), &params, &result);
  if (status != ZX_OK) {
    GPU_ERROR("Failed to set unit %ld protected status %ld code: %d", params.arg1, params.arg2,
              status);
    return status;
  }
  if (result.arg0 != 0) {
    GPU_ERROR("Failed to set unit %ld protected status %ld: %lx", params.arg1, params.arg2,
              result.arg0);
    return ZX_ERR_INTERNAL;
  }
  current_protected_mode_property_.Set(protection_mode);
  return ZX_OK;
}

zx_status_t AmlGpu::ArmMaliEnterProtectedMode() {
  if (!secure_monitor_) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  return SetProtected(DMC_DEV_TYPE_SECURE);
}

zx_status_t AmlGpu::ArmMaliStartExitProtectedMode() {
  if (!secure_monitor_) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  // Switch device to inaccessible mode. This will prevent writes to all memory
  // and start resetting the GPU.
  return SetProtected(DMC_DEV_TYPE_INACCESSIBLE);
}

zx_status_t AmlGpu::ArmMaliFinishExitProtectedMode() {
  if (!secure_monitor_) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  // Switch to non-secure mode. This will check that the device has been reset
  // and will re-enable access to non-protected memory.
  return SetProtected(DMC_DEV_TYPE_NON_SECURE);
}

void AmlGpu::SetFrequencySource(SetFrequencySourceRequestView request,
                                SetFrequencySourceCompleter::Sync& completer) {
  if (request->source >= kMaxGpuClkFreq) {
    GPU_ERROR("Invalid clock freq source index\n");
    completer.Reply(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  SetClkFreqSource(request->source);
  completer.Reply(ZX_OK);
}

zx_status_t AmlGpu::ProcessMetadata(std::vector<uint8_t> raw_metadata) {
  properties_ = {};
  fidl::DecodedMessage<fuchsia_hardware_gpu_amlogic::wire::Metadata> decoded(
      raw_metadata.data(), static_cast<uint32_t>(raw_metadata.size()));
  if (!decoded.ok()) {
    GPU_ERROR("Unable to parse metadata %s", decoded.FormatDescription().c_str());
    return ZX_ERR_INTERNAL;
  }
  const auto& metadata = decoded.PrimaryObject();
  if (metadata->has_supports_protected_mode() && metadata->supports_protected_mode()) {
    properties_.supports_protected_mode = true;
  }
  return ZX_OK;
}

zx_status_t AmlGpu::Bind() {
  root_ = inspector_.GetRoot().CreateChild("aml-gpu");
  current_clk_source_property_ = root_.CreateUint("current_clk_source", current_clk_source_);
  current_clk_mux_source_property_ = root_.CreateUint("current_clk_mux_source", 0);
  current_clk_freq_hz_property_ = root_.CreateUint("current_clk_freq_hz", 0);
  // GPU is in unknown mode on Bind.
  current_protected_mode_property_ = root_.CreateInt("current_protected_mode", -1);
  size_t size;
  zx_status_t status = DdkGetMetadataSize(fuchsia_hardware_gpu_amlogic::wire::kMaliMetadata, &size);
  if (status == ZX_OK) {
    std::vector<uint8_t> raw_metadata(size);
    size_t actual;
    status = DdkGetMetadata(fuchsia_hardware_gpu_amlogic::wire::kMaliMetadata, raw_metadata.data(),
                            size, &actual);
    if (status != ZX_OK) {
      GPU_ERROR("Failed to get metadata");
      return status;
    }
    if (size != actual) {
      GPU_ERROR("Non-matching sizes %ld %ld", size, actual);
      return ZX_ERR_INTERNAL;
    }
    status = ProcessMetadata(std::move(raw_metadata));
    if (status != ZX_OK) {
      GPU_ERROR("Error processing metadata %d", status);
      return status;
    }
  }

  pdev_ = ddk::PDev::FromFragment(parent_);
  if (!pdev_.is_valid()) {
    GPU_ERROR("could not get platform device protocol\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  status = pdev_.MapMmio(MMIO_GPU, &gpu_buffer_);
  if (status != ZX_OK) {
    GPU_ERROR("pdev_map_mmio_buffer failed\n");
    return status;
  }

  status = pdev_.MapMmio(MMIO_HIU, &hiu_buffer_);
  if (status != ZX_OK) {
    GPU_ERROR("pdev_map_mmio_buffer failed\n");
    return status;
  }

  pdev_device_info_t info;
  status = pdev_.GetDeviceInfo(&info);
  if (status != ZX_OK) {
    GPU_ERROR("pdev_get_device_info failed\n");
    return status;
  }

  switch (info.pid) {
    case PDEV_PID_AMLOGIC_S912:
      gpu_block_ = &s912_gpu_blocks;
      break;
    case PDEV_PID_AMLOGIC_S905D2:
    case PDEV_PID_AMLOGIC_S905D3:
      gpu_block_ = &s905d2_gpu_blocks;
      break;
    // A311D and T931 have the same GPU registers.
    case PDEV_PID_AMLOGIC_T931:
    case PDEV_PID_AMLOGIC_A311D:
      gpu_block_ = &t931_gpu_blocks;
      break;
    default:
      GPU_ERROR("unsupported SOC PID %u\n", info.pid);
      return ZX_ERR_INVALID_ARGS;
  }

  ddk::RegistersProtocolClient reset_register(parent_, "register-reset");
  if (!reset_register.is_valid()) {
    GPU_ERROR("could not get reset_register fragment");
    return ZX_ERR_NO_RESOURCES;
  }
  zx::channel register_client_end, register_server_end;
  if ((status = zx::channel::create(0, &register_client_end, &register_server_end)) != ZX_OK) {
    GPU_ERROR("could not create channel %d\n", status);
    return status;
  }
  reset_register.Connect(std::move(register_server_end));
  reset_register_ =
      fidl::WireSyncClient<fuchsia_hardware_registers::Device>(std::move(register_client_end));

  if (info.pid == PDEV_PID_AMLOGIC_S905D3 && properties_.supports_protected_mode) {
    // S905D3 needs to use an SMC into the TEE to do protected mode switching.
    static constexpr uint32_t kTrustedOsSmcIndex = 0;
    status = pdev_.GetSmc(kTrustedOsSmcIndex, &secure_monitor_);
    if (status != ZX_OK) {
      GPU_ERROR("Unable to retrieve secure monitor SMC: %d", status);
      return status;
    }

    properties_.use_protected_mode_callbacks = true;
  }

  if (info.pid == PDEV_PID_AMLOGIC_S905D2 || info.pid == PDEV_PID_AMLOGIC_S905D3) {
    status = Gp0Init();
    if (status != ZX_OK) {
      GPU_ERROR("aml_gp0_init failed: %d\n", status);
      return status;
    }
  }

  InitClock();

  zx_device_prop_t props[] = {
      {BIND_PROTOCOL, 0, bind::fuchsia::platform::BIND_PROTOCOL_DEVICE},
      {BIND_PLATFORM_DEV_VID, 0, bind::fuchsia::arm::platform::BIND_PLATFORM_DEV_VID_ARM},
      {BIND_PLATFORM_DEV_PID, 0, bind::fuchsia::platform::BIND_PLATFORM_DEV_PID_GENERIC},
      {BIND_PLATFORM_DEV_DID, 0, bind::fuchsia::arm::platform::BIND_PLATFORM_DEV_DID_MAGMA_MALI},
  };

  status = DdkAdd(
      ddk::DeviceAddArgs("aml-gpu").set_props(props).set_inspect_vmo(inspector_.DuplicateVmo()));

  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

zx_status_t aml_gpu_bind(void* ctx, zx_device_t* parent) {
  auto aml_gpu = std::make_unique<AmlGpu>(parent);
  zx_status_t status = aml_gpu->Bind();
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-gpu error binding: %d", status);
    return status;
  }
  aml_gpu.release();
  return ZX_OK;
}

}  // namespace aml_gpu

static zx_driver_ops_t aml_gpu_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = aml_gpu::aml_gpu_bind,
};

// clang-format off
ZIRCON_DRIVER(aml_gpu, aml_gpu_driver_ops, "zircon", "0.1");
