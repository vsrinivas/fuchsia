// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common_utils::common::macros::{fx_err_and_bail, with_line};
use anyhow::Error;
use fidl_fuchsia_device::{ControllerMarker, NameProviderMarker, DEFAULT_DEVICE_NAME};
use fuchsia_component::client;
use fuchsia_syslog::macros::*;
use serde_json::Value;

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
        let proxy = match client::connect_to_service::<NameProviderMarker>() {
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

    pub async fn rebind(&self, args: Value) -> Result<(), Error> {
        let tag = "DeviceFacade::rebind";

        let device = args
            .get("device")
            .ok_or(format_err!("No device argument specified"))?
            .as_str()
            .ok_or(format_err!("device argument must be a string"))?;

        let driver = args
            .get("driver")
            .ok_or(format_err!("No driver argument specified"))?
            .as_str()
            .ok_or(format_err!("driver argument must be a string"))?;

        let (proxy, server) = match fidl::endpoints::create_proxy::<ControllerMarker>() {
            Ok(r) => r,
            Err(e) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("Failed to get device proxy: {:?}", e)
            ),
        };

        match fdio::service_connect(device, server.into_channel()) {
            Ok(r) => r,
            Err(e) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("Failed to connect to device {}: {:?}", device, e)
            ),
        }

        proxy.rebind(driver).await?.map_err(|e| format_err!("Rebind failed: {:?}", e))
    }
}
