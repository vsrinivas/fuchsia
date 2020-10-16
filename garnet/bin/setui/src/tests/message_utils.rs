// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::base::{Address, MessageEvent, Payload};
use crate::message::message_client::MessageClient;
use crate::message::receptor::Receptor;
use futures::future::BoxFuture;
use futures::StreamExt;

/// Ensures the payload matches expected value and invokes an action closure.
/// If a client_fn is not provided, the message is acknowledged.
pub(crate) async fn verify_payload<P: Payload + PartialEq + 'static, A: Address + 'static>(
    payload: P,
    receptor: &mut Receptor<P, A>,
    client_fn: Option<
        Box<dyn Fn(&mut MessageClient<P, A>) -> BoxFuture<'_, ()> + Send + Sync + 'static>,
    >,
) {
    while let Some(message_event) = receptor.next().await {
        if let MessageEvent::Message(incoming_payload, mut client) = message_event {
            assert_eq!(payload, incoming_payload);
            if let Some(func) = client_fn {
                (func)(&mut client).await;
            } else {
                client.acknowledge().await;
            }
            return;
        }
    }

    panic!("Should have received value");
}
