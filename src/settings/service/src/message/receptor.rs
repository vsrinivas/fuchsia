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
use std::convert::TryFrom;
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
    pub(crate) fn get_signature(&self) -> Signature<A> {
        self.signature
    }

    /// Returns the next pending payload, returning an Error if the origin
    /// message (if any) was not deliverable or another error was encountered.
    pub(crate) async fn next_payload(&mut self) -> Result<(P, MessageClient<P, A, R>), Error> {
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

        Err(format_err!("could not retrieve payload"))
    }

    pub(crate) async fn next_of<T: TryFrom<P>>(
        &mut self,
    ) -> Result<(T, MessageClient<P, A, R>), Error>
    where
        <T as std::convert::TryFrom<P>>::Error: std::fmt::Debug,
    {
        let (payload, client) = self.next_payload().await?;

        let converted_payload = T::try_from(payload)
            .map(move |converted_payload| (converted_payload, client))
            .map_err(|err| format_err!("conversion failed: {:?}", err));

        // Treat any conversion failures as fatal.
        if converted_payload.is_err() {
            panic!("did not receive payload of expected type");
        }

        converted_payload
    }

    #[cfg(test)]
    pub(crate) async fn wait_for_acknowledge(&mut self) -> Result<(), Error> {
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

        Err(format_err!("did not encounter acknowledged status"))
    }

    // Used to consume receptor.
    pub(crate) fn ack(self) {}
}

/// Extracts the payload from a given `MessageEvent`. Such event is provided
/// in an optional argument to match the return value from `Receptor` stream.
pub(crate) fn extract_payload<P: Payload + 'static, A: Address + 'static, R: Role + 'static>(
    event: Option<MessageEvent<P, A, R>>,
) -> Option<P> {
    if let Some(MessageEvent::Message(payload, _)) = event {
        Some(payload)
    } else {
        None
    }
}
