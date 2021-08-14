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
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    futures::channel::mpsc,
};

pub struct StaticEventStream {
    state: State,
}

enum State {
    ReadyToListen(Option<fsys::EventStreamRequestStream>),
    Listening(fasync::Task<()>),
}

impl StaticEventStream {
    pub fn new(stream: fsys::EventStreamRequestStream) -> Self {
        Self { state: State::ReadyToListen(Some(stream)) }
    }
}

#[async_trait]
impl EventSource for StaticEventStream {
    async fn listen(&mut self, sender: mpsc::Sender<ComponentEvent>) -> Result<(), EventError> {
        match &mut self.state {
            State::Listening(_) => Err(EventError::StreamAlreadyTaken),
            State::ReadyToListen(ref mut stream) => {
                // unwrap safe since we initialize to Some().
                debug_assert!(stream.is_some());
                let stream = stream.take().unwrap();
                self.state = State::Listening(EventStreamServer::new(sender).spawn(stream));
                Ok(())
            }
        }
    }
}
