// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::KeyboardTest;
use fidl_fuchsia_input::*;
use fidl_fuchsia_settings::*;
mod common;

#[fuchsia::test]
async fn test_keyboard() {
    let instance = KeyboardTest::create_realm().await.expect("setting up test realm");
    let proxy = KeyboardTest::connect_to_keyboardmarker(&instance);

    let initial_value = KeyboardSettings {
        keymap: Some(KeymapId::UsQwerty),
        autorepeat: None,
        ..KeyboardSettings::EMPTY
    };

    // Ensure retrieved value matches the expected default value.
    let settings = proxy.watch().await.expect("watch completed");
    assert_eq!(settings.keymap, initial_value.keymap);
    assert_eq!(settings.autorepeat, None);

    let changed_value = KeyboardSettings {
        keymap: Some(KeymapId::UsDvorak),
        autorepeat: Some(Autorepeat { delay: 2, period: 1 }),
        ..KeyboardSettings::EMPTY
    };

    // Ensure setting interface propagates correctly.
    proxy.set(changed_value.clone()).await.expect("set completed").expect("set successful");

    // Ensure retrieved value matches set value.
    let settings = proxy.watch().await.expect("watch completed");
    assert_eq!(settings.keymap, changed_value.keymap);
    assert_eq!(settings.autorepeat, changed_value.autorepeat);

    let _ = instance.destroy().await;
}

#[fuchsia::test]
async fn test_keyboard_clean_autorepeat_and_unset_keymap() {
    let instance = KeyboardTest::create_realm().await.expect("setting up test realm");
    let proxy = KeyboardTest::connect_to_keyboardmarker(&instance);

    let initial_value = KeyboardSettings {
        keymap: Some(KeymapId::UsQwerty),
        autorepeat: Some(Autorepeat { delay: 1, period: 2 }),
        ..KeyboardSettings::EMPTY
    };

    // Setup initial values.
    proxy.set(initial_value.clone()).await.expect("set completed").expect("set successful");

    // Ensure retrieved value matches set value.
    let settings = proxy.watch().await.expect("watch completed");
    assert_eq!(settings.keymap, initial_value.keymap);
    assert_eq!(settings.autorepeat, initial_value.autorepeat);

    // Ensure setting interface propagates correctly.
    let mut keyboard_settings = fidl_fuchsia_settings::KeyboardSettings::EMPTY;
    keyboard_settings.keymap = None;
    keyboard_settings.autorepeat = Some(fidl_fuchsia_settings::Autorepeat { delay: 0, period: 0 });
    proxy.set(keyboard_settings).await.expect("set completed").expect("set successful");

    let changed_value = KeyboardSettings {
        keymap: Some(KeymapId::UsQwerty),
        autorepeat: None,
        ..KeyboardSettings::EMPTY
    };

    // Ensure retrieved value matches set value.
    let settings = proxy.watch().await.expect("watch completed");
    assert_eq!(settings.keymap, changed_value.keymap);
    assert_eq!(settings.autorepeat, changed_value.autorepeat);

    let _ = instance.destroy().await;
}
