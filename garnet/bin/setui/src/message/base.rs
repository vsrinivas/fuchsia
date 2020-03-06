// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::beacon::Beacon;
use crate::message::message_client::MessageClient;
use futures::channel::mpsc::UnboundedSender;
use std::hash::Hash;

/// Trait alias for types of data that can be used as the payload in a
/// MessageHub.
pub trait Payload: Clone + Send + Sync {}
impl<T: Clone + Send + Sync> Payload for T {}

/// Trait alias for types of data that can be used as an address in a
/// MessageHub.
pub trait Address: Clone + Eq + Hash + Send + Sync {}
impl<T: Clone + Eq + Hash + Send + Sync> Address for T {}

/// A MessageEvent defines the data that can be returned through a message
/// receptor.
pub enum MessageEvent<P: Payload + 'static, A: Address + 'static> {
    /// A message that has been delivered, either as a new message directed at to
    /// the recipient's address or a reply to a previously sent message
    /// (dependent on the receptor's context).
    Message(MessageClient<P, A>),
    /// A status update for the message that spawned the receptor delivering this
    /// update.
    Status(DeliveryStatus),
}

/// The types of results possible from sending or replying.
#[derive(Clone, PartialEq, Debug)]
pub enum DeliveryStatus {
    // Sent to some audience, potentially no one.
    Broadcasted,
    // Received by the intended address.
    Received,
    // Could not be delivered to the specified target.
    Undeliverable,
}

/// The intended recipients for a message.
#[derive(Clone)]
pub enum Audience<A> {
    // All non-broker messengers outside of the sender.
    Broadcast,
    // The messenger at the specified address.
    Address(A),
}

/// The messengers that can participate in messaging
#[derive(Clone)]
pub enum MessengerType<A: Address + 'static> {
    /// An endpoint in the messenger graph. Can have messages specifically
    /// addressed to it and can author new messages.
    Addressable(A),
    /// A intermediary messenger. Will receive every forwarded message. Brokers
    /// are able to send and reply to messages, but the main purpose is to observe
    /// messages.
    Broker,
}

/// MessageType captures details about the Message's source.
#[derive(Clone)]
pub enum MessageType<P: Payload + 'static, A: Address + 'static> {
    /// A completely new message that is intended for the specified audience.
    Origin(Audience<A>),
    /// A response to a previously received message. Note that the value must
    /// be boxed to mitigate recursive sizing issues as MessageType is held by
    /// Message.
    Reply(Box<Message<P, A>>),
}

/// The core messaging unit. A Message may be annotated by messengers, but is
/// not associated with a particular Messenger instance.
#[derive(Clone)]
pub struct Message<P: Payload + 'static, A: Address + 'static> {
    payload: P,
    message_type: MessageType<P, A>,
    // The return path is generated while the message is passed from messenger
    // to messenger on the way to the intended recipient. It indicates the
    // messengers that would like to be informed of replies to this message.
    // The message author is always the last element in this vector. New
    // participants are pushed to the front.
    return_path: Vec<Beacon<P, A>>,
}

impl<P: Payload + 'static, A: Address + 'static> Message<P, A> {
    /// Returns a new Message instance. Only the MessageHub can mint new messages.
    pub(super) fn new(payload: P, message_type: MessageType<P, A>) -> Message<P, A> {
        Message { payload: payload, message_type: message_type, return_path: vec![] }
    }

    /// Adds an entity to be notified on any replies.
    pub(super) fn add_participant(&mut self, participant: Beacon<P, A>) {
        self.return_path.insert(0, participant);
    }

    /// Returns the list of participants for the reply return path.
    pub(super) fn get_return_path(&self) -> Vec<Beacon<P, A>> {
        return self.return_path.clone();
    }

    /// Returns the message's type.
    pub(super) fn get_message_type(&self) -> MessageType<P, A> {
        return self.message_type.clone();
    }

    /// Returns a copy of the message's payload.
    pub fn payload(&self) -> P {
        self.payload.clone()
    }

    /// Delivers the supplied status to all participants in the return path.
    pub(super) async fn report_status(&mut self, status: DeliveryStatus) {
        for beacon in self.return_path.clone() {
            beacon.status(status.clone()).await.ok();
        }
    }
}

/// Type definition for a sender handed by the MessageHub to messengers to
/// send actions.
pub(super) type ActionSender<P, A> =
    UnboundedSender<(MessengerId, MessageAction<P, A>, Option<Beacon<P, A>>)>;

/// An internal identifier used by the MessageHub to identify messengers.
pub(super) type MessengerId = usize;

/// Internal representation for possible actions on a message.
pub(super) enum MessageAction<P: Payload + 'static, A: Address + 'static> {
    // A new message sent to the specified audience.
    Send(P, MessageType<P, A>),
    // The message has been forwarded by the current holder.
    Forward(Message<P, A>),
}
