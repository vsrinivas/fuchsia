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
pub trait Address: Clone + Debug + Eq + Hash + Unpin + Send + Sync {}
impl<T: Clone + Debug + Eq + Hash + Unpin + Send + Sync> Address for T {}

/// Trait alias for types of data that can be used as a role in a
/// MessageHub.
pub trait Role: Clone + Debug + Eq + Hash + Send + Sync {}
impl<T: Clone + Debug + Eq + Hash + Send + Sync> Role for T {}

/// A mod for housing common definitions for messengers. Messengers are
/// MessageHub participants, which are capable of sending and receiving
/// messages.
pub(super) mod messenger {
    use super::{role, Address, MessengerType, Payload, Role};
    use std::collections::HashSet;

    /// `Descriptor` is a blueprint for creating a messenger. It is sent to the
    /// MessageHub by clients, which interprets the information to build the
    /// messenger.
    #[derive(Clone)]
    pub struct Descriptor<P: Payload + 'static, A: Address + 'static, R: Role + 'static> {
        /// The type of messenger to be created. This determines how messages
        /// can be directed to a messenger created from this Descriptor.
        /// Please reference [`Audience`] to see how these types map to audience
        /// targets.
        pub messenger_type: MessengerType<P, A, R>,
        /// The roles to associate with this messenger. When a messenger
        /// is associated with a given [`Role`], any message directed to that
        /// [`Role`] will be delivered to the messenger.
        pub roles: HashSet<role::Signature<R>>,
    }
}

/// A MessageEvent defines the data that can be returned through a message
/// receptor.
#[derive(Debug, PartialEq)]
pub enum MessageEvent<P: Payload + 'static, A: Address + 'static, R: Role + 'static = default::Role>
{
    /// A message that has been delivered, either as a new message directed at to
    /// the recipient's address or a reply to a previously sent message
    /// (dependent on the receptor's context).
    Message(P, MessageClient<P, A, R>),
    /// A status update for the message that spawned the receptor delivering this
    /// update.
    Status(Status),
}

/// This mod contains common definitions for working with [`Role`]. [`Role`]
/// defines a group which messengers can belong to and messages can be directed
/// to.
pub mod role {
    use super::Role;
    use futures::channel::mpsc::UnboundedSender;
    use futures::channel::oneshot::Sender;

    /// An enumeration of role-related actions that can be requested upon the
    /// MessageHub.
    #[allow(dead_code)]
    pub(in crate::message) enum Action<R: Role + 'static> {
        /// Creates an anonymous Role at runtime.
        Create(ResultSender<R>),
    }

    /// A sender given to MessageHub clients to relay role-related requests.
    pub(in crate::message) type ActionSender<R> = UnboundedSender<Action<R>>;

    /// A sender passed along with some [`Action`] types in order to send back a
    /// response.
    pub(in crate::message) type ResultSender<R> = Sender<Result<Response<R>, Error>>;

    /// The return value in response to an [`Action`] upon success.
    pub(in crate::message) enum Response<R: Role + 'static> {
        Role(Signature<R>),
    }

    /// The error types sent when [`Action`] fails.
    #[derive(thiserror::Error, Debug, Clone, PartialEq)]
    pub enum Error {
        /// The MessageHub handed back a response type we weren't expecting.
        #[error("Unexpected response")]
        UnexpectedResponse,
        /// There was an issue communicating back the response.
        #[error("Communication Error")]
        CommunicationError,
    }

    /// The public representation of a role. `Signature` is used for adding a
    /// messenger to a particular role group and targeting a particular group
    /// as the audience for an outbound message.
    #[derive(PartialEq, Copy, Clone, Debug, Eq, Hash)]
    pub struct Signature<R: Role + 'static> {
        signature_type: SignatureType<R>,
    }

    impl<R: Role + 'static> Signature<R> {
        /// Returns a `Signature` based on the a predefined role.
        pub fn role(role: R) -> Self {
            Self { signature_type: SignatureType::Role(role) }
        }

        /// Returns a `Signature` based on a generated or anonymous role. This
        /// `Signature` can only be generated by the MessageHub.
        pub(in crate::message) fn handle(handle: Handle) -> Self {
            Self { signature_type: SignatureType::Anonymous(handle) }
        }
    }

    /// An enumeration of role types used internally in [`Signature`] to
    /// uniquely identify the role.
    #[derive(PartialEq, Copy, Clone, Debug, Eq, Hash)]
    enum SignatureType<R: Role + 'static> {
        Role(R),
        Anonymous(Handle),
    }

    /// A `Handle` is a reference to a role generated at runtime. `Handle`
    /// should be transparent to the client and only produced as part of a
    /// [`Signature`] variant through the MessageHub.
    #[derive(PartialEq, Copy, Clone, Debug, Eq, Hash)]
    pub(in crate::message) struct Handle {
        id: usize,
    }

    impl Handle {
        pub(super) fn new(id: usize) -> Self {
            Handle { id }
        }
    }

    /// `Generator` is a helper for generating roles at runtime. Each invocation
    /// produces a [`Signature`] for a unique anonymous role.
    pub(in crate::message) struct Generator {
        next_id: usize,
    }

    impl Generator {
        /// Instantiates a new `Generator`.
        pub(in crate::message) fn new() -> Self {
            Self { next_id: 0 }
        }

        /// Produces a `Signature` referencing a unique role at runtime.
        pub(in crate::message) fn generate<R: Role + 'static>(&mut self) -> Signature<R> {
            let handle = Handle::new(self.next_id);
            self.next_id += 1;

            Signature::handle(handle)
        }
    }
}

