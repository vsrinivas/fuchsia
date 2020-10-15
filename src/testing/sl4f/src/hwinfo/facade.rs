// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    common_utils::common::macros::{fx_err_and_bail, with_line},
    hwinfo::types::{SerializableBoardInfo, SerializableDeviceInfo, SerializableProductInfo},
};
use anyhow::Error;
use fidl_fuchsia_hwinfo::{BoardMarker, DeviceMarker, ProductMarker};
use fuchsia_component as app;
use fuchsia_syslog::macros::{fx_log_err, fx_log_info};

/// Perform HwInfo fidl operations.
///
/// Note this object is shared among all threads created by server.
///
#[derive(Debug)]
pub struct HwinfoFacade {}

impl HwinfoFacade {
    pub fn new() -> HwinfoFacade {
        HwinfoFacade {}
    }

    /// Returns the device info of the hwinfo proxy service. Currently
    /// only returns the serial number.
    pub async fn get_device_info(&self) -> Result<SerializableDeviceInfo, Error> {
        let tag = "HwinfoFacade::get_info";

        let device_info_proxy = app::client::connect_to_service::<DeviceMarker>();

        match device_info_proxy {
            Ok(p) => {
                let device_info = p.get_info().await?;
                let device_info_string = format!("Device info found: {:?}", device_info);
                fx_log_info!(tag: &with_line!(tag), "{}", device_info_string);
                Ok(SerializableDeviceInfo::new(&device_info))
            }
            Err(err) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("Failed to create hwinfo proxy: {}", err)
            ),
        }
    }

    /// Returns the device info of the product info proxy service.
    pub async fn get_product_info(&self) -> Result<SerializableProductInfo, Error> {
        let tag = "HwinfoFacade::get_product_info";

        let product_info_proxy = app::client::connect_to_service::<ProductMarker>();

        match product_info_proxy {
            Ok(p) => {
                let product_info = p.get_info().await?;
                let product_info_string = format!("Product info found: {:?}", product_info);
                fx_log_info!(tag: &with_line!(tag), "{}", product_info_string);
                Ok(SerializableProductInfo::new(&product_info))
            }
            Err(err) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("Failed to create hwinfo proxy: {}", err)
            ),
        }
    }

    /// Returns the board info of the hwinfo proxy service.
    pub async fn get_board_info(&self) -> Result<SerializableBoardInfo, Error> {
        let tag = "HwinfoFacade::get_board_info";
        let board_info_proxy = app::client::connect_to_service::<BoardMarker>();
        match board_info_proxy {
            Ok(p) => {
                let board_info = p.get_info().await?;
                let board_info_string = format!("Board info found: {:?}", board_info);
                fx_log_info!(tag: &with_line!(tag), "{}", board_info_string);
                Ok(SerializableBoardInfo::new(&board_info))
            }
            Err(err) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("Failed to create hwinfo proxy: {}", err)
            ),
        }
    }
}
