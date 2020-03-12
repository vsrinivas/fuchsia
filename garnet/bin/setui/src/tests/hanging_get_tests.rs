// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use crate::registry::device_storage::testing::*;
use crate::switchboard::base::{ConfigurationInterfaceFlags, SettingType, SetupInfo};
use crate::tests::fakes::device_admin_service::DeviceAdminService;
use crate::tests::fakes::service_registry::ServiceRegistry;
use crate::EnvironmentBuilder;
use fidl_fuchsia_settings::*;
use fuchsia_component::server::NestedEnvironment;
use futures::lock::Mutex;
use std::sync::Arc;

const ENV_NAME: &str = "hanging_get_test_environment";

#[fuchsia_async::run_singlethreaded(test)]
async fn test_multiple_watches() {
    let storage_factory = InMemoryStorageFactory::create_handle();
    let store =
        storage_factory.lock().await.get_device_storage::<SetupInfo>(StorageAccessContext::Test);
    let initial_interfaces =
        ConfigurationInterfaceFlags::WIFI | ConfigurationInterfaceFlags::ETHERNET;

    // Prepopulate initial value
    {
        let mut store_lock = store.lock().await;
        let initial_data = SetupInfo { configuration_interfaces: initial_interfaces };
        assert!(store_lock.write(&initial_data, false).await.is_ok());
    }

    let service_registry = ServiceRegistry::create();
    let device_admin_service_handle = Arc::new(Mutex::new(DeviceAdminService::new()));
    service_registry.lock().await.register_service(device_admin_service_handle.clone());

    let env = EnvironmentBuilder::new(storage_factory)
        .service(ServiceRegistry::serve(service_registry.clone()))
        .settings(&[SettingType::Setup, SettingType::Power])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    let setup_service = env.connect_to_service::<SetupMarker>().unwrap();

    // This should return immediately with value.
    verify(
        setup_service.watch().await,
        Some(
            fidl_fuchsia_settings::ConfigurationInterfaces::Ethernet
                | fidl_fuchsia_settings::ConfigurationInterfaces::Wifi,
        ),
    );

    // The following calls should succeed but not return as no value is available.
    let second_watch = setup_service.watch();
    let third_watch = setup_service.watch();

    set_interfaces(env, Some(fidl_fuchsia_settings::ConfigurationInterfaces::Ethernet)).await;

    verify(second_watch.await, Some(fidl_fuchsia_settings::ConfigurationInterfaces::Ethernet));
    verify(third_watch.await, Some(fidl_fuchsia_settings::ConfigurationInterfaces::Ethernet));
}

fn verify(
    watch_result: Result<fidl_fuchsia_settings::SetupSettings, fidl::Error>,
    expected_configuration: Option<fidl_fuchsia_settings::ConfigurationInterfaces>,
) {
    let setup_values = watch_result.expect("watch completed");
    assert_eq!(setup_values.enabled_configuration_interfaces, expected_configuration);
}

async fn set_interfaces(
    env: NestedEnvironment,
    interfaces: Option<fidl_fuchsia_settings::ConfigurationInterfaces>,
) {
    let mut setup_settings = fidl_fuchsia_settings::SetupSettings::empty();
    setup_settings.enabled_configuration_interfaces = interfaces;
    let setup_service = env.connect_to_service::<SetupMarker>().unwrap();
    setup_service.set(setup_settings).await.ok();
}
