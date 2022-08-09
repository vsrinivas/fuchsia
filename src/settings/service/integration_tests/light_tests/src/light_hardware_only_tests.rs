// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_settings::{LightError, LightGroup, LightState, LightType, LightValue};
use fidl_fuchsia_ui_types::ColorRgb;
use light_realm::{assert_fidl_light_group_eq, assert_lights_eq, HardwareLight, LightRealm};
use std::collections::HashMap;

const LIGHT_NAME_1: &str = "light_name_1";
const LIGHT_NAME_2: &str = "light_name_2";
const LIGHT_VAL: f64 = 0.42;

// Ensure this lines up with the data in `get_test_light_groups`.
fn get_test_hardware_lights() -> Vec<HardwareLight> {
    vec![
        HardwareLight { name: LIGHT_NAME_1.to_owned(), value: LightValue::Brightness(LIGHT_VAL) },
        HardwareLight { name: LIGHT_NAME_2.to_owned(), value: LightValue::On(true) },
    ]
}

fn get_test_light_groups() -> HashMap<String, LightGroup> {
    HashMap::from([
        (
            LIGHT_NAME_1.to_string(),
            LightGroup {
                name: Some(LIGHT_NAME_1.to_string()),
                enabled: Some(true),
                type_: Some(LightType::Brightness),
                lights: Some(vec![LightState {
                    value: Some(LightValue::Brightness(LIGHT_VAL)),
                    ..LightState::EMPTY
                }]),
                ..LightGroup::EMPTY
            },
        ),
        (
            LIGHT_NAME_2.to_string(),
            LightGroup {
                name: Some(LIGHT_NAME_2.to_string()),
                enabled: Some(true),
                type_: Some(LightType::Simple),
                lights: Some(vec![LightState {
                    value: Some(LightValue::On(true)),
                    ..LightState::EMPTY
                }]),
                ..LightGroup::EMPTY
            },
        ),
    ])
}

#[fuchsia::test]
async fn test_light_restore() {
    let realm = LightRealm::create_realm(get_test_hardware_lights())
        .await
        .expect("realm should be created");
    let light_proxy = LightRealm::connect_to_light_marker(&realm);
    let expected_light_info = get_test_light_groups();

    // Light controller will return values restored from the hardware service.
    let settings = light_proxy.watch_light_groups().await.expect("watch completed");
    assert_lights_eq!(settings, expected_light_info);
}

#[fuchsia::test]
async fn test_light_set_and_watch() {
    let mut expected_light_info = get_test_light_groups();
    let mut changed_light_group = expected_light_info[LIGHT_NAME_1].clone();
    let changed_light_state =
        LightState { value: Some(LightValue::Brightness(0.128)), ..LightState::EMPTY };
    changed_light_group.lights = Some(vec![changed_light_state.clone()]);
    let _ = expected_light_info.insert(LIGHT_NAME_1.to_string(), changed_light_group);

    let realm =
        LightRealm::create_realm(get_test_hardware_lights()).await.expect("should create realm");
    let light_proxy = LightRealm::connect_to_light_marker(&realm);

    light_proxy
        .set_light_group_values(LIGHT_NAME_1, &mut [changed_light_state].into_iter())
        .await
        .expect("fidl failed")
        .expect("set failed");

    // Ensure value from Watch matches set value.
    let light_groups: Vec<LightGroup> =
        light_proxy.watch_light_groups().await.expect("watch completed");
    assert_lights_eq!(light_groups, expected_light_info);
}

#[fuchsia::test]
async fn test_light_set_no_lights() {
    let realm = LightRealm::create_realm(None).await.expect("realm should be created");
    let light_proxy = LightRealm::connect_to_light_marker(&realm);

    // Hardware initialized with no lights, try to set one.
    let _ = light_proxy
        .set_light_group_values(
            LIGHT_NAME_1,
            &mut vec![LightState {
                value: Some(LightValue::Brightness(0.128)),
                ..LightState::EMPTY
            }]
            .into_iter()
            .map(LightState::into),
        )
        .await
        .expect("set completed")
        .expect_err("expected error");
}

