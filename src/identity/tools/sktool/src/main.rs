// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! `sktool` is a basic tool to scan a Fuchsia system for FIDO security keys and perform simple
//! operations on those security keys.
#![deny(missing_docs)]

mod ctap_device;
mod hid;

pub use crate::ctap_device::CtapDevice;
pub use crate::hid::HidCtapDevice;
use failure::Error;
use fuchsia_async as fasync;
use log::info;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["auth"]).expect("Can't init logger");

    info!("Starting Security Key tool.");
    print_devices().await;
    Ok(())
}

async fn print_devices() {
    match HidCtapDevice::devices().await.as_ref().map(|vec| vec.as_slice()) {
        Err(err) => println!("Fatal error reading devices: {:?}", err),
        Ok([]) => println!("No valid devices were found"),
        Ok(devices) => {
            for device in devices {
                println!(
                    "Device at {:?} has a report descriptor of size {:?}",
                    device.path(),
                    device.report_descriptor().await.unwrap_or_default().len()
                );
            }
        }
    }
}
