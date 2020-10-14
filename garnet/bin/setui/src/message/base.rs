// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::internal::common::Timestamp;
use crate::message::action_fuse::ActionFuseHandle;
use crate::message::beacon::Beacon;
use crate::message::message_client::MessageClient;
use crate::message::messenger::{Messenger, MessengerClient};
use crate::message::receptor::Receptor;
use futures::channel::mpsc::UnboundedSender;
use futures::channel::oneshot::Sender;
use std::collections::HashSet;
use std::fmt::Debug;
use std::hash::Hash;
use thiserror::Error;

/// Trait alias for types of data that can be used as the payload in a
/// MessageHub.
pub trait Payload: Clone + Debug + Send + Sync {}
impl<T: Clone + Debug + Send + Sync> Payload for T {}

/// Trait alias for types of data that can be used as an address in a
/// MessageHub.
pub trait Address: Clone + Debug + Eq + Hash + Send + Sync {}
impl<T: Clone + Debug + Eq + Hash + Send + Sync> Address for T {}

/// A MessageEvent defines the data that can be returned through a message
/// receptor.
#[derive(Debug, PartialEq)]
pub enum MessageEvent<P: Payload + 'static, A: Address + 'static> {
    /// A message that has been delivered, either as a new message directed at to
    /// the recipient's address or a reply to a previously sent message
    /// (dependent on the receptor's context).
    Message(P, MessageClient<P, A>),
    /// A status update for the message that spawned the receptor delivering this
    /// update.
    Status(Status),
}

#[derive(Error, Debug, Clone)]
pub enum MessageError<A: Address + 'static> {
    #[error("Address conflig:{address:?} already exists")]
    AddressConflict { address: A },
    #[error("Unexpected Error")]
    Unexpected,
}

/// The types of results possible from sending or replying.
#[derive(Clone, PartialEq, Debug)]
pub enum Status {
    // Sent to some audience, potentially no one.
    Broadcasted,
    // Received by the intended address.
    Received,
    // Could not be delivered to the specified target.
    // TODO(61469): add intended address to this enum.
    Undeliverable,
    // Acknowledged by a recipient.
    Acknowledged,
    Timeout,
}

/// The intended recipients for a message.
#[derive(Clone, Debug, PartialEq, Hash, Eq)]
pub enum Audience<A: Address + 'static> {
    // All non-broker messengers outside of the sender.
    Broadcast,
    // A Audience Group.
    Group(group::Group<A>),
    // The messenger at the specified address.
    Address(A),
    // The messenger with the specified signature.
    Messenger(Signature<A>),
}

impl<A: Address + 'static> Audience<A> {
    /// Indicates whether a message directed towards this `Audience` must match
    /// to a messenger or if it's okay for the message to be delivered to no
    /// one. For example, broadcasts are meant to be delivered to any
    /// (potentially no) messenger.
    pub fn requires_delivery(&self) -> bool {
        match self {
            Audience::Broadcast => false,
            Audience::Group(group) => {
                group.audiences.iter().any(|audience| audience.requires_delivery())
            }
            Audience::Address(_) | Audience::Messenger(_) => true,
        }
    }

    pub fn contains(&self, audience: &Audience<A>) -> bool {
        audience.flatten().is_subset(&self.flatten())
    }

    fn flatten(&self) -> HashSet<Audience<A>> {
        match self {
            Audience::Group(group) => {
                group.audiences.iter().map(|audience| audience.flatten()).flatten().collect()
            }
            _ => [self.clone()].iter().cloned().collect(),
        }
    }
}

pub mod group {
    use super::{Address, Audience};
    #[derive(Clone, Debug, PartialEq, Hash, Eq)]
    pub struct Group<A: Address + 'static> {
        pub audiences: Vec<Audience<A>>,
    }

    impl<A: Address + 'static> Group<A> {
        pub fn contains(&self, audience: &Audience<A>) -> bool {
            for target in &self.audiences {
                if target == audience {
                    return true;
                } else if let Audience::Group(group) = target {
                    if group.contains(audience) {
                        return true;
                    }
                }
            }
            return false;
        }
    }

    pub struct Builder<A: Address + 'static> {
        audiences: Vec<Audience<A>>,
    }

    impl<A: Address + 'static> Builder<A> {
        pub fn new() -> Self {
            Self { audiences: vec![] }
        }

        pub fn add(mut self, audience: Audience<A>) -> Self {
            self.audiences.push(audience);
            self
        }

        pub fn build(self) -> Group<A> {
            Group { audiences: self.audiences }
        }
    }
}
/// An identifier that can be used to send messages directly to a Messenger.
/// Included with Message instances.
#[derive(Copy, Clone, Debug, PartialEq, Hash, Eq)]
pub enum Signature<A> {
    // Messenger at a given address.
    Address(A),

