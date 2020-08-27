// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    anyhow::Error,
    fidl_fuchsia_hwinfo::{BoardMarker, DeviceMarker, ProductMarker},
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
};

#[fasync::run_singlethreaded(test)]
async fn request_device_info() -> Result<(), Error> {
    let device_info_provider =
        connect_to_service::<DeviceMarker>().expect("Failed to connect to device info service");
    let response = device_info_provider.get_info().await?;
    assert!(response.serial_number.is_none());
    assert_eq!(response.is_retail_demo.unwrap(), false);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn request_board_info() -> Result<(), Error> {
    let board_info_provider =
        connect_to_service::<BoardMarker>().expect("Failed to connect to device info service");
    let response = board_info_provider.get_info().await?;
    assert!(response.name.is_none());
    assert!(response.revision.is_none());
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn request_product_info() -> Result<(), Error> {
    let product_info_provider =
        connect_to_service::<ProductMarker>().expect("Failed to connect to device info service");
    let response = product_info_provider.get_info().await?;
    assert!(response.sku.is_none());
    assert!(response.language.is_none());
    assert!(response.locale_list.is_none());
    assert!(response.regulatory_domain.is_none());
    assert!(response.name.is_none());
    assert!(response.model.is_none());
    assert!(response.manufacturer.is_none());
    assert!(response.build_date.is_none());
    Ok(())
}
