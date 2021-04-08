// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use anyhow::{format_err, Error};
use async_trait::async_trait;
use fidl_fuchsia_wlan_common as fidl_common;
use fidl_fuchsia_wlan_internal as fidl_internal;
use fuchsia_syslog::macros::*;
use serde::{Deserialize, Serialize};
use serde_json::{to_value, Value};
use std::collections::HashMap;

// Testing helper methods
use crate::wlan::facade::WlanFacade;

use crate::common_utils::common::parse_u64_identifier;

// We're using serde's "remote derive" feature to allow us to derive (De)Serialize for a third-
// party type (i.e. fidl_internal::BssDescription). See here for more info:
// https://serde.rs/remote-derive.html

#[derive(Serialize, Deserialize)]
#[serde(remote = "fidl_common::Cbw")]
#[repr(u32)]
pub enum CbwDef {
    Cbw20 = 0,
    Cbw40 = 1,
    Cbw40Below = 2,
    Cbw80 = 3,
    Cbw160 = 4,
    Cbw80P80 = 5,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "fidl_common::WlanChan")]
pub struct WlanChanDef {
    pub primary: u8,
    #[serde(with = "CbwDef")]
    pub cbw: fidl_common::Cbw,
    pub secondary80: u8,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "fidl_internal::BssTypes")]
pub enum BssTypesDef {
    Infrastructure = 1,
    Personal = 2,
    Independent = 3,
    Mesh = 4,
    AnyBss = 5,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "fidl_internal::BssDescription")]
struct BssDescriptionDef {
    pub bssid: [u8; 6],
    #[serde(with = "BssTypesDef")]
    pub bss_type: fidl_internal::BssTypes,
    pub beacon_period: u16,
    pub timestamp: u64,
    pub local_time: u64,
    pub cap: u16,
    pub ies: Vec<u8>,
    #[serde(with = "WlanChanDef")]
    pub chan: fidl_common::WlanChan,
    pub rssi_dbm: i8,
    pub snr_db: i8,
}
#[derive(serde::Serialize)]
struct BssDescriptionWrapper<'a>(
    #[serde(with = "BssDescriptionDef")] &'a fidl_internal::BssDescription,
);

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
            "scan_for_bss_info" => {
                fx_log_info!(tag: "WlanFacade", "performing wlan scan");
                let results = self.scan_for_bss_info().await?;
                fx_log_info!(tag: "WlanFacade", "received {:?} scan results", results.len());
                // convert all BssDescription, which can't be serialized, to BssDescriptionWrapper
                let results: HashMap<String, Vec<BssDescriptionWrapper<'_>>> = results
                    .iter()
                    .map(|(ssid, bss_desc)| {
                        (
                            String::from_utf8(ssid.clone()).unwrap(),
                            bss_desc
                                .iter()
                                .map(|bss_desc| BssDescriptionWrapper(&**bss_desc))
                                .collect(),
                        )
                    })
                    .collect();
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

                let target_bss_desc = match args.get("target_bss_desc") {
                    Some(bss_desc_json) => {
                        let bss_desc = BssDescriptionDef::deserialize(bss_desc_json)?;
                        Some(Box::new(bss_desc))
                    }
                    None => None,
                };

                fx_log_info!(tag: "WlanFacade", "performing wlan connect to SSID: {:?}", target_ssid);
                let results = self.connect(target_ssid, target_pwd, target_bss_desc).await?;
                to_value(results)
                    .map_err(|e| format_err!("error handling connection result: {}", e))
            }
            "get_iface_id_list" => {
                fx_log_info!(tag: "WlanFacade", "Getting the interface id list.");
                let result = self.get_iface_id_list().await?;
                to_value(result).map_err(|e| format_err!("error handling get_iface_id_list: {}", e))
            }
            "get_phy_id_list" => {
                fx_log_info!(tag: "WlanFacade", "Getting the phy id list.");
                let result = self.get_phy_id_list().await?;
                to_value(result).map_err(|e| format_err!("error handling get_phy_id_list: {}", e))
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
            "query_iface" => {
                let iface_id = match args.get("iface_id") {
                    Some(iface_id) => match iface_id.as_u64() {
                        Some(iface_id) => iface_id as u16,
                        None => return Err(format_err!("Could not parse iface id")),
                    },
                    None => return Err(format_err!("Please provide target iface id")),
                };

                fx_log_info!(tag: "WlanFacade", "performing wlan query iface");
                let result = self.query_iface(iface_id).await?;
                to_value(result).map_err(|e| format_err!("error handling query iface: {}", e))
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
