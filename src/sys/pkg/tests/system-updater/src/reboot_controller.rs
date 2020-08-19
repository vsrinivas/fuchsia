// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*,
    fidl_fuchsia_update_installer_ext::{start_update, StateId},
    pretty_assertions::assert_eq,
};

#[fasync::run_singlethreaded(test)]
async fn reboot_controller_detach_causes_deferred_reboot() {
    let env = TestEnv::builder().build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake zbi");

    // Start the system update.
    let (reboot_proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
    let attempt = start_update(
        &UPDATE_PKG_URL.parse().unwrap(),
        default_options(),
        env.installer_proxy(),
        Some(server_end),
    )
    .await
    .unwrap();

    // When we call detach, we should observe DeferReboot at the end.
    let () = reboot_proxy.detach().unwrap();
    assert_eq!(
        attempt.map(|res| res.unwrap()).collect::<Vec<_>>().await.last().unwrap().id(),
        StateId::DeferReboot
    );

    // Verify we didn't make a reboot call.
    assert!(!env.take_interactions().contains(&Reboot));
}

#[fasync::run_singlethreaded(test)]
async fn reboot_controller_unblock_causes_reboot() {
    let env = TestEnv::builder().build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake zbi");

    // Start the system update.
    let (reboot_proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
    let attempt = start_update(
        &UPDATE_PKG_URL.parse().unwrap(),
        default_options(),
        env.installer_proxy(),
        Some(server_end),
    )
    .await
    .unwrap();

    // When we call unblock, we should observe Reboot at the end.
    let () = reboot_proxy.unblock().unwrap();
    assert_eq!(
        attempt.map(|res| res.unwrap()).collect::<Vec<_>>().await.last().unwrap().id(),
        StateId::Reboot
    );

    // Verify we made a reboot call.
    assert_eq!(env.take_interactions().last().unwrap(), &Reboot);
}

#[fasync::run_singlethreaded(test)]
async fn reboot_controller_dropped_causes_reboot() {
    let env = TestEnv::builder().build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake zbi");

    // Start the system update.
    let (reboot_proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
    let attempt = start_update(
        &UPDATE_PKG_URL.parse().unwrap(),
        default_options(),
        env.installer_proxy(),
        Some(server_end),
    )
    .await
    .unwrap();

    // When we drop the reboot controller, we should observe Reboot at the end.
    drop(reboot_proxy);
    assert_eq!(
        attempt.map(|res| res.unwrap()).collect::<Vec<_>>().await.last().unwrap().id(),
        StateId::Reboot
    );

    // Verify we made a reboot call.
    assert_eq!(env.take_interactions().last().unwrap(), &Reboot);
}
