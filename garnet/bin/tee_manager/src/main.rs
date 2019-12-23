// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "128"]

mod device;
mod device_server;
mod provider_server;

use {
    crate::device::TeeDeviceConnection,
    crate::device_server::serve_passthrough,
    failure::{format_err, Error},
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_tee::DeviceMarker,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::fx_log_err,
    fuchsia_vfs_watcher as vfs,
    futures::{
        future::{abortable, Aborted},
        prelude::*,
    },
    io_util::{open_directory_in_namespace, OPEN_RIGHT_READABLE},
    std::path::PathBuf,
};

const DEV_TEE_PATH: &str = "/dev/class/tee";

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut device_list = Vec::new();

    // Enumerate existing TEE devices
    {
        let mut watcher = create_watcher().await?;

        while let Some(msg) = watcher.try_next().await? {
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
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_service_at(DeviceMarker::NAME, |channel| {
        fasync::spawn(
            serve_passthrough(dev_connection.clone(), channel)
                .unwrap_or_else(|e| fx_log_err!("{:?}", e)),
        );
        None
    });
    fs.take_and_serve_directory_handle()?;
    let (fidl_server, abort_handle) = abortable(fs.collect());
    dev_connection.register_abort_handle_on_closed(abort_handle).await;

    let _: Result<(), Aborted> = fidl_server.await;

    Ok(())
}

async fn create_watcher() -> Result<vfs::Watcher, Error> {
    let tee_dir = open_directory_in_namespace(DEV_TEE_PATH, OPEN_RIGHT_READABLE)?;
    let watcher = vfs::Watcher::new(tee_dir).await?;
    Ok(watcher)
}
