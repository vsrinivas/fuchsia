// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/protocol/usb/dci.h>
#include <ddktl/protocol/usb/function.h>
#include <fbl/array.h>
#include <fbl/ref_counted.h>
#include <fuchsia/hardware/usb/peripheral/c/fidl.h>

#include "usb-peripheral.h"

namespace usb_peripheral {

class UsbFunction;
using UsbFunctionType = ddk::Device<UsbFunction>;

// This class represents a USB function in the peripheral role configurations.
// USB function drivers bind to this.
class UsbFunction : public UsbFunctionType,
                    public ddk::UsbFunctionProtocol<UsbFunction, ddk::base_protocol>,
                    public fbl::RefCounted<UsbFunction>  {
public:
    UsbFunction(zx_device_t* parent, UsbPeripheral* peripheral, const FunctionDescriptor* desc)
        : UsbFunctionType(parent), peripheral_(peripheral), function_descriptor_(*desc) {}

    // Device protocol implementation.
    void DdkRelease();

    // UsbFunctionProtocol implementation.
    zx_status_t UsbFunctionSetInterface(const usb_function_interface_t* interface);
    zx_status_t UsbFunctionAllocInterface(uint8_t* out_intf_num);
    zx_status_t UsbFunctionAllocEp(uint8_t direction, uint8_t* out_address);
    zx_status_t UsbFunctionConfigEp(const usb_endpoint_descriptor_t* ep_desc,
                                    const usb_ss_ep_comp_descriptor_t* ss_comp_desc);
    zx_status_t UsbFunctionDisableEp(uint8_t address);
    zx_status_t UsbFunctionAllocStringDesc(const char* str, uint8_t* out_index);
    void UsbFunctionRequestQueue(usb_request_t* usb_request,
                                 const usb_request_complete_t* complete_cb);
    zx_status_t UsbFunctionEpSetStall(uint8_t ep_address);
    zx_status_t UsbFunctionEpClearStall(uint8_t ep_address);
    size_t UsbFunctionGetRequestSize();

    zx_status_t SetConfigured(bool configured, usb_speed_t speed);
    zx_status_t SetInterface(uint8_t interface, uint8_t alt_setting);
    zx_status_t Control(const usb_setup_t* setup, const void* write_buffer, size_t write_size,
                        void* read_buffer, size_t read_size, size_t* out_read_actual);

    inline const usb_descriptor_header_t* GetDescriptors(size_t* out_length) const {
        *out_length = descriptors_.size();
        return reinterpret_cast<usb_descriptor_header_t*>(descriptors_.get());
    }

    inline const FunctionDescriptor& GetFunctionDescriptor() const { return function_descriptor_; }

    inline uint8_t GetNumInterfaces() const {
        return num_interfaces_;
    }

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(UsbFunction);

    UsbPeripheral* peripheral_;
    ddk::UsbFunctionInterfaceClient function_intf_;
    const FunctionDescriptor function_descriptor_;

    uint8_t num_interfaces_ = 0;
    fbl::Array<uint8_t> descriptors_;
};

 } // namespace usb_peripheral
