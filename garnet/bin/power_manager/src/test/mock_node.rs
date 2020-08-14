// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error::PowerManagerError;
use crate::message::{Message, MessageReturn};
use crate::node::Node;
use async_trait::async_trait;
use std::cell::{Cell, RefCell};
use std::rc::Rc;

/// Convenience macro for specifying a MessageMatcher while creating a MockNode.
#[macro_export]
macro_rules! msg_eq {
    ($($msg:tt)*) => {
        MessageMatcher::Eq(Message::$($msg)*)
    };
}

/// Convenience macro for specifying a MessageReturn value while creating a MockNode.
#[macro_export]
macro_rules! msg_ok_return {
    ($($msg_ret:tt)*) => {
        Ok(MessageReturn::$($msg_ret)*)
    };
}

/// Emulate the behavior of a Node object by handling incoming messages and responding with
/// specified data.
struct MockNode {
    /// Name of this MockNode, used mainly for logging
    name: String,

    /// A vector of (Message, Result) pairs. This specifies the list of Messages the MockNode
    /// expects to receive, along with the Result that the MockNode will respond with.
    msg_response_pairs: RefCell<Vec<(MessageMatcher, Result<MessageReturn, PowerManagerError>)>>,

    /// A count that increases each time the MockNode receives a message, used mainly for logging.
    msg_rcv_count: Cell<u32>,
}

/// Represents the comparison method to be used for determining if the underlying Message matches
/// another Message. To be considered a match, the two Message objects must be of the same variant
/// and their argument values must match according to the comparison method.
#[derive(Debug)]
pub enum MessageMatcher {
    Eq(Message), // Message arguments are equal
}

impl MessageMatcher {
    /// Compare the underlying Message with the specified `message` using the appropriate comparison
    /// type as determined by the MessageMatcher variant.
    fn is_match(&self, message: &Message) -> bool {
        match self {
            MessageMatcher::Eq(this_msg) => {
                match_variant(&this_msg, message) && this_msg == message
            }
        }
    }
}

/// Returns true if the two messages are of the same Message variant.
fn match_variant(msg1: &Message, msg2: &Message) -> bool {
    std::mem::discriminant(msg1) == std::mem::discriminant(msg2)
}

#[async_trait(?Send)]
impl Node for MockNode {
    fn name(&self) -> String {
        self.name.clone()
    }

    async fn handle_message(&self, msg: &Message) -> Result<MessageReturn, PowerManagerError> {
        self.msg_rcv_count.set(self.msg_rcv_count.get() + 1);

        // Verify the vector of expected messages is not empty
        assert!(
            self.msg_response_pairs.borrow().len() > 0,
            "{} received more messages than expected (message count: {}; message: {:?}",
            self.name(),
            self.msg_rcv_count.get(),
            msg
        );

        // Get the next MessageMatcher and Result in the vector
        let (msg_matcher, response) = self.msg_response_pairs.borrow_mut().pop().unwrap();

        // Verify the incoming Message is a match
        assert!(
            msg_matcher.is_match(msg),
            "{} received unexpected Message (received {:?}; expected {:?}",
            self.name(),
            msg,
            msg_matcher
        );

        // Reply with the specified response
        response
    }
}

/// Implement Drop for the MockNode so that we can verify all expected messages have been received
/// when the MockNode is finally dropped.
impl Drop for MockNode {
    fn drop(&mut self) {
        assert_eq!(
            self.msg_response_pairs.borrow().len(),
            0,
            "{} expected to receive more messages ({:?})",
            self.name(),
            self.msg_response_pairs.borrow().iter().map(|(msg_matcher, _result)| msg_matcher)
        );
    }
}

/// Creates a new MockNode with the specified name and (MessageMatcher, Result) pairs.
pub fn create_mock_node(
    name: &'static str,
    mut msg_response_pairs: Vec<(MessageMatcher, Result<MessageReturn, PowerManagerError>)>,
) -> Rc<dyn Node> {
    // Reverse the vector so that `pop` gives us the elements in the same order they were specified
    msg_response_pairs.reverse();

    Rc::new(MockNode {
        name: name.to_string(),
        msg_response_pairs: RefCell::new(msg_response_pairs),
        msg_rcv_count: Cell::new(0),
    })
}

