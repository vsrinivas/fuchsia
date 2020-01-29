// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use anyhow::Error;
use futures::future::{FutureExt, LocalBoxFuture};
use serde_json::{to_value, Value};

use crate::hwinfo::facade::HwinfoFacade;

impl Facade for HwinfoFacade {
    fn handle_request(
        &self,
        method: String,
        args: Value,
    ) -> LocalBoxFuture<'_, Result<Value, Error>> {
        hwinfo_method_to_fidl(method, args, self).boxed_local()
    }
}

async fn hwinfo_method_to_fidl(
    method_name: String,
    _args: Value,
    facade: &HwinfoFacade,
) -> Result<Value, Error> {
    match method_name.as_ref() {
        "HwinfoGetDeviceInfo" => {
            let result = facade.get_device_info().await?;
            Ok(to_value(result)?)
        }
        "HwinfoGetProductInfo" => {
            let result = facade.get_product_info().await?;
            Ok(to_value(result)?)
        }
        _ => bail!("Invalid Hwinfo FIDL method: {:?}", method_name),
    }
}
