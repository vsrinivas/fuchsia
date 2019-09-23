// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    crate::create_fidl_service,
    crate::registry::device_storage::testing::*,
    crate::registry::device_storage::DeviceStorageFactory,
    crate::registry::service_context::ServiceContext,
    crate::switchboard::base::SettingType,
    crate::switchboard::base::{ConfigurationInterfaceFlags, SetupInfo},
    fidl_fuchsia_settings::*,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
    parking_lot::RwLock,
    std::sync::Arc,
};

const ENV_NAME: &str = "settings_service_setup_test_environment";

// Ensures the default value returned is WiFi.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_setup_default() {
    let mut fs = ServiceFs::new();

    let storage_factory = Box::new(InMemoryStorageFactory::create());

    create_fidl_service(
        fs.root_dir(),
        [SettingType::Setup].iter().cloned().collect(),
        Arc::new(RwLock::new(ServiceContext::new(None))),
        storage_factory,
    );

    let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
    fasync::spawn(fs.collect());

    let setup_service = env.connect_to_service::<SetupMarker>().unwrap();

    // Ensure retrieved value matches default value
    let settings = setup_service.watch().await.expect("watch completed");
    assert_eq!(
        settings.enabled_configuration_interfaces,
        Some(fidl_fuchsia_settings::ConfigurationInterfaces::Wifi)
    );
}

// Setup doesn't rely on any service yet. In the future this test will be
// updated to verify restart request is made on interface change.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_setup() {
    let mut fs = ServiceFs::new();

    let storage_factory = Box::new(InMemoryStorageFactory::create());
    let store = storage_factory.get_store::<SetupInfo>();

    // Prepopulate initial value
    {
        let mut store_lock = store.lock().await;
        // Ethernet and WiFi is written out as initial value since the default
        // is currently WiFi only.
        assert!(store_lock
            .write(
                SetupInfo {
                    configuration_interfaces: ConfigurationInterfaceFlags::WIFI
                        | ConfigurationInterfaceFlags::ETHERNET
                },
                false
            )
            .await
            .is_ok());
    }

    create_fidl_service(
        fs.root_dir(),
        [SettingType::Setup].iter().cloned().collect(),
        Arc::new(RwLock::new(ServiceContext::new(None))),
        storage_factory,
    );

    let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
    fasync::spawn(fs.collect());

    let setup_service = env.connect_to_service::<SetupMarker>().unwrap();

    // Ensure retrieved value matches set value
    let settings = setup_service.watch().await.expect("watch completed");
    assert_eq!(
        settings.enabled_configuration_interfaces,
        Some(
            fidl_fuchsia_settings::ConfigurationInterfaces::Wifi
                | fidl_fuchsia_settings::ConfigurationInterfaces::Ethernet
        )
    );

    let expected_interfaces = fidl_fuchsia_settings::ConfigurationInterfaces::Ethernet;

    // Ensure setting interface propagates  change correctly
    let mut setup_settings = fidl_fuchsia_settings::SetupSettings::empty();
    setup_settings.enabled_configuration_interfaces = Some(expected_interfaces);
    setup_service.set(setup_settings).await.expect("set completed").expect("set successful");

    // Ensure retrieved value matches set value
    let settings = setup_service.watch().await.expect("watch completed");
    assert_eq!(settings.enabled_configuration_interfaces, Some(expected_interfaces));

    // Check to make sure value wrote out to store correctly.
    {
        let mut store_lock = store.lock().await;
        assert_eq!(
            store_lock.get().await.configuration_interfaces,
            ConfigurationInterfaceFlags::ETHERNET
        );
    }
}
