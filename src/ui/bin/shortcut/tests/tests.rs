// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use anyhow::{Context as _, Error};
use fidl::client::QueryResponseFut;
use fidl_fuchsia_ui_input2 as ui_input;
use fidl_fuchsia_ui_shortcut as ui_shortcut;
use fidl_fuchsia_ui_views as ui_views;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use fuchsia_zircon as zx;
use futures::future::join;
use futures::StreamExt;
use std::sync::Once;

static TEST_SHORTCUT_ID: u32 = 123;
static TEST_SHORTCUT_2_ID: u32 = 321;

static START: Once = Once::new();

struct ShortcutService {
    registry: ui_shortcut::RegistryProxy,
    manager: ui_shortcut::ManagerProxy,
    listener: ui_shortcut::ListenerRequestStream,
}

impl ShortcutService {
    async fn new() -> Result<ShortcutService, Error> {
        START.call_once(|| {
            fuchsia_syslog::init_with_tags(&["shortcut"])
                .expect("shortcut syslog init should not fail");
        });

        let registry = connect_to_service::<ui_shortcut::RegistryMarker>()
            .context("Failed to connect to Shortcut registry service")?;

        let manager = connect_to_service::<ui_shortcut::ManagerMarker>()
            .context("Failed to connect to Shortcut manager service")?;

        let (listener_client_end, listener) =
            fidl::endpoints::create_request_stream::<ui_shortcut::ListenerMarker>()?;

        // Set listener and view ref.
        let (raw_event_pair, _) = zx::EventPair::create()?;
        let view_ref = &mut ui_views::ViewRef { reference: raw_event_pair };
        registry.set_view(view_ref, listener_client_end).expect("set_view");

        Ok(ShortcutService { registry, manager, listener })
    }

    fn press_key(
        &self,
        key: ui_input::Key,
        modifiers: Option<ui_input::Modifiers>,
    ) -> QueryResponseFut<bool> {
        // Process key event that triggers a shortcut.
        let event = ui_input::KeyEvent {
            key: Some(key),
            modifiers: modifiers,
            phase: Some(ui_input::KeyEventPhase::Pressed),
            physical_key: None,
            semantic_key: None,
        };

        self.manager.handle_key_event(event)
    }

    fn release_key(
        &self,
        key: ui_input::Key,
        modifiers: Option<ui_input::Modifiers>,
    ) -> QueryResponseFut<bool> {
        // Process key event that triggers a shortcut.
        let event = ui_input::KeyEvent {
            key: Some(key),
            modifiers: modifiers,
            phase: Some(ui_input::KeyEventPhase::Released),
            physical_key: None,
            semantic_key: None,
        };

        self.manager.handle_key_event(event)
    }

    async fn handle_shortcut_activation<HandleFunc>(&mut self, mut handler: HandleFunc)
    where
        HandleFunc: FnMut(u32) -> bool,
    {
        if let Some(Ok(ui_shortcut::ListenerRequest::OnShortcut { id, responder, .. })) =
            self.listener.next().await
        {
            responder.send(handler(id)).expect("responding from shortcut listener for shift")
        } else {
            panic!("Error from listener.next() on shift shortcut activation");
        };
    }
}