#[fuchsia::test]
async fn test_light_set_wrong_size() {
    let realm = LightRealm::create_realm(get_test_hardware_lights())
        .await
        .expect("realm should be created");
    let light_proxy = LightRealm::connect_to_light_marker(&realm);

    // Light group only has one light, attempt to set two lights.
    let _ = light_proxy
        .set_light_group_values(
            LIGHT_NAME_1,
            &mut vec![
                LightState { value: Some(LightValue::Brightness(0.128)), ..LightState::EMPTY },
                LightState { value: Some(LightValue::Brightness(0.11)), ..LightState::EMPTY },
            ]
            .into_iter()
            .map(LightState::into),
        )
        .await
        .expect("set completed")
        .expect_err("expected error");
}

#[fuchsia::test]
async fn test_individual_light_group() {
    let light_groups = get_test_light_groups();
    let light_group_1 = light_groups.get(LIGHT_NAME_1).unwrap().clone();
    let mut light_group_1_updated = light_group_1.clone();
    light_group_1_updated.lights =
        Some(vec![LightState { value: Some(LightValue::Brightness(0.128)), ..LightState::EMPTY }]);

    let light_group_2 = light_groups.get(LIGHT_NAME_2).unwrap().clone();
    let mut light_group_2_updated = light_group_2.clone();
    light_group_2_updated.lights =
        Some(vec![LightState { value: Some(LightValue::On(false)), ..LightState::EMPTY }]);

    let realm = LightRealm::create_realm(get_test_hardware_lights())
        .await
        .expect("realm should be created");
    let light_proxy = LightRealm::connect_to_light_marker(&realm);

    // Ensure values from Watch matches set values.
    let settings = light_proxy.watch_light_group(LIGHT_NAME_1).await.expect("watch completed");
    assert_fidl_light_group_eq!(&settings, &light_group_1);
    let settings = light_proxy.watch_light_group(LIGHT_NAME_2).await.expect("watch completed");
    assert_fidl_light_group_eq!(&settings, &light_group_2);

    // Set updated values for the two lights.
    {
        let light_group_1_updated = light_group_1_updated.clone();
        light_proxy
            .set_light_group_values(
                light_group_1_updated.name.as_deref().unwrap(),
                &mut light_group_1_updated.lights.unwrap().into_iter(),
            )
            .await
            .expect("set called")
            .expect("set completed");
    }

    let settings = light_proxy.watch_light_group(LIGHT_NAME_1).await.expect("watch completed");
    assert_fidl_light_group_eq!(&settings, &light_group_1_updated);

    {
        let light_group_2_updated = light_group_2_updated.clone();
        light_proxy
            .set_light_group_values(
                light_group_2_updated.name.as_deref().unwrap(),
                &mut light_group_2_updated.lights.unwrap().into_iter(),
            )
            .await
            .expect("set called")
            .expect("set completed");
    }

    let settings = light_proxy.watch_light_group(LIGHT_NAME_2).await.expect("watch completed");
    assert_fidl_light_group_eq!(&settings, &light_group_2_updated);
}

#[fuchsia::test]
async fn test_watch_unknown_light_group_name() {
    let realm = LightRealm::create_realm(get_test_hardware_lights())
        .await
        .expect("realm should be created");
    let light_proxy = LightRealm::connect_to_light_marker(&realm);

    // Unknown name should be rejected.
    let _ = light_proxy.watch_light_group("unknown_name").await.expect_err("watch should fail");
}

#[fuchsia::test]
async fn test_set_unknown_light_group_name() {
    let realm = LightRealm::create_realm(get_test_hardware_lights())
        .await
        .expect("realm should be created");
    let light_proxy = LightRealm::connect_to_light_marker(&realm);

    let mut groups = get_test_light_groups();
    let lights = groups.remove(LIGHT_NAME_1).unwrap().lights.unwrap();

    // Unknown name should be rejected.
    let result = light_proxy
        .set_light_group_values("unknown_name", &mut lights.into_iter())
        .await
        .expect("set returns");
    assert_eq!(result, Err(LightError::InvalidName));
}

