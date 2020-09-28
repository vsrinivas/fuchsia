// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use anyhow::{Context as _, Error};
use fidl::endpoints::create_request_stream;
use fidl_fuchsia_ui_input2 as ui_input;
use fidl_fuchsia_ui_views as ui_views;
use fuchsia_async;
use fuchsia_component::client::connect_to_service;
use fuchsia_zircon as zx;
use futures::StreamExt;
use input_synthesis;
use std::time::Duration;
use ui_test_tools;

static HID_KEY_A: u32 = 0x04;

#[fuchsia_async::run_singlethreaded(test)]
async fn keyboard_events() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["keyboard"])
        .expect("keyboard e2e syslog init should not fail");

    // When service goes out of scope, dependent services will be disconnected.
    let _service = ui_test_tools::setup(ui_test_tools::EnvironmentType::ScenicWithRootPresenter)
        .await
        .context("ui_test_tools::setup")?;

    let keyboard = connect_to_service::<ui_input::KeyboardMarker>()
        .context("Failed to connect to Keyboard service")?;

    let (listener_client_end, mut listener_stream) =
        create_request_stream::<ui_input::KeyListenerMarker>()?;

    let (raw_event_pair, _) = zx::EventPair::create()?;
    let view_ref = &mut ui_views::ViewRef { reference: raw_event_pair };

    // Set listener and view ref.
    keyboard.set_listener(view_ref, listener_client_end).await.expect("set_listener");

    input_synthesis::keyboard_event_command(HID_KEY_A, Duration::from_millis(0))
        .await
        .expect("key_event_command injects input");

    // Wait for key listener callback to be activated.
    match listener_stream.next().await {
        Some(Ok(ui_input::KeyListenerRequest::OnKeyEvent { event, responder, .. })) => {
            assert_eq!(event.key, Some(ui_input::Key::A));
            responder.send(ui_input::Status::Handled).expect("responding from key listener")
        }
        Some(Err(e)) => {
            panic!("Error from listener_stream.next(): {:}", e);
        }
        None => {
            panic!("Error from listener_stream.next(): empty stream");
        }
    }
    Ok(())
}
