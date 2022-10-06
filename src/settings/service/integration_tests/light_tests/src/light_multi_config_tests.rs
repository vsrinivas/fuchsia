// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_settings::{LightError, LightGroup, LightState, LightType, LightValue};
use fidl_fuchsia_ui_input::MediaButtonsEvent;
use fidl_fuchsia_ui_types::ColorRgb;
use futures::{channel::mpsc, StreamExt};
use light_realm::{assert_fidl_light_group_eq, assert_lights_eq, HardwareLight, LightRealm};
use std::collections::HashMap;
use test_case::test_case;

const LIGHT_NAME_1: &str = "LED1";
const LIGHT_NAME_2: &str = "LED2";
const LIGHT_NAME_3: &str = "LED3";
const LIGHT_VAL_1: f64 = 0.42;
const LIGHT_VAL_2: f64 = 0.84;
const RGB_VAL_R: f32 = 0.1;
const RGB_VAL_G: f32 = 0.2;
const RGB_VAL_B: f32 = 0.3;

// Ensure this lines up with the data in `get_test_light_groups`.
fn get_test_hardware_lights() -> Vec<HardwareLight> {
    vec![
        HardwareLight { name: LIGHT_NAME_1.to_owned(), value: LightValue::Brightness(LIGHT_VAL_1) },
        HardwareLight { name: LIGHT_NAME_1.to_owned(), value: LightValue::Brightness(LIGHT_VAL_2) },
        HardwareLight { name: LIGHT_NAME_2.to_owned(), value: LightValue::On(true) },
        HardwareLight {
            name: LIGHT_NAME_3.to_owned(),
            value: LightValue::Color(ColorRgb {
                red: RGB_VAL_R,
                green: RGB_VAL_G,
                blue: RGB_VAL_B,
            }),
        },
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
                lights: Some(vec![
                    LightState {
                        value: Some(LightValue::Brightness(LIGHT_VAL_1)),
                        ..LightState::EMPTY
                    },
                    LightState {
                        value: Some(LightValue::Brightness(LIGHT_VAL_2)),
                        ..LightState::EMPTY
                    },
                ]),
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
        (
            LIGHT_NAME_3.to_string(),
            LightGroup {
                name: Some(LIGHT_NAME_3.to_string()),
                enabled: Some(true),
                type_: Some(LightType::Rgb),
                lights: Some(vec![LightState {
                    value: Some(LightValue::Color(ColorRgb {
                        red: RGB_VAL_R,
                        green: RGB_VAL_G,
                        blue: RGB_VAL_B,
                    })),
                    ..LightState::EMPTY
                }]),
                ..LightGroup::EMPTY
            },
        ),
    ])
}

#[fuchsia::test]
async fn test_light_restore() {
    let (tx, mut rx) = mpsc::channel(0);
    let realm = LightRealm::create_realm_with_input_device_registry(get_test_hardware_lights(), tx)
        .await
        .expect("realm should be created");
    let light_proxy = LightRealm::connect_to_light_marker(&realm);

    // Wait for the listener to be registered by the settings service.
    let _ = rx.next().await.unwrap();

    let expected_light_info = get_test_light_groups();

    // Light controller will return values restored from the hardware service.
    let settings = light_proxy.watch_light_groups().await.expect("watch completed");
    assert_lights_eq!(settings, expected_light_info);

    let _ = realm.destroy().await;
}

// Tests that when a `LightHardwareConfiguration` is specified, that light groups configured with
// `DisableConditions::MicSwitch` have their enabled bits set to off when the mic is unmuted.
#[fuchsia::test]
async fn test_light_disabled_by_mic_mute_off() {
    let (tx, mut rx) = mpsc::channel(0);
    let realm = LightRealm::create_realm_with_input_device_registry(get_test_hardware_lights(), tx)
        .await
        .expect("realm should be created");
    let light_proxy = LightRealm::connect_to_light_marker(&realm);

    let light_group = get_test_light_groups().remove(LIGHT_NAME_1).unwrap();
    let expected_light_group = LightGroup { enabled: Some(false), ..light_group };

    // Wait for the listener to be registered by the settings service.
    let listener_proxy = rx.next().await.unwrap();

    // Send mic unmuted, which should disable the light.
    listener_proxy
        .on_event(MediaButtonsEvent { mic_mute: Some(false), ..MediaButtonsEvent::EMPTY })
        .await
        .expect("on event called");

    // Verify that the expected value is returned on a watch call.
    let settings: LightGroup =
        light_proxy.watch_light_group(LIGHT_NAME_1).await.expect("watch completed");
    assert_fidl_light_group_eq!(&expected_light_group, &settings);

    let _ = realm.destroy().await;
}

