// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use crate::update::{facade::UpdateFacade, types::UpdateMethod};
use anyhow::Error;
use futures::future::{FutureExt, LocalBoxFuture};
use serde_json::{to_value, Value};

impl Facade for UpdateFacade {
    fn handle_request(
        &self,
        method: String,
        args: Value,
    ) -> LocalBoxFuture<'_, Result<Value, Error>> {
        update_method_to_fidl(method, args, self).boxed_local()
    }
}

// Takes SL4F method command and executes corresponding update facade method.
async fn update_method_to_fidl(
    method_name: String,
    args: Value,
    facade: &UpdateFacade,
) -> Result<Value, Error> {
    Ok(match method_name.parse()? {
        UpdateMethod::GetState => to_value(facade.get_state().await?),
        UpdateMethod::CheckNow => to_value(facade.check_now(args).await?),
        UpdateMethod::GetCurrentChannel => to_value(facade.get_current_channel().await?),
        UpdateMethod::GetTargetChannel => to_value(facade.get_target_channel().await?),
        UpdateMethod::SetTargetChannel => to_value(facade.set_target_channel(args).await?),
        UpdateMethod::GetChannelList => to_value(facade.get_channel_list().await?),
    }?)
}
