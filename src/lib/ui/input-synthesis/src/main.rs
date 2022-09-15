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

use {
    anyhow::Result,
    fidl_test_inputsynthesis::{
        Error, MouseRequest, MouseRequestStream, TextRequest, TextRequestStream,
    },
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::StreamExt,
    input_synthesis::synthesizer::InputDevice,
    std::time::Duration,
    tracing::{error, info, warn},
};

#[fuchsia::main(logging_tags = ["input-synthesis"])]
async fn main() -> Result<()> {
    info!("starting input synthesis test component");

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(|mut stream: TextRequestStream| {
        fasync::Task::local(async move {
            while let Some(request) = stream.next().await {
                info!("got request: {:?}", &request);
                match request {
                    Ok(TextRequest::Send_ { text, responder, .. }) => {
                        input_synthesis::text_command(text, Duration::from_millis(100))
                            .await
                            .expect("text command is sent");
                        responder.send().expect("send a response to TextRequest");
                    }
                    Err(e) => {
                        error!("could not receive request: {:?}", e);
                    }
                }
            }
            warn!("text requester terminated - no more text injection is possible");
        })
        .detach();
    });
    fs.dir("svc").add_fidl_service(|mut stream: MouseRequestStream| {
        fasync::Task::local(async move {
            let mut devices: Vec<Box<dyn InputDevice>> = Vec::new();
            while let Some(request) = stream.next().await {
                info!("got request: {:?}", &request);
                match request {
                    Ok(MouseRequest::AddDevice { responder, .. }) => {
                        let device = input_synthesis::add_mouse_device_command(127, 127)
                            .await
                            .expect("input device created");
                        devices.push(device);
                        responder
                            .send(devices.len() as u32 - 1)
                            .expect("send a response to MouseRequest");
                    }
                    Ok(MouseRequest::SendInputReport {
                        device_id,
                        report,
                        event_time,
                        responder,
                        ..
                    }) => {
                        let id = device_id as usize;
                        if id >= devices.len() {
                            error!(device_id = id, "unknown");
                            responder
                                .send(&mut Err(Error::InvalidDeviceId))
                                .expect("send a Error to MouseRequest");
                            // This is intended to ignore the invalid request.
                            continue;
                        }
                        devices[id].mouse(report, event_time).expect("synthesis mouse is sent");
                        responder.send(&mut Ok({})).expect("send a response to MouseRequest");
                    }
                    Err(e) => {
                        error!("could not receive request: {:?}", e);
                    }
                }
            }
            warn!("mouse requester terminated - no more mouse injection is possible");
        })
        .detach();
    });
    fs.take_and_serve_directory_handle()?;

    fs.collect::<()>().await;

    Ok(())
}
