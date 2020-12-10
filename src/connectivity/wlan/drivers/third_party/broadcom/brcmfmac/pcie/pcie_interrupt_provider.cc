// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_interrupt_provider.h"

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

enum class PcieInterruptProvider::UserPacketCommand : uint64_t {
  kInvalid = 0,
  kStart = 1,
  kStop = 2,
  kAddHandler = 3,
  kRemoveHandler = 4,
};

PcieInterruptProvider::PcieInterruptProvider() = default;

PcieInterruptProvider::~PcieInterruptProvider() {
  if (!pci_interrupt_handlers_.empty()) {
    BRCMF_ERR("%zu interrupt handlers registered at shutdown", pci_interrupt_handlers_.size());
  }
  if (pci_interrupt_thread_.joinable()) {
    zx_port_packet_t packet = {};
    packet.user.u64[0] = static_cast<uint64_t>(UserPacketCommand::kStop);
    pci_interrupt_port_.queue(&packet);
    pci_interrupt_thread_.join();
  }
}

// static
zx_status_t PcieInterruptProvider::Create(
    zx_device_t* device, PcieBuscore* buscore,
    std::unique_ptr<PcieInterruptProvider>* out_interrupt_provider) {
  zx_status_t status = ZX_OK;

  auto pci_core_window = std::make_unique<PcieBuscore::PcieRegisterWindow>();
  if ((status = buscore->GetCoreWindow(Backplane::CoreId::kPcie2Core, pci_core_window.get())) !=
      ZX_OK) {
    BRCMF_ERR("Failed to get PCIE2 core window: %s", zx_status_get_string(status));
    return status;
  }

  // Disable interrupts while we're setting them up.
  if ((pci_core_window->Write(BRCMF_PCIE_PCIE2REG_MAILBOXMASK, 0)) != ZX_OK) {
    BRCMF_ERR("Failed to disable PCIE2 core interrupts: %s", zx_status_get_string(status));
    return status;
  }

  // Get the PCI resources necessary to operate this device.
  auto pci_proto = std::make_unique<ddk::PciProtocolClient>();
  if ((status = ddk::PciProtocolClient::CreateFromDevice(device, pci_proto.get())) != ZX_OK) {
    BRCMF_ERR("ddk::PciProtocolClient::CreateFromDevice() failed: %s",
              zx_status_get_string(status));
    return status;
  }
  if ((status = pci_proto->SetIrqMode(ZX_PCIE_IRQ_MODE_MSI, 1)) != ZX_OK) {
    BRCMF_ERR("Failed to set MSI interrupt mode: %s", zx_status_get_string(status));
    return status;
  }

  zx::interrupt pci_interrupt;
  if ((status = pci_proto->MapInterrupt(0, &pci_interrupt)) != ZX_OK) {
    BRCMF_ERR("Failed to map MSI interrupt: %s", zx_status_get_string(status));
    return status;
  }

  zx::port pci_interrupt_port;
  if ((status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &pci_interrupt_port)) != ZX_OK) {
    BRCMF_ERR("Failed to create interrupt port: %s", zx_status_get_string(status));
    return status;
  }
  if ((status = pci_interrupt.bind(pci_interrupt_port, 0, ZX_INTERRUPT_BIND)) != ZX_OK) {
    BRCMF_ERR("Failed to bind interrupt: %s", zx_status_get_string(status));
    return status;
  }

  auto interrupt_provider = std::make_unique<PcieInterruptProvider>();
  interrupt_provider->pci_core_window_ = std::move(pci_core_window);
  interrupt_provider->pci_interrupt_ = std::move(pci_interrupt);
  interrupt_provider->pci_interrupt_port_ = std::move(pci_interrupt_port);
  interrupt_provider->pci_interrupt_thread_ =
      std::thread(&PcieInterruptProvider::InterruptServiceFunction, interrupt_provider.get());

  // Block until interrupts are enabled or the enable fails.
  {
    zx_status_t status = ZX_OK;
    sync_completion_t completion;

    zx_port_packet_t packet = {};
    packet.user.u64[0] = static_cast<uint64_t>(UserPacketCommand::kStart);
    packet.user.u64[1] = reinterpret_cast<uint64_t>(&status);
    packet.user.u64[2] = reinterpret_cast<uint64_t>(&completion);
    interrupt_provider->pci_interrupt_port_.queue(&packet);
    sync_completion_wait(&completion, ZX_TIME_INFINITE);

    if (status != ZX_OK) {
      return status;
    }
  }

  *out_interrupt_provider = std::move(interrupt_provider);
  return ZX_OK;
}

zx_status_t PcieInterruptProvider::AddInterruptHandler(InterruptHandler* handler) {
  return ModifyInterruptHandler(UserPacketCommand::kAddHandler, handler);
}

zx_status_t PcieInterruptProvider::RemoveInterruptHandler(InterruptHandler* handler) {
  return ModifyInterruptHandler(UserPacketCommand::kRemoveHandler, handler);
}

zx_status_t PcieInterruptProvider::ModifyInterruptHandler(UserPacketCommand command,
                                                          InterruptHandler* handler) {
  zx_status_t status = ZX_OK;
  sync_completion_t completion;

  zx_port_packet_t packet = {};
  packet.user.u64[0] = static_cast<uint64_t>(command);
  packet.user.u64[1] = reinterpret_cast<uint64_t>(&status);
  packet.user.u64[2] = reinterpret_cast<uint64_t>(&completion);
  packet.user.u64[3] = reinterpret_cast<uint64_t>(handler);
  pci_interrupt_port_.queue(&packet);
  sync_completion_wait(&completion, ZX_TIME_INFINITE);
  return status;
}

