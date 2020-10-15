// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use crate::update::{facade::UpdateFacade, types::UpdateMethod};
use anyhow::Error;
use async_trait::async_trait;
use serde_json::{to_value, Value};

#[async_trait(?Send)]
impl Facade for UpdateFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        Ok(match method.parse()? {
            UpdateMethod::CheckNow => to_value(self.check_now(args).await?),
            UpdateMethod::GetCurrentChannel => to_value(self.get_current_channel().await?),
            UpdateMethod::GetTargetChannel => to_value(self.get_target_channel().await?),
            UpdateMethod::SetTargetChannel => to_value(self.set_target_channel(args).await?),
            UpdateMethod::GetChannelList => to_value(self.get_channel_list().await?),
        }?)
    }
}
