// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    crate::create_fidl_service,
    crate::registry::device_storage::{testing::*, DeviceStorageFactory},
    crate::service_context::ServiceContext,
    crate::switchboard::base::{DoNotDisturbInfo, SettingType},
    fidl_fuchsia_settings::{DoNotDisturbMarker, DoNotDisturbProxy, DoNotDisturbSettings},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
    parking_lot::RwLock,
    std::sync::Arc,
};

const ENV_NAME: &str = "settings_service_do_not_disturb_test_environment";

#[fuchsia_async::run_singlethreaded(test)]
async fn test_do_not_disturb() {
    let mut fs = ServiceFs::new();

    let storage_factory = Box::new(InMemoryStorageFactory::create());
    let store = storage_factory.get_store::<DoNotDisturbInfo>();

    // Prepopulate initial value.
    {
        let mut store_lock = store.lock().await;
        assert!(store_lock
            .write(&DoNotDisturbInfo { user_dnd: true, night_mode_dnd: false }, false)
            .await
            .is_ok());
    }

    create_fidl_service(
        fs.root_dir(),
        [SettingType::DoNotDisturb].iter().cloned().collect(),
        Arc::new(RwLock::new(ServiceContext::new(None))),
        storage_factory,
    );

    let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
    fasync::spawn(fs.collect());

    let dnd_proxy = env.connect_to_service::<DoNotDisturbMarker>().expect("connected to service");

    verify_dnd_watch(&dnd_proxy, DoNotDisturbInfo { user_dnd: true, night_mode_dnd: false }).await;

    set_dnd(&dnd_proxy, "NIGHT_MODE_DND", true).await;

    verify_dnd_watch(&dnd_proxy, DoNotDisturbInfo { user_dnd: true, night_mode_dnd: true }).await;

    set_dnd(&dnd_proxy, "USER_DND", false).await;

    verify_dnd_watch(&dnd_proxy, DoNotDisturbInfo { user_dnd: false, night_mode_dnd: true }).await;

    set_dnd(&dnd_proxy, "NIGHT_MODE_DND", false).await;

    verify_dnd_watch(&dnd_proxy, DoNotDisturbInfo { user_dnd: false, night_mode_dnd: false }).await;
}

async fn set_dnd(dnd_proxy: &DoNotDisturbProxy, setting: &str, value: bool) {
    let mut dnd_settings = DoNotDisturbSettings::empty();
    match setting {
        "USER_DND" => dnd_settings.user_initiated_do_not_disturb = Some(value),
        "NIGHT_MODE_DND" => dnd_settings.night_mode_initiated_do_not_disturb = Some(value),
        _ => panic!("Attempted to set an unrecognized dnd attribute: {}", setting),
    };
    dnd_proxy.set(dnd_settings).await.expect("set completed").expect("set successful");
}

async fn verify_dnd_watch(dnd_proxy: &DoNotDisturbProxy, expected_dnd: DoNotDisturbInfo) {
    let settings = dnd_proxy.watch().await.expect("watch completed");
    assert_eq!(settings.user_initiated_do_not_disturb, Some(expected_dnd.user_dnd));
    assert_eq!(settings.night_mode_initiated_do_not_disturb, Some(expected_dnd.night_mode_dnd));
}
