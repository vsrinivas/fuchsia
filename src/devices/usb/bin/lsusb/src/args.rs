// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::{FromArgValue, FromArgs};

#[derive(Debug, PartialEq)]
pub struct UsbDevice {
    pub vendor_id: u16,
    pub product_id: Option<u16>,
}

impl FromArgValue for UsbDevice {
    fn from_arg_value(value: &str) -> Result<Self, String> {
        if let Some((vid, pid)) = value.split_once(':') {
            let vid = u16::from_str_radix(vid, 16)
                .map_err(|_| "Vendor ID is not a valid hexadecimal number'.".to_string())?;

            let pid = u16::from_str_radix(pid, 16)
                .map_err(|_| "Product ID is not a valid hexadecimal number'.".to_string())?;

            Ok(UsbDevice { vendor_id: vid, product_id: Some(pid) })
        } else {
            let vid = u16::from_str_radix(value, 16)
                .map_err(|_| "Vendor id is not a valid hexadecimal number'.".to_string())?;

            Ok(UsbDevice { vendor_id: vid, product_id: None })
        }
    }
}

#[derive(FromArgs, Debug, PartialEq)]
/// Display USB information
pub struct Args {
    #[argh(switch, short = 't')]
    /// prints USB device tree
    pub tree: bool,
    #[argh(switch, short = 'v')]
    /// verbose output (prints descriptors)
    pub verbose: bool,
    #[argh(option, short = 'c')]
    /// prints configuration descriptor for specified configuration (rather than
    /// current configuration)
    pub configuration: Option<u8>,
    #[argh(option, short = 'd')]
    /// shows only devices with the specified vendor and product ID numbers (in hexadecimal)
    /// UsbDevice must be in format vendor[:product]
    pub device: Option<UsbDevice>,
}
