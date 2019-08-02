// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::netstack::types::NetstackMethod;
use failure::Error;
use serde_json::Value;
use std::sync::Arc;

use crate::netstack::facade::NetstackFacade;

// Takes ACTS method command and executes corresponding Netstack Client
// FIDL methods.
pub async fn netstack_method_to_fidl(
    method_name: String,
    _args: Value,
    facade: Arc<NetstackFacade>,
) -> Result<Value, Error> {
    match <NetstackMethod as std::str::FromStr>::from_str(&method_name)? {
        NetstackMethod::ListInterfaces => facade.list_interfaces().await,
    }
}
