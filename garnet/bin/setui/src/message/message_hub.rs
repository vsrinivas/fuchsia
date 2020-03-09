// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::base::{
    ActionSender, Address, Audience, DeliveryStatus, Message, MessageAction, MessageType,
    MessengerId, MessengerType, Payload,
};
use crate::message::beacon::Beacon;
use crate::message::messenger::Messenger;
use crate::message::receptor::Receptor;
use fuchsia_async as fasync;
use futures::lock::Mutex;
use futures::StreamExt;
use std::collections::HashMap;
use std::sync::Arc;

/// Type definition for a handle to the MessageHub. There is a single instance
/// of a hub per communication ecosystem and therefore held behind an Arc mutex.
pub type MessageHubHandle<P, A> = Arc<Mutex<MessageHub<P, A>>>;

/// The MessageHub controls the message flow for a set of messengers. It
/// processes actions upon messages, incorporates brokers, and signals receipt
/// of messages.
pub struct MessageHub<P: Payload + 'static, A: Address + 'static> {
    /// A sender given to messengers to signal actions upon the MessageHub.
    action_tx: ActionSender<P, A>,
    /// Address mapping for looking up messengers. Used for sending messages
    /// to an addressable recipient.
    addresses: HashMap<A, MessengerId>,
    /// Mapping of registered messengers (including brokers) to beacons. Used for
    /// delivering messages from a resolved address or a list of participants.
    beacons: HashMap<MessengerId, Beacon<P, A>>,
    /// An ordered set of messengers who will be forwarded messages.
    brokers: Vec<MessengerId>,
    // The next id to be given to a messenger.
    next_id: MessengerId,
}

impl<P: Payload + 'static, A: Address + 'static> MessageHub<P, A> {
    /// Returns a new MessageHub for the given types.
    pub fn create() -> MessageHubHandle<P, A> {
        let (action_tx, mut action_rx) = futures::channel::mpsc::unbounded::<(
            MessengerId,
            MessageAction<P, A>,
            Option<Beacon<P, A>>,
        )>();
        let hub = Arc::new(Mutex::new(MessageHub {
            next_id: 0,
            action_tx: action_tx,
            beacons: HashMap::new(),
            addresses: HashMap::new(),
            brokers: Vec::new(),
        }));

        let hub_clone = hub.clone();

        // Spawn a separate thread to service any request to this instance.
        fasync::spawn(async move {
            while let Some((id, action, beacon)) = action_rx.next().await {
                hub_clone.lock().await.process_request(id, action, beacon).await;
            }
        });

        hub
    }

    // Determines whether the beacon belongs to a broker.
    async fn is_broker(&self, beacon: Beacon<P, A>) -> bool {
        self.brokers.contains(&beacon.get_messenger_id())
    }

