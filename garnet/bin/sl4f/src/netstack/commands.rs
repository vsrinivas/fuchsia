// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::netstack::types::NetstackMethod;
use crate::server::Facade;
use anyhow::Error;
use futures::future::{FutureExt, LocalBoxFuture};
use serde_json::{to_value, Value};

use crate::common_utils::common::parse_u64_identifier;
use crate::netstack::facade::NetstackFacade;

impl Facade for NetstackFacade {
    fn handle_request(
        &self,
        method: String,
        args: Value,
    ) -> LocalBoxFuture<'_, Result<Value, Error>> {
        netstack_method_to_fidl(method, args, self).boxed_local()
    }
}

// Takes ACTS method command and executes corresponding Netstack Client
// FIDL methods.
async fn netstack_method_to_fidl(
    method_name: String,
    args: Value,
    facade: &NetstackFacade,
) -> Result<Value, Error> {
    match NetstackMethod::from_str(&method_name) {
        NetstackMethod::InitNetstack => {
            let result = facade.init_netstack_proxy()?;
            Ok(to_value(result)?)
        }
        NetstackMethod::ListInterfaces => {
            let result = facade.list_interfaces().await?;
            Ok(to_value(result)?)
        }
        NetstackMethod::GetInterfaceInfo => {
            let identifier = parse_u64_identifier(args)?;
            let result = facade.get_interface_info(identifier).await?;
            Ok(to_value(result)?)
        }
        NetstackMethod::EnableInterface => {
            let identifier = parse_u64_identifier(args)?;
            let result = facade.enable_interface(identifier).await?;
            Ok(to_value(result)?)
        }
        NetstackMethod::DisableInterface => {
            let identifier = parse_u64_identifier(args)?;
            let result = facade.disable_interface(identifier).await?;
            Ok(to_value(result)?)
        }
        _ => return Err(format_err!("Invalid Netstack FIDL method: {:?}", method_name)),
    }
}
