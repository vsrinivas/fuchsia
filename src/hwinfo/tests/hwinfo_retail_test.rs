// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    anyhow::Error, fidl_fuchsia_hwinfo::DeviceMarker, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
};

#[fasync::run_singlethreaded(test)]
async fn request_device_info() -> Result<(), Error> {
    let device_info_provider =
        connect_to_service::<DeviceMarker>().expect("Failed to connect to device info service");
    let response = device_info_provider.get_info().await?;
    assert_eq!(response.serial_number.unwrap().to_string(), "dummy_serial_number".to_string());
    assert_eq!(response.is_retail_demo.unwrap(), true);
    assert_eq!(response.retail_sku.unwrap(), "XX12345-YY");
    Ok(())
}