/// This mod contains the default type definitions for the MessageHub's type
/// parameters when not specified.
pub mod default {
    /// `Address` provides a [`Address`] definition for message hubs not needing
    /// an address.
    #[derive(PartialEq, Copy, Clone, Debug, Eq, Hash)]
    pub enum Address {}

    /// `Role` provides a [`Role`] definition for message hubs not needing
    /// roles.
    #[derive(PartialEq, Copy, Clone, Debug, Eq, Hash)]
    pub enum Role {}
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
pub enum Audience<A: Address + 'static, R: Role + 'static = default::Role> {
    // All non-broker messengers outside of the sender.
    Broadcast,
    // An Audience Group.
    Group(group::Group<A, R>),
    // The messenger at the specified address.
    Address(A),
    // The messenger with the specified signature.
    Messenger(Signature<A>),
    // A messenger who belongs to the specified role.
    Role(role::Signature<R>),
}

impl<A: Address + 'static, R: Role + 'static> Audience<A, R> {
    /// Indicates whether a message directed towards this `Audience` must match
    /// to a messenger or if it's okay for the message to be delivered to no
    /// one. For example, broadcasts are meant to be delivered to any
    /// (potentially no) messenger.
    pub fn requires_delivery(&self) -> bool {
        match self {
            Audience::Broadcast => false,
            Audience::Role(_) => false,
            Audience::Group(group) => {
                group.audiences.iter().any(|audience| audience.requires_delivery())
            }
            Audience::Address(_) | Audience::Messenger(_) => true,
        }
    }

    pub fn contains(&self, audience: &Audience<A, R>) -> bool {
        audience.flatten().is_subset(&self.flatten())
    }

    fn flatten(&self) -> HashSet<Audience<A, R>> {
        match self {
            Audience::Group(group) => {
                group.audiences.iter().map(|audience| audience.flatten()).flatten().collect()
            }
            _ => [self.clone()].iter().cloned().collect(),
        }
    }
}

pub mod group {
    use super::{Address, Audience, Role};
    #[derive(Clone, Debug, PartialEq, Hash, Eq)]
    pub struct Group<A: Address + 'static, R: Role + 'static> {
        pub audiences: Vec<Audience<A, R>>,
    }

