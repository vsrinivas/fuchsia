// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_mlme::{
        MinstrelListResponse, MinstrelStatsRequest, MinstrelStatsResponse, MlmeProxy,
    },
    futures::Future,
};

pub struct MlmeQueryProxy {
    proxy: MlmeProxy,
}

impl MlmeQueryProxy {
    pub fn new(proxy: MlmeProxy) -> Self {
        MlmeQueryProxy { proxy }
    }

    pub fn get_minstrel_list(
        &self,
    ) -> impl Future<Output = Result<MinstrelListResponse, fidl::Error>> {
        self.proxy.list_minstrel_peers()
    }

    pub fn get_minstrel_peer(
        &self,
        mac_addr: [u8; 6],
    ) -> impl Future<Output = Result<MinstrelStatsResponse, fidl::Error>> {
        let mut req = MinstrelStatsRequest { mac_addr };
        self.proxy.get_minstrel_stats(&mut req)
    }
}
