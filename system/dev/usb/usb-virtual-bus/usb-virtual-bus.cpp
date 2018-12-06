// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-virtual-bus.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/usb/virtualbus/c/fidl.h>
#include <usb/usb-request.h>

namespace usb_virtual_bus {

// For mapping bEndpointAddress value to/from index in range 0 - 31.
// OUT endpoints are in range 1 - 15, IN endpoints are in range 17 - 31.
static inline uint8_t EpAddressToIndex(uint8_t addr) {
    return static_cast<uint8_t>(((addr) & 0xF) | (((addr) & 0x80) >> 3));
}
static constexpr uint8_t IN_EP_START = 17;

// Internal context for USB requests, used for both host and peripheral side.
struct UsbReqInternal {
     // Callback to the upper layer.
     usb_request_complete_t complete_cb;
     // For queueing requests internally.
     list_node_t node;
};

#define USB_REQ_TO_INTERNAL(req) \
    ((UsbReqInternal *)((uintptr_t)(req) + sizeof(usb_request_t)))
#define INTERNAL_TO_USB_REQ(ctx) ((usb_request_t *)((uintptr_t)(ctx) - sizeof(usb_request_t)))

#define DEVICE_SLOT_ID  0
#define DEVICE_HUB_ID   0
#define DEVICE_SPEED    USB_SPEED_HIGH

zx_status_t UsbVirtualBus::Create(zx_device_t* parent) {
    fbl::AllocChecker ac;
    auto bus = fbl::make_unique_checked<UsbVirtualBus>(&ac, parent);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    auto status = bus->Init();
    if (status != ZX_OK) {
        return status;
    }

    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = bus.release();
    return ZX_OK;
}

zx_status_t UsbVirtualBus::CreateDevice() {
    fbl::AllocChecker ac;
    device_ = fbl::make_unique_checked<UsbVirtualDevice>(&ac, zxdev(), this);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    auto status = device_->DdkAdd("usb-virtual-device");
    if (status != ZX_OK) {
        device_ = nullptr;
        return status;
    }

    return ZX_OK;
}

zx_status_t UsbVirtualBus::CreateHost() {
    fbl::AllocChecker ac;
    host_ = fbl::make_unique_checked<UsbVirtualHost>(&ac, zxdev(), this);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    auto status = host_->DdkAdd("usb-virtual-host");
    if (status != ZX_OK) {
        host_ = nullptr;
        return status;
    }

    return ZX_OK;
}

zx_status_t UsbVirtualBus::Init() {
    for (unsigned i = 0; i < USB_MAX_EPS; i++) {
        usb_virtual_ep_t* ep = &eps_[i];
        list_initialize(&ep->host_reqs);
        list_initialize(&ep->device_reqs);
    }

    auto status = DdkAdd("usb-virtual-bus", DEVICE_ADD_NON_BINDABLE);
    if (status != ZX_OK) {
        return status;
    }

    int rc = thrd_create_with_name(&thread_,
                                   [](void* arg) -> int {
                                       return reinterpret_cast<UsbVirtualBus*>(arg)->Thread();
                                   },
                                   reinterpret_cast<void*>(this),
                                   "usb-virtual-bus-thread");
    if (rc != thrd_success) {
        DdkRemove();
        return ZX_ERR_INTERNAL;
    }
    return ZX_OK;
}

int UsbVirtualBus::Thread() {
    while (1) {
        UsbReqInternal* req_int;
        list_node_t completed = LIST_INITIAL_VALUE(completed);

        sync_completion_wait(&thread_signal_, ZX_TIME_INFINITE);
        sync_completion_reset(&thread_signal_);

        lock_.Acquire();

        if (unbinding_) {
            for (unsigned i = 0; i < USB_MAX_EPS; i++) {
                usb_virtual_ep_t* ep = &eps_[i];

                while ((req_int = list_remove_head_type(&ep->host_reqs, UsbReqInternal,
                                                        node)) != nullptr) {
                    list_add_tail(&completed, &req_int->node);
                }
                while ((req_int = list_remove_head_type(&ep->device_reqs, UsbReqInternal,
                                                        node)) != nullptr) {
                    list_add_tail(&completed, &req_int->node);
                }
            }

            lock_.Release();

            // Complete requests outside of the lock to avoid deadlock.
            while ((req_int = list_remove_head_type(&completed, UsbReqInternal, node))
                    != nullptr) {
                usb_request_t* req = INTERNAL_TO_USB_REQ(req_int);
                usb_request_complete(req, ZX_ERR_IO_NOT_PRESENT, 0, &req_int->complete_cb);
            }

            return 0;
        }

        // special case endpoint zero
        while ((req_int = list_remove_head_type(&eps_[0].host_reqs, UsbReqInternal, node))
                != nullptr) {
            lock_.Release();
            // Handle control requests outside of the lock to avoid deadlock.
            HandleControl(INTERNAL_TO_USB_REQ(req_int));
            lock_.Acquire();
        }

        for (unsigned i = 1; i < USB_MAX_EPS; i++) {
            usb_virtual_ep_t* ep = &eps_[i];
            bool out = (i < IN_EP_START);

            while ((req_int = list_peek_head_type(&ep->host_reqs, UsbReqInternal, node))
                    != nullptr) {
                UsbReqInternal* device_req_int;
                device_req_int = list_remove_head_type(&ep->device_reqs, UsbReqInternal, node);

                if (device_req_int) {
                    usb_request_t* req = INTERNAL_TO_USB_REQ(req_int);
                    usb_request_t* device_req = INTERNAL_TO_USB_REQ(device_req_int);
                    zx_off_t offset = ep->req_offset;
                    size_t length = req->header.length - offset;
                    if (length > device_req->header.length) {
                        length = device_req->header.length;
                    }

                    void* device_buffer;
                    auto status = usb_request_mmap(device_req, &device_buffer);
                    if (status != ZX_OK) {
                        zxlogf(ERROR, "%s: usb_request_mmap failed: %d\n", __func__, status);
                        req->response.status = status;
                        device_req->response.status = status;
                        list_add_tail(&completed, &device_req_int->node);
                        list_add_tail(&completed, &req_int->node);
                        continue;
                    }

                    if (out) {
                        usb_request_copy_from(req, device_buffer, length, offset);
                    } else {
                        usb_request_copy_to(req, device_buffer, length, offset);
                    }

                    device_req->response.status = ZX_OK;
                    device_req->response.actual = length;
                    list_add_tail(&completed, &device_req_int->node);

                    offset += length;
                    if (offset < req->header.length) {
                        ep->req_offset = offset;
                    } else {
                        list_delete(&req_int->node);
                        usb_request_t* req = INTERNAL_TO_USB_REQ(req_int);
                        req->response.status = ZX_OK;
                        req->response.actual = length;
                        list_add_tail(&completed, &req_int->node);
                        ep->req_offset = 0;
                    }
                } else {
                    break;
                }
            }
        }

        lock_.Release();

        // Complete requests outside of the lock to avoid deadlock.
        while ((req_int = list_remove_head_type(&completed, UsbReqInternal, node))
                != nullptr) {
            usb_request_t* req = INTERNAL_TO_USB_REQ(req_int);
            usb_request_complete(req, req->response.status, req->response.actual,
                                 &req_int->complete_cb);
        }
    }
    return 0;
}

void UsbVirtualBus::HandleControl(usb_request_t* req) {
    const usb_setup_t* setup = &req->setup;
    auto* req_int = USB_REQ_TO_INTERNAL(req);
    zx_status_t status;
    size_t length = le16toh(setup->wLength);
    size_t actual = 0;

    zxlogf(TRACE, "%s type: 0x%02X req: %d value: %d index: %d length: %zu\n", __func__,
           setup->bmRequestType, setup->bRequest, le16toh(setup->wValue), le16toh(setup->wIndex),
           length);

    if (dci_intf_.is_valid()) {
        void* buffer = nullptr;

        if (length > 0) {
            auto status = usb_request_mmap(req, &buffer);
            if (status != ZX_OK) {
                zxlogf(ERROR, "%s: usb_request_mmap failed: %d\n", __func__, status);
                usb_request_complete(req, status, 0, &req_int->complete_cb);
                return;
            }
        }

        if ((setup->bmRequestType & USB_ENDPOINT_DIR_MASK) == USB_ENDPOINT_IN) {
            status = dci_intf_.Control(setup, nullptr, 0, buffer, length, &actual);
        } else {
            status = dci_intf_.Control(setup, buffer, length, nullptr, 0, nullptr);
        }
    } else {
        status = ZX_ERR_UNAVAILABLE;
    }

    usb_request_complete(req, status, actual, &req_int->complete_cb);
}

void UsbVirtualBus::SetConnected(bool connected) {
    bool was_connected = connected;
    {
      fbl::AutoLock lock(&lock_);
      std::swap(connected_, was_connected);
    }

    if (connected && !was_connected) {
        if (bus_intf_.is_valid()) {
            bus_intf_.AddDevice(DEVICE_SLOT_ID, DEVICE_HUB_ID, DEVICE_SPEED);
        }
        if (dci_intf_.is_valid()) {
            dci_intf_.SetConnected(true);
        }
    } else if (!connected && was_connected) {
        if (bus_intf_.is_valid()) {
            bus_intf_.RemoveDevice(DEVICE_SLOT_ID);
        }
        if (dci_intf_.is_valid()) {
            dci_intf_.SetConnected(false);
        }

        UsbReqInternal* req_int;
        list_node_t completed = LIST_INITIAL_VALUE(completed);

        {
            fbl::AutoLock lock(&lock_);

            for (unsigned i = 0; i < USB_MAX_EPS; i++) {
                usb_virtual_ep_t* ep = &eps_[i];

                while ((req_int = list_remove_head_type(&ep->host_reqs, UsbReqInternal,
                                                        node)) != nullptr) {
                    list_add_tail(&completed, &req_int->node);
                }
                while ((req_int = list_remove_head_type(&ep->device_reqs, UsbReqInternal,
                                                        node)) != nullptr) {
                    list_add_tail(&completed, &req_int->node);
                }
            }
        }

        while ((req_int = list_remove_head_type(&completed, UsbReqInternal, node))
                != nullptr) {
            usb_request_t* req = INTERNAL_TO_USB_REQ(req_int);
            usb_request_complete(req, ZX_ERR_IO_NOT_PRESENT, 0, &req_int->complete_cb);
        }
    }
}

zx_status_t UsbVirtualBus::SetStall(uint8_t ep_address, bool stall) {
    uint8_t index = EpAddressToIndex(ep_address);
    if (index >= USB_MAX_EPS) {
        return ZX_ERR_INVALID_ARGS;
    }

    UsbReqInternal* req_int = nullptr;
    {
        fbl::AutoLock lock(&lock_);

        usb_virtual_ep_t* ep = &eps_[index];
        ep->stalled = stall;

        if (stall) {
            req_int = list_remove_head_type(&ep->host_reqs, UsbReqInternal, node);
        }
    }

    if (req_int) {
        usb_request_t* req = INTERNAL_TO_USB_REQ(req_int);
        usb_request_complete(req, ZX_ERR_IO_REFUSED, 0, &req_int->complete_cb);
    }

    return ZX_OK;
}

static fuchsia_usb_virtualbus_Bus_ops_t fidl_ops = {
    .Enable = [](void* ctx, fidl_txn_t* txn) {
                            return reinterpret_cast<UsbVirtualBus*>(ctx)->MsgEnable(txn); },
    .Disable = [](void* ctx, fidl_txn_t* txn) {
                            return reinterpret_cast<UsbVirtualBus*>(ctx)->MsgDisable(txn); },
    .Connect = [](void* ctx, fidl_txn_t* txn) {
                            return reinterpret_cast<UsbVirtualBus*>(ctx)->MsgConnect(txn); },
    .Disconnect = [](void* ctx, fidl_txn_t* txn) {
                            return reinterpret_cast<UsbVirtualBus*>(ctx)->MsgDisconnect(txn); },
};

zx_status_t UsbVirtualBus::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    return fuchsia_usb_virtualbus_Bus_dispatch(this, txn, msg, &fidl_ops);
}

