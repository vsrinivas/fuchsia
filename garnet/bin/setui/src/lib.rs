// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::accessibility::spawn_accessibility_controller,
    crate::accessibility::spawn_accessibility_fidl_handler,
    crate::account::spawn_account_controller,
    crate::audio::spawn_audio_controller,
    crate::audio::spawn_audio_fidl_handler,
    crate::device::spawn_device_controller,
    crate::device::spawn_device_fidl_handler,
    crate::display::spawn_display_controller,
    crate::display::spawn_display_fidl_handler,
    crate::display::spawn_light_sensor_controller,
    crate::do_not_disturb::spawn_do_not_disturb_controller,
    crate::do_not_disturb::spawn_do_not_disturb_fidl_handler,
    crate::intl::intl_controller::IntlController,
    crate::intl::intl_fidl_handler::spawn_intl_fidl_handler,
    crate::power::spawn_power_controller,
    crate::privacy::privacy_controller::PrivacyController,
    crate::privacy::spawn_privacy_fidl_handler,
    crate::registry::base::Registry,
    crate::registry::device_storage::DeviceStorageFactory,
    crate::registry::registry_impl::RegistryImpl,
    crate::service_context::ServiceContext,
    crate::setup::setup_controller::SetupController,
    crate::setup::spawn_setup_fidl_handler,
    crate::switchboard::base::{SettingAction, SettingType},
    crate::switchboard::switchboard_impl::SwitchboardImpl,
    crate::system::spawn_setui_fidl_handler,
    crate::system::spawn_system_controller,
    crate::system::spawn_system_fidl_handler,
    fidl_fuchsia_settings::*,
    fidl_fuchsia_setui::SetUiServiceRequestStream,
    fuchsia_component::server::{ServiceFsDir, ServiceObj},
    fuchsia_syslog::fx_log_err,
    parking_lot::RwLock,
    std::collections::HashSet,
    std::sync::Arc,
};

mod accessibility;
mod account;
mod audio;
mod device;
mod display;
mod do_not_disturb;
mod fidl_clone;
mod fidl_processor;
mod input;
mod intl;
mod power;
mod privacy;
mod setup;
mod system;

pub mod config;
pub mod registry;
pub mod service_context;
pub mod switchboard;

