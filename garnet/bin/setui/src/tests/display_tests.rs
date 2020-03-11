// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    crate::agent::restore_agent::RestoreAgent,
    crate::registry::device_storage::testing::*,
    crate::switchboard::base::{DisplayInfo, SettingType},
    crate::tests::fakes::brightness_service::BrightnessService,
    crate::tests::fakes::service_registry::ServiceRegistry,
    crate::EnvironmentBuilder,
    crate::Runtime,
    anyhow::format_err,
    fidl::endpoints::{ServerEnd, ServiceMarker},
    fidl_fuchsia_settings::*,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::future::BoxFuture,
    futures::lock::Mutex,
    futures::prelude::*,
    std::sync::Arc,
};

const ENV_NAME: &str = "settings_service_display_test_environment";

/// Tests that the FIDL calls result in appropriate commands sent to the switchboard
#[fuchsia_async::run_singlethreaded(test)]
async fn test_display() {
    const STARTING_BRIGHTNESS: f32 = 0.5;
    const CHANGED_BRIGHTNESS: f32 = 0.8;

    let service_gen = |service_name: &str,
                       channel: zx::Channel|
     -> BoxFuture<'static, Result<(), anyhow::Error>> {
        if service_name != fidl_fuchsia_ui_brightness::ControlMarker::NAME {
            return Box::pin(async { Err(format_err!("unsupported!")) });
        }

        let manager_stream_result =
            ServerEnd::<fidl_fuchsia_ui_brightness::ControlMarker>::new(channel).into_stream();

        if manager_stream_result.is_err() {
            return Box::pin(async { Err(format_err!("could not move into stream")) });
        }

        let mut manager_stream = manager_stream_result.unwrap();

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

        Box::pin(async { Ok(()) })
    };

    let env =
        EnvironmentBuilder::new(Runtime::Nested(ENV_NAME), InMemoryStorageFactory::create_handle())
            .service(Box::new(service_gen))
            .settings(&[SettingType::Display])
            .spawn_and_get_nested_environment()
            .await
            .unwrap();

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
async fn test_display_restore() {
    // Ensure auto-brightness value is restored correctly
    validate_restore(0.7, true).await;

    // Ensure manual-brightness value is restored correctly
    validate_restore(0.9, false).await;
}

async fn validate_restore(manual_brightness: f32, auto_brightness: bool) {
    let service_registry = ServiceRegistry::create();
    let brightness_service_handle = BrightnessService::create();
    service_registry.lock().await.register_service(brightness_service_handle.clone());

    let storage_factory = InMemoryStorageFactory::create_handle();
    {
        let store = storage_factory
            .lock()
            .await
            .get_device_storage::<DisplayInfo>(StorageAccessContext::Test);
        let info = DisplayInfo {
            manual_brightness_value: manual_brightness,
            auto_brightness: auto_brightness,
        };
        assert!(store.lock().await.write(&info, false).await.is_ok());
    }

    let env = EnvironmentBuilder::new(Runtime::Nested(ENV_NAME), storage_factory)
        .service(Box::new(ServiceRegistry::serve(service_registry)))
        .agents(&[Arc::new(Mutex::new(RestoreAgent::new()))])
        .settings(&[SettingType::Display])
        .spawn()
        .unwrap();

    assert!(env.completion_rx.await.unwrap().is_ok());

    if auto_brightness {
        let service_auto_brightness =
            brightness_service_handle.lock().await.get_auto_brightness().lock().await.unwrap();
        assert_eq!(service_auto_brightness, auto_brightness);
    } else {
        let service_manual_brightness =
            brightness_service_handle.lock().await.get_manual_brightness().lock().await.unwrap();
        assert_eq!(service_manual_brightness, manual_brightness);
    }
}

/// Makes sure that a failing display stream doesn't cause a failure for a different interface.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_display_failure() {
    let service_gen = |service_name: &str,
                       channel: zx::Channel|
     -> BoxFuture<'static, Result<(), anyhow::Error>> {
        match service_name {
            fidl_fuchsia_ui_brightness::ControlMarker::NAME => {
                // This stream is closed immediately
                let manager_stream_result =
                    ServerEnd::<fidl_fuchsia_ui_brightness::ControlMarker>::new(channel)
                        .into_stream();

                if manager_stream_result.is_err() {
                    return Box::pin(async {
                        Err(format_err!("could not move brightness channel into stream"))
                    });
                }
                return Box::pin(async { Ok(()) });
            }
            fidl_fuchsia_deprecatedtimezone::TimezoneMarker::NAME => {
                let timezone_stream_result =
                    ServerEnd::<fidl_fuchsia_deprecatedtimezone::TimezoneMarker>::new(channel)
                        .into_stream();

                if timezone_stream_result.is_err() {
                    return Box::pin(async {
                        Err(format_err!("could not move timezone channel into stream"))
                    });
                }
                let mut timezone_stream = timezone_stream_result.unwrap();
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
                return Box::pin(async { Ok(()) });
            }
            _ => Box::pin(async { Err(format_err!("unsupported")) }),
        }
    };

    let env =
        EnvironmentBuilder::new(Runtime::Nested(ENV_NAME), InMemoryStorageFactory::create_handle())
            .service(Box::new(service_gen))
            .settings(&[SettingType::Display, SettingType::Intl])
            .spawn_and_get_nested_environment()
            .await
            .unwrap();

    let display_proxy = env.connect_to_service::<DisplayMarker>().expect("connected to service");

    let _settings_value = display_proxy.watch().await.expect("watch completed");

    let intl_service = env.connect_to_service::<IntlMarker>().unwrap();
    let _settings = intl_service.watch().await.expect("watch completed").expect("watch successful");
}
