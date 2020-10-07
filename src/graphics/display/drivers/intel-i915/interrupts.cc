// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "interrupts.h"

#include <zircon/syscalls.h>

#include "intel-i915.h"
#include "macros.h"
#include "registers.h"

namespace {
static int irq_handler(void* arg) { return static_cast<i915::Interrupts*>(arg)->IrqLoop(); }
}  // namespace

namespace i915 {

Interrupts::Interrupts(Controller* controller) : controller_(controller) {
  mtx_init(&lock_, mtx_plain);
}

Interrupts::~Interrupts() { ZX_ASSERT(irq_ == ZX_HANDLE_INVALID); }

void Interrupts::Destroy() {
  if (irq_ != ZX_HANDLE_INVALID) {
    zx_interrupt_destroy(irq_.get());
    thrd_join(irq_thread_, nullptr);

    irq_.reset();
  }
}

int Interrupts::IrqLoop() {
  zx_handle_t handle;
  zx_status_t status =
      device_get_profile(controller_->zxdev(), ZX_PRIORITY_HIGH, "i915-interrupt", &handle);
  if (status != ZX_OK) {
    zxlogf(ERROR, "i915: device_get_profile failed: %d", status);
    return -1;
  }
  status = zx_object_set_profile(zx_thread_self(), handle, 0u);
  if (status != ZX_OK) {
    zxlogf(ERROR, "i915: zx_object_set_profile failed: %d", status);
    return -1;
  }

  for (;;) {
    zx_time_t timestamp;
    if (zx_interrupt_wait(irq_.get(), &timestamp) != ZX_OK) {
      zxlogf(INFO, "interrupt wait failed");
      break;
    }
    auto interrupt_ctrl =
        registers::MasterInterruptControl::Get().ReadFrom(controller_->mmio_space());
    interrupt_ctrl.set_enable_mask(0);
    interrupt_ctrl.WriteTo(controller_->mmio_space());

    if (interrupt_ctrl.sde_int_pending()) {
      auto sde_int_identity =
          registers::SdeInterruptBase::Get(registers ::SdeInterruptBase::kSdeIntIdentity)
              .ReadFrom(controller_->mmio_space());
      auto hp_ctrl1 =
          registers::HotplugCtrl ::Get(registers::DDI_A).ReadFrom(controller_->mmio_space());
      auto hp_ctrl2 =
          registers::HotplugCtrl ::Get(registers::DDI_E).ReadFrom(controller_->mmio_space());
      for (uint32_t i = 0; i < registers::kDdiCount; i++) {
        registers::Ddi ddi = registers::kDdis[i];
        auto hp_ctrl = ddi < registers::DDI_E ? hp_ctrl1 : hp_ctrl2;
        bool hp_detected =
            sde_int_identity.ddi_bit(ddi).get() &
            (hp_ctrl.hpd_long_pulse(ddi).get() || hp_ctrl.hpd_short_pulse(ddi).get());
        if (hp_detected) {
          controller_->HandleHotplug(ddi, hp_ctrl.hpd_long_pulse(ddi).get());
        }
      }
      // Write back the register to clear the bits
      hp_ctrl1.WriteTo(controller_->mmio_space());
      hp_ctrl2.WriteTo(controller_->mmio_space());
      sde_int_identity.WriteTo(controller_->mmio_space());
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
    interrupt_ctrl.WriteTo(controller_->mmio_space());
  }
  return 0;
}

void Interrupts::HandlePipeInterrupt(registers::Pipe pipe, zx_time_t timestamp) {
  registers::PipeRegs regs(pipe);
  auto identity = regs.PipeDeInterrupt(regs.kIdentityReg).ReadFrom(controller_->mmio_space());
  identity.WriteTo(controller_->mmio_space());

  if (identity.vsync()) {
    controller_->HandlePipeVsync(pipe, timestamp);
  }
}

void Interrupts::EnablePipeVsync(registers::Pipe pipe, bool enable) {
  registers::PipeRegs regs(pipe);
  auto mask_reg = regs.PipeDeInterrupt(regs.kMaskReg).FromValue(0);
  mask_reg.set_vsync(!enable);
  mask_reg.WriteTo(controller_->mmio_space());

  auto enable_reg = regs.PipeDeInterrupt(regs.kEnableReg).FromValue(0);
  enable_reg.set_vsync(enable);
  enable_reg.WriteTo(controller_->mmio_space());
}

void Interrupts::EnableHotplugInterrupts() {
  auto sfuse_strap = registers::SouthFuseStrap::Get().ReadFrom(controller_->mmio_space());
  for (uint32_t i = 0; i < registers::kDdiCount; i++) {
    registers::Ddi ddi = registers::kDdis[i];
    bool enabled = (ddi == registers::DDI_A) || (ddi == registers::DDI_E) ||
                   (ddi == registers::DDI_B && sfuse_strap.port_b_present()) ||
                   (ddi == registers::DDI_C && sfuse_strap.port_c_present()) ||
                   (ddi == registers::DDI_D && sfuse_strap.port_d_present());

    auto hp_ctrl = registers::HotplugCtrl::Get(ddi).ReadFrom(controller_->mmio_space());
    hp_ctrl.hpd_enable(ddi).set(enabled);
    hp_ctrl.WriteTo(controller_->mmio_space());

    auto mask = registers::SdeInterruptBase::Get(registers::SdeInterruptBase::kSdeIntMask)
                    .ReadFrom(controller_->mmio_space());
    mask.ddi_bit(ddi).set(!enabled);
    mask.WriteTo(controller_->mmio_space());

    auto enable = registers::SdeInterruptBase::Get(registers::SdeInterruptBase::kSdeIntEnable)
                      .ReadFrom(controller_->mmio_space());
    enable.ddi_bit(ddi).set(enabled);
    enable.WriteTo(controller_->mmio_space());
  }
}

zx_status_t Interrupts::SetInterruptCallback(const zx_intel_gpu_core_interrupt_t* callback,
                                             uint32_t interrupt_mask) {
  fbl::AutoLock lock(&lock_);
  if (callback->callback != nullptr && interrupt_cb_.callback != nullptr) {
    return ZX_ERR_ALREADY_BOUND;
  }
  interrupt_cb_ = *callback;
  interrupt_mask_ = interrupt_mask;
  return ZX_OK;
}

zx_status_t Interrupts::Init() {
  ddk::MmioBuffer* mmio_space = controller_->mmio_space();

  // Disable interrupts here, re-enable them in ::FinishInit()
  auto interrupt_ctrl = registers::MasterInterruptControl::Get().ReadFrom(mmio_space);
  interrupt_ctrl.set_enable_mask(0);
  interrupt_ctrl.WriteTo(mmio_space);

  uint32_t irq_cnt = 0;
  zx_status_t status = pci_query_irq_mode(controller_->pci(), ZX_PCIE_IRQ_MODE_LEGACY, &irq_cnt);
  if (status != ZX_OK || !irq_cnt) {
    zxlogf(ERROR, "Failed to find interrupts (%d %d)", status, irq_cnt);
    return ZX_ERR_INTERNAL;
  }

  if ((status = pci_set_irq_mode(controller_->pci(), ZX_PCIE_IRQ_MODE_LEGACY, 1)) != ZX_OK) {
    zxlogf(ERROR, "Failed to set irq mode (%d)", status);
    return status;
  }

  if ((status = pci_map_interrupt(controller_->pci(), 0, irq_.reset_and_get_address()) != ZX_OK)) {
    zxlogf(ERROR, "Failed to map interrupt (%d)", status);
    return status;
  }

  status = thrd_create_with_name(&irq_thread_, irq_handler, this, "i915-irq-thread");
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to create irq thread");
    irq_.reset();
    return status;
  }

  Resume();
  return ZX_OK;
}

void Interrupts::FinishInit() {
  auto ctrl = registers::MasterInterruptControl::Get().ReadFrom(controller_->mmio_space());
  ctrl.set_enable_mask(1);
  ctrl.WriteTo(controller_->mmio_space());
}

void Interrupts::Resume() { EnableHotplugInterrupts(); }

}  // namespace i915