    impl<A: Address + 'static, R: Role + 'static> Group<A, R> {
        pub fn contains(&self, audience: &Audience<A, R>) -> bool {
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

    pub struct Builder<A: Address + 'static, R: Role + 'static> {
        audiences: Vec<Audience<A, R>>,
    }

    impl<A: Address + 'static, R: Role + 'static> Builder<A, R> {
        pub fn new() -> Self {
            Self { audiences: vec![] }
        }

        pub fn add(mut self, audience: Audience<A, R>) -> Self {
            self.audiences.push(audience);
            self
        }

        pub fn build(self) -> Group<A, R> {
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
pub enum MessengerType<
    P: Payload + 'static,
    A: Address + 'static,
    R: Role + 'static = default::Role,
> {
    /// An endpoint in the messenger graph. Can have messages specifically
    /// addressed to it and can author new messages.
    Addressable(A),
    /// A intermediary messenger. Will receive every forwarded message. Brokers
    /// are able to send and reply to messages, but the main purpose is to observe
    /// messages. An optional filter may be specified, which limits the messages
    /// directed to this broker.
    Broker(Option<filter::Filter<P, A, R>>),
    /// A messenger that cannot be reached by an address.
    Unbound,
}

pub mod filter {
    use super::{Address, Audience, Message, MessageType, Payload, Role};
    use core::fmt::{Debug, Formatter};
    use std::sync::Arc;

    /// `Condition` allows specifying a filter condition that must be true
    /// for a filter to match.
    #[derive(Clone)]
    pub enum Condition<P: Payload + 'static, A: Address + 'static, R: Role + 'static> {
        /// Matches on the message's intended audience as specified by the
        /// sender.
        Audience(Audience<A, R>),
        /// Matches on a custom closure that may evaluate the sent message.
        Custom(Arc<dyn Fn(&Message<P, A, R>) -> bool + Send + Sync>),
        /// Matches on another filter and its conditions.
        Filter(Filter<P, A, R>),
    }

    /// We must implement Debug since the `Condition::Custom` does not provide
    /// a `Debug` implementation.
    impl<P: Payload + 'static, A: Address + 'static, R: Role + 'static> Debug for Condition<P, A, R> {
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
    pub struct Builder<P: Payload + 'static, A: Address + 'static, R: Role + 'static> {
        conjugation: Conjugation,
        conditions: Vec<Condition<P, A, R>>,
    }

    impl<P: Payload + 'static, A: Address + 'static, R: Role + 'static> Builder<P, A, R> {
        pub fn new(condition: Condition<P, A, R>, conjugation: Conjugation) -> Self {
            Self { conjugation, conditions: vec![condition] }
        }

        /// Shorthand method to create a filter based on a single condition.
        pub fn single(condition: Condition<P, A, R>) -> Filter<P, A, R> {
            Builder::new(condition, Conjugation::All).build()
        }

        /// Adds an additional condition to the filter under construction.
        pub fn append(mut self, condition: Condition<P, A, R>) -> Self {
            self.conditions.push(condition);

            self
        }

        pub fn build(self) -> Filter<P, A, R> {
            Filter { conjugation: self.conjugation, conditions: self.conditions }
        }
    }

    /// `Filter` is used by the `MessageHub` to determine whether an incoming
    /// message should be directed to associated broker.
    #[derive(Clone, Debug)]
    pub struct Filter<P: Payload + 'static, A: Address + 'static, R: Role + 'static> {
        conjugation: Conjugation,
        conditions: Vec<Condition<P, A, R>>,
    }

