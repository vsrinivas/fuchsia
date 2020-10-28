// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

#[fasync::run_singlethreaded(test)]
async fn promotes_target_channel_as_current_channel() {
    let env = TestEnv::builder().build();

    env.set_target_channel("target-channel");

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake zbi");

    env.run_update().await.unwrap();

    env.verify_current_channel(Some(b"target-channel"));

    // even if current channel already exists.

    env.set_target_channel("target-channel-2");

    env.run_update().await.unwrap();

    env.verify_current_channel(Some(b"target-channel-2"));
}

#[fasync::run_singlethreaded(test)]
async fn succeeds_even_if_target_channel_does_not_exist() {
    let env = TestEnv::builder().build();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake zbi");

    env.run_update().await.unwrap();

    env.verify_current_channel(None);
}

#[fasync::run_singlethreaded(test)]
async fn does_not_promote_target_channel_on_failure() {
    let env =
        TestEnv::builder().paver_service(|builder| builder.call_hook(|_| Status::INTERNAL)).build();

    env.set_target_channel("target-channel");

    env.resolver
        .register_package("update", "upd4t3")
        .add_file("packages.json", make_packages_json([]))
        .add_file("zbi", "fake zbi");

    let result = env.run_update().await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    env.verify_current_channel(None);
}
