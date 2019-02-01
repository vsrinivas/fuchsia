// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{Error, format_err};
use fuchsia_syslog::macros::*;
use serde_json::{to_value, Value};
use std::sync::Arc;

// Testing helper methods
use crate::wlan::facade::WlanFacade;

// Takes ACTS method command and executes corresponding FIDL method
// Packages result into serde::Value
pub async fn wlan_method_to_fidl(method_name: String,
                                 _args: Value,
                                 wlan_facade: Arc<WlanFacade>)
        -> Result<Value, Error> {
    match method_name.as_ref() {
        "scan" => {
            fx_log_info!(tag: "WlanFacade", "performing wlan scan");
            let results = await!(wlan_facade.scan());
            match results {
                Ok(results) => {
                    fx_log_info!(tag: "WlanFacade", "received {:?} scan results", results.len());
                    // return the scan results
                    to_value(results).map_err(|e| format_err!("error handling scan results: {}", e))
                },
                Err(e) => {
                    fx_log_info!(tag: "WlanFacade", "scan failed with error: {}", e);
                    // TODO: (CONN-3) need to improve error handlind and reporting.  for now, send
                    // back empty results to make sure the test fails
                    let empty_results: Vec<String> = Vec::new();
                    to_value(empty_results)
                        .map_err(|e| format_err!("report scan error failed: {}", e))
                },
            }
        },
        _ => {
            return Err(format_err!("unsupported command!"))
        }
    }
}
