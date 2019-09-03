// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![feature(async_await)]
#![allow(unreachable_code)]
#![allow(dead_code)]

use {
    crate::accessibility::spawn_accessibility_controller,
    crate::accessibility::spawn_accessibility_fidl_handler,
    crate::audio::spawn_audio_controller,
    crate::audio::spawn_audio_fidl_handler,
    crate::default_store::DefaultStore,
    crate::display::spawn_display_controller,
    crate::display::spawn_display_fidl_handler,
    crate::do_not_disturb::do_not_disturb_controller::spawn_do_not_disturb_controller,
    crate::do_not_disturb::do_not_disturb_fidl_handler::spawn_do_not_disturb_fidl_handler,
    crate::intl::intl_controller::IntlController,
    crate::intl::intl_fidl_handler::IntlFidlHandler,
    crate::json_codec::JsonCodec,
    crate::mutation::*,
    crate::registry::base::Registry,
    crate::registry::device_storage::{DeviceStorageFactory, StashDeviceStorageFactory},
    crate::registry::registry_impl::RegistryImpl,
    crate::registry::service_context::ServiceContext,
    crate::setting_adapter::{MutationHandler, SettingAdapter},
    crate::setup::setup_controller::SetupController,
    crate::setup::setup_fidl_handler::SetupFidlHandler,
    crate::switchboard::base::{get_all_setting_types, SettingAction, SettingType},
    crate::switchboard::switchboard_impl::SwitchboardImpl,
    crate::system::spawn_system_controller,
    crate::system::spawn_system_fidl_handler,
    failure::Error,
    fidl_fuchsia_settings::*,
    fidl_fuchsia_setui::*,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_component::server::{ServiceFs, ServiceFsDir, ServiceObj},
    fuchsia_syslog::{self as syslog, fx_log_err, fx_log_info},
    futures::StreamExt,
    log::error,
    setui_handler::SetUIHandler,
    std::collections::HashSet,
    std::sync::{Arc, RwLock},
};

mod accessibility;
mod audio;
mod common;
mod default_store;
mod display;
mod do_not_disturb;
mod fidl_clone;
mod intl;
mod json_codec;
mod mutation;
mod registry;
mod setting_adapter;
mod setui_handler;
mod setup;
mod switchboard;
mod system;

const STASH_IDENTITY: &str = "settings_service";

fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["setui-service"]).expect("Can't init logger");
    fx_log_info!("Starting setui-service...");

    let mut executor = fasync::Executor::new()?;

    let service_context = Arc::new(RwLock::new(ServiceContext::new(None)));

    let mut fs = ServiceFs::new();

    let storage_factory = StashDeviceStorageFactory::create(
        STASH_IDENTITY,
        connect_to_service::<fidl_fuchsia_stash::StoreMarker>().unwrap(),
    );

    create_fidl_service(
        fs.dir("svc"),
        get_all_setting_types(),
        service_context,
        Box::new(storage_factory),
    );

    fs.take_and_serve_directory_handle()?;
    let () = executor.run_singlethreaded(fs.collect());
    Ok(())
}

