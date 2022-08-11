// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use common::{DisplayTest, Request};
use fidl_fuchsia_settings::{DisplaySettings, LowLightMode as FidlLowLightMode};
use futures::StreamExt;
use std::sync::atomic::Ordering;
use std::sync::Arc;

const STARTING_BRIGHTNESS: f32 = 0.5;
const CHANGED_BRIGHTNESS: f32 = 0.8;
const AUTO_BRIGHTNESS_LEVEL: f32 = 0.9;

// Tests that the FIDL calls for manual brightness result in appropriate
// commands sent to the service.
#[fuchsia::test]
// Comparisons are just checking that set values are returned the same.
#[allow(clippy::float_cmp)]
async fn test_manual_brightness_with_brightness_controller() {
    let manual_brightness = DisplayTest::get_init_manual_brightness();
    // Initialize channel with buffer of 0 so that the senders can only send one item at a time.
    let (requests_sender, mut requests_receiver) = futures::channel::mpsc::channel::<Request>(0);
    let instance = DisplayTest::create_realm_with_brightness_controller(
        Arc::clone(&manual_brightness),
        Arc::clone(&DisplayTest::get_init_auto_brightness()),
        Arc::clone(&DisplayTest::get_init_num_changes()),
        requests_sender,
    )
    .await
    .expect("setting up test realm");

    let proxy = DisplayTest::connect_to_displaymarker(&instance);
    // On service starts, a set manual brightness call will be made to set the initial value.
    assert_eq!(Some(Request::SetManualBrightness), requests_receiver.next().await);

    // Make a watch call.
    let settings = proxy.watch().await.expect("watch completed");
    assert_eq!(settings.brightness_value, Some(STARTING_BRIGHTNESS));

    // Set manual brightness value.
    let mut display_settings = DisplaySettings::EMPTY;
    display_settings.brightness_value = Some(CHANGED_BRIGHTNESS);
    proxy.set(display_settings).await.expect("set completed").expect("set successful");

    // Verify the brightness changes as expected.
    let settings = proxy.watch().await.expect("watch completed");
    assert_eq!(settings.brightness_value, Some(CHANGED_BRIGHTNESS));

    // Verify that the mock brightness service finishes processing requests.
    assert_eq!(Some(Request::SetManualBrightness), requests_receiver.next().await);

    // Verify changes reached the brightness dependency.
    assert_eq!(manual_brightness.lock().await.expect("brightness not yet set"), CHANGED_BRIGHTNESS);

    let _ = instance.destroy().await;
}

// Tests that the FIDL calls for auto brightness result in appropriate
// commands sent to the service.
#[fuchsia::test]
async fn test_auto_brightness_with_brightness_controller() {
    // Initialize channel with buffer of 0 so that the senders can only send one item at a time.
    let (requests_sender, mut requests_receiver) = futures::channel::mpsc::channel::<Request>(0);
    let auto_brightness = DisplayTest::get_init_auto_brightness();
    let instance = DisplayTest::create_realm_with_brightness_controller(
        Arc::clone(&DisplayTest::get_init_manual_brightness()),
        Arc::clone(&auto_brightness),
        Arc::clone(&DisplayTest::get_init_num_changes()),
        requests_sender,
    )
    .await
    .expect("setting up test realm");

    let proxy = DisplayTest::connect_to_displaymarker(&instance);
    // On service starts, a set manual brightness call will be made to set the initial value.
    assert_eq!(Some(Request::SetManualBrightness), requests_receiver.next().await);

    // Make a set call.
    let mut display_settings = DisplaySettings::EMPTY;
    display_settings.auto_brightness = Some(true);
    display_settings.adjusted_auto_brightness = Some(AUTO_BRIGHTNESS_LEVEL);
    proxy.set(display_settings).await.expect("set completed").expect("set successful");

    // Verify changes as expected.
    let settings = proxy.watch().await.expect("watch completed");
    assert_eq!(settings.auto_brightness, Some(true));
    assert_eq!(settings.adjusted_auto_brightness, Some(AUTO_BRIGHTNESS_LEVEL));

    // Verify that the mock brightness service finishes processing requests.
    assert_eq!(Some(Request::SetAutoBrightness), requests_receiver.next().await);

    // Verify the changes reached the brightness dependency.
    assert!(auto_brightness.lock().await.expect("get successful"));

    let _ = instance.destroy().await;
}

