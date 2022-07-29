// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::HangingGetTest;
use fidl_fuchsia_settings::*;
mod common;

#[fuchsia::test]
async fn test_multiple_watches() {
    let instance = HangingGetTest::create_realm().await.expect("setting up test realm");
    let proxy = HangingGetTest::connect_to_dndmarker(&instance);

    // We expect the first watch to complete and return an initial value.
    let result = proxy.watch().await;
    assert!(matches!(result, Ok(_)));

    // Double-check that the initial setting is different from the one we're going to update it to.
    let initial_setting = result.expect("watch completed");
    let updated_setting = DoNotDisturbSettings {
        user_initiated_do_not_disturb: Some(true),
        night_mode_initiated_do_not_disturb: Some(true),
        ..DoNotDisturbSettings::EMPTY
    };
    assert_ne!(initial_setting, updated_setting);

    // The following call should succeed but not return as no value is available.
    let second_watch = proxy.watch();

    // Now we'll set a value which should cause the pending watch to complete.
    proxy.set(updated_setting.clone()).await.expect("set completed").expect("set successful");

    assert_eq!(second_watch.await.expect("watch completed"), updated_setting);

    let _ = instance.destroy().await;
}
