// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::TestEnv, fidl_fuchsia_power_clientlevel as fpowerclient,
    fidl_fuchsia_power_systemmode as fpowermode,
};

/// Convenience type for interacting with the Power Manager's client power level service.
pub struct PowerLevelClient {
    watcher_proxy: fpowerclient::WatcherProxy,
}

impl PowerLevelClient {
    pub fn new(test_env: &TestEnv, client_type: &str) -> Self {
        let connector = test_env.connect_to_protocol::<fpowerclient::ConnectorMarker>();
        let (watcher_proxy, watcher_remote) =
            fidl::endpoints::create_proxy::<fpowerclient::WatcherMarker>().unwrap();
        connector
            .connect(client_type_from_str(client_type), watcher_remote)
            .expect("Failed to connect power client");
        Self { watcher_proxy }
    }

    pub async fn get_power_level(&self) -> u64 {
        self.watcher_proxy.watch().await.expect("Failed to get client power level")
    }
}

/// Convenience type for interacting with the Power Manager's system power mode requester and
/// configurator services.
pub struct SystemModeClient {
    _requester_proxy: fpowermode::RequesterProxy,
    configurator_proxy: fpowermode::ClientConfiguratorProxy,
}

impl SystemModeClient {
    pub fn new(test_env: &TestEnv) -> Self {
        let _requester_proxy = test_env.connect_to_protocol::<fpowermode::RequesterMarker>();
        let configurator_proxy =
            test_env.connect_to_protocol::<fpowermode::ClientConfiguratorMarker>();
        Self { _requester_proxy, configurator_proxy }
    }

    pub async fn set_client_default_power_level(&self, client_type: &str, level: u64) {
        let client_type = client_type_from_str(client_type);
        let mut config = self
            .configurator_proxy
            .get(client_type)
            .await
            .expect("Failed to get client configuration")
            .expect("Missing client configuration");
        config.default_level = level;
        self.configurator_proxy
            .set(client_type, &mut config)
            .await
            .expect("Failed to set client configuration");
    }
}

fn client_type_from_str(client_type: &str) -> fpowerclient::ClientType {
    match client_type {
        "wlan" => fpowerclient::ClientType::Wlan,
        e => panic!("Invalid client type: {}", e),
    }
}
