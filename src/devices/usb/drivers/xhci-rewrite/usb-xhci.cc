// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-xhci.h"

#include <lib/device-protocol/pci.h>
#include <lib/fit/promise.h>
#include <lib/zx/bti.h>
#include <lib/zx/interrupt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <algorithm>
#include <memory>
#include <string>

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <hw/arch_ops.h>
#include <hw/reg.h>

#include "src/devices/usb/drivers/xhci-rewrite/usb_xhci_bind.h"

namespace usb_xhci {

namespace {

// Obtains the slot index for a specified endpoint.
uint32_t Log2(uint32_t value) { return 31 - __builtin_clz(value); }

// Computes the interval value for a specified endpoint.
int ComputeInterval(const usb_endpoint_descriptor_t* ep, usb_speed_t speed) {
  uint8_t ep_type = ep->bmAttributes & USB_ENDPOINT_TYPE_MASK;
  uint8_t interval = std::clamp(ep->bInterval, static_cast<uint8_t>(1), static_cast<uint8_t>(16));
  if (ep_type == USB_ENDPOINT_CONTROL || ep_type == USB_ENDPOINT_BULK) {
    if (speed == USB_SPEED_HIGH) {
      return Log2(interval);
    } else {
      return 0;
    }
  }

  // now we deal with interrupt and isochronous endpoints
  // first make sure bInterval is in legal range
  // See table 6-12 in xHCI specification section 6.2.3.6
  if (ep_type == USB_ENDPOINT_INTERRUPT && (speed == USB_SPEED_LOW || speed == USB_SPEED_FULL)) {
    interval = static_cast<uint8_t>(std::clamp(static_cast<int>(interval), 1, 255));
  } else {
    interval = static_cast<uint8_t>(std::clamp(static_cast<int>(interval), 1, 16));
  }

  switch (speed) {
    case USB_SPEED_LOW:
      return Log2(interval) + 3;  // + 3 to convert 125us to 1ms
    case USB_SPEED_FULL:
      if (ep_type == USB_ENDPOINT_ISOCHRONOUS) {
        return (interval - 1) + 3;
      } else {
        return Log2(interval) + 3;
      }
    case USB_SPEED_SUPER:
    case USB_SPEED_HIGH:
      return interval - 1;
    default:
      return 0;
  }
}

uint8_t XhciEndpointIndex(uint8_t ep_address) {
  if (ep_address == 0)
    return 0;
  uint8_t index = static_cast<uint8_t>(2 * (ep_address & ~USB_ENDPOINT_DIR_MASK));
  if ((ep_address & USB_ENDPOINT_DIR_MASK) == USB_ENDPOINT_OUT)
    index--;
  return index;
}

// Converts a USB request promise to a TRB promise. The returned TRB pointer
// will be nullptr.
TRBPromise USBRequestToTRBPromise(fit::promise<OwnedRequest> promise) {
  return promise.then([=](fit::result<OwnedRequest>& result) -> fit::result<TRB*, zx_status_t> {
    if (result.value().request()->response.status != ZX_OK) {
      return fit::error(result.value().request()->response.status);
    }
    return fit::ok<TRB*>(nullptr);
  });
}

}  // namespace

// Tracks the state of a USB request
// This state is passed around to the various transfer request
// queueing methods, and its lifetime should not outlast
// the lifetime of the transaction. This struct should be
// stack-allocated.
// None of the values in this field should be accessed
// after the USB transaction has been sent to hardware.
struct UsbXhci::UsbRequestState {
  // Invokes the completion callback if the request was marked as completed.
  // Returns true if the completer was called, false otherwise.
  bool Complete();

  // Request status
  zx_status_t status;

  // Number of bytes transferred
  size_t bytes_transferred = 0;

  // Whether or not the request is complete
  bool complete = false;

  // Size of the slot
  size_t slot_size;

  // Max burst size (value of the max burst size register + 1, since it is zero-based)
  uint32_t burst_size;

  // Max packet size
  uint32_t max_packet_size;

  // True if the current transfer is isochronous
  bool is_isochronous_transfer;

  // First TRB in the transfer
  // This is owned by the transfer ring.
  TRB* first_trb = nullptr;

  // Value to set the cycle bit on the first TRB to
  bool first_cycle;

  // TransferRing transaction state
  TransferRing::State transaction;

  ContiguousTRBInfo info;

  // The transfer ring to post transactions to
  // This is owned by UsbXhci and is valid for
  // the duration of this transaction.
  TransferRing* transfer_ring;

  // Index of the transfer ring
  uint8_t index;

  // Transfer context
  std::unique_ptr<TRBContext> context;

  // The number of packets in the transfer
  size_t packet_count = 0;

  // The slot ID of the transfer
  uint8_t slot;

  // Total length of the transfer
  uint32_t total_len = 0;

  // The setup TRB
  // This is owned by the transfer ring.
  TRB* setup;

  // The interrupter to use
  uint8_t interrupter = 0;

  // Pointer to the status TRB
  // This is owned by the transfer ring.
  TRB* status_trb_ptr = nullptr;

