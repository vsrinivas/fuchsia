// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use anyhow::{format_err, Error};
use async_trait::async_trait;
use fuchsia_syslog::macros::*;
use serde_json::{to_value, Value};

// Testing helper methods
use crate::wlan::facade::WlanFacade;

use crate::common_utils::common::parse_u64_identifier;

#[async_trait(?Send)]
impl Facade for WlanFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.as_ref() {
            "scan" => {
                fx_log_info!(tag: "WlanFacade", "performing wlan scan");
                let results = self.scan().await?;
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
                                return Err(format_err!("Please provide a target ssid"));
                            }
                        };
                        ssid
                    }
                    None => return Err(format_err!("Please provide a target ssid")),
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
                let results = self.connect(target_ssid, target_pwd).await?;
                to_value(results)
                    .map_err(|e| format_err!("error handling connection result: {}", e))
            }
            "get_iface_id_list" => {
                fx_log_info!(tag: "WlanFacade", "Getting the interface id list.");
                let result = self.get_iface_id_list().await?;
                to_value(result).map_err(|e| format_err!("error handling get_iface_id_list: {}", e))
            }
            "destroy_iface" => {
                fx_log_info!(tag: "WlanFacade", "Performing wlan destroy_iface");
                let iface_id = parse_u64_identifier(args.clone())?;
                self.destroy_iface(iface_id as u16).await?;
                to_value(true).map_err(|e| format_err!("error handling destroy_iface: {}", e))
            }
            "disconnect" => {
                fx_log_info!(tag: "WlanFacade", "performing wlan disconnect");
                self.disconnect().await?;
                to_value(true).map_err(|e| format_err!("error handling disconnect: {}", e))
            }
            "status" => {
                fx_log_info!(tag: "WlanFacade", "fetching connection status");
                let result = self.status().await?;
                to_value(result).map_err(|e| format_err!("error handling connection status: {}", e))
            }
            _ => return Err(format_err!("unsupported command!")),
        }
    }
}
