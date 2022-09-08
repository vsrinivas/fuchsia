// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::restore_agent;
use crate::base::SettingType;
use crate::config::base::ControllerFlag;
use crate::display::types::{DisplayInfo, LowLightMode, Theme};
use crate::ingress::fidl::{display, Interface};
use crate::storage::testing::InMemoryStorageFactory;
use crate::tests::fakes::brightness_service::BrightnessService;
use crate::tests::fakes::service_registry::ServiceRegistry;
use crate::tests::test_failure_utils::create_test_env_with_failures;
use crate::EnvironmentBuilder;
use anyhow::{anyhow, Result};
use assert_matches::assert_matches;
use fidl::Error::ClientChannelClosed;
use fidl::{endpoints::ServerEnd, prelude::*};
use fidl_fuchsia_settings::{DisplayMarker, DisplayProxy, IntlMarker};
use fuchsia_zircon::{self as zx, Status};
use futures::future::BoxFuture;
use futures::lock::Mutex;
use std::sync::Arc;

const ENV_NAME: &str = "settings_service_display_test_environment";
const AUTO_BRIGHTNESS_LEVEL: f32 = 0.9;

// Creates an environment that will fail on a get request.
async fn create_display_test_env_with_failures(
    storage_factory: Arc<InMemoryStorageFactory>,
) -> DisplayProxy {
    create_test_env_with_failures(
        storage_factory,
        ENV_NAME,
        Interface::Display(display::InterfaceFlags::BASE),
        SettingType::Display,
    )
    .await
    .connect_to_protocol::<DisplayMarker>()
    .unwrap()
}

// Makes sure that settings are restored from storage when service comes online.
#[fuchsia_async::run_until_stalled(test)]
async fn test_display_restore_with_storage_controller() {
    // Ensure auto-brightness value is restored correctly.
    validate_restore_with_storage_controller(
        0.7,
        AUTO_BRIGHTNESS_LEVEL,
        true,
        true,
        LowLightMode::Enable,
        None,
    )
    .await;

    // Ensure manual-brightness value is restored correctly.
    validate_restore_with_storage_controller(
        0.9,
        AUTO_BRIGHTNESS_LEVEL,
        false,
        true,
        LowLightMode::Disable,
        None,
    )
    .await;
}

async fn validate_restore_with_storage_controller(
    manual_brightness: f32,
    auto_brightness_value: f32,
    auto_brightness: bool,
    screen_enabled: bool,
    low_light_mode: LowLightMode,
    theme: Option<Theme>,
) {
    let service_registry = ServiceRegistry::create();
    let info = DisplayInfo {
        manual_brightness_value: manual_brightness,
        auto_brightness_value,
        auto_brightness,
        screen_enabled,
        low_light_mode,
        theme,
    };
    let storage_factory = InMemoryStorageFactory::with_initial_data(&info);

    let env = EnvironmentBuilder::new(Arc::new(storage_factory))
        .service(Box::new(ServiceRegistry::serve(service_registry)))
        .agents(&[restore_agent::blueprint::create()])
        .fidl_interfaces(&[Interface::Display(display::InterfaceFlags::BASE)])
        .spawn_and_get_protocol_connector(ENV_NAME)
        .await
        .ok();

    assert!(env.is_some());

    let display_proxy = env.unwrap().connect_to_protocol::<DisplayMarker>().unwrap();
    let settings = display_proxy.watch().await.expect("watch completed");

    if auto_brightness {
        assert_eq!(settings.auto_brightness, Some(auto_brightness));
        assert_eq!(settings.adjusted_auto_brightness, Some(auto_brightness_value));
    } else {
        assert_eq!(settings.brightness_value, Some(manual_brightness));
    }
}

