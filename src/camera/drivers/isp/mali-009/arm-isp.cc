// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arm-isp.h"

#include <lib/syslog/global.h>
#include <stdlib.h>
#include <unistd.h>
#include <zircon/types.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>

#include "arm-isp-regs.h"

namespace camera {

namespace {

constexpr uint32_t kHiu = 0;
constexpr uint32_t kPowerDomain = 1;
constexpr uint32_t kMemoryDomain = 2;
constexpr uint32_t kReset = 3;
constexpr uint32_t kIsp = 4;

// CLK Shifts & Masks
constexpr uint32_t kClkMuxMask = 0xfff;
constexpr uint32_t kClockEnableShift = 8;

constexpr uint8_t kPing = 0;
constexpr uint8_t kPong = 1;

constexpr uint8_t kCopyToIsp = 0;
constexpr uint8_t kCopyFromIsp = 1;

constexpr uint8_t kSafeStop = 0;
constexpr uint8_t kSafeStart = 1;

enum {
  COMPONENT_PDEV,
  COMPONENT_CAMERA_SENSOR,
  COMPONENT_COUNT,
};

}  // namespace

void ArmIspDevice::IspHWReset(bool reset) {
  if (reset) {
    reset_mmio_.ClearBits32(1 << 1, RESET4_LEVEL);
  } else {
    reset_mmio_.SetBits32(1 << 1, RESET4_LEVEL);
  }
  // Reference code has a sleep in this path.
  // TODO(braval@) Double check to look into if
  // this sleep is really necessary.
  zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));
}

void ArmIspDevice::PowerUpIsp() {
  // set bit[18-19]=0
  // TODO(braval@) Double check to look into if
  // this sleep is really necessary.
  power_mmio_.ClearBits32(1 << 18 | 1 << 19, AO_RTI_GEN_PWR_SLEEP0);
  zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));

  // set bit[18-19]=0
  power_mmio_.ClearBits32(1 << 18 | 1 << 19, AO_RTI_GEN_PWR_ISO0);

  // MEM_PD_REG0 set 0
  memory_pd_mmio_.Write32(0, HHI_ISP_MEM_PD_REG0);
  // MEM_PD_REG1 set 0
  memory_pd_mmio_.Write32(0, HHI_ISP_MEM_PD_REG1);

  // Refer to reference source code
  hiu_mmio_.Write32(0x5b446585, HHI_CSI_PHY_CNTL0);
  hiu_mmio_.Write32(0x803f4321, HHI_CSI_PHY_CNTL1);

  // Setup Clocks.
  // clear existing values
  hiu_mmio_.ClearBits32(kClkMuxMask, HHI_MIPI_ISP_CLK_CNTL);
  // set the divisor = 1 (writing (1-1) to div field)
  // source for the unused mux = S905D2_FCLK_DIV3   = 3 // 666.7 MHz
  hiu_mmio_.SetBits32(((1 << kClockEnableShift) | 4 << 9), HHI_MIPI_ISP_CLK_CNTL);
}

void ArmIspDevice::HandleDmaError() {
  auto global_mon_status = IspGlobalMonitor_Status::Get().ReadFrom(&isp_mmio_);

  auto global_mon_failures = IspGlobalMonitor_Failures::Get().ReadFrom(&isp_mmio_);

  global_mon_status.Print();
  global_mon_failures.Print();

  IspGlobalMonitor_ClearError::Get()
      .ReadFrom(&isp_mmio_)
      .set_output_dma_clr_alarm(1)
      .set_temper_dma_clr_alarm(1)
      .WriteTo(&isp_mmio_);

  IspGlobalMonitor_ClearError::Get()
      .ReadFrom(&isp_mmio_)
      .set_output_dma_clr_alarm(1)
      .WriteTo(&isp_mmio_);

  // Now read the alarms:
  global_mon_status = IspGlobalMonitor_Status::Get().ReadFrom(&isp_mmio_);
  global_mon_failures = IspGlobalMonitor_Failures::Get().ReadFrom(&isp_mmio_);

  global_mon_status.Print();
  global_mon_failures.Print();

  zxlogf(TRACE, "DMA Writer statuses:\n");
  full_resolution_dma_->PrintStatus(&isp_mmio_);
  downscaled_dma_->PrintStatus(&isp_mmio_);

  zxlogf(TRACE, "Clearing dma alarm\n");
  IspGlobalMonitor_ClearError::Get()
      .ReadFrom(&isp_mmio_)
      .set_output_dma_clr_alarm(0)
      .set_temper_dma_clr_alarm(0)
      .WriteTo(&isp_mmio_);

  IspGlobalMonitor_ClearError::Get()
      .ReadFrom(&isp_mmio_)
      .set_output_dma_clr_alarm(0)
      .WriteTo(&isp_mmio_);
}