#[fuchsia::test]
async fn test_light_set_and_watch() {
    let (tx, mut rx) = mpsc::channel(0);
    let realm = LightRealm::create_realm_with_input_device_registry(get_test_hardware_lights(), tx)
        .await
        .expect("should create realm");
    let light_proxy = LightRealm::connect_to_light_marker(&realm);

    // Wait for the listener to be registered by the settings service.
    let _ = rx.next().await.unwrap();

    let mut expected_light_info = get_test_light_groups();
    let mut changed_light_group = expected_light_info[LIGHT_NAME_2].clone();
    let changed_light_state =
        LightState { value: Some(LightValue::On(false)), ..LightState::EMPTY };
    changed_light_group.lights = Some(vec![changed_light_state.clone()]);
    let _ = expected_light_info.insert(LIGHT_NAME_2.to_string(), changed_light_group);

    light_proxy
        .set_light_group_values(LIGHT_NAME_2, &mut [changed_light_state].into_iter())
        .await
        .expect("fidl failed")
        .expect("set failed");

    // Ensure value from Watch matches set value.
    let light_groups: Vec<LightGroup> =
        light_proxy.watch_light_groups().await.expect("watch completed");
    assert_lights_eq!(light_groups, expected_light_info);

    let _ = realm.destroy().await;
}

#[fuchsia::test]
async fn test_light_set_wrong_size() {
    let (tx, mut rx) = mpsc::channel(0);
    let realm = LightRealm::create_realm_with_input_device_registry(get_test_hardware_lights(), tx)
        .await
        .expect("should create realm");
    let light_proxy = LightRealm::connect_to_light_marker(&realm);

    // Wait for the listener to be registered by the settings service.
    let _ = rx.next().await.unwrap();

    // Light group has two lights, attempt to set one light.
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

    let _ = realm.destroy().await;
}

// Tests that when there are multiple lights, one light can be set at a time.
#[fuchsia::test]
async fn test_light_set_single_light() {
    const LIGHT_2_CHANGED_VAL: f64 = 0.11;

    // When changing the light group, specify None for the first light.
    let changed_lights = vec![
        LightState { value: None, ..LightState::EMPTY },
        LightState {
            value: Some(LightValue::Brightness(LIGHT_2_CHANGED_VAL)),
            ..LightState::EMPTY
        },
    ];

    // Only the second light should change.
    let expected_light_group = LightGroup {
        lights: Some(vec![
            LightState { value: Some(LightValue::Brightness(LIGHT_VAL_1)), ..LightState::EMPTY },
            LightState {
                value: Some(LightValue::Brightness(LIGHT_2_CHANGED_VAL)),
                ..LightState::EMPTY
            },
        ]),
        ..get_test_light_groups()[LIGHT_NAME_1].clone()
    };

    let (tx, mut rx) = mpsc::channel(0);
    let realm = LightRealm::create_realm_with_input_device_registry(get_test_hardware_lights(), tx)
        .await
        .expect("should create realm");
    let light_proxy = LightRealm::connect_to_light_marker(&realm);

    // Wait for the listener to be registered by the settings service.
    let _ = rx.next().await.unwrap();

    light_proxy
        .set_light_group_values(LIGHT_NAME_1, &mut changed_lights.into_iter())
        .await
        .expect("set called")
        .expect("set succeeded");

    // Ensure value from Watch matches set value.
    let mut settings: Vec<LightGroup> =
        light_proxy.watch_light_groups().await.expect("watch completed");
    settings.sort_by_key(|group: &LightGroup| group.name.clone());

    let settings = light_proxy.watch_light_group(LIGHT_NAME_1).await.expect("watch completed");
    assert_eq!(settings, expected_light_group);

    let _ = realm.destroy().await;
}

