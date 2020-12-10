// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::action_fuse::ActionFuseHandle;
use crate::message::base::{
    default, messenger, role, ActionSender, Address, Audience, CreateMessengerResult, Fingerprint,
    Message, MessageAction, MessageError, MessageType, MessengerAction, MessengerActionSender,
    MessengerId, MessengerType, Payload, Role, Signature,
};
use crate::message::beacon::Beacon;
use crate::message::message_builder::MessageBuilder;

use std::collections::HashSet;
use std::convert::identity;

/// `Builder` is the default way for creating a new messenger. Beyond the base
/// messenger type, this helper allows for roles to be associated as well
/// during construction.
pub struct Builder<P: Payload + 'static, A: Address + 'static, R: Role + 'static> {
    /// The sender for sending messenger creation requests to the MessageHub.
    messenger_action_tx: MessengerActionSender<P, A, R>,
    /// The type of messenger to be created. Along with roles, the messenger
    /// type determines what audiences the messenger is included in.
    messenger_type: MessengerType<P, A, R>,
    /// The roles to associate with this messenger.
    roles: HashSet<role::Signature<R>>,
}

impl<P: Payload + 'static, A: Address + 'static, R: Role + 'static> Builder<P, A, R> {
    /// Creates a new builder for constructing a messenger of the given
    /// type.
    fn new(
        messenger_action_tx: MessengerActionSender<P, A, R>,
        messenger_type: MessengerType<P, A, R>,
    ) -> Self {
        Self { messenger_action_tx, messenger_type, roles: HashSet::new() }
    }

    /// Includes the specified role in the list of roles to be associated with
    /// the new messenger.
    pub fn add_role(mut self, role: role::Signature<R>) -> Self {
        self.roles.insert(role);
        self
    }

    /// Constructs a messenger based on specifications supplied.
    pub async fn build(self) -> CreateMessengerResult<P, A, R> {
        let (tx, rx) = futures::channel::oneshot::channel::<CreateMessengerResult<P, A, R>>();

        self.messenger_action_tx
            .unbounded_send(MessengerAction::Create(
                messenger::Descriptor { messenger_type: self.messenger_type, roles: self.roles },
                tx,
                self.messenger_action_tx.clone(),
            ))
            .ok();

        rx.await.map_err(|_| MessageError::Unexpected).and_then(identity)
    }
}

/// MessengerFactory is the artifact of creating a MessageHub. It can be used
/// to create new messengers.
#[derive(Clone)]
pub struct MessengerFactory<P: Payload + 'static, A: Address + 'static, R: Role + 'static> {
    role_action_tx: role::ActionSender<R>,
    messenger_action_tx: MessengerActionSender<P, A, R>,
}

impl<P: Payload + 'static, A: Address + 'static, R: Role + 'static> MessengerFactory<P, A, R> {
    pub(super) fn new(
        action_tx: MessengerActionSender<P, A, R>,
        role_action_tx: role::ActionSender<R>,
    ) -> MessengerFactory<P, A, R> {
        MessengerFactory { messenger_action_tx: action_tx, role_action_tx }
    }

    /// This method is soft-deprecated for now.
    // #[deprecated(note = "Please use messenger_builder instead")]
    pub async fn create_role(&self) -> Result<role::Signature<R>, role::Error> {
        let (tx, rx) =
            futures::channel::oneshot::channel::<Result<role::Response<R>, role::Error>>();

        self.role_action_tx.unbounded_send(role::Action::Create(tx)).ok();

        rx.await.unwrap_or(Err(role::Error::CommunicationError)).and_then(|result| match result {
            role::Response::Role(signature) => Ok(signature),
        })
    }

    /// Returns a builder for constructing a new messenger.
    pub fn messenger_builder(&self, messenger_type: MessengerType<P, A, R>) -> Builder<P, A, R> {
        Builder::new(self.messenger_action_tx.clone(), messenger_type)
    }

    pub async fn create(
        &self,
        messenger_type: MessengerType<P, A, R>,
    ) -> CreateMessengerResult<P, A, R> {
        self.messenger_builder(messenger_type).build().await
    }

    pub fn delete(&self, signature: Signature<A>) {
        self.messenger_action_tx.unbounded_send(MessengerAction::DeleteBySignature(signature)).ok();
    }
}

/// MessengerClient is a wrapper around a messenger with a fuse.
#[derive(Clone, Debug)]
pub struct MessengerClient<
    P: Payload + 'static,
    A: Address + 'static,
    R: Role + 'static = default::Role,
> {
    messenger: Messenger<P, A, R>,
    fuse: ActionFuseHandle,
}

impl<P: Payload + 'static, A: Address + 'static, R: Role + 'static> MessengerClient<P, A, R> {
    pub(super) fn new(
        messenger: Messenger<P, A, R>,
        fuse: ActionFuseHandle,
    ) -> MessengerClient<P, A, R> {
        MessengerClient { messenger, fuse }
    }

    /// Creates a MessageBuilder for a new message with the specified payload
    /// and audience.
    pub fn message(&self, payload: P, audience: Audience<A, R>) -> MessageBuilder<P, A, R> {
        MessageBuilder::new(payload, MessageType::Origin(audience), self.messenger.clone())
    }
}

/// Messengers provide clients the ability to send messages to other registered
/// clients. They can only be created through a MessageHub.
#[derive(Clone, Debug)]
pub struct Messenger<P: Payload + 'static, A: Address + 'static, R: Role + 'static> {
    fingerprint: Fingerprint<A>,
    action_tx: ActionSender<P, A, R>,
}

impl<P: Payload + 'static, A: Address + 'static, R: Role + 'static> Messenger<P, A, R> {
    pub(super) fn new(
        fingerprint: Fingerprint<A>,
        action_tx: ActionSender<P, A, R>,
    ) -> Messenger<P, A, R> {
        Messenger { fingerprint, action_tx }
    }

    /// Returns the identification for this Messenger.
    pub(super) fn get_id(&self) -> MessengerId {
        self.fingerprint.id
    }

    /// Forwards the message to the next Messenger. Note that this method is
    /// private and only called through the MessageClient.
    pub(super) fn forward(&self, message: Message<P, A, R>, beacon: Option<Beacon<P, A, R>>) {
        self.transmit(MessageAction::Forward(message), beacon);
    }

    /// Tranmits a given action to the message hub. This is a common utility
    /// method to be used for immediate actions (forwarding, observing) and
    /// deferred actions as well (sending, replying).
    pub(super) fn transmit(&self, action: MessageAction<P, A, R>, beacon: Option<Beacon<P, A, R>>) {
        self.action_tx.unbounded_send((self.fingerprint.clone(), action, beacon)).ok();
    }

    pub(super) fn get_signature(&self) -> Signature<A> {
        self.fingerprint.signature.clone()
    }
}
