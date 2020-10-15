// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::adc::types::AdcRequest;
use crate::common_utils::common::macros::{fx_err_and_bail, with_line};
use anyhow::Error;
use fidl_fuchsia_hardware_adc::{DeviceMarker, DeviceProxy};
use fuchsia_syslog::macros::fx_log_err;
use fuchsia_zircon as zx;
use serde_json::Value;
use std::path::Path;

/// Perform Adc operations.
///
/// Note this object is shared among all threads created by server.
///
#[derive(Debug)]
pub struct AdcFacade {
    /// Adc device proxy that may be optionally provided for testing. The proxy is not cached during
    /// normal operation.
    proxy: Option<DeviceProxy>,
}

impl AdcFacade {
    pub fn new() -> AdcFacade {
        AdcFacade { proxy: None }
    }

    /// Connect to the ADC device specified by `device_path`.
    ///
    /// # Arguments
    /// * `device_path` - String representing the ADC device path (e.g., /dev/class/adc/000)
    fn get_proxy(&self, device_path: String) -> Result<DeviceProxy, Error> {
        let tag = "AdcFacade::get_proxy";

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

    /// Gets the resolution from a specified ADC device.
    ///
    /// # Arguments
    /// * `args`: JSON value containing the AdcRequest information, where AdcRequest contains the
    ///   device path to read from.
    pub async fn get_resolution(&self, args: Value) -> Result<u8, Error> {
        let req: AdcRequest = serde_json::from_value(args)?;
        let resolution = self
            .get_proxy(req.device_path)?
            .get_resolution()
            .await?
            .map_err(|e| zx::Status::from_raw(e))?;
        Ok(resolution)
    }

    /// Gets a sample from a specified ADC device.
    ///
    /// # Arguments
    /// * `args`: JSON value containing the AdcRequest information, where AdcRequest contains the
    ///   device path to read from.
    pub async fn get_sample(&self, args: Value) -> Result<u32, Error> {
        let req: AdcRequest = serde_json::from_value(args)?;
        let sample = self
            .get_proxy(req.device_path)?
            .get_sample()
            .await?
            .map_err(|e| zx::Status::from_raw(e))?;
        Ok(sample)
    }

    /// Gets a normalized sample from a specified ADC device.
    ///
    /// # Arguments
    /// * `args`: JSON value containing the AdcRequest information, where AdcRequest contains the
    ///   device path to read from.
    pub async fn get_normalized_sample(&self, args: Value) -> Result<f32, Error> {
        let req: AdcRequest = serde_json::from_value(args)?;
        let sample = self
            .get_proxy(req.device_path)?
            .get_normalized_sample()
            .await?
            .map_err(|e| zx::Status::from_raw(e))?;
        Ok(sample)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_hardware_adc::DeviceRequest;
    use fuchsia_async as fasync;
    use futures::prelude::*;
    use serde_json::json;

    /// Tests that the `get_resolution` method correctly queries the ADC device and returns the
    /// expected resolution value.
    #[fasync::run_singlethreaded(test)]
    async fn test_get_resolution() {
        let (proxy, mut stream) = create_proxy_and_stream::<DeviceMarker>().unwrap();
        let facade = AdcFacade { proxy: Some(proxy) };
        let facade_fut = async move {
            assert_eq!(
                facade
                    .get_resolution(json!({
                        "device_path": "/dev/class/adc/000"
                    }))
                    .await
                    .unwrap(),
                12
            );
        };
        let stream_fut = async move {
            match stream.try_next().await {
                Ok(Some(DeviceRequest::GetResolution { responder })) => {
                    responder.send(&mut Ok(12)).unwrap();
                }
                err => panic!("Err in request handler: {:?}", err),
            }
        };
        future::join(facade_fut, stream_fut).await;
    }

    /// Tests that the `get_sample` method correctly queries the ADC device and returns the expected
    /// sample value.
    #[fasync::run_singlethreaded(test)]
    async fn test_get_sample() {
        let (proxy, mut stream) = create_proxy_and_stream::<DeviceMarker>().unwrap();
        let facade = AdcFacade { proxy: Some(proxy) };
        let facade_fut = async move {
            assert_eq!(
                facade
                    .get_sample(json!({
                        "device_path": "/dev/class/adc/000"
                    }))
                    .await
                    .unwrap(),
                12
            );
        };
        let stream_fut = async move {
            match stream.try_next().await {
                Ok(Some(DeviceRequest::GetSample { responder })) => {
                    responder.send(&mut Ok(12)).unwrap();
                }
                err => panic!("Err in request handler: {:?}", err),
            }
        };
        future::join(facade_fut, stream_fut).await;
    }

    /// Tests that the `get_normalized_sample` method correctly queries the ADC device and returns
    /// the expected normalized sample value.
    #[fasync::run_singlethreaded(test)]
    async fn test_get_normalized_sample() {
        let (proxy, mut stream) = create_proxy_and_stream::<DeviceMarker>().unwrap();
        let facade = AdcFacade { proxy: Some(proxy) };
        let facade_fut = async move {
            assert_eq!(
                facade
                    .get_normalized_sample(json!({
                        "device_path": "/dev/class/adc/000"
                    }))
                    .await
                    .unwrap(),
                12.34
            );
        };
        let stream_fut = async move {
            match stream.try_next().await {
                Ok(Some(DeviceRequest::GetNormalizedSample { responder })) => {
                    responder.send(&mut Ok(12.34)).unwrap();
                }
                err => panic!("Err in request handler: {:?}", err),
            }
        };
        future::join(facade_fut, stream_fut).await;
    }
}
