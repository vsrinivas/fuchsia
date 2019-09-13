// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    crate::create_fidl_service, crate::registry::device_storage::testing::*,
    crate::registry::service_context::ServiceContext, crate::switchboard::base::SettingType,
    fidl_fuchsia_settings::*, fuchsia_async as fasync, fuchsia_component::server::ServiceFs,
    futures::prelude::*, parking_lot::RwLock, std::sync::Arc,
};

const ENV_NAME: &str = "settings_service_system_test_environment";

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

    let settings = system_proxy.watch().await.expect("watch completed").expect("watch successful");

    assert_eq!(settings.mode, Some(STARTING_LOGIN_MODE));

    let mut system_settings = SystemSettings::empty();
    system_settings.mode = Some(CHANGED_LOGIN_MODE);
    system_proxy.set(system_settings).await.expect("set completed").expect("set successful");

    let settings = system_proxy.watch().await.expect("watch completed").expect("watch successful");

    assert_eq!(settings.mode, Some(CHANGED_LOGIN_MODE));
}
