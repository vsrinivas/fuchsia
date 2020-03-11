// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::base::*;
use crate::message::message_client::MessageClient;
use crate::message::message_hub::MessageHub;
use crate::message::receptor::*;
use futures::executor::block_on;
use futures::lock::Mutex;
use std::fmt::Debug;
use std::hash::Hash;
use std::sync::Arc;

#[derive(Clone, PartialEq, Debug, Copy)]
enum TestMessage {
    Foo,
    Bar,
}

#[derive(Clone, Eq, PartialEq, Debug, Copy, Hash)]
enum TestAddress {
    Foo(u64),
}

/// Ensures the payload matches expected value and invokes an action closure.
async fn verify_payload<
    P: Clone + Debug + PartialEq + Send + Sync + 'static,
    A: Clone + Eq + Hash + Send + Sync + 'static,
>(
    payload: P,
    receptor: &mut Receptor<P, A>,
    client_fn: Option<Box<dyn Fn(&mut MessageClient<P, A>) + Send + Sync + 'static>>,
) {
    while let Ok(message_event) = receptor.watch().await {
        if let MessageEvent::Message(incoming_payload, mut client) = message_event {
            assert_eq!(payload, incoming_payload);
            if let Some(func) = client_fn {
                (func)(&mut client);
            }
            return;
        }
    }

    panic!("Should have received value");
}

/// Ensures the delivery result matches expected value.
async fn verify_result<
    P: Clone + Debug + PartialEq + Send + Sync + 'static,
    A: Clone + Eq + Hash + Send + Sync + 'static,
>(
    expected: DeliveryStatus,
    receptor: &mut Receptor<P, A>,
) {
    while let Ok(message_event) = receptor.watch().await {
        if let MessageEvent::Status(status) = message_event {
            if status == expected {
                return;
            }
        }
    }

    panic!("Didn't receive result expected");
}

static ORIGINAL: TestMessage = TestMessage::Foo;
static REPLY: TestMessage = TestMessage::Bar;

/// Tests basic functionality of the MessageHub, ensuring messages and replies
/// are properly delivered.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_end_to_end_messaging() {
    let hub = MessageHub::<TestMessage, TestAddress>::create();

    let (messenger_1, _) =
        hub.lock().await.create_messenger(MessengerType::Addressable(TestAddress::Foo(1)));
    let (_, mut receptor_2) =
        hub.lock().await.create_messenger(MessengerType::Addressable(TestAddress::Foo(2)));

    let mut reply_receptor =
        messenger_1.message(ORIGINAL, Audience::Address(TestAddress::Foo(2))).send();

    verify_payload(
        ORIGINAL,
        &mut receptor_2,
        Some(Box::new(|client| {
            let _ = client.reply(REPLY).send();
        })),
    )
    .await;

    verify_payload(REPLY, &mut reply_receptor, None).await;
}

/// Tests forwarding behavior, making sure a message is forwarded in the case
/// the client does nothing with it.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_implicit_forward() {
    let hub = MessageHub::<TestMessage, TestAddress>::create();

    let (messenger_1, _) =
        hub.lock().await.create_messenger(MessengerType::Addressable(TestAddress::Foo(1)));
    let (_, mut receiver_2) = hub.lock().await.create_messenger(MessengerType::Broker);
    let (_, mut receiver_3) =
        hub.lock().await.create_messenger(MessengerType::Addressable(TestAddress::Foo(3)));

    let mut reply_receptor =
        messenger_1.message(ORIGINAL, Audience::Address(TestAddress::Foo(3))).send();

    // Ensure observer gets payload and then do nothing with message.
    verify_payload(ORIGINAL, &mut receiver_2, None).await;

    verify_payload(
        ORIGINAL,
        &mut receiver_3,
        Some(Box::new(|client| {
            let _ = client.reply(REPLY).send();
        })),
    )
    .await;

    verify_payload(REPLY, &mut reply_receptor, None).await;
}

