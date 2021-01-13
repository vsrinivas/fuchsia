// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::events::{
        error::EventError,
        sources::core::EventStreamServer,
        types::{ComponentEvent, EventSource},
    },
    async_trait::async_trait,
    fidl_fuchsia_sys2 as fsys,
    futures::channel::mpsc,
};

pub struct StaticEventStream {
    stream: Option<fsys::EventStreamRequestStream>,
}

impl StaticEventStream {
    pub fn new(stream: fsys::EventStreamRequestStream) -> Self {
        Self { stream: Some(stream) }
    }
}

#[async_trait]
impl EventSource for StaticEventStream {
    async fn listen(&mut self, sender: mpsc::Sender<ComponentEvent>) -> Result<(), EventError> {
        match self.stream.take() {
            None => Err(EventError::StreamAlreadyTaken),
            Some(stream) => {
                EventStreamServer::new(sender).spawn(stream);
                Ok(())
            }
        }
    }
}
