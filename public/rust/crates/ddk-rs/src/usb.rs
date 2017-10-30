// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use byteorder::{ByteOrder, LittleEndian};
use ddk_sys;
use ddk_sys::{device_get_protocol, usb_device_descriptor_t, usb_protocol_t, usb_request_type_t, USB_DIR_IN, USB_DIR_MASK, USB_DIR_OUT, USB_DT_DEVICE, USB_DT_STRING, USB_REQ_GET_DESCRIPTOR, USB_REQ_GET_STATUS};
use std::char::decode_utf16;
use std::mem::size_of;
use std::slice;
use zircon::{Status, Time};
use into_result;
use Device;

pub struct UsbProtocol(usb_protocol_t);

unsafe fn as_slice<T: Sized>(s: &mut T) -> &mut [u8] {
    slice::from_raw_parts_mut(s as *mut T as *mut u8, size_of::<T>())
}

impl UsbProtocol {
    /// Get a new instance of UsbProtocol for a given device, if it is available.
    pub fn get(device: &Device) -> Result<UsbProtocol, Status> {
        let mut protocol: usb_protocol_t = Default::default();
        let status = unsafe {
            device_get_protocol(device.device, ddk_sys::ZX_PROTOCOL_USB,
                &mut protocol as *mut usb_protocol_t as *mut u8)
        };
        into_result(status, || UsbProtocol(protocol))
    }

    /// Send a USB control message.
    fn control(&mut self, request_type: usb_request_type_t, request: u8,
        value: u16, index: u16, data: &mut [u8], timeout: Time) -> Result<usize, Status>
    {
        let mut out_length: usize = 0;
        let status = unsafe {
            ((*self.0.ops).control)(self.0.ctx, request_type, request, value, index,
                data.as_mut_ptr(), data.len(), timeout.nanos(), &mut out_length)
        };
        into_result(status, || out_length)
    }

    fn control_struct<T: Sized>(&mut self, request_type: usb_request_type_t, request: u8,
        value: u16, index: u16, data: &mut T, timeout: Time) -> Result<usize, Status>
    {
        let mut out_length: usize = 0;
        let status = unsafe {
            ((*self.0.ops).control)(self.0.ctx, request_type, request, value, index,
                data as *mut T as *mut u8, size_of::<T>(), timeout.nanos(), &mut out_length)
        };
        into_result(status, || out_length)
    }

    /// Send a USB control message which only sends data, doesn't receive any.
    pub fn control_out(&mut self, request_type: usb_request_type_t, request: u8,
        value: u16, index: u16, data: &[u8], timeout: Time) -> Result<usize, Status>
    {
        // Force request type to be an out request, so data doesn't need to be mutable.
        let request_type_out = (request_type & !USB_DIR_MASK) | USB_DIR_OUT;
        let mut out_length: usize = 0;
        let status = unsafe {
            ((*self.0.ops).control)(self.0.ctx, request_type_out, request, value, index,
                data.as_ptr() as *mut u8, data.len(), timeout.nanos(), &mut out_length)
        };
        into_result(status, || out_length)
    }

    /// Get any sort of USB descriptor.
    fn get_descriptor(&mut self, descriptor_type: u8, descriptor_index: u8, language_id: u16,
        data: &mut [u8], timeout: Time) -> Result<usize, Status>
    {
        self.control(USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
            (descriptor_type as u16) << 8 | descriptor_index as u16, language_id, data, timeout)
    }

    /// Get the USB device descriptor.
    pub fn get_device_descriptor(&mut self, descriptor: &mut usb_device_descriptor_t, timeout: Time)
        -> Result<(), Status>
    {
        let data = unsafe { as_slice(descriptor) };
        let length = self.get_descriptor(USB_DT_DEVICE, 0, 0, data, timeout)?;
        if length != size_of::<usb_device_descriptor_t>() {
            return Err(Status::IO);
        }
        Ok(())
    }

    /// Get the given USB string descriptor.
    pub fn get_string_descriptor(&mut self, descriptor_index: u8, language_id: u16, timeout: Time)
        -> Result<String, Status>
    {
        let mut byte_buffer = [0; 254];
        let length = self.get_descriptor(USB_DT_STRING, descriptor_index, language_id,
            &mut byte_buffer, timeout)?;
        // Must be an even number of bytes to be valid UTF-16-LE
        if length < 2 || length % 2 != 0 {
            return Err(Status::IO);
        }
        // Skip the first two bytes, which are the length and descriptor type.
        decode_utf16(byte_buffer[2..length].chunks(2).map(LittleEndian::read_u16))
            .collect::<Result<String, _>>().or(Err(Status::IO))
    }

    /// Get the list of available languages for string descriptors.
    pub fn get_string_descriptor_languages(&mut self, timeout: Time) -> Result<Vec<u16>, Status> {
        let mut byte_buffer = [0; 254];
        // Descriptor index 0, language ID 0 should contain a list of all supported language IDs.
        let length = self.get_descriptor(USB_DT_STRING, 0, 0, &mut byte_buffer, timeout)?;
        // Should be an even number of bytes, starting with the length and descriptor type (which we
        // ignore).
        if length < 2 || length % 2 != 0 {
            return Err(Status::IO);
        }
        Ok(byte_buffer[2..length].chunks(2).map(LittleEndian::read_u16).collect::<Vec<u16>>())
    }

    pub fn get_status(&mut self, request_type: usb_request_type_t, index: u16, data: &mut [u8],
        timeout: Time) -> Result<usize, Status>
    {
        self.control(request_type | USB_DIR_IN, USB_REQ_GET_STATUS, 0, index,
            data, timeout)
    }
}
