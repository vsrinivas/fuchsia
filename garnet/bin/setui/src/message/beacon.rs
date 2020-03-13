// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::action_fuse::ActionFuseHandle;
use crate::message::base::{Address, DeliveryStatus, Message, MessageEvent, MessengerId, Payload};
use crate::message::message_client::MessageClient;
use crate::message::messenger::Messenger;
use crate::message::receptor::Receptor;
use anyhow::{format_err, Error};
use futures::channel::mpsc::UnboundedSender;

/// A Beacon is the conduit for sending messages to a particular Receptor. An
/// instance may be cloned and passed around to other components. All copies of
/// a particular Beacon share a reference to an flag that signals whether the
/// Receptor is active, which controls whether future messages will be sent.
///
/// It is important to note that Beacons spawn from sending a Message. Status
/// and other context sent through the Beacon are in relation to this original
/// Message (either an origin or reply).
#[derive(Clone)]
pub struct Beacon<P: Payload + 'static, A: Address + 'static> {
    /// A reference to the associated Messenger. This is only used when delivering
    /// a new message to a beacon, where a MessageClient (which references both
    /// the recipient's Messenger and the message) must be created.
    messenger: Messenger<P, A>,
    /// The sender half of an internal channel established between the Beacon and
    /// Receptor.
    event_sender: UnboundedSender<MessageEvent<P, A>>,
}

impl<P: Payload + 'static, A: Address + 'static> Beacon<P, A> {
    /// Creates a Beacon, Receptor tuple. The Messenger provided as an argument
    /// will be associated with any delivered Message for reply purposes.
    pub fn create(
        messenger: Messenger<P, A>,
        fuse: Option<ActionFuseHandle>,
    ) -> (Beacon<P, A>, Receptor<P, A>) {
        let (event_tx, event_rx) = futures::channel::mpsc::unbounded::<MessageEvent<P, A>>();
        let beacon = Beacon { messenger: messenger, event_sender: event_tx };

        // pass fuse to receptor to hold and set when it goes out of scope.
        let receptor = Receptor::new(event_rx, fuse);

        (beacon, receptor)
    }

    /// Sends the DeliveryStatus associated with the original message that spawned
    /// this beacon.
    pub async fn status(&self, status: DeliveryStatus) -> Result<(), Error> {
        if self.event_sender.unbounded_send(MessageEvent::Status(status)).is_err() {
            return Err(format_err!("failed to deliver status"));
        }

        Ok(())
    }

    /// Delivers a response to the original message that spawned this Beacon.
    pub async fn deliver(&self, message: Message<P, A>) -> Result<(), Error> {
        if self
            .event_sender
            .unbounded_send(MessageEvent::Message(
                message.payload(),
                MessageClient::new(message, self.messenger.clone()),
            ))
            .is_err()
        {
            return Err(format_err!("failed to deliver message"));
        }

        Ok(())
    }

    /// Returns the identifier for the associated Messenger.
    pub(super) fn get_messenger_id(&self) -> MessengerId {
        self.messenger.get_id()
    }
}