    // The messenger cannot be directly addressed.
    Anonymous(MessengerId),
}

#[derive(Copy, Clone, Debug)]
pub struct Fingerprint<A> {
    pub id: MessengerId,
    pub signature: Signature<A>,
}

/// The messengers that can participate in messaging
#[derive(Clone, Debug)]
pub enum MessengerType<P: Payload + 'static, A: Address + 'static> {
    /// An endpoint in the messenger graph. Can have messages specifically
    /// addressed to it and can author new messages.
    Addressable(A),
    /// A intermediary messenger. Will receive every forwarded message. Brokers
    /// are able to send and reply to messages, but the main purpose is to observe
    /// messages. An optional filter may be specified, which limits the messages
    /// directed to this broker.
    Broker(Option<filter::Filter<P, A>>),
    /// A messenger that cannot be reached by an address.
    Unbound,
}

pub mod filter {
    use super::{Address, Audience, Message, MessageType, Payload};
    use core::fmt::{Debug, Formatter};
    use std::sync::Arc;

    /// `Condition` allows specifying a filter condition that must be true
    /// for a filter to match.
    #[derive(Clone)]
    pub enum Condition<P: Payload + 'static, A: Address + 'static> {
        /// Matches on the message's intended audience as specified by the
        /// sender.
        Audience(Audience<A>),
        /// Matches on a custom closure that may evaluate the sent message.
        Custom(Arc<dyn Fn(&Message<P, A>) -> bool + Send + Sync>),
        /// Matches on another filter and its conditions.
        Filter(Filter<P, A>),
    }

    /// We must implement Debug since the `Condition::Custom` does not provide
    /// a `Debug` implementation.
    impl<P: Payload + 'static, A: Address + 'static> Debug for Condition<P, A> {
        fn fmt(&self, f: &mut Formatter<'_>) -> core::fmt::Result {
            let condition = match self {
                Condition::Audience(audience) => format!("audience:{:?}", audience),
                Condition::Custom(_) => "custom".to_string(),
                Condition::Filter(filter) => format!("filter:{:?}", filter),
            };

            write!(f, "Condition: {:?}", condition)
        }
    }

    /// `Conjugation` dictates how multiple conditions are combined to determine
    /// a match.
    #[derive(Clone, Debug, PartialEq)]
    pub enum Conjugation {
        /// All conditions must match.
        All,
        /// Any condition may declare a match.
        Any,
    }

    /// `Builder` provides a convenient way to construct a [`Filter`] based on
    /// a number of conditions.
    ///
    /// [`Filter`]: struct.Filter.html
    pub struct Builder<P: Payload + 'static, A: Address + 'static> {
        conjugation: Conjugation,
        conditions: Vec<Condition<P, A>>,
    }

    impl<P: Payload + 'static, A: Address + 'static> Builder<P, A> {
        pub fn new(condition: Condition<P, A>, conjugation: Conjugation) -> Self {
            Self { conjugation, conditions: vec![condition] }
        }

        /// Shorthand method to create a filter based on a single condition.
        pub fn single(condition: Condition<P, A>) -> Filter<P, A> {
            Builder::new(condition, Conjugation::All).build()
        }

        /// Adds an additional condition to the filter under construction.
        pub fn append(mut self, condition: Condition<P, A>) -> Self {
            self.conditions.push(condition);

            self
        }

        pub fn build(self) -> Filter<P, A> {
            Filter { conjugation: self.conjugation, conditions: self.conditions }
        }
    }

