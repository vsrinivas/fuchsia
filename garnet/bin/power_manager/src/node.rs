// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::{Message, MessageReturn};
use async_trait::async_trait;
use failure::Error;
use fuchsia_syslog::{fx_log_err, fx_vlog};
use std::rc::Rc;

/// A trait that all nodes in the PowerManager must implement
#[async_trait(?Send)]
pub trait Node {
    /// Return a string to indicate the name of this node
    ///
    /// Each node should use this function to indicate a meaningful name.
    /// The name may be used for logging and/or debugging purposes.
    fn name(&self) -> &'static str;

    /// Handle a new message
    ///
    /// All nodes must implement this message to support communication between nodes. This
    /// is the entry point for a Node to receive new messages.
    async fn handle_message(&self, msg: &Message) -> Result<MessageReturn, Error>;

    /// Send a message to another node
    ///
    /// This is implemented as a future to support scenarios where
    /// a node wishes to send messages to multiple other nodes.
    /// Errors are logged automatically.
    async fn send_message(
        &self,
        node: &Rc<dyn Node>,
        msg: &Message,
    ) -> Result<MessageReturn, Error> {
        let result = node.handle_message(msg).await;
        match result.as_ref() {
            Ok(r) => {
                fx_vlog!(1, "{} -> {}: msg={:?}; res={:?}", self.name(), node.name(), msg, r);
            }
            Err(e) => {
                fx_log_err!("{} -> {}: msg={:?}; res={:?}", self.name(), node.name(), msg, e);
            }
        }
        result
    }
}
