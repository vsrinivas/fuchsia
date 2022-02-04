// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::facade::NetstackFacade;
use crate::common_utils::common::parse_u64_identifier;
use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::{to_value, Value};

enum NetstackMethod<'a> {
    DisableInterface,
    EnableInterface,
    GetIpv6Addresses,
    GetLinkLocalIpv6Addresses,
    ListInterfaces,
    Undefined(&'a str),
}

impl NetstackMethod<'_> {
    pub fn from_str(method: &str) -> NetstackMethod<'_> {
        match method {
            "DisableInterface" => NetstackMethod::DisableInterface,
            "EnableInterface" => NetstackMethod::EnableInterface,
            "GetIpv6Addresses" => NetstackMethod::GetIpv6Addresses,
            "GetLinkLocalIpv6Addresses" => NetstackMethod::GetLinkLocalIpv6Addresses,
            "ListInterfaces" => NetstackMethod::ListInterfaces,
            method => NetstackMethod::Undefined(method),
        }
    }
}

#[async_trait(?Send)]
impl Facade for NetstackFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match NetstackMethod::from_str(&method) {
            NetstackMethod::ListInterfaces => {
                let result = self.list_interfaces().await?;
                to_value(result).map_err(Into::into)
            }
            NetstackMethod::GetIpv6Addresses => {
                let result = self.get_ipv6_addresses().await?;
                to_value(result).map_err(Into::into)
            }
            NetstackMethod::GetLinkLocalIpv6Addresses => {
                let result = self.get_link_local_ipv6_addresses().await?;
                to_value(result).map_err(Into::into)
            }
            NetstackMethod::EnableInterface => {
                let identifier = parse_u64_identifier(args)?;
                let result = self.enable_interface(identifier).await?;
                to_value(result).map_err(Into::into)
            }
            NetstackMethod::DisableInterface => {
                let identifier = parse_u64_identifier(args)?;
                let result = self.disable_interface(identifier).await?;
                to_value(result).map_err(Into::into)
            }
            NetstackMethod::Undefined(method) => {
                Err(anyhow!("invalid Netstack method: {}", method))
            }
        }
    }
}
