// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![feature(async_await, await_macro)]
#![allow(unreachable_code)]
#![allow(dead_code)]

use {
    crate::default_store::DefaultStore,
    crate::display::spawn_display_controller,
    crate::display::spawn_display_fidl_handler,
    crate::json_codec::JsonCodec,
    crate::mutation::*,
    crate::registry::base::Registry,
    crate::registry::registry_impl::RegistryImpl,
    crate::setting_adapter::{MutationHandler, SettingAdapter},
    crate::switchboard::base::SettingAction,
    crate::switchboard::switchboard_impl::SwitchboardImpl,
    failure::Error,
    fidl_fuchsia_settings::*,
    fidl_fuchsia_setui::*,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_component::server::{ServiceFs, ServiceObj},
    fuchsia_syslog::{self as syslog, fx_log_info},
    futures::StreamExt,
    log::error,
    setui_handler::SetUIHandler,
    std::sync::Arc,
    system_handler::SystemStreamHandler,
};

mod common;
mod default_store;
mod display;
mod fidl_clone;
mod json_codec;
mod mutation;
mod registry;
mod setting_adapter;
mod setui_handler;
mod switchboard;
mod system_handler;

fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["setui-service"]).expect("Can't init logger");
    fx_log_info!("Starting setui-service...");

    let mut executor = fasync::Executor::new()?;

    let brightness_service =
        connect_to_service::<fidl_fuchsia_device_display::ManagerMarker>().unwrap();

    let mut fs = create_fidl_service(brightness_service);

    fs.take_and_serve_directory_handle()?;
    let () = executor.run_singlethreaded(fs.collect());
    Ok(())
}

fn create_fidl_service<'a>(
    brightness_service: fidl_fuchsia_device_display::ManagerProxy,
) -> ServiceFs<ServiceObj<'a, ()>> {
    let (action_tx, action_rx) = futures::channel::mpsc::unbounded::<SettingAction>();

    // Creates switchboard, handed to interface implementations to send messages
    // to handlers.
    let (switchboard_handle, event_tx) = SwitchboardImpl::create(action_tx);

    // Creates registry, used to register handlers for setting types.
    let registry_handle = RegistryImpl::create(event_tx, action_rx);

    registry_handle
        .write()
        .unwrap()
        .register(
            switchboard::base::SettingType::Display,
            spawn_display_controller(brightness_service),
        )
        .unwrap();

    let mut fs = ServiceFs::new();
    let handler = Arc::new(SetUIHandler::new());
    let system_handler = Arc::new(SystemStreamHandler::new(handler.clone()));

    // TODO(SU-210): Remove once other adapters are ready.
    handler.register_adapter(Box::new(SettingAdapter::new(
        SettingType::Unknown,
        Box::new(DefaultStore::new("/data/unknown.dat".to_string(), Box::new(JsonCodec::new()))),
        MutationHandler { process: &process_string_mutation, check_sync: None },
        None,
    )));

    handler.register_adapter(Box::new(SettingAdapter::new(
        SettingType::Account,
        Box::new(DefaultStore::new("/data/account.dat".to_string(), Box::new(JsonCodec::new()))),
        MutationHandler {
            process: &process_account_mutation,
            check_sync: Some(&should_sync_account_mutation),
        },
        Some(SettingData::Account(AccountSettings { mode: None })),
    )));

    let handler_clone = handler.clone();
    fs.dir("svc").add_fidl_service(move |stream: SetUiServiceRequestStream| {
        let handler_clone = handler_clone.clone();

        fx_log_info!("Connecting to setui_service");
        fasync::spawn(async move {
            await!(handler_clone.handle_stream(stream))
                .unwrap_or_else(|e| error!("Failed to spawn {:?}", e))
        });
    });

    // Register for the new settings APIs as well.
    fs.dir("svc").add_fidl_service(move |stream: SystemRequestStream| {
        let system_handler_clone = system_handler.clone();
        fx_log_info!("Connecting to System");
        fasync::spawn(async move {
            await!(system_handler_clone.handle_system_stream(stream))
                .unwrap_or_else(|e| error!("Failed to spawn {:?}", e))
        });
    });

    fs.dir("svc").add_fidl_service(move |stream: DisplayRequestStream| {
        spawn_display_fidl_handler(switchboard_handle.clone(), stream);
    });
    fs
}


