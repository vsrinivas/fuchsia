// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <inttypes.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/hw/inout.h>
#include <lib/virtio/backends/pci.h>
#include <lib/virtio/device.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <array>
#include <memory>
#include <utility>

#include <pretty/hexdump.h>

namespace virtio {

Device::Device(zx_device_t* bus_device, zx::bti bti, std::unique_ptr<Backend> backend)
    : bti_(std::move(bti)), backend_(std::move(backend)), bus_device_(bus_device) {
  device_ops_.version = DEVICE_OPS_VERSION;
}

Device::~Device() { zxlogf(TRACE, "%s: exit", __func__); }

void Device::Unbind(ddk::UnbindTxn txn) {
  zxlogf(TRACE, "%s unbound", tag());
  txn.Reply();
}

void Device::Release() {
  backend_->Terminate();
  irq_thread_should_exit_.store(true, std::memory_order_release);
  thrd_join(irq_thread_, nullptr);
  backend_.reset();
}

void Device::IrqWorker() {
  const auto irq_mode = backend_->InterruptMode();
  ZX_DEBUG_ASSERT(irq_mode == PCI_IRQ_MODE_LEGACY || irq_mode == PCI_IRQ_MODE_MSI_X);
  zxlogf(DEBUG, "%s: starting %s irq worker", tag(),
         (irq_mode == PCI_IRQ_MODE_LEGACY) ? "legacy" : "msi-x");

  while (backend_->InterruptValid() == ZX_OK) {
    auto result = backend_->WaitForInterrupt();
    if (!result.is_ok()) {
      // Timeouts are fine, but need to continue because there's nothing to ack.
      if (result.status_value() == ZX_ERR_TIMED_OUT) {
        continue;
      }

      zxlogf(DEBUG, "%s: error while waiting for interrupt: %s", tag(), result.status_string());
      break;
    }

    // Ack the interrupt we saw based on the key returned from the port. For legacy interrupts
    // this will always be 0, but MSI-X will depend on the number of vectors configured.
    auto key = result.value();
    backend_->InterruptAck(key);

    // Read the status before completing the interrupt in case
    // another interrupt fires and changes the status.
    if (irq_mode == PCI_IRQ_MODE_LEGACY) {
      uint32_t irq_status = IsrStatus();
      zxlogf(TRACE, "%s: irq_status: %#x\n", __func__, irq_status);

      // Since we handle both interrupt types here it's possible for a
      // spurious interrupt if they come in sequence and we check IsrStatus
      // after both have been triggered.
      if (irq_status) {
        if (irq_status & VIRTIO_ISR_QUEUE_INT) { /* used ring update */
          IrqRingUpdate();
        }
        if (irq_status & VIRTIO_ISR_DEV_CFG_INT) { /* config change */
          IrqConfigChange();
        }
      }
    } else {
      // MSI-X
      zxlogf(TRACE, "%s: irq key: %u\n", __func__, key);
      switch (key) {
        case PciBackend::kMsiConfigVector:
          IrqConfigChange();
          break;
        case PciBackend::kMsiQueueVector:
          IrqRingUpdate();
          break;
      }
    }

    if (irq_thread_should_exit_.load(std::memory_order_relaxed)) {
      break;
    }
  }
}

int Device::IrqThreadEntry(void* arg) {
  Device* d = static_cast<Device*>(arg);
  d->IrqWorker();
  return 0;
}

void Device::StartIrqThread() {
  std::array<char, ZX_MAX_NAME_LEN> name{};
  snprintf(name.data(), name.size(), "%s-irq-worker", tag());
  thrd_create_with_name(&irq_thread_, IrqThreadEntry, this, name.data());
}

void Device::CopyDeviceConfig(void* _buf, size_t len) const {
  assert(_buf);

  for (size_t i = 0; i < len; i++) {
    backend_->ReadDeviceConfig(static_cast<uint16_t>(i), static_cast<uint8_t*>(_buf) + i);
  }
}

}  // namespace virtio
