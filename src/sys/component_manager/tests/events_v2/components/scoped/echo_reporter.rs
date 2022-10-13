// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{events::*, matcher::*},
    fidl_fuchsia_sys2 as fsys,
};

#[fuchsia::main]
async fn main() {
    let mut event_stream = EventStream::open().await.unwrap();
    loop {
        let event = event_stream.next().await.unwrap();
        if matches!(event.header.unwrap().event_type.unwrap(), fsys::EventType::Started) {
            break;
        }
    }
    EventMatcher::ok()
        .stop(Some(ExitStatusMatcher::Clean))
        .moniker("./echo_server")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}