void UsbVirtualBus::DdkUnbind() {
    {
        fbl::AutoLock lock(&lock_);
        unbinding_ = true;
    }
    sync_completion_signal(&thread_signal_);
    thrd_join(thread_, nullptr);

    auto* host = host_.release();
    if (host) {
        host->DdkRemove();
    }
    auto* device = device_.release();
    if (device) {
        device->DdkRemove();
    }
}

void UsbVirtualBus::DdkRelease() {
    delete this;
}

void UsbVirtualBus::UsbDciRequestQueue(usb_request_t* req,
                                       const usb_request_complete_t* complete_cb) {
    auto* req_int = USB_REQ_TO_INTERNAL(req);
    req_int->complete_cb = *complete_cb;

    uint8_t index = EpAddressToIndex(req->header.ep_address);
    if (index == 0 || index >= USB_MAX_EPS) {
        printf("%s: bad endpoint %u\n", __func__, req->header.ep_address);
        usb_request_complete(req, ZX_ERR_INVALID_ARGS, 0, complete_cb);
        return;
    }
    lock_.Acquire();
    if (!connected_) {
        lock_.Release();
        usb_request_complete(req, ZX_ERR_IO_NOT_PRESENT, 0, complete_cb);
        return;
    }

    list_add_tail(&eps_[index].device_reqs, &req_int->node);
    lock_.Release();

    sync_completion_signal(&thread_signal_);
}

