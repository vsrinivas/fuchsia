// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::lib::NightModeTest;
mod lib;

struct NightModeInfo {
    pub night_mode_enabled: Option<bool>,
}

#[fuchsia::test]
async fn test_night_mode() {
    let instance = NightModeTest::create_realm().await.expect("setting up test realm");
    let initial_value = NightModeInfo { night_mode_enabled: None };
    let changed_value = NightModeInfo { night_mode_enabled: Some(true) };
    let night_mode_service = NightModeTest::connect_to_night_mode_marker(&instance);

    // Ensure retrieved value matches set value.
    let settings = night_mode_service.watch().await.expect("watch completed");
    assert_eq!(settings.night_mode_enabled, initial_value.night_mode_enabled);

    // Ensure setting interface propagates correctly.
    let mut night_mode_settings = fidl_fuchsia_settings::NightModeSettings::EMPTY;
    night_mode_settings.night_mode_enabled = Some(true);
    night_mode_service
        .set(night_mode_settings)
        .await
        .expect("set completed")
        .expect("set successful");

    // Ensure retrieved value matches set value.
    let settings = night_mode_service.watch().await.expect("watch completed");
    assert_eq!(settings.night_mode_enabled, changed_value.night_mode_enabled);

    let _ = instance.destroy();
}
