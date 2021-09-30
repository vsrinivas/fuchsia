// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::action_fuse::ActionFuseBuilder;
use crate::message::action_fuse::ActionFuseHandle;
use crate::message::base::{
    filter::Filter, messenger, role, ActionSender, Address, Attribution, Audience, Fingerprint,
    Message, MessageAction, MessageClientId, MessageError, MessageType, MessengerAction,
    MessengerId, MessengerType, Payload, Role, Signature, Status,
};
use crate::message::beacon::{Beacon, BeaconBuilder};
use crate::message::delegate::Delegate;
use crate::message::messenger::{Messenger, MessengerClient};
use crate::trace::TracingNonce;
use crate::{trace, trace_guard};
use anyhow::format_err;
use fuchsia_async as fasync;
use fuchsia_syslog::{fx_log_err, fx_log_warn};
use futures::lock::Mutex;
use futures::StreamExt;
use std::borrow::Cow;
use std::collections::{HashMap, HashSet};
use std::iter::FromIterator;
use std::sync::Arc;

/// Type definition for a handle to the MessageHub. There is a single instance
/// of a hub per communication ecosystem and therefore held behind an Arc mutex.
pub type MessageHubHandle<P, A, R> = Arc<Mutex<MessageHub<P, A, R>>>;

/// Type definition for exit message sender.
type ExitSender = futures::channel::mpsc::UnboundedSender<()>;

#[derive(thiserror::Error, Debug, Clone)]
pub enum Error {
    #[error("Failed to send response for operation: {0:?}")]
    ResponseSendFail(Cow<'static, str>),
    #[error("Messenger not present")]
    MessengerNotFound,
}

/// `Broker` captures the information necessary to process messages to a broker.
#[derive(Clone)]
struct Broker<P: Payload + 'static, A: Address + 'static, R: Role + 'static> {
    /// The `MessengerId` associated with the broker so that it can be distinguished from other
    /// messengers.
    messenger_id: MessengerId,
    /// A condition that is applied to a message to determine whether it should be directed to the
    /// broker.
    filter: Option<Filter<P, A, R>>,
}

impl<P: Payload + 'static, A: Address + 'static, R: Role + 'static> PartialEq for Broker<P, A, R> {
    fn eq(&self, other: &Self) -> bool {
        // Since each broker has a unique [`MessengerId`], it is implied that any brokers that share
        // the same [`MessengerId`] are the same, having matching filters as well.
        self.messenger_id == other.messenger_id
    }
}

/// The MessageHub controls the message flow for a set of messengers. It
/// processes actions upon messages, incorporates brokers, and signals receipt
/// of messages.
pub struct MessageHub<P: Payload + 'static, A: Address + 'static, R: Role + 'static> {
    /// A sender given to messengers to signal actions upon the MessageHub.
    action_tx: ActionSender<P, A, R>,
    /// Address mapping for looking up messengers. Used for sending messages
    /// to an addressable recipient.
    addresses: HashMap<A, MessengerId>,
    /// MessengerId mapping to roles. This mapping allows the `MessageHub`
    /// to remove a messenger from role memberships upon removal.
    messengers: HashMap<MessengerId, messenger::Roles<R>>,
    /// Role mapping for looking up which messengers belong to a particular
    /// role.
    roles: HashMap<role::Signature<R>, HashSet<MessengerId>>,
    /// Mapping of registered messengers (including brokers) to beacons. Used for
    /// delivering messages from a resolved address or a list of participants.
    beacons: HashMap<MessengerId, Beacon<P, A, R>>,
    /// An ordered set of messengers who will be forwarded messages.
    brokers: Vec<Broker<P, A, R>>,
    /// The next id to be given to a messenger.
    next_id: MessengerId,
    /// The next id to be given to a `MessageClient`.
    next_message_client_id: MessageClientId,
    /// Indicates whether the messenger channel has closed.
    messenger_channel_closed: bool,
    /// Generator for creating new roles.
    role_generator: role::Generator,
    /// Sender to signal when the hub should exit.
    exit_tx: ExitSender,
}

