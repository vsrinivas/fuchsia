// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use async_trait::async_trait;
use serde::Serialize;
use serde_json::{from_value, to_value, Value};

use crate::camera::facade::CameraFacade;
use crate::camera::types::SetCfgRequest;
use crate::server::Facade;

fn wrap<T>(x: T) -> Result<Value, Error>
where
    T: Serialize,
{
    Ok(to_value(x)?)
}

#[async_trait(?Send)]
impl Facade for CameraFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.as_ref() {
            "Detect" => wrap(self.detect().await?),
            "GetSn" => wrap(self.get_sn().await?),
            "GetCfg" => wrap(self.get_cfg().await?),
            "SetCfg" => {
                let req: SetCfgRequest = from_value(args)?;
                wrap(
                    self.set_cfg(req.mode, req.integration_time, req.analog_gain, req.digital_gain)
                        .await?,
                )
            }
            "Capture" => {
                let req: String = from_value(args)?;
                wrap(self.capture(req).await?)
            }
            "GetOtp" => {
                let req: String = from_value(args)?;
                wrap(self.get_otp(req).await?)
            }
            "ColorBars" => {
                let req: bool = from_value(args)?;
                wrap(self.color_bars(req).await?)
            }
            "Enable" => {
                let req: bool = from_value(args)?;
                wrap(self.enable(req).await?)
            }
            "Reset" => {
                let req: bool = from_value(args)?;
                wrap(self.reset(req).await?)
            }
            _ => return Err(format_err!("invalid method: {}", method)),
        }
    }
}
