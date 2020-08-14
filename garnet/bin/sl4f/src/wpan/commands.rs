// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use crate::wpan::{facade::WpanFacade, types::WpanMethod};
use anyhow::Error;
use async_trait::async_trait;
use serde_json::{to_value, Value};

#[async_trait(?Send)]
impl Facade for WpanFacade {
    async fn handle_request(&self, method: String, _args: Value) -> Result<Value, Error> {
        Ok(match method.parse()? {
            WpanMethod::GetNcpChannel => to_value(self.get_ncp_channel().await?),
            WpanMethod::GetNcpMacAddress => to_value(self.get_ncp_mac_address().await?),
            WpanMethod::GetNcpRssi => to_value(self.get_ncp_rssi().await?),
            WpanMethod::GetNetworkName => to_value(self.get_network_name().await?),
            WpanMethod::GetPartitionId => to_value(self.get_partition_id().await?),
            WpanMethod::GetThreadRloc16 => to_value(self.get_thread_rloc16().await?),
            WpanMethod::GetWeaveNodeId => to_value(self.get_weave_node_id().await?),
            WpanMethod::InitializeProxies => to_value(self.initialize_proxies().await?),
        }?)
    }
}