// Tests that the FIDL calls for light mode result in appropriate
// commands sent to the service.
#[fuchsia::test]
async fn test_light_mode_with_brightness_controller() {
    // Initialize channel with buffer of 0 so that the senders can only send one item at a time.
    let (requests_sender, mut requests_receiver) = futures::channel::mpsc::channel::<Request>(0);
    let instance = DisplayTest::create_realm_with_brightness_controller(
        Arc::clone(&DisplayTest::get_init_manual_brightness()),
        Arc::clone(&DisplayTest::get_init_auto_brightness()),
        Arc::clone(&DisplayTest::get_init_num_changes()),
        requests_sender,
    )
    .await
    .expect("setting up test realm");

    let proxy = DisplayTest::connect_to_displaymarker(&instance);
    // On service starts, a set manual brightness call will be made to set the initial value.
    assert_eq!(Some(Request::SetManualBrightness), requests_receiver.next().await);

    // Test that if display is enabled, it is reflected.
    let mut display_settings = DisplaySettings::EMPTY;
    display_settings.low_light_mode = Some(FidlLowLightMode::Enable);
    proxy.set(display_settings).await.expect("set completed").expect("set successful");

    let settings = proxy.watch().await.expect("watch completed");

    assert_eq!(settings.low_light_mode, Some(FidlLowLightMode::Enable));

    // Test that if display is disabled, it is reflected.
    let mut display_settings = DisplaySettings::EMPTY;
    display_settings.low_light_mode = Some(FidlLowLightMode::Disable);
    proxy.set(display_settings).await.expect("set completed").expect("set successful");

    let settings = proxy.watch().await.expect("watch completed");

    assert_eq!(settings.low_light_mode, Some(FidlLowLightMode::Disable));

    // Test that if display is disabled immediately, it is reflected.
    let mut display_settings = DisplaySettings::EMPTY;
    display_settings.low_light_mode = Some(FidlLowLightMode::DisableImmediately);
    proxy.set(display_settings).await.expect("set completed").expect("set successful");

    let settings = proxy.watch().await.expect("watch completed");

    assert_eq!(settings.low_light_mode, Some(FidlLowLightMode::DisableImmediately));

    let _ = instance.destroy().await;
}

// Tests that calls to the external brightness component are only made when
// the brightness changes.
#[fuchsia::test]
async fn test_deduped_external_brightness_calls() {
    // Initialize channel with buffer of 0 so that the senders can only send one item at a time.
    let (requests_sender, mut requests_receiver) = futures::channel::mpsc::channel::<Request>(0);
    let num_changes = DisplayTest::get_init_num_changes();
    let instance = DisplayTest::create_realm_with_brightness_controller(
        Arc::clone(&DisplayTest::get_init_manual_brightness()),
        Arc::clone(&DisplayTest::get_init_auto_brightness()),
        Arc::clone(&num_changes),
        requests_sender,
    )
    .await
    .expect("setting up test realm");

    let proxy = DisplayTest::connect_to_displaymarker(&instance);
    // On service starts, a set manual brightness call will be made to set the initial value.
    assert_eq!(Some(Request::SetManualBrightness), requests_receiver.next().await);

    let settings = proxy.watch().await.expect("watch completed");
    assert_eq!(settings.brightness_value, Some(STARTING_BRIGHTNESS));

    // Make two same set calls and verify no duplicated calls to the external brightness service.
    let mut display_settings = DisplaySettings::EMPTY;
    display_settings.brightness_value = Some(STARTING_BRIGHTNESS);
    proxy.set(display_settings.clone()).await.expect("set completed").expect("set successful");
    let init_num = num_changes.load(Ordering::Relaxed);
    proxy.set(display_settings).await.expect("set completed").expect("set successful");
    assert_eq!(num_changes.load(Ordering::Relaxed), init_num);

    let mut display_settings_changed = DisplaySettings::EMPTY;
    display_settings_changed.brightness_value = Some(CHANGED_BRIGHTNESS);
    proxy.set(display_settings_changed).await.expect("set completed").expect("set successful");
    // Verify that the mock brightness service finishes processing requests.
    assert_eq!(Some(Request::SetManualBrightness), requests_receiver.next().await);
    assert_eq!(num_changes.load(Ordering::Relaxed), init_num + 1);

    let _ = instance.destroy().await;
}

// Tests that the FIDL calls for screen enabled result in appropriate
// commands sent to the service.
#[fuchsia::test]
async fn test_screen_enabled_with_brightness_controller() {
    // Initialize channel with buffer of 0 so that the senders can only send one item at a time.
    let (requests_sender, mut requests_receiver) = futures::channel::mpsc::channel::<Request>(0);
    let instance = DisplayTest::create_realm_with_brightness_controller(
        Arc::clone(&DisplayTest::get_init_manual_brightness()),
        Arc::clone(&DisplayTest::get_init_auto_brightness()),
        Arc::clone(&DisplayTest::get_init_num_changes()),
        requests_sender,
    )
    .await
    .expect("setting up test realm");

    let proxy = DisplayTest::connect_to_displaymarker(&instance);
    // On service starts, a set manual brightness call will be made to set the initial value.
    assert_eq!(Some(Request::SetManualBrightness), requests_receiver.next().await);

    DisplayTest::test_screen_enabled(proxy).await;

    let _ = instance.destroy().await;
}
