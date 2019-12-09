// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use failure::{format_err, Error};
use fuchsia_syslog::macros::*;
use futures::future::{FutureExt, LocalBoxFuture};
use serde_json::{to_value, Value};

// Testing helper methods
use crate::wlan::facade::WlanFacade;

use crate::common_utils::common::parse_u64_identifier;

impl Facade for WlanFacade {
    fn handle_request(
        &self,
        method: String,
        args: Value,
    ) -> LocalBoxFuture<'_, Result<Value, Error>> {
        wlan_method_to_fidl(method, args, self).boxed_local()
    }
}

// Takes ACTS method command and executes corresponding FIDL method
// Packages result into serde::Value
async fn wlan_method_to_fidl(
    method_name: String,
    args: Value,
    wlan_facade: &WlanFacade,
) -> Result<Value, Error> {
    match method_name.as_ref() {
        "scan" => {
            fx_log_info!(tag: "WlanFacade", "performing wlan scan");
            let results = wlan_facade.scan().await?;
            fx_log_info!(tag: "WlanFacade", "received {:?} scan results", results.len());
            // return the scan results
            to_value(results).map_err(|e| format_err!("error handling scan results: {}", e))
        }
        "connect" => {
            let target_ssid = match args.get("target_ssid") {
                Some(ssid) => {
                    let ssid = match ssid.as_str() {
                        Some(ssid) => ssid.as_bytes().to_vec(),
                        None => {
                            bail!("Please provide a target ssid");
                        }
                    };
                    ssid
                }
                None => bail!("Please provide a target ssid"),
            };

            let target_pwd = match args.get("target_pwd") {
                Some(pwd) => match pwd.clone().as_str() {
                    Some(pwd) => pwd.as_bytes().to_vec(),
                    None => {
                        fx_log_info!(tag: "WlanFacade", "Please check provided password");
                        vec![0; 0]
                    }
                },
                _ => vec![0; 0],
            };

            fx_log_info!(tag: "WlanFacade", "performing wlan connect to SSID: {:?}", target_ssid);
            let results = wlan_facade.connect(target_ssid, target_pwd).await?;
            to_value(results).map_err(|e| format_err!("error handling connection result: {}", e))
        }
        "get_iface_id_list" => {
            fx_log_info!(tag: "WlanFacade", "Getting the interface id list.");
            let result = wlan_facade.get_iface_id_list().await?;
            to_value(result).map_err(|e| format_err!("error handling get_iface_id_list: {}", e))
        }
        "destroy_iface" => {
            fx_log_info!(tag: "WlanFacade", "Performing wlan destroy_iface");
            let iface_id = parse_u64_identifier(args.clone())?;
            wlan_facade.destroy_iface(iface_id as u16).await?;
            to_value(true).map_err(|e| format_err!("error handling destroy_iface: {}", e))
        }
        "disconnect" => {
            fx_log_info!(tag: "WlanFacade", "performing wlan disconnect");
            wlan_facade.disconnect().await?;
            to_value(true).map_err(|e| format_err!("error handling disconnect: {}", e))
        }
        "status" => {
            fx_log_info!(tag: "WlanFacade", "fetching connection status");
            let result = wlan_facade.status().await?;
            to_value(result).map_err(|e| format_err!("error handling connection status: {}", e))
        }
        _ => return Err(format_err!("unsupported command!")),
    }
}
