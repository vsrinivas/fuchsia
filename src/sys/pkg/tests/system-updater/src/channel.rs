// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

#[fasync::run_singlethreaded(test)]
async fn promotes_target_channel_as_current_channel() {
    let env = TestEnv::new();

    env.set_target_channel("target-channel");

    env.resolver
        .register_package("update", "upd4t3")
        .add_file(
            "packages",
            "system_image/0=42ade6f4fd51636f70c68811228b4271ed52c4eb9a647305123b4f4d0741f296\n",
        )
        .add_file("zbi", "fake zbi");

    env.run_system_updater(SystemUpdaterArgs {
        initiator: "manual",
        target: "m3rk13",
        update: None,
        reboot: None,
        skip_recovery: None,
    })
    .await
    .unwrap();

    env.verify_current_channel(Some(b"target-channel"));

    // even if current channel already exists.

    env.set_target_channel("target-channel-2");

    env.run_system_updater(SystemUpdaterArgs {
        initiator: "manual",
        target: "m3rk13",
        update: None,
        reboot: None,
        skip_recovery: None,
    })
    .await
    .unwrap();

    env.verify_current_channel(Some(b"target-channel-2"));
}

#[fasync::run_singlethreaded(test)]
async fn succeeds_even_if_target_channel_does_not_exist() {
    let env = TestEnv::new();

    env.resolver
        .register_package("update", "upd4t3")
        .add_file(
            "packages",
            "system_image/0=42ade6f4fd51636f70c68811228b4271ed52c4eb9a647305123b4f4d0741f296\n",
        )
        .add_file("zbi", "fake zbi");

    env.run_system_updater(SystemUpdaterArgs {
        initiator: "manual",
        target: "m3rk13",
        update: None,
        reboot: None,
        skip_recovery: None,
    })
    .await
    .unwrap();

    env.verify_current_channel(None);
}

#[fasync::run_singlethreaded(test)]
async fn does_not_promote_target_channel_on_failure() {
    let env =
        TestEnv::builder().paver_service(|builder| builder.call_hook(|_| Status::INTERNAL)).build();

    env.set_target_channel("target-channel");

    env.resolver
        .register_package("update", "upd4t3")
        .add_file(
            "packages",
            "system_image/0=42ade6f4fd51636f70c68811228b4271ed52c4eb9a647305123b4f4d0741f296\n",
        )
        .add_file("zbi", "fake zbi");

    let result = env
        .run_system_updater(SystemUpdaterArgs {
            initiator: "manual",
            target: "m3rk13",
            update: None,
            reboot: None,
            skip_recovery: None,
        })
        .await;
    assert!(result.is_err(), "system updater succeeded when it should fail");

    env.verify_current_channel(None);
}
