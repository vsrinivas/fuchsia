// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::action_fuse::ActionFuseBuilder;
use crate::message::action_fuse::ActionFuseHandle;
use crate::message::base::{
    filter::Filter, ActionSender, Address, Audience, Fingerprint, Message, MessageAction,
    MessageClientId, MessageError, MessageType, MessengerAction, MessengerId, MessengerType,
    Payload, Signature, Status,
};
use crate::message::beacon::{Beacon, BeaconBuilder};
use crate::message::messenger::{Messenger, MessengerClient, MessengerFactory};
use fuchsia_async as fasync;
use futures::lock::Mutex;
use futures::StreamExt;
use std::collections::HashMap;
use std::sync::Arc;

/// Type definition for a handle to the MessageHub. There is a single instance
/// of a hub per communication ecosystem and therefore held behind an Arc mutex.
pub type MessageHubHandle<P, A> = Arc<Mutex<MessageHub<P, A>>>;

/// Type definition for exit message sender.
type ExitSender = futures::channel::mpsc::UnboundedSender<()>;

/// `Broker` captures the information necessary to process messages to a broker:
/// messenger_id: The `MessengerId` associated with the broker so that it can be
///               distinguished from other messengers.
/// filter:       A condition that is applied to a message to determine whether
///               it should be directed to the broker.
#[derive(Clone)]
struct Broker<P: Payload + 'static, A: Address + 'static> {
    messenger_id: MessengerId,
    filter: Option<Filter<P, A>>,
}

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
    brokers: Vec<Broker<P, A>>,
    /// The next id to be given to a messenger.
    next_id: MessengerId,
    /// The next id to be given to a `MessageClient`.
    next_message_client_id: MessageClientId,
    /// Indicates whether the messenger channel has closed.
    messenger_channel_closed: bool,
    /// Sender to signal when the hub should exit.
    exit_tx: ExitSender,
}

impl<P: Payload + 'static, A: Address + 'static> MessageHub<P, A> {
    /// Returns a new MessageHub for the given types.
    pub fn create(fuse: Option<ActionFuseHandle>) -> MessengerFactory<P, A> {
        let (action_tx, mut action_rx) = futures::channel::mpsc::unbounded::<(
            Fingerprint<A>,
            MessageAction<P, A>,
            Option<Beacon<P, A>>,
        )>();
        let (messenger_tx, mut messenger_rx) =
            futures::channel::mpsc::unbounded::<MessengerAction<P, A>>();

        let (exit_tx, mut exit_rx) = futures::channel::mpsc::unbounded::<()>();

        let mut hub = MessageHub {
            next_id: 0,
            next_message_client_id: 0,
            action_tx,
            beacons: HashMap::new(),
            addresses: HashMap::new(),
            brokers: Vec::new(),
            messenger_channel_closed: false,
            exit_tx,
        };

        fasync::Task::spawn(async move {
            // Released when exiting scope to signal completion.
            let _scope_fuse = fuse;

            loop {
                // We must prioritize the action futures. Exit actions
                // take absolute priority. Message actions are ordered before
                // messenger in case the messenger is subsequently deleted.
                futures::select_biased! {
                    exit_action = exit_rx.next() => {
                        break;
                    }
                    message_action = action_rx.next() => {
                        if let Some((fingerprint, action, beacon)) = message_action {
                            hub.process_request(fingerprint, action, beacon).await;
                        }
                    }
                    messenger_action = messenger_rx.next() => {
                        match messenger_action {
                            Some(action) => {
                                hub.process_messenger_request(action).await;
                            }
                            None => {
                                hub.messenger_channel_closed = true;
                                hub.check_exit();
                            }
                        }
                    }
                }
            }
        })
        .detach();

        MessengerFactory::new(messenger_tx)
    }

    fn check_exit(&self) {
        if self.messenger_channel_closed && self.beacons.is_empty() {
            self.exit_tx.unbounded_send(()).ok();
        }
    }

