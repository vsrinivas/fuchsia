// 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]
#![cfg(test)]
use failure::{Error, ResultExt};
use fidl_fuchsia_ui_input2 as ui_input;
use fidl_fuchsia_ui_policy as ui_policy;
use fidl_fuchsia_ui_scenic as ui_scenic;
use fidl_fuchsia_ui_views as ui_views;
use fuchsia_async;
use fuchsia_component::client::connect_to_service;
use fuchsia_scenic as scenic;
use fuchsia_zircon as zx;
use futures::StreamExt;
use input_synthesis;
use std::time::Duration;

static HID_KEY_A: u32 = 0x04;

#[test]
fn keyboard_events() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["keyboard"])
        .expect("keyboard e2e syslog init should not fail");

    let mut executor = fuchsia_async::Executor::new()?;

    let presenter = connect_to_service::<ui_policy::PresenterMarker>()
        .context("Failed to connect to Presentation service")?;

    let keyboard = connect_to_service::<ui_input::KeyboardMarker>()
        .context("Failed to connect to Keyboard service")?;

    let (listener_client_end, mut listener_stream) =
        fidl::endpoints::create_request_stream::<ui_input::KeyListenerMarker>()?;

    // Set listener and view ref.
    let (raw_event_pair, _) = zx::EventPair::create()?;
    let view_ref = &mut ui_views::ViewRef { reference: raw_event_pair };
    keyboard.set_listener(view_ref, listener_client_end).expect("set_listener");

    executor.run_singlethreaded(async move {
        let mut token_pair = scenic::ViewTokenPair::new().expect("create ViewTokenPair");

        presenter.present_view(&mut token_pair.view_holder_token, None).expect("present_view");

        let scenic = connect_to_service::<ui_scenic::ScenicMarker>().expect("connect to Scenic");
        scenic.get_display_info().await.expect("get_display_info");

        // Scenic setup complete, start test.

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
    });

    Ok(())
}
