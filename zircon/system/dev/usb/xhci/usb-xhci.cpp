// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-xhci.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/pci-lib.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <hw/arch_ops.h>
#include <hw/reg.h>
#include <lib/zx/bti.h>
#include <lib/zx/interrupt.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <algorithm>
#include <memory>

#include "xdc.h"
#include "xhci-device-manager.h"
#include "xhci-root-hub.h"
#include "xhci-util.h"
#include "xhci.h"

namespace usb_xhci {

#define MAX_SLOTS 255

#define DEFAULT_PRIORITY 16
#define HIGH_PRIORITY    24

#define PDEV_MMIO_INDEX  0
#define PDEV_IRQ_INDEX   0

zx_status_t xhci_add_device(xhci_t* xhci, int slot_id, int hub_address, int speed) {
    zxlogf(TRACE, "xhci_add_new_device\n");

    if (!xhci->bus.ops) {
        zxlogf(ERROR, "no bus device in xhci_add_device\n");
        return ZX_ERR_INTERNAL;
    }

    return usb_bus_interface_add_device(&xhci->bus, slot_id, hub_address, speed);
}

void xhci_remove_device(xhci_t* xhci, int slot_id) {
    zxlogf(TRACE, "xhci_remove_device %d\n", slot_id);

    if (!xhci->bus.ops) {
        zxlogf(ERROR, "no bus device in xhci_remove_device\n");
        return;
    }

    usb_bus_interface_remove_device(&xhci->bus, slot_id);
}

void UsbXhci::UsbHciRequestQueue(usb_request_t* usb_request,
                                 const usb_request_complete_t* complete_cb) {
    xhci_request_queue(xhci_, usb_request, complete_cb);
}

void UsbXhci::UsbHciSetBusInterface(const usb_bus_interface_t* bus_intf) {
    if (bus_intf) {
        memcpy(&xhci_->bus, bus_intf, sizeof(xhci_->bus));
        // wait until bus driver has started before doing this
        xhci_queue_start_root_hubs(xhci_);
    } else {
        memset(&xhci_->bus, 0, sizeof(xhci_->bus));
    }
}

size_t UsbXhci::UsbHciGetMaxDeviceCount() {
    return xhci_->max_slots + XHCI_RH_COUNT + 1;
}

zx_status_t UsbXhci::UsbHciEnableEndpoint(uint32_t device_id,
                                          const usb_endpoint_descriptor_t* ep_desc,
                                          const usb_ss_ep_comp_descriptor_t* ss_com_desc,
                                          bool enable) {
    return xhci_enable_endpoint(xhci_, device_id, ep_desc, ss_com_desc, enable);
}

uint64_t UsbXhci::UsbHciGetCurrentFrame() {
    return xhci_get_current_frame(xhci_);
}

zx_status_t UsbXhci::UsbHciConfigureHub(uint32_t device_id, usb_speed_t speed,
                                        const usb_hub_descriptor_t* desc) {
    return xhci_configure_hub(xhci_, device_id, speed, desc);
}

zx_status_t UsbXhci::UsbHciHubDeviceAdded(uint32_t device_id, uint32_t port, usb_speed_t speed) {
    return xhci_enumerate_device(xhci_, device_id, port, speed);
}

zx_status_t UsbXhci::UsbHciHubDeviceRemoved(uint32_t device_id, uint32_t port) {
    xhci_device_disconnected(xhci_, device_id, port);
    return ZX_OK;
}

zx_status_t UsbXhci::UsbHciHubDeviceReset(uint32_t device_id, uint32_t port) {
    return xhci_device_reset(xhci_, device_id, port);
}

zx_status_t UsbXhci::UsbHciResetEndpoint(uint32_t device_id, uint8_t ep_address) {
    return xhci_reset_endpoint(xhci_, device_id, ep_address);
}

zx_status_t UsbXhci::UsbHciResetDevice(uint32_t hub_address, uint32_t device_id) {
    auto* slot = &xhci_->slots[device_id];
    uint32_t port = slot->port;
    if (slot->hub_address == 0) {
        // Convert real port number to virtual root hub number.
        port = xhci_->rh_port_map[port - 1] + 1;
    }
    zxlogf(TRACE, "xhci_reset_device slot_id: %u port: %u hub_address: %u\n",
           device_id, port, hub_address);

    return usb_bus_interface_reset_port(&xhci_->bus, hub_address, port, false);
}

static size_t xhci_get_max_transfer_size(uint8_t ep_address) {
    if (ep_address == 0) {
        // control requests have uint16 length field so we need to support UINT16_MAX
        // we require one setup, status and data event TRB in addition to data transfer TRBs
        // and subtract one more to account for the link TRB
        static_assert(PAGE_SIZE * (TRANSFER_RING_SIZE - 4) >= UINT16_MAX,
                      "TRANSFER_RING_SIZE too small");
        return UINT16_MAX;
    }
    // non-control transfers consist of normal transfer TRBs plus one data event TRB
    // Subtract 2 to reserve a TRB for data event and to account for the link TRB
    return PAGE_SIZE * (TRANSFER_RING_SIZE - 2);
}

size_t UsbXhci::UsbHciGetMaxTransferSize(uint32_t device_id, uint8_t ep_address) {
    return xhci_get_max_transfer_size(ep_address);
}

zx_status_t UsbXhci::UsbHciCancelAll(uint32_t device_id, uint8_t ep_address) {
    return xhci_cancel_transfers(xhci_, device_id, xhci_endpoint_index(ep_address));
}

size_t UsbXhci::UsbHciGetRequestSize() {
    return sizeof(xhci_usb_request_internal_t) + sizeof(usb_request_t);
}

void xhci_request_queue(xhci_t* xhci, usb_request_t* req,
                        const usb_request_complete_t* complete_cb) {
    zx_status_t status;

    xhci_usb_request_internal_t* req_int = USB_REQ_TO_XHCI_INTERNAL(req);
    req_int->complete_cb = *complete_cb;
    if (req->header.length > xhci_get_max_transfer_size(req->header.ep_address)) {
        status = ZX_ERR_INVALID_ARGS;
    } else {
        status = xhci_queue_transfer(xhci, req);
    }

    if (status != ZX_OK && status != ZX_ERR_BUFFER_TOO_SMALL) {
        usb_request_complete(req, status, 0, complete_cb);
    }
}

static void xhci_shutdown(xhci_t* xhci) {
    // stop the controller and our device thread
    xhci_stop(xhci);
    xhci->suspended.store(true);
    // stop our interrupt threads
    for (uint32_t i = 0; i < xhci->num_interrupts; i++) {
        zx_interrupt_destroy(xhci->irq_handles[i]);
        thrd_join(xhci->completer_threads[i], nullptr);
        zx_handle_close(xhci->irq_handles[i]);
    }
}

zx_status_t UsbXhci::DdkSuspend(uint32_t flags) {
    zxlogf(TRACE, "UsbXhci::DdkSuspend %u\n", flags);
    // TODO(voydanoff) do different things based on the flags.
    // for now we shutdown the driver in preparation for mexec
    xhci_shutdown(xhci_);

    return ZX_OK;
}

void UsbXhci::DdkUnbind() {
    zxlogf(INFO, "UsbXhci::DdkUnbind\n");
    xhci_shutdown(xhci_);
    DdkRemove();
}

void UsbXhci::DdkRelease() {
    zxlogf(INFO, "UsbXhci::DdkRelease\n");
    mmio_buffer_release(&xhci_->mmio);
    zx_handle_close(xhci_->cfg_handle);
    xhci_free(xhci_);
    delete this;
}

typedef struct completer {
    xhci_t *xhci;
    uint32_t interrupter;
    uint32_t priority;
} completer_t;

static int completer_thread(void *arg) {
    completer_t* completer = (completer_t*)arg;
    zx_handle_t irq_handle = completer->xhci->irq_handles[completer->interrupter];

    // TODO(johngro): See ZX-940.  Get rid of this.  For now we need thread
    // priorities so that realtime transactions use the completer which ends
    // up getting realtime latency guarantees.
    zx_thread_set_priority(completer->priority);

    while (1) {
        zx_status_t wait_res;
        wait_res = zx_interrupt_wait(irq_handle, nullptr);
        if (wait_res != ZX_OK) {
            if (wait_res != ZX_ERR_CANCELED) {
                zxlogf(ERROR, "unexpected zx_interrupt_wait failure (%d)\n", wait_res);
            }
            break;
        }
        if (completer->xhci->suspended.load()) {
            // TODO(ravoorir): Remove this hack once the interrupt signalling bug
            // is resolved.
            zxlogf(ERROR, "race in zx_interrupt_cancel triggered. Kick off workaround for now\n");
            break;
        }
        xhci_handle_interrupt(completer->xhci, completer->interrupter);
    }
    zxlogf(TRACE, "xhci completer %u thread done\n", completer->interrupter);
    free(completer);
    return 0;
}

int UsbXhci::StartThread() {
    zxlogf(TRACE, "%s start\n", __func__);

    completer_t* completers[xhci_->num_interrupts];
    uint32_t num_completers_initialized = 0;

    auto cleanup = fbl::MakeAutoCall([this, &completers, num_completers_initialized]() {
        DdkRemove();
        free(xhci_);
        for (uint32_t i = 0; i < num_completers_initialized; i++) {
            free(completers[i]);
        }
    });

    zx_status_t status;
    for (uint32_t i = 0; i < xhci_->num_interrupts; i++) {
        auto* completer = static_cast<completer_t*>(calloc(1, sizeof(completer_t)));
        if (completer == nullptr) {
            return ZX_ERR_NO_MEMORY;
        }
        completers[i] = completer;
        completer->interrupter = i;
        completer->xhci = xhci_;
        // We need a high priority thread for isochronous transfers.
        // If there is only one interrupt available, that thread will need
        // to be high priority.
        completer->priority = (i == ISOCH_INTERRUPTER || xhci_->num_interrupts == 1) ?
                              HIGH_PRIORITY : DEFAULT_PRIORITY;
        num_completers_initialized++;
    }

    // xhci_start will block, so do this part here instead of in usb_xhci_bind
    status = xhci_start(xhci_);
#if defined(__x86_64__)
    if (status == ZX_OK) {
        // TODO(jocelyndang): start xdc in a new process.
        status = xdc_bind(zxdev(), xhci_->bti_handle, xhci_->mmio.vaddr);
        if (status != ZX_OK) {
            zxlogf(ERROR, "xhci_start: xdc_bind failed %d\n", status);
        }
    }
#endif

    if (status != ZX_OK) {
        return status;
    }

    DdkMakeVisible();
    for (uint32_t i = 0; i < xhci_->num_interrupts; i++) {
        thrd_create_with_name(&xhci_->completer_threads[i], completer_thread, completers[i],
                              "completer_thread");
    }

    zxlogf(TRACE, "%s done\n", __func__);
    cleanup.cancel();
    return 0;
}

zx_status_t UsbXhci::FinishBind() {
    auto status = DdkAdd("xhci", DEVICE_ADD_INVISIBLE);
    if (status != ZX_OK) {
        return status;
    }

    thrd_t thread;
    thrd_create_with_name(&thread,
                          [](void* arg) -> int {
                              return reinterpret_cast<UsbXhci*>(arg)->StartThread();
                          },
                          reinterpret_cast<void*>(this), "xhci_start_thread");
    thrd_detach(thread);

    return ZX_OK;
}

zx_status_t UsbXhci::InitPci() {
    zx_handle_t cfg_handle = ZX_HANDLE_INVALID;
    uint32_t num_irq_handles_initialized = 0;
    zx_status_t status;

    auto cleanup = fbl::MakeAutoCall([this, num_irq_handles_initialized]() {
        zx_handle_close(xhci_->bti_handle);
        for (uint32_t i = 0; i < num_irq_handles_initialized; i++) {
            zx_handle_close(xhci_->irq_handles[i]);
        }
        mmio_buffer_release(&xhci_->mmio);
        zx_handle_close(xhci_->cfg_handle);
        free(xhci_);
    });

    xhci_ = static_cast<xhci_t*>(calloc(1, sizeof(xhci_t)));
    if (!xhci_) {
        return ZX_ERR_NO_MEMORY;
    }

    zx::bti bti;
    status = pci_.GetBti(0, &bti);
    if (status != ZX_OK) {
        return status;
    }
    xhci_->bti_handle = bti.release();

    // eXtensible Host Controller Interface revision 1.1, section 5, xhci
    // should only use BARs 0 and 1. 0 for 32 bit addressing, and 0+1 for 64 bit addressing.

    // TODO(voydanoff) find a C++ way to do this
    pci_protocol_t pci;
    pci_.GetProto(&pci);
    status = pci_map_bar_buffer(&pci, 0u, ZX_CACHE_POLICY_UNCACHED, &xhci_->mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s could not map bar\n", __func__);
        return status;
    }

    uint32_t irq_cnt;
    irq_cnt = 0;
    status = pci_.QueryIrqMode(ZX_PCIE_IRQ_MODE_MSI, &irq_cnt);
    if (status != ZX_OK) {
        zxlogf(ERROR, "pci_query_irq_mode failed %d\n", status);
        return status;
    }

    // Cap IRQ count at the number of interrupters we want to use and
    // the number of interrupters supported by XHCI.
    irq_cnt = std::min(irq_cnt, INTERRUPTER_COUNT);
    irq_cnt = std::min(irq_cnt, xhci_get_max_interrupters(xhci_));

    // select our IRQ mode
    xhci_mode_t mode;
    mode = XHCI_PCI_MSI;
    status = pci_.SetIrqMode(ZX_PCIE_IRQ_MODE_MSI, irq_cnt);
    if (status < 0) {
        zxlogf(ERROR, "MSI interrupts not available, irq_cnt: %d, err: %d\n",
               irq_cnt, status);
        zx_status_t status_legacy = pci_.SetIrqMode(ZX_PCIE_IRQ_MODE_LEGACY, 1);

        if (status_legacy < 0) {
            zxlogf(ERROR, "usb_xhci_bind Failed to set IRQ mode to either MSI "
                   "(err = %d) or Legacy (err = %d)\n",
                   status, status_legacy);
            return status;
        }

        mode = XHCI_PCI_LEGACY;
        irq_cnt = 1;
    }

    for (uint32_t i = 0; i < irq_cnt; i++) {
        // register for interrupts
        zx::interrupt irq;
        status = pci_.MapInterrupt(i, &irq);
        if (status != ZX_OK) {
            zxlogf(ERROR, "usb_xhci_bind map_interrupt failed %d\n", status);
            return status;
        }
        xhci_->irq_handles[i] = irq.release();
        num_irq_handles_initialized++;
    }
    xhci_->cfg_handle = cfg_handle;

    // used for enabling bus mastering
    pci_.GetProto(&xhci_->pci);

    status = xhci_init(xhci_, mode, irq_cnt);
    if (status != ZX_OK) {
        return status;
    }
    status = FinishBind();
    if (status != ZX_OK) {
        return status;
    }

    cleanup.cancel();
    return ZX_OK;
}

zx_status_t UsbXhci::InitPdev() {
    zx_status_t status;

    auto cleanup = fbl::MakeAutoCall([this]() {
        zx_handle_close(xhci_->bti_handle);
        mmio_buffer_release(&xhci_->mmio);
        zx_handle_close(xhci_->irq_handles[0]);
        free(xhci_);
    });

    xhci_ = static_cast<xhci_t*>(calloc(1, sizeof(xhci_t)));
    if (!xhci_) {
        return ZX_ERR_NO_MEMORY;
    }

    zx::bti bti;
    status = pdev_.GetBti(0, &bti);
    if (status != ZX_OK) {
        return status;
    }
    xhci_->bti_handle = bti.release();

    // TODO(voydanoff) find a C++ way to do this
    pdev_protocol_t pdev;
    pdev_.GetProto(&pdev);
    status = pdev_map_mmio_buffer(&pdev, PDEV_MMIO_INDEX, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                  &xhci_->mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_map_mmio failed\n", __func__);
        return status;
    }

    zx::interrupt irq;
    status = pdev_.GetInterrupt(PDEV_IRQ_INDEX, 0, &irq);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_map_interrupt failed\n", __func__);
        return status;
    }

