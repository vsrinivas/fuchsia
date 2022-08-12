// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fidl_examples_routing_echo as fecho, fidl_fuchsia_sys2 as fsys,
    fuchsia_component::client::{connect_to_protocol, connect_to_protocol_at_path},
    fuchsia_fs::directory::readdir,
    tracing::info,
};

pub async fn expect_dir_listing(path: &str, mut expected_listing: Vec<&str>) {
    info!("{} should contain {:?}", path, expected_listing);
    let dir_proxy =
        fuchsia_fs::directory::open_in_namespace(path, fuchsia_fs::OpenFlags::RIGHT_READABLE)
            .unwrap();
    let actual_listing = readdir(&dir_proxy).await.unwrap();

    for actual_entry in &actual_listing {
        let index = expected_listing
            .iter()
            .position(|expected_entry| *expected_entry == actual_entry.name)
            .unwrap();
        expected_listing.remove(index);
    }

    assert_eq!(expected_listing.len(), 0);
}

pub async fn expect_dir_listing_with_optionals(
    path: &str,
    mut must_have: Vec<&str>,
    mut may_have: Vec<&str>,
) {
    info!("{} should contain {:?}", path, must_have);
    info!("{} may contain {:?}", path, may_have);
    let dir_proxy =
        fuchsia_fs::directory::open_in_namespace(path, fuchsia_fs::OpenFlags::RIGHT_READABLE)
            .unwrap();
    let mut actual_listing = readdir(&dir_proxy).await.unwrap();

    actual_listing.retain(|actual_entry| {
        if let Some(index) =
            must_have.iter().position(|must_entry| *must_entry == actual_entry.name)
        {
            must_have.remove(index);
            return false;
        }
        if let Some(index) = may_have.iter().position(|may_entry| *may_entry == actual_entry.name) {
            may_have.remove(index);
            return false;
        }
        return true;
    });

    // All must_haves are present
    assert_eq!(must_have.len(), 0);
    // No actuals are unexpected
    assert_eq!(actual_listing.len(), 0);
}

pub async fn expect_file_content(path: &str, expected_file_content: &str) {
    info!("{} should contain \"{}\"", path, expected_file_content);
    let actual_file_content = fuchsia_fs::file::read_in_namespace_to_string(path).await.unwrap();
    assert_eq!(expected_file_content, actual_file_content);
}

pub async fn expect_echo_service(path: &str) {
    info!("{} should be an Echo service", path);
    let echo_proxy = connect_to_protocol_at_path::<fecho::EchoMarker>(path).unwrap();
    let result = echo_proxy.echo_string(Some("hippos")).await.unwrap().unwrap();
    assert_eq!(&result, "hippos");
}

pub async fn resolve_component(relative_moniker: &str, expect_success: bool) {
    info!("Attempting to resolve {}", relative_moniker);
    let lifecycle_controller_proxy =
        connect_to_protocol::<fsys::LifecycleControllerMarker>().unwrap();
    let result = lifecycle_controller_proxy.resolve(relative_moniker).await.unwrap();
    if expect_success {
        result.unwrap();
    } else {
        result.unwrap_err();
    }
}

pub async fn start_component(relative_moniker: &str, expect_success: bool) {
    info!("Attempting to start {}", relative_moniker);
    let lifecycle_controller_proxy =
        connect_to_protocol::<fsys::LifecycleControllerMarker>().unwrap();
    let result = lifecycle_controller_proxy.start(relative_moniker).await.unwrap();
    if expect_success {
        result.unwrap();
    } else {
        result.unwrap_err();
    }
}

pub async fn stop_component(relative_moniker: &str, expect_success: bool) {
    info!("Attempting to stop {}", relative_moniker);
    let lifecycle_controller_proxy =
        connect_to_protocol::<fsys::LifecycleControllerMarker>().unwrap();
    let result = lifecycle_controller_proxy.stop(relative_moniker, false).await.unwrap();
    if expect_success {
        result.unwrap();
    } else {
        result.unwrap_err();
    }
}
