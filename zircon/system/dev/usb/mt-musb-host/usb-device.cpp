// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trace.h"
#include "usb-device.h"

#include <ddk/debug.h>
#include <soc/mt8167/mt8167-usb.h>
#include <zircon/status.h>

namespace mt_usb_hci {

namespace regs = board_mt8167;

zx_status_t HardwareDevice::HandleRequest(usb::UnownedRequest<> req) {
    auto ep = static_cast<uint8_t>(usb_ep_num2(req.request()->header.ep_address));
    ZX_ASSERT_MSG(ep_.at(ep), "endpoint not configured");
    return ep_[ep]->QueueRequest(std::move(req));
}

zx_status_t HardwareDevice::Enumerate() {
    TRACE();
    // Note that per the USB spec., endpoint-0 is always a ControlEndpoint.
    auto ep0 = static_cast<ControlEndpoint*>(ep_[0].get());

    usb_device_descriptor_t desc;
    auto status = ep0->GetDeviceDescriptor(&desc);
    if (status != ZX_OK) {
        zxlogf(ERROR, "GET_DESCRIPTOR (device) error: %s\n", zx_status_get_string(status));
        return status;
    }

    // TODO(hansens) add support for multipoint devices (i.e. downstream hubs).
    if (desc.bDeviceClass == USB_CLASS_HUB) {
        zxlogf(ERROR, "usb host does not currently support downstream hubs\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    status = ep0->SetAddress(static_cast<uint8_t>(id_));
    if (status != ZX_OK) {
        zxlogf(ERROR, "SET_ADDRESS error: %s\n", zx_status_get_string(status));
        return status;
    }

    // Having processed a SET_ADDRESS transaction, the device is now in the ADDRESS state (see: USB
    // 2.0 spec. section 9.1) and is ready to be managed by the upper USB layers.  The necessary
    // enumeration steps to follow will be performed by the usb stack and need not be executed here.
    //
    // Currently, the device only has one configured endpoint: the control endpoint (which all
    // devices have).  To further dispatch and process incoming enumeration transactions, we'll kick
    // the ControlEndpoint's processing thread into execution.

    // TODO(hansens) use the queue to enumerate the device instead of discrete endpoint routines.
    status = ep0->StartQueueThread();
    if (status != ZX_OK) {
        zxlogf(ERROR, "endpoint thread init error: %s\n", zx_status_get_string(status));
        return status;
    }

    return ZX_OK;
}

void HardwareDevice::Disconnect() {
    for (uint8_t i=0; i<=kMaxEpNum; i++) {
        if (ep_[i] != nullptr) {
            ep_[i]->Halt();
        }
    }
}

zx_status_t HardwareDevice::CancelAll(uint8_t ep) {
    // We cannot guarantee the endpoint is configured.
    if (ep_[ep]) {
        ep_[ep]->CancelAll();
    }
    return ZX_OK;
}

void HardwareDevice::ResizeFifo(uint8_t ep, size_t pkt_sz) {
    uint8_t fifo_size;

    // For table details, see: MUSBMHDRC section 3.10.1.
    if (pkt_sz <= 8) {
        fifo_size = 0;
    } else if (pkt_sz <= 16) {
        fifo_size = 1;
    } else if (pkt_sz <= 32) {
        fifo_size = 2;
    } else if (pkt_sz <= 64) {
        fifo_size = 3;
    } else if (pkt_sz <= 128) {
        fifo_size = 4;
    } else if (pkt_sz <= 256) {
        fifo_size = 5;
    } else if (pkt_sz <= 512) {
        fifo_size = 6;
    } else if (pkt_sz <= 1024) {
        fifo_size = 7;
    } else if (pkt_sz <= 2048) {
        fifo_size = 8;
    } else {
        fifo_size = 9; // Max single-buffered FIFO size.
    }

    regs::INDEX::Get().FromValue(0).set_selected_endpoint(ep).WriteTo(&usb_);
    regs::TXFIFOSZ::Get().FromValue(0).set_txsz(fifo_size).WriteTo(&usb_);
    regs::RXFIFOSZ::Get().FromValue(0).set_rxsz(fifo_size).WriteTo(&usb_);
    regs::INDEX::Get().FromValue(0).set_selected_endpoint(0).WriteTo(&usb_);
}

zx_status_t HardwareDevice::EnableEndpoint(const usb_endpoint_descriptor_t& descriptor) {
    const uint8_t ep = usb_ep_num(&descriptor);
    const uint8_t type = usb_ep_type(&descriptor);

    // Note that control endpoints are always present and thus not created from a descriptor.
    switch (type) {
    case USB_ENDPOINT_BULK:
        ep_[ep] = std::make_unique<BulkEndpoint>(usb_.View(0), id_, descriptor);
        break;
    case USB_ENDPOINT_INTERRUPT:
        ep_[ep] = std::make_unique<InterruptEndpoint>(usb_.View(0), id_, descriptor);
        break;
    default:
        zxlogf(ERROR, "unsupported endpoint type: 0x%x\n", type);
        break;
    }

    // Perform direction-specific config.
    if (usb_ep_direction(&descriptor) == USB_ENDPOINT_IN) {
        auto intrrxe = regs::INTRRXE::Get().ReadFrom(&usb_);
        auto val = static_cast<uint16_t>(intrrxe.ep_rx());
        val |= static_cast<uint16_t>(1 << ep);
        intrrxe.set_ep_rx(val).WriteTo(&usb_);

        regs::RXCSR_HOST::Get(ep).ReadFrom(&usb_).set_clrdatatog(1).WriteTo(&usb_);
    } else { // USB_ENDPOINT_OUT
        auto intrtxe = regs::INTRTXE::Get().ReadFrom(&usb_);
        auto val = static_cast<uint16_t>(intrtxe.ep_tx());
        val |= static_cast<uint16_t>(1 << ep);
        intrtxe.set_ep_tx(val).WriteTo(&usb_);

        regs::TXCSR_HOST::Get(ep).ReadFrom(&usb_).set_clrdatatog(1).WriteTo(&usb_);
    }

    Endpoint* endpoint = ep_[ep].get();
    ResizeFifo(ep, endpoint->GetMaxTransferSize());
    return endpoint->StartQueueThread();
}

zx_status_t HardwareDevice::DisableEndpoint(const usb_endpoint_descriptor_t& desc) {
    const uint8_t ep = usb_ep_num(&desc);
    ep_[ep].reset();

    // Disable the requisite interrupt.
    if (usb_ep_direction(&desc) == USB_ENDPOINT_IN) {
        auto intrrxe = regs::INTRRXE::Get().ReadFrom(&usb_);
        auto val = static_cast<uint16_t>(intrrxe.ep_rx());
        val &= static_cast<uint16_t>(~(1 << ep));
        intrrxe.set_ep_rx(val).WriteTo(&usb_);
    } else { // USB_ENDPOINT_OUT
        auto intrtxe = regs::INTRTXE::Get().ReadFrom(&usb_);
        auto val = static_cast<uint16_t>(intrtxe.ep_tx());
        val &= static_cast<uint16_t>(~(1 << ep));
        intrtxe.set_ep_tx(val).WriteTo(&usb_);
    }

    ResizeFifo(ep, kFifoMaxSize);
    return ZX_OK;
}

size_t HardwareDevice::GetMaxTransferSize(uint8_t ep) {
    if (ep > kMaxEpNum || !ep_[ep]) {
        zxlogf(ERROR, "%s: unconfigured endpoint\n", __func__);
        return 0;
    }
    return ep_[ep]->GetMaxTransferSize();
}

} // namespace mt_usb_hci
