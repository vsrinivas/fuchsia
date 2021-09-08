// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "xhci-device-state.h"

#include "usb-xhci.h"

namespace usb_xhci {

zx_status_t DeviceState::InitializeSlotBuffer(const UsbXhci& hci, uint8_t slot_id, uint8_t port_id,
                                              const std::optional<HubInfo>& hub_info,
                                              std::unique_ptr<dma_buffer::PagedBuffer>* out) {
  // Section 4.3.3
  // 6.2.5 (Input Context initialization)
  std::unique_ptr<dma_buffer::PagedBuffer> buffer;
  zx_status_t status =
      hci.buffer_factory().CreatePaged(hci.bti(), zx_system_get_page_size(), false, &buffer);
  if (status != ZX_OK) {
    return status;
  }
  if (hci.Is32BitController() && (buffer->phys()[0] >= UINT32_MAX)) {
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
  usb_speed_t speed;
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
  zx_status_t status = hci.buffer_factory().CreatePaged(hci.bti(), zx_system_get_page_size(), false,
                                                        &output_context_buffer);
  if (status != ZX_OK) {
    return status;
  }
  if (hci.Is32BitController() && (output_context_buffer->phys()[0] >= UINT32_MAX)) {
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
    hci->GetPortState()[port - 1].slot_id = slot;
  }
  std::unique_ptr<dma_buffer::PagedBuffer> slot_context_buffer;
  std::unique_ptr<dma_buffer::PagedBuffer> output_context_buffer;
  zx_status_t status = InitializeSlotBuffer(*hci, slot, port, hub_info, &slot_context_buffer);
  if (status != ZX_OK) {
    return fpromise::make_error_promise(status);
  }

  // Allocate the transfer ring (see section 4.9)
  // TODO (bbosak): Assign an Interrupter from the pool
  fbl::AutoLock _(&transaction_lock_);
  status = tr_.Init(hci->GetPageSize(), hci->bti(), event_ring, hci->Is32BitController(),
                    mmio, *hci);
  if (status != ZX_OK) {
    return fpromise::make_result_promise(
               fpromise::result<TRB*, zx_status_t>(fpromise::error(status)))
        .box();
  }

  status = InitializeEndpointContext(*hci, slot, port, hub_info, slot_context_buffer.get());
  if (status != ZX_OK) {
    return fpromise::make_result_promise(
               fpromise::result<TRB*, zx_status_t>(fpromise::error(status)))
        .box();
  }

  status = InitializeOutputContextBuffer(*hci, slot, port, hub_info, dcbaa, &output_context_buffer);
  if (status != ZX_OK) {
    return fpromise::make_result_promise(
               fpromise::result<TRB*, zx_status_t>(fpromise::error(status)))
        .box();
  }

  // Issue an address device command for the device slot
  // See sections 3.3.4 and 6.4.3.4
  usb_xhci::AddressDeviceStruct command;
  command.ptr = slot_context_buffer->phys()[0];
  command.set_SlotID(slot);
  command.set_BSR(bsr);
  auto command_context = command_ring->AllocateContext();
  if (!command_context) {
    return fpromise::make_result_promise(
               fpromise::result<TRB*, zx_status_t>(fpromise::error(ZX_ERR_NO_MEMORY)))
        .box();
  }
  command_context->port_number = port;
  hw_mb();
  input_context_ = std::move(slot_context_buffer);
  device_context_ = std::move(output_context_buffer);
  return hci->SubmitCommand(command, std::move(command_context));
}

}  // namespace usb_xhci