/// A mock node which responds to all messages with an error. Intended to be used as a "don't care"
/// mock node. This is useful when a mock node is needed in order to construct the node under test
/// and the messages/responses to/from the mock node are not important.
struct DummyNode {}

#[async_trait(?Send)]
impl Node for DummyNode {
    fn name(&self) -> String {
        "DummyNode".to_string()
    }

    async fn handle_message(&self, _msg: &Message) -> Result<MessageReturn, PowerManagerError> {
        Err(PowerManagerError::Unsupported)
    }
}

/// Creates a new DummyNode.
pub fn create_dummy_node() -> Rc<dyn Node> {
    Rc::new(DummyNode {})
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;
    use matches::assert_matches;

    /// Tests that receiving an unexpected Message variant results in a panic.
    #[fasync::run_singlethreaded(test)]
    #[should_panic]
    async fn test_incorrect_rcv_message_panic() {
        let mock_node = create_mock_node(
            "MockNode",
            vec![(
                MessageMatcher::Eq(Message::GetTotalCpuLoad),
                Ok(MessageReturn::GetTotalCpuLoad(4.0)),
            )],
        );
        let _ = mock_node.handle_message(&Message::GetNumCpus).await;
    }

    /// Tests that sending an expected Message results in the specified response.
    #[fasync::run_singlethreaded(test)]
    async fn test_message_response() {
        let mock_node = create_mock_node(
            "MockNode",
            vec![(
                MessageMatcher::Eq(Message::GetPerformanceState),
                Ok(MessageReturn::GetPerformanceState(3)),
            )],
        );

        match mock_node.handle_message(&Message::GetPerformanceState).await {
            Ok(MessageReturn::GetPerformanceState(3)) => {}
            e => panic!("Unexpected return value: {:?}", e),
        }
    }

    /// Tests that expecting an equal Message match but sending a non-equal Message results in a
    /// panic.
    #[fasync::run_singlethreaded(test)]
    #[should_panic]
    async fn test_message_arg_eq_mismatch_panic() {
        let mock_node = create_mock_node(
            "MockNode",
            vec![(
                MessageMatcher::Eq(Message::SetPerformanceState(2)),
                Ok(MessageReturn::SetPerformanceState),
            )],
        );
        let _ = mock_node.handle_message(&Message::SetPerformanceState(1)).await;
    }

    /// Tests that dropping a MockNode while it's still expecting to receive a Message results in a
    /// panic.
    #[test]
    #[should_panic]
    fn test_leftover_messages_panic() {
        let _mock_node = create_mock_node(
            "MockNode",
            vec![(
                MessageMatcher::Eq(Message::GetTotalCpuLoad),
                Ok(MessageReturn::GetTotalCpuLoad(4.0)),
            )],
        );
    }

    /// Tests that the `msg_<comparison>` family of macros expands to the expected values.
    #[test]
    fn test_msg_matcher_macros() {
        // Test the `msg_eq` macro
        match msg_eq!(SetPerformanceState(1)) {
            MessageMatcher::Eq(Message::SetPerformanceState(1)) => {}
            e => panic!("Unexpected value expanded from msg_eq!(): {:?}", e),
        }
    }

    /// Tests that the `msg_<result>_<messagereturn>` family of macros expands to the expected
    /// values.
    #[test]
    fn test_msg_return_macros() {
        // Test the `msg_ok_return` macro. The compiler can't infer the Err type coming from the
        // macro call, so type annotation is required here.
        let ret: Result<MessageReturn, PowerManagerError> = msg_ok_return!(GetNumCpus(4));
        match ret {
            Ok(MessageReturn::GetNumCpus(4)) => {}
            e => panic!("Unexpected expression expanded from msg_ok_return!(): {:?}", e),
        }
    }

    /// Tests that a DummyNode responds to an arbitrary Message with
    /// Err(PowerManagerError::Unsupported).
    #[fasync::run_singlethreaded(test)]
    async fn test_dummy_node() {
        let node = create_dummy_node();
        assert_matches!(
            node.handle_message(&Message::SetPerformanceState(1)).await,
            Err(PowerManagerError::Unsupported)
        )
    }
}
