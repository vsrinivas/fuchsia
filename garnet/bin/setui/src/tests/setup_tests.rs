// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    crate::registry::device_storage::testing::*,
    crate::registry::device_storage::DeviceStorageFactory,
    crate::switchboard::base::SettingType,
    crate::switchboard::base::{ConfigurationInterfaceFlags, SetupInfo},
    crate::tests::fakes::device_admin_service::{Action, DeviceAdminService},
    crate::tests::fakes::service_registry::ServiceRegistry,
    crate::EnvironmentBuilder,
    crate::Runtime,
    fidl_fuchsia_settings::*,
    futures::lock::Mutex,
    std::sync::Arc,
};

const ENV_NAME: &str = "settings_service_setup_test_environment";

// Ensures the default value returned is WiFi.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_setup_default() {
    let env =
        EnvironmentBuilder::new(Runtime::Nested(ENV_NAME), InMemoryStorageFactory::create_handle())
            .settings(&[SettingType::Setup])
            .spawn_and_get_nested_environment()
            .await
            .unwrap();

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
    let storage_factory = InMemoryStorageFactory::create_handle();
    let store = storage_factory.lock().await.get_store::<SetupInfo>();

    // Prepopulate initial value
    {
        let initial_data = SetupInfo {
            configuration_interfaces: ConfigurationInterfaceFlags::WIFI
                | ConfigurationInterfaceFlags::ETHERNET,
        };

        let mut store_lock = store.lock().await;
        // Ethernet and WiFi is written out as initial value since the default
        // is currently WiFi only.
        assert!(store_lock.write(&initial_data, false).await.is_ok());
    }

    let service_registry = ServiceRegistry::create();
    let device_admin_service_handle = Arc::new(Mutex::new(DeviceAdminService::new()));
    service_registry.lock().await.register_service(device_admin_service_handle.clone());

    // Handle reboot
    let env = EnvironmentBuilder::new(Runtime::Nested(ENV_NAME), storage_factory)
        .service(ServiceRegistry::serve(service_registry.clone()))
        .settings(&[SettingType::Setup])
        .spawn_and_get_nested_environment()
        .await
        .unwrap();

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

    // Check to make sure value wrote out to store correctly
    {
        let mut store_lock = store.lock().await;
        assert_eq!(
            store_lock.get().await.configuration_interfaces,
            ConfigurationInterfaceFlags::ETHERNET
        );
    }

    // Ensure reboot was requested by the controller
    assert!(device_admin_service_handle
        .lock()
        .await
        .verify_action_sequence([Action::Reboot].to_vec()));
}