/// Exercises the observation functionality. Makes sure a broker who has
/// indicated they would like to participate in a message path receives the
/// reply.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_observe_addressable() {
    let hub = MessageHub::<TestMessage, TestAddress>::create();

    let (messenger_1, _) =
        hub.lock().await.create_messenger(MessengerType::Addressable(TestAddress::Foo(1)));
    let (_, mut receptor_2) = hub.lock().await.create_messenger(MessengerType::Broker);
    let (_, mut receptor_3) =
        hub.lock().await.create_messenger(MessengerType::Addressable(TestAddress::Foo(3)));

    let mut reply_receptor =
        messenger_1.message(ORIGINAL, Audience::Address(TestAddress::Foo(3))).send();

    let observe_receptor = Arc::new(Mutex::new(None));

    let observe_receptor_clone = observe_receptor.clone();
    verify_payload(
        ORIGINAL,
        &mut receptor_2,
        Some(Box::new(move |client| {
            let mut receptor = block_on(observe_receptor_clone.lock());
            *receptor = Some(client.observe());
        })),
    )
    .await;

    verify_payload(
        ORIGINAL,
        &mut receptor_3,
        Some(Box::new(|client| {
            let _ = client.reply(REPLY).send();
        })),
    )
    .await;

    if let Some(mut receptor) = observe_receptor.lock().await.take() {
        verify_payload(REPLY, &mut receptor, None).await;
    } else {
        panic!("A receptor should have been assigned")
    }
    verify_payload(REPLY, &mut reply_receptor, None).await;
}

/// Tests the broadcast functionality. Ensures all non-sending, addressable
/// messengers receive a broadcast message.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_broadcast() {
    let hub = MessageHub::<TestMessage, TestAddress>::create();

    let (messenger_1, _) =
        hub.lock().await.create_messenger(MessengerType::Addressable(TestAddress::Foo(1)));
    let (_, mut receptor_2) =
        hub.lock().await.create_messenger(MessengerType::Addressable(TestAddress::Foo(2)));
    let (_, mut receptor_3) =
        hub.lock().await.create_messenger(MessengerType::Addressable(TestAddress::Foo(3)));

    messenger_1.message(ORIGINAL, Audience::Broadcast).send();

    verify_payload(ORIGINAL, &mut receptor_2, None).await;
    verify_payload(ORIGINAL, &mut receptor_3, None).await;
}

/// Verifies delivery statuses are properly relayed back to the original sender.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_delivery_status() {
    let hub = MessageHub::<TestMessage, TestAddress>::create();
    let known_receiver_address = TestAddress::Foo(2);
    let unknown_address = TestAddress::Foo(3);
    let (messenger_1, _) =
        hub.lock().await.create_messenger(MessengerType::Addressable(TestAddress::Foo(1)));
    let (_, mut receptor_2) =
        hub.lock().await.create_messenger(MessengerType::Addressable(known_receiver_address));

    {
        let mut receptor =
            messenger_1.message(ORIGINAL, Audience::Address(known_receiver_address)).send();

        // Ensure observer gets payload and then do nothing with message.
        verify_payload(ORIGINAL, &mut receptor_2, None).await;

        verify_result(DeliveryStatus::Received, &mut receptor).await;
    }

    {
        let mut receptor = messenger_1.message(ORIGINAL, Audience::Address(unknown_address)).send();

        verify_result(DeliveryStatus::Undeliverable, &mut receptor).await;
    }
}

/// Verifies beacon returns error when receptor goes out of scope.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_beacon_error() {
    let hub = MessageHub::<TestMessage, TestAddress>::create();

    let (messenger, _) =
        hub.lock().await.create_messenger(MessengerType::Addressable(TestAddress::Foo(1)));
    {
        let (_, mut receptor) =
            hub.lock().await.create_messenger(MessengerType::Addressable(TestAddress::Foo(2)));

        verify_result(
            DeliveryStatus::Received,
            &mut messenger.message(ORIGINAL, Audience::Address(TestAddress::Foo(2))).send(),
        )
        .await;
        verify_payload(ORIGINAL, &mut receptor, None).await;
    }

    verify_result(
        DeliveryStatus::Undeliverable,
        &mut messenger.message(ORIGINAL, Audience::Address(TestAddress::Foo(2))).send(),
    )
    .await;
}
