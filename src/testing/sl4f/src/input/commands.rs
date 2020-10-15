// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::input::{facade::InputFacade, types::InputMethod};
use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::{to_value, Value};

#[async_trait(?Send)]
impl Facade for InputFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.parse()? {
            InputMethod::Tap => {
                let result = self.tap(args).await?;
                Ok(to_value(result)?)
            }
            InputMethod::MultiFingerTap => {
                let result = self.multi_finger_tap(args).await?;
                Ok(to_value(result)?)
            }
            InputMethod::Swipe => {
                let result = self.swipe(args).await?;
                Ok(to_value(result)?)
            }
            InputMethod::MultiFingerSwipe => {
                let result = self.multi_finger_swipe(args).await?;
                Ok(to_value(result)?)
            }
        }
    }
}
