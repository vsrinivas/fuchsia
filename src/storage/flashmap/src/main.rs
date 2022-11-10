// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod flashmap;
mod manager;
mod util;

use {
    crate::manager::Manager,
    anyhow::{self, Context},
    fidl_fuchsia_acpi_chromeos::DeviceMarker,
    fidl_fuchsia_nand_flashmap::ManagerRequestStream,
    fuchsia_component::{client::connect_to_named_protocol_at_dir_root, server::ServiceFs},
    fuchsia_fs::OpenFlags,
    fuchsia_inspect::{component, health::Reporter},
    fuchsia_zircon as zx,
    futures::prelude::*,
    tracing::{debug, warn},
};

const FLASH_PHYS_BASE: u64 = 0xff000000;

enum IncomingRequest {
    FlashmapManager(ManagerRequestStream),
}

/// Find and connect to the ChromeOS ACPI device.
async fn find_chromeos_acpi_device(
) -> Result<fidl_fuchsia_acpi_chromeos::DeviceProxy, anyhow::Error> {
    const ACPI_PATH: &str = "/dev/class/chromeos-acpi";
    let proxy = fuchsia_fs::directory::open_in_namespace(
        ACPI_PATH,
        OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE,
    )
    .with_context(|| format!("Opening {}", ACPI_PATH))?;

    let path = device_watcher::watch_for_files(Clone::clone(&proxy))
        .await
        .with_context(|| format!("Watching for files in {}", ACPI_PATH))?
        .try_next()
        .await
        .with_context(|| format!("Getting a file from {}", ACPI_PATH))?;

    match path {
        Some(path) => {
            connect_to_named_protocol_at_dir_root::<DeviceMarker>(&proxy, path.to_str().unwrap())
                .context("Connecting to chromeos_acpi device")
        }
        None => Err(anyhow::anyhow!(
            "Watching for files finished before any chromeos ACPI device could be found"
        )),
    }
}

#[fuchsia::main(logging = true)]
async fn main() -> Result<(), anyhow::Error> {
    let mut service_fs = ServiceFs::new_local();

    // Initialize inspect
    inspect_runtime::serve(component::inspector(), &mut service_fs)?;
    component::health().set_starting_up();

    let device = find_chromeos_acpi_device().await.context("Finding chromeos-acpi device")?;
    let address =
        match device.get_flashmap_address().await.context("Sending get flashmap address")? {
            Ok(value) => Some(value - FLASH_PHYS_BASE),
            Err(e) => {
                warn!("Failed to get flashmap address: {:?}", zx::Status::from_raw(e));
                None
            }
        };

    let manager = Manager::new(address);

    service_fs.dir("svc").add_fidl_service(IncomingRequest::FlashmapManager);
    service_fs.take_and_serve_directory_handle().context("failed to serve outgoing namespace")?;

    component::health().set_ok();
    debug!("Initialized.");

    let manager_ref = &manager;
    service_fs
        .for_each_concurrent(None, |request: IncomingRequest| async move {
            match request {
                IncomingRequest::FlashmapManager(stream) => manager_ref.serve(stream).await,
            };
        })
        .await;

    Ok(())
}
