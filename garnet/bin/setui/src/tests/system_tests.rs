// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    crate::create_environment,
    crate::registry::device_storage::testing::*,
    crate::registry::device_storage::DeviceStorageFactory,
    crate::service_context::ServiceContext,
    crate::switchboard::base::{SettingType, SystemInfo, SystemLoginOverrideMode},
    crate::tests::fakes::device_admin_service::{Action, DeviceAdminService},
    crate::tests::fakes::device_settings_service::DeviceSettingsService,
    crate::tests::fakes::service_registry::ServiceRegistry,
    fidl_fuchsia_settings::*,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::lock::Mutex,
    futures::prelude::*,
    std::sync::Arc,
};

const ENV_NAME: &str = "settings_service_system_test_environment";
const FACTORY_RESET_FLAG: &str = "FactoryReset";

#[fuchsia_async::run_singlethreaded(test)]
async fn test_system() {
    const STARTING_LOGIN_MODE: fidl_fuchsia_settings::LoginOverride =
        fidl_fuchsia_settings::LoginOverride::AutologinGuest;
    const CHANGED_LOGIN_MODE: fidl_fuchsia_settings::LoginOverride =
        fidl_fuchsia_settings::LoginOverride::AuthProvider;
    let mut fs = ServiceFs::new();

    let storage_factory = Box::new(InMemoryStorageFactory::create());
    let store = storage_factory.get_store::<SystemInfo>();

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
    let device_admin_service_handle = Arc::new(Mutex::new(DeviceAdminService::new()));
    service_registry.lock().await.register_service(device_admin_service_handle.clone());

    assert!(create_environment(
        fs.root_dir(),
        [SettingType::System].iter().cloned().collect(),
        vec![],
        ServiceContext::create(ServiceRegistry::serve(service_registry)),
        storage_factory,
    )
    .await
    .unwrap()
    .is_ok());

    let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
    fasync::spawn(fs.collect());

    let system_proxy = env.connect_to_service::<SystemMarker>().unwrap();

    let settings = system_proxy.watch().await.expect("watch completed").expect("watch successful");

    assert_eq!(settings.mode, Some(STARTING_LOGIN_MODE));

    let mut system_settings = SystemSettings::empty();
    system_settings.mode = Some(CHANGED_LOGIN_MODE);
    system_proxy.set(system_settings).await.expect("set completed").expect("set successful");

    let settings = system_proxy.watch().await.expect("watch completed").expect("watch successful");

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
    assert!(device_admin_service_handle
        .lock()
        .await
        .verify_action_sequence([Action::Reboot].to_vec()));
}
