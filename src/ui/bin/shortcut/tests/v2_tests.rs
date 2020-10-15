// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

//! Test cases for input2-related shortcut services.

use {
    anyhow::Error, fidl_fuchsia_ui_input2 as ui_input, fidl_fuchsia_ui_shortcut as ui_shortcut,
    fuchsia_async as fasync, futures::future::join,
};

use crate::test_helpers::{ManagerService, RegistryService, ShortcutBuilder};

static TEST_SHORTCUT_ID: u32 = 123;
static TEST_SHORTCUT_2_ID: u32 = 321;

#[fasync::run_singlethreaded(test)]
async fn test_as_client() -> Result<(), Error> {
    let mut registry_service = RegistryService::new().await?;
    let manager_service = ManagerService::new().await?;

    // Set shortcut for either LEFT_SHIFT or RIGHT_SHIFT + A.
    let shortcut = ShortcutBuilder::new()
        .set_id(TEST_SHORTCUT_ID)
        .set_modifiers(ui_input::Modifiers::Shift)
        .set_key(ui_input::Key::A)
        .build();
    registry_service.register_shortcut(shortcut).await;

    // Set shortcut for RIGHT_CONTROL + B.
    let shortcut = ShortcutBuilder::new()
        .set_modifiers(ui_input::Modifiers::RightControl)
        .set_key(ui_input::Key::B)
        .build();
    registry_service.register_shortcut(shortcut).await;

    // Process key event that *does not* trigger a shortcut.
    let was_handled = manager_service
        .press_key2(ui_input::Key::A, None)
        .await
        .expect("handle_key not activating a shortcut");
    assert_eq!(false, was_handled);

    // Press a key that triggers a shortcut.
    let was_handled = join(
        manager_service.press_key2(
            ui_input::Key::A,
            Some(
                ui_input::Modifiers::Shift
                    | ui_input::Modifiers::LeftShift
                    | ui_input::Modifiers::CapsLock,
            ),
        ),
        registry_service.handle_shortcut_activation(|id| {
            assert_eq!(id, TEST_SHORTCUT_ID);
            true
        }),
    )
    .await
    .0
    .expect("handle_key_event true");
    assert_eq!(true, was_handled);

    // LEFT_CONTROL + B should *not* trigger the shortcut.
    let was_handled = manager_service
        .press_key2(
            ui_input::Key::B,
            Some(ui_input::Modifiers::LeftControl | ui_input::Modifiers::Control),
        )
        .await
        .expect("handle_key_event false for left_control");
    assert_eq!(false, was_handled);

    // RIGHT_CONTROL + B should trigger the shortcut.
    let was_handled = join(
        manager_service.press_key2(
            ui_input::Key::B,
            Some(ui_input::Modifiers::RightControl | ui_input::Modifiers::Control),
        ),
        registry_service.handle_shortcut_activation(|_| true),
    )
    .await
    .0
    .expect("handle_key_event true for right_control");
    assert_eq!(true, was_handled);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_modifiers_not_activated_on_release() -> Result<(), Error> {
    let mut registry_service = RegistryService::new().await?;
    let manager_service = ManagerService::new().await?;

    // Set modifier-only shortcut for LEFT_SHIFT
    let shortcut = ShortcutBuilder::new()
        .set_id(TEST_SHORTCUT_ID)
        .set_modifiers(ui_input::Modifiers::LeftShift)
        .set_trigger(ui_shortcut::Trigger::KeyPressedAndReleased)
        .build();
    registry_service.register_shortcut(shortcut).await;

    // Set shortcut for LEFT_SHIFT + A.
    let shortcut = ShortcutBuilder::new()
        .set_id(TEST_SHORTCUT_2_ID)
        .set_modifiers(ui_input::Modifiers::LeftShift)
        .set_key(ui_input::Key::C)
        .build();
    registry_service.register_shortcut(shortcut).await;

    let was_handled = manager_service
        .press_key2(
            ui_input::Key::LeftShift,
            Some(ui_input::Modifiers::LeftShift | ui_input::Modifiers::Shift),
        )
        .await
        .expect("Press LeftShift");
    assert_eq!(false, was_handled);

    let was_handled = join(
        manager_service.press_key2(
            ui_input::Key::C,
            Some(ui_input::Modifiers::LeftShift | ui_input::Modifiers::Shift),
        ),
        registry_service.handle_shortcut_activation(|id| {
            assert_eq!(id, TEST_SHORTCUT_2_ID);
            true
        }),
    )
    .await
    .0
    .expect("handle_key_event left_shift + C");
    assert_eq!(true, was_handled);

    let was_handled = manager_service
        .release_key2(
            ui_input::Key::LeftShift,
            Some(ui_input::Modifiers::LeftShift | ui_input::Modifiers::Shift),
        )
        .await
        .expect("Release LeftShift");
    assert_eq!(false, was_handled);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_modifiers_activated_on_release() -> Result<(), Error> {
    let mut registry_service = RegistryService::new().await?;
    let manager_service = ManagerService::new().await?;

    // Set shortcut for LEFT_META
    let shortcut = ShortcutBuilder::new()
        .set_id(TEST_SHORTCUT_ID)
        .set_modifiers(ui_input::Modifiers::LeftMeta)
        .set_trigger(ui_shortcut::Trigger::KeyPressedAndReleased)
        .build();
    registry_service.register_shortcut(shortcut).await;

    // Set shortcut for LEFT_META + Q.
    let shortcut = ShortcutBuilder::new()
        .set_id(TEST_SHORTCUT_2_ID)
        .set_modifiers(ui_input::Modifiers::LeftMeta)
        .set_key(ui_input::Key::Q)
        .build();
    registry_service.register_shortcut(shortcut).await;

    let was_handled =
        manager_service.press_key2(ui_input::Key::LeftMeta, None).await.expect("Press LeftMeta");
    assert_eq!(false, was_handled);

    let was_handled = join(
        manager_service.release_key2(
            ui_input::Key::LeftMeta,
            Some(ui_input::Modifiers::LeftMeta | ui_input::Modifiers::Meta),
        ),
        registry_service.handle_shortcut_activation(|id| {
            assert_eq!(id, TEST_SHORTCUT_ID);
            true
        }),
    )
    .await
    .0
    .expect("handle_key_event true for left_meta");
    assert_eq!(true, was_handled);

    Ok(())
}
