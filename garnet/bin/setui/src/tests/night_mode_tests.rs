// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::handler::device_storage::testing::InMemoryStorageFactory;
use crate::ingress::fidl::Interface;
use crate::night_mode::types::NightModeInfo;
use crate::EnvironmentBuilder;
use fidl_fuchsia_settings::NightModeMarker;
use std::sync::Arc;

const ENV_NAME: &str = "settings_service_night_mode_test_environment";

#[fuchsia_async::run_until_stalled(test)]
async fn test_night_mode() {
    let initial_value = NightModeInfo { night_mode_enabled: None };
    let changed_value = NightModeInfo { night_mode_enabled: Some(true) };

    // Create and fetch a store from device storage so we can read stored value for testing.
    let factory = Arc::new(InMemoryStorageFactory::new());

    let env = EnvironmentBuilder::new(Arc::clone(&factory))
        .fidl_interfaces(&[Interface::NightMode])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    let night_mode_service = env.connect_to_protocol::<NightModeMarker>().unwrap();
    // Ensure retrieved value matches set value
    let settings = night_mode_service.watch().await.expect("watch completed");
    assert_eq!(settings.night_mode_enabled, initial_value.night_mode_enabled);
    // Ensure setting interface propagates correctly
    let mut night_mode_settings = fidl_fuchsia_settings::NightModeSettings::EMPTY;
    night_mode_settings.night_mode_enabled = Some(true);
    night_mode_service
        .set(night_mode_settings)
        .await
        .expect("set completed")
        .expect("set successful");

    let store = factory.get_device_storage().await;
    // Verify the value we set is persisted in DeviceStorage.
    let retrieved_struct = store.get().await;
    assert_eq!(changed_value, retrieved_struct);

    // Ensure retrieved value matches set value
    let settings = night_mode_service.watch().await.expect("watch completed");
    assert_eq!(settings.night_mode_enabled, changed_value.night_mode_enabled);
}
