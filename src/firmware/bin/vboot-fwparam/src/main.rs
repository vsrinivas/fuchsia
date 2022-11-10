// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod error;
mod nvdata;

use crate::nvdata::{Nvdata, NvdataVersion};
use anyhow::{anyhow, Context};
use fidl_fuchsia_acpi_chromeos as chrome_acpi;
use fidl_fuchsia_device::ControllerMarker;
use fidl_fuchsia_hardware_nvram as fnvram;
use fidl_fuchsia_nand_flashmap::{FlashmapMarker, FlashmapProxy, ManagerMarker};
use fidl_fuchsia_vboot::FirmwareParamRequestStream;
use fuchsia_component::{
    client::{
        connect_to_named_protocol_at_dir_root, connect_to_protocol, connect_to_protocol_at_path,
    },
    server::ServiceFs,
};
use fuchsia_fs::OpenFlags;
use fuchsia_inspect::{component, health::Reporter};
use fuchsia_zircon as zx;
use futures::{StreamExt, TryStreamExt};
use tracing::{error, info};

enum IncomingRequest {
    FirmwareParam(FirmwareParamRequestStream),
}

/// Find and connect to the nvram device containing the vboot nvdata.
async fn find_nvram_device() -> Result<fnvram::DeviceProxy, anyhow::Error> {
    // The RTC houses the nvram where nvdata is stored.
    let nvram_path = "/dev/class/rtc";
    let proxy = fuchsia_fs::directory::open_in_namespace(
        nvram_path,
        OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE,
    )
    .context("Opening /dev/class/rtc")?;
    let contents =
        fuchsia_fs::directory::readdir(&proxy).await.context("Reading /dev/class/rtc")?;
    if contents.len() > 1 {
        return Err(anyhow!("Too many rtc devices"));
    }
    if contents.is_empty() {
        return Err(anyhow!("No rtc devices found"));
    }

    Ok(connect_to_protocol_at_path::<fnvram::DeviceMarker>(
        &(nvram_path.to_owned() + "/" + &contents[0].name),
    )
    .context("Connecting to nvram device")?)
}

/// Find the NAND flash device and start the flashmap service on it.
async fn find_flashmap_device() -> Result<FlashmapProxy, anyhow::Error> {
    // Look for the first flash device.
    let nand_path = "/dev/class/nand";
    let proxy = fuchsia_fs::directory::open_in_namespace(
        nand_path,
        OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE,
    )
    .context("Opening /dev/class/nand")?;
    let contents =
        fuchsia_fs::directory::readdir(&proxy).await.context("Reading /dev/class/nand")?;
    if contents.len() > 1 {
        return Err(anyhow!("Too many nand devices"));
    }
    if contents.is_empty() {
        return Err(anyhow!("No nand devices found"));
    }

    // Connect to the driver manager and try to bind the broker.
    let device = connect_to_protocol_at_path::<ControllerMarker>(
        &(nand_path.to_owned() + "/" + &contents[0].name),
    )
    .context("Connecting to nand device")?;

    // Get the "real" path of the device, so that we can access the broker.
    let path = device
        .get_topological_path()
        .await
        .context("Sending get topological path")?
        .map_err(zx::Status::from_raw)
        .context("Getting topological path")?;

    // Connect to the broker.
    let (local, remote) = zx::Channel::create().context("Creating channels")?;
    fdio::service_connect(&(path + "/broker"), remote).context("Connecting to broker")?;

    // Start the flashmap manager on the device we found.
    let (proxy, server) =
        fidl::endpoints::create_proxy::<FlashmapMarker>().context("Creating proxy and stream")?;
    let manager =
        connect_to_protocol::<ManagerMarker>().context("Connecting to flashmap manager")?;
    manager.start(fidl::endpoints::ClientEnd::new(local), server).context("Sending start")?;
    Ok(proxy)
}

/// Find and connect to the ChromeOS ACPI device.
async fn find_chromeos_acpi_device() -> Result<chrome_acpi::DeviceProxy, anyhow::Error> {
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
        Some(path) => connect_to_named_protocol_at_dir_root::<chrome_acpi::DeviceMarker>(
            &proxy,
            path.to_str().unwrap(),
        )
        .context("Connecting to chromeos_acpi device"),
        None => Err(anyhow!(
            "Watching for files finished before any chromeos ACPI device could be found"
        )),
    }
}

#[fuchsia::main(logging = true)]
async fn main() {
    if let Err(e) = real_main().await {
        error!("Error: {:?}", e);
    }
}

async fn real_main() -> Result<(), anyhow::Error> {
    let mut service_fs = ServiceFs::new();

    inspect_runtime::serve(component::inspector(), &mut service_fs)?;
    component::health().set_starting_up();

    let _ = service_fs.dir("svc").add_fidl_service(IncomingRequest::FirmwareParam);
    let _ = service_fs.take_and_serve_directory_handle().context("Serving outgoing namespace")?;

    // Make sure we support the version of nvdata in use on this system.
    let acpi = find_chromeos_acpi_device().await.context("Finding chromeos-acpi device")?;
    let nvdata_version = acpi
        .get_nvdata_version()
        .await?
        .map_err(zx::Status::from_raw)
        .context("Getting nvram version")?;

    match NvdataVersion::from_raw(nvdata_version as usize) {
        Ok(_) => {}
        Err(version) => {
            error!("Unsupported nvdata version {}, wanted 1 or 2.", version);
            component::health().set_unhealthy("Unsupported nvdata version");
            return Ok(());
        }
    };

    // Determine where the nvdata is stored within nvram.
    let (base, size) = acpi
        .get_nvram_metadata_location()
        .await?
        .map_err(zx::Status::from_raw)
        .context("Getting nvram location")?;
    info!(%size, "Using vboot nvram range: {}", base);
    // Connect to the flash device which contains a backup of the nvdata.
    let flashmap = find_flashmap_device().await.context("Finding flashmap device")?;
    let flash = nvdata::flash::Flash::new_with_proxy(flashmap)
        .await
        .context("Connecting to flash device")?;

    // Connect to the nvram device and parse nvdata from it.
    let nvram = find_nvram_device().await.context("While finding nvram device")?;
    let nvdata =
        Nvdata::new(base, size, nvram, flash, component::inspector().root().create_child("nvdata"))
            .await
            .context("Loading nvdata")?;
    component::health().set_ok();

    let nvdata_ref = &nvdata;
    service_fs
        .for_each_concurrent(None, |request: IncomingRequest| async move {
            match request {
                IncomingRequest::FirmwareParam(stream) => {
                    nvdata_ref.serve(stream).await.unwrap_or_else(|err| {
                        error!(?err, "Failed while serving stream");
                    })
                }
            }
        })
        .await;
    Ok(())
}
