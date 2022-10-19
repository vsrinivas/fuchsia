// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915/interrupts.h"

#include <lib/device-protocol/pci.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include "src/graphics/display/drivers/intel-i915/intel-i915.h"
#include "src/graphics/display/drivers/intel-i915/macros.h"
#include "src/graphics/display/drivers/intel-i915/registers-ddi.h"
#include "src/graphics/display/drivers/intel-i915/registers.h"

namespace i915 {

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

    auto interrupt_ctrl = registers::MasterInterruptControl::Get().ReadFrom(mmio_space_);
    interrupt_ctrl.set_enable_mask(0);
    interrupt_ctrl.WriteTo(mmio_space_);

    if (interrupt_ctrl.sde_int_pending()) {
      auto sde_int_identity =
          registers::SdeInterruptBase::Get(registers ::SdeInterruptBase::kSdeIntIdentity)
              .ReadFrom(mmio_space_);
      auto hp_ctrl1 = registers::HotplugCtrl ::Get(registers::DDI_A).ReadFrom(mmio_space_);
      auto hp_ctrl2 = registers::HotplugCtrl ::Get(registers::DDI_E).ReadFrom(mmio_space_);
      for (const auto ddi : ddis_) {
        auto hp_ctrl = ddi < registers::DDI_E ? hp_ctrl1 : hp_ctrl2;
        bool hp_detected =
            sde_int_identity.ddi_bit(ddi).get() &
            (hp_ctrl.hpd_long_pulse(ddi).get() || hp_ctrl.hpd_short_pulse(ddi).get());
        if (hp_detected) {
          hotplug_callback_(ddi, hp_ctrl.hpd_long_pulse(ddi).get());
        }
      }
      // Write back the register to clear the bits
      hp_ctrl1.WriteTo(mmio_space_);
      hp_ctrl2.WriteTo(mmio_space_);
      sde_int_identity.WriteTo(mmio_space_);
    }

    if (interrupt_ctrl.de_pipe_c_int_pending()) {
      HandlePipeInterrupt(registers::PIPE_C, timestamp);
    } else if (interrupt_ctrl.de_pipe_b_int_pending()) {
      HandlePipeInterrupt(registers::PIPE_B, timestamp);
    } else if (interrupt_ctrl.de_pipe_a_int_pending()) {
      HandlePipeInterrupt(registers::PIPE_A, timestamp);
    }

    {
      fbl::AutoLock lock(&lock_);
      if (interrupt_ctrl.reg_value() & interrupt_mask_) {
        interrupt_cb_.callback(interrupt_cb_.ctx, interrupt_ctrl.reg_value(), timestamp);
      }
    }

    interrupt_ctrl.set_enable_mask(1);
    interrupt_ctrl.WriteTo(mmio_space_);
  }
}

void Interrupts::HandlePipeInterrupt(registers::Pipe pipe, zx_time_t timestamp) {
  registers::PipeRegs regs(pipe);
  auto identity = regs.PipeDeInterrupt(regs.kIdentityReg).ReadFrom(mmio_space_);
  identity.WriteTo(mmio_space_);

  if (identity.underrun()) {
    zxlogf(WARNING, "Transcoder underrun on pipe %d", pipe);
  }
  if (identity.vsync()) {
    pipe_vsync_callback_(pipe, timestamp);
  }
}

void Interrupts::EnablePipeVsync(registers::Pipe pipe, bool enable) {
  registers::PipeRegs regs(pipe);
  auto mask_reg = regs.PipeDeInterrupt(regs.kMaskReg).FromValue(0);
  mask_reg.set_underrun(!enable).set_vsync(!enable);
  mask_reg.WriteTo(mmio_space_);

  auto enable_reg = regs.PipeDeInterrupt(regs.kEnableReg).FromValue(0);
  enable_reg.set_underrun(enable).set_vsync(enable);
  enable_reg.WriteTo(mmio_space_);
}

void Interrupts::EnableHotplugInterrupts() {
  auto pch_fuses = registers::PchDisplayFuses::Get().ReadFrom(mmio_space_);
  ZX_DEBUG_ASSERT(ddis_.data());
  for (const auto ddi : ddis_) {
    bool enabled = (ddi == registers::DDI_A) || (ddi == registers::DDI_E) ||
                   (ddi == registers::DDI_B && pch_fuses.port_b_present()) ||
                   (ddi == registers::DDI_C && pch_fuses.port_c_present()) ||
                   (ddi == registers::DDI_D && pch_fuses.port_d_present());

    auto hp_ctrl = registers::HotplugCtrl::Get(ddi).ReadFrom(mmio_space_);
    hp_ctrl.hpd_enable(ddi).set(enabled);
    hp_ctrl.WriteTo(mmio_space_);

    auto mask = registers::SdeInterruptBase::Get(registers::SdeInterruptBase::kSdeIntMask)
                    .ReadFrom(mmio_space_);
    mask.ddi_bit(ddi).set(!enabled);
    mask.WriteTo(mmio_space_);

    auto enable = registers::SdeInterruptBase::Get(registers::SdeInterruptBase::kSdeIntEnable)
                      .ReadFrom(mmio_space_);
    enable.ddi_bit(ddi).set(enabled);
    enable.WriteTo(mmio_space_);
  }
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
                             const ddk::Pci& pci, fdf::MmioBuffer* mmio_space,
                             cpp20::span<const registers::Ddi> ddis) {
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
  ddis_ = ddis;

  // Disable interrupts here, re-enable them in ::FinishInit()
  auto interrupt_ctrl = registers::MasterInterruptControl::Get().ReadFrom(mmio_space);
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
  auto ctrl = registers::MasterInterruptControl::Get().ReadFrom(mmio_space_);
  ctrl.set_enable_mask(1);
  ctrl.WriteTo(mmio_space_);
}

void Interrupts::Resume() { EnableHotplugInterrupts(); }

}  // namespace i915
