// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use battery_client::BatteryClient;
use fidl::client::QueryResponseFut;
use fidl_fuchsia_power as fpower;
use futures::StreamExt;

pub struct TestBatteryManager {
    _stream: fpower::BatteryManagerRequestStream,
    watcher_client: fpower::BatteryInfoWatcherProxy,
}

impl TestBatteryManager {
    pub fn new(
        s: fpower::BatteryManagerRequestStream,
        watcher_client: fpower::BatteryInfoWatcherProxy,
    ) -> Self {
        Self { _stream: s, watcher_client }
    }

    pub async fn make_battery_client_with_test_manager() -> (BatteryClient, TestBatteryManager) {
        let (c, mut s) =
            fidl::endpoints::create_proxy_and_stream::<fpower::BatteryManagerMarker>().unwrap();

        let battery_client =
            BatteryClient::register_updates(c).expect("can register battery client");
        let watcher_client = match s.next().await.expect("valid fidl request") {
            Ok(fpower::BatteryManagerRequest::Watch { watcher, .. }) => {
                watcher.into_proxy().unwrap()
            }
            x => panic!("Expected watch request, got: {:?}", x),
        };

        let mgr = TestBatteryManager::new(s, watcher_client);
        (battery_client, mgr)
    }

    /// Sends the provided `update` to the `watcher_client`. Returns a Future associated with the
    /// send request.
    pub fn send_update(&self, update: fpower::BatteryInfo) -> QueryResponseFut<()> {
        let update_fut = self.watcher_client.on_change_battery_info(update);
        update_fut
    }
}
