// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::handler::device_storage::testing::InMemoryStorageFactory,
    crate::ingress::fidl::Interface, crate::EnvironmentBuilder,
    fidl_fuchsia_settings::DeviceMarker, std::sync::Arc,
};

const ENV_NAME: &str = "settings_service_device_test_environment";

/// Tests that the FIDL calls for the device service result in appropriate commands
/// sent to the service.
#[fuchsia_async::run_until_stalled(test)]
async fn test_device() {
    let env = EnvironmentBuilder::new(Arc::new(InMemoryStorageFactory::new()))
        .fidl_interfaces(&[Interface::Device])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    let device_proxy = env.connect_to_protocol::<DeviceMarker>().expect("connected to service");

    let settings = device_proxy.watch().await.expect("watch completed");

    // The tag could be in different formats based on whether it's a release build or not,
    // just check that it is nonempty.
    #[allow(clippy::bool_assert_comparison)]
    match settings.build_tag {
        Some(tag) => assert_eq!(tag.is_empty(), false),
        None => panic!("Build tag not loaded from file"),
    }
}
