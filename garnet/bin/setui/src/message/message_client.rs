// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::action_fuse::{ActionFuse, ActionFuseHandle};
use crate::message::base::{Address, Message, MessageType, Payload};
use crate::message::beacon::Beacon;
use crate::message::message_builder::MessageBuilder;
use crate::message::messenger::Messenger;
use crate::message::receptor::Receptor;

/// MessageClient provides a subset of Messenger functionality around a specific
/// delivered message. The client may duplicate/move the MessageClient as
/// desired. Once all MessageClient instances go out of scope, the original
/// message is forwarded to the next Messenger if no interaction preceded it.
#[derive(Clone)]
pub struct MessageClient<P: Payload + 'static, A: Address + 'static> {
    // The "source" message for the client. Any replies or action are done in the
    // context of this message.
    message: Message<P, A>,
    // The messenger to receive any actions.
    messenger: Messenger<P, A>,
    // Auto-trigger for automatically forwarding the message to the next
    // recipient.
    forwarder: ActionFuseHandle,
}

impl<P: Payload + 'static, A: Address + 'static> MessageClient<P, A> {
    pub(super) fn new(message: Message<P, A>, messenger: Messenger<P, A>) -> MessageClient<P, A> {
        let fuse_messenger_clone = messenger.clone();
        let fuse_message_clone = message.clone();
        MessageClient {
            message: message.clone(),
            messenger: messenger.clone(),
            forwarder: ActionFuse::create(Box::new(move || {
                fuse_messenger_clone.forward(fuse_message_clone.clone(), None);
            })),
        }
    }

    /// Returns the payload associated with the associated Message.
    pub fn get_payload(&self) -> P {
        self.message.payload()
    }

    /// Marks this client's Messenger as an active participant in any return
    /// communication.
    pub fn observe(&mut self) -> Receptor<P, A> {
        let (beacon, receptor) = Beacon::create(self.messenger.clone());
        self.messenger.forward(self.message.clone(), Some(beacon));
        ActionFuse::defuse(self.forwarder.clone());

        receptor
    }

    /// Creates a MessageBuilder for the reply to this message.
    pub fn reply(&mut self, payload: P) -> MessageBuilder<P, A> {
        // Return a MessageBuilder for a reply. Note that the auto-forwarder is
        // handed off so the automatic forwarding behavior follows the
        // MessageBuilder rather than this MessageClient.
        MessageBuilder::new(
            payload,
            MessageType::Reply(Box::new(self.message.clone())),
            self.messenger.clone(),
        )
        .auto_forwarder(self.forwarder.clone())
    }
}
