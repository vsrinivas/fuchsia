// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.&

use crate::common::{DisplayTest, Mocks};
use anyhow::Error;
use async_trait::async_trait;
use fidl_fuchsia_settings::{DisplaySettings, LowLightMode as FidlLowLightMode};
use fidl_fuchsia_ui_brightness::{ControlRequest, ControlRequestStream};
use fuchsia_async as fasync;
use fuchsia_component::server::{ServiceFs, ServiceFsDir};
use fuchsia_component_test::LocalComponentHandles;
use futures::lock::Mutex;
use futures::{StreamExt, TryStreamExt};
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::Arc;
mod common;

#[async_trait]
impl Mocks for DisplayTest {
    // Mock the brightness dependency and verify the settings service interacts with the brightness
    // service by checking input value changes through the various requests.
    async fn brightness_service_impl(
        handles: LocalComponentHandles,
        manual_brightness: Arc<Mutex<Option<f32>>>,
        auto_brightness: Arc<Mutex<Option<bool>>>,
        num_changes: Arc<AtomicU32>,
    ) -> Result<(), Error> {
        let mut fs = ServiceFs::new();
        let _: &mut ServiceFsDir<'_, _> =
            fs.dir("svc").add_fidl_service(move |mut stream: ControlRequestStream| {
                let auto_brightness_handle = auto_brightness.clone();
                let brightness_handle = manual_brightness.clone();
                let num_changes_handle = num_changes.clone();
                fasync::Task::spawn(async move {
                    while let Ok(Some(req)) = stream.try_next().await {
                        // Support future expansion of FIDL.
                        #[allow(unreachable_patterns)]
                        match req {
                            ControlRequest::WatchCurrentBrightness { responder } => {
                                responder
                                    .send(
                                        brightness_handle
                                            .lock()
                                            .await
                                            .expect("brightness not yet set"),
                                    )
                                    .unwrap();
                            }
                            ControlRequest::SetAutoBrightness { control_handle: _ } => {
                                *auto_brightness_handle.lock().await = Some(true);
                                let current_num = num_changes_handle.load(Ordering::Relaxed);
                                (*num_changes_handle).store(current_num + 1, Ordering::Relaxed);
                            }
                            ControlRequest::SetManualBrightness { value, control_handle: _ } => {
                                *brightness_handle.lock().await = Some(value);
                                let current_num = num_changes_handle.load(Ordering::Relaxed);
                                (*num_changes_handle).store(current_num + 1, Ordering::Relaxed);
                            }
                            ControlRequest::WatchAutoBrightness { responder } => {
                                responder
                                    .send(auto_brightness_handle.lock().await.unwrap_or(false))
                                    .unwrap();
                            }
                            _ => {}
                        }
                    }
                })
                .detach();
            });
        let _: &mut ServiceFs<_> =
            fs.serve_connection(handles.outgoing_dir.into_channel()).unwrap();
        fs.collect::<()>().await;
        Ok(())
    }
}

const STARTING_BRIGHTNESS: f32 = 0.5;
const CHANGED_BRIGHTNESS: f32 = 0.8;
const AUTO_BRIGHTNESS_LEVEL: f32 = 0.9;

// Tests that the FIDL calls for manual brightness result in appropriate
// commands sent to the service.
#[fuchsia_async::run_singlethreaded(test)]
// Comparisons are just checking that set values are returned the same.
#[allow(clippy::float_cmp)]
async fn test_manual_brightness_with_brightness_controller() {
    let manual_brightness = DisplayTest::get_init_manual_brightness();
    let instance = DisplayTest::create_realm_with_brightness_controller(
        Arc::clone(&manual_brightness),
        Arc::clone(&DisplayTest::get_init_auto_brightness()),
        Arc::clone(&DisplayTest::get_init_num_changes()),
    )
    .await
    .expect("setting up test realm");

    let proxy = DisplayTest::connect_to_displaymarker(&instance);

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

    // Verify changes reached the brightness dependency.
    assert_eq!(manual_brightness.lock().await.expect("brightness not yet set"), CHANGED_BRIGHTNESS);

    let _ = instance.destroy().await;
}

