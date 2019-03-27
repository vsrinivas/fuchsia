// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]
#![recursion_limit = "128"]

mod device;
mod device_server;
mod service_provider_server;

use {
    crate::device::TeeDeviceConnection,
    crate::device_server::DeviceServer,
    failure::{format_err, Error, ResultExt},
    fuchsia_app::server::ServicesServer,
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    fuchsia_vfs_watcher as vfs,
    futures::{
        future::{AbortHandle, Abortable},
        prelude::*,
    },
    std::{fs::File, path::PathBuf},
};

const DEV_TEE_PATH: &str = "/dev/class/tee";

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut device_list = Vec::new();

    // Enumerate existing TEE devices
    {
        let mut watcher = create_watcher()?;

        while let Some(msg) = await!(watcher.try_next())? {
            match msg.event {
                vfs::WatchEvent::EXISTING => {
                    let path = PathBuf::new().join(DEV_TEE_PATH).join(msg.filename);
                    match TeeDeviceConnection::create(&path) {
                        Ok(dev) => {
                            device_list.push(dev);
                        }
                        Err(e) => {
                            fx_log_err!("{:?}", e);
                        }
                    }
                }
                vfs::WatchEvent::IDLE => {
                    break;
                }
                _ => {
                    unreachable!("Non-WatchEvent::EXISTING found before WatchEvent::IDLE");
                }
            }
        }
    }

    if device_list.len() == 0 {
        return Err(format_err!("No TEE devices found"));
    } else if device_list.len() > 1 {
        // Cannot handle more than one TEE device
        // If this becomes supported, Manager will need to provide a method for clients to
        // enumerate and select a device to connect to.
        return Err(format_err!("Found more than 1 TEE device - this is currently not supported"));
    }

    let dev_connection = device_list.pop().unwrap();
    let (abort_handle, abort_registration) = AbortHandle::new_pair();
    let fidl_server = Abortable::new(
        ServicesServer::new()
            .add_service(DeviceServer::factory(dev_connection.clone()))
            .start()
            .context("Failed to start TEE Manager ServicesServer")?,
        abort_registration,
    )
    .unwrap_or_else(|_| Ok(()));
    await!(dev_connection.register_abort_handle_on_closed(abort_handle));

    await!(fidl_server)?;

    Ok(())
}

fn create_watcher() -> Result<vfs::Watcher, Error> {
    let tee_dir = File::open(DEV_TEE_PATH)?;
    let watcher = vfs::Watcher::new(&tee_dir)?;
    Ok(watcher)
}
