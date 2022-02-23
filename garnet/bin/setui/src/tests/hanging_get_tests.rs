// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::storage::device_storage::testing::InMemoryStorageFactory;
use crate::do_not_disturb::types::DoNotDisturbInfo;
use crate::ingress::fidl::Interface;
use crate::EnvironmentBuilder;
use fidl_fuchsia_settings::{DoNotDisturbMarker, DoNotDisturbSettings};
use fuchsia_component::server::NestedEnvironment;
use std::sync::Arc;

const ENV_NAME: &str = "hanging_get_test_environment";

#[fuchsia_async::run_until_stalled(test)]
async fn test_multiple_watches() {
    // Prepopulate initial value
    let initial_data = DoNotDisturbInfo::new(true, false);
    let storage_factory = InMemoryStorageFactory::with_initial_data(&initial_data);

    let env = EnvironmentBuilder::new(Arc::new(storage_factory))
        .fidl_interfaces(&[Interface::DoNotDisturb])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    let dnd_proxy = env.connect_to_protocol::<DoNotDisturbMarker>().unwrap();

    verify(dnd_proxy.watch().await, DoNotDisturbInfo::new(true, false));

    // The following calls should succeed but not return as no value is available.
    let second_watch = dnd_proxy.watch();
    let third_watch = dnd_proxy.watch();

    set_dnd(env, None, Some(true)).await;

    verify(second_watch.await, DoNotDisturbInfo::new(true, true));
    verify(third_watch.await, DoNotDisturbInfo::new(true, true));
}

fn verify(
    watch_result: Result<fidl_fuchsia_settings::DoNotDisturbSettings, fidl::Error>,
    expected_dnd: DoNotDisturbInfo,
) {
    let dnd_values = watch_result.expect("watch completed");
    assert_eq!(dnd_values.user_initiated_do_not_disturb, expected_dnd.user_dnd);
    assert_eq!(dnd_values.night_mode_initiated_do_not_disturb, expected_dnd.night_mode_dnd);
}

async fn set_dnd(env: NestedEnvironment, user_dnd: Option<bool>, night_mode_dnd: Option<bool>) {
    let mut dnd_settings = DoNotDisturbSettings::EMPTY;
    dnd_settings.user_initiated_do_not_disturb = user_dnd;
    dnd_settings.night_mode_initiated_do_not_disturb = night_mode_dnd;

    let dnd_proxy = env.connect_to_protocol::<DoNotDisturbMarker>().unwrap();
    dnd_proxy.set(dnd_settings).await.expect("set completed").expect("set successful");
}
