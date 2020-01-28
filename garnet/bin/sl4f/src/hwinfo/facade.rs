// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    common_utils::common::macros::{fx_err_and_bail, with_line},
    hwinfo::types::SerializableDeviceInfo,
};
use anyhow::Error;
use fidl_fuchsia_hwinfo::DeviceMarker;
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

        let hwinfo_proxy = app::client::connect_to_service::<DeviceMarker>();

        match hwinfo_proxy {
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
}
