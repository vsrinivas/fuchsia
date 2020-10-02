// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Context, Result},
    fastboot::{
        command::{ClientVariable, Command},
        reply::Reply,
        send, upload,
    },
    std::{
        fs::read,
        io::{Read, Write},
    },
    usb_bulk::{Interface, InterfaceInfo, Open},
};

//TODO(fxbug.dev/52733) - this info will probably get rolled into the target struct
#[derive(Debug)]
pub struct FastbootDevice {
    pub product: String,
    pub serial: String,
}

fn is_fastboot_match(info: &InterfaceInfo) -> bool {
    (info.dev_vendor == 0x18d1) && ((info.dev_product == 0x4ee0) || (info.dev_product == 0x0d02))
}

fn extract_serial_number(info: &InterfaceInfo) -> String {
    let null_pos = match info.serial_number.iter().position(|&c| c == 0) {
        Some(p) => p,
        None => {
            return "".to_string();
        }
    };
    (*String::from_utf8_lossy(&info.serial_number[..null_pos])).to_string()
}

fn open_interface<F>(mut cb: F) -> Result<Interface>
where
    F: FnMut(&InterfaceInfo) -> bool,
{
    let mut open_cb = |info: &InterfaceInfo| -> bool {
        if is_fastboot_match(info) {
            cb(info)
        } else {
            // Do not open.
            false
        }
    };
    Interface::open(&mut open_cb)
}

fn enumerate_interfaces<F>(mut cb: F)
where
    F: FnMut(&InterfaceInfo),
{
    let mut cb = |info: &InterfaceInfo| -> bool {
        if is_fastboot_match(info) {
            cb(info)
        }
        // Do not open anything.
        false
    };
    let _result = Interface::open(&mut cb);
}

fn find_serial_numbers() -> Vec<String> {
    let mut serials = Vec::new();
    let cb = |info: &InterfaceInfo| serials.push(extract_serial_number(info));
    enumerate_interfaces(cb);
    serials
}

pub fn find_devices() -> Vec<FastbootDevice> {
    let mut products = Vec::new();
    let serials = find_serial_numbers();
    for serial in serials {
        match open_interface_with_serial(&serial) {
            Ok(mut usb_interface) => {
                if let Ok(Reply::Okay(version)) =
                    send(Command::GetVar(ClientVariable::Version), &mut usb_interface)
                {
                    // Only support 0.4 right now.
                    if version == "0.4".to_string() {
                        if let Ok(Reply::Okay(product)) =
                            send(Command::GetVar(ClientVariable::Product), &mut usb_interface)
                        {
                            products.push(FastbootDevice { product, serial })
                        }
                    }
                }
            }
            Err(e) => log::error!("error opening usb interface: {}", e),
        }
    }
    products
}

pub fn open_interface_with_serial(serial: &String) -> Result<Interface> {
    open_interface(|info: &InterfaceInfo| -> bool { extract_serial_number(info) == *serial })
}

pub fn stage<T: Read + Write>(interface: &mut T, file: &String) -> Result<()> {
    let bytes = read(file)?;
    log::debug!("uploading file size: {}", bytes.len());
    match upload(&bytes[..], interface).context(format!("uploading {}", file))? {
        Reply::Okay(s) => {
            log::debug!("Received response from download command: {}", s);
            Ok(())
        }
        Reply::Fail(s) => bail!("Failed to upload {}: {}", file, s),
        _ => bail!("Unexpected reply from fastboot device for download"),
    }
}

pub fn flash<T: Read + Write>(interface: &mut T, file: &String, name: &String) -> Result<()> {
    let bytes = read(file)?;
    log::debug!("uploading file size: {}", bytes.len());
    let upload_reply = upload(&bytes[..], interface).context(format!("uploading {}", file))?;
    match upload_reply {
        Reply::Okay(s) => log::debug!("Received response from download command: {}", s),
        Reply::Fail(s) => bail!("Failed to upload {}: {}", file, s),
        _ => bail!("Unexpected reply from fastboot device for download: {:?}", upload_reply),
    }
    let send_reply = send(Command::Flash(name.to_string()), interface).context("sending flash")?;
    match send_reply {
        Reply::Okay(_) => {
            log::debug!("Successfully flashed parition: {}", name);
            Ok(())
        }
        Reply::Fail(s) => bail!("Failed to flash \"{}\": {}", name, s),
        _ => bail!("Unexpected reply from fastboot device for flash command: {:?}", send_reply),
    }
}

