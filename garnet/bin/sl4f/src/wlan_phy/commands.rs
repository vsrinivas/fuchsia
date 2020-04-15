// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{server::Facade, wlan_phy::facade::WlanPhyFacade};
use anyhow::{format_err, Error};
use async_trait::async_trait;
use serde_json::{to_value, Value};
use std::convert::TryInto;

#[async_trait(?Send)]
impl Facade for WlanPhyFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.as_ref() {
            "get_country" => {
                let phy_id =
                    args.get("phy_id").ok_or_else(|| format_err!("Must provide a `phy_id`"))?;
                let phy_id: u16 = phy_id
                    .as_u64()
                    .ok_or_else(|| format_err!("`phy_id` must be a number, but was {:?}", phy_id))?
                    .try_into()
                    .or_else(|_err| {
                        Err(format_err!("`phy_id` must fit u16, but was {:?}", phy_id))
                    })?;
                Ok(to_value(self.get_country(phy_id).await?)?)
            }
            _ => return Err(format_err!("unsupported command {} for wlan-phy-facade!", method)),
        }
    }
}
