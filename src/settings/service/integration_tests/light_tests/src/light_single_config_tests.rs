// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fasync::DurationExt;
use fidl_fuchsia_settings::{LightError, LightGroup, LightState, LightType, LightValue};
use fidl_fuchsia_ui_input::MediaButtonsEvent;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::{channel::mpsc, StreamExt};
use light_realm::{assert_fidl_light_group_eq, assert_lights_eq, HardwareLight, LightRealm};
use std::collections::HashMap;

const LIGHT_NAME_1: &str = "LED";
const LIGHT_VAL: f64 = 0.42;
// TODO(fxbug.dev/107545) Remove once test is converted to unit test.
// One more second that the setup proxy teardown timeout. This should ensure the controllers are
// torn down by the time the next call is made.
const TEARDOWN_TIMEOUT: i64 = 6;

// Ensure this lines up with the data in `get_test_light_groups`.
fn get_test_hardware_lights() -> Vec<HardwareLight> {
    vec![HardwareLight { name: LIGHT_NAME_1.to_owned(), value: LightValue::Brightness(LIGHT_VAL) }]
}

fn get_test_light_groups() -> HashMap<String, LightGroup> {
    HashMap::from([(
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
    )])
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

// Tests that when a `LightHardwareConfiguration` is specified, light groups configured with
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

// TODO(fxbug.dev/107545) Remove once test is converted to unit test.
// Tests that mic mute still works even after the setup proxy tears down the controller in between
// calls.
#[fuchsia::test]
async fn test_mic_mute_can_restore_after_timeout() {
    let (tx, mut rx) = mpsc::channel(0);
    let realm = LightRealm::create_realm_with_input_device_registry(get_test_hardware_lights(), tx)
        .await
        .expect("realm should be created");
    let light_proxy = LightRealm::connect_to_light_marker(&realm);

    let light_group = get_test_light_groups().remove(LIGHT_NAME_1).unwrap();
    let expected_light_group = LightGroup { enabled: Some(false), ..light_group.clone() };

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

    // Wait 6 seconds, which should trigger cleanup of the controller.
    fasync::Timer::new(zx::Duration::from_seconds(TEARDOWN_TIMEOUT).after_now()).await;

    // Re-enable the light.
    listener_proxy
        .on_event(MediaButtonsEvent { mic_mute: Some(true), ..MediaButtonsEvent::EMPTY })
        .await
        .expect("on event called");

    // Verify that the expected value is returned on a watch call.
    let settings: LightGroup =
        light_proxy.watch_light_group(LIGHT_NAME_1).await.expect("watch completed");
    let expected_light_group = LightGroup { enabled: Some(true), ..light_group };
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
    let mut changed_light_group = expected_light_info[LIGHT_NAME_1].clone();
    let changed_light_state =
        LightState { value: Some(LightValue::Brightness(0.128)), ..LightState::EMPTY };
    changed_light_group.lights = Some(vec![changed_light_state.clone()]);
    let _ = expected_light_info.insert(LIGHT_NAME_1.to_string(), changed_light_group);

    light_proxy
        .set_light_group_values(LIGHT_NAME_1, &mut [changed_light_state].into_iter())
        .await
        .expect("fidl failed")
        .expect("set failed");

    // Ensure value from Watch matches set value.
    let light_groups: Vec<LightGroup> =
        light_proxy.watch_light_groups().await.expect("watch completed");
    assert_lights_eq!(light_groups, expected_light_info);

    let _ = realm.destroy().await;
}

// TODO(fxbug.dev/107545) Remove once test is converted to unit test.
#[fuchsia::test]
async fn test_light_set_restore_after_timeout() {
    let (tx, mut rx) = mpsc::channel(0);
    let realm = LightRealm::create_realm_with_input_device_registry(get_test_hardware_lights(), tx)
        .await
        .expect("should create realm");
    let light_proxy = LightRealm::connect_to_light_marker(&realm);

    // Wait for the listener to be registered by the settings service.
    let _ = rx.next().await.unwrap();

    let mut expected_light_info = get_test_light_groups();
    let mut changed_light_group = expected_light_info[LIGHT_NAME_1].clone();
    let changed_light_state =
        LightState { value: Some(LightValue::Brightness(0.128)), ..LightState::EMPTY };
    changed_light_group.lights = Some(vec![changed_light_state.clone()]);
    let _ = expected_light_info.insert(LIGHT_NAME_1.to_string(), changed_light_group.clone());

    light_proxy
        .set_light_group_values(LIGHT_NAME_1, &mut [changed_light_state].into_iter())
        .await
        .expect("fidl failed")
        .expect("set failed");

    // Ensure value from Watch matches set value.
    let light_groups: Vec<LightGroup> =
        light_proxy.watch_light_groups().await.expect("watch completed");
    assert_lights_eq!(light_groups, expected_light_info.clone());

    // Wait 6 seconds, which should trigger cleanup of the controller.
    fasync::Timer::new(zx::Duration::from_seconds(TEARDOWN_TIMEOUT).after_now()).await;

    // Call set again.
    let changed_light_state =
        LightState { value: Some(LightValue::Brightness(0.256)), ..LightState::EMPTY };
    changed_light_group.lights = Some(vec![changed_light_state.clone()]);
    let _ = expected_light_info.insert(LIGHT_NAME_1.to_string(), changed_light_group);

    light_proxy
        .set_light_group_values(LIGHT_NAME_1, &mut [changed_light_state].into_iter())
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
        .expect("realm should be created");
    let light_proxy = LightRealm::connect_to_light_marker(&realm);

    // Wait for the listener to be registered by the settings service.
    let _ = rx.next().await.unwrap();

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
