// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::backlight::facade::BacklightFacade;
use crate::backlight::types::BacklightMethod;
use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::{to_value, Value};
use std::str::FromStr;

#[async_trait(?Send)]
impl Facade for BacklightFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match BacklightMethod::from_str(&method)? {
            BacklightMethod::GetStateNormalized => {
                Ok(to_value(self.get_state_normalized(args).await?)?)
            }
            BacklightMethod::SetStateNormalized => {
                Ok(to_value(self.set_state_normalized(args).await?)?)
            }
            BacklightMethod::GetNormalizedBrightnessScale => {
                Ok(to_value(self.get_normalized_brightness_scale(args).await?)?)
            }
            BacklightMethod::SetNormalizedBrightnessScale => {
                Ok(to_value(self.set_normalized_brightness_scale(args).await?)?)
            }
            // TODO(bradenkell): Add Get/SetStateAbsolute and GetMaxAbsoluteBrightness if they end
            // up being needed.
            _ => bail!("Invalid Backlight FIDL method: {:?}", method),
        }
    }
}