zx_status_t UsbVirtualBus::UsbDciSetInterface(const usb_dci_interface_t* dci_intf) {
    if (dci_intf) {
        dci_intf_ = ddk::UsbDciInterfaceClient(dci_intf);
    } else {
        dci_intf_.clear();
    }
    return ZX_OK;
}

zx_status_t UsbVirtualBus::UsbDciConfigEp(const usb_endpoint_descriptor_t* ep_desc,
                                          const usb_ss_ep_comp_descriptor_t* ss_comp_desc) {
    return ZX_OK;
}

zx_status_t UsbVirtualBus::UsbDciDisableEp(uint8_t ep_address) {
    return ZX_OK;
}

zx_status_t UsbVirtualBus::UsbDciEpSetStall(uint8_t ep_address) {
    return SetStall(ep_address, true);
}

zx_status_t UsbVirtualBus::UsbDciEpClearStall(uint8_t ep_address) {
    return SetStall(ep_address, false);
}

size_t UsbVirtualBus::UsbDciGetRequestSize() {
    return sizeof(usb_request_t) + sizeof(UsbReqInternal);
}

void UsbVirtualBus::UsbHciRequestQueue(usb_request_t* req,
                                       const usb_request_complete_t* complete_cb) {
    auto* req_int = USB_REQ_TO_INTERNAL(req);
    req_int->complete_cb = *complete_cb;

    uint8_t index = EpAddressToIndex(req->header.ep_address);
    if (index >= USB_MAX_EPS) {
        printf("usb_virtual_bus_host_queue bad endpoint %u\n", req->header.ep_address);
        usb_request_complete(req, ZX_ERR_INVALID_ARGS, 0, complete_cb);
        return;
    }

    lock_.Acquire();
    if (!connected_) {
        lock_.Release();
        usb_request_complete(req, ZX_ERR_IO_NOT_PRESENT, 0, complete_cb);
        return;
    }

    usb_virtual_ep_t* ep = &eps_[index];

    if (ep->stalled) {
        lock_.Release();
        usb_request_complete(req, ZX_ERR_IO_REFUSED, 0, complete_cb);
        return;
    }
    list_add_tail(&ep->host_reqs, &req_int->node);
    lock_.Release();

    sync_completion_signal(&thread_signal_);
}

