// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use failure::{Error, ResultExt};
use fidl::client::QueryResponseFut;
use fidl_fuchsia_ui_input2 as ui_input;
use fidl_fuchsia_ui_shortcut as ui_shortcut;
use fidl_fuchsia_ui_views as ui_views;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use fuchsia_zircon as zx;
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
    };
    service.registry.register_shortcut(shortcut).await.expect("register_shortcut shift");

    // Set shortcut for RIGHT_CONTROL + B.
    let shortcut = ui_shortcut::Shortcut {
        id: None,
        modifiers: Some(ui_input::Modifiers::RightControl),
        key: Some(ui_input::Key::B),
        use_priority: None,
        trigger: None,
    };
    service.registry.register_shortcut(shortcut).await.expect("register_shortcut right_control");

    // Process key event that *does not* trigger a shortcut.
    let was_handled =
        service.press_key(ui_input::Key::A, None).await.expect("handle_key_event false");
    assert_eq!(false, was_handled);

    // Process key event that triggers shift shortcut.
    // The order is important here, as handle_key_event() dispatches the key
    // to be processed by the manager, which is expected to result in listener
    // message in the next block.
    // At the same time, handle_key_event should return true, which is validated
    // later.
    let was_handled_fut = service.press_key(
        ui_input::Key::A,
        Some(
            ui_input::Modifiers::Shift
                | ui_input::Modifiers::LeftShift
                | ui_input::Modifiers::CapsLock,
        ),
    );

    // React to one shortcut activation message from the listener stream.
    if let Some(Ok(ui_shortcut::ListenerRequest::OnShortcut { id, responder, .. })) =
        service.listener.next().await
    {
        assert_eq!(id, TEST_SHORTCUT_ID);
        responder.send(true).expect("responding from shortcut listener for shift")
    } else {
        panic!("Error from listener.next() on shift shortcut activation");
    }

    let was_handled = was_handled_fut.await.expect("handle_key_event true");
    // Expect key event to be handled.
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
    let was_handled_fut = service.press_key(
        ui_input::Key::B,
        Some(ui_input::Modifiers::RightControl | ui_input::Modifiers::Control),
    );

    if let Some(Ok(ui_shortcut::ListenerRequest::OnShortcut { responder, .. })) =
        service.listener.next().await
    {
        responder.send(true).expect("responding from shortcut listener for right control");
    }

    let was_handled = was_handled_fut.await.expect("handle_key_event true for right_control");
    assert_eq!(true, was_handled);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_modifiers_only() -> Result<(), Error> {
    let mut service = ShortcutService::new().await?;

    // Set shortcut for LEFT_SHIFT
    let shortcut = ui_shortcut::Shortcut {
        id: Some(TEST_SHORTCUT_ID),
        modifiers: Some(ui_input::Modifiers::LeftShift),
        key: Some(ui_input::Key::LeftShift),
        use_priority: None,
        trigger: None,
    };
    service.registry.register_shortcut(shortcut).await.expect("register_shortcut left_shift");

    // Set shortcut for LEFT_SHIFT + A.
    let shortcut = ui_shortcut::Shortcut {
        id: Some(TEST_SHORTCUT_2_ID),
        modifiers: Some(ui_input::Modifiers::LeftShift),
        key: Some(ui_input::Key::C),
        use_priority: None,
        trigger: None,
    };
    service.registry.register_shortcut(shortcut).await.expect("register_shortcut right_control");

    service
        .press_key(
            ui_input::Key::LeftShift,
            Some(ui_input::Modifiers::LeftShift | ui_input::Modifiers::Shift),
        )
        .await
        .expect("Press LeftShift");

    // React to one shortcut activation message from the listener stream.
    if let Some(Ok(ui_shortcut::ListenerRequest::OnShortcut { id, responder, .. })) =
        service.listener.next().await
    {
        assert_eq!(id, TEST_SHORTCUT_ID);
        responder.send(true).expect("responding from shortcut listener for shift")
    } else {
        panic!("Error from listener.next() on shift shortcut activation");
    }

    service
        .press_key(
            ui_input::Key::C,
            Some(ui_input::Modifiers::LeftShift | ui_input::Modifiers::Shift),
        )
        .await
        .expect("Press C");

    // React to one shortcut activation message from the listener stream.
    if let Some(Ok(ui_shortcut::ListenerRequest::OnShortcut { id, responder, .. })) =
        service.listener.next().await
    {
        assert_eq!(id, TEST_SHORTCUT_2_ID);
        responder.send(true).expect("responding from shortcut listener for shift + A")
    } else {
        panic!("Error from listener.next() on shift + C shortcut activation");
    }

    Ok(())
}