  // Cycle bit of the setup TRB during the allocation phase
  bool setup_cycle;
  // Last TRB in the transfer
  // This is owned by the transfer ring.
  TRB* last_trb;
};

TRBPromise UsbXhci::DisableSlotCommand(uint32_t slot_id) {
  uint8_t port;
  bool connected_to_hub = false;
  {
    fbl::AutoLock _(&device_state_[slot_id - 1].transaction_lock());
    device_state_[slot_id - 1].Disconnect();
    port = device_state_[slot_id - 1].GetPort();
    connected_to_hub = static_cast<bool>(device_state_[slot_id - 1].GetHubLocked());
  }
  DisableSlot cmd;
  cmd.set_slot(slot_id);
  auto context = command_ring_.AllocateContext();
  if (!context) {
    return fit::make_result_promise(fit::result<TRB*, zx_status_t>(fit::error(ZX_ERR_BAD_STATE)))
        .box();
  }
  if (!connected_to_hub) {
    port_state_[port - 1].slot_id = 0;
  }

  return SubmitCommand(cmd, std::move(context))
      .then([slot_id,
             this](fit::result<TRB*, zx_status_t>& result) -> fit::result<TRB*, zx_status_t> {
        if (result.is_error()) {
          return result;
        }
        TRB* trb = result.value();
        auto completion_event = reinterpret_cast<CommandCompletionEvent*>(trb);
        if (completion_event->CompletionCode() != CommandCompletionEvent::Success) {
          return fit::error(ZX_ERR_BAD_STATE);
        }
        dcbaa_[completion_event->SlotID()] = 0;
        {
          fbl::AutoLock _(&device_state_[slot_id - 1].transaction_lock());
          device_state_[slot_id - 1].reset();
        }
        return fit::ok(trb);
      })
      .box();
}

TRBPromise UsbXhci::EnableSlotCommand() {
  TRB trb;
  Control::Get().FromValue(0).set_Type(Control::EnableSlot).ToTrb(&trb);
  auto context = command_ring_.AllocateContext();
  return SubmitCommand(trb, std::move(context));
}

fit::promise<OwnedRequest, void> UsbXhci::UsbHciRequestQueue(OwnedRequest usb_request) {
  fit::bridge<OwnedRequest, void> bridge;
  usb_request_complete_t completion;
  completion.callback = [](void* ctx, usb_request_t* req) {
    auto completer = static_cast<fit::completer<OwnedRequest, void>*>(ctx);
    completer->complete_ok(OwnedRequest(req, sizeof(usb_request_t)));
    delete completer;
  };
  completion.ctx = new fit::completer<OwnedRequest, void>(std::move(bridge.completer));
  UsbHciRequestQueue(usb_request.take(), &completion);
  return bridge.consumer.promise().box();
}

zx_status_t DeviceState::InitializeSlotBuffer(const UsbXhci& hci, uint8_t slot_id, uint8_t port_id,
                                              const std::optional<HubInfo>& hub_info,
                                              std::unique_ptr<dma_buffer::PagedBuffer>* out) {
  // Section 4.3.3
  // 6.2.5 (Input Context initialization)
  std::unique_ptr<dma_buffer::PagedBuffer> buffer;
  zx_status_t status = hci.buffer_factory().CreatePaged(hci.bti(), ZX_PAGE_SIZE, false, &buffer);
  if (status != ZX_OK) {
    return status;
  }
  if (hci.get_is_32_bit_controller() && (buffer->phys()[0] >= UINT32_MAX)) {
    return ZX_ERR_NO_MEMORY;
  }
  // 6.2.5.1 -- Initialize input control context
  // NOTE: Input Control Context consumes 64 bytes if CSZ is 1
  // Enable bit starts at offset 2
  auto control = static_cast<uint32_t*>(buffer->virt());
  control[1] = 0x3;  // Enable both slot and endpoint context.
  size_t slot_size = (hci.CSZ()) ? 64 : 32;
  // Initialize input slot context data structure (6.2.2) with 1 context entry
  // Set root hub port number to port number and context entries to 1
  auto slot_context =
      reinterpret_cast<SlotContext*>(reinterpret_cast<unsigned char*>(control) + slot_size);
  if (hub_info) {
    slot_context->set_CONTEXT_ENTRIES(1)
        .set_ROUTE_STRING(hub_info->route_string)
        .set_PORTNO(hub_info->rh_port)
        .set_SPEED(hub_info->speed);
  } else {
    slot_context->set_CONTEXT_ENTRIES(1).set_PORTNO(port_id).set_SPEED(hci.GetPortSpeed(port_id));
  }
  *out = std::move(buffer);
  return ZX_OK;
}

zx_status_t DeviceState::InitializeEndpointContext(const UsbXhci& hci, uint8_t slot_id,
                                                   uint8_t port_id,
                                                   const std::optional<HubInfo>& hub_info,
                                                   dma_buffer::PagedBuffer* slot_context_buffer) {
  CRCR trb_phys = tr_.phys(hci.CapLength());
  auto* control = static_cast<uint32_t*>(slot_context_buffer->virt());
  size_t slot_size = (hci.CSZ()) ? 64 : 32;
  auto slot_context =
      reinterpret_cast<SlotContext*>(reinterpret_cast<unsigned char*>(control) + slot_size);
  // Initialize endpoint context 0
  // Set CERR to 3, TR dequeue pointer, max packet size, EP type = control, DCS = 1
  auto endpoint_context = reinterpret_cast<EndpointContext*>(
      reinterpret_cast<unsigned char*>(control) + (slot_size * 2));
  uint16_t mps = 8;
  uint8_t speed;
  if (hub_info) {
    speed = static_cast<uint8_t>(hub_info->speed);
    // TODO (fxbug.dev/34355): USB 3.1 support. Section 6.2.2
    if (((speed == USB_SPEED_LOW) || (speed == USB_SPEED_FULL)) &&
        (hub_info->hub_speed == USB_SPEED_HIGH)) {
      slot_context->set_PARENT_HUB_SLOT_ID(hci.DeviceIdToSlotId(hub_info->hub_id))
          .set_PARENT_PORT_NUMBER(speed);
    }
  } else {
    speed = hci.GetPortSpeed(port_id);
  }
  switch (speed) {
    case USB_SPEED_SUPER:
      mps = 512;
      break;
    case USB_SPEED_FULL:
    case USB_SPEED_HIGH:
      mps = 64;
      break;
    case USB_SPEED_LOW:
    default:
      mps = 8;
      break;
  }
  endpoint_context->Init(EndpointContext::Control, trb_phys, mps);
  return ZX_OK;
}

zx_status_t DeviceState::InitializeOutputContextBuffer(
    const UsbXhci& hci, uint8_t slot_id, uint8_t port_id, const std::optional<HubInfo>& hub_info,
    uint64_t* dcbaa, std::unique_ptr<dma_buffer::PagedBuffer>* out) {
  // Allocate an output device context data structure (6.2.1)
  // Update the DCBAA entry for this slot.
  std::unique_ptr<dma_buffer::PagedBuffer> output_context_buffer;
  zx_status_t status =
      hci.buffer_factory().CreatePaged(hci.bti(), ZX_PAGE_SIZE, false, &output_context_buffer);
  if (status != ZX_OK) {
    return status;
  }
  if (hci.get_is_32_bit_controller() && (output_context_buffer->phys()[0] >= UINT32_MAX)) {
    return ZX_ERR_NO_MEMORY;
  }
  dcbaa[slot_id] = output_context_buffer->phys()[0];
  if (!hub_info) {
    slot_id = static_cast<uint8_t>(slot_id);
  }
  hub_ = hub_info;
  hw_mb();
  *out = std::move(output_context_buffer);
  return ZX_OK;
}

TRBPromise DeviceState::AddressDeviceCommand(UsbXhci* hci, uint8_t slot, uint8_t port,
                                             std::optional<HubInfo> hub_info, uint64_t* dcbaa,
                                             EventRing* event_ring, CommandRing* command_ring,
                                             ddk::MmioBuffer* mmio, bool bsr) {
  if (!hub_info.has_value()) {
    hci->get_port_state()[port - 1].slot_id = slot;
  }
  std::unique_ptr<dma_buffer::PagedBuffer> slot_context_buffer;
  std::unique_ptr<dma_buffer::PagedBuffer> output_context_buffer;
  zx_status_t status = InitializeSlotBuffer(*hci, slot, port, hub_info, &slot_context_buffer);
  if (status != ZX_OK) {
    return fit::make_error_promise(status);
  }

  // Allocate the transfer ring (see section 4.9)
  // TODO (bbosak): Assign an Interrupter from the pool
  fbl::AutoLock _(&transaction_lock_);
  status = tr_.Init(hci->get_page_size(), hci->bti(), event_ring, hci->get_is_32_bit_controller(),
                    mmio, *hci);
  if (status != ZX_OK) {
    return fit::make_result_promise(fit::result<TRB*, zx_status_t>(fit::error(status))).box();
  }

  status = InitializeEndpointContext(*hci, slot, port, hub_info, slot_context_buffer.get());
  if (status != ZX_OK) {
    return fit::make_result_promise(fit::result<TRB*, zx_status_t>(fit::error(status))).box();
  }

  status = InitializeOutputContextBuffer(*hci, slot, port, hub_info, dcbaa, &output_context_buffer);
  if (status != ZX_OK) {
    return fit::make_result_promise(fit::result<TRB*, zx_status_t>(fit::error(status))).box();
  }

  // Issue an address device command for the device slot
  // See sections 3.3.4 and 6.4.3.4
  usb_xhci::AddressDeviceStruct command;
  command.ptr = slot_context_buffer->phys()[0];
  command.set_SlotID(slot);
  command.set_BSR(bsr);
  auto command_context = command_ring->AllocateContext();
  if (!command_context) {
    return fit::make_result_promise(fit::result<TRB*, zx_status_t>(fit::error(ZX_ERR_NO_MEMORY)))
        .box();
  }
  command_context->port_number = port;
  hw_mb();
  input_context_ = std::move(slot_context_buffer);
  device_context_ = std::move(output_context_buffer);
  return hci->SubmitCommand(command, std::move(command_context));
}

TRBPromise UsbXhci::AddressDeviceCommand(uint8_t slot_id) {
  usb_xhci::AddressDeviceStruct cmd;
  cmd.set_BSR(0);
  cmd.set_SlotID(slot_id);
  return SubmitCommand(cmd, command_ring_.AllocateContext());
}

usb_speed_t UsbXhci::GetDeviceSpeed(uint8_t slot) {
  {
    fbl::AutoLock _(&device_state_[slot - 1].transaction_lock());
    if (device_state_[slot - 1].GetHubLocked()) {
      return device_state_[slot - 1].GetHubLocked()->speed;
    }
  }
  return PORTSC::Get(cap_length_, device_state_[slot - 1].GetPort())
      .ReadFrom(&mmio_.value())
      .PortSpeed();
}

zx_status_t Interrupter::Start(uint32_t interrupter, const RuntimeRegisterOffset& offset,
                               ddk::MmioView mmio_view, UsbXhci* hci) {
  hci_ = hci;
  interrupter_ = interrupter;
  ERDP erdp = ERDP::Get(offset, interrupter).ReadFrom(&mmio_view);
  if (!event_ring_.erdp_phys()) {
    return ZX_ERR_BAD_STATE;
  }
  erdp.set_reg_value(event_ring_.erdp_phys());
  erdp.WriteTo(&mmio_view);
  ERSTBA ba = ERSTBA::Get(offset, interrupter).ReadFrom(&mmio_view);
  // This enables the interrupter
  ba.set_Pointer(event_ring_.erst()).WriteTo(&mmio_view);
  IMAN::Get(offset, interrupter).FromValue(0).set_IE(1).WriteTo(&mmio_view);
  thread_ = std::thread([this]() { IrqThread(); });
  return ZX_OK;
}

uint8_t UsbXhci::GetPortSpeed(uint8_t port_id) const {
  return static_cast<uint8_t>(
      PORTSC::Get(cap_length_, port_id).ReadFrom(&mmio_.value()).PortSpeed());
}

TRBPromise UsbXhci::AddressDeviceCommand(uint8_t slot_id, uint8_t port_id,
                                         std::optional<HubInfo> hub_info, bool bsr) {
  return device_state_[slot_id - 1].AddressDeviceCommand(this, slot_id, port_id, hub_info, dcbaa_,
                                                         &interrupters_[0].ring(), &command_ring_,
                                                         &mmio_.value(), bsr);
}

void UsbXhci::SetDeviceInformation(uint8_t slot, uint8_t port, const std::optional<HubInfo>& hub) {
  fbl::AutoLock _(&device_state_[slot - 1].transaction_lock());
  device_state_[slot - 1].SetDeviceInformation(slot, port, hub);
  if (hub) {
    uint8_t hub_id = hub->hub_id;
    fbl::AutoLock _(&device_state_[hub_id].transaction_lock());
    device_state_[hub_id].GetHubLocked()->port_to_device[port - 1] = static_cast<uint8_t>(slot - 1);
  }
}

TRBPromise UsbXhci::SetMaxPacketSizeCommand(uint8_t slot_id, uint8_t bMaxPacketSize0) {
  auto& state = device_state_[slot_id - 1];
  usb_xhci::AddressDeviceStruct cmd;
  {
    fbl::AutoLock _(&state.transaction_lock());
    auto control = reinterpret_cast<uint32_t*>(state.GetInputContext()->virt());
    size_t slot_size = (hcc_.CSZ()) ? 64 : 32;
    auto endpoint_context = reinterpret_cast<EndpointContext*>(
        reinterpret_cast<unsigned char*>(control) + (slot_size * 2));
    endpoint_context->set_MAX_PACKET_SIZE(bMaxPacketSize0);
    Control::Get().FromValue(0).set_Type(Control::EvaluateContextCommand).ToTrb(&cmd);
    cmd.set_SlotID(slot_id);

    cmd.ptr = state.GetInputContext()->phys()[0];
  }
  auto context = command_ring_.AllocateContext();
  return SubmitCommand(cmd, std::move(context));
}

zx_status_t UsbXhci::DeviceOnline(uint32_t slot, uint16_t port, usb_speed_t speed) {
  bool is_usb_3 = false;
  {
    fbl::AutoLock transaction_lock(&device_state_[slot - 1].transaction_lock());
    if (device_state_[slot - 1].GetHubLocked()) {
      transaction_lock.release();
      PostCallback([=](const ddk::UsbBusInterfaceProtocolClient& bus) {
        uint32_t hub_id;
        {
          fbl::AutoLock _(&get_device_state()[slot - 1].transaction_lock());
          if (!get_device_state()[slot - 1].GetHubLocked()) {
            // Race condition -- device was unplugged before we got a chance to notify the bus
            // driver.
            return ZX_OK;
          }
          hub_id = get_device_state()[slot - 1].GetHubLocked()->hub_id;
        }
        bus.AddDevice(slot - 1, hub_id, speed);
        return ZX_OK;
      });
      return ZX_OK;
    }
    is_usb_3 = get_port_state()[port].is_USB3;
  }
  PostCallback([=](const ddk::UsbBusInterfaceProtocolClient& bus) {
    bus.AddDevice(slot - 1,
                  static_cast<uint32_t>(is_usb_3 ? UsbHciGetMaxDeviceCount() - 1
                                                 : UsbHciGetMaxDeviceCount() - 2),
                  speed);
    return ZX_OK;
  });
  return ZX_OK;
}

TRBPromise UsbXhci::DeviceOffline(uint32_t slot, TRB* continuation) {
  fbl::AutoLock _(&device_state_[slot - 1].transaction_lock());
  device_state_[slot - 1].Disconnect();
  fit::bridge<TRB*, zx_status_t> bridge;
  PostCallback([this, slot, cb = std::move(bridge.completer),
                continuation](const ddk::UsbBusInterfaceProtocolClient& bus) mutable {
    for (size_t i = 0; i < kMaxEndpoints; i++) {
      fbl::AutoLock _(&device_state_[slot - 1].transaction_lock());
      auto trbs = device_state_[slot - 1].GetTransferRing(i).TakePendingTRBs();
      for (auto& trb : trbs) {
        trb.request->Complete(ZX_ERR_IO_NOT_PRESENT, 0);
      }
    }
    fbl::DoublyLinkedList<std::unique_ptr<TRBContext>> trbs;
    {
      fbl::AutoLock _(&device_state_[slot - 1].transaction_lock());
      trbs = device_state_[slot - 1].GetTransferRing().TakePendingTRBs();
    }
    for (auto& trb : trbs) {
      trb.request->Complete(ZX_ERR_IO_NOT_PRESENT, 0);
    }
    zx_status_t status = bus.RemoveDevice(slot - 1);
    if (status != ZX_OK) {
      cb.complete_error(status);
      return status;
    }
    cb.complete_ok(continuation);
    return status;
  });
  return bridge.consumer.promise().box();
}

void UsbXhci::ResetPort(uint16_t port) {
  PORTSC sc = PORTSC::Get(cap_length_, port).ReadFrom(&mmio_.value());
  PORTSC::Get(cap_length_, port)
      .FromValue(0)
      .set_CCS(sc.CCS())
      .set_PortSpeed(sc.PortSpeed())
      .set_PIC(sc.PIC())
      .set_PLS(sc.PLS())
      .set_PP(sc.PP())
      .set_PR(1)
      .WriteTo(&mmio_.value());
}

TRBPromise UsbXhci::UsbHciHubDeviceAddedAsync(uint32_t device_id, uint32_t port,
                                              usb_speed_t speed) {
  auto state = &device_state_[device_id];
  // Acquire a slot
  HubInfo hub;
  {
    fbl::AutoLock _(&state->transaction_lock());
    hub.hub_id = static_cast<uint8_t>(device_id);
    hub.speed = speed;
    hub.multi_tt = state->GetHubLocked()->multi_tt;
    hub.route_string =
        (state->GetHubLocked()->route_string) | ((port) << (state->GetHubLocked()->hub_depth * 4));
    hub.parent_port_number = static_cast<uint8_t>(port);
    hub.hub_depth = static_cast<uint8_t>(state->GetHubLocked()->hub_depth);
    hub.hub_speed = static_cast<uint8_t>(state->GetHubLocked()->speed);
    hub.rh_port = state->GetHubLocked()->rh_port;
  }
  return EnumerateDevice(this, static_cast<uint8_t>(port), std::make_optional(hub));
}

TRBPromise UsbXhci::ConfigureHubAsync(uint32_t device_id, usb_speed_t speed,
                                      const usb_hub_descriptor_t* desc, bool multi_tt) {
  auto state = &device_state_[device_id];
  HubInfo hub;
  struct AddressDeviceStruct cmd;
  std::unique_ptr<TRBContext> context;
  {
    fbl::AutoLock _(&state->transaction_lock());
    hub.hub_id = static_cast<uint8_t>(device_id);
    hub.speed = speed;
    hub.hub_speed = static_cast<uint8_t>(speed);
    hub.multi_tt = multi_tt;
    hub.rh_port = state->GetPort();
    if (state->GetHubLocked()) {
      hub.parent_port_number = state->GetHubLocked()->parent_port_number;
      hub.route_string = state->GetHubLocked()->route_string;
      hub.hub_depth = static_cast<uint8_t>(state->GetHubLocked()->hub_depth + 1);
      hub.rh_port = state->GetHubLocked()->rh_port;
    }
    state->GetHubLocked() = hub;
    uint8_t slot = state->GetSlot();
    size_t slot_size = (hcc_.CSZ()) ? 64 : 32;
    // Initialize input slot context data structure (6.2.2) with 1 context entry
    // Set root hub port number to port number and context entries to 1
    auto control = static_cast<uint32_t*>(state->GetInputContext()->virt());
    // Evaluate slot context
    control[0] = 0;
    control[1] = 1;
    auto slot_context =
        reinterpret_cast<SlotContext*>(reinterpret_cast<unsigned char*>(control) + slot_size);
    slot_context->set_SPEED(speed)
        .set_MULTI_TT(multi_tt)
        .set_HUB(1)
        .set_PORT_COUNT(desc->bNbrPorts)
        .set_TTT((speed == USB_SPEED_HIGH) ? ((desc->wHubCharacteristics >> 5) & 3) : 0);
    Control::Get().FromValue(0).set_Type(Control::EvaluateContextCommand).ToTrb(&cmd);
    cmd.set_SlotID(slot).set_BSR(0);
    cmd.ptr = state->GetInputContext()->phys()[0];
    hw_mb();
    context = command_ring_.AllocateContext();
  }
  return SubmitCommand(cmd, std::move(context))
      .then([=](fit::result<TRB*, zx_status_t>& result) -> TRBPromise {
        if (result.is_error()) {
          return fit::make_result_promise(result);
        }
        auto completion = reinterpret_cast<CommandCompletionEvent*>(result.value());
        if (completion->CompletionCode() != CommandCompletionEvent::Success) {
          return fit::make_error_promise(ZX_ERR_IO);
        }
        if (speed == USB_SPEED_SUPER) {
          std::optional<usb::Request<void>> request_wrapper;
          zx_status_t status =
              usb::Request<void>::Alloc(&request_wrapper, 0, 0, sizeof(usb_request_t));
          if (status != ZX_OK) {
            return fit::make_error_promise(status);
          }
          usb_request_t* request = request_wrapper->request();
          request->direct = true;
          request->header.device_id = device_id;
          request->header.ep_address = 0;
          request->setup.bm_request_type = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_DEVICE;
          {
            fbl::AutoLock _(&state->transaction_lock());
            request->setup.w_value = state->GetHubLocked()->hub_depth;
          }
          request->setup.w_index = 0;
          request->setup.b_request = USB_HUB_SET_DEPTH;
          request->setup.w_length = 0;
          return USBRequestToTRBPromise(UsbHciRequestQueue(std::move(*request_wrapper)).box());
        }
        return fit::make_result_promise(result);
      })
      .box();
}

void UsbXhci::DdkSuspend(ddk::SuspendTxn txn) {
  sync_completion_wait(&init_complete_, ZX_TIME_INFINITE);
  if (!mmio_.has_value()) {
    txn.Reply(ZX_ERR_BAD_STATE, 0);
    return;
  }
  // TODO(fxbug.dev/42612): do different things based on the requested_state and suspend reason.
  // for now we shutdown the driver in preparation for mexec
  USBCMD::Get(cap_length_).ReadFrom(&mmio_.value()).set_ENABLE(0).WriteTo(&mmio_.value());
  while (!USBSTS::Get(cap_length_).ReadFrom(&mmio_.value()).HCHalted()) {
  }
  txn.Reply(ZX_OK, 0);
}

void UsbXhci::DdkUnbind(ddk::UnbindTxn txn) {
  // Prevent anything external to us from queueing any more work during shutdown.
  sync_completion_wait(&init_complete_, ZX_TIME_INFINITE);

  running_ = false;
  PostCallback([this, transaction = std::move(txn)](
                   const ddk::UsbBusInterfaceProtocolClient& client) mutable {
    ddk_interaction_loop_.Quit();
    USBCMD::Get(cap_length_).ReadFrom(&mmio_.value()).set_ENABLE(0).WriteTo(&mmio_.value());
    while (!USBSTS::Get(cap_length_).ReadFrom(&mmio_.value()).HCHalted()) {
    }
    // Disable all interrupters

    uint32_t active_interrupters;
    {
      // This is safe because no new interrupters will be added after the running
      // bit is set to false.
      fbl::AutoLock _(&scheduler_lock_);
      active_interrupters = active_interrupters_;
    }
    for (size_t i = 0; i < active_interrupters; i++) {
      interrupters_[i].Stop();
    }
    // Should now be safe to terminate everything on the command ring
    bool pending;
    do {
      pending = false;
      auto trbs = command_ring_.TakePendingTRBs();
      for (auto& trb : trbs) {
        pending = true;
        CommandCompletionEvent evt;
        evt.ptr = 0;
        evt.set_Type(Control::CommandCompletionEvent);
        evt.set_CompletionCode(CommandCompletionEvent::CommandRingStopped);
        if (trb.completer.has_value()) {
          trb.completer.value().complete_ok(trb.trb);
        }
      }
      // Ensure that we've actually invoked the completions above
      // before moving to the next step.
      // TODO (fxbug.dev/44375): Migrate to joins
      RunUntilIdle();
      for (size_t i = 0; i < max_slots_; i++) {
        fbl::DoublyLinkedList<std::unique_ptr<TRBContext>> trbs;
        {
          fbl::AutoLock _(&device_state_[i].transaction_lock());
          trbs = device_state_[i].GetTransferRing().TakePendingTRBs();
        }
        for (auto& trb : trbs) {
          pending = true;
          trb.request->Complete(ZX_ERR_IO_NOT_PRESENT, 0);
        }
        for (size_t c = 0; c < 32; c++) {
          fbl::DoublyLinkedList<std::unique_ptr<TRBContext>> trbs;
          {
            fbl::AutoLock _(&device_state_[i].transaction_lock());
            trbs = device_state_[i].GetTransferRing(c).TakePendingTRBs();
          }
          for (auto& trb : trbs) {
            pending = true;
            trb.request->Complete(ZX_ERR_IO_NOT_PRESENT, 0);
          }
        }
      }
      // Flush any outstanding async I/O
      // TODO (fxbug.dev/44375): Migrate to joins
      RunUntilIdle();
    } while (pending);
    interrupters_.reset();
    transaction.Reply();
    return ZX_OK;
  });
}

void UsbXhci::DdkRelease() {
  int output;
  if (ddk_interaction_thread_.has_value()) {
    thrd_join(*ddk_interaction_thread_, &output);
  }
  if (init_thread_.has_value()) {
    thrd_join(*init_thread_, &output);
  }
  delete this;
}

// USB HCI protocol implementation.
void UsbXhci::UsbHciRequestQueue(usb_request_t* usb_request_,
                                 const usb_request_complete_t* complete_cb_) {
  Request request(usb_request_, *complete_cb_, sizeof(usb_request_t));

  if (!running_) {
    request.Complete(ZX_ERR_IO_NOT_PRESENT, 0);
    return;
  }
  if (request.request()->header.device_id >= params_.MaxSlots()) {
    request.Complete(ZX_ERR_INVALID_ARGS, 0);
    return;
  }
  {
    fbl::AutoLock _(&device_state_[request.request()->header.device_id].transaction_lock());
    if (!device_state_[request.request()->header.device_id].GetSlot()) {
      request.Complete(ZX_ERR_IO_NOT_PRESENT, 0);
      return;
    }
  }
  if (unlikely(request.request()->header.ep_address == 0)) {
    UsbHciControlRequestQueue(std::move(request));
  } else {
    UsbHciNormalRequestQueue(std::move(request));
  }
}

void UsbXhci::WaitForIsochronousReady(UsbRequestState* state) {
  // Cannot schedule more than 895 microseconds into the future per section 4.11.2.5
  // in the xHCI specification (revision 1.2)
  constexpr int kMaxSchedulingInterval = 895;
  if (state->context->request->request()->header.frame) {
    uint64_t frame = UsbHciGetCurrentFrame();
    while (static_cast<int32_t>(state->context->request->request()->header.frame - frame) >=
           kMaxSchedulingInterval) {
      uint32_t time =
          static_cast<uint32_t>((state->context->request->request()->header.frame - frame) -
                                kMaxSchedulingInterval) *
          1000;
      zx::nanosleep(zx::deadline_after(zx::msec(time)));
      frame = UsbHciGetCurrentFrame();
    }

    if (state->context->request->request()->header.frame < frame) {
      state->complete = true;
      state->status = ZX_ERR_IO;
      state->bytes_transferred = 0;
    }
  }
}

void UsbXhci::StartNormalTransaction(UsbRequestState* state) {
  size_t packet_count = 0;

  // Normal transfer
  zx_status_t status = state->context->request->PhysMap(bti_);
  if (status != ZX_OK) {
    state->complete = true;
    state->status = status;
    state->bytes_transferred = 0;
    return;
  }
  size_t pending_len = state->context->request->request()->header.length;
  uint32_t total_len = 0;
  for (auto [paddr, len] : state->context->request->phys_iter(0)) {
    if (len > pending_len) {
      len = pending_len;
    }
    if (!paddr) {
      break;
    }
    if (!len) {
      continue;
    }
    total_len += static_cast<uint32_t>(len);
    packet_count++;
    pending_len -= len;
  }

  if (pending_len) {
    // Something doesn't add up here....
    state->complete = true;
    state->status = ZX_ERR_BAD_STATE;
    state->bytes_transferred = 0;
    return;
  }
  // Allocate contiguous memory
  auto contig_trb_info = state->transfer_ring->AllocateContiguous(packet_count);
  if (contig_trb_info.is_error()) {
    state->complete = true;
    state->status = contig_trb_info.error_value();
    state->bytes_transferred = 0;
    return;
  }
  state->info = contig_trb_info.value();
  state->total_len = total_len;
  state->packet_count = packet_count;
  state->first_cycle = state->info.first()[0].status;
  state->first_trb = state->info.first().data();
  state->last_trb = state->info.trbs.data() + (packet_count - 1);
}

void UsbXhci::ContinueNormalTransaction(UsbRequestState* state) {
  // TODO(fxbug.dev/42611): Assign an interrupter dynamically from the pool
  // Data stage
  size_t pending_len = state->context->request->request()->header.length;
  auto current_nop = state->info.nop.data();
  if (current_nop) {
    while (Control::FromTRB(current_nop).Type() == Control::Nop) {
      bool producer_cycle_state = current_nop->status;
      bool cycle = (current_nop == state->first_trb) ? !producer_cycle_state : producer_cycle_state;
      Control::FromTRB(current_nop).set_Cycle(cycle).ToTrb(current_nop);
      current_nop->status = 0;
      current_nop++;
    }
  }
  if (state->first_trb) {
    TRB* current = state->info.trbs.data();
    for (auto [paddr, len] : state->context->request->phys_iter(0)) {
      if (!len) {
        break;
      }
      len = std::min(len, pending_len);
      pending_len -= len;
      state->packet_count--;
      TRB* next = current + 1;
      if (next == state->last_trb + 1) {
        next = nullptr;
      }
      uint32_t pcs = current->status;
      current->status = 0;
      enum Control::Type type;
      if (((state->is_isochronous_transfer) && state->first_trb == current)) {
        // Force direct mode as workaround for USB audio latency issue.
        type = Control::Isoch;
        Isoch* data = reinterpret_cast<Isoch*>(current);
        // Burst size is number of packets, not bytes
        uint32_t burst_size = state->burst_size;
        uint32_t packet_size = state->max_packet_size;
        uint32_t packet_count = state->total_len / packet_size;
        if (!packet_count) {
          packet_count = 1;
        }
        // Number of bursts - 1
        uint32_t burst_count = packet_count / burst_size;
        if (burst_count) {
          burst_count--;
        }
        // Zero-based last-burst-packet count (where 0 == 1 packet)
        uint32_t last_burst_packet_count = packet_count % burst_size;
        if (last_burst_packet_count) {
          last_burst_packet_count--;
        }
        data->set_CHAIN(next != nullptr)
            .set_SIA(state->context->request->request()->header.frame == 0)
            .set_TLBPC(last_burst_packet_count)
            .set_FrameID(
                static_cast<uint32_t>(state->context->request->request()->header.frame % 2048))
            .set_TBC(burst_count)
            .set_INTERRUPTER(0)
            .set_LENGTH(static_cast<uint16_t>(len))
            .set_SIZE(static_cast<uint32_t>(packet_count))
            .set_NO_SNOOP(!has_coherent_cache_)
            .set_IOC(next == nullptr)
            .set_ISP(true);
      } else {
        type = Control::Normal;
        Normal* data = reinterpret_cast<Normal*>(current);
        data->set_CHAIN(next != nullptr)
            .set_INTERRUPTER(0)
            .set_LENGTH(static_cast<uint16_t>(len))
            .set_SIZE(static_cast<uint32_t>(state->packet_count))
            .set_NO_SNOOP(!has_coherent_cache_)
            .set_IOC(next == nullptr)
            .set_ISP(true);
      }

      current->ptr = paddr;
      Control::FromTRB(current)
          .set_Cycle(unlikely(current == state->first_trb) ? !pcs : pcs)
          .set_Type(type)
          .ToTrb(current);
      current = next;
    }
  }
}

void UsbXhci::CommitNormalTransaction(UsbRequestState* state) {
  hw_mb();
  // Start the transaction!
  if (!has_coherent_cache_) {
    usb_request_cache_flush_invalidate(state->context->request->request(), 0,
                                       state->context->request->request()->header.length);
  }
  state->transfer_ring->AssignContext(state->last_trb, std::move(state->context), state->first_trb);
  Control::FromTRB(state->first_trb).set_Cycle(state->first_cycle).ToTrb(state->first_trb);

  state->transfer_ring->CommitTransaction(state->transaction);
  DOORBELL::Get(doorbell_offset_, state->slot)
      .FromValue(0)
      .set_Target(2 + state->index)
      .WriteTo(&mmio_.value());
}

bool UsbXhci::UsbRequestState::Complete() {
  if (complete) {
    context->request->Complete(status, bytes_transferred);
    return true;
  }
  return false;
}

void UsbXhci::UsbHciNormalRequestQueue(Request request) {
  UsbRequestState pending_transfer;
  uint8_t index = static_cast<uint8_t>(XhciEndpointIndex(request.request()->header.ep_address) - 1);
  auto& state = device_state_[request.request()->header.device_id];
  fbl::AutoLock transaction_lock(&state.transaction_lock());
  if (state.GetTransferRing(index).stalled()) {
    transaction_lock.release();
    request.Complete(ZX_ERR_IO_REFUSED, 0);
    return;
  }
  auto* control = reinterpret_cast<uint32_t*>(state.GetInputContext()->virt());
  size_t slot_size = (hcc_.CSZ()) ? 64 : 32;
  auto endpoint_context = reinterpret_cast<EndpointContext*>(
      reinterpret_cast<unsigned char*>(control) + (slot_size * (2 + (index + 1))));
  if (!state.GetTransferRing((index)).active()) {
    return;
  }
  pending_transfer.is_isochronous_transfer = state.GetTransferRing(index).IsIsochronous();
  pending_transfer.transfer_ring = &state.GetTransferRing(index);
  pending_transfer.burst_size = endpoint_context->MaxBurstSize() + 1;
  pending_transfer.max_packet_size = endpoint_context->MAX_PACKET_SIZE();
  pending_transfer.slot_size = (hcc_.CSZ()) ? 64 : 32;
  pending_transfer.complete = false;
  pending_transfer.index = index;
  pending_transfer.context = state.GetTransferRing(index).AllocateContext();
  pending_transfer.context->request = std::move(request);
  pending_transfer.slot =
      device_state_[pending_transfer.context->request->request()->header.device_id].GetSlot();
  if (!pending_transfer.context) {
    transaction_lock.release();
    pending_transfer.context->request->Complete(ZX_ERR_NO_MEMORY, 0);
    return;
  }

  if (pending_transfer.is_isochronous_transfer) {
    // Release the lock while we're sleeping to avoid blocking
    // other operations.
    state.transaction_lock().Release();
    WaitForIsochronousReady(&pending_transfer);
    if (pending_transfer.Complete()) {
      state.transaction_lock().Acquire();
      return;
    }
    state.transaction_lock().Acquire();
  }

  // Start the transaction
  pending_transfer.transaction = state.GetTransferRing(index).SaveState();
  auto rollback_transaction = [&]() __TA_NO_THREAD_SAFETY_ANALYSIS {
    state.GetTransferRing(index).Restore(pending_transfer.transaction);
  };
  StartNormalTransaction(&pending_transfer);
  if (pending_transfer.complete) {
    rollback_transaction();
    transaction_lock.release();
    pending_transfer.Complete();
    return;
  }
  // Continue the transaction
  ContinueNormalTransaction(&pending_transfer);
  if (pending_transfer.complete) {
    rollback_transaction();
    transaction_lock.release();
    pending_transfer.Complete();
    return;
  }
  // Commit the transaction -- starting the actual transfer
  CommitNormalTransaction(&pending_transfer);
}

void UsbXhci::UsbHciControlRequestQueue(Request req) {
  auto device_state = &device_state_[req.request()->header.device_id];
  fbl::AutoLock transaction_lock(&device_state->transaction_lock());
  if (device_state->GetTransferRing().stalled()) {
    transaction_lock.release();
    req.Complete(ZX_ERR_IO_REFUSED, 0);
    return;
  }
  auto context = device_state->GetTransferRing().AllocateContext();
  if (!context) {
    transaction_lock.release();
    req.Complete(ZX_ERR_NO_MEMORY, 0);
    return;
  }
  TransferRing::State transaction;
  TRB* setup;
  zx_status_t status = device_state->GetTransferRing().AllocateTRB(&setup, &transaction);
  auto rollback_transaction = [=]() __TA_NO_THREAD_SAFETY_ANALYSIS {
    device_state->GetTransferRing().Restore(transaction);
  };
  if (status != ZX_OK) {
    rollback_transaction();
    transaction_lock.release();
    req.Complete(status, 0);
    return;
  }

  context->request = std::move(req);
  UsbRequestState pending_transfer;
  pending_transfer.context = std::move(context);
  pending_transfer.setup = setup;
  pending_transfer.transaction = transaction;
  pending_transfer.transfer_ring = &device_state->GetTransferRing();
  pending_transfer.slot = device_state->GetSlot();
  ControlRequestAllocationPhase(&pending_transfer);
  fbl::AutoCall call([&]() __TA_NO_THREAD_SAFETY_ANALYSIS {
    rollback_transaction();
    transaction_lock.release();
    pending_transfer.Complete();
  });
  if (pending_transfer.complete) {
    return;
  }
  ControlRequestStatusPhase(&pending_transfer);
  if (pending_transfer.complete) {
    return;
  }
  ControlRequestDataPhase(&pending_transfer);
  if (pending_transfer.complete) {
    return;
  }
  ControlRequestSetupPhase(&pending_transfer);
  if (pending_transfer.complete) {
    return;
  }
  ControlRequestCommit(&pending_transfer);
  call.cancel();
}

void UsbXhci::ControlRequestAllocationPhase(UsbRequestState* state) {
  state->setup_cycle = state->setup->status;
  state->setup->status = 0;
  if (state->context->request->request()->header.length) {
    zx_status_t status = state->context->request->PhysMap(bti_);
    if (status != ZX_OK) {
      state->status = status;
      state->complete = true;
      state->bytes_transferred = 0;
      return;
    }
    TRB* current_trb = nullptr;
    for (auto [paddr, len] : state->context->request->phys_iter(0)) {
      if (!len) {
        break;
      }
      state->packet_count++;
      TRB* prev = current_trb;
      zx_status_t status = state->transfer_ring->AllocateTRB(&current_trb, nullptr);
      if (status != ZX_OK) {
        state->status = status;
        state->complete = true;
        state->bytes_transferred = 0;
        return;
      }
      static_assert(sizeof(TRB*) == sizeof(uint64_t));
      if (likely(prev)) {
        prev->ptr = reinterpret_cast<uint64_t>(current_trb);
      } else {
        state->first_trb = current_trb;
      }
    }
  }
}

void UsbXhci::ControlRequestStatusPhase(UsbRequestState* state) {
  // TODO(fxbug.dev/42611): Assign an interrupter dynamically from the pool
  state->interrupter = 0;
  bool status_in = true;
  // See table 4-7 in section 4.11.2.2
  if (state->first_trb &&
      (state->context->request->request()->setup.bm_request_type & USB_DIR_IN)) {
    status_in = false;
  }
  zx_status_t status = state->transfer_ring->AllocateTRB(&state->status_trb_ptr, nullptr);
  if (status != ZX_OK) {
    state->status = status;
    state->complete = true;
    state->bytes_transferred = 0;
    return;
  }
  Control::FromTRB(state->status_trb_ptr)
      .set_Cycle(state->status_trb_ptr->status)
      .set_Type(Control::Status)
      .ToTrb(state->status_trb_ptr);
  state->status_trb_ptr->status = 0;
  auto* status_trb = static_cast<Status*>(state->status_trb_ptr);
  status_trb->set_DIRECTION(status_in).set_INTERRUPTER(state->interrupter).set_IOC(1);
}

void UsbXhci::ControlRequestDataPhase(UsbRequestState* state) {
  // Data stage
  if (state->first_trb) {
    TRB* current = state->first_trb;
    for (auto [paddr, len] : state->context->request->phys_iter(0)) {
      if (!len) {
        break;
      }
      state->packet_count--;
      TRB* next = reinterpret_cast<TRB*>(current->ptr);
      uint32_t pcs = current->status;
      current->status = 0;
      enum Control::Type type;
      if (current == state->first_trb) {
        type = Control::Data;
        ControlData* data = reinterpret_cast<ControlData*>(current);
        // Control transfers always get interrupter 0 (we consider those to be low-priority)
        // TODO (fxbug.dev/34068): Change bus snooping options based on input from higher-level
        // drivers.
        data->set_CHAIN(next != nullptr)
            .set_DIRECTION(
                (state->context->request->request()->setup.bm_request_type & USB_DIR_IN) != 0)
            .set_INTERRUPTER(0)
            .set_LENGTH(static_cast<uint16_t>(len))
            .set_SIZE(static_cast<uint32_t>(state->packet_count))
            .set_ISP(true)
            .set_NO_SNOOP(!has_coherent_cache_);
      } else {
        type = Control::Normal;
        Normal* data = reinterpret_cast<Normal*>(current);
        data->set_CHAIN(next != nullptr)
            .set_INTERRUPTER(0)
            .set_LENGTH(static_cast<uint16_t>(len))
            .set_SIZE(static_cast<uint32_t>(state->packet_count))
            .set_ISP(true)
            .set_NO_SNOOP(!has_coherent_cache_);
      }
      current->ptr = paddr;
      Control::FromTRB(current).set_Cycle(pcs).set_Type(type).ToTrb(current);
      current = next;
    }
  }
}

void UsbXhci::ControlRequestSetupPhase(UsbRequestState* state) {
  // Setup phase (4.11.2.2)
  memcpy(&state->setup->ptr, &state->context->request->request()->setup,
         sizeof(state->context->request->request()->setup));
  Setup* setup_trb = reinterpret_cast<Setup*>(state->setup);
  setup_trb->set_INTERRUPTER(state->interrupter)
      .set_length(8)
      .set_IDT(1)
      .set_TRT(((state->context->request->request()->setup.bm_request_type & USB_DIR_IN) != 0)
                   ? Setup::IN
                   : Setup::OUT);
  hw_mb();
}

void UsbXhci::ControlRequestCommit(UsbRequestState* state) {
  // Start the transaction!
  if (!has_coherent_cache_) {
    usb_request_cache_flush_invalidate(state->context->request->request(), 0,
                                       state->context->request->request()->header.length);
  }
  state->transfer_ring->AssignContext(state->status_trb_ptr, std::move(state->context),
                                      state->first_trb);
  Control::FromTRB(state->setup)
      .set_Type(Control::Setup)
      .set_Cycle(state->setup_cycle)
      .ToTrb(state->setup);
  state->transfer_ring->CommitTransaction(state->transaction);
  DOORBELL::Get(doorbell_offset_, state->slot).FromValue(0).set_Target(1).WriteTo(&mmio_.value());
}

void UsbXhci::UsbHciSetBusInterface(const usb_bus_interface_protocol_t* bus_intf) {
  // We must be unbinding if the bus is currently valid.
  if (bus_.is_valid()) {
    // Assert that we've started unbinding and are no longer accepting
    // any requests to prevent a use-after-free situation.
    ZX_ASSERT(!running_);
    return;
  }
  ZX_ASSERT(bus_intf != nullptr);
  bus_ = bus_intf;
  sync_completion_signal(&bus_completion);
}
size_t UsbXhci::UsbHciGetMaxDeviceCount() {
  // Last two slots represent the virtual hubs (USB 2.0 and 3.0 respectively)
  return params_.MaxSlots() + 2;
}

zx_status_t UsbXhci::UsbHciEnableEndpoint(uint32_t device_id,
                                          const usb_endpoint_descriptor_t* ep_desc,
                                          const usb_ss_ep_comp_descriptor_t* ss_com_desc,
                                          bool enable) {
  if (!running_) {
    return ZX_ERR_IO_NOT_PRESENT;
  }
  if (device_id >= params_.MaxSlots()) {
    // TODO: Root hub endpoint support
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (!enable) {
    return RunSynchronously(UsbHciDisableEndpoint(device_id, ep_desc, ss_com_desc));
  }
  return RunSynchronously(UsbHciEnableEndpoint(device_id, ep_desc, ss_com_desc));
}

TRBPromise UsbXhci::UsbHciEnableEndpoint(uint32_t device_id,
                                         const usb_endpoint_descriptor_t* ep_desc,
                                         const usb_ss_ep_comp_descriptor_t* ss_com_desc) {
  auto context = command_ring_.AllocateContext();
  auto state = &device_state_[device_id];
  SlotContext* slot_context;
  TRB trb;
  uint32_t context_entries;
  uint8_t index;
  {
    fbl::AutoLock _(&state->transaction_lock());
    auto control = reinterpret_cast<uint32_t*>(state->GetInputContext()->virt());
    size_t slot_size = (hcc_.CSZ()) ? 64 : 32;
    // Initialize input slot context data structure (6.2.2) with 1 context entry
    // Set root hub port number to port number and context entries to 1
    slot_context =
        reinterpret_cast<SlotContext*>(reinterpret_cast<unsigned char*>(control) + slot_size);
    context_entries = slot_context->CONTEXT_ENTRIES();
    index = XhciEndpointIndex(ep_desc->bEndpointAddress);
    if (index >= context_entries) {
      slot_context->set_CONTEXT_ENTRIES(index + 1);
    }
    // Allocate the transfer ring (see section 4.9)
    // TODO (bbosak): Assign an Interrupter from the pool
    control[0] = 0;
    control[1] = 1 | (1 << (index + 1));
    // TODO(bbosak): Dynamically assign an event ring
    uint32_t event_ring = 0;
    zx_status_t status = state->GetTransferRing(index - 1).Init(
        page_size_, bti_, &this->interrupters_[event_ring].ring(), is_32bit_, &mmio_.value(),
        *this);
    if (status != ZX_OK) {
      return fit::make_error_promise(status);
    }
    CRCR trb_phys = state->GetTransferRing(index - 1).phys(cap_length_);
    // Initialize endpoint context 0
    // Set CERR to 3, TR dequeue pointer, max packet size, EP type = control, DCS = 1
    auto endpoint_context = reinterpret_cast<EndpointContext*>(
        reinterpret_cast<unsigned char*>(control) + (slot_size * (2 + index)));

    // See section 4.3.6
    uint32_t ep_type = ep_desc->bmAttributes & USB_ENDPOINT_TYPE_MASK;
    if (ep_type == USB_ENDPOINT_ISOCHRONOUS) {
      state->GetTransferRing(index - 1).SetIsochronous();
    }
    uint32_t ep_index = ep_type;
    if ((ep_desc->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_ENDPOINT_IN) {
      ep_index += 4;
    }
    endpoint_context->Init(static_cast<EndpointContext::EndpointType>(ep_index), trb_phys,
                           ep_desc->wMaxPacketSize & 0x07FF);
    int interval = ComputeInterval(ep_desc, slot_context->SPEED());
    if (interval == -1) {
      interval = 1;
    }
    endpoint_context->set_Interval(interval);
    // Section 6.2.3.4
    uint32_t max_burst = 0;
    if (ss_com_desc) {
      max_burst = ss_com_desc->b_max_burst;
    } else {
      // TODO: Handle special case for interrupt/isochronous endpoints
      if ((slot_context->SPEED() == USB_SPEED_HIGH) && (ep_type == USB_ENDPOINT_ISOCHRONOUS)) {
        max_burst = (le16toh((ep_desc)->wMaxPacketSize) >> 11) & 3;
      }
    }
    endpoint_context->set_MaxBurstSize(max_burst);
    if (ep_type == USB_ENDPOINT_ISOCHRONOUS) {
      endpoint_context->set_MAX_ESIT_PAYLOAD_LOW((ep_desc->wMaxPacketSize & 0x07FF) * max_burst);
    }
    trb.ptr = state->GetInputContext()->phys()[0];
    Control::Get()
        .FromValue((device_id + 1) << 24)
        .set_Type(Control::ConfigureEndpointCommand)
        .ToTrb(&trb);
  }
  // TODO (fxbug.dev/34140): Implement async support
  hw_mb();
  return SubmitCommand(trb, std::move(context))
      .then([=](fit::result<TRB*, zx_status_t>& result) {
        fbl::AutoCall free_buffers([=]() {
          fbl::AutoLock _(&state->transaction_lock());
          state->GetTransferRing(index - 1).Deinit();
          slot_context->set_CONTEXT_ENTRIES(context_entries);
        });
        if (result.is_error()) {
          return result;
        }
        auto completion = static_cast<CommandCompletionEvent*>(result.value());
        bool success = completion->CompletionCode() == CommandCompletionEvent::Success;
        if (success) {
          free_buffers.cancel();
        } else {
          return fit::result<TRB*, zx_status_t>(fit::error(ZX_ERR_IO));
        }
        return result;
      })
      .box();
}

TRBPromise UsbXhci::UsbHciDisableEndpoint(uint32_t device_id,
                                          const usb_endpoint_descriptor_t* ep_desc,
                                          const usb_ss_ep_comp_descriptor_t* ss_com_desc) {
  auto context = command_ring_.AllocateContext();
  auto state = &device_state_[device_id];
  size_t slot_size = (hcc_.CSZ()) ? 64 : 32;
  uint8_t index = XhciEndpointIndex(ep_desc->bEndpointAddress);
  TRB trb;
  uint32_t* control;
  {
    fbl::AutoLock _(&state->transaction_lock());
    control = reinterpret_cast<uint32_t*>(state->GetInputContext()->virt());
    // Initialize input slot context data structure (6.2.2) with 1 context entry
    // Set root hub port number to port number and context entries to 1
    control[0] = (1 << (index + 1));
    control[1] = 1;
    trb.ptr = state->GetInputContext()->phys()[0];
    Control::Get()
        .FromValue((device_id + 1) << 24)
        .set_Type(Control::ConfigureEndpointCommand)
        .ToTrb(&trb);
  }
  // TODO (fxbug.dev/34140): Implement async support
  hw_mb();
  return SubmitCommand(trb, std::move(context))
      .then([=](fit::result<TRB*, zx_status_t>& result) -> fit::result<TRB*, zx_status_t> {
        if (result.is_error()) {
          return fit::error(ZX_ERR_BAD_STATE);
        }
        auto completion = reinterpret_cast<CommandCompletionEvent*>(result.value());
        bool success = completion->CompletionCode() == CommandCompletionEvent::Success;
        if (!success) {
          return fit::error(ZX_ERR_BAD_STATE);
        }
        auto endpoint_context = reinterpret_cast<EndpointContext*>(
            reinterpret_cast<unsigned char*>(control) + (slot_size * (2 + index)));
        endpoint_context->Deinit();
        fbl::AutoLock _(&state->transaction_lock());
        zx_status_t status = state->GetTransferRing(index - 1).Deinit();
        // If we can't deinit the ring something is seriously wrong.
        if (status != ZX_OK) {
          return fit::error(ZX_ERR_BAD_STATE);
        }
        return result;
      })
      .box();
}

uint64_t UsbXhci::UsbHciGetCurrentFrame() {
  if (!running_) {
    return 0;
  }
  uint32_t mfindex = MFINDEX::Get(runtime_offset_).ReadFrom(&mmio_.value()).INDEX();
  if (mfindex < last_mfindex_) {
    // Wrapped
    wrap_count_++;
  }

  last_mfindex_ = mfindex;
  uint64_t wrap_count = wrap_count_;
  // shift three to convert from 125us microframes to 1ms frames
  return ((wrap_count * (1 << 14)) + mfindex) >> 3;
}

zx_status_t UsbXhci::UsbHciConfigureHub(uint32_t device_id, usb_speed_t speed,
                                        const usb_hub_descriptor_t* desc, bool multi_tt) {
  if (!running_) {
    return ZX_ERR_IO_NOT_PRESENT;
  }
  sync_completion_t completion;
  zx_status_t hub_status = ZX_OK;
  ScheduleTask(ConfigureHubAsync(device_id, speed, desc, multi_tt)
                   .then([&](fit::result<TRB*, zx_status_t>& result) {
                     if (result.is_ok()) {
                       hub_status = ZX_OK;
                     } else {
                       hub_status = result.error();
                     }
                     sync_completion_signal(&completion);
                     return result;
                   })
                   .box());
  sync_completion_wait(&completion, ZX_TIME_INFINITE);
  return hub_status;
}
zx_status_t UsbXhci::UsbHciHubDeviceAdded(uint32_t device_id, uint32_t port, usb_speed_t speed) {
  if (!running_) {
    return ZX_ERR_IO_NOT_PRESENT;
  }
  sync_completion_t completion;
  zx_status_t out_status;
  ScheduleTask(UsbHciHubDeviceAddedAsync(device_id, port, speed)
                   .then([&](fit::result<TRB*, zx_status_t>& result) {
                     if (result.is_ok()) {
                       out_status = ZX_OK;
                     } else {
                       out_status = result.error();
                     }
                     sync_completion_signal(&completion);
                     return result;
                   })
                   .box());
  sync_completion_wait(&completion, ZX_TIME_INFINITE);
  return ZX_OK;
}

zx_status_t UsbXhci::UsbHciHubDeviceRemoved(uint32_t hub_id, uint32_t port) {
  if (!running_) {
    return ZX_ERR_IO_NOT_PRESENT;
  }
  auto hub_state = &device_state_[hub_id];
  uint32_t slot;
  {
    fbl::AutoLock _(&hub_state->transaction_lock());
    // In the case where the hub itself is unplugged,
    // we will likely have torn down the hub state
    // prior to teardown of devices connected to said hub.
    // If this is the case, just return OK.
    // Teardown of child devices will complete asynchronously
    if (!hub_state->GetHubLocked()) {
      return ZX_OK;
    }
    uint32_t device_id = hub_state->GetHubLocked()->port_to_device[port - 1];
    auto device_state = &device_state_[device_id];
    slot = device_state->GetSlot();
  }
  bool success = false;
  sync_completion_t event;
  for (size_t i = 0; i < 32; i++) {
    fbl::DoublyLinkedList<std::unique_ptr<TRBContext>> trbs;
    {
      fbl::AutoLock _(&device_state_[slot - 1].transaction_lock());
      trbs = device_state_[slot - 1].GetTransferRing(i).TakePendingTRBs();
    }
    for (auto& trb : trbs) {
      trb.request->Complete(ZX_ERR_IO_NOT_PRESENT, 0);
    }
  }
  RunUntilIdle();
  fbl::DoublyLinkedList<std::unique_ptr<TRBContext>> trbs;
  {
    fbl::AutoLock _(&device_state_[slot - 1].transaction_lock());
    trbs = device_state_[slot - 1].GetTransferRing().TakePendingTRBs();
  }
  for (auto& trb : trbs) {
    trb.request->Complete(ZX_ERR_IO_NOT_PRESENT, 0);
  }
  RunUntilIdle();
  // Bus should always be valid since we're getting a callback from a hub
  // that is a child of the bus.
  ZX_ASSERT(bus_.is_valid());
  zx_status_t status = bus_.RemoveDevice(slot - 1);
  if (status != ZX_OK) {
    return status;
  }
  ScheduleTask(DisableSlotCommand(slot)
                   .then([&](fit::result<TRB*, zx_status_t>& result) {
                     if (result.is_error()) {
                       success = false;
                       return result;
                     }
                     auto completion = static_cast<CommandCompletionEvent*>(result.value());
                     success = completion->CompletionCode() == CommandCompletionEvent::Success;
                     sync_completion_signal(&event);
                     return result;
                   })
                   .box());
  sync_completion_wait(&event, ZX_TIME_INFINITE);
  return success ? ZX_OK : ZX_ERR_IO;
}

zx_status_t UsbXhci::UsbHciHubDeviceReset(uint32_t device_id, uint32_t port) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t UsbXhci::UsbHciResetEndpoint(uint32_t device_id, uint8_t ep_address) {
  return RunSynchronously(UsbHciResetEndpointAsync(device_id, ep_address));
}

TRBPromise UsbXhci::UsbHciResetEndpointAsync(uint32_t device_id, uint8_t ep_address) {
  if (device_id >= params_.MaxSlots()) {
    return fit::make_error_promise(ZX_ERR_NOT_SUPPORTED);
  }
  auto state = &device_state_[device_id];
  uint8_t index = XhciEndpointIndex(ep_address) - 1;
  ResetEndpoint reset_command;
  {
    fbl::AutoLock _(&state->transaction_lock());
    reset_command.set_ENDPOINT(XhciEndpointIndex(ep_address) + 1);
    reset_command.set_SLOT(state->GetSlot());
  }
  auto context = command_ring_.AllocateContext();
  if (!context) {
    return fit::make_error_promise(ZX_ERR_NO_MEMORY);
  }

  TransferRing* ring;
  {
    fbl::AutoLock l(&state->transaction_lock());
    if (ep_address == 0) {
      ring = &state->GetTransferRing();
      index = 0;
    } else {
      ring = &state->GetTransferRing(index);
    }
    if (!ring->stalled()) {
      return fit::make_error_promise(ZX_ERR_INVALID_ARGS);
    }
  }
  return SubmitCommand(reset_command, std::move(context))
      .then([=](fit::result<TRB*, zx_status_t>& result) {
        if (result.is_error()) {
          return fit::make_result_promise(result);
        }
        CommandCompletionEvent* evt = static_cast<CommandCompletionEvent*>(result.value());
        if (evt->CompletionCode() != CommandCompletionEvent::Success) {
          return fit::make_error_promise(ZX_ERR_IO);
        }
        return fit::make_result_promise(result);
      })
      .and_then([=](TRB*& trb) -> TRBPromise {
        SetTRDequeuePointer cmd;
        cmd.set_ENDPOINT(XhciEndpointIndex(ep_address) + 1);
        cmd.set_SLOT(state->GetSlot());
        auto res = ring->PeekCommandRingControlRegister(CapLength());
        if (res.is_error()) {
          return fit::make_error_promise(res.error_value());
        }
        cmd.SetPtr(res.value());
        auto context = command_ring_.AllocateContext();
        return SubmitCommand(cmd, std::move(context))
            .and_then([=](TRB*& result) {
              CommandCompletionEvent* evt = static_cast<CommandCompletionEvent*>(result);
              if (evt->CompletionCode() != CommandCompletionEvent::Success) {
                return fit::make_error_promise(ZX_ERR_IO);
              }
              fbl::AutoLock l(&state->transaction_lock());
              ring->set_stall(false);
              return fit::make_ok_promise(result);
            })
            .box();
      })
      .box();
}

// TODO (fxbug.dev/34637): Either decide what these reset methods should do,
// or get rid of them.
zx_status_t UsbXhci::UsbHciResetDevice(uint32_t hub_address, uint32_t device_id) {
  return ZX_ERR_NOT_SUPPORTED;
}

size_t UsbXhci::UsbHciGetMaxTransferSize(uint32_t device_id, uint8_t ep_address) {
  if (device_id >= params_.MaxSlots()) {
    // TODO: Root hub endpoint support
    return 0;
  }
  auto state = &device_state_[device_id];

  fbl::AutoLock _(&state->transaction_lock());
  auto control = reinterpret_cast<uint32_t*>(state->GetInputContext()->virt());
  size_t slot_size = (hcc_.CSZ()) ? 64 : 32;
  uint8_t index = XhciEndpointIndex(ep_address);
  auto endpoint_context = reinterpret_cast<EndpointContext*>(
      reinterpret_cast<unsigned char*>(control) + (slot_size * (2 + index)));
  return endpoint_context->MAX_PACKET_SIZE();
}

zx_status_t UsbXhci::UsbHciCancelAll(uint32_t device_id, uint8_t ep_address) {
  return RunSynchronously(UsbHciCancelAllAsync(device_id, ep_address));
}

TRBPromise UsbXhci::UsbHciCancelAllAsync(uint32_t device_id, uint8_t ep_address) {
  auto state = &device_state_[device_id];
  StopEndpoint stop;
  {
    fbl::AutoLock state_lock(&state->transaction_lock());
    uint8_t index = static_cast<uint8_t>(XhciEndpointIndex(ep_address) + 1);
    stop.set_ENDPOINT(index);
    stop.set_SLOT(state->GetSlot());
  }
  auto context = command_ring_.AllocateContext();
  return SubmitCommand(stop, std::move(context))
      .then([=](fit::result<TRB*, zx_status_t>& result) -> TRBPromise {
        if (result.is_error()) {
          return fit::make_result_promise(result);
        }
        auto completion_event = static_cast<CommandCompletionEvent*>(result.value());
        auto completion_code = completion_event->CompletionCode();
        zx_status_t status =
            (completion_code == CommandCompletionEvent::Success) ? ZX_OK : ZX_ERR_IO;
        if (status != ZX_OK) {
          return fit::make_error_promise(status);
        }
        // We can now move everything off of the transfer ring starting at the dequeue pointer
        uint8_t index;
        fbl::DoublyLinkedList<std::unique_ptr<TRBContext>> trbs;
        zx_paddr_t new_ptr_phys = 0;
        {
          TRB* new_ptr = nullptr;
          fbl::AutoLock _(&state->transaction_lock());
          index = static_cast<uint8_t>(XhciEndpointIndex(ep_address) - 1);
          if (!state->GetTransferRing(index).active()) {
            return fit::make_error_promise(ZX_ERR_IO_NOT_PRESENT);
          }
          trbs = state->GetTransferRing(index).TakePendingTRBs();
          for (auto& trb : trbs) {
            new_ptr = trb.trb;
            auto control = Control::FromTRB(trb.trb);
            control.set_Cycle(!control.Cycle());
          }
          if (new_ptr) {
            new_ptr_phys = state->GetTransferRing(index).VirtToPhys(new_ptr + 1);
          }
        }
        for (auto& trb : trbs) {
          trb.request->Complete(ZX_ERR_CANCELED, 0);
        }
        // It's possible that the dequeue pointer was in the middle of a multi-TRB TD when we
        // stopped. If this is the case, we need to adjust the dequeue pointer to point to the index
        // of the first TRB that we know about.
        if (new_ptr_phys) {
          SetTRDequeuePointer cmd;
          cmd.set_ENDPOINT(index + 2);
          cmd.set_SLOT(state->GetSlot());
          cmd.ptr = new_ptr_phys;
          auto context = command_ring_.AllocateContext();
          return SubmitCommand(cmd, std::move(context))
              .then([=](fit::result<TRB*, zx_status_t>& result) -> fit::result<TRB*, zx_status_t> {
                if (result.is_error()) {
                  return result;
                }
                auto completion_event = static_cast<CommandCompletionEvent*>(result.value());
                auto completion_code = completion_event->CompletionCode();
                bool command_success = completion_code == CommandCompletionEvent::Success;
                zx_status_t status = command_success ? ZX_OK : ZX_ERR_IO;
                if (status == ZX_OK) {
                  return fit::ok(result.value());
                } else {
                  return fit::error(status);
                }
              });
        } else {
          return fit::make_ok_promise(result.value());
        }
      })
      .box();
}

size_t UsbXhci::UsbHciGetRequestSize() { return Request::RequestSize(sizeof(usb_request_t)); }

void UsbXhci::Shutdown(zx_status_t status) {
  USBCMD::Get(cap_length_).ReadFrom(&mmio_.value()).set_ENABLE(0).WriteTo(&mmio_.value());
  while (!USBSTS::Get(cap_length_).ReadFrom(&mmio_.value()).HCHalted()) {
  }
  if (status != ZX_OK) {
    // If we're shutting down due to an error (not just regular unbind)
    // ensure that
    DdkAsyncRemove();
  }
}

void UsbXhci::InitQuirks() {
  pcie_device_info_t info;
  pci_.GetDeviceInfo(&info);
  if ((info.vendor_id == 0x1033) && (info.device_id == 0x194)) {
    qemu_quirk_ = true;
  }
  if ((info.vendor_id) == 0x8086 && (info.device_id == 0x8C31)) {
    // TODO (bbosak): Implement stub EHCI driver so we can properly
    // do the handoff in case the BIOS is managing a device on EHCI.
    // Quirk for some older Intel chipsets
    // Switch ports from EHCI to XHCI.
    uint32_t ports_available;
    pci_.ConfigRead32(0xdc, &ports_available);
    if (ports_available) {
      pci_.ConfigWrite32(0xd8, ports_available);
    }
    // Route power and data lines for USB 2.0 ports
    pci_.ConfigRead32(0xd4, &ports_available);
    if (ports_available) {
      pci_.ConfigWrite32(0xD0, ports_available);
    }
    // Handoff takes 5 seconds if we're contending with the EHCI controller.
    // (have to wait for enumeration to time out)
    sleep(5);
  }
}

zx_status_t UsbXhci::InitPci() {
  // Perform vendor-specific workarounds.
  InitQuirks();
  // PCIe interface supports cache snooping
  has_coherent_cache_ = true;
  // Initialize MMIO
  std::optional<ddk::MmioBuffer> buffer;
  zx_status_t status = pci_.MapMmio(0, ZX_CACHE_POLICY_UNCACHED_DEVICE, &buffer);
  if (status != ZX_OK) {
    return status;
  }
  mmio_ = std::move(*buffer);
  // TODO (bbosak): Convert to MSI-X when we have kernel support
  // for it (see kpci.c TODO).
  uint32_t irq_count;
  status = pci_.QueryIrqMode(ZX_PCIE_IRQ_MODE_MSI, &irq_count);
  if (status != ZX_OK) {
    return status;
  }
  if (pci_.SetIrqMode(ZX_PCIE_IRQ_MODE_MSI, irq_count) != ZX_OK) {
    return status;
  }
  irq_count_ = irq_count;
  fbl::AllocChecker ac;
  interrupters_.reset(new (&ac) Interrupter[irq_count]);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  for (uint32_t i = 0; i < irq_count; i++) {
    status = pci_.MapInterrupt(i, &interrupters_[i].GetIrq());
    if (status != ZX_OK) {
      return status;
    }
  }
  status = pci_.EnableBusMaster(true);
  if (status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

zx_status_t UsbXhci::InitMmio() {
  if (!pdev_.is_valid()) {
    return 0;
  }
  std::optional<ddk::MmioBuffer> mmio;
  if (pdev_.MapMmio(0, &mmio) != ZX_OK) {
    return 0;
  }
  mmio_ = std::move(*mmio);
  uint32_t irq_count = 1;
  irq_count_ = irq_count;
  fbl::AllocChecker ac;
  interrupters_.reset(new (&ac) Interrupter[irq_count]);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  for (uint32_t i = 0; i < irq_count; i++) {
    if (pdev_.GetInterrupt(i, &interrupters_[i].GetIrq()) != ZX_OK) {
      return 0;
    }
  }
  return 0;
}

void UsbXhci::BiosHandoff() {
  HCCPARAMS1 hcc = HCCPARAMS1::Get().ReadFrom(&mmio_.value());
  if (hcc.ReadFrom(&mmio_.value()).xECP()) {
    XECP current = XECP::Get(hcc).ReadFrom(&mmio_.value());
    while (true) {
      if (current.ID() == XECP::UsbLegacySupport) {
        current.set_reg_value(current.reg_value() | 1 << 24).WriteTo(&mmio_.value());
        while ((current = current.ReadFrom(&mmio_.value())).reg_value() & 1 << 16) {
        };
      }
      if (!current.NEXT()) {
        break;
      }
      current = current.Next().ReadFrom(&mmio_.value());
    }
  }
}

void UsbXhci::ResetController() {
  USBCMD::Get(cap_length_).ReadFrom(&mmio_.value()).set_ENABLE(0).WriteTo(&mmio_.value());
  while (!USBSTS::Get(cap_length_).ReadFrom(&mmio_.value()).HCHalted()) {
    zx::nanosleep(zx::deadline_after(zx::msec(1)));
  }
  while (USBSTS::Get(cap_length_).ReadFrom(&mmio_.value()).CNR()) {
    zx::nanosleep(zx::deadline_after(zx::msec(1)));
  }
  USBCMD::Get(cap_length_).ReadFrom(&mmio_.value()).set_RESET(1).WriteTo(&mmio_.value());
  while (USBCMD::Get(cap_length_).ReadFrom(&mmio_.value()).RESET()) {
    zx::nanosleep(zx::deadline_after(zx::msec(1)));
  }
  while (USBSTS::Get(cap_length_).ReadFrom(&mmio_.value()).CNR()) {
    zx::nanosleep(zx::deadline_after(zx::msec(1)));
  }
}

int UsbXhci::InitThread() {
  ZX_ASSERT(init_txn_.has_value());  // This is set in DdkInit before creating this thread.

  fbl::AutoCall call([=]() { init_txn_->Reply(ZX_ERR_INTERNAL); });
  fbl::AutoCall init_completer([=]() { sync_completion_signal(&init_complete_); });
  // Initialize either the PCI or MMIO structures first
  if (pci_.is_valid()) {
    zx_status_t status = InitPci();
    if (status != 0) {
      zxlogf(ERROR, "PCI initialization failed with code %i", (int)status);
      return status;
    }
  } else {
    int status = InitMmio();
    if (status != 0) {
      zxlogf(ERROR, "MMIO initialization failed with code %i", (int)status);
      return status;
    }
  }
  // Perform the BIOS handoff if necessary
  BiosHandoff();
  // At startup the device is in an unknown state
  // Reset the xHCI to place everything in its well-defined
  // initial state.
  uint8_t cap_length = CapLength::Get().ReadFrom(&mmio_.value()).Length();
  cap_length_ = cap_length;
  // Perform xHCI reset process
  ResetController();
  // Start DDK interaction thread
  thrd_t thrd;
  int thread_status = thrd_create_with_name(
      &thrd,
      [](void* ctx) {
        auto hci = static_cast<UsbXhci*>(ctx);
        hci->ddk_interaction_loop_.Run();
        return 0;
      },
      this, "ddk_interaction_thread");
  if (thread_status != thrd_success) {
    return thread_status;
  }
  ddk_interaction_thread_ = thrd;
  // Finish HCI initialization
  zx_status_t status = HciFinalize();
  if (status != ZX_OK) {
    zxlogf(ERROR, "xHCI initialization failed with code %i", (int)status);
    return status;
  }
  // If |HciFinalize| succeeded, it would have replied to |init_txn_| and made the device visible.
  call.cancel();
  return 0;
}

zx_status_t UsbXhci::HciFinalize() {
  hcc_ = HCCPARAMS1::Get().ReadFrom(&mmio_.value());
  HCSPARAMS1 hcsparams1 = HCSPARAMS1::Get().ReadFrom(&mmio_.value());
  is_32bit_ = !hcc_.AC64();
  params_ = hcsparams1;
  CONFIG::Get(cap_length_)
      .ReadFrom(&mmio_.value())
      .set_MaxSlotsEn(hcsparams1.MaxSlots())
      .WriteTo(&mmio_.value());
  {
    zx::bti bti;
    if (pci_.is_valid()) {
      if (pci_.GetBti(0, &bti) != ZX_OK) {
        return ZX_ERR_INTERNAL;
      }
    } else {
      if (pdev_.GetBti(0, &bti) != ZX_OK) {
        return ZX_ERR_INTERNAL;
      }
    }
    bti_ = std::move(bti);
  }
  uint32_t page_size = USB_PAGESIZE::Get(cap_length_).ReadFrom(&mmio_.value()).PageSize() << 12;
  page_size_ = page_size;
  // TODO (bbosak): Correct this to use variable alignment when we get kernel
  // support for this.
  if (page_size != ZX_PAGE_SIZE) {
    return ZX_ERR_INTERNAL;
  }
  uint32_t align_log2 = 0;
  if (buffer_factory_->CreatePaged(bti_, ZX_PAGE_SIZE, false, &dcbaa_buffer_) != ZX_OK) {
    return ZX_ERR_INTERNAL;
  }
  if (is_32bit_ && (dcbaa_buffer_->phys()[0] >= UINT32_MAX)) {
    return ZX_ERR_INTERNAL;
  }
  dcbaa_ = static_cast<uint64_t*>(dcbaa_buffer_->virt());
  fbl::AllocChecker ac;
  HCSPARAMS2 hcsparams2 = HCSPARAMS2::Get().ReadFrom(&mmio_.value());
  RuntimeRegisterOffset offset = RuntimeRegisterOffset::Get().ReadFrom(&mmio_.value());
  runtime_offset_ = offset;
  uint32_t buffers = hcsparams2.MAX_SCRATCHPAD_BUFFERS_LOW() |
                     ((hcsparams2.MAX_SCRATCHPAD_BUFFERS_HIGH() << 5) + 1);
  scratchpad_buffers_ = std::make_unique<std::unique_ptr<dma_buffer::ContiguousBuffer>[]>(buffers);
  if (fbl::round_up(buffers * sizeof(uint64_t), ZX_PAGE_SIZE) > ZX_PAGE_SIZE) {
    // We can't create multi-page contiguously physical uncached buffers.
    // This is presently not supported in the kernel.
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (buffer_factory_->CreatePaged(bti_, ZX_PAGE_SIZE, false, &scratchpad_buffer_array_) != ZX_OK) {
    return ZX_ERR_INTERNAL;
  }
  if (is_32bit_ && (scratchpad_buffer_array_->phys()[0] >= UINT32_MAX)) {
    return ZX_ERR_INTERNAL;
  }
  uint64_t* scratchpad_buffer_array = static_cast<uint64_t*>(scratchpad_buffer_array_->virt());
  for (size_t i = 0; i < buffers - 1; i++) {
    if (buffer_factory_->CreateContiguous(bti_, page_size, align_log2, &scratchpad_buffers_[i]) !=
        ZX_OK) {
      return ZX_ERR_INTERNAL;
    }
    if (is_32bit_ && (scratchpad_buffers_[i]->phys() >= UINT32_MAX)) {
      return ZX_ERR_INTERNAL;
    }
    scratchpad_buffer_array[i] = scratchpad_buffers_[i]->phys();
  }
  static_cast<uint64_t*>(dcbaa_buffer_->virt())[0] = scratchpad_buffer_array_->phys()[0];
  max_slots_ = hcsparams1.MaxSlots();
  device_state_.reset(new (&ac) DeviceState[max_slots_]);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  port_state_.reset(new (&ac) PortState[hcsparams1.MaxPorts()]);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  hw_mb();
  DCBAAP::Get(cap_length_).FromValue(0).set_PTR(dcbaa_buffer_->phys()[0]).WriteTo(&mmio_.value());
  // Initialize command ring
  doorbell_offset_ = DoorbellOffset::Get().ReadFrom(&mmio_.value());
  // Interrupt moderation interval == 30 microseconds (optimal value derived from scheduler trace)
  // TODO: Change this based on P state (performance states) for power management
  IMODI::Get(offset, 0).ReadFrom(&mmio_.value()).set_MODI(240).WriteTo(&mmio_.value());
  if (interrupters_[0].ring().Init(
          page_size, bti_, &mmio_.value(), is_32bit_, 1 << hcsparams2.ERST_MAX(),
          ERSTSZ::Get(offset, 0).ReadFrom(&mmio_.value()),
          ERDP::Get(offset, 0).ReadFrom(&mmio_.value()), IMAN::Get(offset, 0).FromValue(0),
          cap_length_, HCSPARAMS1::Get().ReadFrom(&mmio_.value()), &command_ring_, doorbell_offset_,
          this, hcc_, dcbaa_) != ZX_OK) {
    return ZX_ERR_INTERNAL;
  }
  if (command_ring_.Init(page_size, &bti_, &interrupters_[0].ring(), is_32bit_, &mmio_.value(),
                         this) != ZX_OK) {
    return ZX_ERR_INTERNAL;
  }
  CRCR cr = command_ring_.phys(cap_length_);
  cr.WriteTo(&mmio_.value());
  // Initialize initial interrupter. We will later demand-allocate interrupters
  // as we get additional load.
  {
    fbl::AutoLock sched_lock(&scheduler_lock_);
    active_interrupters_ = 1;
  }
  if (interrupters_[0].Start(0, offset, mmio_.value().View(0), this) != ZX_OK) {
    return ZX_ERR_INTERNAL;
  }
  init_txn_->Reply(ZX_OK);  // This will make the device visible and able to be unbound.
  sync_completion_wait(&bus_completion, ZX_TIME_INFINITE);
  USBCMD::Get(cap_length_)
      .ReadFrom(&mmio_.value())
      .set_ENABLE(1)
      .set_INTE(1)
      .set_HSEE(1)
      .set_EWE(1)
      .WriteTo(&mmio_.value());
  while (USBSTS::Get(cap_length_).ReadFrom(&mmio_.value()).HCHalted()) {
    zx::nanosleep(zx::deadline_after(zx::msec(1)));
  }
  sync_completion_signal(&bringup_);
  return ZX_OK;
}

zx_status_t UsbXhci::Init() {
  if (!(pci_.is_valid() || pdev_.is_valid())) {
    return ZX_ERR_IO_INVALID;
  }
  zx_status_t status =
      device_get_profile(zxdev_, /*HIGH_PRIORITY*/ 31, "src/devices/usb/drivers/xhci/usb-xhci",
                         profile_.reset_and_get_address());
  if (status != ZX_OK) {
    zxlogf(WARNING, "Failed to obtain scheduler profile for high priority completer (res %d)",
           status);
  }
  return DdkAdd("xhci");
}

void UsbXhci::DdkInit(ddk::InitTxn txn) {
  init_txn_ = std::move(txn);
  thrd_t init_thread;
  if (thrd_create_with_name(
          &init_thread,
          [](void* ctx) {
            UsbXhci* hci = static_cast<UsbXhci*>(ctx);
            return hci->InitThread();
          },
          this, "xhci-init-thread") != thrd_success) {
    return init_txn_->Reply(ZX_ERR_INTERNAL);  // This will schedule unbinding of the device.
  }
  init_thread_ = init_thread;
  // The init thread will reply to |init_txn_| once it is ready to make the device visible
  // and able to be unbound.
}

TRBPromise UsbXhci::SubmitCommand(const TRB& command, std::unique_ptr<TRBContext> trb_context) {
  fit::bridge<TRB*, zx_status_t> bridge;
  trb_context->completer = std::move(bridge.completer);
  zx_status_t status = command_ring_.AddTRB(command, std::move(trb_context));
  if (status != ZX_OK) {
    return fit::make_result_promise(fit::result<TRB*, zx_status_t>(fit::error(status))).box();
  }
  // Ring the doorbell
  DOORBELL::Get(doorbell_offset_, 0).FromValue(0).WriteTo(&mmio_.value());
  return bridge.consumer.promise().box();
}

zx_status_t UsbXhci::Create(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker ac;
  auto dev = std::unique_ptr<UsbXhci>(new (&ac) UsbXhci(parent, dma_buffer::CreateBufferFactory()));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  if (device_get_fragment_count(parent) > 0) {
    // We need at least a PDEV, but the PHY is optional
    // for devices not implementing OTG.
    dev->phy_ = ddk::UsbPhyProtocolClient(parent, "xhci-phy");
    dev->pdev_ = ddk::PDev::FromFragment(parent);
    if (!dev->pdev_.is_valid()) {
      zxlogf(ERROR, "UsbXhci::Init: could not get platform device protocol");
      return ZX_ERR_NOT_SUPPORTED;
    }
  }
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  auto status = dev->Init();
  if (status != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* dummy = dev.release();
  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = UsbXhci::Create;
  return ops;
}();

}  // namespace usb_xhci

ZIRCON_DRIVER(usb_xhci, usb_xhci::driver_ops, "zircon", "0.1");