#[fuchsia::test]
async fn test_individual_light_group() {
    let mut groups = get_test_light_groups();
    let light_group_1 = groups.remove(LIGHT_NAME_1).unwrap();
    let light_group_1_updated_lights = vec![
        LightState { value: Some(LightValue::Brightness(0.128)), ..LightState::EMPTY },
        LightState { value: Some(LightValue::Brightness(0.128)), ..LightState::EMPTY },
    ];
    let light_group_1_updated =
        LightGroup { lights: Some(light_group_1_updated_lights.clone()), ..light_group_1.clone() };

    let light_group_2 = groups.remove(LIGHT_NAME_2).unwrap();
    let light_group_2_updated_lights =
        vec![LightState { value: Some(LightValue::On(false)), ..LightState::EMPTY }];
    let light_group_2_updated =
        LightGroup { lights: Some(light_group_2_updated_lights.clone()), ..light_group_2.clone() };

    let light_group_3 = groups.remove(LIGHT_NAME_3).unwrap();
    let light_group_3_updated_lights = vec![LightState {
        value: Some(LightValue::Color(ColorRgb { red: 0.3, green: 0.4, blue: 0.5 })),
        ..LightState::EMPTY
    }];
    let light_group_3_updated =
        LightGroup { lights: Some(light_group_3_updated_lights.clone()), ..light_group_3.clone() };

    let (tx, mut rx) = mpsc::channel(0);
    let realm = LightRealm::create_realm_with_input_device_registry(get_test_hardware_lights(), tx)
        .await
        .expect("should create realm");
    let light_proxy = LightRealm::connect_to_light_marker(&realm);

    // Wait for the listener to be registered by the settings service.
    let _ = rx.next().await.unwrap();

    // Ensure values from Watch matches set values.
    let settings = light_proxy.watch_light_group(LIGHT_NAME_1).await.expect("watch completed");
    assert_fidl_light_group_eq!(&settings, &light_group_1);
    let settings = light_proxy.watch_light_group(LIGHT_NAME_2).await.expect("watch completed");
    assert_fidl_light_group_eq!(&settings, &light_group_2);
    let settings = light_proxy.watch_light_group(LIGHT_NAME_3).await.expect("watch completed");
    assert_fidl_light_group_eq!(&settings, &light_group_3);

    // Set updated values for the two lights.
    light_proxy
        .set_light_group_values(LIGHT_NAME_1, &mut light_group_1_updated_lights.into_iter())
        .await
        .expect("set completed")
        .expect("set succeeded");

    let settings = light_proxy.watch_light_group(LIGHT_NAME_1).await.expect("watch completed");
    assert_fidl_light_group_eq!(&settings, &light_group_1_updated);

    light_proxy
        .set_light_group_values(LIGHT_NAME_2, &mut light_group_2_updated_lights.into_iter())
        .await
        .expect("set completed")
        .expect("set succeeded");

    let settings = light_proxy.watch_light_group(LIGHT_NAME_2).await.expect("watch completed");
    assert_fidl_light_group_eq!(&settings, &light_group_2_updated);

    light_proxy
        .set_light_group_values(LIGHT_NAME_3, &mut light_group_3_updated_lights.into_iter())
        .await
        .expect("set completed")
        .expect("set succeeded");

    let settings = light_proxy.watch_light_group(LIGHT_NAME_3).await.expect("watch completed");
    assert_fidl_light_group_eq!(&settings, &light_group_3_updated);

    let _ = realm.destroy().await;
}

#[fuchsia::test]
async fn test_watch_unknown_light_group_name() {
    let (tx, mut rx) = mpsc::channel(0);
    let realm = LightRealm::create_realm_with_input_device_registry(get_test_hardware_lights(), tx)
        .await
        .expect("realm should be created");
    let light_proxy = LightRealm::connect_to_light_marker(&realm);

    // Wait for the listener to be registered by the settings service.
    let _ = rx.next().await.unwrap();

    // Unknown name should be rejected.
    let _ = light_proxy.watch_light_group("unknown_name").await.expect_err("watch should fail");

    let _ = realm.destroy().await;
}