zx_status_t ArmIspDevice::ErrorRoutine() {
  // Mask all IRQs
  IspGlobalInterrupt_MaskVector::Get().ReadFrom(&isp_mmio_).mask_all().WriteTo(&isp_mmio_);

  zx_status_t status = SetPort(kSafeStop);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Stopping ISP failed \n", __func__);
    return status;
  }

  IspGlobal_Config0::Get().ReadFrom(&isp_mmio_).set_global_fsm_reset(1).WriteTo(&isp_mmio_);

  IspGlobal_Config0::Get().ReadFrom(&isp_mmio_).set_global_fsm_reset(0).WriteTo(&isp_mmio_);

  IspGlobalInterrupt_MaskVector::Get()
      .ReadFrom(&isp_mmio_)
      .set_isp_start(0)
      .set_ctx_management_error(0)
      .set_broken_frame_error(0)
      .set_wdg_timer_timed_out(0)
      .set_frame_collision_error(0)
      .set_dma_error_interrupt(0)
      .set_fr_y_dma_write_done(0)
      .set_fr_uv_dma_write_done(0)
      .set_ds_y_dma_write_done(0)
      .set_ds_uv_dma_write_done(0)
      .WriteTo(&isp_mmio_);

  status = SetPort(kSafeStart);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Starting ISP failed \n", __func__);
    return status;
  }
  return status;
}

// Interrupt handler for the ISP.
int ArmIspDevice::IspIrqHandler() {
  zxlogf(INFO, "%s start\n", __func__);
  zx_status_t status = ZX_OK;

  while (running_.load()) {
    zx::time time;
    status = isp_irq_.wait(&time);
    if (status != ZX_OK) {
      return status;
    }

    auto irq_status = IspGlobalInterrupt_StatusVector::Get().ReadFrom(&isp_mmio_);

    IspGlobalInterrupt_ClearVector::Get()
        .ReadFrom(&isp_mmio_)
        .set_reg_value(0xFFFFFFFF)
        .WriteTo(&isp_mmio_);

    // Clear IRQ Vector
    IspGlobalInterrupt_Clear::Get().ReadFrom(&isp_mmio_).set_value(0).WriteTo(&isp_mmio_);

    IspGlobalInterrupt_Clear::Get().ReadFrom(&isp_mmio_).set_value(1).WriteTo(&isp_mmio_);

    IspGlobalInterrupt_Clear::Get().ReadFrom(&isp_mmio_).set_value(0).WriteTo(&isp_mmio_);

    if (irq_status.has_errors()) {
      zxlogf(ERROR, "%s ISP Error Occured, resetting ISP\n", __func__);
      if (irq_status.dma_error_interrupt()) {
        HandleDmaError();
      } else {
        status = ErrorRoutine();
        if (status != ZX_OK) {
          return status;
        }
      }
      continue;
    }

    // Currently only handling Frame Start Interrupt.
    if (irq_status.isp_start()) {
      // Frame Start Interrupt
      auto current_config = IspGlobal_Config4::Get().ReadFrom(&isp_mmio_);
      if (current_config.is_pong()) {
        // Use PING for next frame
        IspGlobal_Config3::Get().ReadFrom(&isp_mmio_).select_config_ping().WriteTo(&isp_mmio_);

        if (IsFrameProcessingInProgress()) {
          // TODO: (braval): Handle dropped frame

        } else {
          // Copy Config from local memory to ISP PING Config space
          CopyContextInfo(kPing, kCopyToIsp);
          // Copy Metering Info from ISP to Local Memory
          CopyMeteringInfo(kPing);
          // Start processing this new frame.
          sync_completion_signal(&frame_processing_signal_);
        }

      } else {
        // CURRENT CONFIG IS PING
        // Use PONG for next frame
        IspGlobal_Config3::Get().ReadFrom(&isp_mmio_).select_config_pong().WriteTo(&isp_mmio_);

        if (IsFrameProcessingInProgress()) {
          // TODO: (braval): Handle dropped frame

        } else {
          // Copy Config from local memory to ISP PING Config space
          CopyContextInfo(kPong, kCopyToIsp);
          // Copy Metering Info from ISP to Local Memory
          CopyMeteringInfo(kPong);
          // Start processing this new frame.
          sync_completion_signal(&frame_processing_signal_);
        }
      }
    }
  }
  return status;
}

