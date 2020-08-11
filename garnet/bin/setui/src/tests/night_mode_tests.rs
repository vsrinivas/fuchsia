// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    crate::handler::device_storage::testing::*,
    crate::switchboard::base::{NightModeInfo, SettingType},
    crate::EnvironmentBuilder,
    fidl_fuchsia_settings::*,
};

const ENV_NAME: &str = "settings_service_night_mode_test_environment";
const CONTEXT_ID: u64 = 0;

#[fuchsia_async::run_until_stalled(test)]
async fn test_night_mode() {
    let initial_value = NightModeInfo { night_mode_enabled: None };
    let changed_value = NightModeInfo { night_mode_enabled: Some(true) };

    // Create and fetch a store from device storage so we can read stored value for testing.
    let factory = InMemoryStorageFactory::create();
    let store = factory
        .lock()
        .await
        .get_device_storage::<NightModeInfo>(StorageAccessContext::Test, CONTEXT_ID);

    let env = EnvironmentBuilder::new(factory)
        .settings(&[SettingType::NightMode])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    let night_mode_service = env.connect_to_service::<NightModeMarker>().unwrap();
    // Ensure retrieved value matches set value
    let settings = night_mode_service.watch().await.expect("watch completed");
    assert_eq!(settings.night_mode_enabled, initial_value.night_mode_enabled);
    // Ensure setting interface propagates correctly
    let mut night_mode_settings = fidl_fuchsia_settings::NightModeSettings::empty();
    night_mode_settings.night_mode_enabled = Some(true);
    night_mode_service
        .set(night_mode_settings)
        .await
        .expect("set completed")
        .expect("set successful");

    // Verify the value we set is persisted in DeviceStorage.
    let mut store_lock = store.lock().await;
    let retrieved_struct = store_lock.get().await;
    assert_eq!(changed_value, retrieved_struct);

    // Ensure retrieved value matches set value
    let settings = night_mode_service.watch().await.expect("watch completed");
    assert_eq!(settings.night_mode_enabled, changed_value.night_mode_enabled);
}
