// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Tests are based on the [Zedmon power monitor](https://fuchsia.googlesource.com/zedmon).
///
/// In order to run them, the host should be connected to exactly one fastboot device
#[cfg(test)]
mod tests {
    use {
        anyhow::Error,
        fastboot::{
            command::{ClientVariable, Command},
            reply::Reply,
            send,
        },
        usb_bulk::{Interface, InterfaceInfo, Open},
    };

    fn fastboot_match(ifc: &InterfaceInfo) -> bool {
        (ifc.dev_vendor == 0x18d1) && ((ifc.dev_product == 0xaf00) || (ifc.dev_product == 0x0d02))
    }

    #[test]
    fn test_fastboot_enumeration() {
        let mut serials = Vec::new();
        let mut cb = |info: &InterfaceInfo| -> bool {
            if fastboot_match(info) {
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
        assert_eq!(serials.len(), 1, "Host should have exactly one fastboot device connected");
    }

    #[test]
    pub fn test_get_fast_version() -> Result<(), Error> {
        let mut usb_interface = Interface::open(&mut fastboot_match)?;
        let response = send(Command::GetVar(ClientVariable::Version), &mut usb_interface);
        assert_eq!(response.unwrap(), Reply::Okay("0.4".to_string()));
        Ok(())
    }
}
