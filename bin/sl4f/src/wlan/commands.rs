// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{Error, format_err};
use serde_json::{to_value, Value};
use std::sync::Arc;
use crate::wlan::facade::WlanFacade;

// Takes ACTS method command and executes corresponding FIDL method
// Packages result into serde::Value
pub async fn wlan_method_to_fidl(method_name: String,
                                 _args: Value,
                                 _wlan_facade: Arc<WlanFacade>)
        -> Result<Value, Error> {
    let _result = match method_name.as_ref() {
        "scan" => {
            println!("got the scan command!");
        },
        _ => {
            return Err(format_err!("unsupported command!"))
        }
    };
    // for now we will just return true
    to_value(true).map_err(|_| format_err!("error with to_value"))
}
