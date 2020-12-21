// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use fidl::endpoints::RequestStream;
use fidl_fidl_test_protocoleventremove as fidl_lib;
use futures::prelude::*;

// [START contents]
fn send_events(stream: fidl_lib::ExampleRequestStream) -> Result<(), fidl::Error> {
    let control_handle = stream.control_handle();
    control_handle.send_on_existing_event()?;
    control_handle.send_on_old_event()?;
    Ok(())
}

async fn receive_events(client: fidl_lib::ExampleProxy) -> Result<(), fidl::Error> {
    let mut event_stream = client.take_event_stream();
    while let Some(event) = event_stream.try_next().await? {
        match event {
            fidl_lib::ExampleEvent::OnExistingEvent { .. } => {}
            fidl_lib::ExampleEvent::OnOldEvent { .. } => {}
        }
    }
    Ok(())
}
// [END contents]

fn main() {}
