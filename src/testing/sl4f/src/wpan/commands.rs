// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use crate::wpan::{facade::WpanFacade, types::WpanMethod};
use anyhow::Error;
use async_trait::async_trait;
use serde_json::{from_value, to_value, Value};

#[async_trait(?Send)]
impl Facade for WpanFacade {
    async fn handle_request(&self, method: String, _args: Value) -> Result<Value, Error> {
        Ok(match method.parse()? {
            WpanMethod::GetIsCommissioned => to_value(self.get_is_commissioned().await?),
            WpanMethod::GetMacAddressFilterSettings => {
                to_value(self.get_mac_address_filter_settings().await?)
            }
            WpanMethod::GetNcpChannel => to_value(self.get_ncp_channel().await?),
            WpanMethod::GetNcpMacAddress => to_value(self.get_ncp_mac_address().await?),
            WpanMethod::GetNcpRssi => to_value(self.get_ncp_rssi().await?),
            WpanMethod::GetNcpState => to_value(self.get_ncp_state().await?),
            WpanMethod::GetNetworkName => to_value(self.get_network_name().await?),
            WpanMethod::GetPanId => to_value(self.get_panid().await?),
            WpanMethod::GetPartitionId => to_value(self.get_partition_id().await?),
            WpanMethod::GetThreadRloc16 => to_value(self.get_thread_rloc16().await?),
            WpanMethod::GetThreadRouterId => to_value(self.get_thread_router_id().await?),
            WpanMethod::GetWeaveNodeId => to_value(self.get_weave_node_id().await?),
            WpanMethod::InitializeProxies => to_value(self.initialize_proxies().await?),
            WpanMethod::ReplaceMacAddressFilterSettings => to_value(
                self.replace_mac_address_filter_settings(match from_value(_args.clone()) {
                    Ok(settings) => settings,
                    _ => bail!(
                        "Invalid json argument to ReplaceMacAddressFilterSettings! - {}",
                        _args
                    ),
                })
                .await?,
            ),
        }?)
    }
}
