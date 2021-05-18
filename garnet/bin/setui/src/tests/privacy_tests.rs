// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::base::SettingType,
    crate::handler::device_storage::testing::InMemoryStorageFactory,
    crate::handler::device_storage::DeviceStorage,
    crate::ingress::fidl::Interface,
    crate::privacy::types::PrivacyInfo,
    crate::tests::test_failure_utils::create_test_env_with_failures,
    crate::EnvironmentBuilder,
    fidl::Error::ClientChannelClosed,
    fidl_fuchsia_settings::{PrivacyMarker, PrivacyProxy},
    fuchsia_zircon::Status,
    matches::assert_matches,
    std::sync::Arc,
};

const ENV_NAME: &str = "settings_service_privacy_test_environment";

/// Creates an environment that will fail on a get request.
async fn create_privacy_test_env_with_failures() -> PrivacyProxy {
    let storage_factory = InMemoryStorageFactory::new();
    create_test_env_with_failures(
        Arc::new(storage_factory),
        ENV_NAME,
        Interface::Privacy,
        SettingType::Privacy,
    )
    .await
    .connect_to_protocol::<PrivacyMarker>()
    .unwrap()
}

/// Creates an environment for privacy.
async fn create_test_privacy_env(
    storage_factory: Arc<InMemoryStorageFactory>,
) -> (PrivacyProxy, Arc<DeviceStorage>) {
    let env = EnvironmentBuilder::new(Arc::clone(&storage_factory))
        .fidl_interfaces(&[Interface::Privacy])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    let privacy_service = env.connect_to_protocol::<PrivacyMarker>().unwrap();
    let store = storage_factory.get_device_storage().await;

    (privacy_service, store)
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_privacy() {
    let initial_value = PrivacyInfo { user_data_sharing_consent: None };
    let changed_value = PrivacyInfo { user_data_sharing_consent: Some(true) };
    let factory = InMemoryStorageFactory::new();

    // Create and fetch a store from device storage so we can read stored value for testing.
    let (privacy_service, store) = create_test_privacy_env(Arc::new(factory)).await;

    // Ensure retrieved value matches set value
    let settings = privacy_service.watch().await.expect("watch completed");
    assert_eq!(settings.user_data_sharing_consent, initial_value.user_data_sharing_consent);

    // Ensure setting interface propagates correctly
    let mut privacy_settings = fidl_fuchsia_settings::PrivacySettings::EMPTY;
    privacy_settings.user_data_sharing_consent = Some(true);
    privacy_service.set(privacy_settings).await.expect("set completed").expect("set successful");

    // Verify the value we set is persisted in DeviceStorage.
    let retrieved_struct = store.get().await;
    assert_eq!(changed_value, retrieved_struct);

    // Ensure retrieved value matches set value
    let settings = privacy_service.watch().await.expect("watch completed");
    assert_eq!(settings.user_data_sharing_consent, changed_value.user_data_sharing_consent);
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_channel_failure_watch() {
    let privacy_service = create_privacy_test_env_with_failures().await;
    let result = privacy_service.watch().await;
    assert_matches!(result, Err(ClientChannelClosed { status: Status::UNAVAILABLE, .. }));
}
