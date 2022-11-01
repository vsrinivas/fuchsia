// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fuchsia_ui_pointer as fpointer, fuchsia_async::Task, tracing::debug};

// Returns a Task that continuously listens for touch events and responds to them.
pub fn autolisten_touch(touch_source: fpointer::TouchSourceProxy) -> Task<()> {
    Task::local(async move {
        let mut returned_events = Vec::<fpointer::TouchEvent>::new();
        loop {
            let responses: Vec<fpointer::TouchResponse> = returned_events
                .iter()
                .map(|event| {
                    let mut response = fpointer::TouchResponse::EMPTY;
                    if event.pointer_sample.is_some() {
                        response.response_type = Some(fpointer::TouchResponseType::Yes);
                    }
                    response
                })
                .collect();
            returned_events = touch_source
                .watch(&mut responses.into_iter())
                .await
                .expect("touch source Watch() error");
            debug!("Touch event received");
        }
    })
}
