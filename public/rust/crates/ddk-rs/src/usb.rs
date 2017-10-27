// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use ddk_sys;
use ddk_sys::{device_get_protocol, usb_protocol_t, usb_request_type_t, usb_request_value_t, USB_DIR_IN};
use zircon::{Status, Time};
use into_result;
use Device;

pub struct UsbProtocol(usb_protocol_t);

impl UsbProtocol {
    pub fn get(device: &Device) -> Result<UsbProtocol, Status> {
        let mut protocol: usb_protocol_t = Default::default();
        let status = unsafe {
            device_get_protocol(device.device, ddk_sys::ZX_PROTOCOL_USB,
                &mut protocol as *mut usb_protocol_t as *mut u8)
        };
        into_result(status, || UsbProtocol(protocol))
    }

    fn control(&mut self, request_type: usb_request_type_t, request: usb_request_value_t,
        value: u16, index: u16, data: &mut [u8], timeout: Time) -> Result<usize, Status>
    {
        let mut out_length: usize = 0;
        let status = unsafe {
            ((*self.0.ops).control)(self.0.ctx, request_type, request, value, index,
                data.as_mut_ptr(), data.len(), timeout.nanos(), &mut out_length)
        };
        into_result(status, || out_length)
    }

    pub fn get_descriptor(&mut self, request_type: usb_request_type_t, descriptor_type: u16,
        descriptor_index: u16, data: &mut [u8], timeout: Time) -> Result<usize, Status>
    {
        self.control(request_type | USB_DIR_IN, usb_request_value_t::USB_REQ_GET_DESCRIPTOR,
            descriptor_type << 8 | descriptor_index, 0, data, timeout)
    }

    pub fn get_status(&mut self, request_type: usb_request_type_t, index: u16, data: &mut [u8],
        timeout: Time) -> Result<usize, Status>
    {
        self.control(request_type | USB_DIR_IN, usb_request_value_t::USB_REQ_GET_STATUS, 0, index,
            data, timeout)
    }
}
