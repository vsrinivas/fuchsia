// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common_utils::common::macros::{fx_err_and_bail, with_line};
use crate::temperature::types::TemperatureRequest;
use anyhow::Error;
use fidl_fuchsia_hardware_temperature::{DeviceMarker, DeviceProxy};
use fuchsia_syslog::macros::fx_log_err;
use serde_json::Value;
use std::path::Path;

/// Perform Temperature operations.
///
/// Note this object is shared among all threads created by server.
///
#[derive(Debug)]
pub struct TemperatureFacade {
    /// Temperature device proxy that may be optionally provided for testing. The proxy is not
    /// cached during normal operation.
    proxy: Option<DeviceProxy>,
}

impl TemperatureFacade {
    pub fn new() -> TemperatureFacade {
        TemperatureFacade { proxy: None }
    }

    /// Connect to the temperature device specified by `device_path`.
    ///
    /// # Arguments
    /// * `device_path` - String representing the temperature device path (e.g.,
    ///   /dev/class/temperature/000)
    fn get_proxy(&self, device_path: String) -> Result<DeviceProxy, Error> {
        let tag = "TemperatureFacade::get_proxy";

        if let Some(proxy) = &self.proxy {
            Ok(proxy.clone())
        } else {
            let (proxy, server) = match fidl::endpoints::create_proxy::<DeviceMarker>() {
                Ok(r) => r,
                Err(e) => fx_err_and_bail!(
                    &with_line!(tag),
                    format_err!("Failed to create proxy {:?}", e)
                ),
            };

            if Path::new(&device_path).exists() {
                fdio::service_connect(device_path.as_ref(), server.into_channel())?;
                Ok(proxy)
            } else {
                fx_err_and_bail!(
                    &with_line!(tag),
                    format_err!("Failed to find device: {}", device_path)
                );
            }
        }
    }

    /// Reads the temperature from a specified temperature device.
    ///
    /// # Arguments
    /// * `args`: JSON value containing the TemperatureRequest information, where TemperatureRequest
    ///   contains the device path to read from.
    pub async fn get_temperature_celsius(&self, args: Value) -> Result<f32, Error> {
        let req: TemperatureRequest = serde_json::from_value(args)?;
        let (status, temperature) =
            self.get_proxy(req.device_path)?.get_temperature_celsius().await?;
        fuchsia_zircon::Status::ok(status)?;
        Ok(temperature)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_hardware_temperature::DeviceRequest;
    use fuchsia_async as fasync;
    use futures::prelude::*;
    use serde_json::json;

    /// Tests that the `get_temperature_celsius` method correctly queries the temperature device and
    /// returns the expected temperature value.
    #[fasync::run_singlethreaded(test)]
    async fn test_get_temperature_celsius() {
        let (proxy, mut stream) = create_proxy_and_stream::<DeviceMarker>().unwrap();
        let facade = TemperatureFacade { proxy: Some(proxy) };
        let facade_fut = async move {
            assert_eq!(
                facade
                    .get_temperature_celsius(json!({
                        "device_path": "/dev/class/temperature/000"
                    }))
                    .await
                    .unwrap(),
                12.34
            );
        };
        let stream_fut = async move {
            match stream.try_next().await {
                Ok(Some(DeviceRequest::GetTemperatureCelsius { responder })) => {
                    responder.send(0, 12.34).unwrap();
                }
                err => panic!("Err in request handler: {:?}", err),
            }
        };
        future::join(facade_fut, stream_fut).await;
    }
}