#[cfg(test)]
mod tests {
    use super::*;
    use futures::prelude::*;

    const ENV_NAME: &str = "settings_service_test_environment";

    enum Services {
        Manager(fidl_fuchsia_device_display::ManagerRequestStream),
        Display(DisplayRequestStream),
    }

    /// Tests that the FIDL calls result in appropriate commands sent to the switchboard
    /// TODO(ejia): refactor to be more common with main function
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_display() {
        const STARTING_BRIGHTNESS: f32 = 0.5;
        const CHANGED_BRIGHTNESS: f32 = 0.8;

        let mut fs = ServiceFs::new();
        fs.add_fidl_service(Services::Manager);
        let test_env = fs.create_salted_nested_environment(ENV_NAME).unwrap();

        fasync::spawn(fs.for_each_concurrent(None, move |connection| {
            async move {
                match connection {
                    Services::Manager(mut manager_stream) => {
                        let mut stored_brightness_value: f64 = STARTING_BRIGHTNESS.into();
                        while let Some(req) = await!(manager_stream.try_next()).unwrap() {
                            #[allow(unreachable_patterns)]
                            match req {
                                fidl_fuchsia_device_display::ManagerRequest::GetBrightness {
                                    responder,
                                } => {
                                    responder.send(true, stored_brightness_value.into()).unwrap();
                                }
                                fidl_fuchsia_device_display::ManagerRequest::SetBrightness {
                                    brightness,
                                    responder,
                                } => {
                                    stored_brightness_value = brightness;
                                    responder.send(true).unwrap();
                                }
                            }
                        }
                    }
                    _ => {
                        panic!("Unexpected service");
                    }
                }

            }
        }));

        let brightness_service =
            test_env.connect_to_service::<fidl_fuchsia_device_display::ManagerMarker>().unwrap();

        let (action_tx, action_rx) = futures::channel::mpsc::unbounded::<SettingAction>();

        // Creates switchboard, handed to interface implementations to send messages
        // to handlers.
        let (switchboard_handle, event_tx) = SwitchboardImpl::create(action_tx);

        // Creates registry, used to register handlers for setting types.
        let registry_handle = RegistryImpl::create(event_tx, action_rx);

        registry_handle
            .write()
            .unwrap()
            .register(
                switchboard::base::SettingType::Display,
                spawn_display_controller(brightness_service),
            )
            .unwrap();

        let mut fs = ServiceFs::new();

        fs.add_fidl_service(Services::Display);

        let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();

        fasync::spawn(fs.for_each_concurrent(None, move |connection| {
            let switchboard_handle = switchboard_handle.clone();
            async move {
                match connection {
                    Services::Display(display_stream) => {
                        spawn_display_fidl_handler(switchboard_handle.clone(), display_stream);
                    }
                    _ => {
                        panic!("Unexpected service");
                    }
                }

            }
        }));


        let display_service = env.connect_to_service::<DisplayMarker>().unwrap();
        let settings =
            await!(display_service.watch()).expect("watch completed").expect("watch successful");

        assert_eq!(settings.brightness_value, Some(STARTING_BRIGHTNESS));

        let mut display_settings = DisplaySettings::empty();
        display_settings.brightness_value = Some(CHANGED_BRIGHTNESS);
        await!(display_service.set(display_settings))
            .expect("set completed")
            .expect("set successful");
        let settings =
            await!(display_service.watch()).expect("watch completed").expect("watch successful");

        assert_eq!(settings.brightness_value, Some(CHANGED_BRIGHTNESS));

    }
}
