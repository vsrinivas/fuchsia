// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/interrupts.h"

#include <lib/device-protocol/pci.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include <bitset>

#include "src/graphics/display/drivers/intel-i915-tgl/ddi.h"
#include "src/graphics/display/drivers/intel-i915-tgl/intel-i915-tgl.h"
#include "src/graphics/display/drivers/intel-i915-tgl/macros.h"
#include "src/graphics/display/drivers/intel-i915-tgl/pci-ids.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-ddi.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers.h"

namespace i915_tgl {

namespace {

struct HotplugDetectionResult {
  constexpr static size_t kMaxAllowedDdis = 32;
  std::bitset<kMaxAllowedDdis> detected;
  std::bitset<kMaxAllowedDdis> long_pulse;
};

HotplugDetectionResult SklDetectHotplug(fdf::MmioBuffer* mmio_space) {
  HotplugDetectionResult result;

  auto sde_int_identity =
      tgl_registers::SdeInterruptBase::Get(tgl_registers ::SdeInterruptBase::kSdeIntIdentity)
          .ReadFrom(mmio_space);
  auto hp_ctrl1 = tgl_registers::SouthHotplugCtrl ::Get(tgl_registers::DDI_A).ReadFrom(mmio_space);
  auto hp_ctrl2 = tgl_registers::SouthHotplugCtrl ::Get(tgl_registers::DDI_E).ReadFrom(mmio_space);
  for (auto ddi : kSklDdis) {
    auto hp_ctrl = ddi < tgl_registers::DDI_E ? hp_ctrl1 : hp_ctrl2;
    result.detected[ddi] =
        sde_int_identity.skl_ddi_bit(ddi).get() &
        (hp_ctrl.hpd_long_pulse(ddi).get() || hp_ctrl.hpd_short_pulse(ddi).get());
    result.long_pulse[ddi] = hp_ctrl.hpd_long_pulse(ddi).get();
  }
  // Write back the register to clear the bits
  hp_ctrl1.WriteTo(mmio_space);
  hp_ctrl2.WriteTo(mmio_space);
  sde_int_identity.WriteTo(mmio_space);

  return result;
}

HotplugDetectionResult TglDetectHotplug(fdf::MmioBuffer* mmio_space) {
  HotplugDetectionResult result;

  auto sde_int_identity =
      tgl_registers::SdeInterruptBase::Get(tgl_registers ::SdeInterruptBase::kSdeIntIdentity)
          .ReadFrom(mmio_space);
  auto hpd_int_identity =
      tgl_registers::HpdInterruptBase::Get(tgl_registers::HpdInterruptBase::kHpdIntIdentity)
          .ReadFrom(mmio_space);

  auto pch_ddi_ctrl =
      tgl_registers::IclSouthHotplugCtrl::Get(tgl_registers::DDI_A).ReadFrom(mmio_space);
  auto pch_tc_ctrl =
      tgl_registers::IclSouthHotplugCtrl::Get(tgl_registers::DDI_TC_1).ReadFrom(mmio_space);

  auto tbt_ctrl = tgl_registers::TbtHotplugCtrl::Get().ReadFrom(mmio_space);
  auto tc_ctrl = tgl_registers::TcHotplugCtrl::Get().ReadFrom(mmio_space);

  for (auto ddi : kTglDdis) {
    switch (ddi) {
      case tgl_registers::DDI_A:
      case tgl_registers::DDI_B:
      case tgl_registers::DDI_C: {
        result.detected[ddi] =
            sde_int_identity.icl_ddi_bit(ddi).get() &
            (pch_ddi_ctrl.hpd_long_pulse(ddi).get() || pch_ddi_ctrl.hpd_short_pulse(ddi).get());
        result.long_pulse[ddi] = pch_ddi_ctrl.hpd_long_pulse(ddi).get();
      } break;
      case tgl_registers::DDI_TC_1:
      case tgl_registers::DDI_TC_2:
      case tgl_registers::DDI_TC_3:
      case tgl_registers::DDI_TC_4:
      case tgl_registers::DDI_TC_5:
      case tgl_registers::DDI_TC_6: {
        bool sde_detected = sde_int_identity.icl_ddi_bit(ddi).get();
        bool tbt_detected = hpd_int_identity.tbt_hotplug(ddi).get();
        bool tc_detected = hpd_int_identity.tc_hotplug(ddi).get();
        result.detected[ddi] = tbt_detected || tc_detected || sde_detected;
        result.long_pulse[ddi] = (tbt_detected && tbt_ctrl.hpd_long_pulse(ddi).get()) ||
                                 (tc_detected && tc_ctrl.hpd_long_pulse(ddi).get()) ||
                                 (sde_detected && pch_tc_ctrl.hpd_long_pulse(ddi).get());
      } break;
    }
  }

  // Write back the register to clear the bits
  pch_ddi_ctrl.WriteTo(mmio_space);
  pch_tc_ctrl.WriteTo(mmio_space);
  tbt_ctrl.WriteTo(mmio_space);
  tc_ctrl.WriteTo(mmio_space);
  sde_int_identity.WriteTo(mmio_space);
  hpd_int_identity.WriteTo(mmio_space);

  return result;
}

void SklEnableHotplugInterrupts(fdf::MmioBuffer* mmio_space) {
  auto pch_fuses = tgl_registers::PchDisplayFuses::Get().ReadFrom(mmio_space);

  for (const auto ddi : kSklDdis) {
    bool enabled = false;
    switch (ddi) {
      case tgl_registers::DDI_A:
      case tgl_registers::DDI_E:
        enabled = true;
        break;
      case tgl_registers::DDI_B:
        enabled = pch_fuses.port_b_present();
        break;
      case tgl_registers::DDI_C:
        enabled = pch_fuses.port_c_present();
        break;
      case tgl_registers::DDI_D:
        enabled = pch_fuses.port_d_present();
        break;
      case tgl_registers::DDI_TC_3:
      case tgl_registers::DDI_TC_4:
      case tgl_registers::DDI_TC_5:
      case tgl_registers::DDI_TC_6:
        ZX_DEBUG_ASSERT_MSG(false, "Unsupported DDI (%d)", ddi);
        break;
    }

    auto hp_ctrl = tgl_registers::SouthHotplugCtrl::Get(ddi).ReadFrom(mmio_space);
    hp_ctrl.hpd_enable(ddi).set(enabled);
    hp_ctrl.WriteTo(mmio_space);

    auto mask = tgl_registers::SdeInterruptBase::Get(tgl_registers::SdeInterruptBase::kSdeIntMask)
                    .ReadFrom(mmio_space);
    mask.skl_ddi_bit(ddi).set(!enabled);
    mask.WriteTo(mmio_space);

    auto enable =
        tgl_registers::SdeInterruptBase::Get(tgl_registers::SdeInterruptBase::kSdeIntEnable)
            .ReadFrom(mmio_space);
    enable.skl_ddi_bit(ddi).set(enabled);
    enable.WriteTo(mmio_space);
  }
}

void TglEnableHotplugInterrupts(fdf::MmioBuffer* mmio_space) {
  constexpr zx_off_t kSHPD_FILTER_CNT = 0xc4038;
  constexpr uint32_t kSHPD_FILTER_CNT_500_ADJ = 0x001d9;
  mmio_space->Write32(kSHPD_FILTER_CNT_500_ADJ, kSHPD_FILTER_CNT);

  for (const auto ddi : kTglDdis) {
    switch (ddi) {
      case tgl_registers::DDI_TC_1:
      case tgl_registers::DDI_TC_2:
      case tgl_registers::DDI_TC_3:
      case tgl_registers::DDI_TC_4:
      case tgl_registers::DDI_TC_5:
      case tgl_registers::DDI_TC_6: {
        auto hp_ctrl = tgl_registers::TcHotplugCtrl::Get().ReadFrom(mmio_space);
        hp_ctrl.hpd_enable(ddi).set(1);
        hp_ctrl.WriteTo(mmio_space);

        auto mask =
            tgl_registers::HpdInterruptBase::Get(tgl_registers::HpdInterruptBase::kHpdIntMask)
                .ReadFrom(mmio_space);
        mask.set_reg_value(0);
        mask.WriteTo(mmio_space);

        auto enable =
            tgl_registers::HpdInterruptBase::Get(tgl_registers::HpdInterruptBase::kHpdIntEnable)
                .ReadFrom(mmio_space);
        enable.tc_hotplug(ddi).set(1);
        enable.tbt_hotplug(ddi).set(1);
        enable.WriteTo(mmio_space);
      }
        __FALLTHROUGH;
      case tgl_registers::DDI_A:
      case tgl_registers::DDI_B:
      case tgl_registers::DDI_C: {
        auto hp_ctrl = tgl_registers::IclSouthHotplugCtrl::Get(ddi).ReadFrom(mmio_space);
        hp_ctrl.hpd_enable(ddi).set(1);
        hp_ctrl.WriteTo(mmio_space);

        auto mask =
            tgl_registers::SdeInterruptBase::Get(tgl_registers::SdeInterruptBase::kSdeIntMask)
                .ReadFrom(mmio_space);
        mask.set_reg_value(0);
        mask.WriteTo(mmio_space);
        mask.ReadFrom(mmio_space);

        auto enable =
            tgl_registers::SdeInterruptBase::Get(tgl_registers::SdeInterruptBase::kSdeIntEnable)
                .ReadFrom(mmio_space);
        enable.icl_ddi_bit(ddi).set(1);
        enable.WriteTo(mmio_space);
      } break;
    }
  }
}

}  // namespace

Interrupts::Interrupts() { mtx_init(&lock_, mtx_plain); }

Interrupts::~Interrupts() { Destroy(); }

void Interrupts::Destroy() {
  irq_.destroy();
  if (irq_thread_) {
    thrd_join(irq_thread_.value(), nullptr);
    irq_thread_ = std::nullopt;
  }
  irq_.reset();
}

int Interrupts::IrqLoop() {
  for (;;) {
    zx_time_t timestamp;
    if (zx_interrupt_wait(irq_.get(), &timestamp) != ZX_OK) {
      zxlogf(INFO, "interrupt wait failed");
      return -1;
    }

    auto master = tgl_registers::GfxMasterInterrupt::Get().FromValue(0);
    if (is_tgl(device_id_)) {
      master.ReadFrom(mmio_space_).set_primary_interrupt(0).WriteTo(mmio_space_);
    }

    auto interrupt_ctrl = tgl_registers::DisplayInterruptControl::Get().ReadFrom(mmio_space_);
    interrupt_ctrl.set_enable_mask(0);
    interrupt_ctrl.WriteTo(mmio_space_);

    bool has_hot_plug_interrupt = interrupt_ctrl.sde_int_pending() ||
                                  (is_tgl(device_id_) && interrupt_ctrl.de_hpd_int_pending());

    if (has_hot_plug_interrupt) {
      auto detect_result =
          is_tgl(device_id_) ? TglDetectHotplug(mmio_space_) : SklDetectHotplug(mmio_space_);
      for (auto ddi : GetDdis(device_id_)) {
        if (detect_result.detected[ddi]) {
          zxlogf(TRACE, "Detected hot plug interrupt on ddi %d", ddi);
          hotplug_callback_(ddi, detect_result.long_pulse[ddi]);
        }
      }
    }

    if (interrupt_ctrl.de_pipe_c_int_pending()) {
      HandlePipeInterrupt(tgl_registers::PIPE_C, timestamp);
    } else if (interrupt_ctrl.de_pipe_b_int_pending()) {
      HandlePipeInterrupt(tgl_registers::PIPE_B, timestamp);
    } else if (interrupt_ctrl.de_pipe_a_int_pending()) {
      HandlePipeInterrupt(tgl_registers::PIPE_A, timestamp);
    }

    {
      // Handle GT interrupts via callback.
      fbl::AutoLock lock(&lock_);
      if (interrupt_cb_.callback) {
        if (is_tgl(device_id_)) {
          if (master.gt1() || master.gt0()) {
            // Mask isn't used
            interrupt_cb_.callback(interrupt_cb_.ctx, 0, timestamp);
          }
        } else {
          if (interrupt_ctrl.reg_value() & interrupt_mask_) {
            interrupt_cb_.callback(interrupt_cb_.ctx, interrupt_ctrl.reg_value(), timestamp);
          }
        }
      }
    }

    interrupt_ctrl.set_enable_mask(1);
    interrupt_ctrl.WriteTo(mmio_space_);

    if (is_tgl(device_id_)) {
      master.set_primary_interrupt(1).WriteTo(mmio_space_);
    }
  }
}

void Interrupts::HandlePipeInterrupt(tgl_registers::Pipe pipe, zx_time_t timestamp) {
  tgl_registers::PipeRegs regs(pipe);
  auto identity = regs.PipeDeInterrupt(regs.kIdentityReg).ReadFrom(mmio_space_);
  identity.WriteTo(mmio_space_);

  if (identity.vsync()) {
    pipe_vsync_callback_(pipe, timestamp);
  }
}

void Interrupts::EnablePipeVsync(tgl_registers::Pipe pipe, bool enable) {
  tgl_registers::PipeRegs regs(pipe);
  auto mask_reg = regs.PipeDeInterrupt(regs.kMaskReg).FromValue(0);
  mask_reg.set_vsync(!enable);
  mask_reg.WriteTo(mmio_space_);

  auto enable_reg = regs.PipeDeInterrupt(regs.kEnableReg).FromValue(0);
  enable_reg.set_vsync(enable);
  enable_reg.WriteTo(mmio_space_);
}

zx_status_t Interrupts::SetInterruptCallback(const intel_gpu_core_interrupt_t* callback,
                                             uint32_t interrupt_mask) {
  fbl::AutoLock lock(&lock_);
  if (callback->callback != nullptr && interrupt_cb_.callback != nullptr) {
    return ZX_ERR_ALREADY_BOUND;
  }
  interrupt_cb_ = *callback;
  interrupt_mask_ = interrupt_mask;
  return ZX_OK;
}

zx_status_t Interrupts::Init(PipeVsyncCallback pipe_vsync_callback,
                             HotplugCallback hotplug_callback, zx_device_t* dev,
                             const ddk::Pci& pci, fdf::MmioBuffer* mmio_space, uint16_t device_id) {
  ZX_DEBUG_ASSERT(pipe_vsync_callback);
  ZX_DEBUG_ASSERT(hotplug_callback);
  ZX_DEBUG_ASSERT(dev);
  ZX_DEBUG_ASSERT(mmio_space);

  // TODO(fxbug.dev/86038): Looks like calling Init multiple times is allowed for unit tests but it
  // would make the state of instances of this class more predictable to disallow this.
  if (irq_) {
    Destroy();
  }

  pipe_vsync_callback_ = std::move(pipe_vsync_callback);
  hotplug_callback_ = std::move(hotplug_callback);
  mmio_space_ = mmio_space;
  device_id_ = device_id;

  // Disable interrupts here, re-enable them in ::FinishInit()
  zxlogf(TRACE, "Interrupts disabled");

  if (is_tgl(device_id_)) {
    auto master = tgl_registers::GfxMasterInterrupt::Get().ReadFrom(mmio_space_);
    master.set_primary_interrupt(0);
    master.WriteTo(mmio_space_);
  }

  auto interrupt_ctrl = tgl_registers::DisplayInterruptControl::Get().ReadFrom(mmio_space);
  interrupt_ctrl.set_enable_mask(0);
  interrupt_ctrl.WriteTo(mmio_space);

  // Assume that PCI will enable bus mastering as required for MSI interrupts.
  zx_status_t status = pci.ConfigureInterruptMode(1, &irq_mode_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to configure irq mode (%d)", status);
    return ZX_ERR_INTERNAL;
  }

  if ((status = pci.MapInterrupt(0, &irq_)) != ZX_OK) {
    zxlogf(ERROR, "Failed to map interrupt (%d)", status);
    return status;
  }

  {
    thrd_t thread;
    int thrd_status = thrd_create_with_name(
        &thread, [](void* ctx) { return static_cast<Interrupts*>(ctx)->IrqLoop(); }, this,
        "i915-irq-thread");
    if (thrd_status != thrd_success) {
      status = thrd_status_to_zx_status(thrd_status);
      zxlogf(ERROR, "Failed to create irq thread (%d)", status);
      irq_.reset();
      return status;
    }
    irq_thread_ = thread;
  }

  zx_handle_t profile;
  status = device_get_profile(dev, ZX_PRIORITY_HIGH, "i915-interrupt", &profile);
  if (status != ZX_OK) {
    zxlogf(ERROR, "i915: device_get_profile failed: %d", status);
    irq_.reset();
    return status;
  }
  status = zx_object_set_profile(thrd_get_zx_handle(*irq_thread_), profile, 0u);
  if (status != ZX_OK) {
    zxlogf(ERROR, "i915: zx_object_set_profile failed: %d", status);
    // TODO(fxbug.dev/86042): This syscall is guaranteed to return an error in unit tests since
    // mock-ddk currently does not fully support `device_get_profile` (it returns ZX_HANDLE_INVALID
    // for `profile` even when reporting success). A failure here should become an error condition
    // and abort initialization when this can be faked, e.g. using lib/fake-object.
  }

  Resume();
  return ZX_OK;
}

void Interrupts::FinishInit() {
  zxlogf(TRACE, "Interrupts re-enabled");

  auto ctrl = tgl_registers::DisplayInterruptControl::Get().ReadFrom(mmio_space_);
  ctrl.set_enable_mask(1);
  ctrl.WriteTo(mmio_space_);

  if (is_tgl(device_id_)) {
    auto master = tgl_registers::GfxMasterInterrupt::Get().ReadFrom(mmio_space_);
    master.set_primary_interrupt(1);
    master.WriteTo(mmio_space_);
    master.ReadFrom(mmio_space_);  // posting read
  }
}

void Interrupts::Resume() {
  if (is_tgl(device_id_)) {
    TglEnableHotplugInterrupts(mmio_space_);
  } else {
    SklEnableHotplugInterrupts(mmio_space_);
  }
}

}  // namespace i915_tgl
