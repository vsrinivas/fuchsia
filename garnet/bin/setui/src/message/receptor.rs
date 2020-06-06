// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::action_fuse::ActionFuseHandle;
use crate::message::base::{Address, DeliveryStatus, MessageEvent, Payload};
use crate::message::message_client::MessageClient;
use anyhow::{format_err, Error};
use futures::channel::mpsc::UnboundedReceiver;
use futures::StreamExt;

use futures::lock::Mutex;
use std::sync::Arc;

type EventReceiver<P, A> = UnboundedReceiver<MessageEvent<P, A>>;

/// A Receptor is a wrapper around a channel dedicated towards either receiving
/// top-level messages delivered to the recipient's address or replies to a
/// message the recipient sent previously. Receptors are always paired with a
/// Beacon.
///
/// Clients interact with the Receptor similar to a Receiver, waiting on a new
/// MessageEvent via the watch method.
pub struct Receptor<P: Payload + 'static, A: Address + 'static> {
    event_rx: Arc<Mutex<EventReceiver<P, A>>>,
    // Fuse to be triggered when all receptors go out of scope.
    _fuse: ActionFuseHandle,
}

impl<P: Payload + 'static, A: Address + 'static> Receptor<P, A> {
    pub(super) fn new(event_rx: EventReceiver<P, A>, fuse: ActionFuseHandle) -> Receptor<P, A> {
        Receptor { event_rx: Arc::new(Mutex::new(event_rx)), _fuse: fuse }
    }

    pub async fn watch(&mut self) -> Result<MessageEvent<P, A>, Error> {
        if let Some(event) = self.event_rx.lock().await.next().await {
            return Ok(event);
        }

        return Err(format_err!("could not retrieve event"));
    }

    /// Returns the next pending payload, returning an Error if the origin
    /// message (if any) was not deliverable or another error was encountered.
    pub async fn next_payload(&mut self) -> Result<(P, MessageClient<P, A>), Error> {
        while let Ok(event) = self.watch().await {
            match event {
                MessageEvent::Message(payload, client) => {
                    return Ok((payload, client));
                }
                MessageEvent::Status(DeliveryStatus::Undeliverable) => {
                    return Err(format_err!("origin message not delivered"));
                }
                _ => {}
            }
        }

        return Err(format_err!("could not retrieve payload"));
    }

    // Used to consume receptor.
    pub fn ack(self) {}
}
