// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::Services;
use crate::ENV_NAME;
use anyhow::{Context as _, Error};
use fidl_fuchsia_settings::{
    DisplayMarker, DisplayRequest, DisplaySettings, LightSensorData, LowLightMode, Theme, ThemeType,
};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;
use setui_client_lib::display;

use parking_lot::RwLock;
use std::sync::Arc;

// Can only check one mutate option at once.
pub(crate) async fn validate_display(
    expected_brightness: Option<f32>,
    expected_auto_brightness: Option<bool>,
    expected_auto_brightness_value: Option<f32>,
    expected_low_light_mode: Option<LowLightMode>,
    expected_theme_type: Option<ThemeType>,
    expected_screen_enabled: Option<bool>,
) -> Result<(), Error> {
    let env = create_service!(
        Services::Display, DisplayRequest::Set { settings, responder, } => {
            if let (Some(brightness_value), Some(expected_brightness_value)) =
              (settings.brightness_value, expected_brightness) {
                assert_eq!(brightness_value, expected_brightness_value);
                responder.send(&mut Ok(()))?;
            } else if let (Some(auto_brightness), Some(expected_auto_brightness_value)) =
              (settings.auto_brightness, expected_auto_brightness) {
                assert_eq!(auto_brightness, expected_auto_brightness_value);
                responder.send(&mut Ok(()))?;
            } else if let (Some(auto_brightness_value), Some(expected_auto_brightness_value)) =
              (settings.adjusted_auto_brightness, expected_auto_brightness_value) {
                assert_eq!(auto_brightness_value, expected_auto_brightness_value);
                responder.send(&mut Ok(()))?;
            } else if let (Some(low_light_mode), Some(expected_low_light_mode_value)) =
              (settings.low_light_mode, expected_low_light_mode) {
                assert_eq!(low_light_mode, expected_low_light_mode_value);
                responder.send(&mut Ok(()))?;
            } else if let (Some(Theme{ theme_type: Some(theme_type), ..}), Some(expected_theme_type_value)) =
              (settings.theme, expected_theme_type) {
                assert_eq!(theme_type, expected_theme_type_value);
                responder.send(&mut Ok(()))?;
            } else if let (Some(screen_enabled), Some(expected_screen_enabled_value)) =
              (settings.screen_enabled, expected_screen_enabled) {
              assert_eq!(screen_enabled, expected_screen_enabled_value);
              responder.send(&mut Ok(()))?;
            } else {
                panic!("Unexpected call to set");
            }
        },
        DisplayRequest::Watch { responder } => {
            responder.send(DisplaySettings {
                auto_brightness: Some(false),
                adjusted_auto_brightness: Some(0.5),
                brightness_value: Some(0.5),
                low_light_mode: Some(LowLightMode::Disable),
                theme: Some(Theme{theme_type: Some(ThemeType::Default), ..Theme::EMPTY}),
                screen_enabled: Some(true),
                ..DisplaySettings::EMPTY
            })?;
        }
    );

    let display_service = env
        .connect_to_protocol::<DisplayMarker>()
        .context("Failed to connect to display service")?;

    assert_successful!(display::command(
        display_service,
        expected_brightness,
        expected_auto_brightness,
        expected_auto_brightness_value,
        false,
        expected_low_light_mode,
        Some(Theme { theme_type: expected_theme_type, ..Theme::EMPTY }),
        expected_screen_enabled,
    ));

    Ok(())
}

// Can only check one mutate option at once
pub(crate) async fn validate_light_sensor() -> Result<(), Error> {
    let watch_called = Arc::new(RwLock::new(false));

    let watch_called_clone = watch_called.clone();

    let (display_service, mut stream) =
        fidl::endpoints::create_proxy_and_stream::<DisplayMarker>().unwrap();

    fasync::Task::spawn(async move {
        while let Some(request) = stream.try_next().await.unwrap() {
            match request {
                DisplayRequest::WatchLightSensor2 { delta: _, responder } => {
                    *watch_called_clone.write() = true;
                    responder
                        .send(LightSensorData {
                            illuminance_lux: Some(100.0),
                            color: Some(fidl_fuchsia_ui_types::ColorRgb {
                                red: 25.0,
                                green: 16.0,
                                blue: 59.0,
                            }),
                            ..LightSensorData::EMPTY
                        })
                        .unwrap();
                }
                _ => {}
            }
        }
    })
    .detach();

    assert_watch!(display::command(display_service, None, None, None, true, None, None, None));
    assert_eq!(*watch_called.read(), true);
    Ok(())
}
