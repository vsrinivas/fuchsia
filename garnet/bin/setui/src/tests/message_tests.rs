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
async fn verify_payload<P: Payload + PartialEq + 'static, A: Address + PartialEq + 'static>(
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
async fn verify_result<P: Payload + PartialEq + 'static, A: Address + PartialEq + 'static>(
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

/// Tests messenger creation and address space collision.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_messenger_creation() {
    let messenger_factory = MessageHub::<u64, u64>::create();
    let address = 1;

    let messenger_1_result = messenger_factory.create(MessengerType::Addressable(address)).await;
    assert!(messenger_1_result.is_ok());

    assert!(messenger_factory.create(MessengerType::Addressable(address)).await.is_err());
}

/// Tests basic functionality of the MessageHub, ensuring messages and replies
/// are properly delivered.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_end_to_end_messaging() {
    let messenger_factory = MessageHub::<TestMessage, TestAddress>::create();

    let (messenger_1, _) =
        messenger_factory.create(MessengerType::Addressable(TestAddress::Foo(1))).await.unwrap();
    let (_, mut receptor_2) =
        messenger_factory.create(MessengerType::Addressable(TestAddress::Foo(2))).await.unwrap();

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
    let messenger_factory = MessageHub::<TestMessage, TestAddress>::create();

    let (messenger_1, _) =
        messenger_factory.create(MessengerType::Addressable(TestAddress::Foo(1))).await.unwrap();
    let (_, mut receiver_2) = messenger_factory.create(MessengerType::Broker).await.unwrap();
    let (_, mut receiver_3) =
        messenger_factory.create(MessengerType::Addressable(TestAddress::Foo(3))).await.unwrap();

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
    let messenger_factory = MessageHub::<TestMessage, TestAddress>::create();

    let (messenger_1, _) =
        messenger_factory.create(MessengerType::Addressable(TestAddress::Foo(1))).await.unwrap();
    let (_, mut receptor_2) = messenger_factory.create(MessengerType::Broker).await.unwrap();
    let (_, mut receptor_3) =
        messenger_factory.create(MessengerType::Addressable(TestAddress::Foo(3))).await.unwrap();

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
    let messenger_factory = MessageHub::<TestMessage, TestAddress>::create();

    let (messenger_1, _) =
        messenger_factory.create(MessengerType::Addressable(TestAddress::Foo(1))).await.unwrap();
    let (_, mut receptor_2) =
        messenger_factory.create(MessengerType::Addressable(TestAddress::Foo(2))).await.unwrap();
    let (_, mut receptor_3) =
        messenger_factory.create(MessengerType::Addressable(TestAddress::Foo(3))).await.unwrap();

    messenger_1.message(ORIGINAL, Audience::Broadcast).send();

    verify_payload(ORIGINAL, &mut receptor_2, None).await;
    verify_payload(ORIGINAL, &mut receptor_3, None).await;
}

/// Verifies delivery statuses are properly relayed back to the original sender.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_delivery_status() {
    let messenger_factory = MessageHub::<TestMessage, TestAddress>::create();
    let known_receiver_address = TestAddress::Foo(2);
    let unknown_address = TestAddress::Foo(3);
    let (messenger_1, _) =
        messenger_factory.create(MessengerType::Addressable(TestAddress::Foo(1))).await.unwrap();
    let (_, mut receptor_2) =
        messenger_factory.create(MessengerType::Addressable(known_receiver_address)).await.unwrap();

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
    let messenger_factory = MessageHub::<TestMessage, TestAddress>::create();

    let (messenger, _) =
        messenger_factory.create(MessengerType::Addressable(TestAddress::Foo(1))).await.unwrap();
    {
        let (_, mut receptor) = messenger_factory
            .create(MessengerType::Addressable(TestAddress::Foo(2)))
            .await
            .unwrap();

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
