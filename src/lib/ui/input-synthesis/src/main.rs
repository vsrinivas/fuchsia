// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Wraps the `input-synthesis` library into a binary, which can then be
//! added as a component into integration tests.  The `input_synthesis.test.fidl` API
//! can then be used to drive this binary.  The binary only forwards
//! the FIDL API calls to the Rust `input_synthesis` library code.
//!
//! This approach is useful in integration tests. The integration tests
//! can not link Rust directly due to external constraints, but can use
//! prebuilt components.
//!
//! If you can link to Rust code directly, you can use the `input_synthesis`
//! library directly too, without this complication.
//!
//! For context, the `input_synthesis` library allows test code to pretend to
//! be one or more input devices, such as a keyboard, mouse or touchpad. It then
//! allows the test fixture to send high-level commands that get converted into
//! low-level input messages, rather than needing to write these commands
//! directly.  For example, it will convert a command `Text.Send("hello")` into
//! a sequence of presses and releases of the keys `h`, `e`, `l`, `l`, and `o` in
//! a rapid succession.  The input pipeline can not distinguish these fake key
//! presses from those typed up on a real keyboard.

use anyhow::Result;
use fidl_test_inputsynthesis::{MouseRequest, MouseRequestStream, TextRequest, TextRequestStream};
use fuchsia_async::{self as fasync, futures::StreamExt};
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn};
use std::time::Duration;

#[fasync::run_singlethreaded]
async fn main() -> Result<()> {
    fuchsia_syslog::init_with_tags(&["input-synthesis"]).expect("Failed to init syslog");

    fx_log_info!("starting input synthesis test component");

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(|mut stream: TextRequestStream| {
        fasync::Task::local(async move {
            while let Some(request) = stream.next().await {
                fx_log_info!("got request: {:?}", &request);
                match request {
                    Ok(TextRequest::Send_ { text, responder, .. }) => {
                        input_synthesis::text_command(text, Duration::from_millis(100))
                            .await
                            .expect("text command is sent");
                        responder.send().expect("send a response to TextRequest");
                    }
                    Err(e) => {
                        fx_log_err!("could not receive request: {:?}", e);
                    }
                }
            }
            fx_log_warn!("text requester terminated - no more text injection is possible");
        })
        .detach();
    });
    fs.dir("svc").add_fidl_service(|mut stream: MouseRequestStream| {
        fasync::Task::local(async move {
            while let Some(request) = stream.next().await {
                fx_log_info!("got request: {:?}", &request);
                match request {
                    Ok(MouseRequest::Change {
                        movement_x,
                        movement_y,
                        pressed_buttons,
                        responder,
                        ..
                    }) => {
                        input_synthesis::mouse_command(
                            Some((movement_x, movement_y)),
                            pressed_buttons,
                            127,
                            127,
                        )
                        .await
                        .expect("mouse command is sent");
                        responder.send().expect("send a response to MouseRequest");
                    }
                    Err(e) => {
                        fx_log_err!("could not receive request: {:?}", e);
                    }
                }
            }
            fx_log_warn!("mouse requester terminated - no more mouse injection is possible");
        })
        .detach();
    });
    fs.take_and_serve_directory_handle()?;

    fs.collect::<()>().await;

    Ok(())
}
