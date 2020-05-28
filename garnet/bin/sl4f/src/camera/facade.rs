// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fuchsia_syslog::fx_log_err;
use serde_json::{to_value, Value};

/// Facade that exposes various Camera interfaces.
#[derive(Debug)]
pub struct CameraFacade {}

impl CameraFacade {
    /// Instantiate a Facade.
    pub fn new() -> Self {
        CameraFacade {}
    }

    /// Checks whether the device under test has a camera and provides information about
    /// it. If the device has multiple cameras, the first one listed is chosen.
    ///
    /// Takes no arguments. Outputs the serialized DeviceInfo in a DetectResult struct.
    pub async fn detect(&self) -> Result<Value, Error> {
        const TAG: &str = "CameraFacade::detect";
        fx_log_err!("{} {}", TAG, "NOT IMEPLEMENTED");
        Ok(to_value(())?)
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

    // TODO(52737): Revise the documentation for this method once more information becomes available.
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

    // TODO(52737): Revise the documentation for this method once more information becomes available.
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
    // TODO(42847): end-to-end tests once FIDL service is linked up.
    use super::*;
    use fuchsia_async as fasync;

    #[fasync::run_singlethreaded(test)]
    async fn sanity() -> Result<(), Error> {
        let _facade = CameraFacade::new();
        Ok(())
    }
}