void PcieInterruptProvider::HandleUserPacket(const zx_port_packet_t& packet, bool* exit_isr) {
  // This is an user command packet.
  const auto packet_command = static_cast<UserPacketCommand>(packet.user.u64[0]);
  switch (packet_command) {
    case UserPacketCommand::kStart: {
      // Enable interrupts to wait for them.
      const auto status = reinterpret_cast<zx_status_t*>(packet.user.u64[1]);
      if ((*status = pci_core_window_->Write(BRCMF_PCIE_PCIE2REG_MAILBOXMASK,
                                             BRCMF_PCIE_MB_INT_D2H_DB | BRCMF_PCIE_MB_INT_FN0_0 |
                                                 BRCMF_PCIE_MB_INT_FN0_1)) != ZX_OK) {
        BRCMF_ERR("Failed to enable PCIE2 core interrupts: %s", zx_status_get_string(*status));
      }
      sync_completion_signal(reinterpret_cast<sync_completion_t*>(packet.user.u64[2]));
      break;
    }

    case UserPacketCommand::kStop: {
      // Terminate the interrupt loop.
      *exit_isr = true;
      break;
    }

    case UserPacketCommand::kAddHandler: {
      // Add a handler, if it doesn't already exist.
      const auto status = reinterpret_cast<zx_status_t*>(packet.user.u64[1]);
      const auto handler = reinterpret_cast<InterruptHandler*>(packet.user.u64[3]);
      if (std::find(pci_interrupt_handlers_.begin(), pci_interrupt_handlers_.end(), handler) !=
          pci_interrupt_handlers_.end()) {
        *status = ZX_ERR_ALREADY_EXISTS;
      } else {
        pci_interrupt_handlers_.emplace_back(handler);
        *status = ZX_OK;
      }
      sync_completion_signal(reinterpret_cast<sync_completion_t*>(packet.user.u64[2]));
      break;
    }

    case UserPacketCommand::kRemoveHandler: {
      // Remove a handler, if it exists.
      const auto status = reinterpret_cast<zx_status_t*>(packet.user.u64[1]);
      const auto handler = reinterpret_cast<InterruptHandler*>(packet.user.u64[3]);
      auto iter =
          std::find(pci_interrupt_handlers_.begin(), pci_interrupt_handlers_.end(), handler);
      if (iter == pci_interrupt_handlers_.end()) {
        *status = ZX_ERR_NOT_FOUND;
      } else {
        pci_interrupt_handlers_.erase(iter);
        *status = ZX_OK;
      }
      sync_completion_signal(reinterpret_cast<sync_completion_t*>(packet.user.u64[2]));
      break;
    }

    default: {
      BRCMF_ERR("Invalid user packet command: %d", static_cast<int>(packet.user.u64[0]));
      break;
    }
  }
}

void PcieInterruptProvider::HandleInterruptPacket(const zx_port_packet_t& packet) {
  zx_status_t status = ZX_OK;

  // Disable interrupts while we handle them.
  if ((status = pci_core_window_->Write(BRCMF_PCIE_PCIE2REG_MAILBOXMASK, 0)) != ZX_OK) {
    BRCMF_ERR("Failed to disable PCIE2 core interrupts before handlers: %s",
              zx_status_get_string(status));
    return;
  }

  uint32_t mailboxint = 0;
  if ((status = pci_core_window_->Read(BRCMF_PCIE_PCIE2REG_MAILBOXINT, &mailboxint)) != ZX_OK) {
    BRCMF_ERR("Failed to read PCIE2 core interrupt vector: %s", zx_status_get_string(status));
    return;
  }
  uint32_t mailboxint_mask = 0;
  for (auto handler : pci_interrupt_handlers_) {
    mailboxint_mask |= handler->HandleInterrupt(mailboxint & ~mailboxint_mask);
  }

  // Now re-enable interrupts.
  pci_interrupt_.ack();
  if ((status = pci_core_window_->Write(BRCMF_PCIE_PCIE2REG_MAILBOXINT, mailboxint_mask)) !=
      ZX_OK) {
    BRCMF_ERR("Failed to write PCIE2 core interrupt vector: %s", zx_status_get_string(status));
    return;
  }
  if ((status = pci_core_window_->Write(BRCMF_PCIE_PCIE2REG_MAILBOXMASK,
                                        BRCMF_PCIE_MB_INT_D2H_DB | BRCMF_PCIE_MB_INT_FN0_0 |
                                            BRCMF_PCIE_MB_INT_FN0_1)) != ZX_OK) {
    BRCMF_ERR("Failed to enable PCIE2 core interrupts after handlers: %s",
              zx_status_get_string(status));
    return;
  }
}

void PcieInterruptProvider::InterruptServiceFunction() {
  bool exit_isr = false;
  while (!exit_isr) {
    zx_status_t status = ZX_OK;
    zx_port_packet_t packet = {};
    if ((status = pci_interrupt_port_.wait(zx::time::infinite(), &packet)) != ZX_OK) {
      BRCMF_ERR("Failed to wait for PCI interrupt port: %s", zx_status_get_string(status));
      break;
    }

    switch (packet.type) {
      case ZX_PKT_TYPE_USER: {
        // This is an user command packet.
        HandleUserPacket(packet, &exit_isr);
        break;
      }

      case ZX_PKT_TYPE_INTERRUPT: {
        // This is an interrupt packet.
        HandleInterruptPacket(packet);
        break;
      }

      default: {
        BRCMF_ERR("Invalid packet type %d", packet.type);
        break;
      }
    }
  }

  // We're done!
  pci_core_window_->Write(BRCMF_PCIE_PCIE2REG_MAILBOXMASK, 0);
}

}  // namespace brcmfmac
}  // namespace wlan
