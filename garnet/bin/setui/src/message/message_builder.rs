// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::action_fuse::{ActionFuse, ActionFuseHandle};
use crate::message::base::{Address, MessageAction, MessageType, Payload};
use crate::message::beacon::Beacon;
use crate::message::messenger::Messenger;
use crate::message::receptor::Receptor;

/// MessageBuilder allows constructing a message or reply with optional signals.
pub struct MessageBuilder<P: Payload + 'static, A: Address + 'static> {
    payload: P,
    message_type: MessageType<P, A>,
    messenger: Messenger<P, A>,
    forwarder: Option<ActionFuseHandle>,
}

impl<P: Payload + 'static, A: Address + 'static> MessageBuilder<P, A> {
    /// Returns a new MessageBuilder. Note that this is private as clients should
    /// retrieve builders through either a Messenger or MessageClient.
    pub(super) fn new(
        payload: P,
        message_type: MessageType<P, A>,
        messenger: Messenger<P, A>,
    ) -> MessageBuilder<P, A> {
        MessageBuilder {
            payload: payload,
            message_type: message_type,
            messenger: messenger,
            forwarder: None,
        }
    }

    /// Sets an AutoForwarder to be disabled when the message is sent. This is
    /// private as only a MessageClient should be able to set this.
    pub(super) fn auto_forwarder(
        mut self,
        forwarder_handle: ActionFuseHandle,
    ) -> MessageBuilder<P, A> {
        self.forwarder = Some(forwarder_handle);
        self
    }

    /// Consumes the MessageBuilder and sends the message to the MessageHub.
    pub fn send(self) -> Receptor<P, A> {
        let (beacon, receptor) = Beacon::create(self.messenger.clone(), None);
        self.messenger.transmit(MessageAction::Send(self.payload, self.message_type), Some(beacon));

        if let Some(forwarder) = self.forwarder {
            ActionFuse::defuse(forwarder.clone());
        }

        receptor
    }
}
