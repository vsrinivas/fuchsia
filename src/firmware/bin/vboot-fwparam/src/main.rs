// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod error;
mod nvdata;

use crate::nvdata::{Nvdata, NvdataVersion};
use anyhow::{anyhow, Context};
use fidl_fuchsia_acpi_chromeos as chrome_acpi;
use fidl_fuchsia_hardware_nvram as fnvram;
use fidl_fuchsia_vboot_fwparam::FirmwareParamRequestStream;
use fuchsia_component::{client::connect_to_protocol_at_path, server::ServiceFs};
use fuchsia_inspect::{component, health::Reporter};
use fuchsia_syslog::{fx_log_err, fx_log_info};
use fuchsia_zircon as zx;
use futures::StreamExt;
use io_util::{OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE};

enum IncomingRequest {
    FirmwareParam(FirmwareParamRequestStream),
}

/// Find and connect to the nvram device containing the vboot nvdata.
async fn find_nvram_device() -> Result<fnvram::DeviceProxy, anyhow::Error> {
    // The RTC houses the nvram where nvdata is stored.
    let nvram_path = "/dev/class/rtc";
    let proxy =
        io_util::open_directory_in_namespace(nvram_path, OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE)
            .context("Opening /dev/class/rtc")?;
    let contents = files_async::readdir(&proxy).await.context("Reading /dev/class/rtc")?;
    if contents.len() > 1 {
        return Err(anyhow!("Too many rtc devices"));
    }

    Ok(connect_to_protocol_at_path::<fnvram::DeviceMarker>(
        &(nvram_path.to_owned() + "/" + &contents[0].name),
    )
    .context("Connecting to nvram device")?)
}

/// Find and connect to the ChromeOS ACPI device.
fn find_chromeos_acpi_device() -> Result<chrome_acpi::DeviceProxy, anyhow::Error> {
    let path = "/dev/sys/platform/acpi/acpi-CRHW/chromeos_acpi";
    Ok(connect_to_protocol_at_path::<chrome_acpi::DeviceMarker>(path)
        .context("Connecting to chromeos_acpi device")?)
}

#[fuchsia::component(logging = true)]
async fn main() {
    if let Err(e) = real_main().await {
        fx_log_err!("Error: {:?}", e);
    }
}

async fn real_main() -> Result<(), anyhow::Error> {
    let mut service_fs = ServiceFs::new();

    inspect_runtime::serve(component::inspector(), &mut service_fs)?;
    component::health().set_starting_up();

    let _ = service_fs.dir("svc").add_fidl_service(IncomingRequest::FirmwareParam);
    let _ = service_fs.take_and_serve_directory_handle().context("Serving outgoing namespace")?;

    // Make sure we support the version of nvdata in use on this system.
    let acpi = find_chromeos_acpi_device().context("Finding chromeos-acpi device")?;
    let nvdata_version = acpi
        .get_nvdata_version()
        .await?
        .map_err(zx::Status::from_raw)
        .context("Getting nvram version")?;

    match NvdataVersion::from_raw(nvdata_version as usize) {
        Ok(_) => {}
        Err(version) => {
            fx_log_err!("Unsupported nvdata version {}, wanted 1 or 2.", version);
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
    fx_log_info!("Using vboot nvram range: {}, size={}", base, size);

    // Connect to the nvram device and parse nvdata from it.
    let nvram = find_nvram_device().await.context("While finding nvram device")?;
    let nvdata =
        Nvdata::new(base, size, nvram, component::inspector().root().create_child("nvdata"))
            .await
            .context("Loading nvdata")?;
    component::health().set_ok();

    let nvdata_ref = &nvdata;
    service_fs
        .for_each_concurrent(None, |request: IncomingRequest| async move {
            match request {
                IncomingRequest::FirmwareParam(stream) => {
                    nvdata_ref.serve(stream).await.unwrap_or_else(|e| {
                        fx_log_err!("Failed while serving stream: {:?}", e);
                    })
                }
            }
        })
        .await;
    Ok(())
}
