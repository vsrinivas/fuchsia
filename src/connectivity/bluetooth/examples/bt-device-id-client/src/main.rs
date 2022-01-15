// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use fidl_fuchsia_bluetooth_deviceid as di;
use fuchsia_async as fasync;
use futures::Future;
use tracing::info;

fn example_di_record() -> di::DeviceIdentificationRecord {
    di::DeviceIdentificationRecord {
        vendor_id: Some(di::VendorId::BluetoothSigId(0x00E0)), // Google
        product_id: Some(5),
        version: Some(di::DeviceReleaseNumber {
            major: Some(1),
            minor: Some(0),
            subminor: Some(10),
            ..di::DeviceReleaseNumber::EMPTY
        }),
        service_description: Some("Example DI record".to_string()),
        ..di::DeviceIdentificationRecord::EMPTY
    }
}

/// Sets the device identification of the Fuchsia device.
/// Returns a future representing the request and the client of the DI channel - this should be kept
/// alive.
fn set_device_identification(
    di_svc: di::DeviceIdentificationProxy,
) -> Result<(impl Future<Output = ()>, di::DeviceIdentificationHandleProxy), Error> {
    let records = vec![example_di_record()];
    let (client, server) = fidl::endpoints::create_proxy::<di::DeviceIdentificationHandleMarker>()?;

    let request_fut = di_svc.set_device_identification(&mut records.into_iter(), server).check()?;

    let fut = async move {
        let result = request_fut.await;
        info!("Set DI request finished with status: {:?}", result);
    };
    Ok((fut, client))
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["bt-device-id-client"]).expect("Unable to initialize logger");

    let di_svc = fuchsia_component::client::connect_to_protocol::<di::DeviceIdentificationMarker>()
        .context("Couldn't connect to DI capability")?;

    // Register the DI advertisement. Keep the returned `handle` alive to maintain the request.
    let (fut, _handle) = set_device_identification(di_svc)?;
    let () = fut.await;
    info!("Example DI client exiting...");
    Ok(())
}
