// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::storage::device_storage::testing::InMemoryStorageFactory;
use crate::agent::storage::device_storage::DeviceStorage;
use crate::base::SettingType;
use crate::ingress::fidl::Interface;
use crate::keyboard::types::{Autorepeat, KeyboardInfo, KeymapId};
use crate::tests::test_failure_utils::create_test_env_with_failures;
use crate::EnvironmentBuilder;
use assert_matches::assert_matches;
use fidl::Error::ClientChannelClosed;
use fidl_fuchsia_settings::{KeyboardMarker, KeyboardProxy};
use fuchsia_zircon::Status;
use std::convert::TryFrom;
use std::sync::Arc;

const ENV_NAME: &str = "settings_service_keyboard_test_environment";

/// Creates an environment that will fail on a get request.
async fn create_keyboard_test_env_with_failures() -> KeyboardProxy {
    let storage_factory = InMemoryStorageFactory::new();
    create_test_env_with_failures(
        Arc::new(storage_factory),
        ENV_NAME,
        Interface::Keyboard,
        SettingType::Keyboard,
    )
    .await
    .connect_to_protocol::<KeyboardMarker>()
    .unwrap()
}

/// Creates an environment for keyboard.
async fn create_test_keyboard_env(
    storage_factory: Arc<InMemoryStorageFactory>,
) -> (KeyboardProxy, Arc<DeviceStorage>) {
    let env = EnvironmentBuilder::new(Arc::clone(&storage_factory))
        .fidl_interfaces(&[Interface::Keyboard])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    let keyboard_service = env.connect_to_protocol::<KeyboardMarker>().unwrap();
    let store = storage_factory.get_device_storage().await;

    (keyboard_service, store)
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_keyboard() {
    let initial_value = KeyboardInfo { keymap: Some(KeymapId::UsQwerty), autorepeat: None };
    let changed_value = KeyboardInfo {
        keymap: Some(KeymapId::UsDvorak),
        autorepeat: Some(Autorepeat { delay: 2, period: 1 }),
    };
    let factory = InMemoryStorageFactory::new();

    // Create and fetch a store from device storage so we can read stored value for testing.
    let (keyboard_service, store) = create_test_keyboard_env(Arc::new(factory)).await;

    // Ensure retrieved value matches set value.
    let settings = keyboard_service.watch().await.expect("watch completed");
    let keymap_watch_result = settings.keymap.map(|id| KeymapId::try_from(id).unwrap());
    assert_eq!(keymap_watch_result, initial_value.keymap);
    assert_eq!(settings.autorepeat, None);

    // Ensure setting interface propagates correctly.
    let mut keyboard_settings = fidl_fuchsia_settings::KeyboardSettings::EMPTY;
    keyboard_settings.keymap = Some(fidl_fuchsia_input::KeymapId::UsDvorak);
    keyboard_settings.autorepeat = Some(fidl_fuchsia_settings::Autorepeat { delay: 2, period: 1 });
    keyboard_service.set(keyboard_settings).await.expect("set completed").expect("set successful");

    // Verify the value we set is persisted in DeviceStorage.
    let retrieved_struct = store.get().await;
    assert_eq!(changed_value, retrieved_struct);

    // Ensure retrieved value matches set value.
    let settings = keyboard_service.watch().await.expect("watch completed");
    let keymap_watch_result = settings.keymap.map(|id| KeymapId::try_from(id).unwrap());
    assert_eq!(keymap_watch_result, changed_value.keymap);
    let autorepeat_watch_result = settings.autorepeat.map(|val| val.into());
    assert_eq!(autorepeat_watch_result, changed_value.autorepeat);
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_keyboard_clean_autorepeat_and_unset_keymap() {
    let initial_value = KeyboardInfo {
        keymap: Some(KeymapId::UsQwerty),
        autorepeat: Some(Autorepeat { delay: 1, period: 2 }),
    };
    let changed_value = KeyboardInfo { keymap: Some(KeymapId::UsQwerty), autorepeat: None };
    let factory = InMemoryStorageFactory::new();

    // Create and fetch a store from device storage so we can read stored value for testing.
    let (keyboard_service, store) = create_test_keyboard_env(Arc::new(factory)).await;

    // Setup initial values.
    let mut init_settings = fidl_fuchsia_settings::KeyboardSettings::EMPTY;
    init_settings.keymap = Some(fidl_fuchsia_input::KeymapId::UsQwerty);
    init_settings.autorepeat = Some(fidl_fuchsia_settings::Autorepeat { delay: 1, period: 2 });
    keyboard_service.set(init_settings).await.expect("set completed").expect("set successful");

    // Ensure retrieved value matches set value.
    let settings = keyboard_service.watch().await.expect("watch completed");
    let keymap_watch_result = settings.keymap.map(|id| KeymapId::try_from(id).unwrap());
    assert_eq!(keymap_watch_result, initial_value.keymap);
    assert_eq!(settings.autorepeat.map(|val| Autorepeat::from(val)), initial_value.autorepeat);

    // Ensure setting interface propagates correctly.
    let mut keyboard_settings = fidl_fuchsia_settings::KeyboardSettings::EMPTY;
    keyboard_settings.keymap = None;
    keyboard_settings.autorepeat = Some(fidl_fuchsia_settings::Autorepeat { delay: 0, period: 0 });
    keyboard_service.set(keyboard_settings).await.expect("set completed").expect("set successful");

    // Verify the value we set is persisted in DeviceStorage.
    let retrieved_struct = store.get().await;
    assert_eq!(changed_value, retrieved_struct);

    // Ensure retrieved value matches set value.
    let settings = keyboard_service.watch().await.expect("watch completed");
    let keymap_watch_result = settings.keymap.map(|id| KeymapId::try_from(id).unwrap());
    assert_eq!(keymap_watch_result, changed_value.keymap);
    let autorepeat_watch_result = settings.autorepeat.map(|val| val.into());
    assert_eq!(autorepeat_watch_result, changed_value.autorepeat);
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_channel_failure_watch() {
    let keyboard_service = create_keyboard_test_env_with_failures().await;
    let result = keyboard_service.watch().await;
    assert_matches!(result, Err(ClientChannelClosed { status: Status::UNAVAILABLE, .. }));
}
