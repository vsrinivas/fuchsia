// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::TestEnv, fidl_fuchsia_thermal as fthermal};

/// Convenience type for interacting with the Power Manager's thermal client service.
pub struct ThermalClient {
    watcher_proxy: fthermal::ClientStateWatcherProxy,
}

impl ThermalClient {
    pub fn new(test_env: &TestEnv, client_type: &str) -> Self {
        let connector = test_env.connect_to_protocol::<fthermal::ClientStateConnectorMarker>();
        let (watcher_proxy, watcher_remote) =
            fidl::endpoints::create_proxy::<fthermal::ClientStateWatcherMarker>().unwrap();
        connector.connect(client_type, watcher_remote).expect("Failed to connect thermal client");
        Self { watcher_proxy }
    }

    pub async fn get_thermal_state(&self) -> Result<u64, anyhow::Error> {
        self.watcher_proxy.watch().await.map_err(|e| e.into())
    }
}