bool ArmIspDevice::IsFrameProcessingInProgress() {
  return sync_completion_signaled(&frame_processing_signal_);
}

// Note: We have only one copy of local config and
//       metering info, so assign the correct device_offset
//       depending if it is PING or PONG context
//       before we copy the data to/from the ISP.
void ArmIspDevice::CopyContextInfo(uint8_t config_space, uint8_t direction) {
  zx_off_t device_offset;

  if (config_space == kPing) {
    device_offset = kDecompander0PingOffset;
  } else {
    // PONG Context
    device_offset = kDecompander0PongOffset;
  }

  if (direction == kCopyToIsp) {
    // Copy to ISP from Local Config Buffer
    isp_mmio_.CopyFrom32(isp_mmio_local_, kDecompander0PingOffset, device_offset, kConfigSize / 4);
  } else {
    // Copy from ISP to Local Config Buffer
    isp_mmio_local_.CopyFrom32(isp_mmio_, device_offset, kDecompander0PingOffset, kConfigSize / 4);
  }
}

void ArmIspDevice::CopyMeteringInfo(uint8_t config_space) {
  zx_off_t device_offset;

  if (config_space == kPing) {
    // PING Context
    device_offset = kPingMeteringStatsOffset;
  } else {
    // PONG Context
    device_offset = kPongMeteringStatsOffset;
  }

  // Copy from ISP to Local Config Buffer
  isp_mmio_local_.CopyFrom32(isp_mmio_, kAexpHistStatsOffset, kAexpHistStatsOffset, kHistSize / 4);
  isp_mmio_local_.CopyFrom32(isp_mmio_, device_offset, kPingMeteringStatsOffset, kMeteringSize / 4);
}

zx_status_t ArmIspDevice::IspContextInit() {
  // This is actually writing to the HW
  IspLoadSeq_settings();

  // This is being written to the local_config_buffer_
  IspLoadSeq_settings_context();

  sensor_module_ = camera::Sensor::Create(isp_mmio_.View(0), isp_mmio_local_, camera_sensor_);
  if (sensor_module_ == nullptr) {
    zxlogf(ERROR, "%s: Unable to start Sensor \n", __func__);
    return ZX_ERR_NO_MEMORY;
  }

  // We are setting up assuming kWDR_MODE_LINEAR as default mode
  IspLoadSeq_linear();

  // Call custom_init()
  IspLoadCustomSequence();

  // Initialize Gamma.
  gamma_rgb_fr_regs_.Init();
  gamma_rgb_ds_regs_.Init();

  // Input port safe start
  return SetPort(kSafeStart);
}

ArmIspRegisterDump ArmIspDevice::DumpRegisters() {
  ArmIspRegisterDump dump;
  // First dump the global registers:
  for (size_t i = 0; i < kGlobalConfigSize; i++) {
    dump.global_config[i] = isp_mmio_.Read<uint32_t>(4 * i);
  }
  // Then ping and pong:
  for (size_t i = 0; i < kContextConfigSize; i++) {
    dump.ping_config[i] = isp_mmio_.Read<uint32_t>(kPingContextConfigOffset + 4 * i);
    dump.pong_config[i] = isp_mmio_.Read<uint32_t>(kPongContextConfigOffset + 4 * i);
  }
  return dump;
}

