// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    crate::create_fidl_service,
    crate::registry::device_storage::testing::*,
    crate::service_context::ServiceContext,
    crate::switchboard::base::SettingType,
    anyhow::format_err,
    fidl::endpoints::{ServerEnd, ServiceMarker},
    fidl_fuchsia_settings::*,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon as zx,
    futures::prelude::*,
    parking_lot::RwLock,
    std::sync::Arc,
};

const ENV_NAME: &str = "settings_service_display_test_environment";

/// Tests that the FIDL calls result in appropriate commands sent to the switchboard
#[fuchsia_async::run_singlethreaded(test)]
async fn test_display() {
    const STARTING_BRIGHTNESS: f32 = 0.5;
    const CHANGED_BRIGHTNESS: f32 = 0.8;

    let service_gen = move |service_name: &str, channel: zx::Channel| {
        if service_name != fidl_fuchsia_ui_brightness::ControlMarker::NAME {
            return Err(format_err!("unsupported!"));
        }

        let mut manager_stream =
            ServerEnd::<fidl_fuchsia_ui_brightness::ControlMarker>::new(channel).into_stream()?;

        fasync::spawn(async move {
            let mut stored_brightness_value: f32 = STARTING_BRIGHTNESS;
            let mut auto_brightness_on = false;

            while let Some(req) = manager_stream.try_next().await.unwrap() {
                #[allow(unreachable_patterns)]
                match req {
                    fidl_fuchsia_ui_brightness::ControlRequest::WatchCurrentBrightness {
                        responder,
                    } => {
                        responder.send(stored_brightness_value).unwrap();
                    }
                    fidl_fuchsia_ui_brightness::ControlRequest::SetManualBrightness {
                        value,
                        control_handle: _,
                    } => {
                        stored_brightness_value = value;
                        auto_brightness_on = false;
                    }
                    fidl_fuchsia_ui_brightness::ControlRequest::SetAutoBrightness {
                        control_handle: _,
                    } => {
                        auto_brightness_on = true;
                    }

                    fidl_fuchsia_ui_brightness::ControlRequest::WatchAutoBrightness {
                        responder,
                    } => {
                        responder.send(auto_brightness_on).unwrap();
                    }
                    _ => {}
                }
            }
        });

        Ok(())
    };

    let mut fs = ServiceFs::new();

    create_fidl_service(
        fs.root_dir(),
        [SettingType::Display].iter().cloned().collect(),
        Arc::new(RwLock::new(ServiceContext::new(Some(Box::new(service_gen))))),
        Box::new(InMemoryStorageFactory::create()),
    );

    let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
    fasync::spawn(fs.collect());

    let display_proxy = env.connect_to_service::<DisplayMarker>().unwrap();

    let settings = display_proxy.watch().await.expect("watch completed").expect("watch successful");

    assert_eq!(settings.brightness_value, Some(STARTING_BRIGHTNESS));

    let mut display_settings = DisplaySettings::empty();
    display_settings.brightness_value = Some(CHANGED_BRIGHTNESS);
    display_proxy.set(display_settings).await.expect("set completed").expect("set successful");

    let settings = display_proxy.watch().await.expect("watch completed").expect("watch successful");

    assert_eq!(settings.brightness_value, Some(CHANGED_BRIGHTNESS));

    let mut display_settings = DisplaySettings::empty();
    display_settings.auto_brightness = Some(true);
    display_proxy.set(display_settings).await.expect("set completed").expect("set successful");

    let settings = display_proxy.watch().await.expect("watch completed").expect("watch successful");

    assert_eq!(settings.auto_brightness, Some(true));
}

/// Makes sure that a failing display stream doesn't cause a failure for a different interface.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_display_failure() {
    let service_gen = move |service_name: &str, channel: zx::Channel| match service_name {
        fidl_fuchsia_ui_brightness::ControlMarker::NAME => {
            // This stream is closed immediately
            let _manager_stream =
                ServerEnd::<fidl_fuchsia_ui_brightness::ControlMarker>::new(channel)
                    .into_stream()?;
            Ok(())
        }
        fidl_fuchsia_deprecatedtimezone::TimezoneMarker::NAME => {
            let mut timezone_stream =
                ServerEnd::<fidl_fuchsia_deprecatedtimezone::TimezoneMarker>::new(channel)
                    .into_stream()?;
            fasync::spawn(async move {
                while let Some(req) = timezone_stream.try_next().await.unwrap() {
                    #[allow(unreachable_patterns)]
                    match req {
                        fidl_fuchsia_deprecatedtimezone::TimezoneRequest::GetTimezoneId {
                            responder,
                        } => {
                            responder.send("PDT").unwrap();
                        }
                        _ => {}
                    }
                }
            });
            Ok(())
        }
        _ => Err(format_err!("unsupported!")),
    };

    let mut fs = ServiceFs::new();

    create_fidl_service(
        fs.root_dir(),
        [SettingType::Display, SettingType::Intl].iter().cloned().collect(),
        Arc::new(RwLock::new(ServiceContext::new(Some(Box::new(service_gen))))),
        Box::new(InMemoryStorageFactory::create()),
    );

    let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
    fasync::spawn(fs.collect());

    let display_proxy = env.connect_to_service::<DisplayMarker>().expect("connected to service");

    let _settings_value = display_proxy.watch().await.expect("watch completed");

    let intl_service = env.connect_to_service::<IntlMarker>().unwrap();
    let _settings = intl_service.watch().await.expect("watch completed").expect("watch successful");
}