#[fasync::run_singlethreaded(test)]
async fn test_as_client() -> Result<(), Error> {
    let mut service = ShortcutService::new().await?;

    // Set shortcut for either LEFT_SHIFT or RIGHT_SHIFT + A.
    let shortcut = ui_shortcut::Shortcut {
        id: Some(TEST_SHORTCUT_ID),
        modifiers: Some(ui_input::Modifiers::Shift),
        key: Some(ui_input::Key::A),
        use_priority: None,
        trigger: None,
        key3: None,
        keys_required: None,
    };
    service.registry.register_shortcut(shortcut).await.expect("register_shortcut shift");

    // Set shortcut for RIGHT_CONTROL + B.
    let shortcut = ui_shortcut::Shortcut {
        id: None,
        modifiers: Some(ui_input::Modifiers::RightControl),
        key: Some(ui_input::Key::B),
        use_priority: None,
        trigger: None,
        key3: None,
        keys_required: None,
    };
    service.registry.register_shortcut(shortcut).await.expect("register_shortcut right_control");

    // Process key event that *does not* trigger a shortcut.
    let was_handled = service
        .press_key(ui_input::Key::A, None)
        .await
        .expect("handle_key not activating a shortcut");
    assert_eq!(false, was_handled);

    // Press a key that triggers a shortcut.
    let was_handled = join(
        service.press_key(
            ui_input::Key::A,
            Some(
                ui_input::Modifiers::Shift
                    | ui_input::Modifiers::LeftShift
                    | ui_input::Modifiers::CapsLock,
            ),
        ),
        service.handle_shortcut_activation(|id| {
            assert_eq!(id, TEST_SHORTCUT_ID);
            true
        }),
    )
    .await
    .0
    .expect("handle_key_event true");
    assert_eq!(true, was_handled);

    // LEFT_CONTROL + B should *not* trigger the shortcut.
    let was_handled = service
        .press_key(
            ui_input::Key::B,
            Some(ui_input::Modifiers::LeftControl | ui_input::Modifiers::Control),
        )
        .await
        .expect("handle_key_event false for left_control");
    assert_eq!(false, was_handled);

    // RIGHT_CONTROL + B should trigger the shortcut.
    let was_handled = join(
        service.press_key(
            ui_input::Key::B,
            Some(ui_input::Modifiers::RightControl | ui_input::Modifiers::Control),
        ),
        service.handle_shortcut_activation(|_| true),
    )
    .await
    .0
    .expect("handle_key_event true for right_control");
    assert_eq!(true, was_handled);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_modifiers_not_activated_on_release() -> Result<(), Error> {
    let mut service = ShortcutService::new().await?;

    // Set modifier-only shortcut for LEFT_SHIFT
    let shortcut = ui_shortcut::Shortcut {
        id: Some(TEST_SHORTCUT_ID),
        modifiers: Some(ui_input::Modifiers::LeftShift),
        key: None,
        use_priority: None,
        trigger: Some(ui_shortcut::Trigger::KeyPressedAndReleased),
        key3: None,
        keys_required: None,
    };
    service.registry.register_shortcut(shortcut).await.expect("register_shortcut left_shift");

    // Set shortcut for LEFT_SHIFT + A.
    let shortcut = ui_shortcut::Shortcut {
        id: Some(TEST_SHORTCUT_2_ID),
        modifiers: Some(ui_input::Modifiers::LeftShift),
        key: Some(ui_input::Key::C),
        use_priority: None,
        trigger: None,
        key3: None,
        keys_required: None,
    };
    service.registry.register_shortcut(shortcut).await.expect("register_shortcut left_shift+c");

    let was_handled = service
        .press_key(
            ui_input::Key::LeftShift,
            Some(ui_input::Modifiers::LeftShift | ui_input::Modifiers::Shift),
        )
        .await
        .expect("Press LeftShift");
    assert_eq!(false, was_handled);

    let was_handled = join(
        service.press_key(
            ui_input::Key::C,
            Some(ui_input::Modifiers::LeftShift | ui_input::Modifiers::Shift),
        ),
        service.handle_shortcut_activation(|id| {
            assert_eq!(id, TEST_SHORTCUT_2_ID);
            true
        }),
    )
    .await
    .0
    .expect("handle_key_event left_shift + C");
    assert_eq!(true, was_handled);

    let was_handled = service
        .release_key(
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
    let mut service = ShortcutService::new().await?;

    // Set shortcut for LEFT_META
    let shortcut = ui_shortcut::Shortcut {
        id: Some(TEST_SHORTCUT_ID),
        modifiers: Some(ui_input::Modifiers::LeftMeta),
        key: None,
        use_priority: None,
        trigger: Some(ui_shortcut::Trigger::KeyPressedAndReleased),
        key3: None,
        keys_required: None,
    };
    service.registry.register_shortcut(shortcut).await.expect("register_shortcut left_meta");

    // Set shortcut for LEFT_META + Q.
    let shortcut = ui_shortcut::Shortcut {
        id: Some(TEST_SHORTCUT_2_ID),
        modifiers: Some(ui_input::Modifiers::LeftMeta),
        key: Some(ui_input::Key::Q),
        use_priority: None,
        trigger: None,
        key3: None,
        keys_required: None,
    };
    service.registry.register_shortcut(shortcut).await.expect("register_shortcut left_meta+q");

    let was_handled =
        service.press_key(ui_input::Key::LeftMeta, None).await.expect("Press LeftMeta");
    assert_eq!(false, was_handled);

    let was_handled_fut = service.release_key(
        ui_input::Key::LeftMeta,
        Some(ui_input::Modifiers::LeftMeta | ui_input::Modifiers::Meta),
    );

    service
        .handle_shortcut_activation(|id| {
            assert_eq!(id, TEST_SHORTCUT_ID);
            true
        })
        .await;

    let was_handled = was_handled_fut.await.expect("handle_key_event true for left_meta");
    assert_eq!(true, was_handled);

    Ok(())
}
