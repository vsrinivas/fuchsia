// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_camera_test_virtualcamera::{
    StreamConfig, VirtualCameraDeviceMarker, VirtualCameraDeviceProxy,
};
use fuchsia_component::client::connect_to_protocol;
use fuchsia_syslog::macros::*;
use serde_json::{to_value, Value};
use std::convert::TryInto;

/// Facade providing access to Virtual Camera interfaces.
#[derive(Debug)]
pub struct VirtualCameraFacade {
    /// Virtual camera device proxy that may be optionally provided for testing.
    /// The proxy is not cached during normal operation.
    camera_proxy: Option<VirtualCameraDeviceProxy>,
}

impl VirtualCameraFacade {
    pub fn new() -> VirtualCameraFacade {
        VirtualCameraFacade { camera_proxy: None }
    }

    /// Adds a stream config to the virtual device.
    ///
    /// # Arguments
    /// * `args`: JSON value containing the desired index of the stream as
    ///   uint64, pixel width as uint32, and pixel height as uint32.
    pub async fn add_stream_config(&self, args: Value) -> Result<Value, Error> {
        // Pull the args and cast them if need be.
        let config_index =
            args["index"].as_u64().ok_or(format_err!("index not a number"))?.try_into()?;

        fx_log_info!("AddStreamConfig: index received {:?}", config_index);

        let config_width = (args
            .get("width")
            .ok_or(format_err!("Expected a serde_json Value width."))?
            .as_u64()
            .ok_or(format_err!("Expected u64 type for width."))?
            as u32)
            .try_into()?;

        fx_log_info!("AddStreamConfig: width received {:?}", config_width);

        let config_height = (args
            .get("height")
            .ok_or(format_err!("Expected a serde_json Value height."))?
            .as_u64()
            .ok_or(format_err!("Expected u64 type for height."))?
            as u32)
            .try_into()?;

        fx_log_info!("AddStreamConfig: height received {:?}", config_height);

        // Use the test proxy if one was provided, otherwise connect to the
        // discoverable Virtual Camera service.
        let camera_proxy = match self.camera_proxy.as_ref() {
            Some(proxy) => proxy.clone(),
            None => match connect_to_protocol::<VirtualCameraDeviceMarker>() {
                Ok(proxy) => proxy,
                Err(e) => bail!("Failed to connect to VirtualCameraDevice FIDL service {:?}.", e),
            },
        };

        // Set up StreamConfig struct.
        let stream_config =
            { StreamConfig { width: config_width, height: config_height, ..StreamConfig::EMPTY } };

        // Call the FIDL method.
        fx_log_info!("Stream Config specifications {:?}", stream_config);
        match camera_proxy.add_stream_config(config_index, stream_config) {
            Ok(_) => Ok(to_value(true)?),
            Err(e) => Err(format_err!("AddStreamConfig failed with err {:?}", e)),
        }
    }

    pub async fn add_to_device_watcher(&self) -> Result<Value, Error> {
        // Use the test proxy if one was provided, otherwise connect to the discoverable
        // Virtual Camera service.
        let camera_proxy = match self.camera_proxy.as_ref() {
            Some(proxy) => proxy.clone(),
            None => match connect_to_protocol::<VirtualCameraDeviceMarker>() {
                Ok(proxy) => proxy,
                Err(e) => bail!("Failed to connect to VirtualCameraDevice FIDL service {:?}.", e),
            },
        };

        fx_log_info!("AddToDeviceWatcher FIDL protocol connected");

        match camera_proxy.add_to_device_watcher().await? {
            Ok(_) => Ok(to_value(true)?),
            Err(e) => Err(format_err!("AddToDeviceWatcher failed with err {:?}", e)),
        }
    }

    // TODO(b/195762320) Add remaining method parsing for AddDataSource,
    // SetDataSourceForStreamConfig, ClearDataSourceForStreamConfig
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::{create_proxy, create_proxy_and_stream};
    use fidl_fuchsia_camera_test_virtualcamera::VirtualCameraDeviceRequest;
    use fuchsia_async as fasync;
    use futures::prelude::*;
    use futures::TryStreamExt;
    use serde_json::json;