    impl<P: Payload + 'static, A: Address + 'static, R: Role + 'static> Filter<P, A, R> {
        pub fn matches(&self, message: &Message<P, A, R>) -> bool {
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
pub enum MessageType<P: Payload + 'static, A: Address + 'static, R: Role + 'static> {
    /// A completely new message that is intended for the specified audience.
    Origin(Audience<A, R>),
    /// A response to a previously received message. Note that the value must
    /// be boxed to mitigate recursive sizing issues as MessageType is held by
    /// Message.
    Reply(Box<Message<P, A, R>>),
}

/// `Attribution` describes the relationship of the message path in relation
/// to the author.
#[derive(Clone, Debug)]
pub enum Attribution<P: Payload + 'static, A: Address + 'static, R: Role + 'static> {
    /// `Source` attributed messages are the original messages to be sent on a
    /// path. For example, a source attribution for an origin message type will
    /// be authored by the original sender. In a reply message type, a source
    /// attribution means the reply was authored by the original message's
    /// intended target.
    Source(MessageType<P, A, R>),
    /// `Derived` attributed messages are messages that have been modified by
    /// someone in the message path. They follow the same trajectory (audience
    /// or return path), but their message has been altered.
    Derived(Box<Message<P, A, R>>),
}

/// The core messaging unit. A Message may be annotated by messengers, but is
/// not associated with a particular Messenger instance.
#[derive(Clone, Debug)]
pub struct Message<P: Payload + 'static, A: Address + 'static, R: Role + 'static> {
    author: Fingerprint<A>,
    timestamp: Timestamp,
    payload: P,
    attribution: Attribution<P, A, R>,
    // The return path is generated while the message is passed from messenger
    // to messenger on the way to the intended recipient. It indicates the
    // messengers that would like to be informed of replies to this message.
    // The message author is always the last element in this vector. New
    // participants are pushed to the front.
    return_path: Vec<Beacon<P, A, R>>,
}

impl<P: Payload + 'static, A: Address + 'static, R: Role + 'static> Message<P, A, R> {
    /// Returns a new Message instance. Only the MessageHub can mint new messages.
    pub(super) fn new(
        author: Fingerprint<A>,
        timestamp: Timestamp,
        payload: P,
        attribution: Attribution<P, A, R>,
    ) -> Message<P, A, R> {
        let mut return_path = vec![];

        // A derived message adopts the return path of the original message.
        if let Attribution::Derived(message) = &attribution {
            return_path.append(&mut message.get_return_path());
        }

        Message { author, timestamp, payload, attribution, return_path }
    }

    /// Adds an entity to be notified on any replies.
    pub(super) fn add_participant(&mut self, participant: Beacon<P, A, R>) {
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
    pub(super) fn get_return_path(&self) -> Vec<Beacon<P, A, R>> {
        return self.return_path.clone();
    }

    /// Returns the message's type.
    pub(super) fn get_message_type(&self) -> MessageType<P, A, R> {
        match &self.attribution {
            Attribution::Source(message_type) => message_type.clone(),
            Attribution::Derived(message) => message.get_message_type(),
        }
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
pub(super) type ActionSender<P, A, R> =
    UnboundedSender<(Fingerprint<A>, MessageAction<P, A, R>, Option<Beacon<P, A, R>>)>;

/// An internal identifier used by the MessageHub to identify messengers.
pub(super) type MessengerId = usize;

/// An internal identifier used by the `MessageHub` to identify `MessageClient`.
pub(super) type MessageClientId = usize;

pub(super) type CreateMessengerResult<P, A, R> =
    Result<(MessengerClient<P, A, R>, Receptor<P, A, R>), MessageError<A>>;

/// Callback for handing back a messenger
pub(super) type MessengerSender<P, A, R> = Sender<CreateMessengerResult<P, A, R>>;

/// Type definition for a sender handed by the MessageHub to spawned components
/// (messenger factories and messengers) to control messengers.
pub(super) type MessengerActionSender<P, A, R> = UnboundedSender<MessengerAction<P, A, R>>;

/// Internal representation of possible actions around a messenger.
pub(super) enum MessengerAction<P: Payload + 'static, A: Address + 'static, R: Role + 'static> {
    /// Creates a top level messenger
    Create(
        messenger::Descriptor<P, A, R>,
        MessengerSender<P, A, R>,
        MessengerActionSender<P, A, R>,
    ),
    /// Deletes a given messenger
    Delete(Messenger<P, A, R>),
    /// Deletes a messenger by its `Signature`
    DeleteBySignature(Signature<A>),
}

/// Internal representation for possible actions on a message.
#[derive(Debug)]
pub(super) enum MessageAction<P: Payload + 'static, A: Address + 'static, R: Role + 'static> {
    // A new message sent to the specified audience.
    Send(P, Attribution<P, A, R>, Timestamp),
    // The message has been forwarded by the current holder.
    Forward(Message<P, A, R>),
}
