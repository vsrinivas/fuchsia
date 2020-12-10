// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::action_fuse::ActionFuseHandle;
use crate::message::base::{Address, MessageEvent, Payload, Role, Signature, Status};
use crate::message::message_client::MessageClient;
use anyhow::{format_err, Error};
use futures::channel::mpsc::UnboundedReceiver;
use futures::task::{Context, Poll};
use futures::Stream;
use futures::StreamExt;
use std::pin::Pin;

type EventReceiver<P, A, R> = UnboundedReceiver<MessageEvent<P, A, R>>;

/// A Receptor is a wrapper around a channel dedicated towards either receiving
/// top-level messages delivered to the recipient's address or replies to a
/// message the recipient sent previously. Receptors are always paired with a
/// Beacon.
///
/// Clients interact with the Receptor similar to a Receiver, waiting on a new
/// MessageEvent via the watch method.
pub struct Receptor<P: Payload + 'static, A: Address + 'static, R: Role + 'static> {
    signature: Signature<A>,
    event_rx: EventReceiver<P, A, R>,
    // Fuse to be triggered when all receptors go out of scope.
    _fuse: ActionFuseHandle,
}

impl<P: Payload + 'static, A: Address + 'static, R: Role + 'static> Stream for Receptor<P, A, R> {
    type Item = MessageEvent<P, A, R>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        self.event_rx.poll_next_unpin(cx)
    }
}

impl<P: Payload + 'static, A: Address + 'static, R: Role + 'static> Receptor<P, A, R> {
    pub(super) fn new(
        signature: Signature<A>,
        event_rx: EventReceiver<P, A, R>,
        fuse: ActionFuseHandle,
    ) -> Self {
        Self { signature, event_rx, _fuse: fuse }
    }

    /// Returns the signature associated the top level messenger associated with
    /// this receptor.
    pub fn get_signature(&self) -> Signature<A> {
        self.signature.clone()
    }

    /// Returns the next pending payload, returning an Error if the origin
    /// message (if any) was not deliverable or another error was encountered.
    pub async fn next_payload(&mut self) -> Result<(P, MessageClient<P, A, R>), Error> {
        while let Some(event) = self.next().await {
            match event {
                MessageEvent::Message(payload, client) => {
                    return Ok((payload, client));
                }
                MessageEvent::Status(Status::Undeliverable) => {
                    return Err(format_err!("origin message not delivered"));
                }
                _ => {}
            }
        }

        return Err(format_err!("could not retrieve payload"));
    }

    pub async fn wait_for_acknowledge(&mut self) -> Result<(), Error> {
        while let Some(event) = self.next().await {
            match event {
                MessageEvent::Status(Status::Acknowledged) => {
                    return Ok(());
                }
                MessageEvent::Status(Status::Undeliverable) => {
                    return Err(format_err!("origin message not delivered"));
                }
                _ => {}
            }
        }

        return Err(format_err!("did not encounter acknowledged status"));
    }

    // Used to consume receptor.
    pub fn ack(self) {}

    /// Relays a response to the given message client. Useful for chaining
    /// together responses.
    pub async fn relay(
        &mut self,
        client: MessageClient<P, A, R>,
    ) -> Result<Receptor<P, A, R>, Error> {
        self.next_payload().await.map(|payload| client.reply(payload.0).send())
    }
}

/// Extracts the payload from a given `MessageEvent`. Such event is provided
/// in an optional argument to match the return value from `Receptor` stream.
pub fn extract_payload<P: Payload + 'static, A: Address + 'static, R: Role + 'static>(
    event: Option<MessageEvent<P, A, R>>,
) -> Option<P> {
    if let Some(MessageEvent::Message(payload, _)) = event {
        return Some(payload);
    } else {
        return None;
    }
}