zx_status_t ArmIspDevice::InitIsp() {
  // The ISP and MIPI module is in same power domain.
  // So if we don't call the power sequence of ISP, the mipi module
  // won't work and it will block accesses to the  mipi register block.
  PowerUpIsp();

  IspHWReset(true);

  // Start ISP Interrupt Handling Thread.
  auto start_thread = [](void* arg) -> int {
    return static_cast<ArmIspDevice*>(arg)->IspIrqHandler();
  };

  sync_completion_reset(&frame_processing_signal_);
  running_.store(true);
  int rc = thrd_create_with_name(&irq_thread_, start_thread, this, "isp_irq_thread");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }

  // Start frame processing thread
  auto frame_processing_func = [](void* arg) -> int {
    return static_cast<ArmIspDevice*>(arg)->FrameProcessingThread();
  };

  running_frame_processing_.store(true);
  rc = thrd_create_with_name(&frame_processing_thread_, frame_processing_func, this,
                             "frame_processing thread");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }

  IspHWReset(false);

  // validate the ISP product ID
  if (Id_Product::Get().ReadFrom(&isp_mmio_).value() != PRODUCT_ID_DEFAULT) {
    zxlogf(ERROR, "%s: Unknown product ID\n", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t status = DmaManager::Create(bti_, isp_mmio_local_, DmaManager::Stream::FullResolution,
                                          &full_resolution_dma_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Unable to start Full Resolution DMA Module \n", __func__);
    return status;
  }
  status =
      DmaManager::Create(bti_, isp_mmio_local_, DmaManager::Stream::Downscaled, &downscaled_dma_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Unable to start Downscaled DMA Module \n", __func__);
    return status;
  }

  return ZX_OK;
}

zx_status_t ArmIspDevice::SetupIspConfig() {
  // Mask all IRQs
  IspGlobalInterrupt_MaskVector::Get().ReadFrom(&isp_mmio_).mask_all().WriteTo(&isp_mmio_);

  // Now copy all ping config settings & metering settings and store it.
  CopyContextInfo(kPing, kCopyFromIsp);

  zx_status_t status = IspContextInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: IspContextInit failed %d\n", __func__, status);
    return status;
  }

  // Copy current context to ISP
  CopyContextInfo(kPing, kCopyToIsp);
  CopyContextInfo(kPong, kCopyToIsp);

  while (IspGlobalInterrupt_StatusVector::Get().ReadFrom(&isp_mmio_).reg_value()) {
    // driver is initialized. we can start processing interrupts
    // wait until irq mask is cleared and start processing
    IspGlobalInterrupt_Clear::Get().ReadFrom(&isp_mmio_).set_value(0).WriteTo(&isp_mmio_);
    IspGlobalInterrupt_Clear::Get().ReadFrom(&isp_mmio_).set_value(1).WriteTo(&isp_mmio_);
  }

  IspGlobalInterrupt_MaskVector::Get()
      .ReadFrom(&isp_mmio_)
      .set_isp_start(0)
      .set_ctx_management_error(0)
      .set_broken_frame_error(0)
      .set_wdg_timer_timed_out(0)
      .set_frame_collision_error(0)
      .set_dma_error_interrupt(0)
      .set_fr_y_dma_write_done(0)
      .set_fr_uv_dma_write_done(0)
      .set_ds_y_dma_write_done(0)
      .set_ds_uv_dma_write_done(0)
      .WriteTo(&isp_mmio_);

  // put ping pong in slave mode
  // SW only mode
  IspGlobal_Config3::Get()
      .ReadFrom(&isp_mmio_)
      .set_mcu_override_config_select(1)
      .WriteTo(&isp_mmio_);

  return ZX_OK;
}

zx_status_t ArmIspDevice::SetPort(uint8_t kMode) {
  constexpr uint32_t kCheckInterval = ZX_USEC(2000);
  constexpr uint32_t kTimeout = ZX_MSEC(100);

  // Input port safe stop or stop
  InputPort_Config3::Get().ReadFrom(&isp_mmio_).set_mode_request(kMode).WriteTo(&isp_mmio_);

  // timeout 100ms
  // NOTE: This is a required timeout. The mode register "should be [updated] within 1 frame ...
  // set the timeout as 100 ms"
  zx_time_t deadline = zx_deadline_after(kTimeout);
  do {
    if (InputPort_ModeStatus::Get().ReadFrom(&isp_mmio_).value() == kMode) {
      return ZX_OK;
    }
    zx_nanosleep(zx_deadline_after(kCheckInterval));
  } while (zx_clock_get_monotonic() < deadline);
  return ZX_ERR_TIMED_OUT;
}

