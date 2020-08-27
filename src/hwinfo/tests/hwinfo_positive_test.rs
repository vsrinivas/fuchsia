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
    assert_eq!(response.serial_number.unwrap().to_string(), "dummy_serial_number".to_string());
    assert_eq!(response.is_retail_demo.unwrap(), false);
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn request_board_info() -> Result<(), Error> {
    let board_info_provider =
        connect_to_service::<BoardMarker>().expect("Failed to connect to device info service");
    let response = board_info_provider.get_info().await?;
    assert_eq!(response.name.unwrap().to_string(), "dummy_board_name".to_string());
    assert_eq!(response.revision.unwrap().to_string(), "dummy_board_revision".to_string());
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn request_product_info() -> Result<(), Error> {
    let product_info_provider =
        connect_to_service::<ProductMarker>().expect("Failed to connect to device info service");
    let mut response = product_info_provider.get_info().await?;
    assert_eq!(response.sku.unwrap().to_string(), "config_value".to_string());
    assert_eq!(response.name.unwrap().to_string(), "test_product_name".to_string());
    assert_eq!(response.language.unwrap().to_string(), "en".to_string());
    assert_eq!(response.model.unwrap().to_string(), "test_product_model".to_string());
    assert_eq!(response.manufacturer.unwrap().to_string(), "test_manufacturer".to_string());
    assert_eq!(response.build_date.unwrap().to_string(), "2019-10-24T04:23:49".to_string());
    assert_eq!(
        response.regulatory_domain.unwrap().country_code.unwrap().to_string(),
        "US".to_string()
    );
    assert_eq!(response.locale_list.as_mut().unwrap()[0].id.to_string(), "en-US".to_string());
    assert_eq!(response.locale_list.as_mut().unwrap()[1].id.to_string(), "en-UK".to_string());
    Ok(())
}
