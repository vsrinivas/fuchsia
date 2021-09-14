// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use anyhow::{Context as _, Result};
use fidl_fuchsia_input as input;
use fidl_fuchsia_ui_input as ui_input;
use fidl_fuchsia_ui_input3 as ui_input3;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_protocol;
use fuchsia_zircon as zx;
use futures::{StreamExt, TryStreamExt};
use keymaps::usages::Usages;
use test_helpers::{bind_editor, get_state_update, simulate_keypress};

#[fasync::run_singlethreaded(test)]
async fn test_visibility_service_sends_initial_update() -> Result<()> {
    let visibility_service = connect_to_protocol::<ui_input::ImeVisibilityServiceMarker>()
        .context("Failed to connect to ImeVisibilityService")?;
    let mut visiblity_event_stream = visibility_service.take_event_stream();

    let ui_input::ImeVisibilityServiceEvent::OnKeyboardVisibilityChanged { visible } =
        visiblity_event_stream
            .try_next()
            .await
            .expect("expected working event stream")
            .expect("visibility service should have sent message");
    assert_eq!(visible, false);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_visibility_service_resends_update_after_closing_keyboard() -> Result<()> {
    let visibility_service = connect_to_protocol::<ui_input::ImeVisibilityServiceMarker>()
        .context("Failed to connect to ImeVisibilityService")?;
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let visiblity_event_stream = visibility_service.take_event_stream();

    // Hide keyboard using `fuchsia.ui.input.ImeService`.
    ime_service.hide_keyboard()?;

    let ui_input::ImeVisibilityServiceEvent::OnKeyboardVisibilityChanged { visible } =
        visiblity_event_stream
            .skip(1)
            .try_next()
            .await
            .expect("expected working event stream")
            .expect("visibility service should have sent message");
    assert_eq!(visible, false);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_visibility_service_resends_update_after_opening_keyboard() -> Result<()> {
    let visibility_service = connect_to_protocol::<ui_input::ImeVisibilityServiceMarker>()
        .context("Failed to connect to ImeVisibilityService")?;
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    let visiblity_event_stream = visibility_service.take_event_stream();

    // Show keyboard using `fuchsia.ui.input.ImeService`.
    ime_service.show_keyboard()?;

    let ui_input::ImeVisibilityServiceEvent::OnKeyboardVisibilityChanged { visible } =
        visiblity_event_stream
            .skip(1)
            .try_next()
            .await
            .expect("expected working event stream")
            .expect("visibility service should have sent message");
    assert_eq!(visible, true);

    // Call `hide_keyboard()` to restore state.
    ime_service.hide_keyboard()?;

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_open_and_close_from_keyboard() -> Result<()> {
    let visibility_service = connect_to_protocol::<ui_input::ImeVisibilityServiceMarker>()
        .context("Failed to connect to ImeVisibilityService")?;
    let ime_service = connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to ImeService")?;

    // Note that the initial message from the visibility service is skipped.
    let mut visiblity_event_stream = visibility_service.take_event_stream().skip(1);

    let (ime, _editor_stream) = bind_editor(&ime_service)?;

    // Show keyboard using `fuchsia.ui.input.InputMethodEditor`.
    ime.show()?;

    let ui_input::ImeVisibilityServiceEvent::OnKeyboardVisibilityChanged { visible } =
        visiblity_event_stream
            .try_next()
            .await
            .expect("expected working event stream")
            .expect("visibility service should have sent message");
    assert_eq!(visible, true);

    // Hide keyboard using `fuchsia.ui.input.InputMethodEditor`.
    ime.hide()?;

    let ui_input::ImeVisibilityServiceEvent::OnKeyboardVisibilityChanged { visible } =
        visiblity_event_stream
            .try_next()
            .await
            .expect("expected working event stream")
            .expect("visibility service should have sent message");
    assert_eq!(visible, false);

    Ok(())
}

/// Connects to a service that provides key injection.
fn connect_to_key_event_service() -> Result<ui_input3::KeyEventInjectorProxy> {
    connect_to_protocol::<ui_input3::KeyEventInjectorMarker>()
        .context("Failed to connect to fuchsia.ui.input3.KeyEventInjector")
}

fn connect_to_ime_service() -> Result<ui_input::ImeServiceProxy> {
    connect_to_protocol::<ui_input::ImeServiceMarker>()
        .context("Failed to connect to fuchsia.ui.input.ImeService")
}

const DELAY: zx::Duration = zx::Duration::from_seconds(5);

// TODO(fxbug.dev/75030) This is horrible, but inevitable. Until the linked
// bug is fixed.  It can't work in 100% of the cases, and it can't work 100%
// reliably regardless of the value of DELAY.
async fn wait_for_editor_binding() {
    fasync::Timer::new(fasync::Time::after(DELAY)).await;
}

#[fasync::run_singlethreaded(test)]
async fn test_input_updates_ime_state() -> Result<()> {
    let ime_service = connect_to_ime_service()?;
    let key_event_service = connect_to_key_event_service()?;

    let (_ime, mut editor_stream) = bind_editor(&ime_service)?;
    wait_for_editor_binding().await;

    simulate_keypress(&key_event_service, input::Key::A).await?;

    // get first message with keypress event but no state update
    let (state, event) = get_state_update(&mut editor_stream).await?;
    let event = event.expect("expected event to be set");
    assert_eq!(event.phase, ui_input::KeyboardEventPhase::Pressed);
    assert_eq!(event.code_point, 97);
    assert_eq!(state.text, "");

    // get second message with state update
    let (state, event) = get_state_update(&mut editor_stream).await?;
    assert!(event.is_none());
    assert_eq!(state.text, "a");
    assert_eq!(state.selection.base, 1);
    assert_eq!(state.selection.extent, 1);

    // get third message with keyrelease event but no state update
    let (state, event) = get_state_update(&mut editor_stream).await?;
    let event = event.expect("expected event to be set");
    assert_eq!(event.phase, ui_input::KeyboardEventPhase::Released);
    assert_eq!(event.code_point, 97);
    assert_eq!(state.text, "a");

    // press left arrow
    simulate_keypress(&key_event_service, input::Key::Left).await?;

    // get first message with keypress event but no state update
    let (state, event) = get_state_update(&mut editor_stream).await?;
    let event = event.expect("expected event to be set");
    assert_eq!(event.phase, ui_input::KeyboardEventPhase::Pressed);
    assert_eq!(event.code_point, 0);
    assert_eq!(event.hid_usage, Usages::HidUsageKeyLeft as u32);
    assert_eq!(state.text, "a");

    // get second message with state update
    let (state, event) = get_state_update(&mut editor_stream).await?;
    assert!(event.is_none());
    assert_eq!(state.text, "a");
    assert_eq!(state.selection.base, 0);
    assert_eq!(state.selection.extent, 0);

    // get first message with keyrelease event but no state update
    let (state, event) = get_state_update(&mut editor_stream).await?;
    let event = event.expect("expected event to be set");
    assert_eq!(event.phase, ui_input::KeyboardEventPhase::Released);
    assert_eq!(event.code_point, 0);
    assert_eq!(event.hid_usage, Usages::HidUsageKeyLeft as u32);
    assert_eq!(state.text, "a");

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_input_triggers_action() -> Result<()> {
    let ime_service = connect_to_ime_service()?;
    let key_event_service = connect_to_key_event_service()?;

    let (_ime, mut editor_stream) = bind_editor(&ime_service)?;
    wait_for_editor_binding().await;

    // send key events
    simulate_keypress(&key_event_service, input::Key::Enter).await?;

    // get first message with keypress event
    let (_state, event) = get_state_update(&mut editor_stream).await?;
    let event = event.expect("expected event to be set");
    assert_eq!(event.phase, ui_input::KeyboardEventPhase::Pressed);
    assert_eq!(event.code_point, 0);
    assert_eq!(event.hid_usage, Usages::HidUsageKeyEnter as u32);

    // get second message with onaction event
    let msg = editor_stream
        .try_next()
        .await
        .expect("expected working event stream")
        .expect("ime should have sent message");
    if let ui_input::InputMethodEditorClientRequest::OnAction { action, .. } = msg {
        assert_eq!(action, ui_input::InputMethodAction::Done);
    } else {
        panic!("request should be OnAction");
    };

    Ok(())
}
