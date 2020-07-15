// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use std::sync::Arc;

use crate::agent::restore_agent;
use fidl_fuchsia_settings::{LightMarker, LightProxy};
use futures::lock::Mutex;

use crate::registry::device_storage::testing::*;
use crate::registry::device_storage::DeviceStorage;
use crate::switchboard::base::SettingType;
use crate::switchboard::light_types::{LightGroup, LightInfo, LightState, LightValue};
use crate::tests::fakes::hardware_light_service::HardwareLightService;
use crate::tests::fakes::service_registry::ServiceRegistry;
use crate::EnvironmentBuilder;
use fidl_fuchsia_hardware_light::{Capability, Info};

const ENV_NAME: &str = "settings_service_light_test_environment";
const CONTEXT_ID: u64 = 0;

/// Creates an environment for light.
async fn create_test_light_env(
    storage_factory: Arc<Mutex<InMemoryStorageFactory>>,
) -> (LightProxy, Arc<Mutex<DeviceStorage<LightInfo>>>) {
    let store = storage_factory
        .lock()
        .await
        .get_device_storage::<LightInfo>(StorageAccessContext::Test, CONTEXT_ID);

    let service_registry = ServiceRegistry::create();
    let hardware_light_service_handle = Arc::new(Mutex::new(HardwareLightService::new()));
    service_registry.lock().await.register_service(hardware_light_service_handle.clone());

    let env = EnvironmentBuilder::new(storage_factory)
        .service(Box::new(ServiceRegistry::serve(service_registry)))
        .settings(&[SettingType::Light])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    let light_service = env.connect_to_service::<LightMarker>().unwrap();

    (light_service, store)
}