// Makes sure that settings are restored from storage when service comes online.
#[fuchsia_async::run_until_stalled(test)]
async fn test_display_restore_with_brightness_controller() {
    // Ensure auto-brightness value is restored correctly.
    validate_restore_with_brightness_controller(
        0.7,
        AUTO_BRIGHTNESS_LEVEL,
        true,
        true,
        LowLightMode::Enable,
        None,
    )
    .await;

    // Ensure manual-brightness value is restored correctly.
    validate_restore_with_brightness_controller(
        0.9,
        AUTO_BRIGHTNESS_LEVEL,
        false,
        true,
        LowLightMode::Disable,
        None,
    )
    .await;
}

// Float comparisons are checking that set values are the same when retrieved.
#[allow(clippy::float_cmp)]
async fn validate_restore_with_brightness_controller(
    manual_brightness: f32,
    auto_brightness_value: f32,
    auto_brightness: bool,
    screen_enabled: bool,
    low_light_mode: LowLightMode,
    theme: Option<Theme>,
) {
    let service_registry = ServiceRegistry::create();
    let brightness_service_handle = BrightnessService::create();
    service_registry
        .lock()
        .await
        .register_service(Arc::new(Mutex::new(brightness_service_handle.clone())));
    let info = DisplayInfo {
        manual_brightness_value: manual_brightness,
        auto_brightness_value,
        auto_brightness,
        screen_enabled,
        low_light_mode,
        theme,
    };
    let storage_factory = InMemoryStorageFactory::with_initial_data(&info);

    assert!(EnvironmentBuilder::new(Arc::new(storage_factory))
        .service(Box::new(ServiceRegistry::serve(service_registry)))
        .agents(&[restore_agent::blueprint::create()])
        .fidl_interfaces(&[Interface::Display(display::InterfaceFlags::BASE)])
        .flags(&[ControllerFlag::ExternalBrightnessControl])
        .spawn_and_get_protocol_connector(ENV_NAME)
        .await
        .is_ok());

    if auto_brightness {
        let service_auto_brightness =
            brightness_service_handle.get_auto_brightness().lock().await.unwrap();
        assert_eq!(service_auto_brightness, auto_brightness);
    } else {
        let service_manual_brightness =
            brightness_service_handle.get_manual_brightness().lock().await.unwrap();
        assert_eq!(service_manual_brightness, manual_brightness);
    }
}

// Makes sure that a failing display stream doesn't cause a failure for a different interface.
#[fuchsia_async::run_until_stalled(test)]
async fn test_display_failure() {
    let service_gen =
        |service_name: &str, channel: zx::Channel| -> BoxFuture<'static, Result<()>> {
            match service_name {
                fidl_fuchsia_ui_brightness::ControlMarker::PROTOCOL_NAME => {
                    // This stream is closed immediately
                    let manager_stream_result =
                        ServerEnd::<fidl_fuchsia_ui_brightness::ControlMarker>::new(channel)
                            .into_stream();

                    if manager_stream_result.is_err() {
                        return Box::pin(async {
                            Err(anyhow!("could not move brightness channel into stream"))
                        });
                    }
                    return Box::pin(async { Ok(()) });
                }
                _ => Box::pin(async { Err(anyhow!("unsupported")) }),
            }
        };

    let env = EnvironmentBuilder::new(Arc::new(InMemoryStorageFactory::new()))
        .service(Box::new(service_gen))
        .fidl_interfaces(&[Interface::Display(display::InterfaceFlags::BASE), Interface::Intl])
        .spawn_and_get_protocol_connector(ENV_NAME)
        .await
        .unwrap();

    let display_proxy = env.connect_to_protocol::<DisplayMarker>().expect("connected to service");

    let _settings_value = display_proxy.watch().await.expect("watch completed");

    let intl_service = env.connect_to_protocol::<IntlMarker>().unwrap();
    let _settings = intl_service.watch().await.expect("watch completed");
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_channel_failure_watch() {
    let display_proxy =
        create_display_test_env_with_failures(Arc::new(InMemoryStorageFactory::new())).await;
    let result = display_proxy.watch().await;
    assert_matches!(result, Err(ClientChannelClosed { status: Status::UNAVAILABLE, .. }));
}