    xhci_->irq_handles[0] = irq.release();

    status = xhci_init(xhci_, XHCI_PDEV, 1);
    if (status != ZX_OK) {
        return status;
    }
    status = FinishBind();
    if (status != ZX_OK) {
        return status;
    }

    cleanup.cancel();
    return ZX_OK;
}

zx_status_t UsbXhci::Init() {
    if (pci_.is_valid()) {
        return InitPci();
    } else if (pdev_.is_valid()) {
        return InitPdev();
    } else {
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t UsbXhci::Create(void* ctx, zx_device_t* parent) {
    fbl::AllocChecker ac;
    auto dev = std::unique_ptr<UsbXhci>(new (&ac) UsbXhci(parent));
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

static zx_driver_ops_t driver_ops = [](){
    zx_driver_ops_t ops;
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = UsbXhci::Create;
    return ops;
}();

} // namespace usb_xhci

// clang-format off
ZIRCON_DRIVER_BEGIN(usb_xhci, usb_xhci::driver_ops, "zircon", "0.1", 9)
    // PCI binding support
    BI_GOTO_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI, 0),
    BI_ABORT_IF(NE, BIND_PCI_CLASS, 0x0C),
    BI_ABORT_IF(NE, BIND_PCI_SUBCLASS, 0x03),
    BI_MATCH_IF(EQ, BIND_PCI_INTERFACE, 0x30),

    // platform bus binding support
    BI_LABEL(0),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_USB_XHCI),

    BI_ABORT(),
ZIRCON_DRIVER_END(usb_xhci)
