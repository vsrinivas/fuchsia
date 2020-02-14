// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "controller.h"

#include <inttypes.h>
#include <lib/device-protocol/pci.h>
#include <lib/zx/clock.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/listnode.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/io-buffer.h>
#include <ddk/phys-iter.h>
#include <ddk/protocol/pci.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

#include "pci-bus.h"
#include "sata.h"

namespace ahci {

//clang-format on

// TODO(sron): Check return values from bus_->RegRead() and RegWrite().
// Handle properly for buses that may by unplugged at runtime.
uint32_t Controller::RegRead(size_t offset) {
  uint32_t val = 0;
  bus_->RegRead(offset, &val);
  return val;
}

zx_status_t Controller::RegWrite(size_t offset, uint32_t val) {
  return bus_->RegWrite(offset, val);
}

void Controller::AhciEnable() {
  uint32_t ghc = RegRead(kHbaGlobalHostControl);
  if (ghc & AHCI_GHC_AE)
    return;
  for (int i = 0; i < 5; i++) {
    ghc |= AHCI_GHC_AE;
    RegWrite(kHbaGlobalHostControl, ghc);
    ghc = RegRead(kHbaGlobalHostControl);
    if (ghc & AHCI_GHC_AE)
      return;
    usleep(10 * 1000);
  }
}

zx_status_t Controller::HbaReset() {
  // AHCI 1.3: Software may perform an HBA reset prior to initializing the controller
  uint32_t ghc = RegRead(kHbaGlobalHostControl);
  ghc |= AHCI_GHC_AE;
  RegWrite(kHbaGlobalHostControl, ghc);
  ghc |= AHCI_GHC_HR;
  RegWrite(kHbaGlobalHostControl, ghc);
  // reset should complete within 1 second
  zx_status_t status = bus_->WaitForClear(kHbaGlobalHostControl, AHCI_GHC_HR, zx::sec(1));
  if (status) {
    zxlogf(ERROR, "ahci: hba reset timed out\n");
  }
  return status;
}

zx_status_t Controller::SetDevInfo(uint32_t portnr, sata_devinfo_t* devinfo) {
  if (portnr >= AHCI_MAX_PORTS) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  ports_[portnr].SetDevInfo(devinfo);
  return ZX_OK;
}

void Controller::Queue(uint32_t portnr, sata_txn_t* txn) {
  ZX_DEBUG_ASSERT(portnr < AHCI_MAX_PORTS);
  Port* port = &ports_[portnr];
  zx_status_t status = port->Queue(txn);
  if (status == ZX_OK) {
    zxlogf(SPEW, "ahci.%u: queue txn %p offset_dev 0x%" PRIx64 " length 0x%x\n", port->num(), txn,
           txn->bop.rw.offset_dev, txn->bop.rw.length);
    // hit the worker thread
    sync_completion_signal(&worker_completion_);
  } else {
    zxlogf(INFO, "ahci.%u: failed to queue txn %p: %d\n", port->num(), txn, status);
    // TODO: close transaction.
  }
}

Controller::~Controller() {}

void Controller::Release(void* ctx) {
  Controller* controller = static_cast<Controller*>(ctx);
  controller->Shutdown();
  delete controller;
}

bool Controller::ShouldExit() {
  fbl::AutoLock lock(&lock_);
  return threads_should_exit_;
}

// worker thread

int Controller::WorkerLoop() {
  Port* port;
  for (;;) {
    // iterate all the ports and run or complete commands
    bool port_active = false;
    for (uint32_t i = 0; i < AHCI_MAX_PORTS; i++) {
      port = &ports_[i];

      // Complete commands first.
      bool txns_in_progress = port->Complete();
      // Process queued txns.
      bool txns_added = port->ProcessQueued();
      port_active = txns_in_progress || txns_added;
    }

    // Exit only when there are no more transactions in flight.
    if ((!port_active) && ShouldExit()) {
      return 0;
    }

    // Wait here until more commands are queued, or a port becomes idle.
    sync_completion_wait(&worker_completion_, ZX_TIME_INFINITE);
    sync_completion_reset(&worker_completion_);
  }
}

// irq handler:

int Controller::IrqLoop() {
  for (;;) {
    zx_status_t status = bus_->InterruptWait();
    if (status != ZX_OK) {
      if (!ShouldExit()) {
        zxlogf(ERROR, "ahci: error %d waiting for interrupt\n", status);
      }
      return 0;
    }
    // mask hba interrupts while interrupts are being handled
    uint32_t ghc = RegRead(kHbaGlobalHostControl);
    RegWrite(kHbaGlobalHostControl, ghc & ~AHCI_GHC_IE);

    // handle interrupt for each port
    uint32_t is = RegRead(kHbaInterruptStatus);
    RegWrite(kHbaInterruptStatus, is);
    for (uint32_t i = 0; is && i < AHCI_MAX_PORTS; i++) {
      if (is & 0x1) {
        bool txn_handled = ports_[i].HandleIrq();
        if (txn_handled) {
          // hit the worker thread to complete commands
          sync_completion_signal(&worker_completion_);
        }
      }
      is >>= 1;
    }

    // unmask hba interrupts
    ghc = RegRead(kHbaGlobalHostControl);
    RegWrite(kHbaGlobalHostControl, ghc | AHCI_GHC_IE);
  }
}

// implement device protocol:

zx_protocol_device_t ahci_device_proto = []() {
  zx_protocol_device_t device = {};
  device.version = DEVICE_OPS_VERSION;
  device.release = Controller::Release;
  return device;
}();

int Controller::InitScan() {
  // reset
  HbaReset();

  // enable ahci mode
  AhciEnable();

  cap_ = RegRead(kHbaCapabilities);

  // count number of ports
  uint32_t port_map = RegRead(kHbaPortsImplemented);

  // initialize ports
  zx_status_t status;
  for (uint32_t i = 0; i < AHCI_MAX_PORTS; i++) {
    if (!(port_map & (1u << i)))
      continue;  // port not implemented
    status = ports_[i].Configure(i, bus_.get(), kHbaPorts, cap_);
    if (status != ZX_OK) {
      return status;
    }
  }

  // clear hba interrupts
  RegWrite(kHbaInterruptStatus, RegRead(kHbaInterruptStatus));

  // enable hba interrupts
  uint32_t ghc = RegRead(kHbaGlobalHostControl);
  ghc |= AHCI_GHC_IE;
  RegWrite(kHbaGlobalHostControl, ghc);

  // this part of port init happens after enabling interrupts in ghc
  for (uint32_t i = 0; i < AHCI_MAX_PORTS; i++) {
    Port* port = &ports_[i];
    if (!(port->is_implemented()))
      continue;

    // enable port
    port->Enable();

    // enable interrupts
    port->RegWrite(kPortInterruptEnable, AHCI_PORT_INT_MASK);

    // reset port
    port->Reset();

    // FIXME proper layering?
    if (port->RegRead(kPortSataStatus) & AHCI_PORT_SSTS_DET_PRESENT) {
      port->set_present(true);
      if (port->RegRead(kPortSignature) == AHCI_PORT_SIG_SATA) {
        sata_bind(this, zxdev_, port->num());
      }
    }
  }

  return ZX_OK;
}

zx_status_t Controller::Create(zx_device_t* parent, std::unique_ptr<Controller>* con_out) {
  fbl::AllocChecker ac;
  std::unique_ptr<Bus> bus(new (&ac) PciBus());
  if (!ac.check()) {
    zxlogf(ERROR, "ahci: out of memory\n");
    return ZX_ERR_NO_MEMORY;
  }
  return CreateWithBus(parent, std::move(bus), con_out);
}

zx_status_t Controller::CreateWithBus(zx_device_t* parent, std::unique_ptr<Bus> bus,
                                      std::unique_ptr<Controller>* con_out) {
  fbl::AllocChecker ac;
  std::unique_ptr<Controller> controller(new (&ac) Controller());
  if (!ac.check()) {
    zxlogf(ERROR, "ahci: out of memory\n");
    return ZX_ERR_NO_MEMORY;
  }
  zx_status_t status = bus->Configure(parent);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ahci: failed to configure host bus\n");
    return status;
  }
  controller->bus_ = std::move(bus);
  *con_out = std::move(controller);
  return ZX_OK;
}

