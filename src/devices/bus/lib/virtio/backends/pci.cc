// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../include/lib/virtio/backends/pci.h"

#include <assert.h>
#include <lib/zx/handle.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/port.h>
#include <zircon/syscalls/port.h>

#include <ddk/debug.h>
#include <ddktl/protocol/pci.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <virtio/virtio.h>

namespace virtio {

PciBackend::PciBackend(ddk::PciProtocolClient pci, zx_pcie_device_info_t info)
    : pci_(pci), info_(info) {
  snprintf(tag_, sizeof(tag_), "pci[%02x:%02x.%1x]", info_.bus_id, info_.dev_id, info_.func_id);
}

zx_status_t PciBackend::Bind() {
  zx::interrupt interrupt;
  zx_status_t st;

  st = zx::port::create(/*options=*/ZX_PORT_BIND_TO_INTERRUPT, &wait_port_);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: cannot create wait port: %d", tag(), st);
    return st;
  }

  // enable bus mastering
  if ((st = pci().EnableBusMaster(true)) != ZX_OK) {
    zxlogf(ERROR, "%s: cannot enable bus master %d", tag(), st);
    return st;
  }

  // try to set up our IRQ mode
  if ((st = pci().ConfigureIrqMode(1)) != ZX_OK) {
    zxlogf(ERROR, "%s: failed to configure irqs: %d", tag(), st);
    return st;
  }

  if ((st = pci().MapInterrupt(0, &interrupt)) != ZX_OK) {
    zxlogf(ERROR, "%s: failed to map irq %d", tag(), st);
    return st;
  }

  st = interrupt.bind(wait_port_, /*key=*/0, /*options=*/0);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: failed to bind interrupt %d", tag(), st);
    return st;
  }

  irq_handle_ = std::move(interrupt);
  zxlogf(TRACE, "%s: irq handle %u", tag(), irq_handle_.get());
  return Init();
}

zx_status_t PciBackend::InterruptValid() {
  if (!irq_handle_) {
    return ZX_ERR_BAD_HANDLE;
  }
  return ZX_OK;
}

zx_status_t PciBackend::WaitForInterrupt() {
  zx_port_packet packet;
  return wait_port_.wait(zx::deadline_after(zx::msec(100)), &packet);
}

void PciBackend::InterruptAck() { zx_interrupt_ack(irq_handle_.get()); }

}  // namespace virtio
