// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
///! This module provides types for managing the set of wayland objects for a
///! connection. The |ObjectMap| associates a numeric object id with a
///! |MessageReceiver| that can intepret the message and provide the logic to
///! implement the interface contract for that object.
///!
///! At a high level, the |RequestReceiver<I:Interface>| trait allows for a
///! decoded request to be interacted with. In the middle we provide the
///! |RequestDispatcher| struct that implements the |MessageReceiver| trait
///! by decoding the |Message| into the concrete |Request| type for the
///! |Interface|.
///!
///! Consumers should mostly have to only concern themselves with the
///! |RequestReceiver<I:Interface>| trait, with the other types being mostly the
///! glue and dispatch logic.
///!
///! Ex:
///!    let mut map = ObjectMap::new();
///!    // Add the global wl_display object (object ID 0).
///!    map.add_object(WlDisplay, 0, |request| {
///!        match request {
///!        WlDisplayRequest::GetRegistry {..} => { ... },
///!        WlDispayRequest::Sync {..} => { ... },
///!        }
///!    });
use std::collections::hash_map::{Entry, HashMap};
use std::marker::PhantomData;

use failure::{format_err, Error};

use crate::{FromMessage, Interface, Message};

/// The |ObjectMap| holds the state of active objects for a single connection.
///
/// When a new connection is established, the server should populate the
/// |ObjectMap| with the "wl_display" singleton object. From the the client can
/// query the registry and bind new objects to the interfaces the client
/// understands.
pub struct ObjectMap {
    objects: HashMap<u32, Box<dyn MessageReceiver>>,
}

impl ObjectMap {
    pub fn new() -> Self {
        ObjectMap {
            objects: HashMap::new(),
        }
    }

    /// Reads the message header to find the target for this message and then
    /// forwards the message to the |MessageReceiver|.
    ///
    /// Returns Err if no object is associated with the sender field in the
    /// message header, or if the objects receiver itself fails.
    pub fn receive_message(&mut self, mut message: Message) -> Result<(), Error> {
        let header = message.peek_header()?;
        self.objects
            .get_mut(&header.sender)
            .ok_or(format_err!("object id {} does not exist", header.sender))
            .and_then(|receiver| receiver.receive(message))?;
        Ok(())
    }

    /// Adds a new object into the map that will handle messages with the sender
    /// set to |id|. When a message is received with the corresponding |id|, the
    /// message will be decoded and forwarded to the |RequestReceiver|.
    ///
    /// Returns Err if there is already an object for |id| in this |ObjectMap|.
    pub fn add_object<I: Interface + 'static, R: RequestReceiver<I> + 'static>(
        &mut self, _: I, id: u32, receiver: R,
    ) -> Result<(), Error> {
        if let Entry::Vacant(entry) = self.objects.entry(id) {
            let message_receiver = RequestDispatcher::new(receiver);
            entry.insert(Box::new(message_receiver));
            Ok(())
        } else {
            Err(format_err!("Can't add duplicate object with id {}. ", id))
        }
    }
}

/// A |MessageReceiver| is a type that can accept in-bound messages from a
/// client.
///
/// The server will dispatch |Message|s to the appropriate |MessageReceiver|
/// by reading the sender field in the message header.
pub trait MessageReceiver {
    /// Receive and decode the |Message|. Typically this will perform any
    /// server-side logic required to implement the objects interface as
    /// defined by the protocol.
    fn receive(&mut self, message: Message) -> Result<(), Error>;
}

/// Allow |MessageReceiver|s to be provided as closures.
impl<F> MessageReceiver for F
where
    F: FnMut(Message) -> Result<(), Error>,
{
    fn receive(&mut self, message: Message) -> Result<(), Error> {
        self(message)
    }
}

/// The |RequestReceiver| trait is what high level code will use to work with
/// request messages for a given type.
pub trait RequestReceiver<I: Interface> {
    /// Handle a decoded message for the associated |Interface|.
    fn receive(&mut self, request: I::Request) -> Result<(), Error>;
}