zx_status_t Controller::LaunchThreads() {
  zx_status_t status = irq_thread_.CreateWithName(IrqThread, this, "ahci-irq");
  if (status != ZX_OK) {
    zxlogf(ERROR, "ahci: error %d creating irq thread\n", status);
    return status;
  }
  status = worker_thread_.CreateWithName(WorkerThread, this, "ahci-worker");
  if (status != ZX_OK) {
    zxlogf(ERROR, "ahci: error %d creating worker thread\n", status);
    return status;
  }
  return ZX_OK;
}

void Controller::Shutdown() {
  {
    fbl::AutoLock lock(&lock_);
    threads_should_exit_ = true;
  }

  // Signal the worker thread.
  sync_completion_signal(&worker_completion_);
  worker_thread_.Join();

  // Signal the interrupt thread to exit.
  bus_->InterruptCancel();
  irq_thread_.Join();
}

// implement driver object:

zx_status_t ahci_bind(void* ctx, zx_device_t* parent) {
  std::unique_ptr<Controller> controller;
  zx_status_t status = Controller::Create(parent, &controller);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ahci: failed to create ahci controller (%d)\n", status);
    return status;
  }

  if ((status = controller->LaunchThreads()) != ZX_OK) {
    zxlogf(ERROR, "ahci: failed to start controller threads (%d)\n", status);
    return status;
  }

  // add the device for the controller
  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "ahci";
  args.ctx = controller.get();
  args.ops = &ahci_device_proto;
  args.flags = DEVICE_ADD_NON_BINDABLE;

  status = device_add(parent, &args, controller->zxdev_ptr());
  if (status != ZX_OK) {
    zxlogf(ERROR, "ahci: error %d in device_add\n", status);
    controller->Shutdown();
    return status;
  }

  // initialize controller and detect devices
  thrd_t t;
  int ret = thrd_create_with_name(&t, Controller::InitThread, controller.get(), "ahci-init");
  if (ret != thrd_success) {
    zxlogf(ERROR, "ahci: error %d in init thread create\n", status);
    // This is an error in that no devices will be found, but the AHCI controller is enabled.
    // Not returning an error, but the controller should be removed.
    // TODO: handle this better in upcoming init cleanup CL.
  }

  // Controller is retained by device_add().
  controller.release();
  return ZX_OK;
}

constexpr zx_driver_ops_t ahci_driver_ops = []() {
  zx_driver_ops_t driver = {};
  driver.version = DRIVER_OPS_VERSION;
  driver.bind = ahci_bind;
  return driver;
}();

}  // namespace ahci

// clang-format off
ZIRCON_DRIVER_BEGIN(ahci, ahci::ahci_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_CLASS, 0x01),
    BI_ABORT_IF(NE, BIND_PCI_SUBCLASS, 0x06),
    BI_MATCH_IF(EQ, BIND_PCI_INTERFACE, 0x01),
ZIRCON_DRIVER_END(ahci)