/// Brings up the settings service fidl environment.
///
/// This method generates the necessary infrastructure to support the settings
/// service (switchboard, registry, etc.) and brings up the components necessary
/// to support the components specified in the components HashSet.
pub fn create_fidl_service<'a, T: DeviceStorageFactory>(
    mut service_dir: ServiceFsDir<'_, ServiceObj<'a, ()>>,
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

    registry_handle
        .write()
        .register(
            switchboard::base::SettingType::Power,
            spawn_power_controller(service_context_handle.clone()),
        )
        .unwrap();

    registry_handle
        .write()
        .register(
            switchboard::base::SettingType::Account,
            spawn_account_controller(service_context_handle.clone()),
        )
        .unwrap();

    if components.contains(&SettingType::Accessibility) {
        registry_handle
            .write()
            .register(
                switchboard::base::SettingType::Accessibility,
                spawn_accessibility_controller(
                    unboxed_storage_factory
                        .get_store::<switchboard::accessibility_types::AccessibilityInfo>(),
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
            .register(
                switchboard::base::SettingType::Audio,
                spawn_audio_controller(
                    service_context_handle.clone(),
                    unboxed_storage_factory.get_store::<switchboard::base::AudioInfo>(),
                ),
            )
            .unwrap();

        let switchboard_handle_clone = switchboard_handle.clone();
        service_dir.add_fidl_service(move |stream: AudioRequestStream| {
            spawn_audio_fidl_handler(switchboard_handle_clone.clone(), stream);
        });
    }

    if components.contains(&SettingType::Device) {
        registry_handle
            .write()
            .register(switchboard::base::SettingType::Device, spawn_device_controller())
            .unwrap();

        let switchboard_handle_clone = switchboard_handle.clone();
        service_dir.add_fidl_service(move |stream: DeviceRequestStream| {
            spawn_device_fidl_handler(switchboard_handle_clone.clone(), stream);
        });
    }

    if components.contains(&SettingType::Display) || components.contains(&SettingType::LightSensor)
    {
        if components.contains(&SettingType::Display) {
            registry_handle
                .write()
                .register(
                    switchboard::base::SettingType::Display,
                    spawn_display_controller(
                        service_context_handle.clone(),
                        unboxed_storage_factory.get_store::<switchboard::base::DisplayInfo>(),
                    ),
                )
                .unwrap();
        }
        if components.contains(&SettingType::LightSensor) {
            registry_handle
                .write()
                .register(
                    switchboard::base::SettingType::LightSensor,
                    spawn_light_sensor_controller(service_context_handle.clone()),
                )
                .unwrap();
        }
        let switchboard_handle_clone = switchboard_handle.clone();
        service_dir.add_fidl_service(move |stream: DisplayRequestStream| {
            spawn_display_fidl_handler(switchboard_handle_clone.clone(), stream);
        });
    }

    if components.contains(&SettingType::DoNotDisturb) {
        let register_result = registry_handle.write().register(
            switchboard::base::SettingType::DoNotDisturb,
            spawn_do_not_disturb_controller(
                unboxed_storage_factory.get_store::<switchboard::base::DoNotDisturbInfo>(),
            ),
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
            .register(
                switchboard::base::SettingType::Intl,
                IntlController::spawn(
                    service_context_handle.clone(),
                    unboxed_storage_factory.get_store::<switchboard::intl_types::IntlInfo>(),
                )
                .unwrap(),
            )
            .unwrap();

        let switchboard_handle_clone = switchboard_handle.clone();
        service_dir.add_fidl_service(move |stream: IntlRequestStream| {
            spawn_intl_fidl_handler(switchboard_handle_clone.clone(), stream);
        });
    }

    if components.contains(&SettingType::Privacy) {
        registry_handle
            .write()
            .register(
                switchboard::base::SettingType::Privacy,
                PrivacyController::spawn(
                    unboxed_storage_factory.get_store::<switchboard::base::PrivacyInfo>(),
                )
                .unwrap(),
            )
            .unwrap();

        let switchboard_handle_clone = switchboard_handle.clone();
        service_dir.add_fidl_service(move |stream: PrivacyRequestStream| {
            spawn_privacy_fidl_handler(switchboard_handle_clone.clone(), stream);
        });
    }

    if components.contains(&SettingType::System) {
        registry_handle
            .write()
            .register(
                switchboard::base::SettingType::System,
                spawn_system_controller(
                    unboxed_storage_factory.get_store::<switchboard::base::SystemInfo>(),
                ),
            )
            .unwrap();
        {
            let switchboard_handle_clone = switchboard_handle.clone();
            service_dir.add_fidl_service(move |stream: SystemRequestStream| {
                spawn_system_fidl_handler(switchboard_handle_clone.clone(), stream);
            });
        }
        {
            let switchboard_handle_clone = switchboard_handle.clone();
            service_dir.add_fidl_service(move |stream: SetUiServiceRequestStream| {
                spawn_setui_fidl_handler(switchboard_handle_clone.clone(), stream);
            });
        }
    }

    if components.contains(&SettingType::Setup) {
        registry_handle
            .write()
            .register(
                switchboard::base::SettingType::Setup,
                SetupController::spawn(
                    unboxed_storage_factory.get_store::<switchboard::base::SetupInfo>(),
                )
                .unwrap(),
            )
            .unwrap();
        let switchboard_handle_clone = switchboard_handle.clone();
        service_dir.add_fidl_service(move |stream: SetupRequestStream| {
            spawn_setup_fidl_handler(switchboard_handle_clone.clone(), stream);
        });
    }
}

#[cfg(test)]
mod tests;
