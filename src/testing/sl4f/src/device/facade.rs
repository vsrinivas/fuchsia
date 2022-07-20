// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common_utils::common::macros::{fx_err_and_bail, with_line};
use anyhow::Error;
use fidl_fuchsia_buildinfo::ProviderMarker;
use fidl_fuchsia_device::{NameProviderMarker, DEFAULT_DEVICE_NAME};
use fuchsia_component::client;

/// Perform Fuchsia Device fidl operations.
///
/// Note this object is shared among all threads created by server.
///
#[derive(Debug)]
pub struct DeviceFacade {}

impl DeviceFacade {
    pub fn new() -> DeviceFacade {
        DeviceFacade {}
    }

    /// Returns target's nodename by NameProviderProxy
    pub async fn get_device_name(&self) -> Result<String, Error> {
        let tag = "DeviceFacade::get_device_name";
        let proxy = match client::connect_to_protocol::<NameProviderMarker>() {
            Ok(p) => p,
            Err(err) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("Failed to connect to NameProvider proxy: {:?}", err)
            ),
        };
        let name = proxy
            .get_device_name()
            .await?
            .map_err(|e| format_err!("failed to obtain device name: {:?}", e));
        let device_name = name.unwrap_or(DEFAULT_DEVICE_NAME.to_string());
        Ok(device_name)
    }

    /// Returns target's product name
    pub async fn get_product(&self) -> Result<String, Error> {
        let tag = "DeviceFacade::get_product";
        let proxy = match client::connect_to_protocol::<ProviderMarker>() {
            Ok(p) => p,
            Err(err) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("Failed to connect to fuchsia.buildinfo.Provider proxy: {:?}", err)
            ),
        };
        let buildinfo = proxy.get_build_info().await?;
        let product = buildinfo.product_config.unwrap_or("unknown".to_string());
        Ok(product)
    }

    /// Returns target's version name
    pub async fn get_version(&self) -> Result<String, Error> {
        let tag = "DeviceFacade::get_version";
        let proxy = match client::connect_to_protocol::<ProviderMarker>() {
            Ok(p) => p,
            Err(err) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("Failed to connect to fuchsia.buildinfo.Provider proxy: {:?}", err)
            ),
        };
        let buildinfo = proxy.get_build_info().await?;
        let device_name = buildinfo.version.unwrap_or("unknown".to_string());
        Ok(device_name)
    }
}
