// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::events::{DirectoryReady, Event, EventStream, Running},
    fidl_fuchsia_sys2 as fsys,
};

#[fuchsia::main]
async fn main() {
    // Validate that RUNNING and DirectoryReady is synthesized for root
    let mut event_stream = EventStream::open().await.unwrap();
    let mut found_directory_ready = false;
    let mut found_running = false;
    loop {
        let event = event_stream.next().await.unwrap();
        if matches!(
            event,
            fsys::Event {
                header: Some(fsys::EventHeader { event_type: Some(DirectoryReady::TYPE), .. }),
                ..
            }
        ) {
            found_directory_ready = true;
        }
        if matches!(
            event,
            fsys::Event {
                header: Some(fsys::EventHeader { event_type: Some(Running::TYPE), .. }),
                ..
            }
        ) {
            found_running = true;
        }

        if found_directory_ready && found_running {
            break;
        }
    }
}