/// Brings up the settings service fidl environment.
///
/// This method generates the necessary infrastructure to support the settings
/// service (switchboard, registry, etc.) and brings up the components necessary
/// to support the components specified in the components HashSet.
fn create_fidl_service<'a, T: DeviceStorageFactory>(
    mut service_dir: ServiceFsDir<ServiceObj<'a, ()>>,
    components: HashSet<switchboard::base::SettingType>,
    service_context_handle: Arc<RwLock<ServiceContext>>,
    storage_factory: Box<T>,
) {
    let (action_tx, action_rx) = futures::channel::mpsc::unbounded::<SettingAction>();
    let unboxed_storage_factory = &storage_factory;

    // Creates switchboard, handed to interface implementations to send messages
    // to handlers.
    let (switchboard_handle, event_tx) = SwitchboardImpl::create(action_tx);

    // Creates registry, used to register handlers for setting types.
    let registry_handle = RegistryImpl::create(event_tx, action_rx);

    let handler = Arc::new(SetUIHandler::new());

    // TODO(SU-210): Remove once other adapters are ready.
    handler.register_adapter(Box::new(SettingAdapter::new(
        fidl_fuchsia_setui::SettingType::Unknown,
        Box::new(DefaultStore::new("/data/unknown.dat".to_string(), Box::new(JsonCodec::new()))),
        MutationHandler { process: &process_string_mutation, check_sync: None },
        None,
    )));

    handler.register_adapter(Box::new(SettingAdapter::new(
        fidl_fuchsia_setui::SettingType::Account,
        Box::new(DefaultStore::new("/data/account.dat".to_string(), Box::new(JsonCodec::new()))),
        MutationHandler {
            process: &process_account_mutation,
            check_sync: Some(&should_sync_account_mutation),
        },
        Some(SettingData::Account(AccountSettings { mode: None })),
    )));

    let handler_clone = handler.clone();

    service_dir.add_fidl_service(move |stream: SetUiServiceRequestStream| {
        let handler_clone = handler_clone.clone();

        fx_log_info!("Connecting to setui_service");
        fasync::spawn(async move {
            handler_clone
                .handle_stream(stream)
                .await
                .unwrap_or_else(|e| error!("Failed to spawn {:?}", e))
        });
    });

    if components.contains(&SettingType::Accessibility) {
        registry_handle
            .write()
            .unwrap()
            .register(
                switchboard::base::SettingType::Accessibility,
                spawn_accessibility_controller(
                    service_context_handle.clone(),
                    unboxed_storage_factory.get_store::<switchboard::base::AccessibilityInfo>(),
                ),
            )
            .unwrap();

        let switchboard_handle_clone = switchboard_handle.clone();
        service_dir.add_fidl_service(move |stream: AccessibilityRequestStream| {
            spawn_accessibility_fidl_handler(switchboard_handle_clone.clone(), stream);
        });
    }

    if components.contains(&SettingType::Audio) {
        registry_handle
            .write()
            .unwrap()
            .register(switchboard::base::SettingType::Audio, spawn_audio_controller())
            .unwrap();

        let switchboard_handle_clone = switchboard_handle.clone();
        service_dir.add_fidl_service(move |stream: AudioRequestStream| {
            spawn_audio_fidl_handler(switchboard_handle_clone.clone(), stream);
        });
    }

    if components.contains(&SettingType::Display) {
        registry_handle
            .write()
            .unwrap()
            .register(
                switchboard::base::SettingType::Display,
                spawn_display_controller(
                    service_context_handle.clone(),
                    unboxed_storage_factory.get_store::<switchboard::base::DisplayInfo>(),
                ),
            )
            .unwrap();

        let switchboard_handle_clone = switchboard_handle.clone();
        service_dir.add_fidl_service(move |stream: DisplayRequestStream| {
            spawn_display_fidl_handler(switchboard_handle_clone.clone(), stream);
        });
    }

    if components.contains(&SettingType::DoNotDisturb) {
        let register_result = registry_handle.write().unwrap().register(
            switchboard::base::SettingType::DoNotDisturb,
            spawn_do_not_disturb_controller(service_context_handle.clone()),
        );
        match register_result {
            Ok(_) => {}
            Err(e) => fx_log_err!("failed to register do_not_disturb in registry, {:#?}", e),
        };

        let switchboard_handle_clone = switchboard_handle.clone();
        service_dir.add_fidl_service(move |stream: DoNotDisturbRequestStream| {
            spawn_do_not_disturb_fidl_handler(switchboard_handle_clone.clone(), stream);
        });
    }

    if components.contains(&SettingType::Intl) {
        registry_handle
            .write()
            .unwrap()
            .register(
                switchboard::base::SettingType::Intl,
                IntlController::spawn(service_context_handle.clone()).unwrap(),
            )
            .unwrap();

        let switchboard_handle_clone = switchboard_handle.clone();
        service_dir.add_fidl_service(move |stream: IntlRequestStream| {
            IntlFidlHandler::spawn(switchboard_handle_clone.clone(), stream);
        });
    }

    if components.contains(&SettingType::System) {
        registry_handle
            .write()
            .unwrap()
            .register(switchboard::base::SettingType::System, spawn_system_controller())
            .unwrap();

        let switchboard_handle_clone = switchboard_handle.clone();
        service_dir.add_fidl_service(move |stream: SystemRequestStream| {
            spawn_system_fidl_handler(switchboard_handle_clone.clone(), stream);
        });
    }

    if components.contains(&SettingType::Setup) {
        registry_handle
            .write()
            .unwrap()
            .register(
                switchboard::base::SettingType::Setup,
                SetupController::spawn(
                    service_context_handle.clone(),
                    unboxed_storage_factory.get_store::<switchboard::base::SetupInfo>(),
                )
                .unwrap(),
            )
            .unwrap();
        let switchboard_handle_clone = switchboard_handle.clone();
        service_dir.add_fidl_service(move |stream: SetupRequestStream| {
            SetupFidlHandler::spawn(switchboard_handle_clone.clone(), stream);
        });
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::audio::create_default_audio_stream,
        crate::fidl_clone::FIDLClone,
        crate::registry::device_storage::testing::*,
        crate::registry::device_storage::DeviceStorageFactory,
        crate::switchboard::base::{
            AccessibilityInfo, AudioStreamType, ConfigurationInterfaceFlags, SetupInfo,
        },
        failure::format_err,
        fidl::endpoints::{ServerEnd, ServiceMarker},
        fidl_fuchsia_accessibility::*,
        fidl_fuchsia_settings::ColorBlindnessType,
        fuchsia_zircon as zx,
        futures::prelude::*,
    };

    const ENV_NAME: &str = "settings_service_test_environment";

    enum Services {
        Accessibility(AccessibilityRequestStream),
        DoNotDisturb(DoNotDisturbRequestStream),
        Timezone(fidl_fuchsia_timezone::TimezoneRequestStream),
        Intl(IntlRequestStream),
    }

    // TODO(fxb/35254): move out of main.rs
    async fn create_test_accessibility_env(
        initial_audio_description: bool,
        initial_color_correction: ColorCorrection,
        storage_factory: Box<InMemoryStorageFactory>,
    ) -> fuchsia_component::server::NestedEnvironment {
        // Fake accessibility service for test.
        let service_gen = move |service_name: &str, channel: zx::Channel| {
            if service_name != SettingsManagerMarker::NAME {
                return Err(format_err!("unsupported!"));
            }

            let stored_audio_description: Arc<RwLock<bool>> =
                Arc::new(RwLock::new(initial_audio_description));
            let stored_color_correction: Arc<RwLock<ColorCorrection>> =
                Arc::new(RwLock::new(initial_color_correction));

            // Handle calls to RegisterSettingProvider.
            let mut manager_stream =
                ServerEnd::<SettingsManagerMarker>::new(channel).into_stream()?;
            fasync::spawn(async move {
                while let Some(req) = manager_stream.try_next().await.unwrap() {
                    #[allow(unreachable_patterns)]
                    match req {
                        SettingsManagerRequest::RegisterSettingProvider {
                            settings_provider_request,
                            control_handle: _,
                        } => {
                            let stored_audio_description_clone = stored_audio_description.clone();
                            let stored_color_correction_clone = stored_color_correction.clone();

                            // Handle set calls on the provider that was registered.
                            let mut provider_stream =
                                settings_provider_request.into_stream().unwrap();
                            fasync::spawn(async move {
                                while let Some(req) = provider_stream.try_next().await.unwrap() {
                                    #[allow(unreachable_patterns)]
                                    match req {
                                        SettingsProviderRequest::SetScreenReaderEnabled {
                                            screen_reader_enabled,
                                            responder,
                                        } => {
                                            *stored_audio_description_clone.write().unwrap() =
                                                screen_reader_enabled;
                                            responder.send(SettingsManagerStatus::Ok).unwrap();
                                        }
                                        SettingsProviderRequest::SetColorCorrection {
                                            color_correction,
                                            responder,
                                        } => {
                                            *stored_color_correction_clone.write().unwrap() =
                                                color_correction;
                                            responder.send(SettingsManagerStatus::Ok).unwrap();
                                        }
                                        _ => {}
                                    }
                                }
                            });
                        }
                        _ => {}
                    }
                }
            });

            Ok(())
        };

        // Create the fake accessibility service.
        let mut fs = ServiceFs::new();

        create_fidl_service(
            fs.root_dir(),
            [SettingType::Accessibility].iter().cloned().collect(),
            Arc::new(RwLock::new(ServiceContext::new(Some(Box::new(service_gen))))),
            storage_factory,
        );

        let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
        fasync::spawn(fs.collect());

        env
    }

    // TODO(fxb/35254): move out of main.rs
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_accessibility_audio_description() {
        const INITIAL_AUDIO_DESCRIPTION: bool = false;
        const CHANGED_AUDIO_DESCRIPTION: bool = true;

        const INITIAL_COLOR_CORRECTION: ColorCorrection = ColorCorrection::Disabled;

        let expected_struct = AccessibilityInfo {
            audio_description: CHANGED_AUDIO_DESCRIPTION,
            color_correction: switchboard::base::ColorBlindnessType::None,
        };

        let factory = Box::new(InMemoryStorageFactory::create());
        let store = factory.get_store::<AccessibilityInfo>();

        let env = create_test_accessibility_env(
            INITIAL_AUDIO_DESCRIPTION,
            INITIAL_COLOR_CORRECTION,
            factory,
        )
        .await;

        // Fetch the initial audio description value.
        let accessibility_proxy = env.connect_to_service::<AccessibilityMarker>().unwrap();
        let settings =
            accessibility_proxy.watch().await.expect("watch completed").expect("watch successful");
        assert_eq!(settings.audio_description, Some(INITIAL_AUDIO_DESCRIPTION));

        // Set the audio description value.
        let mut accessibility_settings = AccessibilitySettings::empty();
        accessibility_settings.audio_description = Some(CHANGED_AUDIO_DESCRIPTION);
        accessibility_proxy
            .set(accessibility_settings)
            .await
            .expect("set completed")
            .expect("set successful");

        // Verify the value we set is persisted in DeviceStorage.
        let mut store_lock = store.lock().await;
        let retrieved_struct = store_lock.get().await;
        assert_eq!(expected_struct, retrieved_struct);

        // Verify the value we set is returned when watching.
        let settings =
            accessibility_proxy.watch().await.expect("watch completed").expect("watch successful");
        assert_eq!(settings.audio_description, Some(CHANGED_AUDIO_DESCRIPTION));
    }

    // TODO(fxb/35254): move out of main.rs
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_accessibility_color_correction() {
        const INITIAL_AUDIO_DESCRIPTION: bool = false;
        const INITIAL_COLOR_CORRECTION: ColorCorrection = ColorCorrection::Disabled;

        const INITIAL_COLOR_BLINDNESS_TYPE: ColorBlindnessType = ColorBlindnessType::None;
        const CHANGED_COLOR_BLINDNESS_TYPE: ColorBlindnessType = ColorBlindnessType::Tritanomaly;

        let expected_struct = AccessibilityInfo {
            audio_description: INITIAL_AUDIO_DESCRIPTION,
            color_correction: switchboard::base::ColorBlindnessType::Tritanomaly,
        };

        let factory = Box::new(InMemoryStorageFactory::create());
        let store = factory.get_store::<AccessibilityInfo>();

        let env = create_test_accessibility_env(
            INITIAL_AUDIO_DESCRIPTION,
            INITIAL_COLOR_CORRECTION,
            factory,
        )
        .await;

        // Fetch the initial color correction value.
        let accessibility_proxy = env.connect_to_service::<AccessibilityMarker>().unwrap();
        let settings =
            accessibility_proxy.watch().await.expect("watch completed").expect("watch successful");
        assert_eq!(settings.color_correction, Some(INITIAL_COLOR_BLINDNESS_TYPE));

        // Set the color correction value.
        let mut accessibility_settings = AccessibilitySettings::empty();
        accessibility_settings.color_correction = Some(CHANGED_COLOR_BLINDNESS_TYPE);
        accessibility_proxy
            .set(accessibility_settings)
            .await
            .expect("set completed")
            .expect("set successful");

        // Verify the value we set is persisted in DeviceStorage.
        let mut store_lock = store.lock().await;
        let retrieved_struct = store_lock.get().await;
        assert_eq!(expected_struct, retrieved_struct);

        // Verify the value we set is returned when watching.
        let settings =
            accessibility_proxy.watch().await.expect("watch completed").expect("watch successful");
        assert_eq!(settings.color_correction, Some(CHANGED_COLOR_BLINDNESS_TYPE));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_audio() {
        let mut fs = ServiceFs::new();

        let default_media_stream =
            AudioStreamSettings::from(create_default_audio_stream(AudioStreamType::Media));

        let changed_media_stream = AudioStreamSettings {
            stream: Some(fidl_fuchsia_media::AudioRenderUsage::Media),
            source: Some(AudioStreamSettingSource::User),
            user_volume: Some(Volume { level: Some(0.7), muted: Some(true) }),
        };

        create_fidl_service(
            fs.root_dir(),
            [SettingType::Audio].iter().cloned().collect(),
            Arc::new(RwLock::new(ServiceContext::new(None))),
            Box::new(InMemoryStorageFactory::create()),
        );

        let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
        fasync::spawn(fs.collect());

        let audio_proxy = env.connect_to_service::<AudioMarker>().unwrap();

        let settings =
            audio_proxy.watch().await.expect("watch completed").expect("watch successful");

        let streams = settings.streams.expect("audio settings contain streams");
        streams
            .into_iter()
            .find(|x| *x == default_media_stream)
            .expect("contains default media stream");

        let mut audio_settings = AudioSettings::empty();
        audio_settings.streams = Some(vec![changed_media_stream.clone()]);

        audio_proxy.set(audio_settings).await.expect("set completed").expect("set successful");

        let settings =
            audio_proxy.watch().await.expect("watch completed").expect("watch successful");
        settings
            .streams
            .expect("audio settings contain streams")
            .into_iter()
            .find(|x| *x == changed_media_stream)
            .expect("contains changed media stream");
    }

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
                ServerEnd::<fidl_fuchsia_ui_brightness::ControlMarker>::new(channel)
                    .into_stream()?;

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

        let settings =
            display_proxy.watch().await.expect("watch completed").expect("watch successful");

        assert_eq!(settings.brightness_value, Some(STARTING_BRIGHTNESS));

        let mut display_settings = DisplaySettings::empty();
        display_settings.brightness_value = Some(CHANGED_BRIGHTNESS);
        display_proxy.set(display_settings).await.expect("set completed").expect("set successful");

        let settings =
            display_proxy.watch().await.expect("watch completed").expect("watch successful");

        assert_eq!(settings.brightness_value, Some(CHANGED_BRIGHTNESS));

        let mut display_settings = DisplaySettings::empty();
        display_settings.auto_brightness = Some(true);
        display_proxy.set(display_settings).await.expect("set completed").expect("set successful");

        let settings =
            display_proxy.watch().await.expect("watch completed").expect("watch successful");

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
            fidl_fuchsia_timezone::TimezoneMarker::NAME => {
                let mut timezone_stream =
                    ServerEnd::<fidl_fuchsia_timezone::TimezoneMarker>::new(channel)
                        .into_stream()?;
                fasync::spawn(async move {
                    while let Some(req) = timezone_stream.try_next().await.unwrap() {
                        #[allow(unreachable_patterns)]
                        match req {
                            fidl_fuchsia_timezone::TimezoneRequest::GetTimezoneId { responder } => {
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

        let display_proxy =
            env.connect_to_service::<DisplayMarker>().expect("connected to service");

        let _settings_value = display_proxy.watch().await.expect("watch completed");

        let intl_service = env.connect_to_service::<IntlMarker>().unwrap();
        let _settings =
            intl_service.watch().await.expect("watch completed").expect("watch successful");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_do_not_disturb() {
        let mut fs = ServiceFs::new();

        create_fidl_service(
            fs.root_dir(),
            [SettingType::DoNotDisturb].iter().cloned().collect(),
            Arc::new(RwLock::new(ServiceContext::new(None))),
            Box::new(InMemoryStorageFactory::create()),
        );

        let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
        fasync::spawn(fs.collect());

        let dnd_proxy =
            env.connect_to_service::<DoNotDisturbMarker>().expect("connected to service");

        let mut dnd_settings = DoNotDisturbSettings::empty();
        dnd_settings.user_initiated_do_not_disturb = Some(true);

        // Set doesn't do anything yet, just make sure it doesn't panic
        dnd_proxy.set(dnd_settings).await.expect("set completed").expect("set successful");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_intl() {
        const INITIAL_TIME_ZONE: &'static str = "PDT";

        let service_gen = |service_name: &str, channel: zx::Channel| {
            if service_name != fidl_fuchsia_timezone::TimezoneMarker::NAME {
                return Err(format_err!("unsupported!"));
            }

            let mut timezone_stream =
                ServerEnd::<fidl_fuchsia_timezone::TimezoneMarker>::new(channel).into_stream()?;

            fasync::spawn(async move {
                let mut stored_timezone = INITIAL_TIME_ZONE.to_string();
                while let Some(req) = timezone_stream.try_next().await.unwrap() {
                    #[allow(unreachable_patterns)]
                    match req {
                        fidl_fuchsia_timezone::TimezoneRequest::GetTimezoneId { responder } => {
                            responder.send(&stored_timezone).unwrap();
                        }
                        fidl_fuchsia_timezone::TimezoneRequest::SetTimezone {
                            timezone_id,
                            responder,
                        } => {
                            stored_timezone = timezone_id;
                            responder.send(true).unwrap();
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
            [SettingType::Intl].iter().cloned().collect(),
            Arc::new(RwLock::new(ServiceContext::new(Some(Box::new(service_gen))))),
            Box::new(InMemoryStorageFactory::create()),
        );

        let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
        fasync::spawn(fs.collect());

        let intl_service = env.connect_to_service::<IntlMarker>().unwrap();
        let settings =
            intl_service.watch().await.expect("watch completed").expect("watch successful");

        if let Some(time_zone_id) = settings.time_zone_id {
            assert_eq!(time_zone_id.id, INITIAL_TIME_ZONE);
        }

        let mut intl_settings = fidl_fuchsia_settings::IntlSettings::empty();
        let updated_timezone = "PDT";

        intl_settings.time_zone_id =
            Some(fidl_fuchsia_intl::TimeZoneId { id: updated_timezone.to_string() });
        intl_service.set(intl_settings).await.expect("set completed").expect("set successful");
        let settings =
            intl_service.watch().await.expect("watch completed").expect("watch successful");
        assert_eq!(
            settings.time_zone_id,
            Some(fidl_fuchsia_intl::TimeZoneId { id: updated_timezone.to_string() })
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_system() {
        const STARTING_LOGIN_MODE: fidl_fuchsia_settings::LoginOverride =
            fidl_fuchsia_settings::LoginOverride::None;
        const CHANGED_LOGIN_MODE: fidl_fuchsia_settings::LoginOverride =
            fidl_fuchsia_settings::LoginOverride::AuthProvider;
        let mut fs = ServiceFs::new();

        create_fidl_service(
            fs.root_dir(),
            [SettingType::System].iter().cloned().collect(),
            Arc::new(RwLock::new(ServiceContext::new(None))),
            Box::new(InMemoryStorageFactory::create()),
        );

        let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
        fasync::spawn(fs.collect());

        let system_proxy = env.connect_to_service::<SystemMarker>().unwrap();

        let settings =
            system_proxy.watch().await.expect("watch completed").expect("watch successful");

        assert_eq!(settings.mode, Some(STARTING_LOGIN_MODE));

        let mut system_settings = SystemSettings::empty();
        system_settings.mode = Some(CHANGED_LOGIN_MODE);
        system_proxy.set(system_settings).await.expect("set completed").expect("set successful");

        let settings =
            system_proxy.watch().await.expect("watch completed").expect("watch successful");

        assert_eq!(settings.mode, Some(CHANGED_LOGIN_MODE));
    }

    //TODO(fxb/35371): Move out of main.rs
    // Setup doesn't rely on any service yet. In the future this test will be
    // updated to verify restart request is made on interface change.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_setup() {
        let mut fs = ServiceFs::new();

        let storage_factory = Box::new(InMemoryStorageFactory::create());
        let store = storage_factory.get_store::<SetupInfo>();

        // Prepopulate initial value
        {
            let mut store_lock = store.lock().await;
            assert!(store_lock
                .write(SetupInfo { configuration_interfaces: ConfigurationInterfaceFlags::WIFI })
                .is_ok());
        }

        create_fidl_service(
            fs.root_dir(),
            [SettingType::Setup].iter().cloned().collect(),
            Arc::new(RwLock::new(ServiceContext::new(None))),
            storage_factory,
        );

        let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
        fasync::spawn(fs.collect());

        let setup_service = env.connect_to_service::<SetupMarker>().unwrap();

        // Ensure retrieved value matches set value
        let settings = setup_service.watch().await.expect("watch completed");
        assert_eq!(
            settings.enabled_configuration_interfaces,
            Some(fidl_fuchsia_settings::ConfigurationInterfaces::Wifi)
        );

        let expected_interfaces = fidl_fuchsia_settings::ConfigurationInterfaces::Ethernet;

        // Ensure setting interface propagates  change correctly
        let mut setup_settings = fidl_fuchsia_settings::SetupSettings::empty();
        setup_settings.enabled_configuration_interfaces = Some(expected_interfaces);
        setup_service.set(setup_settings).await.expect("set completed").expect("set successful");

        // Ensure retrieved value matches set value
        let settings = setup_service.watch().await.expect("watch completed");
        assert_eq!(settings.enabled_configuration_interfaces, Some(expected_interfaces));

        // Check to make sure value wrote out to store correctly.
        {
            let mut store_lock = store.lock().await;
            assert_eq!(
                store_lock.get().await.configuration_interfaces,
                ConfigurationInterfaceFlags::ETHERNET
            );
        }
    }
}
