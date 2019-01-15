// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-function.h"
#include "usb-peripheral.h"

#include <ddk/debug.h>
#include <fbl/array.h>

namespace usb_peripheral {

void UsbFunction::DdkRelease() {
    // Release the reference now that devmgr no longer has a pointer to the function.
    __UNUSED bool dummy = Release();
}

// UsbFunctionProtocol implementation.
zx_status_t UsbFunction::UsbFunctionSetInterface(const usb_function_interface_t* function_intf) {
    if (function_intf == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    function_intf_ = ddk::UsbFunctionInterfaceClient(function_intf);

    size_t length = function_intf_.GetDescriptorsSize();
    fbl::AllocChecker ac;
    auto* descriptors = new (&ac) uint8_t[length];
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    size_t actual;
    function_intf_.GetDescriptors(descriptors, length, &actual);
    if (actual != length) {
        zxlogf(ERROR, "UsbFunctionInterfaceClient::GetDescriptors() failed\n");
        delete[] descriptors;
        return ZX_ERR_INTERNAL;
    }

    auto status = peripheral_->ValidateFunction(fbl::RefPtr<UsbFunction>(this), descriptors, length,
                                                &num_interfaces_);
    if (status != ZX_OK) {
        delete[] descriptors;
        return status;
    }

    descriptors_.reset(descriptors, length);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    return peripheral_->FunctionRegistered();
}

zx_status_t UsbFunction::UsbFunctionAllocInterface(uint8_t* out_intf_num) {
    return peripheral_->AllocInterface(fbl::RefPtr<UsbFunction>(this), out_intf_num);
}

zx_status_t UsbFunction::UsbFunctionAllocEp(uint8_t direction, uint8_t* out_address) {
    return peripheral_->AllocEndpoint(fbl::RefPtr<UsbFunction>(this), direction, out_address);
}

zx_status_t UsbFunction::UsbFunctionConfigEp(const usb_endpoint_descriptor_t* ep_desc,
                                             const usb_ss_ep_comp_descriptor_t* ss_comp_desc) {
    return peripheral_->dci().ConfigEp(ep_desc, ss_comp_desc);
}

zx_status_t UsbFunction::UsbFunctionDisableEp(uint8_t address) {
    return peripheral_->dci().DisableEp(address);
}

zx_status_t UsbFunction::UsbFunctionAllocStringDesc(const char* str, uint8_t* out_index) {
    return peripheral_->AllocStringDesc(str, out_index);
}

void UsbFunction::UsbFunctionRequestQueue(usb_request_t* usb_request,
                                          const usb_request_complete_t* complete_cb) {
    return peripheral_->dci().RequestQueue(usb_request, complete_cb);
}

zx_status_t UsbFunction::UsbFunctionEpSetStall(uint8_t ep_address) {
    return peripheral_->dci().EpSetStall(ep_address);
}

zx_status_t UsbFunction::UsbFunctionEpClearStall(uint8_t ep_address) {
    return peripheral_->dci().EpClearStall(ep_address);
}

size_t UsbFunction::UsbFunctionGetRequestSize() {
    return peripheral_->ParentRequestSize();
}

zx_status_t UsbFunction::SetConfigured(bool configured, usb_speed_t speed) {
    if (function_intf_.is_valid()) {
        return function_intf_.SetConfigured(configured, speed);
    } else {
        return ZX_ERR_BAD_STATE;
    }
}

zx_status_t UsbFunction::SetInterface(uint8_t interface, uint8_t alt_setting) {
    if (function_intf_.is_valid()) {
        return function_intf_.SetInterface(interface, alt_setting);
    } else {
        return ZX_ERR_BAD_STATE;
    }
}

zx_status_t UsbFunction::Control(const usb_setup_t* setup, const void* write_buffer,
                                 size_t write_size, void* read_buffer, size_t read_size,
                                 size_t* out_read_actual) {
    if (function_intf_.is_valid()) {
        return function_intf_.Control(setup, write_buffer, write_size, read_buffer, read_size,
                                      out_read_actual);
    } else {
        return ZX_ERR_BAD_STATE;
    }
}

} // namespace usb_peripheral
