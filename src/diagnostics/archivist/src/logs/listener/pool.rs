// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use super::*;
use fidl_fuchsia_logger::LogMessage;
use futures::future::join_all;

/// A pool of log listeners, each of which recieves a stream of log messages from the diagnostics
/// service. Listeners are dropped from the pool when they are no longer connected.
#[derive(Default)]
pub struct Pool {
    listeners: Vec<Listener>,
}

impl Pool {
    /// Sends the provided log message to all listeners in the pool. Removes listeners that become
    /// stale.
    ///
    /// Each message is sent concurrently to all listeners, and the function returns when all
    /// listeners have acknowledged receipt of the message.
    pub async fn send(&mut self, log_msg: &LogMessage) {
        join_all(self.listeners.iter_mut().map(|listener| listener.send_log(log_msg.clone())))
            .await;
        self.listeners.retain(Listener::is_healthy);
    }

    /// Add a new listener to the pool. Listeners in the pool will recieve `LogMessage`s until they
    /// close their channel.
    pub fn add(&mut self, listener: Listener) {
        self.listeners.push(listener);
    }
}
