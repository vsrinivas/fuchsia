// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::action_fuse::{ActionFuse, ActionFuseBuilder, ActionFuseHandle};
use crate::message::base::{
    Address, Audience, Message, MessageClientId, MessageType, Payload, Role, Signature, Status,
};
use crate::message::beacon::BeaconBuilder;
use crate::message::message_builder::MessageBuilder;
use crate::message::messenger::Messenger;
use crate::message::receptor::Receptor;
#[cfg(test)]
use crate::message::Timestamp;

/// MessageClient provides a subset of Messenger functionality around a specific
/// delivered message. The client may duplicate/move the MessageClient as
/// desired. Once all MessageClient instances go out of scope, the original
/// message is forwarded to the next Messenger if no interaction preceded it.
#[derive(Clone, Debug)]
pub struct MessageClient<P: Payload + 'static, A: Address + 'static, R: Role + 'static> {
    // A unique identifier that identifies this client within the parent message
    // hub.
    id: MessageClientId,
    // The "source" message for the client. Any replies or action are done in the
    // context of this message.
    message: Message<P, A, R>,
    // The messenger to receive any actions.
    messenger: Messenger<P, A, R>,
    // Auto-trigger for automatically forwarding the message to the next
    // recipient.
    forwarder: ActionFuseHandle,
}

impl<P: Payload + 'static, A: Address + 'static, R: Role + 'static> PartialEq
    for MessageClient<P, A, R>
{
    fn eq(&self, other: &MessageClient<P, A, R>) -> bool {
        other.id == self.id
    }
}

impl<P: Payload + 'static, A: Address + 'static, R: Role + 'static> MessageClient<P, A, R> {
    pub(super) fn new(
        id: MessageClientId,
        message: Message<P, A, R>,
        messenger: Messenger<P, A, R>,
    ) -> MessageClient<P, A, R> {
        let fuse_messenger_clone = messenger.clone();
        let fuse_message_clone = message.clone();
        MessageClient {
            id,
            message,
            messenger,
            forwarder: ActionFuseBuilder::new()
                .add_action(Box::new(move || {
                    fuse_messenger_clone.forward(fuse_message_clone.clone(), None);
                }))
                .build(),
        }
    }

    #[cfg(test)]
    pub(crate) fn get_timestamp(&self) -> Timestamp {
        self.message.get_timestamp()
    }

    #[cfg(test)]
    pub(crate) fn get_modifiers(&self) -> Vec<Signature<A>> {
        self.message.get_modifiers()
    }

    /// Returns the Signature of the original author of the associated Message.
    /// This value can be used to communicate with the author at top-level
    /// communication.
    pub(crate) fn get_author(&self) -> Signature<A> {
        self.message.get_author()
    }

    /// Returns the audience associated with the underlying [`Message`]. If it
    /// is a new [`Message`] (origin), it will be the target audience.
    /// Otherwise it is the author of the reply.
    pub(crate) fn get_audience(&self) -> Audience<A, R> {
        match self.message.get_type() {
            MessageType::Origin(audience) => audience.clone(),
            MessageType::Reply(message) => Audience::Messenger(message.get_author()),
        }
    }

    /// Creates a dedicated receptor for receiving future communication on this message thread.
    pub(crate) fn spawn_observer(&mut self) -> Receptor<P, A, R> {
        let (beacon, receptor) = BeaconBuilder::new(self.messenger.clone()).build();
        self.messenger.forward(self.message.clone(), Some(beacon));
        ActionFuse::defuse(self.forwarder.clone());

        receptor
    }

    /// Creates a MessageBuilder for the reply to this message.
    pub(crate) fn reply(&self, payload: P) -> MessageBuilder<P, A, R> {
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

    /// Propagates a derived message on the path of the original message.
    pub(crate) fn propagate(&self, payload: P) -> MessageBuilder<P, A, R> {
        MessageBuilder::derive(payload, self.message.clone(), self.messenger.clone())
            .auto_forwarder(self.forwarder.clone())
    }

    /// Report back to the clients that the message has been acknowledged.
    pub(crate) async fn acknowledge(&mut self) {
        self.message.report_status(Status::Acknowledged).await;
    }

    /// Tracks the lifetime of the reply listener, firing the fuse when it
    /// goes out of scope.
    pub(crate) async fn bind_to_recipient(&mut self, fuse: ActionFuseHandle) {
        self.message.bind_to_author(fuse).await;
    }
}