void UsbVirtualBus::UsbHciSetBusInterface(const usb_bus_interface_t* bus_intf) {
    if (bus_intf) {
        bus_intf_ = ddk::UsbBusInterfaceClient(bus_intf);

        lock_.Acquire();
        bool connected = connected_;
        lock_.Release();

        if (connected) {
            bus_intf_.AddDevice(DEVICE_SLOT_ID, DEVICE_HUB_ID, DEVICE_SPEED);
        }
    } else {
        bus_intf_.clear();
    }
}

size_t UsbVirtualBus::UsbHciGetMaxDeviceCount() {
    return 1;
}

zx_status_t UsbVirtualBus::UsbHciEnableEndpoint(uint32_t device_id,
                                                const usb_endpoint_descriptor_t* ep_desc,
                                                const usb_ss_ep_comp_descriptor_t* ss_com_desc,
                                                bool enable) {
    return ZX_OK;
}

uint64_t UsbVirtualBus::UsbHciGetCurrentFrame() {
    return 0;
}

zx_status_t UsbVirtualBus::UsbHciConfigureHub(uint32_t device_id, usb_speed_t speed,
                                              const usb_hub_descriptor_t* desc) {
    return ZX_OK;
}

zx_status_t UsbVirtualBus::UsbHciHubDeviceAdded(uint32_t device_id, uint32_t port,
                                                usb_speed_t speed) {
    return ZX_OK;
}

