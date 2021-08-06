// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error::PowerManagerError;
use crate::message::{Message, MessageReturn};
use anyhow::format_err;
use async_trait::async_trait;
use futures::future::join_all;
use std::rc::Rc;

/// A trait that all nodes in the PowerManager must implement
#[async_trait(?Send)]
pub trait Node {
    /// Return a string to indicate the name of this node
    ///
    /// Each node should use this function to indicate a meaningful name. The name may be used for
    /// logging and/or debugging purposes.
    fn name(&self) -> String;

    /// Handle a new message
    ///
    /// All nodes must implement this message to support communication between nodes. This is the
    /// entry point for a Node to receive new messages.
    async fn handle_message(&self, _msg: &Message) -> Result<MessageReturn, PowerManagerError> {
        Err(PowerManagerError::Unsupported)
    }

    /// Send a message to another node
    ///
    /// This is implemented as a future to support scenarios where a node wishes to send messages to
    /// multiple other nodes. Errors are logged automatically.
    async fn send_message(
        &self,
        node: &Rc<dyn Node>,
        msg: &Message,
    ) -> Result<MessageReturn, PowerManagerError> {
        // TODO(fxbug.dev/44484): Ideally we'd use a duration event here. But due to a limitation in
        // the Rust tracing library, that would require creating any formatted strings (such as the
        // "message" value) unconditionally, even when the tracing category is disabled. To avoid
        // that unnecessary computation, just use an instant event.
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

    /// Send a message to a list of other nodes. The message is sent to each node in a separate
    /// Future and all are joined before returning. The results from all nodes are returned in a
    /// vector in the same node-ordering that was provided.
    async fn send_message_to_many(
        &self,
        nodes: &Vec<Rc<dyn Node>>,
        msg: &Message,
    ) -> Vec<Result<MessageReturn, PowerManagerError>> {
        join_all(nodes.iter().map(|node| async move {
            self.send_message(node, msg).await.map_err(|e| {
                PowerManagerError::GenericError(format_err!(
                    "Failed to send message to {}: {}",
                    node.name(),
                    e
                ))
            })
        }))
        .await
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test::mock_node::{MessageMatcher, MockNodeMaker};
    use crate::{msg_eq, msg_ok_return};
    use fuchsia_async as fasync;
    use matches::assert_matches;

    struct TestNode;
    impl Node for TestNode {
        fn name(&self) -> String {
            "TestNode".to_string()
        }
    }

    /// Tests that `send_message_to_many` sends a message to all provided nodes and the results are
    /// returned correctly.
    #[fasync::run_singlethreaded(test)]
    async fn test_send_message_to_many() {
        let mut mock_maker = MockNodeMaker::new();
        let sending_node = mock_maker.make("sending_node", vec![]);

        let receiving_node_1 = mock_maker.make(
            "receiving_node_1",
            vec![(msg_eq!(GetPerformanceState), msg_ok_return!(GetPerformanceState(1)))],
        );
        let receiving_node_2 = mock_maker.make(
            "receiving_node_1",
            vec![(msg_eq!(GetPerformanceState), msg_ok_return!(GetPerformanceState(2)))],
        );

        let results = sending_node
            .send_message_to_many(
                &vec![receiving_node_1, receiving_node_2],
                &Message::GetPerformanceState,
            )
            .await;

        assert_eq!(results.len(), 2);
        assert_matches!(results[0], Ok(MessageReturn::GetPerformanceState(1)));
        assert_matches!(results[1], Ok(MessageReturn::GetPerformanceState(2)));
    }
}
