// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use async_trait::async_trait;
use serde_json::{from_value, Value};

use crate::camera_factory::facade::CameraFactoryFacade;
use crate::camera_factory::types::{SetConfigRequest, WriteCalibrationDataRequest};
use crate::server::Facade;

#[async_trait(?Send)]
impl Facade for CameraFactoryFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.as_ref() {
            "DetectCamera" => self.detect_camera().await,
            "Init" => self.init().await,
            "Start" => self.start().await,
            "Stop" => self.stop().await,
            "SetConfig" => {
                let req: SetConfigRequest = from_value(args)?;
                self.set_config(req.mode, req.integration_time, req.analog_gain, req.digital_gain)
                    .await
            }
            "CaptureImage" => self.capture_image().await,
            "WriteCalibrationData" => {
                let mut req: WriteCalibrationDataRequest = from_value(args)?;
                self.write_calibration_data(&mut req.calibration_data, req.file_path).await
            }
            _ => return Err(format_err!("invalid FIDL method: {}", method)),
        }
    }
}