// static
zx_status_t ArmIspDevice::Init(void** ctx_out) {
  ZX_ASSERT(ctx_out);
  *ctx_out = nullptr;
  fx_logger_config_t config{};
  config.min_severity = FX_LOG_INFO;
  config.console_fd = STDERR_FILENO;
  return fx_log_init_with_config(&config);  // Used by submodules
}

// static
zx_status_t ArmIspDevice::Create(void* ctx, zx_device_t* parent) {
  ddk::CompositeProtocolClient composite(parent);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "%s could not get composite protocoln", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_device_t* components[COMPONENT_COUNT];
  size_t actual;
  composite.GetComponents(components, COMPONENT_COUNT, &actual);
  if (actual != COMPONENT_COUNT) {
    zxlogf(ERROR, "%s Could not get components\n", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  ddk::PDev pdev(components[COMPONENT_PDEV]);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_PDEV not available\n", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  ddk::CameraSensorProtocolClient camera_sensor(components[COMPONENT_CAMERA_SENSOR]);
  if (!camera_sensor.is_valid()) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_CAMERA_SENSOR not available\n", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  std::optional<ddk::MmioBuffer> hiu_mmio;
  zx_status_t status = pdev.MapMmio(kHiu, &hiu_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: pdev_.MapMmio failed %d\n", __func__, status);
    return status;
  }

  std::optional<ddk::MmioBuffer> power_mmio;
  status = pdev.MapMmio(kPowerDomain, &power_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: pdev_.MapMmio failed %d\n", __func__, status);
    return status;
  }

  std::optional<ddk::MmioBuffer> memory_pd_mmio;
  status = pdev.MapMmio(kMemoryDomain, &memory_pd_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: pdev_.MapMmio failed %d\n", __func__, status);
    return status;
  }

  std::optional<ddk::MmioBuffer> reset_mmio;
  status = pdev.MapMmio(kReset, &reset_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: pdev_.MapMmio failed %d\n", __func__, status);
    return status;
  }

  std::optional<ddk::MmioBuffer> isp_mmio;
  status = pdev.MapMmio(kIsp, &isp_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: pdev_.MapMmio failed %d\n", __func__, status);
    return status;
  }

  zx::interrupt isp_irq;
  status = pdev.GetInterrupt(0, &isp_irq);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: pdev_.GetInterrupt failed %d\n", __func__, status);
    return status;
  }

  // Get our bti.
  zx::bti bti;
  status = pdev.GetBti(0, &bti);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: could not obtain bti: %d\n", __func__, status);
    return status;
  }

  // Allocate buffers for ISP SW configuration and metering information.
  mmio_buffer_t local_mmio_buffer;
  local_mmio_buffer.vaddr =
      new (static_cast<std::align_val_t>(alignof(uint32_t))) char[kLocalBufferSize];
  local_mmio_buffer.size = kLocalBufferSize;
  local_mmio_buffer.vmo = ZX_HANDLE_INVALID;

  auto isp_device = std::make_unique<ArmIspDevice>(
      parent, std::move(*hiu_mmio), std::move(*power_mmio), std::move(*memory_pd_mmio),
      std::move(*reset_mmio), std::move(*isp_mmio), local_mmio_buffer, std::move(isp_irq),
      std::move(bti), components[COMPONENT_CAMERA_SENSOR]);

  status = isp_device->InitIsp();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to Initialize ISP\n", __func__);
    return status;
  }

  zx_device_prop_t props[] = {
      {BIND_PLATFORM_PROTO, 0, ZX_PROTOCOL_ISP},
  };

  status = isp_device->DdkAdd("arm-isp", 0, props, countof(props));
  if (status != ZX_OK) {
    zxlogf(ERROR, "arm-isp: Could not create arm-isp device: %d\n", status);
    return status;
  } else {
    zxlogf(INFO, "arm-isp: Added arm-isp device\n");
  }

  // TODO(garratt): Enable this only under test.
  // Hold the unbind lock so we do not become unbound while the
  // ArmIspDeviceTester is being created:
  fbl::AutoLock guard(&isp_device->unbind_lock_);
  status = ArmIspDeviceTester::Create(isp_device.get(), &(isp_device->on_isp_unbind_));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to create ISP Tester\n", __func__);
    return status;
  }

  // isp_device intentionally leaked as it is now held by DevMgr.
  __UNUSED auto ptr = isp_device.release();

  return status;
}

