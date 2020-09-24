// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Safe wrapper to talk to USB devices from a host.
//!
//! # Supports
//!  - OS backends: Linux and MacOS.
//!  - Bulk only interfaces with a single IN and OUT pipe.
//!
//! # Examples
//!
//! See tests for examples using the Zedmon power monitor.

use anyhow::{format_err, Error};
use std::io::{ErrorKind, Read, Write};
use std::os::raw::c_void;

mod usb;

/// A collection of information about an interface.
///
/// Used for matching interfaces to open.
pub type InterfaceInfo = usb::usb_ifc_info;

/// Opens a USB interface.
///
/// `matcher` will be called on every discovered interface.  When `matcher` returns true, that
/// interface will be opened.
pub trait Open<T> {
    fn open<F>(matcher: &mut F) -> Result<T, Error>
    where
        F: FnMut(&InterfaceInfo) -> bool;
}

/// A USB Interface.
///
/// See top-level crate docs for an example.
#[derive(Debug)]
pub struct Interface {
    interface: *mut usb::UsbInterface,
}

/// Send implementation for USB interface.
///
///  This struct wraps a raw pointer which according to the Rust documentation found at
///  https://doc.rust-lang.org/nomicon/send-and-sync.html: "However raw pointers
///  are, strictly speaking, marked as thread-unsafe as more of a lint. Doing anything useful with
///  a raw pointer requires dereferencing it, which is already unsafe. In that sense, one could
///  argue that it would be "fine" for them to be marked as thread safe."
unsafe impl Send for Interface {}

impl Open<Interface> for Interface {
    fn open<F>(matcher: &mut F) -> Result<Interface, Error>
    where
        F: FnMut(&InterfaceInfo) -> bool,
    {
        // Generate a trampoline for calling our matcher from a C callback.
        extern "C" fn trampoline<F>(ifc_ptr: *mut usb::usb_ifc_info, data: *mut c_void) -> bool
        where
            F: FnMut(&InterfaceInfo) -> bool,
        {
            // Undoes the cast of `matcher` to `*mut c_void` performed in the call to
            // `usb::interface_open`, below.
            let callback: &mut F = unsafe { &mut *(data as *mut F) };

            // Casts the raw interface pointer to a safe reference. Requires that `ifc_ptr`, as
            // as provided by the C++ `interface_open`, be a valid pointer to a `usb::usb_ifc_info`.
            let interface = unsafe { &*ifc_ptr };

            (*callback)(interface)
        }

        // Call into the low level driver to open the interface.  The matcher itself is passesd
        // as a void pointer which is re-intepreted by the above trampoline.  The foreign function
        // call requires an unsafe block.
        let device_ptr = unsafe {
            usb::interface_open(Some(trampoline::<F>), matcher as *mut F as *mut c_void, 200)
        };
        if !device_ptr.is_null() {
            return Ok(Interface { interface: device_ptr as *mut usb::UsbInterface });
        } else {
            return Err(format_err!("No device matched."));
        }
    }
}

impl Drop for Interface {
    fn drop(&mut self) {
        // Foreign function call requires unsafe block.
        unsafe {
            usb::interface_close(self.interface);
        }
    }
}

impl Read for Interface {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        let buf_ptr = buf.as_mut_ptr() as *mut c_void;

        // Foreign function call requires unsafe block.
        let ret =
            unsafe { usb::interface_read(self.interface, buf_ptr, buf.len() as usb::ssize_t) };

        if ret < 0 {
            return Err(std::io::Error::new(ErrorKind::Other, format!("Read error: {}", ret)));
        }
        return Ok(ret as usize);
    }
}

impl Write for Interface {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        let buf_ptr = buf.as_ptr() as *const c_void;

        // Foreign function call requires unsafe block.
        let ret =
            unsafe { usb::interface_write(self.interface, buf_ptr, buf.len() as usb::ssize_t) };

        if ret < 0 {
            return Err(std::io::Error::new(ErrorKind::Other, format!("Write error: {}", ret)));
        }
        return Ok(ret as usize);
    }

    fn flush(&mut self) -> std::io::Result<()> {
        // Do nothing as we're not buffered.
        Ok(())
    }
}

/// Tests are based on the [Zedmon power monitor](https://fuchsia.googlesource.com/zedmon).
///
/// In order to run them, the host should be connected to exactly one Zedmon device, satisfying:
///  - Hardware version 2.1;
///  - Firmware built from the Zedmon repository's revision 9765b27b5f, or equivalent.
#[cfg(test)]
mod tests {
    use super::*;

    fn zedmon_match(ifc: &InterfaceInfo) -> bool {
        (ifc.dev_vendor == 0x18d1)
            && (ifc.dev_product == 0xaf00)
            && (ifc.ifc_class == 0xff)
            && (ifc.ifc_subclass == 0xff)
            && (ifc.ifc_protocol == 0x00)
    }

    #[test]
    fn test_zedmon_enumeration() {
        let mut serials = Vec::new();
        let mut cb = |info: &InterfaceInfo| -> bool {
            if zedmon_match(info) {
                let null_pos = match info.serial_number.iter().position(|&c| c == 0) {
                    Some(p) => p,
                    None => {
                        return false;
                    }
                };
                serials
                    .push((*String::from_utf8_lossy(&info.serial_number[..null_pos])).to_string());
            }
            false
        };
        let result = Interface::open(&mut cb);
        assert!(result.is_err(), "Enumeration matcher should not open any device.");
        assert_eq!(serials.len(), 1, "Host should have exactly one zedmon device connected");
    }

    #[test]
    fn test_zedmon_read_parameter() -> Result<(), Error> {
        // Open USB interface.
        let mut matcher = |info: &InterfaceInfo| -> bool { zedmon_match(info) };
        let mut interface = Interface::open(&mut matcher)?;

        // Send a Query Parameter request.
        interface.write(&[0x02, 0x00])?;

        // Read response.
        let mut packet = [0x00; 64];
        let len = interface.read(&mut packet)?;

        // Verify the parameter is as we expect.  Format of this packet can be
        // found at https://fuchsia.googlesource.com/zedmon/+/HEAD/docs/usb_proto.md
        assert_eq!(
            packet[..len - 1],
            [
                0x83, 0x73, 0x68, 0x75, 0x6e, 0x74, 0x5f, 0x72, 0x65, 0x73, 0x69, 0x73, 0x74, 0x61,
                0x6e, 0x63, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40,
                0x0a, 0xd7, 0x23, 0x3c, 0x00, 0x00, 0x00
            ][..]
        );

        Ok(())
    }
}
