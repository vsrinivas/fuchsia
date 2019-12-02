// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    crate::create_fidl_service, crate::registry::device_storage::testing::*,
    crate::registry::device_storage::DeviceStorageFactory, crate::service_context::ServiceContext,
    crate::switchboard::base::PrivacyInfo, crate::switchboard::base::SettingType,
    fidl_fuchsia_settings::*, fuchsia_async as fasync, fuchsia_component::server::ServiceFs,
    futures::prelude::*, parking_lot::RwLock, std::sync::Arc,
};

const ENV_NAME: &str = "settings_service_privacy_test_environment";

#[fuchsia_async::run_singlethreaded(test)]
async fn test_privacy() {
    let mut fs = ServiceFs::new();

    let initial_value = PrivacyInfo { user_data_sharing_consent: None };

    let changed_value = PrivacyInfo { user_data_sharing_consent: Some(true) };

    // Create and fetch a store from device storage so we can read stored value for testing.
    let factory = Box::new(InMemoryStorageFactory::create());
    let store = factory.get_store::<PrivacyInfo>();

    create_fidl_service(
        fs.root_dir(),
        [SettingType::Privacy].iter().cloned().collect(),
        Arc::new(RwLock::new(ServiceContext::new(None))),
        factory,
    );

    let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
    fasync::spawn(fs.collect());

    let privacy_service = env.connect_to_service::<PrivacyMarker>().unwrap();

    // Ensure retrieved value matches set value
    let settings = privacy_service.watch().await.expect("watch completed");
    assert_eq!(
        settings.unwrap().user_data_sharing_consent,
        initial_value.user_data_sharing_consent
    );

    // Ensure setting interface propagates correctly
    let mut privacy_settings = fidl_fuchsia_settings::PrivacySettings::empty();
    privacy_settings.user_data_sharing_consent = Some(true);
    privacy_service.set(privacy_settings).await.expect("set completed").expect("set successful");

    // Verify the value we set is persisted in DeviceStorage.
    let mut store_lock = store.lock().await;
    let retrieved_struct = store_lock.get().await;
    assert_eq!(changed_value, retrieved_struct);

    // Ensure retrieved value matches set value
    let settings = privacy_service.watch().await.expect("watch completed");
    assert_eq!(
        settings.unwrap().user_data_sharing_consent,
        changed_value.user_data_sharing_consent
    );
}
