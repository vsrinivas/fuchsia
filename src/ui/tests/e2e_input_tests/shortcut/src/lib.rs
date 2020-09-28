// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use anyhow::{Context as _, Error};
use fidl::endpoints::create_request_stream;
use fidl_fuchsia_ui_input2 as ui_input;
use fidl_fuchsia_ui_shortcut as ui_shortcut;
use fidl_fuchsia_ui_views as ui_views;
use fuchsia_async;
use fuchsia_component::client::connect_to_service;
use fuchsia_zircon as zx;
use futures::StreamExt;
use input_synthesis;
use std::time::Duration;
use ui_test_tools;

static TEST_SHORTCUT_ID: u32 = 123;

#[fuchsia_async::run_singlethreaded(test)]
async fn shortcut_detection() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["shortcut"])
        .expect("shortcut e2e syslog init should not fail");

    // When service goes out of scope, dependent services will be disconnected.
    let _service = ui_test_tools::setup(ui_test_tools::EnvironmentType::ScenicWithRootPresenter)
        .await
        .context("ui_test_tools::setup")?;

    let registry = connect_to_service::<ui_shortcut::RegistryMarker>()
        .context("Failed to connect to Shortcut registry service")?;

    let (listener_client_end, mut listener_stream) =
        create_request_stream::<ui_shortcut::ListenerMarker>()?;

    // Set listener and view ref.
    let (raw_event_pair, _) = zx::EventPair::create()?;
    let view_ref = &mut ui_views::ViewRef { reference: raw_event_pair };
    registry.set_view(view_ref, listener_client_end).expect("set_view");

    // Set the shortcut.
    let shortcut = ui_shortcut::Shortcut {
        id: Some(TEST_SHORTCUT_ID),
        modifiers: None,
        key: Some(ui_input::Key::A),
        use_priority: None,
        trigger: None,
    };
    registry.register_shortcut(shortcut).await.expect("register_shortcut");

    input_synthesis::keyboard_event_command(0x04, Duration::from_millis(0))
        .await
        .expect("keyboard_event_command injects input");

    // Wait for shortcut listener callback to be activated.
    match listener_stream.next().await {
        Some(Ok(req)) => {
            match req {
                ui_shortcut::ListenerRequest::OnShortcut { id, responder, .. } => {
                    assert_eq!(id, TEST_SHORTCUT_ID);
                    responder.send(true).expect("responding from shortcut listener")
                }
            };
        }
        Some(Err(e)) => {
            panic!("Error from listener_stream.next(): {:}", e);
        }
        _ => {
            panic!("Error from listener_stream.next(): empty stream");
        }
    }

    Ok(())
}
