// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate ddk_rs;
extern crate ddk_sys;
extern crate fuchsia_zircon;

use fuchsia_zircon::{DurationNum, Status};
use fuchsia_zircon::sys as sys;
use ddk_rs::{DeviceOps, Device, UsbProtocol};
use ddk_rs as ddk;
use ddk_sys::{zx_driver_ops_t, USB_DIR_OUT, USB_TYPE_VENDOR, USB_RECIP_DEVICE};
use std::mem::size_of;
use std::slice;

#[derive(Debug, Copy, Clone, Eq, PartialEq)]
#[repr(u8)]
enum OrbCapabilityFlags {      // Does the orb ...
    HAS_GET_COLOR    = 0x01,   // ... have the GETCOLOR operation implemented ?
    HAS_GET_SEQUENCE = 0x02,   // ... have the GET_SEQUENCE operation implemented?
    HAS_AUX          = 0x04,   // ... have an AUX port and SETAUX implemented ?
    HAS_GAMMA_CORRECT= 0x08,   // ... have built-in gamma correction ?
    HAS_CURRENT_LIMIT= 0x10,   // ... behave well on 500mA USB port ?
}

#[derive(Debug, Copy, Clone, Eq, PartialEq)]
#[repr(u8)]
enum OrbRequest {
    ORB_SETSEQUENCE        = 0,  // IN  orb_sequence_t    ; always implemented.
    ORB_GETCAPABILITIES    = 1,  // OUT orb_capabilities_t; always implemented.
    ORB_SETAUX             = 2,  // IN  byte              ; optional (see flags)
    ORB_GETCOLOR           = 3,  // OUT orb_rgb_t         ; optional (see flags)
    ORB_GETSEQUENCE        = 4,  // OUT orb_sequence_t    ; optional (see flags)
    ORB_POKE_EEPROM        = 5,  // IN  <offset> <bytes>  ; optional (Orb4)
}

#[repr(C)]
#[derive(Clone, Debug, Default, Eq, PartialEq)]
struct orb_rgb_t {
    pub red: u8,
    pub green: u8,
    pub blue: u8,
}

#[repr(C)]
#[derive(Clone, Debug, Default, Eq, PartialEq)]
struct orb_color_period_t {
    pub color: orb_rgb_t,
    pub morph_time: u8,
    pub hold_time: u8,
}

unsafe fn as_slice<T: Sized>(s: &T) -> &[u8] {
    slice::from_raw_parts(s as *const T as *const u8, size_of::<T>())
}

struct MicroOrb {
    usb: UsbProtocol,
}

impl DeviceOps for MicroOrb {
    fn name(&self) -> String {
        return String::from("microorb")
    }

    fn unbind(&mut self, device: &mut Device) {
        println!("DeviceOps::unbind called");
        ddk::remove_device(device);
        println!("remove_device returned");
    }

    fn release(&mut self) {
        println!("DeviceOps::release called");
    }
}

impl MicroOrb {
    fn send(&mut self, command: OrbRequest, input: &[u8]) -> Result<(), Status> {
        let timeout = 2.seconds().after_now();
        self.usb.control_out(USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, command as u8, 0, 0,
            input, timeout)?;
        Ok(())
    }

    fn get_serial(&mut self) -> Result<String, Status> {
        let timeout = 2.seconds().after_now();
        let mut device_descriptor = Default::default();
        self.usb.get_device_descriptor(&mut device_descriptor, timeout)?;
        let serial_number = device_descriptor.iSerialNumber;
        let languages = self.usb.get_string_descriptor_languages(timeout)?;
        if languages.len() < 1 {
            return Err(Status::IO);
        }
        let language_id = languages[0];
        self.usb.get_string_descriptor(serial_number, language_id, timeout)
    }

    fn set_sequence(&mut self, sequence: &[orb_color_period_t]) -> Result<(), Status> {
        // TODO: trim sequence and limit current for older orbs.
        let period_size = size_of::<orb_color_period_t>();
        let data_length = size_of::<u8>() + sequence.len() * period_size;
        let mut data: Vec<u8> = vec![0; data_length];
        data[0] = sequence.len() as u8;
        for (color_period, slice) in sequence.iter().zip(data[1..data_length].chunks_mut(period_size)) {
            unsafe {
                slice.copy_from_slice(as_slice(color_period));
            }
        }
        self.send(OrbRequest::ORB_SETSEQUENCE, &data)
        // TODO: Get sequence back and check that it matches, or else retry.
    }

    fn set_color(&mut self, color: &orb_rgb_t) -> Result<(), Status> {
        let color_period = orb_color_period_t {
            color: color.clone(),
            morph_time: 0,
            hold_time: 1,
        };
        self.set_sequence(&[color_period])
    }
}

fn bind(parent: Device) -> Result<(), Status> {
    let usb = UsbProtocol::get(&parent)?;
    println!("Got UsbProtocol");

    // TODO: Find USB endpoint for our device

    // Create now MicroOrb instance, and add the device to the system.
    let mut orb = Box::new(MicroOrb {
        usb: usb,
    });

    let serial = orb.get_serial()?;
    println!("New MicroOrb, serial {}", serial);
    let color = orb_rgb_t {
        red: 64,
        green: 32,
        blue: 0,
    };
    orb.set_color(&color)?;

    let parent = ddk::add_device(orb, Some(&parent), ddk::DEVICE_ADD_NONE)?;

    Ok(())
}

pub extern fn microorb_bind(mut _ctx: *mut u8, parent: *mut ddk_sys::zx_device_t, _cookie: *mut *mut u8) -> sys::zx_status_t {
    println!("bind called");
    let parent_wrapped = Device::wrap(parent);
    match bind(parent_wrapped) {
        Ok(device) => sys::ZX_OK,
        Err(e) => e.into_raw(),
    }
}

pub extern fn microorb_unbind(mut _ctx: *mut u8, device: *mut ddk_sys::zx_device_t, _cookie: *mut u8) {
    println!("driver unbind called with cookie {:?}", _cookie);
}

#[no_mangle]
pub static DRIVER_OPS_MICROORB: zx_driver_ops_t = zx_driver_ops_t {
    bind: Some(microorb_bind),
    unbind: Some(microorb_unbind),
    ..ddk_sys::DEFAULT_DRIVER_OPS
};