DmaManager* ArmIspDevice::GetStream(stream_type_t type) {
  // Ignore anything that is not FullFrame or Downscaled:
  switch (type) {
    case STREAM_TYPE_FULL_RESOLUTION:
      return full_resolution_dma_.get();
    case STREAM_TYPE_DOWNSCALED:
      return downscaled_dma_.get();
    default:
      zxlogf(ERROR, "%s: Invalid stream type\n", __func__);
  }
  return nullptr;
}

// Functions that the isp_stream_protocol calls:
zx_status_t ArmIspDevice::ReleaseFrame(uint32_t buffer_id, stream_type_t type) {
  auto stream = GetStream(type);
  if (!stream) {
    return ZX_ERR_INVALID_ARGS;
  }
  return stream->ReleaseFrame(buffer_id);
}

// A call to either stream type to start will get the isp to start running.
zx_status_t ArmIspDevice::StartStream(stream_type_t type) {
  auto stream = GetStream(type);
  if (!stream) {
    return ZX_ERR_INVALID_ARGS;
  }

  stream->Enable();

  if (!streaming_) {
    auto status = StartStreaming();
    if (status == ZX_OK) {
      streaming_ = true;
    }
    return status;
  }
  return ZX_OK;
}

// The ISP will only stop streaming when both streams are stopped.
zx_status_t ArmIspDevice::StopStream(stream_type_t type) {
  auto stream = GetStream(type);
  if (!stream) {
    return ZX_ERR_INVALID_ARGS;
  }

  stream->Disable();

  if (streaming_ && !full_resolution_dma_->enabled() && !downscaled_dma_->enabled()) {
    auto status = StopStreaming();
    if (status == ZX_OK) {
      streaming_ = false;
    }
    return status;
  }
  return ZX_OK;
}

zx_status_t ArmIspDevice::StartStreaming() {
  if (streaming_) {
    return ZX_OK;
  }
  // At reset we use PING config
  IspGlobal_Config3::Get().ReadFrom(&isp_mmio_).select_config_ping().WriteTo(&isp_mmio_);

  // Grab a new frame for whichever dma is streaming:
  downscaled_dma_->LoadNewFrame();
  full_resolution_dma_->LoadNewFrame();

  // Copy current context to ISP
  CopyContextInfo(kPing, kCopyToIsp);

  // TODO(Garratt): Test if we need to load pong configuration now.
  full_resolution_dma_->LoadNewFrame();
  downscaled_dma_->LoadNewFrame();
  CopyContextInfo(kPong, kCopyToIsp);

  zx_status_t status = SetPort(kSafeStart);
  if (status != ZX_OK) {
    return status;
  }

  sensor_module_->StartStreaming();
  streaming_ = true;
  return ZX_OK;
}

zx_status_t ArmIspDevice::StopStreaming() {
  if (!streaming_) {
    return ZX_OK;
  }
  sensor_module_->StopStreaming();
  zx_status_t status = SetPort(kSafeStop);
  if (status != ZX_OK) {
    return status;
  }
  streaming_ = false;
  return ZX_OK;
}

// Note that a proper implementation of this method is not forthcoming, as this driver shall shortly
// be deprecated. For now, this implementation ensures compilation and provides identical
// functionality to the current driver - which is to say, there is no cleanup upon stream shutdown.
zx_status_t ArmIspDevice::ShutdownStream(const isp_stream_shutdown_callback_t* shutdown_callback) {
  shutdown_callback->shutdown_complete(shutdown_callback->ctx, ZX_OK);

  return ZX_OK;
}

