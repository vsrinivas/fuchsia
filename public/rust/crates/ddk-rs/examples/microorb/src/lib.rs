// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate ddk_rs;
extern crate fuchsia_zircon;

use fuchsia_zircon::{DurationNum, Status};
use ddk_rs::{DeviceOps, Device, DriverOps, UsbProtocol};
use ddk_rs as ddk;
use ddk_rs::sys::{USB_DIR_IN, USB_DIR_OUT, USB_TYPE_VENDOR, USB_RECIP_DEVICE};
use std::{cmp, fmt, slice, str};
use std::fmt::{Display, Formatter};
use std::mem::size_of;
use std::str::FromStr;

const IOCTL_GETSERIAL: u32 = 1;

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

impl Display for orb_rgb_t {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "#{:02x}{:02x}{:02x}", self.red, self.green, self.blue)
    }
}

impl FromStr for orb_rgb_t {
    type Err = Status;

    fn from_str(s: &str) -> Result<Self, Status> {
        if s.len() != 7 || &s[0..1] != "#" {
            return Err(Status::IO_REFUSED);
        }
        Ok(orb_rgb_t {
            red:   u8::from_str_radix(&s[1..3], 16).or(Err(Status::IO_REFUSED))?,
            green: u8::from_str_radix(&s[3..5], 16).or(Err(Status::IO_REFUSED))?,
            blue:  u8::from_str_radix(&s[5..7], 16).or(Err(Status::IO_REFUSED))?,
        })
    }
}

#[repr(C)]
#[derive(Clone, Debug, Default, Eq, PartialEq)]
struct orb_color_period_t {
    pub color: orb_rgb_t,
    pub morph_time: u8,
    pub hold_time: u8,
}

impl orb_color_period_t {
    fn from_bytes(bytes: &[u8]) -> orb_color_period_t {
        assert!(bytes.len() == 5);
        orb_color_period_t {
            color: orb_rgb_t {
                red: bytes[0],
                green: bytes[1],
                blue: bytes[2],
            },
            morph_time: bytes[3],
            hold_time: bytes[4],
        }
    }
}

impl Display for orb_color_period_t {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "{} {} {}", self.color, self.morph_time, self.hold_time)
    }
}

impl FromStr for orb_color_period_t {
    type Err = Status;

    fn from_str(s: &str) -> Result<Self, Status> {
        let mut parts = s.split(" ");
        let color = parts.next().ok_or(Status::IO_REFUSED)?.parse()?;
        let morph_time = parts.next().ok_or(Status::IO_REFUSED)?.parse().or(Err(Status::IO_REFUSED))?;
        let hold_time = parts.next().ok_or(Status::IO_REFUSED)?.parse().or(Err(Status::IO_REFUSED))?;
        if parts.next() != None {
            return Err(Status::IO_REFUSED);
        }
        Ok(orb_color_period_t {
            color: color,
            morph_time: morph_time,
            hold_time: hold_time,
        })
    }
}

struct Sequence {
    pub periods: Vec<orb_color_period_t>,
}

impl Display for Sequence {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        for entry in &self.periods {
            write!(f, "{}\n", entry)?;
        }
        Ok(())
    }
}

impl FromStr for Sequence {
    type Err = Status;

    fn from_str(s: &str) -> Result<Self, Status> {
        let periods = s.lines().map(|line| line.parse()).collect::<Result<_, _>>()?;
        Ok(Sequence {
            periods: periods
        })
    }
}

const ORB_MAX_SEQUENCE: usize = 16;

unsafe fn as_slice<T: Sized>(s: &T) -> &[u8] {
    slice::from_raw_parts(s as *const T as *const u8, size_of::<T>())
}

struct MicroOrb {
    usb: UsbProtocol,
    serial: Option<String>,
}

impl DeviceOps for MicroOrb {
    fn name(&self) -> String {
        return String::from("microorb")
    }

    fn unbind(&mut self, device: &mut Device) {
        println!("DeviceOps::unbind called");
        device.remove();
        println!("remove_device returned");
    }

    fn release(&mut self) {
        println!("DeviceOps::release called");
    }

    fn get_size(&mut self) -> u64 {
        println!("get_size");
        match self.get_sequence() {
            Ok(sequence) => sequence.to_string().len() as u64,
            Err(_) => 0,
        }
    }

