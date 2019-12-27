// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_interrupt_master.h"

#include <lib/sync/completion.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/time.h>

#include <algorithm>

#include <ddktl/protocol/pci.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chip.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_regs.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/regs.h"

namespace wlan {
namespace brcmfmac {
namespace {

constexpr int kUserPacketExit = 1;
constexpr int kUserPacketAddHandler = 2;
constexpr int kUserPacketRemoveHandler = 3;

}  // namespace

PcieInterruptMaster::InterruptHandler::~InterruptHandler() = default;

PcieInterruptMaster::PcieInterruptMaster() = default;

PcieInterruptMaster::~PcieInterruptMaster() {
  if (!pci_interrupt_handlers_.empty()) {
    BRCMF_ERR("%zu interrupt handlers registered at shutdown\n", pci_interrupt_handlers_.size());
  }
  if (pci_interrupt_thread_.joinable()) {
    zx_port_packet_t packet = {};
    packet.user.u64[0] = kUserPacketExit;
    pci_interrupt_port_.queue(&packet);
    pci_interrupt_thread_.join();
  }
}

// static
zx_status_t PcieInterruptMaster::Create(
    zx_device_t* device, PcieBuscore* buscore,
    std::unique_ptr<PcieInterruptMaster>* out_interrupt_master) {
  zx_status_t status = ZX_OK;

  PcieBuscore::CoreRegs pci_core_regs;
  if ((status = buscore->GetCoreRegs(CHIPSET_PCIE2_CORE, &pci_core_regs)) != ZX_OK) {
    BRCMF_ERR("Failed to get PCIE2 core regs: %s\n", zx_status_get_string(status));
    return status;
  }

  // Disable interrupts while we're setting them up.
  pci_core_regs.RegWrite(BRCMF_PCIE_PCIE2REG_MAILBOXMASK, 0);

  // Get the PCI resources necessary to operate this device.
  auto pci_proto = std::make_unique<ddk::PciProtocolClient>();
  if ((status = ddk::PciProtocolClient::CreateFromDevice(device, pci_proto.get())) != ZX_OK) {
    BRCMF_ERR("ddk::PciProtocolClient::CreateFromDevice() failed: %s\n",
              zx_status_get_string(status));
    return status;
  }
  if ((status = pci_proto->SetIrqMode(ZX_PCIE_IRQ_MODE_MSI, 1)) != ZX_OK) {
    BRCMF_ERR("Failed to set MSI interrupt mode: %s\n", zx_status_get_string(status));
    return status;
  }

  zx::interrupt pci_interrupt;
  if ((status = pci_proto->MapInterrupt(0, &pci_interrupt)) != ZX_OK) {
    BRCMF_ERR("Failed to map MSI interrupt: %s\n", zx_status_get_string(status));
    return status;
  }

  zx::port pci_interrupt_port;
  if ((status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &pci_interrupt_port)) != ZX_OK) {
    BRCMF_ERR("Failed to create interrupt port: %s\n", zx_status_get_string(status));
    return status;
  }
  if ((status = pci_interrupt.bind(pci_interrupt_port, 0, ZX_INTERRUPT_BIND)) != ZX_OK) {
    BRCMF_ERR("Failed to bind interrupt: %s\n", zx_status_get_string(status));
    return status;
  }

  auto interrupt_master = std::make_unique<PcieInterruptMaster>();
  interrupt_master->pci_core_regs_ = std::move(pci_core_regs);
  interrupt_master->pci_interrupt_ = std::move(pci_interrupt);
  interrupt_master->pci_interrupt_port_ = std::move(pci_interrupt_port);
  interrupt_master->pci_interrupt_thread_ =
      std::thread(&PcieInterruptMaster::InterruptServiceFunction, interrupt_master.get());

