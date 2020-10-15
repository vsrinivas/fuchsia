// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_camera2_hal::{ControllerMarker, ControllerProxy};
use fidl_fuchsia_hardware_camera::DeviceProxy;
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_err;
use fuchsia_zircon as zx;
use parking_lot::{RwLock, RwLockUpgradableReadGuard};
use serde_json::{to_value, Value};

use crate::camera::types::DetectResult;
use crate::common_utils::common::macros::{fx_err_and_bail, with_line};

// TODO(fxbug.dev/41671): Don't hardcode values; use glob for path. Especially for multiple cameras.
const CAMERA_ID: i32 = 0;
const CAMERA_PATH: &str = "/dev/class/camera/000";

/// Facade that exposes various Camera interfaces.
#[derive(Debug)]
pub struct CameraFacade {
    hal_svc: RwLock<Option<ControllerProxy>>,
}

impl CameraFacade {
    /// Instantiate the facade.
    pub fn new() -> Self {
        CameraFacade { hal_svc: RwLock::new(None) }
    }

    /// Test-only method to manually inject pre-defined services into the facade.
    #[cfg(test)]
    fn new_with_svc(hal_proxy: Option<ControllerProxy>) -> Self {
        Self { hal_svc: RwLock::new(hal_proxy) }
    }

    /// Helper method to connect to the Camera HAL service.
    fn get_hal_svc(&self, tag: &str) -> Result<ControllerProxy, Error> {
        let lock = self.hal_svc.upgradable_read();
        match lock.as_ref() {
            Some(svc) => Ok(svc.clone()),
            None => {
                let (client, server) = zx::Channel::create()?;
                fdio::service_connect(CAMERA_PATH, server)?;

                let (controller_client, controller_server) =
                    fidl::endpoints::create_proxy::<ControllerMarker>()?;

                match DeviceProxy::new(fasync::Channel::from_channel(client)?)
                    .get_channel2(controller_server.into_channel())
                {
                    Ok(_) => {
                        *RwLockUpgradableReadGuard::upgrade(lock) = Some(controller_client.clone());
                        Ok(controller_client)
                    }
                    Err(e) => fx_err_and_bail!(
                        &with_line!(tag),
                        format_err!("Couldn't call GetChannel2(); {}.", e)
                    ),
                }
            }
        }
    }

    /// Checks whether the device under test has a camera and provides information about
    /// it. If the device has multiple cameras, the first one listed is chosen.
    ///
    /// Takes no arguments. Outputs the serialized DeviceInfo in a DetectResult struct.
    pub async fn detect(&self) -> Result<DetectResult, Error> {
        const TAG: &str = "CameraFacade::detect";
        let svc = self.get_hal_svc(TAG)?;
        match svc.get_device_info().await {
            Ok(r) => Ok(DetectResult { camera_id: CAMERA_ID, camera_info: r }),
            Err(e) => {
                fx_err_and_bail!(&with_line!(TAG), format_err!("Couldn't get DeviceInfo; {}.", e))
            }
        }
    }

    /// Retrieves the sensor's serial number.
    ///
    /// Takes no arguments. Outputs an empty response.
    pub async fn get_sn(&self) -> Result<Value, Error> {
        const TAG: &str = "CameraFacade::get_sn";
        fx_log_err!("{} {}", TAG, "NOT IMEPLEMENTED");
        Ok(to_value(())?)
    }

    /// Stops the camera sensor (and associated MIPI) stream.
    ///
    /// Takes no arguments. Outputs an empty response.
    pub async fn get_cfg(&self) -> Result<Value, Error> {
        const TAG: &str = "CameraFacade::get_cfg";
        fx_log_err!("{} {}", TAG, "NOT IMEPLEMENTED");
        Ok(to_value(())?)
    }

    /// Sets config parameters for the sensor.
    ///
    /// # Arguments
    /// * `mode`: One of the camera's predefined sensor modes (fpms, resolution,
    ///           etc).
    /// * `integration_time`: The camera's sensor integration time parameter.
    /// * `analog_gain`: The camera's sensor analog gain parameter.
    /// * `digital_gain`: The camera's sensor digital gain parameter.
    ///
    /// Outputs an empty response.
    pub async fn set_cfg(
        &self,
        _mode: u32,
        _integration_time: i32,
        _analog_gain: i32,
        _digital_gain: i32,
    ) -> Result<Value, Error> {
        const TAG: &str = "CameraFacade::set_cfg";
        fx_log_err!("{} {}", TAG, "NOT IMEPLEMENTED");
        Ok(to_value(())?)
    }

