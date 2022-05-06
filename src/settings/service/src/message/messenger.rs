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

use fuchsia_syslog::fx_log_warn;
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
    pub(super) fn new(
        messenger_action_tx: MessengerActionSender<P, A, R>,
        messenger_type: MessengerType<P, A, R>,
    ) -> Self {
        Self { messenger_action_tx, messenger_type, roles: HashSet::new() }
    }

    /// Includes the specified role in the list of roles to be associated with
    /// the new messenger.
    pub(crate) fn add_role(mut self, role: role::Signature<R>) -> Self {
        let _ = self.roles.insert(role);
        self
    }

    /// Constructs a messenger based on specifications supplied.
    pub(crate) async fn build(self) -> CreateMessengerResult<P, A, R> {
        let (tx, rx) = futures::channel::oneshot::channel::<CreateMessengerResult<P, A, R>>();

        // Panic if send failed since a messenger cannot be created.
        self.messenger_action_tx
            .unbounded_send(MessengerAction::Create(
                messenger::Descriptor { messenger_type: self.messenger_type, roles: self.roles },
                tx,
                self.messenger_action_tx.clone(),
            ))
            .expect("Builder::build, messenger_action_tx failed to send message");

        rx.await.map_err(|_| MessageError::Unexpected).and_then(identity)
    }
}

/// TargetedMessengerClient is a wrapper around [`MessengerClient`] that limits
/// the audience of sent messages to a preset target.
#[derive(Clone, Debug)]
pub struct TargetedMessengerClient<P: Payload + 'static, A: Address + 'static, R: Role + 'static> {
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    client: MessengerClient<P, A, R>,
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    audience: Audience<A, R>,
}

impl<P: Payload + 'static, A: Address + 'static, R: Role + 'static>
    TargetedMessengerClient<P, A, R>
{
    #[cfg(test)]
    pub(crate) fn new(client: MessengerClient<P, A, R>, audience: Audience<A, R>) -> Self {
        Self { client, audience }
    }

    /// Creates a MessageBuilder for a new message with the specified payload.
    #[cfg(test)]
    pub(crate) fn message(&self, payload: P) -> MessageBuilder<P, A, R> {
        self.client.message(payload, self.audience.clone())
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
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
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
    pub(crate) fn message(&self, payload: P, audience: Audience<A, R>) -> MessageBuilder<P, A, R> {
        MessageBuilder::new(payload, MessageType::Origin(audience), self.messenger.clone())
    }

    /// Returns the signature of the client that will handle any sent messages.
    pub fn get_signature(&self) -> Signature<A> {
        self.messenger.get_signature()
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
        // Do not transmit if the message hub has exited.
        if self.action_tx.is_closed() {
            return;
        }

        // Log info. transmit is called by forward. However, forward might fail if there is no next
        // Messenger exists.
        self.action_tx.unbounded_send((self.fingerprint, action, beacon)).unwrap_or_else(|_| {
            fx_log_warn!("Messenger::transmit, action_tx failed to send message")
        });
    }

    pub(super) fn get_signature(&self) -> Signature<A> {
        self.fingerprint.signature
    }
}
