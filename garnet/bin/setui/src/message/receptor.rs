// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::action_fuse::ActionFuseHandle;
use crate::message::base::{Address, MessageEvent, Payload};
use anyhow::{format_err, Error};
use futures::channel::mpsc::UnboundedReceiver;
use futures::StreamExt;

use futures::lock::Mutex;
use std::sync::Arc;

type EventReceiver<P, A> = UnboundedReceiver<MessageEvent<P, A>>;

/// A Receptor is a wrapper around a channel dedicated towards either receiving
/// top-level messages delivered to the recipient's address or replies to a
/// message the recipient sent previously. Receptors are always paired with a
/// Beacon. When all references to a given Receptor go out of scope, the Beacon
/// is notified to deactivate through the ActionFuse provided at construction.
///
/// Clients interact with the Receptor similar to a Receiver, waiting on a new
/// MessageEvent via the watch method.
#[derive(Clone)]
pub struct Receptor<P: Payload + 'static, A: Address + 'static> {
    event_rx: Arc<Mutex<EventReceiver<P, A>>>,
    fuse: ActionFuseHandle,
}

impl<P: Payload + 'static, A: Address + 'static> Receptor<P, A> {
    pub(super) fn new(fuse: ActionFuseHandle, event_rx: EventReceiver<P, A>) -> Receptor<P, A> {
        Receptor { event_rx: Arc::new(Mutex::new(event_rx)), fuse: fuse }
    }

    pub async fn watch(&mut self) -> Result<MessageEvent<P, A>, Error> {
        if let Some(event) = self.event_rx.lock().await.next().await {
            return Ok(event);
        }

        return Err(format_err!("could not retrieve event"));
    }
}
