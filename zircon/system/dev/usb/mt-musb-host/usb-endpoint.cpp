// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trace.h"
#include "usb-endpoint.h"
#include "usb-spew.h"

#include <ddk/debug.h>
#include <fbl/auto_lock.h>
#include <lib/zx/time.h>
#include <zircon/hw/usb.h>
#include <zircon/status.h>

namespace mt_usb_hci {

zx_status_t TransactionEndpoint::QueueRequest(usb::UnownedRequest<> req) {
    fbl::AutoLock _(&pending_lock_);

    // To prevent a race condition by which a request is enqueued after having stopped the
    // processing thread (thus orphaning the request), this check must be made with the lock held.
    if (halted_.load()) {
        req.Complete(ZX_ERR_IO_NOT_PRESENT, 0);
        return ZX_OK;
    }

    pending_.push(std::move(req));
    pending_cond_.Signal();
    return ZX_OK;
}

zx_status_t TransactionEndpoint::StartQueueThread() {
    auto go = [](void* arg) { return static_cast<TransactionEndpoint*>(arg)->QueueThread(); };
    auto rc = thrd_create_with_name(&pending_thread_, go, this, "usb-endpoint-thread");
    if (rc != thrd_success) {
        return ZX_ERR_INTERNAL;
    }
    return ZX_OK;
}

zx_status_t TransactionEndpoint::CancelAll() {
    fbl::AutoLock _(&pending_lock_);
    if (transaction_) {
        transaction_->Cancel();
    }

    while (!pending_.is_empty()) {
        std::optional<usb::UnownedRequest<>> req = pending_.pop();
        req->Complete(ZX_ERR_CANCELED, 0);
    }

    return ZX_OK;
}

size_t TransactionEndpoint::GetMaxTransferSize() {
    return static_cast<size_t>(max_pkt_sz_);
}

zx_status_t TransactionEndpoint::Halt() {
    fbl::AutoLock _(&pending_lock_);
    if (transaction_) {
        transaction_->Cancel();
    }

    halted_ = true;
    pending_cond_.Signal();

    return ZX_OK;
}

int TransactionEndpoint::QueueThread() {
    zx_status_t status;
    std::optional<usb::UnownedRequest<>> req;

    for (;;) {
        {
            fbl::AutoLock _(&pending_lock_);
            if (pending_.is_empty()) {
                if (halted_.load()) {
                    sync_completion_signal(&complete_);
                    return 0;
                }

                // To prevent deadlock, the halted check must be made both before and after waiting.
                // The first check ensures that Halt() requests issued as a transaction was being
                // processed by the body of this loop are serviced.  The second check ensures that
                // Halt() requests issued while waiting are serviced.
                pending_cond_.Wait(&pending_lock_);

                if (halted_.load()) {
                    sync_completion_signal(&complete_);
                    return 0;
                }
            }
            req = pending_.pop();
        }

        // Popping an empty queue is something that should never really happen.
        ZX_ASSERT(req.has_value());

        status = DispatchRequest(std::move(req.value()));
        if (status != ZX_OK) {
            zxlogf(ERROR, "could not process usb request: %s\n", zx_status_get_string(status));
        }
    }
    return 0;
}

zx_status_t ControlEndpoint::GetDeviceDescriptor(usb_device_descriptor_t* out) {
    TRACE();
    usb_setup_t req = { // GET_DESCRIPTOR request, see: USB 2.0 spec. section 9.4.3.
        0x80,        // .bmRequestType
        0x6,         // .bRequest
        0x0100,      // .wValue (type=device)
        0,           // .wIndex
        sizeof(*out) // .wLength
    };

    transaction_ = std::make_unique<Control>(ControlType::READ, usb_.View(0), req, out,
                                             sizeof(*out), max_pkt_sz_, faddr_);
    transaction_->Advance();
    transaction_->Wait();

    if (!transaction_->Ok()) {
        zxlogf(ERROR, "usb transaction did not complete successfully\n");
        return ZX_ERR_INTERNAL;
    }

    max_pkt_sz_ = static_cast<size_t>(out->bMaxPacketSize0);
    return ZX_OK;
}

zx_status_t ControlEndpoint::SetAddress(uint8_t addr) {
    usb_setup_t req = { // SET_ADDRESS request, see: USB 2.0 spec. section 9.4.6.
        0,    // .bmRequestType
        0x5,  // .bRequest
        addr, // .wValue
        0,    // .wIndex
        0,    // .wLength
    };

    transaction_ = std::make_unique<Control>(ControlType::ZERO, usb_.View(0), req, nullptr, 0,
                                             max_pkt_sz_, faddr_);
    transaction_->Advance();
    transaction_->Wait();

    if (!transaction_->Ok()) {
        zxlogf(ERROR, "usb transaction did not complete successfully\n");
        return ZX_ERR_INTERNAL;
    }

    // The USB spec. requires at least a 2ms sleep for device to finish processing new address
    // (see: USB 2.0 spec. 9.2.6.3).
    zx::nanosleep(zx::deadline_after(zx::msec(5)));

    faddr_ = addr;
    return ZX_OK;
}

zx_status_t ControlEndpoint::DispatchRequest(usb::UnownedRequest<> req) {
    zx_status_t status;
    usb_setup_t setup = req.request()->setup;

    if (setup.wLength == 0) { // See: USB 2.0 spec. section 9.3.5.
        transaction_ = std::make_unique<Control>(ControlType::ZERO, usb_.View(0), setup, nullptr, 0,
                                                 max_pkt_sz_, faddr_);
    } else {
        void* vmo_addr;
        status = req.Mmap(&vmo_addr);
        if (status != ZX_OK) {
            zxlogf(ERROR, "could not map request vmo: %s\n", zx_status_get_string(status));
            req.Complete(status, 0);
            return status;
        }

        size_t size = req.request()->header.length;
        if ((setup.bmRequestType & USB_DIR_MASK) == USB_DIR_IN) {
            transaction_ = std::make_unique<Control>(ControlType::READ, usb_.View(0), setup,
                                                     vmo_addr, size, max_pkt_sz_, faddr_);
        } else { // USB_DIR_OUT
            transaction_ = std::make_unique<Control>(ControlType::WRITE, usb_.View(0), setup,
                                                     vmo_addr, size, max_pkt_sz_, faddr_);
        }
    }

    transaction_->Advance();
    transaction_->Wait();

    if (halted_.load()) {
        req.Complete(ZX_ERR_IO_NOT_PRESENT, 0);
        return ZX_OK;
    } else if (!transaction_->Ok()) {
        zxlogf(ERROR, "usb control transfer did not complete successfully\n");
        req.Complete(ZX_ERR_INTERNAL, 0);
        return ZX_ERR_INTERNAL;
    }
    req.Complete(ZX_OK, transaction_->actual());
    return ZX_OK;
}

zx_status_t BulkEndpoint::DispatchRequest(usb::UnownedRequest<> req) {
    void* vmo_addr;
    auto status = req.Mmap(&vmo_addr);
    if (status != ZX_OK) {
        zxlogf(ERROR, "could not map request vmo: %s\n", zx_status_get_string(status));
        req.Complete(status, 0);
        return status;
    }

    size_t size = req.request()->header.length;
    transaction_ = std::make_unique<Bulk>(usb_.View(0), faddr_, vmo_addr, size, descriptor_);
    transaction_->Advance();
    transaction_->Wait();

    if (halted_.load()) {
        req.Complete(ZX_ERR_IO_NOT_PRESENT, 0);
        return ZX_OK;
    } else if (!transaction_->Ok()) {
        zxlogf(ERROR, "usb bulk transfer did not complete successfully\n");
        req.Complete(ZX_ERR_INTERNAL, 0);
        return ZX_ERR_INTERNAL;
    }
    req.Complete(ZX_OK, transaction_->actual());
    return ZX_OK;
}

zx_status_t InterruptEndpoint::DispatchRequest(usb::UnownedRequest<> req) {
    void* vmo_addr;
    auto status = req.Mmap(&vmo_addr);
    if (status != ZX_OK) {
        zxlogf(ERROR, "could not map request vmo: %s\n", zx_status_get_string(status));
        req.Complete(status, 0);
        return status;
    }

    size_t size = req.request()->header.length;
    transaction_ = std::make_unique<Interrupt>(usb_.View(0), faddr_, vmo_addr, size, descriptor_);
    transaction_->Advance();
    transaction_->Wait();

    if (halted_.load()) {
        req.Complete(ZX_ERR_IO_NOT_PRESENT, 0);
        return ZX_OK;
    } else if (!transaction_->Ok()) {
        zxlogf(ERROR, "usb interrupt transfer did not complete successfully\n");
        req.Complete(ZX_ERR_INTERNAL, 0);
        return ZX_ERR_INTERNAL;
    }
    req.Complete(ZX_OK, transaction_->actual());
    return ZX_OK;
}

} // namespace mt_usb_hci
