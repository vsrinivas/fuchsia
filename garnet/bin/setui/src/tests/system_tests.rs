// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    crate::registry::device_storage::testing::*,
    crate::switchboard::base::{SettingType, SystemInfo, SystemLoginOverrideMode},
    crate::tests::fakes::device_settings_service::DeviceSettingsService,
    crate::tests::fakes::hardware_power_statecontrol_service::{
        Action, HardwarePowerStatecontrolService, Response,
    },
    crate::tests::fakes::service_registry::ServiceRegistry,
    crate::tests::test_failure_utils::create_test_env_with_failures,
    crate::EnvironmentBuilder,
    fidl::Error::ClientChannelClosed,
    fidl_fuchsia_settings::{Error as SettingsError, SystemMarker, SystemProxy, SystemSettings},
    fuchsia_zircon::Status,
    futures::lock::Mutex,
    std::sync::Arc,
};

const ENV_NAME: &str = "settings_service_system_test_environment";
const CONTEXT_ID: u64 = 0;
const FACTORY_RESET_FLAG: &str = "FactoryReset";

/// Creates an environment that will fail on a get request.
async fn create_system_test_env_with_failures() -> SystemProxy {
    let storage_factory = InMemoryStorageFactory::create();
    create_test_env_with_failures(storage_factory, ENV_NAME, SettingType::System)
        .await
        .connect_to_service::<SystemMarker>()
        .unwrap()
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_system() {
    const STARTING_LOGIN_MODE: fidl_fuchsia_settings::LoginOverride =
        fidl_fuchsia_settings::LoginOverride::AutologinGuest;
    const CHANGED_LOGIN_MODE: fidl_fuchsia_settings::LoginOverride =
        fidl_fuchsia_settings::LoginOverride::AuthProvider;

    let storage_factory = InMemoryStorageFactory::create();
    let store = storage_factory
        .lock()
        .await
        .get_device_storage::<SystemInfo>(StorageAccessContext::Test, CONTEXT_ID);

    // Write out initial value to storage.
    {
        let initial_value =
            SystemInfo { login_override_mode: SystemLoginOverrideMode::from(STARTING_LOGIN_MODE) };
        let mut store_lock = store.lock().await;
        store_lock.write(&initial_value, false).await.ok();
    }

    let service_registry = ServiceRegistry::create();
    let device_settings_service_handle = Arc::new(Mutex::new(DeviceSettingsService::new()));
    service_registry.lock().await.register_service(device_settings_service_handle.clone());
    let hardware_power_statecontrol_service_handle =
        Arc::new(Mutex::new(HardwarePowerStatecontrolService::new()));
    service_registry
        .lock()
        .await
        .register_service(hardware_power_statecontrol_service_handle.clone());

    let env = EnvironmentBuilder::new(storage_factory)
        .service(ServiceRegistry::serve(service_registry.clone()))
        .settings(&[SettingType::System, SettingType::Account, SettingType::Power])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    let system_proxy = env.connect_to_service::<SystemMarker>().unwrap();

    let settings = system_proxy.watch2().await.expect("watch completed");

    assert_eq!(settings.mode, Some(STARTING_LOGIN_MODE));

    let mut system_settings = SystemSettings::empty();
    system_settings.mode = Some(CHANGED_LOGIN_MODE);
    system_proxy.set(system_settings).await.expect("set completed").expect("set successful");

    let settings = system_proxy.watch2().await.expect("watch completed");

    assert_eq!(settings.mode, Some(CHANGED_LOGIN_MODE));

    // Verify new value is in storage
    {
        let expected =
            SystemInfo { login_override_mode: SystemLoginOverrideMode::from(CHANGED_LOGIN_MODE) };
        let mut store_lock = store.lock().await;
        assert_eq!(expected, store_lock.get().await);
    }

    let device_settings_lock = device_settings_service_handle.lock().await;

    if let Some(account_reset_flag) =
        device_settings_lock.get_integer(FACTORY_RESET_FLAG.to_string())
    {
        assert_eq!(account_reset_flag, 1);
    } else {
        panic!("factory reset flag should have been set");
    }

    // Ensure reboot was requested by the controller
    assert!(hardware_power_statecontrol_service_handle
        .lock()
        .await
        .verify_action_sequence(vec![Action::Reboot]));
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_failed_reboot() {
    const STARTING_LOGIN_MODE: fidl_fuchsia_settings::LoginOverride =
        fidl_fuchsia_settings::LoginOverride::AutologinGuest;
    const CHANGED_LOGIN_MODE: fidl_fuchsia_settings::LoginOverride =
        fidl_fuchsia_settings::LoginOverride::AuthProvider;

    let storage_factory = InMemoryStorageFactory::create();
    let store = storage_factory
        .lock()
        .await
        .get_device_storage::<SystemInfo>(StorageAccessContext::Test, CONTEXT_ID);

    // Write out initial value to storage.
    {
        let initial_value =
            SystemInfo { login_override_mode: SystemLoginOverrideMode::from(STARTING_LOGIN_MODE) };
        let mut store_lock = store.lock().await;
        store_lock.write(&initial_value, false).await.ok();
    }

    let service_registry = ServiceRegistry::create();
    let device_settings_service_handle = Arc::new(Mutex::new(DeviceSettingsService::new()));
    service_registry.lock().await.register_service(device_settings_service_handle.clone());
    let hardware_power_statecontrol_service_handle =
        Arc::new(Mutex::new(HardwarePowerStatecontrolService::new()));
    hardware_power_statecontrol_service_handle
        .lock()
        .await
        .plan_action_response(Action::Reboot, Response::Fail);
    service_registry
        .lock()
        .await
        .register_service(hardware_power_statecontrol_service_handle.clone());

    let env = EnvironmentBuilder::new(storage_factory)
        .service(ServiceRegistry::serve(service_registry.clone()))
        .settings(&[SettingType::System, SettingType::Account, SettingType::Power])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    let system_proxy = env.connect_to_service::<SystemMarker>().unwrap();

    let settings = system_proxy.watch2().await.expect("watch completed");

    assert_eq!(settings.mode, Some(STARTING_LOGIN_MODE));

    let mut system_settings = SystemSettings::empty();
    system_settings.mode = Some(CHANGED_LOGIN_MODE);
    assert!(
        matches!(
            system_proxy.set(system_settings).await.expect("set completed"),
            Err(SettingsError::Failed)
        ),
        "set should have failed"
    );

    let settings = system_proxy.watch2().await.expect("watch completed");

    assert_eq!(settings.mode, Some(CHANGED_LOGIN_MODE));

    // Verify new value is in storage
    {
        let expected =
            SystemInfo { login_override_mode: SystemLoginOverrideMode::from(CHANGED_LOGIN_MODE) };
        let mut store_lock = store.lock().await;
        assert_eq!(expected, store_lock.get().await);
    }

    let device_settings_lock = device_settings_service_handle.lock().await;

    if let Some(account_reset_flag) =
        device_settings_lock.get_integer(FACTORY_RESET_FLAG.to_string())
    {
        assert_eq!(account_reset_flag, 1);
    } else {
        panic!("factory reset flag should have been set");
    }

    // Ensure reboot was not completed by the controller (note the not)
    assert!(hardware_power_statecontrol_service_handle.lock().await.verify_action_sequence(vec![]));
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_channel_failure_watch() {
    let system_service = create_system_test_env_with_failures().await;
    let result = system_service.watch().await.ok();
    assert_eq!(result, Some(Err(SettingsError::Failed)));
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_channel_failure_watch2() {
    let system_service = create_system_test_env_with_failures().await;
    let result = system_service.watch2().await;
    assert!(result.is_err());
    assert_eq!(
        ClientChannelClosed(Status::INTERNAL).to_string(),
        result.err().unwrap().to_string()
    );
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_simultaneous_watch() {
    let factory = InMemoryStorageFactory::create();

    let env = EnvironmentBuilder::new(factory)
        .settings(&[SettingType::System])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    let system_service = env.connect_to_service::<SystemMarker>().unwrap();

    let settings =
        system_service.watch().await.expect("watch completed").expect("watch successful");
    let settings2 = system_service.watch2().await.expect("watch completed");
    assert_eq!(settings, settings2);
}
