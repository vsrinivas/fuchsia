// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::internal::common::now;
use crate::message::action_fuse::{ActionFuse, ActionFuseHandle};
use crate::message::base::{
    Address, Attribution, Message, MessageAction, MessageType, Payload, Role,
};
use crate::message::beacon::BeaconBuilder;
use crate::message::messenger::Messenger;
use crate::message::receptor::Receptor;
use fuchsia_zircon::Duration;

/// MessageBuilder allows constructing a message or reply with optional signals.
#[derive(Clone)]
pub struct MessageBuilder<P: Payload + 'static, A: Address + 'static, R: Role + 'static> {
    payload: P,
    attribution: Attribution<P, A, R>,
    messenger: Messenger<P, A, R>,
    forwarder: Option<ActionFuseHandle>,
    timeout: Option<Duration>,
}

impl<P: Payload + 'static, A: Address + 'static, R: Role + 'static> MessageBuilder<P, A, R> {
    /// Returns a new MessageBuilder. Note that this is private as clients should
    /// retrieve builders through either a Messenger or MessageClient.
    pub(super) fn new(
        payload: P,
        message_type: MessageType<P, A, R>,
        messenger: Messenger<P, A, R>,
    ) -> MessageBuilder<P, A, R> {
        MessageBuilder {
            payload,
            attribution: Attribution::Source(message_type),
            messenger,
            forwarder: None,
            timeout: None,
        }
    }

    /// Creates a new message derived from the source message with the specified
    /// payload. Derived messages will follow the same path as the source
    /// message.
    pub(super) fn derive(
        payload: P,
        source: Message<P, A, R>,
        messenger: Messenger<P, A, R>,
    ) -> MessageBuilder<P, A, R> {
        MessageBuilder {
            payload,
            attribution: Attribution::Derived(Box::new(source), messenger.get_signature()),
            messenger,
            forwarder: None,
            timeout: None,
        }
    }

    /// Sets an AutoForwarder to be disabled when the message is sent. This is
    /// private as only a MessageClient should be able to set this.
    pub(super) fn auto_forwarder(
        mut self,
        forwarder_handle: ActionFuseHandle,
    ) -> MessageBuilder<P, A, R> {
        self.forwarder = Some(forwarder_handle);
        self
    }

    pub fn set_timeout(mut self, duration: Option<Duration>) -> MessageBuilder<P, A, R> {
        self.timeout = duration;
        self
    }

    /// Consumes the MessageBuilder and sends the message to the MessageHub.
    pub fn send(self) -> Receptor<P, A, R> {
        let (beacon, receptor) =
            BeaconBuilder::new(self.messenger.clone()).set_timeout(self.timeout).build();
        self.messenger
            .transmit(MessageAction::Send(self.payload, self.attribution, now()), Some(beacon));

        if let Some(forwarder) = self.forwarder {
            ActionFuse::defuse(forwarder.clone());
        }

        receptor
    }
}
