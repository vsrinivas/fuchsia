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
use crate::switchboard::light_types::{LightGroup, LightInfo, LightState, LightType, LightValue};
use crate::tests::fakes::hardware_light_service::HardwareLightService;
use crate::tests::fakes::service_registry::ServiceRegistry;
use crate::EnvironmentBuilder;
use std::collections::HashMap;

const ENV_NAME: &str = "settings_service_light_test_environment";
const CONTEXT_ID: u64 = 0;

const LIGHT_NAME_1: &str = "light_name_1";
const LIGHT_NAME_2: &str = "light_name_2";

fn get_test_light_info() -> LightInfo {
    let mut light_groups = HashMap::new();
    light_groups.insert(
        LIGHT_NAME_1.to_string(),
        LightGroup {
            name: Some(LIGHT_NAME_1.to_string()),
            enabled: Some(true),
            light_type: Some(LightType::Brightness),
            lights: Some(vec![LightState { value: Some(LightValue::Brightness(42)) }]),
            hardware_index: vec![0],
        },
    );
    light_groups.insert(
        LIGHT_NAME_2.to_string(),
        LightGroup {
            name: Some(LIGHT_NAME_2.to_string()),
            enabled: Some(true),
            light_type: Some(LightType::Simple),
            lights: Some(vec![LightState { value: Some(LightValue::Simple(true)) }]),
            hardware_index: vec![1],
        },
    );

    LightInfo { light_groups }
}

/// Populates the given HardwareLightService fake with the lights from a LightInfo.
///
/// Assumes that each light group has one light, as if the light groups were read from the
/// underlying fuchsia.hardware.light API.
async fn populate_single_test_lights(
    hardware_light_service_handle: Arc<Mutex<HardwareLightService>>,
) {
    for (name, group) in get_test_light_info().light_groups.into_iter() {
        hardware_light_service_handle
            .lock()
            .await
            .insert_light(
                group.hardware_index[0],
                name,
                group.light_type.unwrap(),
                group.lights.unwrap()[0].value.clone().unwrap(),
            )
            .await;
    }
}

/// Creates an environment for light.
///
/// Automatically populates the light groups from get_test_light_info so that light groups are
/// available on restore, since set and watch calls fail if a light with the given name is not
/// present.
async fn create_test_light_env(
    storage_factory: Arc<Mutex<InMemoryStorageFactory>>,
) -> (LightProxy, Arc<Mutex<DeviceStorage<LightInfo>>>) {
    let store = storage_factory
        .lock()
        .await
        .get_device_storage::<LightInfo>(StorageAccessContext::Test, CONTEXT_ID);

    let service_registry = ServiceRegistry::create();
    let hardware_light_service_handle = Arc::new(Mutex::new(HardwareLightService::new()));

    populate_single_test_lights(hardware_light_service_handle.clone()).await;
    service_registry.lock().await.register_service(hardware_light_service_handle.clone());

    let env = EnvironmentBuilder::new(storage_factory)
        .service(Box::new(ServiceRegistry::serve(service_registry)))
        .agents(&[restore_agent::blueprint::create()])
        .settings(&[SettingType::Light])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    let light_service = env.connect_to_service::<LightMarker>().unwrap();

    (light_service, store)
}

async fn set_light_value(service: &LightProxy, light_group: LightGroup) {
    service
        .set_light_group_values(
            light_group.name.unwrap().as_str(),
            &mut light_group.lights.unwrap().into_iter().map(LightState::into),
        )
        .await
        .expect("set completed")
        .expect("set successful");
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_light_restore() {
    // Create and fetch a store from device storage so we can read stored value for testing.
    let factory = InMemoryStorageFactory::create();
    let (light_service, store) = create_test_light_env(factory).await;

    let expected_light_group = get_test_light_info().light_groups.remove(LIGHT_NAME_1).unwrap();

    // Verify that the restored value is persisted.
    let mut store_lock = store.lock().await;
    let retrieved_struct = store_lock.get().await;
    assert_eq!(
        &expected_light_group.clone(),
        retrieved_struct.light_groups.get(LIGHT_NAME_1).unwrap()
    );

    // Verify that the restored value is returned on a watch call.
    let settings: fidl_fuchsia_settings::LightGroup =
        light_service.watch_light_group(LIGHT_NAME_1).await.expect("watch completed");
    assert_eq!(fidl_fuchsia_settings::LightGroup::from(expected_light_group), settings);
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_light() {
    let mut expected_light_info = get_test_light_info();
    let mut changed_light_group =
        expected_light_info.light_groups.get(LIGHT_NAME_1).unwrap().clone();
    changed_light_group.lights =
        Some(vec![LightState { value: Some(LightValue::Brightness(128)) }]);
    expected_light_info.light_groups.insert(LIGHT_NAME_1.to_string(), changed_light_group.clone());

    // Create and fetch a store from device storage so we can read stored value for testing.
    let factory = InMemoryStorageFactory::create();
    let (light_service, store) = create_test_light_env(factory).await;

    // Set a light value.
    set_light_value(&light_service, changed_light_group).await;

    // Verify the value we set is persisted in DeviceStorage.
    let mut store_lock = store.lock().await;
    let retrieved_struct = store_lock.get().await;
    assert_eq!(expected_light_info, retrieved_struct);

    // Ensure value from Watch matches set value.
    let mut settings: Vec<fidl_fuchsia_settings::LightGroup> =
        light_service.watch_light_groups().await.expect("watch completed");
    settings.sort_by_key(|group: &fidl_fuchsia_settings::LightGroup| group.name.clone());

    // Watch returns vector, internally we use a HashMap, so convert into a vector for comparison.
    let mut expected_value = expected_light_info
        .light_groups
        .drain()
        .map(|(_, value)| fidl_fuchsia_settings::LightGroup::from(value))
        .collect::<Vec<_>>();
    expected_value.sort_by_key(|group| group.name.clone());
    assert_eq!(settings, expected_value);
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
    let test_light_info = get_test_light_info();
    let light_group_1 = test_light_info.light_groups.get(LIGHT_NAME_1).unwrap().clone();
    let mut light_group_1_updated = light_group_1.clone();
    light_group_1_updated.lights =
        Some(vec![LightState { value: Some(LightValue::Brightness(128)) }]);

    let light_group_2 = test_light_info.light_groups.get(LIGHT_NAME_2).unwrap().clone();
    let mut light_group_2_updated = light_group_2.clone();
    light_group_2_updated.lights =
        Some(vec![LightState { value: Some(LightValue::Simple(false)) }]);

    // Create and fetch a store from device storage so we can read stored value for testing.
    let factory = InMemoryStorageFactory::create();
    let (light_service, _) = create_test_light_env(factory).await;

    // Ensure values from Watch matches set values.
    let settings = light_service.watch_light_group(LIGHT_NAME_1).await.expect("watch completed");
    assert_eq!(settings, light_group_1.into());
    let settings = light_service.watch_light_group(LIGHT_NAME_2).await.expect("watch completed");
    assert_eq!(settings, light_group_2.into());

    // Set updated values for the two lights.
    set_light_value(&light_service, light_group_1_updated.clone()).await;

    let settings = light_service.watch_light_group(LIGHT_NAME_1).await.expect("watch completed");
    assert_eq!(settings, light_group_1_updated.into());

    set_light_value(&light_service, light_group_2_updated.clone()).await;

    let settings = light_service.watch_light_group(LIGHT_NAME_2).await.expect("watch completed");
    assert_eq!(settings, light_group_2_updated.into());
}
