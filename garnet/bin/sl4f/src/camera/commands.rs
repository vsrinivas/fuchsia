// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use async_trait::async_trait;
use serde_json::{from_value, to_value, Value};

use crate::camera::facade::CameraFacade;
use crate::camera::types::SetCfgRequest;
use crate::server::Facade;

#[async_trait(?Send)]
impl Facade for CameraFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        let result = match method.as_ref() {
            "DetectCamera" => self.detect().await?,
            "GetSn" => self.get_sn().await?,
            "GetCfg" => self.get_cfg().await?,
            "SetCfg" => {
                let req: SetCfgRequest = from_value(args)?;
                self.set_cfg(req.mode, req.integration_time, req.analog_gain, req.digital_gain)
                    .await?
            }
            "Capture" => {
                let req: String = from_value(args)?;
                self.capture(req).await?
            }
            "GetOtp" => {
                let req: String = from_value(args)?;
                self.get_otp(req).await?
            }
            "ColorBars" => {
                let req: bool = from_value(args)?;
                self.color_bars(req).await?
            }
            "Enable" => {
                let req: bool = from_value(args)?;
                self.enable(req).await?
            }
            "Reset" => {
                let req: bool = from_value(args)?;
                self.reset(req).await?
            }
            _ => return Err(format_err!("invalid method: {}", method)),
        };
        Ok(to_value(result)?)
    }
}