    // Determines whether the beacon belongs to a broker.
    fn is_broker(&self, messenger_id: MessengerId) -> bool {
        self.brokers.iter().any(|broker| broker.messenger_id == messenger_id)
    }

    // Derives the underlying MessengerId from a Signature.
    fn resolve_messenger_id(&self, signature: &Signature<A>) -> MessengerId {
        match signature {
            Signature::Anonymous(id) => *id,
            Signature::Address(address) => {
                *self.addresses.get(&address).expect("signature should be valid")
            }
        }
    }

    /// Internally routes a message to the next appropriate receiver. New messages
    /// are routed based on the intended recipient(s), while replies follow the
    /// return path of the source message. The provided sender id represents the
    /// id of the current messenger possessing the message and not necessarily
    /// the original author.
    async fn send_to_next(&mut self, sender_id: MessengerId, mut message: Message<P, A>) {
        let mut recipients = vec![];

        let message_type = message.get_message_type();

        let mut require_delivery = false;

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
                source.report_status(Status::Received).await;
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
                    message.report_status(Status::Received).await;
                }
            }
        } else if let Some(beacon) = self.beacons.get(&sender_id) {
            let author_id = self.resolve_messenger_id(&message.get_author());

            // If the message is not a reply, determine if the current sender is a broker.
            // In the case of a broker, the message should be forwarded to the next
            // broker.
            let mut target_messengers: Vec<_> = {
                // The author cannot participate as a broker.
                let iter = self
                    .brokers
                    .iter()
                    .filter(|&broker| {
                        broker.messenger_id != author_id
                            && broker
                                .filter
                                .as_ref()
                                .map_or(true, |filter| filter.matches(&message))
                    })
                    .map(|broker| broker.messenger_id);

                let should_find_sender = {
                    let beacon_messenger_id = beacon.get_messenger_id();
                    beacon_messenger_id != author_id && self.is_broker(beacon_messenger_id)
                };

                if should_find_sender {
                    // Ignore until we find the matching broker, then move the next broker one over.
                    iter.skip_while(|&id| id != sender_id).skip(1).take(1).collect()
                } else {
                    iter.take(1).collect()
                }
            };

            // If no broker was added, the original target now should participate.
            if target_messengers.is_empty() {
                if let MessageType::Origin(audience) = message_type {
                    match audience {
                        Audience::Address(address) => {
                            if let Some(&messenger_id) = self.addresses.get(&address) {
                                target_messengers.push(messenger_id);
                                require_delivery = true;
                            } else {
                                // This error will occur if the sender specifies a non-existent
                                // address.
                                message.report_status(Status::Undeliverable).await;
                            }
                        }
                        Audience::Messenger(signature) => {
                            match signature {
                                Signature::Address(address) => {
                                    if let Some(&messenger_id) = self.addresses.get(&address) {
                                        target_messengers.push(messenger_id);
                                        require_delivery = true;
                                    } else {
                                        // This error will occur if the sender specifies a non-existent
                                        // address.
                                        message.report_status(Status::Undeliverable).await;
                                    }
                                }
                                Signature::Anonymous(id) => {
                                    target_messengers.push(id);
                                }
                            }
                        }
                        Audience::Broadcast => {
                            // Broadcasts don't require any audience.
                            message.report_status(Status::Broadcasted).await;

                            // Gather all messengers
                            for &id in self.beacons.keys() {
                                if id != sender_id && !self.is_broker(id) {
                                    target_messengers.push(id);
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

        let mut successful_delivery = None;
        // Send message to each specified recipient.
        for recipient in recipients {
            if recipient.deliver(message.clone(), self.next_message_client_id).await.is_ok() {
                self.next_message_client_id += 1;
                if successful_delivery.is_none() {
                    successful_delivery = Some(true);
                }
            } else {
                successful_delivery = Some(false);
            }
        }

        if require_delivery {
            message
                .report_status(if let Some(true) = successful_delivery {
                    Status::Received
                } else {
                    Status::Undeliverable
                })
                .await;
        }
    }

    async fn process_messenger_request(&mut self, action: MessengerAction<P, A>) {
        match action {
            MessengerAction::Create(messenger_type, responder, messenger_tx) => {
                let mut optional_address = None;
                if let MessengerType::Addressable(address) = messenger_type.clone() {
                    optional_address = Some(address.clone());
                    if self.addresses.contains_key(&address) {
                        responder
                            .send(Err(MessageError::AddressConflict { address: address }))
                            .ok();
                        return;
                    }
                }

                let id = self.next_id;
                let signature = if let Some(address) = optional_address {
                    Signature::Address(address)
                } else {
                    Signature::Anonymous(id)
                };

                let messenger =
                    Messenger::new(Fingerprint { id, signature }, self.action_tx.clone());

                let messenger_clone = messenger.clone();

                // Create fuse to delete Messenger.
                let fuse = ActionFuseBuilder::new()
                    .add_action(Box::new(move || {
                        messenger_tx
                            .unbounded_send(MessengerAction::Delete(messenger_clone.clone()))
                            .ok();
                    }))
                    .build();

                self.next_id += 1;
                let (beacon, receptor) =
                    BeaconBuilder::new(messenger.clone()).add_fuse(fuse.clone()).build();
                self.beacons.insert(id, beacon);

                match messenger_type {
                    MessengerType::Broker(filter) => {
                        self.brokers.push(Broker { messenger_id: id, filter });
                    }
                    MessengerType::Addressable(address) => {
                        self.addresses.insert(address, id);
                    }
                    MessengerType::Unbound => {
                        // We do not track Unbounded messengers.
                    }
                }
                responder.send(Ok((MessengerClient::new(messenger, fuse.clone()), receptor))).ok();
            }
            MessengerAction::Delete(messenger) => {
                self.delete_by_signature(messenger.get_signature())
            }
            MessengerAction::DeleteBySignature(signature) => self.delete_by_signature(signature),
        }
    }

    fn delete_by_signature(&mut self, signature: Signature<A>) {
        let id = self.resolve_messenger_id(&signature);

        // These are all safe if the containers don't contain any items matching `id`.
        self.beacons.remove(&id);
        self.brokers.retain(|broker| id != broker.messenger_id);
        if let Signature::Address(address) = signature {
            self.addresses.remove(&address);
        }

        self.check_exit();
    }

    // Translates messenger requests into actions upon the MessageHub.
    async fn process_request(
        &mut self,
        fingerprint: Fingerprint<A>,
        action: MessageAction<P, A>,
        beacon: Option<Beacon<P, A>>,
    ) {
        let message;

        match action {
            MessageAction::Send(payload, message_type, timestamp) => {
                message = Some(Message::new(fingerprint.clone(), timestamp, payload, message_type));
            }
            MessageAction::Forward(mut forwarded_message) => {
                message = Some(forwarded_message.clone());
                if let Some(beacon) = self.beacons.get(&fingerprint.id) {
                    match forwarded_message.clone().get_message_type() {
                        MessageType::Origin(audience) => {
                            // Can't forward messages meant for forwarder
                            if Audience::Messenger(fingerprint.signature) == audience {
                                return;
                            }
                            // Ignore forward requests from leafs in broadcast
                            if !self.is_broker(beacon.get_messenger_id()) {
                                return;
                            }
                        }
                        MessageType::Reply(source) => {
                            if let Some(recipient) = source.get_return_path().last() {
                                // If the reply recipient drops the message, do not forward.
                                if recipient.get_messenger_id() == fingerprint.id {
                                    return;
                                }
                            } else {
                                // Every reply should have a return path.
                                forwarded_message.report_status(Status::Undeliverable).await;
                                return;
                            }
                        }
                    }
                } else {
                    forwarded_message.report_status(Status::Undeliverable).await;
                    return;
                }
            }
        }

        if let Some(mut outgoing_message) = message {
            if let Some(handle) = beacon {
                outgoing_message.add_participant(handle);
            }

            self.send_to_next(fingerprint.id, outgoing_message).await;
        }
    }
}