pub fn erase<T: Read + Write>(interface: &mut T, name: &String) -> Result<()> {
    let reply = send(Command::Erase(name.to_string()), interface).context("sending erase")?;
    match reply {
        Reply::Okay(_) => {
            log::debug!("Successfully erased parition: {}", name);
            Ok(())
        }
        Reply::Fail(s) => bail!("Failed to erase \"{}\": {}", name, s),
        _ => bail!("Unexpected reply from fastboot device for erase command: {:?}", reply),
    }
}

pub fn reboot<T: Read + Write>(interface: &mut T) -> Result<()> {
    let reply = send(Command::Reboot, interface).context("sending reboot")?;
    match reply {
        Reply::Okay(_) => {
            log::debug!("Successfully sent reboot");
            Ok(())
        }
        Reply::Fail(s) => bail!("Failed to reboot: {}", s),
        _ => bail!("Unexpected reply from fastboot device for reboot command: {:?}", reply),
    }
}

pub fn reboot_bootloader<T: Read + Write>(interface: &mut T) -> Result<()> {
    match send(Command::RebootBootLoader, interface).context("sending reboot bootloader")? {
        Reply::Okay(_) => {
            log::debug!("Successfully sent reboot bootloader");
            Ok(())
        }
        Reply::Fail(s) => bail!("Failed to reboot to bootloader: {}", s),
        _ => bail!("Unexpected reply from fastboot device for reboot bootloader command"),
    }
}

pub fn continue_boot<T: Read + Write>(interface: &mut T) -> Result<()> {
    match send(Command::Continue, interface).context("sending continue")? {
        Reply::Okay(_) => {
            log::debug!("Successfully sent continue");
            Ok(())
        }
        Reply::Fail(s) => bail!("Failed to continue: {}", s),
        _ => bail!("Unexpected reply from fastboot device for continue command"),
    }
}

pub fn set_active<T: Read + Write>(interface: &mut T, slot: &String) -> Result<()> {
    match send(Command::SetActive(slot.to_string()), interface).context("sending set_active")? {
        Reply::Okay(_) => {
            log::debug!("Successfully sent set_active");
            Ok(())
        }
        Reply::Fail(s) => bail!("Failed to set_active: {}", s),
        _ => bail!("Unexpected reply from fastboot device for set_active command"),
    }
}

pub fn oem<T: Read + Write>(interface: &mut T, cmd: &String, params: Vec<String>) -> Result<()> {
    match send(Command::Oem(cmd.to_string(), params), interface).context("sending oem")? {
        Reply::Okay(_) => {
            log::debug!("Successfully sent oem command \"{}\"", cmd);
            Ok(())
        }
        Reply::Fail(s) => bail!("Failed to oem \"{}\": {}", cmd, s),
        _ => bail!("Unexpected reply from fastboot device for oem command \"{}\"", cmd),
    }
}

/// Tests are based on the [Zedmon power monitor](https://fuchsia.googlesource.com/zedmon).
///
/// In order to run them, the host should be connected to exactly one fastboot device
/// The fastboot device should match the appropriate producting information and be running fastboot
/// version 0.4.
///
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_fastboot_enumeration() {
        let serials = find_serial_numbers();
        assert_eq!(serials.len(), 1, "Host should have exactly one fastboot device connected");
    }

    #[test]
    pub fn test_find_devices() -> Result<()> {
        let devices = find_devices();
        assert_eq!(devices.len(), 1, "Host should have exactly one fastboot device connected");
        Ok(())
    }
}