  *out_interrupt_master = std::move(interrupt_master);
  return ZX_OK;
}

zx_status_t PcieInterruptMaster::AddInterruptHandler(InterruptHandler* handler) {
  return ModifyInterruptHandler(kUserPacketAddHandler, handler);
}

zx_status_t PcieInterruptMaster::RemoveInterruptHandler(InterruptHandler* handler) {
  return ModifyInterruptHandler(kUserPacketRemoveHandler, handler);
}

zx_status_t PcieInterruptMaster::ModifyInterruptHandler(int command, InterruptHandler* handler) {
  zx_status_t status = ZX_OK;
  sync_completion_t completion;

  zx_port_packet_t packet = {};
  packet.user.u64[0] = command;
  packet.user.u64[1] = reinterpret_cast<uint64_t>(&completion);
  packet.user.u64[2] = reinterpret_cast<uint64_t>(handler);
  packet.user.u64[3] = reinterpret_cast<uint64_t>(&status);
  pci_interrupt_port_.queue(&packet);
  sync_completion_wait(&completion, ZX_TIME_INFINITE);
  return status;
}

void PcieInterruptMaster::InterruptServiceFunction() {
  zx_status_t status = ZX_OK;

  // Enable interrupts to wait for them.
  pci_core_regs_.RegWrite(BRCMF_PCIE_PCIE2REG_MAILBOXMASK, BRCMF_PCIE_MB_INT_D2H_DB |
                                                               BRCMF_PCIE_MB_INT_FN0_0 |
                                                               BRCMF_PCIE_MB_INT_FN0_1);

  while (true) {
    zx_port_packet_t packet = {};
    if ((status = pci_interrupt_port_.wait(zx::time::infinite(), &packet)) != ZX_OK) {
      BRCMF_ERR("Failed to wait for PCI interrupt port: %s\n", zx_status_get_string(status));
      break;
    }

    if (packet.type == ZX_PKT_TYPE_USER) {
      // This is an user command packet.
      const int command = packet.user.u64[0];
      if (command == kUserPacketExit) {
        // Terminate the interrupt loop.
        break;
      }

      sync_completion_t* const completion =
          reinterpret_cast<sync_completion_t*>(packet.user.u64[1]);
      InterruptHandler* const handler = reinterpret_cast<InterruptHandler*>(packet.user.u64[2]);
      zx_status_t* const status = reinterpret_cast<zx_status_t*>(packet.user.u64[3]);
      if (command == kUserPacketAddHandler) {
        // Add a handler, if it doesn't already exist.
        if (std::find(pci_interrupt_handlers_.begin(), pci_interrupt_handlers_.end(), handler) !=
            pci_interrupt_handlers_.end()) {
          *status = ZX_ERR_ALREADY_EXISTS;
        } else {
          pci_interrupt_handlers_.emplace_back(handler);
          *status = ZX_OK;
        }
      } else if (command == kUserPacketRemoveHandler) {
        // Remove a handler, if it exists.
        auto iter =
            std::find(pci_interrupt_handlers_.begin(), pci_interrupt_handlers_.end(), handler);
        if (iter == pci_interrupt_handlers_.end()) {
          *status = ZX_ERR_NOT_FOUND;
        } else {
          pci_interrupt_handlers_.erase(iter);
          *status = ZX_OK;
        }
      } else {
        BRCMF_ERR("Invalid user command packet: %d\n", command);
        continue;
      }

      sync_completion_signal(completion);
      continue;
    } else if (packet.type != ZX_PKT_TYPE_INTERRUPT) {
      BRCMF_ERR("Invalid packet type %d\n", packet.type);
      continue;
    }

    // Disable interrupts while we handle them.
    pci_core_regs_.RegWrite(BRCMF_PCIE_PCIE2REG_MAILBOXMASK, 0);

    uint32_t mailboxint = pci_core_regs_.RegRead(BRCMF_PCIE_PCIE2REG_MAILBOXINT);
    uint32_t mailboxint_mask = 0;
    for (auto handler : pci_interrupt_handlers_) {
      mailboxint_mask |= handler->HandleInterrupt(mailboxint & ~mailboxint_mask);
    }

    // Now re-enable interrupts.
    pci_interrupt_.ack();
    pci_core_regs_.RegWrite(BRCMF_PCIE_PCIE2REG_MAILBOXINT, mailboxint_mask);
    pci_core_regs_.RegWrite(BRCMF_PCIE_PCIE2REG_MAILBOXMASK, BRCMF_PCIE_MB_INT_D2H_DB |
                                                                 BRCMF_PCIE_MB_INT_FN0_0 |
                                                                 BRCMF_PCIE_MB_INT_FN0_1);
  }

  // We're done!
  pci_core_regs_.RegWrite(BRCMF_PCIE_PCIE2REG_MAILBOXMASK, 0);
}

}  // namespace brcmfmac
}  // namespace wlan