async fn set_light_value(service: &LightProxy, name: &str, light_group: LightGroup) {
    service
        .set_light_group_values(
            name,
            &mut light_group.lights.unwrap().into_iter().map(LightState::into),
        )
        .await
        .expect("set completed")
        .expect("set successful");
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_light_restore() {
    const LIGHT_INDEX: u32 = 0;
    const NAME: &str = "light_name";
    const CAPABILITY: Capability = Capability::Brightness;
    const VALUE: u8 = 42;

    let expected_light_group = LightGroup {
        name: Some(NAME.to_string()),
        enabled: Some(true),
        light_type: Some(CAPABILITY.into()),
        lights: Some(vec![LightState { value: Some(LightValue::Brightness(VALUE)) }]),
    };

    let storage_factory = InMemoryStorageFactory::create();

    let store = storage_factory
        .lock()
        .await
        .get_device_storage::<LightInfo>(StorageAccessContext::Test, CONTEXT_ID);

    let service_registry = ServiceRegistry::create();
    let hardware_light_service_handle = Arc::new(Mutex::new(HardwareLightService::new()));

    // Set fake values before environment spawns so that restore can read them.
    hardware_light_service_handle
        .lock()
        .await
        .insert_light_info(LIGHT_INDEX, Info { name: NAME.to_string(), capability: CAPABILITY })
        .await;
    hardware_light_service_handle.lock().await.insert_brightness_value(LIGHT_INDEX, VALUE).await;

    service_registry.lock().await.register_service(hardware_light_service_handle.clone());

    let env = EnvironmentBuilder::new(storage_factory)
        .service(Box::new(ServiceRegistry::serve(service_registry)))
        .agents(&[restore_agent::blueprint::create()])
        .settings(&[SettingType::Light])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    let light_service = env.connect_to_service::<LightMarker>().unwrap();

    // Verify that the restored value is persisted.
    let mut store_lock = store.lock().await;
    let retrieved_struct = store_lock.get().await;
    assert_eq!(&expected_light_group.clone(), retrieved_struct.light_groups.get(NAME).unwrap());

    // Verify that the restored value is returned on a watch call.
    let settings: fidl_fuchsia_settings::LightGroup =
        light_service.watch_light_group(NAME).await.expect("watch completed");
    assert_eq!(expected_light_group, settings.into());
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_light() {
    const LIGHT_NAME: &str = "test";
    let light_group: LightGroup = LightGroup {
        name: Some(LIGHT_NAME.to_string()),
        enabled: None,
        light_type: None,
        lights: Some(vec![LightState { value: Some(LightValue::Simple(true)) }]),
    };

    let mut changed_value = LightInfo { light_groups: Default::default() };
    changed_value.light_groups.insert(LIGHT_NAME.to_string(), light_group.clone().into());

    // Create and fetch a store from device storage so we can read stored value for testing.
    let factory = InMemoryStorageFactory::create();
    let (light_service, store) = create_test_light_env(factory).await;

    // Set a light value.
    set_light_value(&light_service, LIGHT_NAME, light_group).await;

    // Verify the value we set is persisted in DeviceStorage.
    let mut store_lock = store.lock().await;
    let retrieved_struct = store_lock.get().await;
    assert_eq!(changed_value.light_groups, retrieved_struct.light_groups);

    // Ensure value from Watch matches set value.
    let settings = light_service.watch_light_groups().await.expect("watch completed");
    // Watch returns vector, internally we use a HashMap.
    assert_eq!(
        settings
            .into_iter()
            .filter(|group| group.name.as_ref().map(|n| *n == LIGHT_NAME).unwrap_or(false))
            .collect::<Vec<_>>()[0],
        changed_value.light_groups.get(LIGHT_NAME).unwrap().clone().into()
    );
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_wrong_light_group_name() {
    // Create and fetch a store from device storage so we can read stored value for testing.
    let factory = InMemoryStorageFactory::create();
    let (light_service, _store) = create_test_light_env(factory).await;

    light_service.watch_light_group("random_name").await.expect_err("watch should fail");
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_individual_light_group() {
    const LIGHT_1_NAME: &str = "test";
    let light_group_1 = LightGroup {
        name: Some(LIGHT_1_NAME.to_string()),
        enabled: None,
        light_type: None,
        lights: Some(vec![LightState { value: Some(LightValue::Simple(true)) }]),
    };
    let mut light_group_1_updated = light_group_1.clone();
    light_group_1_updated.lights =
        Some(vec![LightState { value: Some(LightValue::Simple(false)) }]);

    const LIGHT_2_NAME: &str = "test2";
    let mut light_group_2 = light_group_1.clone();
    light_group_2.name = Some(LIGHT_2_NAME.to_string());
    let mut light_group_2_updated = light_group_2.clone();
    light_group_2_updated.lights =
        Some(vec![LightState { value: Some(LightValue::Simple(false)) }]);

    let mut expected_light_info = LightInfo { light_groups: Default::default() };
    expected_light_info.light_groups.insert(LIGHT_1_NAME.to_string(), light_group_1.clone().into());
    expected_light_info.light_groups.insert(LIGHT_2_NAME.to_string(), light_group_2.clone().into());

    // Create and fetch a store from device storage so we can read stored value for testing.
    let factory = InMemoryStorageFactory::create();
    let (light_service, store) = create_test_light_env(factory).await;

    // Set the initial light group values for both lights.
    set_light_value(&light_service, LIGHT_1_NAME, light_group_1.clone()).await;
    set_light_value(&light_service, LIGHT_2_NAME, light_group_2.clone()).await;

    // Verify the value we set is persisted in DeviceStorage.
    let mut store_lock = store.lock().await;
    let retrieved_struct: LightInfo = store_lock.get().await;
    assert_eq!(expected_light_info.light_groups, retrieved_struct.light_groups);

    // Ensure values from Watch matches set values.
    let settings = light_service.watch_light_group(LIGHT_1_NAME).await.expect("watch completed");
    assert_eq!(settings, light_group_1.into());
    let settings = light_service.watch_light_group(LIGHT_2_NAME).await.expect("watch completed");
    assert_eq!(settings, light_group_2.into());

    // Set updated values for the two lights.
    set_light_value(&light_service, LIGHT_1_NAME, light_group_1_updated.clone()).await;

    let settings = light_service.watch_light_group(LIGHT_1_NAME).await.expect("watch completed");
    assert_eq!(settings, light_group_1_updated.into());

    set_light_value(&light_service, LIGHT_2_NAME, light_group_2_updated.clone()).await;

    let settings = light_service.watch_light_group(LIGHT_2_NAME).await.expect("watch completed");
    assert_eq!(settings, light_group_2_updated.into());
}