#[fuchsia::test]
async fn test_set_wrong_state_length() {
    let realm = LightRealm::create_realm(get_test_hardware_lights())
        .await
        .expect("realm should be created");
    let light_proxy = LightRealm::connect_to_light_marker(&realm);

    // Set with no light state should fail.
    let result = light_proxy
        .set_light_group_values(LIGHT_NAME_1, &mut vec![].into_iter())
        .await
        .expect("set returns");
    assert_eq!(result, Err(LightError::InvalidValue));

    // Set with an extra light state should fail.
    let extra_state = vec![
        fidl_fuchsia_settings::LightState {
            value: None,
            ..fidl_fuchsia_settings::LightState::EMPTY
        },
        fidl_fuchsia_settings::LightState {
            value: None,
            ..fidl_fuchsia_settings::LightState::EMPTY
        },
    ];
    let result = light_proxy
        .set_light_group_values(LIGHT_NAME_1, &mut extra_state.into_iter())
        .await
        .expect("set returns");
    assert_eq!(result, Err(LightError::InvalidValue));
}

#[fuchsia::test]
async fn test_set_wrong_value_type() {
    let realm = LightRealm::create_realm(get_test_hardware_lights())
        .await
        .expect("realm should be created");
    let light_proxy = LightRealm::connect_to_light_marker(&realm);

    // One of the light values is On instead of brightness, the set should fail.
    let new_state = vec![LightState { value: Some(LightValue::On(true)), ..LightState::EMPTY }];
    let result = light_proxy
        .set_light_group_values(LIGHT_NAME_1, &mut new_state.into_iter())
        .await
        .expect("set returns");
    assert_eq!(result, Err(LightError::InvalidValue));
}

#[fuchsia::test]
async fn test_set_invalid_rgb_values() {
    const TEST_LIGHT_NAME: &str = "light";
    const LIGHT_START_VAL: f32 = 0.25;
    const INVALID_VAL_1: f32 = 1.1;
    const INVALID_VAL_2: f32 = -0.1;
    let starting_light_info = vec![HardwareLight {
        name: TEST_LIGHT_NAME.to_owned(),
        value: LightValue::Color(ColorRgb {
            red: LIGHT_START_VAL,
            green: LIGHT_START_VAL,
            blue: LIGHT_START_VAL,
        }),
    }];

    let realm =
        LightRealm::create_realm(starting_light_info).await.expect("realm should be created");
    let light_proxy = LightRealm::connect_to_light_marker(&realm);

    // One of the RGB components is too big, the set should fail.
    let result = light_proxy
        .set_light_group_values(
            TEST_LIGHT_NAME,
            &mut vec![fidl_fuchsia_settings::LightState {
                value: Some(fidl_fuchsia_settings::LightValue::Color(ColorRgb {
                    red: LIGHT_START_VAL,
                    green: LIGHT_START_VAL,
                    blue: INVALID_VAL_1,
                })),
                ..fidl_fuchsia_settings::LightState::EMPTY
            }]
            .into_iter(),
        )
        .await
        .expect("set returns");
    assert_eq!(result, Err(LightError::InvalidValue));

    // One of the RGB components is negative, the set should fail.
    let result = light_proxy
        .set_light_group_values(
            TEST_LIGHT_NAME,
            &mut vec![fidl_fuchsia_settings::LightState {
                value: Some(fidl_fuchsia_settings::LightValue::Color(ColorRgb {
                    red: LIGHT_START_VAL,
                    green: INVALID_VAL_2,
                    blue: LIGHT_START_VAL,
                })),
                ..fidl_fuchsia_settings::LightState::EMPTY
            }]
            .into_iter(),
        )
        .await
        .expect("set returns");
    assert_eq!(result, Err(LightError::InvalidValue));
}