impl<P: Payload + 'static, A: Address + 'static, R: Role + 'static> MessageHub<P, A, R> {
    /// Returns a new MessageHub for the given types.
    pub(crate) fn create(fuse: Option<ActionFuseHandle>) -> Delegate<P, A, R> {
        let (action_tx, mut action_rx) = futures::channel::mpsc::unbounded::<(
            Fingerprint<A>,
            MessageAction<P, A, R>,
            Option<Beacon<P, A, R>>,
        )>();
        let (messenger_tx, mut messenger_rx) =
            futures::channel::mpsc::unbounded::<MessengerAction<P, A, R>>();

        // A channel used by the MessageHub to listen to requests for
        // role-related actions.
        let (role_tx, mut role_rx) = futures::channel::mpsc::unbounded::<role::Action<R>>();

        let (exit_tx, mut exit_rx) = futures::channel::mpsc::unbounded::<()>();

        let mut hub = MessageHub {
            next_id: 0,
            next_message_client_id: 0,
            action_tx,
            beacons: HashMap::new(),
            addresses: HashMap::new(),
            messengers: HashMap::new(),
            roles: HashMap::new(),
            brokers: Vec::new(),
            messenger_channel_closed: false,
            role_generator: role::Generator::new(),
            exit_tx,
        };

        fasync::Task::spawn(async move {
            // Released when exiting scope to signal completion.
            let _scope_fuse = fuse;
            let nonce = fuchsia_trace::generate_nonce();

            trace!(nonce, "message hub");
            loop {
                // We must prioritize the action futures. Exit actions
                // take absolute priority. Message actions are ordered before
                // messenger in case the messenger is subsequently deleted.
                futures::select_biased! {
                    _ = exit_rx.next() => {
                        break;
                    }
                    message_action = action_rx.select_next_some() => {
                        trace!(
                            nonce,

                            "message action"
                        );
                        let (fingerprint, action, beacon) = message_action;
                        hub.process_request(nonce, fingerprint, action, beacon).await;
                    }
                    messenger_action = messenger_rx.next() => {
                        trace!(
                            nonce,

                            "messenger action"
                        );
                        match messenger_action {
                            Some(action) => {
                                hub.process_messenger_request(nonce, action).await;
                            }
                            None => {
                                hub.messenger_channel_closed = true;
                                hub.check_exit();
                            }
                        }
                    }
                    role_action = role_rx.select_next_some() => {
                        trace!(
                            nonce,

                            "role action"
                        );
                        if hub.process_role_request(nonce, role_action).is_err() {
                            fx_log_err!("failed to process role action");
                        }
                     }
                }
            }
        })
        .detach();

        Delegate::new(messenger_tx, role_tx)
    }

    fn check_exit(&self) {
        if self.messenger_channel_closed && self.beacons.is_empty() {
            // Panic if send failed, otherwise, spawn might not be ended.
            self.exit_tx
                .unbounded_send(())
                .expect("MessageHub::check_exit, exit_tx failed to send exit signal");
        }
    }

    // Determines whether the beacon belongs to a broker.
    fn is_broker(&self, messenger_id: MessengerId) -> bool {
        self.brokers.iter().any(|broker| broker.messenger_id == messenger_id)
    }

    // Derives the underlying MessengerId from a Signature.
    fn resolve_messenger_id(&self, signature: &Signature<A>) -> Result<MessengerId, Error> {
        Ok(match signature {
            Signature::Anonymous(id) => *id,
            Signature::Address(address) => {
                *self.addresses.get(&address).ok_or(Error::MessengerNotFound)?
            }
        })
    }

    /// Internally routes a message to the next appropriate receiver. New messages
    /// are routed based on the intended recipient(s), while replies follow the
    /// return path of the source message. The provided sender id represents the
    /// id of the current messenger possessing the message and not necessarily
    /// the original author.
    async fn send_to_next(
        &mut self,
        nonce: TracingNonce,
        sender_id: MessengerId,
        message: Message<P, A, R>,
    ) {
        trace!(nonce, "send_to_next");
        let mut recipients = vec![];

        let message_type = message.get_type();

        let mut require_delivery = false;

        // Replies have a predetermined return path.
        if let MessageType::Reply(source) = message_type {
            // The original author of the reply will be the first participant after brokers in
            // the reply's return path. Otherwise, identify current sender in the source return path
            // and forward to next participant.
            let source_return_path = source.get_return_path();
            let mut target_index = None;

            let source_return_path_messenger_ids: HashSet<MessengerId> =
                source_return_path.iter().map(|beacon| beacon.get_messenger_id()).collect();

            // Identify participating brokers. This brokers must:
            // 1. Not be already participating in the return path (with a spawned observer)
            // 2. Not be the author of the reply.
            // 3. Have a matching filter.
            let broker_ids: Vec<_> = self
                .brokers
                .iter()
                .filter(|broker| {
                    !source_return_path_messenger_ids.contains(&broker.messenger_id)
                        && self
                            .resolve_messenger_id(&message.get_author())
                            .map_or(true, |id| id != broker.messenger_id)
                        && broker.filter.as_ref().map_or(true, |filter| filter.matches(&message))
                })
                .map(|broker| broker.messenger_id)
                .collect();

            let mut return_path: Vec<Beacon<P, A, R>> = broker_ids
                .iter()
                .map(|broker_id| {
                    self.beacons.get(&broker_id).expect("beacon should resolve").clone()
                })
                .collect();

            // The return path places the participating brokers before the participants from the
            // source message's reply path.
            return_path.extend(source_return_path.iter().cloned());
            let last_index = return_path.len() - 1;

            if self.is_broker(sender_id) && !source_return_path_messenger_ids.contains(&sender_id) {
                // If the sender is in the return path as a broker and not a participant in the
                // source message's return path, determine next broker to forward to.
                let mut candidate_index = self
                    .brokers
                    .iter()
                    .position(|broker| broker.messenger_id == sender_id)
                    .expect("broker should be found")
                    + 1;

                // A candidate broker is one that is after the sending broker, has a filter
                // matching the current message, and is not in the return path already.
                while candidate_index < self.brokers.len() && target_index.is_none() {
                    target_index = broker_ids.iter().position(|broker_id| {
                        *broker_id == self.brokers[candidate_index].messenger_id
                            && !source_return_path_messenger_ids.contains(&broker_id)
                    });

                    candidate_index += 1;
                }

                // If we can't find a next broker, we should skip over those considered.
                if target_index.is_none() {
                    target_index = Some(broker_ids.len());
                }
            } else if sender_id == message.get_return_path()[0].get_messenger_id()
                && !matches!(message.get_attribution(), Attribution::Derived(..))
            {
                // If this is the reply's original author, send to the first
                // messenger in the original message's return path.
                target_index = Some(0);

                // Mark source message as delivered. In the case the sender is the
                // original intended audience, this will be a no-op. However, if the
                // reply comes beforehand, this will ensure the message is properly
                // acknowledged.
                source.report_status(Status::Received).await;
            } else {
                for (index, beacon) in return_path.iter().enumerate().take(last_index) {
                    if beacon.get_messenger_id() == sender_id {
                        target_index = Some(index + 1);
                    }
                }
            }

            if let Some(index) = target_index {
                recipients.push(return_path.swap_remove(index));

                if index == last_index {
                    // Ack current message if being sent to intended recipient.
                    message.report_status(Status::Received).await;
                }
            }
        } else if let Some(beacon) = self.beacons.get(&sender_id) {
            let author_id = self
                .resolve_messenger_id(&message.get_author())
                .expect("messenger should be present");

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
                    if let Ok((resolved_messengers, delivery_required)) =
                        self.resolve_audience(sender_id, &audience)
                    {
                        target_messengers.append(&mut Vec::from_iter(resolved_messengers));
                        require_delivery |= delivery_required;
                    } else {
                        // This error will occur if the sender specifies a non-existent
                        // address.
                        message.report_status(Status::Undeliverable).await;
                    }

                    if audience.contains(&Audience::Broadcast) {
                        // Broadcasts don't require any audience.
                        message.report_status(Status::Broadcasted).await;
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

    /// Resolves the audience into a set of MessengerIds. Also returns whether
    /// delivery is required (broadcasts for example don't require delivery
    /// confirmation). If there is an issue resolving an audience, an error
    /// is returned. Errors should halt any further processing on the audience
    /// set.
    fn resolve_audience(
        &self,
        sender_id: MessengerId,
        audience: &Audience<A, R>,
    ) -> Result<(HashSet<MessengerId>, bool), anyhow::Error> {
        let mut return_set = HashSet::new();
        let mut delivery_required = false;

        match audience {
            Audience::Address(address) => {
                delivery_required = true;
                if let Some(&messenger_id) = self.addresses.get(&address) {
                    let _ = return_set.insert(messenger_id);
                } else {
                    return Err(format_err!("could not resolve address"));
                }
            }
            Audience::Role(role) => {
                if let Some(messengers) = self.roles.get(role) {
                    return_set.extend(messengers);
                }
            }
            Audience::Group(group) => {
                for audience in &group.audiences {
                    let resolved_audience = self.resolve_audience(sender_id, &audience)?;
                    return_set.extend(resolved_audience.0);
                    delivery_required |= resolved_audience.1;
                }
            }
            Audience::Messenger(signature) => {
                delivery_required = true;
                match signature {
                    Signature::Address(address) => {
                        if let Some(&messenger_id) = self.addresses.get(&address) {
                            let _ = return_set.insert(messenger_id);
                        } else {
                            return Err(format_err!("could not resolve signature"));
                        }
                    }
                    Signature::Anonymous(id) => {
                        let _ = return_set.insert(*id);
                    }
                }
            }
            Audience::Broadcast => {
                // Gather all messengers
                for &id in self.beacons.keys() {
                    if id != sender_id && !self.is_broker(id) {
                        let _ = return_set.insert(id);
                    }
                }
            }
        }

        Ok((return_set, delivery_required))
    }

    async fn process_messenger_request(
        &mut self,
        nonce: TracingNonce,
        action: MessengerAction<P, A, R>,
    ) {
        match action {
            MessengerAction::Create(messenger_descriptor, responder, messenger_tx) => {
                trace!(
                    nonce,

                    "process messenger request create",
                    "messenger_type" => format!("{:?}", messenger_descriptor.messenger_type).as_str()
                );

                let mut optional_address = None;
                if let MessengerType::Addressable(address) = messenger_descriptor.messenger_type {
                    if self.addresses.contains_key(&address) {
                        // Ignore the result since an error would imply the other side is already
                        // closed.
                        let _ = responder.send(Err(MessageError::AddressConflict { address }));
                        return;
                    }
                    optional_address = Some(address);
                }

                let id = self.next_id;
                let signature = if let Some(address) = optional_address {
                    Signature::Address(address)
                } else {
                    Signature::Anonymous(id)
                };

                let messenger =
                    Messenger::new(Fingerprint { id, signature }, self.action_tx.clone());

                // Create fuse to delete Messenger.
                let fuse = ActionFuseBuilder::new()
                    .add_action(Box::new(move || {
                        // Do not send deletion request if other side is closed.
                        if messenger_tx.is_closed() {
                            return;
                        }

                        // ActionFuse drop method might cause the send failed.
                        messenger_tx
                            .unbounded_send(MessengerAction::DeleteBySignature(signature))
                            .unwrap_or_else(|_| {
                                fx_log_warn!(
                                    "messenger_tx failed to send delete action for signature: {:?}",
                                    signature
                                )
                            });
                    }))
                    .build();

                self.next_id += 1;
                let (beacon, receptor) =
                    BeaconBuilder::new(messenger.clone()).add_fuse(Arc::clone(&fuse)).build();
                let _ = self.beacons.insert(id, beacon);

                match messenger_descriptor.messenger_type {
                    MessengerType::Broker(filter) => {
                        self.brokers.push(Broker { messenger_id: id, filter });
                    }
                    MessengerType::Addressable(address) => {
                        let _ = self.addresses.insert(address, id);
                    }
                    MessengerType::Unbound => {
                        // We do not track Unbounded messengers.
                    }
                }

                // Track roles
                for role in &messenger_descriptor.roles {
                    let _ = self.roles.entry(*role).or_insert_with(HashSet::new).insert(id);
                }

                // Update descriptor mapping and role records
                let _ = self.messengers.insert(id, messenger_descriptor.roles);

                let response_result =
                    responder.send(Ok((MessengerClient::new(messenger, fuse), receptor)));
                if let Err(_) = response_result {
                    // TODO(fxbug.dev/85529) Track whether this is common, if so, bubble the error
                    // up.
                    fx_log_warn!(
                        "Receiving end of oneshot closed while trying to create messenger client \
                            for client with id: {}",
                        id,
                    );
                }
            }
            #[cfg(test)]
            MessengerAction::CheckPresence(signature, responder) => {
                trace!(nonce, "process messenger request check presence");
                responder.send(Ok(self.resolve_messenger_id(&signature).is_ok())).ok();
            }
            MessengerAction::DeleteBySignature(signature) => {
                trace!(nonce, "process messenger request delete");
                self.delete_by_signature(signature)
            }
        }
    }

    fn delete_by_signature(&mut self, signature: Signature<A>) {
        let id = self.resolve_messenger_id(&signature).expect("messenger should be present");

        // Clean up roles
        if let Some(roles) = self.messengers.remove(&id) {
            for role in roles {
                // Remove messenger from each role it belongs to. Remove the
                // role as well if it no longer has any members.
                if let Some(messengers) = self.roles.get_mut(&role) {
                    let _ = messengers.remove(&id);

                    if messengers.is_empty() {
                        let _ = self.roles.remove(&role);
                    }
                }
            }
        }

        // These are all safe if the containers don't contain any items matching `id`.
        let _ = self.beacons.remove(&id);
        self.brokers.retain(|broker| id != broker.messenger_id);
        if let Signature::Address(address) = signature {
            let _ = self.addresses.remove(&address);
        }

        self.check_exit();
    }

    // Translates messenger requests into actions upon the MessageHub.
    async fn process_request(
        &mut self,
        nonce: TracingNonce,
        fingerprint: Fingerprint<A>,
        action: MessageAction<P, A, R>,
        beacon: Option<Beacon<P, A, R>>,
    ) {
        let (mut outgoing_message, _guard) = match action {
            MessageAction::Send(payload, message_type, timestamp) => {
                let guard = trace_guard!(
                    nonce,

                    "process request send",
                    "payload" => format!("{:?}", payload).as_str()
                );
                (Message::new(fingerprint, timestamp, payload, message_type), guard)
            }
            MessageAction::Forward(forwarded_message) => {
                let guard = trace_guard!(nonce, "process request forward");
                if let Some(beacon) = self.beacons.get(&fingerprint.id) {
                    match forwarded_message.get_type() {
                        MessageType::Origin(audience) => {
                            // Can't forward messages meant for forwarder
                            if Audience::Messenger(fingerprint.signature) == *audience {
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
                (forwarded_message, guard)
            }
        };

        if let Some(handle) = beacon {
            outgoing_message.add_participant(handle);
        }

        self.send_to_next(nonce, fingerprint.id, outgoing_message).await;
    }

    fn process_role_request(
        &mut self,
        nonce: TracingNonce,
        action: role::Action<R>,
    ) -> Result<(), Error> {
        trace!(nonce, "process role request");
        match action {
            // Handle creating a new role. Currently there is no way this call
            // can fail. A signature for the generated role is handed back
            // through the provided response sender.
            role::Action::Create(result_sender) => result_sender
                .send(Ok(role::Response::Role(self.role_generator.generate())))
                .map(|_| ())
                .map_err(|_| Error::ResponseSendFail("create role".into())),
        }
    }
}
