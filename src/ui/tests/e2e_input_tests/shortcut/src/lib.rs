// 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]
#![cfg(test)]
use failure::{Error, ResultExt};
use fidl_fuchsia_ui_input2 as ui_input;
use fidl_fuchsia_ui_policy as ui_policy;
use fidl_fuchsia_ui_scenic as ui_scenic;
use fidl_fuchsia_ui_shortcut as ui_shortcut;
use fidl_fuchsia_ui_views as ui_views;
use fuchsia_async;
use fuchsia_component::client::connect_to_service;
use fuchsia_scenic as scenic;
use fuchsia_zircon as zx;
use futures::StreamExt;
use input_synthesis;
use std::time::Duration;

static TEST_SHORTCUT_ID: u32 = 123;

#[test]
fn shortcut_detection() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["shortcut"])
        .expect("shortcut e2e syslog init should not fail");

    let mut executor = fuchsia_async::Executor::new()?;

    let presenter = connect_to_service::<ui_policy::PresenterMarker>()
        .context("Failed to connect to Presentation service")?;

    let registry = connect_to_service::<ui_shortcut::RegistryMarker>()
        .context("Failed to connect to Shortcut registry service")?;

    let (listener_client_end, mut listener_stream) =
        fidl::endpoints::create_request_stream::<ui_shortcut::ListenerMarker>()?;

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
    };
    registry.register_shortcut(shortcut).expect("register_shortcut");

    executor.run_singlethreaded(async move {
        let mut token_pair = scenic::ViewTokenPair::new().expect("create ViewTokenPair");

        presenter.present_view(&mut token_pair.view_holder_token, None).expect("present_view");

        let scenic = connect_to_service::<ui_scenic::ScenicMarker>().expect("connect to Scenic");
        scenic.get_display_info().await.expect("get_display_info");

        input_synthesis::keyboard_event_command(0x04, Duration::from_millis(0)).await
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
    });

    Ok(())
}