// Tests that the FIDL calls for auto brightness result in appropriate
// commands sent to the service.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_auto_brightness_with_brightness_controller() {
    let auto_brightness = DisplayTest::get_init_auto_brightness();
    let instance = DisplayTest::create_realm_with_brightness_controller(
        Arc::clone(&DisplayTest::get_init_manual_brightness()),
        Arc::clone(&auto_brightness),
        Arc::clone(&DisplayTest::get_init_num_changes()),
    )
    .await
    .expect("setting up test realm");

    let proxy = DisplayTest::connect_to_displaymarker(&instance);

    // Make a set call.
    let mut display_settings = DisplaySettings::EMPTY;
    display_settings.auto_brightness = Some(true);
    display_settings.adjusted_auto_brightness = Some(AUTO_BRIGHTNESS_LEVEL);
    proxy.set(display_settings).await.expect("set completed").expect("set successful");

    // Verify changes as expected.
    let settings = proxy.watch().await.expect("watch completed");
    assert_eq!(settings.auto_brightness, Some(true));
    assert_eq!(settings.adjusted_auto_brightness, Some(AUTO_BRIGHTNESS_LEVEL));

    // Verify the changes reached the brightness dependency.
    assert!(auto_brightness.lock().await.expect("get successful"));

    let _ = instance.destroy().await;
}

// Tests that the FIDL calls for light mode result in appropriate
// commands sent to the service.
#[fuchsia::test]
async fn test_light_mode_with_brightness_controller() {
    let instance = DisplayTest::create_realm_with_brightness_controller(
        Arc::clone(&DisplayTest::get_init_manual_brightness()),
        Arc::clone(&DisplayTest::get_init_auto_brightness()),
        Arc::clone(&DisplayTest::get_init_num_changes()),
    )
    .await
    .expect("setting up test realm");

    let proxy = DisplayTest::connect_to_displaymarker(&instance);

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
#[fuchsia_async::run_singlethreaded(test)]
async fn test_deduped_external_brightness_calls() {
    let num_changes = DisplayTest::get_init_num_changes();
    let instance = DisplayTest::create_realm_with_brightness_controller(
        Arc::clone(&DisplayTest::get_init_manual_brightness()),
        Arc::clone(&DisplayTest::get_init_auto_brightness()),
        Arc::clone(&num_changes),
    )
    .await
    .expect("setting up test realm");

    let proxy = DisplayTest::connect_to_displaymarker(&instance);

    let settings = proxy.watch().await.expect("watch completed");
    assert_eq!(settings.brightness_value, Some(STARTING_BRIGHTNESS));
    let init_num = num_changes.load(Ordering::Relaxed);
    println!("{}", init_num);

    let mut display_settings = DisplaySettings::EMPTY;
    display_settings.brightness_value = Some(STARTING_BRIGHTNESS);
    proxy.set(display_settings).await.expect("set completed").expect("set successful");
    assert_eq!(num_changes.load(Ordering::Relaxed), init_num);

    let mut display_settings_changed = DisplaySettings::EMPTY;
    display_settings_changed.brightness_value = Some(CHANGED_BRIGHTNESS);
    proxy.set(display_settings_changed).await.expect("set completed").expect("set successful");
    assert_eq!(num_changes.load(Ordering::Relaxed), init_num + 1);

    let _ = instance.destroy().await;
}

// Tests that the FIDL calls for screen enabled result in appropriate
// commands sent to the service.
#[fuchsia::test]
async fn test_screen_enabled_with_brightness_controller() {
    let instance = DisplayTest::create_realm_with_brightness_controller(
        Arc::clone(&DisplayTest::get_init_manual_brightness()),
        Arc::clone(&DisplayTest::get_init_auto_brightness()),
        Arc::clone(&DisplayTest::get_init_num_changes()),
    )
    .await
    .expect("setting up test realm");

    let proxy = DisplayTest::connect_to_displaymarker(&instance);

    // Test that if screen is turned off, it is reflected.
    let mut display_settings = DisplaySettings::EMPTY;
    display_settings.auto_brightness = Some(false);
    proxy.set(display_settings).await.expect("set completed").expect("set successful");

    let mut display_settings = DisplaySettings::EMPTY;
    display_settings.screen_enabled = Some(false);
    proxy.set(display_settings).await.expect("set completed").expect("set successful");

    let settings = proxy.watch().await.expect("watch completed");

    assert_eq!(settings.screen_enabled, Some(false));

    // Test that if display is turned back on, the display and manual brightness are on.
    let mut display_settings = DisplaySettings::EMPTY;
    display_settings.screen_enabled = Some(true);
    proxy.set(display_settings).await.expect("set completed").expect("set successful");

    let settings = proxy.watch().await.expect("watch completed");

    assert_eq!(settings.screen_enabled, Some(true));
    assert_eq!(settings.auto_brightness, Some(false));

    // Test that if auto brightness is turned on, the display and auto brightness are on.
    let mut display_settings = DisplaySettings::EMPTY;
    display_settings.auto_brightness = Some(true);
    proxy.set(display_settings).await.expect("set completed").expect("set successful");

    let settings = proxy.watch().await.expect("watch completed");

    assert_eq!(settings.auto_brightness, Some(true));
    assert_eq!(settings.screen_enabled, Some(true));

    let _ = instance.destroy().await;
}
