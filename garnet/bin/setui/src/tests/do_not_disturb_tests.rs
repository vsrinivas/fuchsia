// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    crate::create_fidl_service,
    crate::registry::device_storage::testing::*,
    crate::registry::service_context::ServiceContext,
    crate::switchboard::base::SettingType,
    fidl_fuchsia_settings::*,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
    std::sync::{Arc, RwLock},
};

const ENV_NAME: &str = "settings_service_do_not_disturb_test_environment";

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

    let dnd_proxy = env.connect_to_service::<DoNotDisturbMarker>().expect("connected to service");

    let mut dnd_settings = DoNotDisturbSettings::empty();
    dnd_settings.user_initiated_do_not_disturb = Some(true);

    // Set doesn't do anything yet, just make sure it doesn't panic
    dnd_proxy.set(dnd_settings).await.expect("set completed").expect("set successful");
}