zx_status_t UsbVirtualBus::UsbHciHubDeviceRemoved(uint32_t device_id, uint32_t port) {
    return ZX_OK;
}

zx_status_t UsbVirtualBus::UsbHciHubDeviceReset(uint32_t device_id, uint32_t port) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t UsbVirtualBus::UsbHciResetEndpoint(uint32_t device_id, uint8_t ep_address) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t UsbVirtualBus::UsbHciResetDevice(uint32_t hub_address, uint32_t device_id) {
    return ZX_ERR_NOT_SUPPORTED;
}

size_t UsbVirtualBus::UsbHciGetMaxTransferSize(uint32_t device_id, uint8_t ep_address) {
    return 65536;
}

zx_status_t UsbVirtualBus::UsbHciCancelAll(uint32_t device_id, uint8_t ep_address) {
    return ZX_ERR_NOT_SUPPORTED;
}

size_t UsbVirtualBus::UsbHciGetRequestSize() {
    return sizeof(usb_request_t) + sizeof(UsbReqInternal);
}

zx_status_t UsbVirtualBus::MsgEnable(fidl_txn_t* txn) {
    fbl::AutoLock lock(&lock_);

    zx_status_t status = ZX_OK;

    if (host_ == nullptr) {
        status = CreateHost();
    }
    if (status == ZX_OK && device_ == nullptr) {
        status = CreateDevice();
    }

    return fuchsia_usb_virtualbus_BusEnable_reply(txn, status);
}

zx_status_t UsbVirtualBus::MsgDisable(fidl_txn_t* txn) {
    SetConnected(false);

    fbl::AutoLock lock(&lock_);

    // Use release() here to avoid double free of these objects.
    // devmgr will handle freeing them.
    auto* host = host_.release();
    if (host) {
        host->DdkRemove();
    }
    auto* device = device_.release();
    if (device) {
        device->DdkRemove();
    }

    return fuchsia_usb_virtualbus_BusDisable_reply(txn, ZX_OK);
}

zx_status_t UsbVirtualBus::MsgConnect(fidl_txn_t* txn) {
    if (host_ == nullptr || device_ == nullptr) {
        return fuchsia_usb_virtualbus_BusConnect_reply(txn, ZX_ERR_BAD_STATE);
    }

    SetConnected(true);
    return fuchsia_usb_virtualbus_BusConnect_reply(txn, ZX_OK);
}

zx_status_t UsbVirtualBus::MsgDisconnect(fidl_txn_t* txn) {
    if (host_ == nullptr || device_ == nullptr) {
        return fuchsia_usb_virtualbus_BusDisconnect_reply(txn, ZX_ERR_BAD_STATE);
    }

    SetConnected(false);
    return fuchsia_usb_virtualbus_BusDisconnect_reply(txn, ZX_OK);
}

static zx_status_t usb_virtual_bus_bind(void* ctx, zx_device_t* parent) {
    return usb_virtual_bus::UsbVirtualBus::Create(parent);
}

static zx_driver_ops_t driver_ops = [](){
    zx_driver_ops_t ops;
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = usb_virtual_bus_bind;
    return ops;
}();

} // namespace usb_virtual_bus

ZIRCON_DRIVER_BEGIN(usb_virtual_bus, usb_virtual_bus::driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_TEST_PARENT),
ZIRCON_DRIVER_END(usb_virtual_bus)
