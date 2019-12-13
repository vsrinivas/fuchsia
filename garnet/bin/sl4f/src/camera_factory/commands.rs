// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use futures::future::{FutureExt, LocalBoxFuture};
use serde_json::{from_value, Value};

use crate::camera_factory::facade::CameraFactoryFacade;
use crate::camera_factory::types::{SetConfigRequest, WriteCalibrationDataRequest};
use crate::server::Facade;

impl Facade for CameraFactoryFacade {
    fn handle_request(
        &self,
        method: String,
        args: Value,
    ) -> LocalBoxFuture<'_, Result<Value, Error>> {
        camera_factory_method_to_fidl(method, args, self).boxed_local()
    }
}

/// Takes JSON-RPC method command and forwards to corresponding CameraFactory FIDL methods.
pub async fn camera_factory_method_to_fidl(
    method_name: String,
    args: Value,
    facade: &CameraFactoryFacade,
) -> Result<Value, Error> {
    let result = match method_name.as_ref() {
        "DetectCamera" => facade.detect_camera().await,
        "Init" => facade.init().await,
        "Start" => facade.start().await,
        "Stop" => facade.stop().await,
        "SetConfig" => {
            let req: SetConfigRequest = from_value(args)?;
            facade.set_config(req.mode, req.exposure, req.analog_gain, req.digital_gain).await
        }
        "CaptureImage" => facade.capture_image().await,
        "WriteCalibrationData" => {
            let mut req: WriteCalibrationDataRequest = from_value(args)?;
            facade.write_calibration_data(&mut req.calibration_data, req.file_path).await
        }
        _ => bail!("invalid FIDL method: {}", method_name),
    }?;
    Ok(result)
}
