// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::session::Session;
use failure::Error;
use futures::channel::mpsc::Receiver;
use futures::{select, FutureExt, StreamExt};

/// The Media Session service.
pub struct Service {
    new_session_receiver: Receiver<Session>,
}

impl Service {
    pub fn new(new_session_receiver: Receiver<Session>) -> Self {
        Self {
            new_session_receiver,
        }
    }

    pub async fn serve(mut self) -> Result<(), Error> {
        let mut new_sessions = self.new_session_receiver.next().fuse();
        loop {
            select! {
                _new_session = new_sessions => {
                    // drop the new session; this service is not implemented
                    // TODO(turnage): implement
                },
            }
        }
    }
}