/// Allow |RequestReceiver|s to be provided as closures.
impl<I, F> RequestReceiver<I> for F
where
    I: Interface,
    F: FnMut(I::Request) -> Result<(), Error>,
{
    fn receive(&mut self, request: I::Request) -> Result<(), Error> {
        self(request)
    }
}

/// Implements a |MessageReceiver| that can decode a request into the
/// appropriate request type for an |Interface|, and then invoke an
/// |Implementation|
///
/// This struct essentially is the glue that sits in between the generic
/// |MessageReceiver| trait that is used to dispatch raw message buffers and
/// the higher level |RequestReceiver| that operates on the decoded request
/// enums.
pub(crate) struct RequestDispatcher<I: Interface, R: RequestReceiver<I>> {
    _marker: PhantomData<I>,
    receiver: R,
}

impl<I: Interface, R: RequestReceiver<I>> RequestDispatcher<I, R> {
    pub fn new(receiver: R) -> Self {
        RequestDispatcher {
            receiver,
            _marker: PhantomData,
        }
    }
}

/// Convert the raw Message into the appropriate request type by delegating
/// to the associated |Request| type of |Interface|, and then invoke the
/// receiver.
impl<I: Interface, R: RequestReceiver<I>> MessageReceiver for RequestDispatcher<I, R> {
    fn receive(&mut self, message: Message) -> Result<(), Error> {
        let request: I::Request =
            <<I as Interface>::Request as FromMessage>::from_message(message)?;
        self.receiver.receive(request)?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::RefCell;
    use std::rc::Rc;

    use crate::message::{Arg, MessageHeader};
    use crate::test_protocol::*;

    #[derive(Clone)]
    struct TestReceiver {
        inner: Rc<RefCell<TestReceiverInner>>,
    }

    impl TestReceiver {
        pub fn new() -> Self {
            TestReceiver {
                inner: Rc::new(RefCell::new(TestReceiverInner { request_count: 0 })),
            }
        }
    }

    struct TestReceiverInner {
        request_count: usize,
    }

    impl RequestReceiver<TestInterface> for TestReceiver {
        fn receive(&mut self, request: TestMessage) -> Result<(), Error> {
            if let TestMessage::Message1 = request {
                self.inner.borrow_mut().request_count += 1;
            }
            Ok(())
        }
    }

    #[test]
    fn dispatch_message_to_request_receiver() -> Result<(), Error> {
        let mut objects = ObjectMap::new();
        let mut receiver = TestReceiver::new();
        objects.add_object(TestInterface, 0, receiver.clone())?;

        // Send a sync message; verify it's received.
        let header = MessageHeader {
            sender: 0,
            opcode: 0,
            length: 12,
        };
        let mut message = Message::new();
        message.write_header(&header)?;
        message.write_arg(Arg::NewId(1))?;
        message.rewind();
        objects.receive_message(message)?;

        assert_eq!(1, receiver.inner.borrow().request_count);
        Ok(())
    }

    #[test]
    fn add_object_duplicate_id() -> Result<(), Error> {
        let mut objects = ObjectMap::new();
        let mut receiver = TestReceiver::new();
        assert!(
            objects
                .add_object(TestInterface, 0, receiver.clone())
                .is_ok()
        );
        assert!(
            objects
                .add_object(TestInterface, 0, receiver.clone())
                .is_err()
        );
        Ok(())
    }

    #[test]
    fn dispatch_message_to_invalid_id() -> Result<(), Error> {
        // Send a message to an empty map.
        let mut objects = ObjectMap::new();
        let header = MessageHeader {
            sender: 0,
            opcode: 0,
            length: 8,
        };
        let mut message = Message::new();
        message.write_header(&header)?;
        message.rewind();
        assert!(objects.receive_message(message).is_err());
        Ok(())
    }
}
