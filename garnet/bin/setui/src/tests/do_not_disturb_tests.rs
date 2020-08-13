// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    crate::handler::device_storage::testing::*,
    crate::switchboard::base::{DoNotDisturbInfo, SettingType},
    crate::EnvironmentBuilder,
    fidl_fuchsia_settings::{DoNotDisturbMarker, DoNotDisturbProxy, DoNotDisturbSettings},
};

const ENV_NAME: &str = "settings_service_do_not_disturb_test_environment";
const CONTEXT_ID: u64 = 0;

#[fuchsia_async::run_until_stalled(test)]
async fn test_do_not_disturb() {
    let storage_factory = InMemoryStorageFactory::create();
    let store = storage_factory
        .lock()
        .await
        .get_device_storage::<DoNotDisturbInfo>(StorageAccessContext::Test, CONTEXT_ID);

    // Prepopulate initial value.
    {
        let mut store_lock = store.lock().await;
        assert!(store_lock.write(&DoNotDisturbInfo::new(true, false), false).await.is_ok());
    }

    let env = EnvironmentBuilder::new(storage_factory)
        .settings(&[SettingType::DoNotDisturb])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    let dnd_proxy = env.connect_to_service::<DoNotDisturbMarker>().expect("connected to service");

    verify_dnd_watch(&dnd_proxy, DoNotDisturbInfo::new(true, false)).await;

    set_dnd(&dnd_proxy, None, Some(true)).await;

    verify_dnd_watch(&dnd_proxy, DoNotDisturbInfo::new(true, true)).await;

    set_dnd(&dnd_proxy, Some(false), None).await;

    verify_dnd_watch(&dnd_proxy, DoNotDisturbInfo::new(false, true)).await;

    set_dnd(&dnd_proxy, Some(false), Some(false)).await;

    verify_dnd_watch(&dnd_proxy, DoNotDisturbInfo::new(false, false)).await;

    set_dnd(&dnd_proxy, Some(true), Some(true)).await;

    verify_dnd_watch(&dnd_proxy, DoNotDisturbInfo::new(true, true)).await;
}

async fn set_dnd(
    dnd_proxy: &DoNotDisturbProxy,
    user_dnd: Option<bool>,
    night_mode_dnd: Option<bool>,
) {
    let mut dnd_settings = DoNotDisturbSettings::empty();
    if let Some(u) = user_dnd {
        dnd_settings.user_initiated_do_not_disturb = Some(u);
    }
    if let Some(n) = night_mode_dnd {
        dnd_settings.night_mode_initiated_do_not_disturb = Some(n);
    }
    dnd_proxy.set(dnd_settings).await.expect("set completed").expect("set successful");
}

async fn verify_dnd_watch(dnd_proxy: &DoNotDisturbProxy, expected_dnd: DoNotDisturbInfo) {
    let settings = dnd_proxy.watch().await.expect("watch completed");
    assert_eq!(settings.user_initiated_do_not_disturb, expected_dnd.user_dnd);
    assert_eq!(settings.night_mode_initiated_do_not_disturb, expected_dnd.night_mode_dnd);
}