    /// Internally routes a message to the next appropriate receiver. New messages
    /// are routed based on the intended recipient(s), while replies follow the
    /// return path of the source message. The provided sender id represents the
    /// id of the current messenger possessing the message and not necessarily
    /// the original author.
    async fn send_to_next(&self, sender_id: MessengerId, mut message: Message<P, A>) {
        let mut recipients = vec![];

        let message_type = message.get_message_type();

        // Replies have a predetermined return path.
        if let MessageType::Reply(mut source) = message_type {
            // The original author of the reply will be the first participant in
            // the reply's return path. Otherwise, identify current sender in
            // the source return path and forward to next participant.
            let source_return_path = source.get_return_path();
            let mut target_index = None;
            let last_index = source_return_path.len() - 1;

            if sender_id == message.get_return_path()[0].get_messenger_id() {
                // If this is the reply's author, send to the first messenger in
                // the original message's return path.
                target_index = Some(0);

                // Mark source message as delivered. In the case the sender is the
                // original intended audience, this will be a no-op. However, if the
                // reply comes beforehand, this will ensure the message is properly
                // acknowledged.
                source.report_status(DeliveryStatus::Received).await;
            } else {
                for index in 0..last_index {
                    if source_return_path[index].get_messenger_id() == sender_id {
                        target_index = Some(index + 1);
                    }
                }
            }

            if let Some(index) = target_index {
                recipients.push(source_return_path[index].clone());

                if index == last_index {
                    // Ack current message if being sent to intended recipient.
                    message.report_status(DeliveryStatus::Received).await;
                }
            }
        } else if let Some(beacon) = self.beacons.get(&sender_id) {
            let mut target_messengers = vec![];
            // If the message is not a reply, determine if the current sender is a broker.
            // In the case of a broker, the message should be forwarded to the next
            // broker.
            if self.is_broker(beacon.clone()).await {
                if let Some(index) = self.brokers.iter().position(|&id| id == sender_id) {
                    if index < self.brokers.len() - 1 {
                        // Add the next broker
                        target_messengers.push(self.brokers[index + 1].clone());
                    }
                }
            } else if let Some(broker) = self.brokers.first() {
                target_messengers.push(broker.clone());
            }

            // If no brokers were added, the original target now should participate.
            if target_messengers.is_empty() {
                if let MessageType::Origin(audience) = message_type {
                    match audience {
                        Audience::Address(address) => {
                            if let Some(messenger_id) = self.addresses.get(&address) {
                                target_messengers.push(messenger_id.clone());

                                // Will be delivering to recipient. Acknowledge
                                message.report_status(DeliveryStatus::Received).await;
                            } else {
                                // This error will occur if the sender specifies a non-existent
                                // address.
                                message.report_status(DeliveryStatus::Undeliverable).await;
                            }
                        }
                        Audience::Broadcast => {
                            // Broadcasts don't require any audience.
                            message.report_status(DeliveryStatus::Broadcasted).await;

                            // Gather all messengers
                            for id in self.beacons.keys() {
                                if *id != sender_id && !self.brokers.contains(id) {
                                    target_messengers.push(id.clone());
                                }
                            }
                        }
                    }
                }
            }

            // Translate selected messengers into beacon
            for messenger in target_messengers {
                if let Some(beacon) = self.beacons.get(&messenger) {
                    recipients.push(beacon.clone());
                }
            }
        }

        // Send message to each specified recipient.
        for recipient in recipients {
            recipient.deliver(message.clone()).await.ok();
        }
    }

    // Translates messenger requests into actions upon the MessageHub.
    async fn process_request(
        &self,
        messenger_id: MessengerId,
        action: MessageAction<P, A>,
        beacon: Option<Beacon<P, A>>,
    ) {
        let message;

        match action {
            MessageAction::Send(payload, message_type) => {
                message = Some(Message::new(payload, message_type));
            }
            MessageAction::Forward(mut forwarded_message) => {
                message = Some(forwarded_message.clone());
                if let Some(beacon) = self.beacons.get(&messenger_id) {
                    match forwarded_message.clone().get_message_type() {
                        MessageType::Origin(_) => {
                            // Ignore forward requests from leafs in broadcast.
                            if !self.is_broker(beacon.clone()).await {
                                return;
                            }
                        }
                        MessageType::Reply(source) => {
                            if let Some(recipient) = source.get_return_path().last() {
                                // If the reply recipient drops the message, do not forward.
                                if recipient.get_messenger_id() == messenger_id {
                                    return;
                                }
                            } else {
                                // Every reply should have a return path.
                                forwarded_message
                                    .report_status(DeliveryStatus::Undeliverable)
                                    .await;
                                return;
                            }
                        }
                    }
                } else {
                    forwarded_message.report_status(DeliveryStatus::Undeliverable).await;
                    return;
                }
            }
        }

        if let Some(mut outgoing_message) = message {
            if let Some(handle) = beacon {
                outgoing_message.add_participant(handle);
            }

            self.send_to_next(messenger_id, outgoing_message).await;
        }
    }

    /// Returns a new messenger that can be used by a client to author new
    /// messages. Messengers are limited to participation within the ecosystem of
    /// the MessageHub that created them.
    pub fn create_messenger(
        &mut self,
        messenger_type: MessengerType<A>,
    ) -> (Messenger<P, A>, Receptor<P, A>) {
        let id = self.next_id;
        let messenger = Messenger::create(id, self.action_tx.clone());
        self.next_id += 1;

        let (beacon, receptor) = Beacon::create(messenger.clone());
        self.beacons.insert(id, beacon.clone());

        match messenger_type {
            MessengerType::Broker => {
                self.brokers.push(id);
            }
            MessengerType::Addressable(address) => {
                self.addresses.insert(address, id);
            }
        }

        (messenger, receptor)
    }
}