#[fuchsia::test]
async fn test_set_unknown_light_group_name() {
    let (tx, mut rx) = mpsc::channel(0);
    let realm = LightRealm::create_realm_with_input_device_registry(get_test_hardware_lights(), tx)
        .await
        .expect("realm should be created");
    let light_proxy = LightRealm::connect_to_light_marker(&realm);

    // Wait for the listener to be registered by the settings service.
    let _ = rx.next().await.unwrap();

    let mut groups = get_test_light_groups();
    let lights = groups.remove(LIGHT_NAME_1).unwrap().lights.unwrap();

    // Unknown name should be rejected.
    let result = light_proxy
        .set_light_group_values("unknown_name", &mut lights.into_iter())
        .await
        .expect("set returns");
    assert_eq!(result, Err(LightError::InvalidName));

    let _ = realm.destroy().await;
}

#[fuchsia::test]
async fn test_set_wrong_state_length() {
    let (tx, mut rx) = mpsc::channel(0);
    let realm = LightRealm::create_realm_with_input_device_registry(get_test_hardware_lights(), tx)
        .await
        .expect("realm should be created");
    let light_proxy = LightRealm::connect_to_light_marker(&realm);

    // Wait for the listener to be registered by the settings service.
    let _ = rx.next().await.unwrap();

    // Set with no light state should fail.
    let result = light_proxy
        .set_light_group_values(LIGHT_NAME_1, &mut vec![].into_iter())
        .await
        .expect("set returns");
    assert_eq!(result, Err(LightError::InvalidValue));

    // Set with an extra light state should fail.
    let extra_state = vec![fidl_fuchsia_settings::LightState {
        value: None,
        ..fidl_fuchsia_settings::LightState::EMPTY
    }];
    let result = light_proxy
        .set_light_group_values(LIGHT_NAME_1, &mut extra_state.into_iter())
        .await
        .expect("set returns");
    assert_eq!(result, Err(LightError::InvalidValue));

    let _ = realm.destroy().await;
}

#[fuchsia::test]
async fn test_set_wrong_value_type() {
    let (tx, mut rx) = mpsc::channel(0);
    let realm = LightRealm::create_realm_with_input_device_registry(get_test_hardware_lights(), tx)
        .await
        .expect("realm should be created");
    let light_proxy = LightRealm::connect_to_light_marker(&realm);

    // Wait for the listener to be registered by the settings service.
    let _ = rx.next().await.unwrap();

    // One of the light values is On instead of brightness, the set should fail.
    let new_state = vec![LightState { value: Some(LightValue::On(true)), ..LightState::EMPTY }];
    let result = light_proxy
        .set_light_group_values(LIGHT_NAME_1, &mut new_state.into_iter())
        .await
        .expect("set returns");
    assert_eq!(result, Err(LightError::InvalidValue));

    let _ = realm.destroy().await;
}

#[test_case(ColorRgb { red: 0.1, green: 0.1, blue: 1.0 + std::f32::EPSILON})]
#[test_case(ColorRgb { red: 0.1, green: 0.1, blue: 0.0 - std::f32::EPSILON})]
#[test_case(ColorRgb { red: 0.1, green: 1.0 + std::f32::EPSILON, blue: 0.1})]
#[test_case(ColorRgb { red: 0.1, green: 0.0 - std::f32::EPSILON, blue: 0.1})]
#[test_case(ColorRgb { red: 1.0 + std::f32::EPSILON, green: 0.1, blue: 0.1})]
#[test_case(ColorRgb { red: 0.0 - std::f32::EPSILON, green: 0.1, blue: 0.1})]
#[fuchsia::test]
async fn test_set_invalid_rgb_values(invalid_rgb: ColorRgb) {
    let (tx, mut rx) = mpsc::channel(0);
    let realm = LightRealm::create_realm_with_input_device_registry(get_test_hardware_lights(), tx)
        .await
        .expect("realm should be created");
    let light_proxy = LightRealm::connect_to_light_marker(&realm);

    // Wait for the listener to be registered by the settings service.
    let _ = rx.next().await.unwrap();

    let result = light_proxy
        .set_light_group_values(
            LIGHT_NAME_3,
            &mut vec![LightState {
                value: Some(LightValue::Color(invalid_rgb)),
                ..fidl_fuchsia_settings::LightState::EMPTY
            }]
            .into_iter(),
        )
        .await
        .expect("set returns");
    assert_eq!(result, Err(LightError::InvalidValue));

    let _ = realm.destroy().await;
}