zx_status_t ArmIspDevice::IspCreateOutputStream(const buffer_collection_info_2_t* buffer_collection,
                                                const image_format_2_t* image_format,
                                                const frame_rate_t* rate, stream_type_t type,
                                                const hw_accel_frame_callback_t* frame_callback,
                                                output_stream_protocol_t* out_s) {
  ZX_ASSERT(frame_callback && frame_callback->frame_ready);

  if (!IsIspConfigInitialized()) {
    zx_status_t status = SetupIspConfig();
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to Setup ISP\n", __func__);
      return status;
    }
    initialized_ = true;
  }

  // TODO(CAM-79): Set frame rate in sensor
  auto frame_ready_callback = [frame_callback =
                                   *frame_callback](const frame_available_info_t info) {
    frame_callback.frame_ready(frame_callback.ctx, &info);
  };

  // Set the control interface:
  out_s->ctx = this;
  switch (type) {
    case STREAM_TYPE_FULL_RESOLUTION:
      out_s->ops->start = [](void* ctx) {
        return reinterpret_cast<ArmIspDevice*>(ctx)->StartStream(STREAM_TYPE_FULL_RESOLUTION);
      };
      out_s->ops->stop = [](void* ctx) {
        return reinterpret_cast<ArmIspDevice*>(ctx)->StopStream(STREAM_TYPE_FULL_RESOLUTION);
      };
      out_s->ops->release_frame = [](void* ctx, uint32_t buffer_id) {
        return reinterpret_cast<ArmIspDevice*>(ctx)->ReleaseFrame(buffer_id,
                                                                  STREAM_TYPE_FULL_RESOLUTION);
      };
      out_s->ops->shutdown = [](void* ctx,
                                const isp_stream_shutdown_callback_t* shutdown_callback) {
        return reinterpret_cast<ArmIspDevice*>(ctx)->ShutdownStream(shutdown_callback);
      };
      return full_resolution_dma_->Configure(*buffer_collection, *image_format,
                                             frame_ready_callback);
    case STREAM_TYPE_DOWNSCALED:
      out_s->ops->start = [](void* ctx) {
        return reinterpret_cast<ArmIspDevice*>(ctx)->StartStream(STREAM_TYPE_DOWNSCALED);
      };
      out_s->ops->stop = [](void* ctx) {
        return reinterpret_cast<ArmIspDevice*>(ctx)->StopStream(STREAM_TYPE_DOWNSCALED);
      };
      out_s->ops->release_frame = [](void* ctx, uint32_t buffer_id) {
        return reinterpret_cast<ArmIspDevice*>(ctx)->ReleaseFrame(buffer_id,
                                                                  STREAM_TYPE_DOWNSCALED);
      };
      out_s->ops->shutdown = [](void* ctx,
                                const isp_stream_shutdown_callback_t* shutdown_callback) {
        return reinterpret_cast<ArmIspDevice*>(ctx)->ShutdownStream(shutdown_callback);
      };
      return downscaled_dma_->Configure(*buffer_collection, *image_format, frame_ready_callback);
    case STREAM_TYPE_SCALAR:
      return ZX_ERR_NOT_SUPPORTED;
    case STREAM_TYPE_INVALID:
    default:
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_ERR_INVALID_ARGS;
}

int ArmIspDevice::FrameProcessingThread() {
  while (running_frame_processing_.load()) {
    sync_completion_wait(&frame_processing_signal_, ZX_TIME_INFINITE);

    // Currently this is called only on the new frame signal, so OnNewFrame
    // checks if it needs to finish processing the last frame.
    full_resolution_dma_->OnNewFrame();
    downscaled_dma_->OnNewFrame();

    // Reset the signal
    sync_completion_reset(&frame_processing_signal_);
  }

  return ZX_OK;
}

ArmIspDevice::~ArmIspDevice() {
  free(isp_mmio_local_.get());
  running_.store(false);
  running_frame_processing_.store(false);
  thrd_join(irq_thread_, NULL);
  thrd_join(frame_processing_thread_, NULL);
  isp_irq_.destroy();
}

void ArmIspDevice::DdkUnbindNew(ddk::UnbindTxn txn) {
  // Make sure we don't unbind while the ArmIspTester is being constructed:
  fbl::AutoLock guard(&unbind_lock_);
  if (on_isp_unbind_) {
    on_isp_unbind_();
  }
  ShutDown();
  txn.Reply();
}

void ArmIspDevice::DdkRelease() { delete this; }

void ArmIspDevice::ShutDown() {}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.init = ArmIspDevice::Init;
  ops.bind = ArmIspDevice::Create;
  return ops;
}();

}  // namespace camera

// clang-format off
ZIRCON_DRIVER_BEGIN(arm-isp, camera::driver_ops, "arm-isp", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_ARM),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_ISP),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_ARM_MALI_IV009),
ZIRCON_DRIVER_END(arm-isp)

