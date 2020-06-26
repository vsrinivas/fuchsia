// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use crate::registry::device_storage::testing::*;

use fidl_fuchsia_settings::*;

use crate::switchboard::base::SettingType;
use crate::switchboard::light_types::LightInfo;
use crate::EnvironmentBuilder;

const ENV_NAME: &str = "settings_service_light_test_environment";
const CONTEXT_ID: u64 = 0;

#[fuchsia_async::run_until_stalled(test)]
async fn test_light() {
    let light_name = "test";
    let mut changed_value = LightInfo { light_groups: Default::default() };
    changed_value.light_groups.insert(
        light_name.to_string(),
        LightGroup {
            name: Some(light_name.to_string()),
            enabled: None,
            type_: None,
            lights: Some(vec![LightState { value: Some(LightValue::On(true)) }]),
        }
        .into(),
    );

    // Create and fetch a store from device storage so we can read stored value for testing.
    let factory = InMemoryStorageFactory::create();
    let store = factory
        .lock()
        .await
        .get_device_storage::<LightInfo>(StorageAccessContext::Test, CONTEXT_ID);

    let env = EnvironmentBuilder::new(factory)
        .settings(&[SettingType::Light])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    let light_service = env.connect_to_service::<LightMarker>().unwrap();

    // Set a light value.
    light_service
        .set_light_group_values(
            light_name,
            &mut vec![LightState { value: Some(LightValue::On(true)) }].into_iter(),
        )
        .await
        .expect("set completed")
        .expect("set successful");

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
            .filter(|group| group.name == Some(light_name.to_string()))
            .collect::<Vec<_>>()[0],
        changed_value.light_groups.get(light_name).unwrap().clone().into()
    );
}