    /// Tests that the `add_stream_config` method correctly sends the index, width and
    /// height to the FIDL and returns true.
    #[fasync::run_singlethreaded(test)]
    async fn test_add_stream_config() {
        let test_index = 0;
        let test_width = 100;
        let test_height = 200;

        let (proxy, mut stream) = create_proxy_and_stream::<VirtualCameraDeviceMarker>().unwrap();

        // Create a facade future that sends a request to `proxy`.
        let facade = VirtualCameraFacade { camera_proxy: Some(proxy) };

        // Set parameters and expect a return value of true from `add_stream_config`.
        let facade_fut = async move {
            assert_eq!(
                facade
                    .add_stream_config(
                        json!({"index": test_index, "width": test_width, "height": test_height})
                    )
                    .await
                    .unwrap(),
                to_value(true).unwrap()
            );
        };

        // Verify stream contents from `AddStreamConfig` match arguments passed into facade.
        let stream_fut = async move {
            match stream.try_next().await {
                Ok(Some(VirtualCameraDeviceRequest::AddStreamConfig { index, config, .. })) => {
                    assert_eq!(index, test_index);
                    assert_eq!(
                        config,
                        StreamConfig {
                            width: Some(test_width),
                            height: Some(test_height),
                            ..StreamConfig::EMPTY
                        }
                    );
                }
                err => panic!("Err in request handler: {:?}", err),
            }
        };
        future::join(facade_fut, stream_fut).await;
    }

    /// Tests that the `add_stream_config` method does not send the index, width and
    /// height to the FIDL because format is incorrect.
    #[fasync::run_singlethreaded(test)]
    async fn test_add_stream_config_with_parameter_error() {
        // Incorrectly set AddStreamConfig parameters to strings.
        let test_index = "one";
        let test_width = "three hundred";
        let test_height = "four hundred";

        let proxy = create_proxy::<VirtualCameraDeviceMarker>().unwrap();

        // Create a facade future that sends a request to `proxy`.
        let facade = VirtualCameraFacade { camera_proxy: Some(proxy.0) };

        // Set parameters and expect a return value of false from `add_stream_config`.
        assert_eq!(
            facade
                .add_stream_config(
                    json!({"index": test_index, "width": test_width, "height": test_height})
                )
                .await
                .is_ok(),
            false
        );
    }

    /// Tests that the `add_to_device_watcher` method correctly returns true
    /// after calling the FIDL service.
    #[fasync::run_singlethreaded(test)]
    async fn test_add_to_device_watcher() {
        let (proxy, mut stream) = create_proxy_and_stream::<VirtualCameraDeviceMarker>().unwrap();

        // Create a facade future that sends a request to `proxy`.
        let facade = VirtualCameraFacade { camera_proxy: Some(proxy) };

        // Set Parameters and expect a return value of true from `add_to_device_watcher`.
        let facade_fut = async move {
            assert_eq!(facade.add_to_device_watcher().await.unwrap(), to_value(true).unwrap());
        };

        // Verify stream contents from `AddToDeviceWatcher` match arguments passed into facade.
        let stream_fut = async move {
            match stream.try_next().await {
                Ok(Some(VirtualCameraDeviceRequest::AddToDeviceWatcher { responder })) => {
                    responder.send(&mut Ok(())).unwrap();
                }
                err => panic!("Err in request handler: {:?}", err),
            }
        };
        future::join(facade_fut, stream_fut).await;
    }

    /// Tests that the `add_to_device_watcher` method correctly returns false
    /// after calling the FIDL service with an error response.
    #[fasync::run_singlethreaded(test)]
    async fn test_add_to_device_watcher_on_error() {
        let (proxy, mut stream) = create_proxy_and_stream::<VirtualCameraDeviceMarker>().unwrap();

        // Create a facade future that sends a request to `proxy`.
        let facade = VirtualCameraFacade { camera_proxy: Some(proxy) };

        // Set parameters and expect a return value of true from `add_to_device_watcher`.
        let facade_fut = async move {
            assert_eq!(facade.add_to_device_watcher().await.is_ok(), false);
        };

        // Verify stream contents from `AddToDeviceWatcher` match arguments passed into facade.
        let stream_fut = async move {
            match stream.try_next().await {
                Ok(Some(VirtualCameraDeviceRequest::AddToDeviceWatcher { responder })) => {
                    responder.send(
                      &mut Err(fidl_fuchsia_camera_test_virtualcamera::
                        Error::AlreadyAddedToDeviceWatcher
                      )).unwrap();
                }
                err => panic!("Err in request handler: {:?}", err),
            }
        };
        future::join(facade_fut, stream_fut).await;
    }
}