    /// Instructs the device to start a stream and save a frame from it.
    ///
    /// # Arguments
    /// * `file_path`: Specifies where on device the frame data is to be written after capture. The caller is expected to scp it from the path.
    ///
    /// Outputs an empty response.
    pub async fn capture(&self, _file_path: String) -> Result<Value, Error> {
        const TAG: &str = "CameraFacade::capture";
        fx_log_err!("{} {}", TAG, "NOT IMEPLEMENTED");
        Ok(to_value(())?)
    }

    /// Retrieves the data stored in the sensor's one time programmable memory.
    ///
    /// # Arguments
    /// * `file_path`: Specifies where on device the data is to be written after retrieval.
    ///
    /// Outputs an empty response.
    pub async fn get_otp(&self, _file_path: String) -> Result<Value, Error> {
        const TAG: &str = "CameraFacade::get_otp";
        fx_log_err!("{} {}", TAG, "NOT IMEPLEMENTED");
        Ok(to_value(())?)
    }

    /// Toggles the sensor's color bar pattern.
    ///
    /// # Arguments
    /// * `toggle`: Turns the color bar on (true) or off (false).
    ///
    /// Outputs an empty response.
    pub async fn color_bars(&self, _toggle: bool) -> Result<Value, Error> {
        const TAG: &str = "CameraFacade::color_bars";
        fx_log_err!("{} {}", TAG, "NOT IMEPLEMENTED");
        Ok(to_value(())?)
    }

    // TODO(fxbug.dev/52737): Revise the documentation for this method once more information becomes available.
    /// Loads and unloads camera modules in order.
    ///
    /// # Arguments
    /// * `toggle`: Loads (true) or unloads (false) the camera modules.
    ///
    /// Outputs an empty response.
    pub async fn enable(&self, _toggle: bool) -> Result<Value, Error> {
        const TAG: &str = "CameraFacade::enable";
        fx_log_err!("{} {}", TAG, "NOT IMEPLEMENTED");
        Ok(to_value(())?)
    }

    // TODO(fxbug.dev/52737): Revise the documentation for this method once more information becomes available.
    /// Updates the GPIO's value.
    ///
    /// # Arguments
    /// * `value`: The value to be loaded into the GPIO.
    ///
    /// Outputs an empty response.
    pub async fn reset(&self, _value: bool) -> Result<Value, Error> {
        const TAG: &str = "CameraFacade::reset";
        fx_log_err!("{} {}", TAG, "NOT IMEPLEMENTED");
        Ok(to_value(())?)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_camera2::{DeviceInfo, DeviceType};
    use fidl_fuchsia_camera2_hal::ControllerRequest;
    use futures::{future::Future, join, stream::StreamExt};

    struct MockCameraFactoryBuilder {
        expected: Vec<Box<dyn FnOnce(ControllerRequest) + Send + 'static>>,
    }

    impl MockCameraFactoryBuilder {
        fn new() -> Self {
            Self { expected: vec![] }
        }

        fn build(self) -> (CameraFacade, impl Future<Output = ()>) {
            let (proxy, mut stream) =
                fidl::endpoints::create_proxy_and_stream::<ControllerMarker>().unwrap();
            let fut = async move {
                for expected in self.expected {
                    expected(stream.next().await.unwrap().unwrap());
                }
                if matches!(stream.next().await, None) {
                } else {
                    panic!("Not what was expected.")
                };
            };

            (CameraFacade::new_with_svc(Some(proxy)), fut)
        }

        fn push(mut self, request: impl FnOnce(ControllerRequest) + Send + 'static) -> Self {
            self.expected.push(Box::new(request));
            self
        }

        fn expect_get_device_info(self, device_info: DeviceInfo) -> Self {
            self.push(move |req| match req {
                ControllerRequest::GetDeviceInfo { responder } => {
                    responder.send(device_info).unwrap()
                }
                _ => panic!("Unexpected request."),
            })
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn detect_calls_get_device_info_fidl() {
        const TEST_UINT16: u16 = 256;
        const TEST_STR: &str = "test_string";

        let mock_device_info = DeviceInfo {
            vendor_id: Some(TEST_UINT16),
            vendor_name: Some(TEST_STR.to_string()),
            product_id: Some(TEST_UINT16),
            product_name: Some(TEST_STR.to_string()),
            type_: Some(DeviceType::Virtual),
        };

        let (facade, fut) =
            MockCameraFactoryBuilder::new().expect_get_device_info(mock_device_info).build();
        let test = async move {
            assert_eq!(
                facade.detect().await.unwrap(),
                DetectResult {
                    camera_id: 0,
                    camera_info: DeviceInfo {
                        vendor_id: Some(TEST_UINT16),
                        vendor_name: Some(TEST_STR.to_string()),
                        product_id: Some(TEST_UINT16),
                        product_name: Some(TEST_STR.to_string()),
                        type_: Some(DeviceType::Virtual),
                    },
                }
            );
        };

        join!(fut, test);
    }
}