    fn read(&mut self, buf: &mut [u8], offset: u64) -> Result<usize, Status> {
        println!("read {} from {}", buf.len(), offset);
        let start = offset as usize;
        let sequence = self.get_sequence()?.to_string();
        let size = cmp::min(buf.len(), cmp::max(sequence.len() - start, 0));
        buf[0..size].copy_from_slice(&sequence.as_bytes()[start..start + size]);
        println!("returning {} bytes", size);
        Ok(size)
    }

    fn write(&mut self, buf: &[u8], offset: u64) -> Result<usize, Status> {
        // Doesn't make sense to write just part of a sequence.
        if offset != 0 {
            return Err(Status::NOT_SUPPORTED);
        }

        let string = str::from_utf8(buf).or(Err(Status::IO_REFUSED))?;
        let sequence = string.parse()?;
        self.set_sequence(&sequence)?;
        Ok(buf.len())
    }

    fn ioctl(&mut self, op: u32, in_buf: &[u8], out_buf: &mut [u8]) -> Result<usize, Status> {
        match op {
            IOCTL_GETSERIAL => {
                let serial = self.get_cached_serial()?;
                let size = cmp::min(out_buf.len(), serial.len());
                out_buf[0..size].copy_from_slice(&serial.as_bytes()[0..size]);
                Ok(size)
            },
            _ => Err(Status::NOT_SUPPORTED)
        }
    }
}

impl MicroOrb {
    fn send(&mut self, command: OrbRequest, input: &[u8]) -> Result<(), Status> {
        let timeout = 2.seconds().after_now();
        self.usb.control_out(USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, command as u8, 0, 0,
            input, timeout)?;
        Ok(())
    }

    fn receive(&mut self, command: OrbRequest, output: &mut [u8]) -> Result<usize, Status> {
        let timeout = 2.seconds().after_now();
        let length = self.usb.control(USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
            command as u8, 0, 0, output, timeout)?;
        Ok(length)
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

    fn get_cached_serial(&mut self) -> Result<&str, Status> {
        match self.serial {
            None => {
                self.serial = Some(self.get_serial()?);
                Ok(self.serial.as_ref().unwrap())
            },
            Some(ref serial) => {
                Ok(&serial)
            },
        }
    }

    fn set_sequence(&mut self, sequence: &Sequence) -> Result<(), Status> {
        // TODO: trim sequence and limit current for older orbs.
        let period_size = size_of::<orb_color_period_t>();
        let data_length = size_of::<u8>() + sequence.periods.len() * period_size;
        let mut data: Vec<u8> = vec![0; data_length];
        data[0] = sequence.periods.len() as u8;
        for (color_period, slice) in sequence.periods.iter().zip(data[1..data_length].chunks_mut(period_size)) {
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
        self.set_sequence(&Sequence { periods: vec![color_period] })
    }

    fn get_sequence(&mut self) -> Result<Sequence, Status> {
        let period_size = size_of::<orb_color_period_t>();
        let mut data = vec![0; size_of::<u8>() + ORB_MAX_SEQUENCE * period_size];
        let received_length = self.receive(OrbRequest::ORB_GETSEQUENCE, &mut data)?;
        let sequence_length = data[0] as usize;
        if received_length != size_of::<u8>() + sequence_length * period_size {
            return Err(Status::IO);
        }
        let mut sequence = Vec::with_capacity(sequence_length);
        for i in 0..sequence_length {
            let start = 1 + i * period_size;
            sequence.push(orb_color_period_t::from_bytes(&data[start..start + period_size]));
        }
        Ok(Sequence { periods: sequence })
    }
}

struct MicroOrbDriver {
}

impl DriverOps for MicroOrbDriver {
    fn bind(&mut self, parent: Device) -> Result<Device, Status> {
        let usb = UsbProtocol::get(&parent)?;
        println!("Got UsbProtocol");

        // TODO: Find USB endpoint for our device

        // Create now MicroOrb instance, and add the device to the system.
        let mut orb = Box::new(MicroOrb {
            usb: usb,
            serial: None,
        });

        println!("New MicroOrb, serial {}", orb.get_cached_serial()?);
        let color = orb_rgb_t {
            red: 64,
            green: 32,
            blue: 0,
        };
        orb.set_color(&color)?;
        println!("Set color {:?}", color);

        Device::add(orb, Some(&parent), ddk::DEVICE_ADD_NONE)
    }
}

#[no_mangle]
pub extern "Rust" fn init() -> Result<Box<DriverOps>, Status> {
    Ok(Box::new(MicroOrbDriver{}))
}
