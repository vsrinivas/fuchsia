// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    std::io::{Read, Write},
    std::os::raw::{c_uchar, c_ushort},
    usb_bulk::InterfaceInfo,
};

const GOOGLE_VENDOR_ID: c_ushort = 0x18d1;
const ZEDMON_PRODUCT_ID: c_ushort = 0xaf00;
const VENDOR_SPECIFIC_CLASS_ID: c_uchar = 0xff;
const ZEDMON_SUBCLASS_ID: c_uchar = 0xff;
const ZEDMON_PROTOCOL_ID: c_uchar = 0x00;

/// Matches the USB interface info of a Zedmon device.
fn zedmon_match(ifc: &InterfaceInfo) -> bool {
    (ifc.dev_vendor == GOOGLE_VENDOR_ID)
        && (ifc.dev_product == ZEDMON_PRODUCT_ID)
        && (ifc.ifc_class == VENDOR_SPECIFIC_CLASS_ID)
        && (ifc.ifc_subclass == ZEDMON_SUBCLASS_ID)
        && (ifc.ifc_protocol == ZEDMON_PROTOCOL_ID)
}

/// Interface to a Zedmon device.
#[derive(Debug)]
pub struct Zedmon<InterfaceType>
where
    InterfaceType: usb_bulk::Open<InterfaceType> + Read + Write,
{
    interface: InterfaceType,
}

// NOTE: Read and Write will be used in upcoming additions.
impl<InterfaceType: usb_bulk::Open<InterfaceType> + Read + Write> Zedmon<InterfaceType> {
    /// Enumerates all connected Zedmons. Returns a `Vec<String>` of their serial numbers.
    fn enumerate() -> Vec<String> {
        let mut serials = Vec::new();

        // Instead of matching any devices, this callback extracts Zedmon serial numbers as
        // InterfaceType::open iterates through them. InterfaceType::open is expected to return an
        // error because no devices match.
        let mut cb = |info: &InterfaceInfo| -> bool {
            if zedmon_match(info) {
                let null_pos = match info.serial_number.iter().position(|&c| c == 0) {
                    Some(p) => p,
                    None => {
                        eprintln!("Warning: Detected a USB device whose serial number was not null-terminated:");
                        eprintln!(
                            "{}",
                            (*String::from_utf8_lossy(&info.serial_number)).to_string()
                        );
                        return false;
                    }
                };
                serials
                    .push((*String::from_utf8_lossy(&info.serial_number[..null_pos])).to_string());
            }
            false
        };

        assert!(
            InterfaceType::open(&mut cb).is_err(),
            "open() should return an error, as the supplied callback cannot match any devices."
        );
        serials
    }
}

