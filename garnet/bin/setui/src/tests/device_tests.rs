// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    crate::create_fidl_service,
    crate::registry::device_storage::testing::*,
    crate::registry::service_context::ServiceContext,
    crate::switchboard::base::SettingType,
    fidl_fuchsia_settings::DeviceMarker,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
    std::sync::{Arc, RwLock},
};

const ENV_NAME: &str = "settings_service_device_test_environment";

/// Tests that the FIDL calls for the device service result in appropriate commands
/// sent to the switchboard.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_device() {
    let mut fs = ServiceFs::new();

    create_fidl_service(
        fs.root_dir(),
        [SettingType::Device].iter().cloned().collect(),
        Arc::new(RwLock::new(ServiceContext::new(None))),
        Box::new(InMemoryStorageFactory::create()),
    );

    let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
    fasync::spawn(fs.collect());

    let _device_proxy = env.connect_to_service::<DeviceMarker>().expect("connected to service");

    // Nothing to test yet
    // TODO (go/fxb/36349): Add tests along with service implementation
}