    /// `Filter` is used by the `MessageHub` to determine whether an incoming
    /// message should be directed to associated broker.
    #[derive(Clone, Debug)]
    pub struct Filter<P: Payload + 'static, A: Address + 'static> {
        conjugation: Conjugation,
        conditions: Vec<Condition<P, A>>,
    }

    impl<P: Payload + 'static, A: Address + 'static> Filter<P, A> {
        pub fn matches(&self, message: &Message<P, A>) -> bool {
            for condition in &self.conditions {
                let match_found = match condition {
                    Condition::Audience(audience) => matches!(
                            message.get_message_type(),
                            MessageType::Origin(target) if target.contains(audience)),
                    Condition::Custom(check_fn) => (check_fn)(message),
                    Condition::Filter(filter) => filter.matches(&message),
                };
                if match_found {
                    if self.conjugation == Conjugation::Any {
                        return true;
                    }
                } else if self.conjugation == Conjugation::All {
                    return false;
                }
            }

            return self.conjugation == Conjugation::All;
        }
    }
}

/// MessageType captures details about the Message's source.
#[derive(Clone, Debug)]
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
#[derive(Clone, Debug)]
pub struct Message<P: Payload + 'static, A: Address + 'static> {
    author: Fingerprint<A>,
    timestamp: Timestamp,
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
    pub(super) fn new(
        author: Fingerprint<A>,
        timestamp: Timestamp,
        payload: P,
        message_type: MessageType<P, A>,
    ) -> Message<P, A> {
        Message {
            author: author,
            timestamp: timestamp,
            payload: payload,
            message_type: message_type,
            return_path: vec![],
        }
    }

    /// Adds an entity to be notified on any replies.
    pub(super) fn add_participant(&mut self, participant: Beacon<P, A>) {
        self.return_path.insert(0, participant);
    }

    pub fn get_timestamp(&self) -> Timestamp {
        self.timestamp.clone()
    }

    pub fn get_author(&self) -> Signature<A> {
        self.author.signature.clone()
    }

    /// Binds the action fuse to the author's receptor. The fuse will fire
    /// once that receptor is released.
    pub(super) async fn bind_to_author(&mut self, fuse: ActionFuseHandle) {
        if let Some(beacon) = self.return_path.last_mut() {
            beacon.add_fuse(fuse).await;
        }
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
    pub(super) async fn report_status(&mut self, status: Status) {
        for beacon in self.return_path.clone() {
            beacon.status(status.clone()).await.ok();
        }
    }
}

/// Type definition for a sender handed by the MessageHub to messengers to
/// send actions.
pub(super) type ActionSender<P, A> =
    UnboundedSender<(Fingerprint<A>, MessageAction<P, A>, Option<Beacon<P, A>>)>;

/// An internal identifier used by the MessageHub to identify messengers.
pub(super) type MessengerId = usize;

/// An internal identifier used by the `MessageHub` to identify `MessageClient`.
pub(super) type MessageClientId = usize;

pub(super) type CreateMessengerResult<P, A> =
    Result<(MessengerClient<P, A>, Receptor<P, A>), MessageError<A>>;

/// Callback for handing back a messenger
pub(super) type MessengerSender<P, A> = Sender<CreateMessengerResult<P, A>>;

/// Type definition for a sender handed by the MessageHub to spawned components
/// (messenger factories and messengers) to control messengers.
pub(super) type MessengerActionSender<P, A> = UnboundedSender<MessengerAction<P, A>>;

/// Internal representation of possible actions around a messenger.
pub(super) enum MessengerAction<P: Payload + 'static, A: Address + 'static> {
    /// Creates a top level messenger
    Create(MessengerType<P, A>, MessengerSender<P, A>, MessengerActionSender<P, A>),
    /// Deletes a given messenger
    Delete(Messenger<P, A>),
    /// Deletes a messenger by its `Signature`
    DeleteBySignature(Signature<A>),
}

/// Internal representation for possible actions on a message.
#[derive(Debug)]
pub(super) enum MessageAction<P: Payload + 'static, A: Address + 'static> {
    // A new message sent to the specified audience.
    Send(P, MessageType<P, A>, Timestamp),
    // The message has been forwarded by the current holder.
    Forward(Message<P, A>),
}
