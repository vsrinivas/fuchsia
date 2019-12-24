// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_factory_camera::{CameraFactoryError, CameraFactoryMarker, CameraFactoryProxy};
use fidl_fuchsia_mem::Buffer;
use fuchsia_component::client::connect_to_service;
use fuchsia_syslog::fx_log_err;
use parking_lot::RwLock;
use serde_json::{to_value, Value};

use crate::camera_factory::types::{
    CaptureImageResult, DetectCameraResult, Sl4fCameraFactoryError,
};
use crate::common_utils::common::macros::{fx_err_and_bail, with_line};

/// Facade providing access to Camera Factory interfaces.
#[derive(Debug)]
pub struct CameraFactoryFacade {
    camera_factory_svc: RwLock<Option<CameraFactoryProxy>>,
}

impl CameraFactoryFacade {
    /// Instantiate a Facade.
    pub fn new() -> Self {
        CameraFactoryFacade { camera_factory_svc: RwLock::new(None) }
    }

    /// Connect to the Camera Factory FIDL service.
    pub async fn init(&self) -> Result<Value, Error> {
        match connect_to_service::<CameraFactoryMarker>() {
            Ok(r) => {
                *self.camera_factory_svc.write() = Some(r);
                Ok(to_value(())?)
            }
            Err(err) => fx_err_and_bail!(
                &with_line!("CameraFactoryFacade::init"),
                format_err!("SL4F Error: Failed to connect to service {}.", err)
            ),
        }
    }

    /// Helper method for the methods that need a connected service. Checks that the init method
    /// has been called.
    async fn init_check(&self, tag: &str) -> Result<CameraFactoryProxy, Error> {
        match self.camera_factory_svc.read().clone() {
            Some(svc) => Ok(svc),
            None => fx_err_and_bail!(&with_line!(tag), "No CameraFactoryProxy created."),
        }
    }

    /// Checks whether the device under test has a camera and provides information about
    /// it. If the device has multiple cameras, the first one listed is chosen.
    ///
    /// Takes no arguments.
    pub async fn detect_camera(&self) -> Result<Value, Error> {
        const TAG: &str = "CameraFactoryFacade::detect_camera";
        let svc = self.init_check(TAG).await?;
        match svc.detect_camera().await? {
            Ok(r) => Ok(to_value(DetectCameraResult { camera_id: r.0, camera_info: r.1 })?),
            Err(_) => Ok(to_value(Sl4fCameraFactoryError::NoCamera)?),
        }
    }

    /// Initializes the camera sensor (and associated MIPI) and connects to a stream.
    ///
    /// Takes no arguments. Outputs an empty response.
    pub async fn start(&self) -> Result<Value, Error> {
        const TAG: &str = "CameraFactoryFacade::start";
        let svc = self.init_check(TAG).await?;
        match svc.start() {
            Ok(_) => Ok(to_value(())?),
            Err(_) => Ok(to_value(Sl4fCameraFactoryError::Streaming)?),
        }
    }

    /// Stops the camera sensor (and associated MIPI) stream.
    ///
    /// Takes no arguments. Outputs an empty response.
    pub async fn stop(&self) -> Result<Value, Error> {
        const TAG: &str = "CameraFactoryFacade::stop";
        let svc = self.init_check(TAG).await?;
        match svc.stop() {
            Ok(_) => Ok(to_value(())?),
            Err(_) => Ok(to_value(Sl4fCameraFactoryError::Streaming)?),
        }
    }

    /// Stops the camera sensor (and associated MIPI) stream.
    ///
    /// # Arguments
    /// * `mode`: One of the camera's predefined sensor modes (fpms, resolution,
    ///           etc).
    /// * `exposure`: The camera's sensor exposure parameter.
    /// * `analog_gain`: The camera's sensor analog gain parameter.
    /// * `digital_gain`: The camera's sensor digital gain parameter.
    ///
    /// Outputs an empty response.
    pub async fn set_config(
        &self,
        mode: u32,
        exposure: u32,
        analog_gain: i32,
        digital_gain: i32,
    ) -> Result<Value, Error> {
        const TAG: &str = "CameraFactoryFacade::set_config";
        let svc = self.init_check(TAG).await?;
        match svc.set_config(mode, exposure, analog_gain, digital_gain).await? {
            Ok(_) => Ok(to_value(())?),
            Err(err) => match err {
                CameraFactoryError::NoCamera => Ok(to_value(Sl4fCameraFactoryError::NoCamera)?),
                CameraFactoryError::Streaming => Ok(to_value(Sl4fCameraFactoryError::Streaming)?),
            },
        }
    }

    /// Instructs the device to save a frame from the stream, and send it back to the host.
    ///
    /// Takes no arguments.
    // TODO(42847): Implement scp functionality.
    pub async fn capture_image(&self) -> Result<Value, Error> {
        const TAG: &str = "CameraFactoryFacade::capture_image";
        let svc = self.init_check(TAG).await?;
        match svc.capture_image().await? {
            Ok(r) => Ok(to_value(CaptureImageResult { image_data: r.0, image_info: r.1 })?),
            Err(_) => Ok(to_value(Sl4fCameraFactoryError::Streaming)?),
        }
    }

    /// Instructs the device to save tuning data to a persistent factory partition.
    ///
    /// # Arguments
    /// * `calibration_data`: A blob of calibration data to be written to disk.
    /// * `file_path`: Outlines where on device the data is to be written.
    ///
    /// Outputs an empty response.
    pub async fn write_calibration_data(
        &self,
        calibration_data: &mut Buffer,
        file_path: String,
    ) -> Result<Value, Error> {
        const TAG: &str = "CameraFactoryFacade::write_calibration_data";
        let svc = self.init_check(TAG).await?;
        match svc.write_calibration_data(calibration_data, &file_path).await? {
            Ok(_) => Ok(to_value(())?),
            Err(_) => Ok(to_value(Sl4fCameraFactoryError::Streaming)?),
        }
    }
}

#[cfg(test)]
mod tests {
    // TODO(42847): end-to-end tests once FIDL service is linked up.
    use super::*;
    use fuchsia_async as fasync;

    #[fasync::run_singlethreaded(test)]
    async fn detect_camera_with_no_camera_fails() -> Result<(), Error> {
        let camera_factory_facade = CameraFactoryFacade::new();
        camera_factory_facade.init().await.unwrap();
        camera_factory_facade.detect_camera().await.unwrap_err();
        Ok(())
    }
}
