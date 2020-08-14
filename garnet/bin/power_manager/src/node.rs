// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error::PowerManagerError;
use crate::message::{Message, MessageReturn};
use async_trait::async_trait;
use std::rc::Rc;

/// A trait that all nodes in the PowerManager must implement
#[async_trait(?Send)]
pub trait Node {
    /// Return a string to indicate the name of this node
    ///
    /// Each node should use this function to indicate a meaningful name.
    /// The name may be used for logging and/or debugging purposes.
    fn name(&self) -> String;

    /// Handle a new message
    ///
    /// All nodes must implement this message to support communication between nodes. This
    /// is the entry point for a Node to receive new messages.
    async fn handle_message(&self, msg: &Message) -> Result<MessageReturn, PowerManagerError>;

    /// Send a message to another node
    ///
    /// This is implemented as a future to support scenarios where
    /// a node wishes to send messages to multiple other nodes.
    /// Errors are logged automatically.
    async fn send_message(
        &self,
        node: &Rc<dyn Node>,
        msg: &Message,
    ) -> Result<MessageReturn, PowerManagerError> {
        // TODO(fxb/44484): Ideally we'd use a duration event here. But due to a limitation in the
        // Rust tracing library, that would require creating any formatted strings (such as the
        // "message" value) unconditionally, even when the tracing category is disabled. To
        // avoid that unnecessary computation, just use an instant event.
        fuchsia_trace::instant!(
            "power_manager:messages",
            "message_start",
            fuchsia_trace::Scope::Thread,
            "message" => format!("{:?}", msg).as_str(),
            "source_node" => self.name().as_str(),
            "dest_node" => node.name().as_str()
        );

        let result = node.handle_message(msg).await;
        fuchsia_trace::instant!(
            "power_manager:messages",
            "message_result",
            fuchsia_trace::Scope::Thread,
            "message" => format!("{:?}", msg).as_str(),
            "source_node" => self.name().as_str(),
            "dest_node" => node.name().as_str(),
            "result" => format!("{:?}", result).as_str()
        );
        result
    }
}