/// Lists the serial numbers of all connected Zedmons.
pub fn list() -> Vec<String> {
    Zedmon::<usb_bulk::Interface>::enumerate()
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::{format_err, Error},
    };

    // Used by `interface_info`, below, as a convenient means of constructing InterfaceInfo.
    struct ShortInterface<'a> {
        dev_vendor: ::std::os::raw::c_ushort,
        dev_product: ::std::os::raw::c_ushort,
        ifc_class: ::std::os::raw::c_uchar,
        ifc_subclass: ::std::os::raw::c_uchar,
        ifc_protocol: ::std::os::raw::c_uchar,
        serial_number: &'a str,
    }

    fn interface_info(short: ShortInterface<'_>) -> InterfaceInfo {
        let mut serial = [0; 256];
        for (i, c) in short.serial_number.as_bytes().iter().enumerate() {
            serial[i] = *c;
        }

        InterfaceInfo {
            dev_vendor: short.dev_vendor,
            dev_product: short.dev_product,
            dev_class: 0,
            dev_subclass: 0,
            dev_protocol: 0,
            ifc_class: short.ifc_class,
            ifc_subclass: short.ifc_subclass,
            ifc_protocol: short.ifc_protocol,
            has_bulk_in: 0,
            has_bulk_out: 0,
            writable: 0,
            serial_number: serial,
            device_path: [0; 256usize],
        }
    }

    #[test]
    fn test_enumerate() {
        use lazy_static::lazy_static;
        use std::sync::RwLock;

        // AVAILABLE_DEVICES is state for the static method FakeInterface::open. It isn't
        // actually shared across threads, but the RwLock removes the need for a mutable static,
        // which would require unsafe blocks to access.
        lazy_static! {
            static ref AVAILABLE_DEVICES: RwLock<Vec<InterfaceInfo>> = RwLock::new(vec![]);
        }
        fn push_device(short: ShortInterface<'_>) {
            let mut devices = AVAILABLE_DEVICES.write().unwrap();
            devices.push(interface_info(short));
        }

        struct FakeInterface {}

        impl usb_bulk::Open<FakeInterface> for FakeInterface {
            fn open<F>(matcher: &mut F) -> Result<FakeInterface, Error>
            where
                F: FnMut(&InterfaceInfo) -> bool,
            {
                let devices = AVAILABLE_DEVICES.read().unwrap();
                for device in devices.iter() {
                    if matcher(device) {
                        return Ok(FakeInterface {});
                    }
                }
                Err(format_err!("No matching devices found."))
            }
        }

        impl Read for FakeInterface {
            fn read(&mut self, _: &mut [u8]) -> std::io::Result<usize> {
                Ok(0)
            }
        }

        impl Write for FakeInterface {
            fn write(&mut self, _: &[u8]) -> std::io::Result<usize> {
                Ok(0)
            }
            fn flush(&mut self) -> std::io::Result<()> {
                Ok(())
            }
        }

        // No devices connected
        let serials = Zedmon::<FakeInterface>::enumerate();
        assert!(serials.is_empty());

        // One device: not-a-zedmon-1
        push_device(ShortInterface {
            dev_vendor: 0xdead,
            dev_product: ZEDMON_PRODUCT_ID,
            ifc_class: VENDOR_SPECIFIC_CLASS_ID,
            ifc_subclass: ZEDMON_SUBCLASS_ID,
            ifc_protocol: ZEDMON_PROTOCOL_ID,
            serial_number: "not-a-zedmon-1",
        });
        let serials = Zedmon::<FakeInterface>::enumerate();
        assert!(serials.is_empty());

        // Two devices: not-a-zedmon-1, zedmon-1
        push_device(ShortInterface {
            dev_vendor: GOOGLE_VENDOR_ID,
            dev_product: ZEDMON_PRODUCT_ID,
            ifc_class: VENDOR_SPECIFIC_CLASS_ID,
            ifc_subclass: ZEDMON_SUBCLASS_ID,
            ifc_protocol: ZEDMON_PROTOCOL_ID,
            serial_number: "zedmon-1",
        });
        let serials = Zedmon::<FakeInterface>::enumerate();
        assert_eq!(serials, ["zedmon-1"]);

        // Three devices: not-a-zedmon-1, zedmon-1, not-a-zedmon-2
        push_device(ShortInterface {
            dev_vendor: GOOGLE_VENDOR_ID,
            dev_product: 0xbeef,
            ifc_class: VENDOR_SPECIFIC_CLASS_ID,
            ifc_subclass: ZEDMON_SUBCLASS_ID,
            ifc_protocol: ZEDMON_PROTOCOL_ID,
            serial_number: "not-a-zedmon-2",
        });
        let serials = Zedmon::<FakeInterface>::enumerate();
        assert_eq!(serials, ["zedmon-1"]);

        // Four devices: not-a-zedmon-1, zedmon-1, not-a-zedmon-2, zedmon-2
        push_device(ShortInterface {
            dev_vendor: GOOGLE_VENDOR_ID,
            dev_product: ZEDMON_PRODUCT_ID,
            ifc_class: VENDOR_SPECIFIC_CLASS_ID,
            ifc_subclass: ZEDMON_SUBCLASS_ID,
            ifc_protocol: ZEDMON_PROTOCOL_ID,
            serial_number: "zedmon-2",
        });
        let serials = Zedmon::<FakeInterface>::enumerate();
        assert_eq!(serials, ["zedmon-1", "zedmon-2"]);
    }
}
