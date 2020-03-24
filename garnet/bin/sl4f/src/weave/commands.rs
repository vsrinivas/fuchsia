// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use crate::weave::{facade::WeaveFacade, types::FactoryDataManagerMethod};
use anyhow::Error;
use futures::future::{FutureExt, LocalBoxFuture};
use serde_json::{to_value, Value};

impl Facade for WeaveFacade {
    fn handle_request(
        &self,
        method: String,
        args: Value,
    ) -> LocalBoxFuture<'_, Result<Value, Error>> {
        weave_method_to_fidl(method, args, self).boxed_local()
    }
}

async fn weave_method_to_fidl(
    method_name: String,
    _args: Value,
    facade: &WeaveFacade,
) -> Result<Value, Error> {
    Ok(match method_name.parse()? {
        FactoryDataManagerMethod::GetPairingCode => to_value(facade.get_pairing_code().await?),
    }?)
}
