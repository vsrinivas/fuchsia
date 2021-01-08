// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![cfg(test)]

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_input as input, fidl_fuchsia_ui_input as ui_input,
    fidl_fuchsia_ui_input3 as ui_input3,
    fuchsia_async::{self as fasync},
    fuchsia_component::client::connect_to_service,
    fuchsia_scenic as scenic,
    futures::{
        future,
        stream::{FusedStream, StreamExt},
    },
    matches::assert_matches,
};

fn create_key_event(key: input::Key, modifiers: ui_input3::Modifiers) -> ui_input3::KeyEvent {
    ui_input3::KeyEvent {
        key: Some(key),
        modifiers: Some(modifiers),
        type_: Some(ui_input3::KeyEventType::Pressed),
        ..ui_input3::KeyEvent::EMPTY
    }
}

async fn expect_key_and_modifiers(
    listener: &mut ui_input3::KeyboardListenerRequestStream,
    key: input::Key,
    modifiers: ui_input3::Modifiers,
) {
    let listener_request = listener.next().await;
    if let Some(Ok(ui_input3::KeyboardListenerRequest::OnKeyEvent { event, responder, .. })) =
        listener_request
    {
        responder.send(ui_input3::KeyEventStatus::Handled).expect("responding from key listener");
        assert_eq!(event.key, Some(key));
        assert_eq!(event.modifiers, Some(modifiers));
    } else {
        panic!("Expected key error: {:?}, got {:?}", (key, modifiers), listener_request);
    }
}

#[fasync::run_singlethreaded(test)]
async fn test_disconnecting_keyboard_client_disconnects_listener() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["keyboard3_integration_test"])
        .expect("syslog init should not fail");

    let ime_service = connect_to_service::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to IME Service")?;

    let keyboard_service_client = connect_to_service::<ui_input3::KeyboardMarker>()
        .context("Failed to connect to input3 Keyboard service")?;

    let keyboard_service_other_client = connect_to_service::<ui_input3::KeyboardMarker>()
        .context("Failed to establish another connection to input3 Keyboard service")?;

    // Create fake client.
    let (listener_client_end, mut listener) =
        fidl::endpoints::create_request_stream::<ui_input3::KeyboardListenerMarker>()?;
    let view_ref = scenic::ViewRefPair::new()?.view_ref;

    keyboard_service_client
        .add_listener(&mut scenic::duplicate_view_ref(&view_ref)?, listener_client_end)
        .await
        .expect("add_listener for first client");

    // Create another fake client.
    let (other_listener_client_end, mut other_listener) =
        fidl::endpoints::create_request_stream::<ui_input3::KeyboardListenerMarker>()?;
    let other_view_ref = scenic::ViewRefPair::new()?.view_ref;

    keyboard_service_other_client
        .add_listener(&mut scenic::duplicate_view_ref(&other_view_ref)?, other_listener_client_end)
        .await
        .expect("add_listener for another client");

    // Focus second client.
    ime_service.view_focus_changed(&mut scenic::duplicate_view_ref(&other_view_ref)?).await?;

    // Drop proxy, emulating first client disconnecting from it.
    std::mem::drop(keyboard_service_client);

    // Expect disconnected client key event listener to be disconnected as well.
    assert_matches!(listener.next().await, None);
    assert_matches!(listener.is_terminated(), true);

    // Ensure that the other client is still connected.
    let (key, modifiers) = (input::Key::A, ui_input3::Modifiers::CapsLock);
    let dispatched_event = create_key_event(key, modifiers);
    let (was_handled, _) = future::join(
        ime_service.dispatch_key3(dispatched_event),
        expect_key_and_modifiers(&mut other_listener, key, modifiers),
    )
    .await;

    assert_eq!(was_handled?, true);
    Ok(())
}
