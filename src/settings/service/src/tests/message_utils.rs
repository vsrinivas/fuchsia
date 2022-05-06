// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::base::{Address, MessageEvent, Payload, Role};
use crate::message::message_client::MessageClient;
use crate::message::receptor::Receptor;
use futures::future::BoxFuture;
use futures::StreamExt;

pub(crate) type ClientFn<P, A, R> =
    Box<dyn FnOnce(MessageClient<P, A, R>) -> BoxFuture<'static, ()> + Send + Sync + 'static>;

/// Ensures the payload matches expected value and invokes an action closure.
/// If a client_fn is not provided, the message is acknowledged.
pub(crate) async fn verify_payload<
    P: Payload + PartialEq + 'static,
    A: Address + 'static,
    R: Role + 'static,
>(
    payload: P,
    receptor: &mut Receptor<P, A, R>,
    client_fn: Option<ClientFn<P, A, R>>,
) {
    while let Some(message_event) = receptor.next().await {
        if let MessageEvent::Message(incoming_payload, mut client) = message_event {
            assert_eq!(payload, incoming_payload);
            if let Some(func) = client_fn {
                (func)(client).await;
            } else {
                client.acknowledge().await;
            }
            return;
        }
    }

    panic!("Should have received value");
}
