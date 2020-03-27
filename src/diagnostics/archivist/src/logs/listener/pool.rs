// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use super::*;
use fidl_fuchsia_logger::LogMessage;

/// A pool of log listeners, each of which recieves a stream of log messages from the diagnostics
/// service. Listeners are dropped from the pool when they are no longer connected.
#[derive(Default)]
pub struct Pool {
    listeners: Vec<Listener>,
}

impl Pool {
    /// Sends the provided log message to all listeners in the pool. Removes listeners that become
    /// stale.
    pub fn send(&mut self, log_msg: &mut LogMessage) {
        for listener in &mut self.listeners {
            listener.send_log(log_msg);
        }
        self.listeners.retain(Listener::is_healthy);
    }

    /// Add a new listener to the pool. Listeners in the pool will recieve `LogMessage`s until they
    /// close their channel.
    pub fn add(&mut self, listener: Listener) {
        self.listeners.push(listener);
    }
}
