// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::clock::now;
use crate::message::action_fuse::ActionFuseBuilder;
use crate::message::base::{
    filter, group, role, Address, Audience, MessageEvent, MessengerType, Payload, Role, Status,
};
use crate::message::messenger::TargetedMessengerClient;
use crate::message::receptor::Receptor;
use crate::message::MessageHubUtil;
use crate::tests::message_utils::verify_payload;
use fuchsia_zircon::DurationNum;
use futures::future::BoxFuture;
use futures::lock::Mutex;
use futures::StreamExt;
use std::fmt::Debug;
use std::hash::Hash;
use std::sync::Arc;
use std::task::Poll;

#[derive(Clone, PartialEq, Debug, Copy)]
pub(crate) enum TestMessage {
    Foo,
    Bar,
    Baz,
    Qux,
    Thud,
}

#[derive(Clone, Eq, PartialEq, Debug, Copy, Hash)]
pub(crate) enum TestAddress {
    Foo(u64),
}

#[derive(Clone, Eq, PartialEq, Debug, Copy, Hash)]
pub(crate) enum TestRole {
    Foo,
    Bar,
}

/// Ensures the delivery result matches expected value.
async fn verify_result<
    P: Payload + PartialEq + 'static,
    A: Address + PartialEq + 'static,
    R: Role + PartialEq + 'static,
>(
    expected: Status,
    receptor: &mut Receptor<P, A, R>,
) {
    while let Some(message_event) = receptor.next().await {
        if let MessageEvent::Status(status) = message_event {
            if status == expected {
                return;
            }
        }
    }

    panic!("Didn't receive result expected");
}

static ORIGINAL: TestMessage = TestMessage::Foo;
static MODIFIED: TestMessage = TestMessage::Qux;
static MODIFIED_2: TestMessage = TestMessage::Thud;
static BROADCAST: TestMessage = TestMessage::Baz;
static REPLY: TestMessage = TestMessage::Bar;

mod test {
    use super::*;
    use crate::message::MessageHubDefinition;

    pub(super) struct MessageHub;
    impl MessageHubDefinition for MessageHub {
        type Payload = TestMessage;
        type Address = TestAddress;
        type Role = TestRole;
    }
}

mod num_test {
    use crate::message::MessageHubDefinition;

    pub(super) struct MessageHub;
    impl MessageHubDefinition for MessageHub {
        type Payload = u64;
        type Address = u64;
        type Role = crate::message::base::default::Role;
    }
}

// Tests message client creation results in unique ids.
#[fuchsia_async::run_until_stalled(test)]
async fn test_message_client_equality() {
    let delegate = test::MessageHub::create_hub();
    let (messenger, _) = delegate.create(MessengerType::Unbound).await.unwrap();
    let (_, mut receptor) = delegate.create(MessengerType::Unbound).await.unwrap();

    let _ = messenger.message(ORIGINAL, Audience::Broadcast).send();
    let (_, client_1) = receptor.next_payload().await.unwrap();

    let _ = messenger.message(ORIGINAL, Audience::Broadcast).send();
    let (_, client_2) = receptor.next_payload().await.unwrap();

    assert!(client_1 != client_2);

    assert_eq!(client_1, client_1.clone());
}

// Tests messenger creation and address space collision.
#[fuchsia_async::run_until_stalled(test)]
async fn test_messenger_creation() {
    let delegate = num_test::MessageHub::create_hub();
    let address = 1;

    let messenger_1_result = delegate.create(MessengerType::Addressable(address)).await;
    assert!(messenger_1_result.is_ok());

    assert!(delegate.create(MessengerType::Addressable(address)).await.is_err());
}

// Tests whether the client is reported as present after being created.
#[fuchsia_async::run_until_stalled(test)]
async fn test_messenger_presence() {
    let delegate = num_test::MessageHub::create_hub();

    // Create unbound messenger
    let (_, receptor) =
        delegate.create(MessengerType::Unbound).await.expect("messenger should be created");

    // Check for messenger's presence
    assert!(delegate.contains(receptor.get_signature()).await.expect("check should complete"));

    // Check for an address that shouldn't exist
    #[allow(clippy::bool_assert_comparison)]
    {
        assert_eq!(
            delegate
                .contains(<num_test::MessageHub as MessageHubUtil>::Signature::Address(1))
                .await
                .expect("check should complete"),
            false
        );
    }
}

// Tests messenger creation and address space collision.
#[fuchsia_async::run_until_stalled(test)]
async fn test_messenger_deletion() {
    let delegate = num_test::MessageHub::create_hub();
    let address = 1;

    {
        let (_, _) = delegate.create(MessengerType::Addressable(address)).await.unwrap();

        // By the time this subsequent create happens, the previous messenger and
        // receptor belonging to this address should have gone out of scope and
        // freed up the address space.
        assert!(delegate.create(MessengerType::Addressable(address)).await.is_ok());
    }

    {
        // Holding onto the MessengerClient should prevent deletion.
        let (_messenger_client, _) =
            delegate.create(MessengerType::Addressable(address)).await.unwrap();
        assert!(delegate.create(MessengerType::Addressable(address)).await.is_err());
    }

    {
        // Holding onto the Receptor should prevent deletion.
        let (_, _receptor) = delegate.create(MessengerType::Addressable(address)).await.unwrap();
        assert!(delegate.create(MessengerType::Addressable(address)).await.is_err());
    }
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_messenger_deletion_with_fingerprint() {
    let delegate = num_test::MessageHub::create_hub();
    let address = 1;
    let (_, mut receptor) =
        delegate.create(MessengerType::Addressable(address)).await.expect("should get receptor");
    delegate.delete(receptor.get_signature());
    assert!(receptor.next().await.is_none());
}

// Tests basic functionality of the MessageHub, ensuring messages and replies
// are properly delivered.
#[fuchsia_async::run_until_stalled(test)]
async fn test_end_to_end_messaging() {
    let delegate = test::MessageHub::create_hub();

    let (messenger_client_1, _) =
        delegate.create(MessengerType::Addressable(TestAddress::Foo(1))).await.unwrap();
    let (_, mut receptor_2) =
        delegate.create(MessengerType::Addressable(TestAddress::Foo(2))).await.unwrap();

    let mut reply_receptor =
        messenger_client_1.message(ORIGINAL, Audience::Address(TestAddress::Foo(2))).send();

    verify_payload(
        ORIGINAL,
        &mut receptor_2,
        Some(Box::new(|client| -> BoxFuture<'_, ()> {
            Box::pin(async move {
                let _ = client.reply(REPLY).send();
            })
        })),
    )
    .await;

    verify_payload(REPLY, &mut reply_receptor, None).await;
}

// Tests forwarding behavior, making sure a message is forwarded in the case
// the client does nothing with it.
#[fuchsia_async::run_until_stalled(test)]
async fn test_implicit_forward() {
    let delegate = test::MessageHub::create_hub();

    let (messenger_client_1, _) =
        delegate.create(MessengerType::Addressable(TestAddress::Foo(1))).await.unwrap();
    let (_, mut receiver_2) = delegate.create(MessengerType::Broker(None)).await.unwrap();
    let (_, mut receiver_3) =
        delegate.create(MessengerType::Addressable(TestAddress::Foo(3))).await.unwrap();

    let mut reply_receptor =
        messenger_client_1.message(ORIGINAL, Audience::Address(TestAddress::Foo(3))).send();

    // Ensure observer gets payload and then do nothing with message.
    verify_payload(ORIGINAL, &mut receiver_2, None).await;

    verify_payload(
        ORIGINAL,
        &mut receiver_3,
        Some(Box::new(|client| -> BoxFuture<'_, ()> {
            Box::pin(async move {
                let _ = client.reply(REPLY).send();
            })
        })),
    )
    .await;

    // Ensure observer gets payload and then do nothing with message.
    verify_payload(REPLY, &mut receiver_2, None).await;

    verify_payload(REPLY, &mut reply_receptor, None).await;
}

// Exercises the observation functionality. Makes sure a broker who has
// indicated they would like to participate in a message path receives the
// reply.
#[fuchsia_async::run_until_stalled(test)]
async fn test_observe_addressable() {
    let delegate = test::MessageHub::create_hub();

    let (messenger_client_1, _) =
        delegate.create(MessengerType::Addressable(TestAddress::Foo(1))).await.unwrap();
    let (_, mut receptor_2) = delegate.create(MessengerType::Broker(None)).await.unwrap();
    let (_, mut receptor_3) =
        delegate.create(MessengerType::Addressable(TestAddress::Foo(3))).await.unwrap();

    let mut reply_receptor =
        messenger_client_1.message(ORIGINAL, Audience::Address(TestAddress::Foo(3))).send();

    let observe_receptor = Arc::new(Mutex::new(None));
    verify_payload(ORIGINAL, &mut receptor_2, {
        let observe_receptor = observe_receptor.clone();
        Some(Box::new(move |mut client| -> BoxFuture<'_, ()> {
            Box::pin(async move {
                let mut receptor = observe_receptor.lock().await;
                *receptor = Some(client.spawn_observer());
            })
        }))
    })
    .await;

    verify_payload(
        ORIGINAL,
        &mut receptor_3,
        Some(Box::new(|client| -> BoxFuture<'_, ()> {
            Box::pin(async move {
                let _ = client.reply(REPLY).send();
            })
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

// Validates that timeout status is reached when there is no response
#[test]
fn test_timeout() {
    let mut executor =
        fuchsia_async::TestExecutor::new_with_fake_time().expect("Failed to create executor");
    let timeout_ms = 1000;

    let fut = async move {
        let delegate = test::MessageHub::create_hub();
        let (messenger_client_1, _) =
            delegate.create(MessengerType::Addressable(TestAddress::Foo(1))).await.unwrap();
        let (_, mut receptor_2) =
            delegate.create(MessengerType::Addressable(TestAddress::Foo(2))).await.unwrap();

        let mut reply_receptor = messenger_client_1
            .message(ORIGINAL, Audience::Address(TestAddress::Foo(2)))
            .set_timeout(Some(timeout_ms.millis()))
            .send();

        verify_payload(
            ORIGINAL,
            &mut receptor_2,
            Some(Box::new(|_| -> BoxFuture<'_, ()> {
                Box::pin(async move {
                    // Do not respond.
                })
            })),
        )
        .await;

        verify_result(Status::Timeout, &mut reply_receptor).await;
    };

    pin_utils::pin_mut!(fut);
    let _result = loop {
        executor.wake_main_future();
        let new_time = fuchsia_async::Time::from_nanos(
            executor.now().into_nanos()
                + fuchsia_zircon::Duration::from_millis(timeout_ms).into_nanos(),
        );
        match executor.run_one_step(&mut fut) {
            Some(Poll::Ready(x)) => break x,
            None => panic!("Executor stalled"),
            Some(Poll::Pending) => {
                executor.set_fake_time(new_time);
            }
        }
    };
}

// Tests the broadcast functionality. Ensures all non-sending, addressable
// messengers receive a broadcast message.
#[fuchsia_async::run_until_stalled(test)]
async fn test_broadcast() {
    let delegate = test::MessageHub::create_hub();

    let (messenger_client_1, _) =
        delegate.create(MessengerType::Addressable(TestAddress::Foo(1))).await.unwrap();
    let (_, mut receptor_2) =
        delegate.create(MessengerType::Addressable(TestAddress::Foo(2))).await.unwrap();
    let (_, mut receptor_3) =
        delegate.create(MessengerType::Addressable(TestAddress::Foo(3))).await.unwrap();

    let _ = messenger_client_1.message(ORIGINAL, Audience::Broadcast).send();

    verify_payload(ORIGINAL, &mut receptor_2, None).await;
    verify_payload(ORIGINAL, &mut receptor_3, None).await;
}

// Verifies delivery statuses are properly relayed back to the original sender.
#[fuchsia_async::run_until_stalled(test)]
async fn test_delivery_status() {
    let delegate = test::MessageHub::create_hub();
    let known_receiver_address = TestAddress::Foo(2);
    let unknown_address = TestAddress::Foo(3);
    let (messenger_client_1, _) =
        delegate.create(MessengerType::Addressable(TestAddress::Foo(1))).await.unwrap();
    let (_, mut receptor_2) =
        delegate.create(MessengerType::Addressable(known_receiver_address)).await.unwrap();

    {
        let mut receptor =
            messenger_client_1.message(ORIGINAL, Audience::Address(known_receiver_address)).send();

        // Ensure observer gets payload and then do nothing with message.
        verify_payload(ORIGINAL, &mut receptor_2, None).await;

        verify_result(Status::Received, &mut receptor).await;
    }

    {
        let mut receptor =
            messenger_client_1.message(ORIGINAL, Audience::Address(unknown_address)).send();

        verify_result(Status::Undeliverable, &mut receptor).await;
    }
}

// Verifies message is delivered even if messenger is deleted right
// after.
#[fuchsia_async::run_until_stalled(test)]
async fn test_send_delete() {
    let delegate = test::MessageHub::create_hub();
    let (_, mut receptor_2) = delegate
        .create(MessengerType::Addressable(TestAddress::Foo(2)))
        .await
        .expect("client should be created");
    {
        let (messenger_client_1, _) =
            delegate.create(MessengerType::Unbound).await.expect("client should be created");
        messenger_client_1.message(ORIGINAL, Audience::Broadcast).send().ack();
    }

    // Ensure observer gets payload and then do nothing with message.
    verify_payload(ORIGINAL, &mut receptor_2, None).await;
}

// Verifies beacon returns error when receptor goes out of scope.
#[fuchsia_async::run_until_stalled(test)]
async fn test_beacon_error() {
    let delegate = test::MessageHub::create_hub();

    let (messenger_client, _) =
        delegate.create(MessengerType::Addressable(TestAddress::Foo(1))).await.unwrap();
    {
        let (_, mut receptor) =
            delegate.create(MessengerType::Addressable(TestAddress::Foo(2))).await.unwrap();

        verify_result(
            Status::Received,
            &mut messenger_client.message(ORIGINAL, Audience::Address(TestAddress::Foo(2))).send(),
        )
        .await;
        verify_payload(ORIGINAL, &mut receptor, None).await;
    }

    verify_result(
        Status::Undeliverable,
        &mut messenger_client.message(ORIGINAL, Audience::Address(TestAddress::Foo(2))).send(),
    )
    .await;
}

// Verifies Acknowledge is fully passed back.
#[fuchsia_async::run_until_stalled(test)]
async fn test_acknowledge() {
    let delegate = test::MessageHub::create_hub();

    let (_, mut receptor) =
        delegate.create(MessengerType::Addressable(TestAddress::Foo(1))).await.unwrap();

    let (messenger, _) = delegate.create(MessengerType::Unbound).await.unwrap();

    let mut message_receptor =
        messenger.message(ORIGINAL, Audience::Address(TestAddress::Foo(1))).send();

    verify_payload(ORIGINAL, &mut receptor, None).await;

    assert!(message_receptor.wait_for_acknowledge().await.is_ok());
}

// Verifies observers can participate in messaging.
#[fuchsia_async::run_until_stalled(test)]
async fn test_messenger_behavior() {
    // Run tests twice to ensure no one instance leads to a deadlock.
    for _ in 0..2 {
        verify_messenger_behavior(MessengerType::Broker(None)).await;
        verify_messenger_behavior(MessengerType::Unbound).await;
        verify_messenger_behavior(MessengerType::Addressable(TestAddress::Foo(2))).await;
    }
}

async fn verify_messenger_behavior(
    messenger_type: MessengerType<TestMessage, TestAddress, TestRole>,
) {
    let delegate = test::MessageHub::create_hub();

    // Messenger to receive message.
    let (target_client, mut target_receptor) =
        delegate.create(MessengerType::Addressable(TestAddress::Foo(1))).await.unwrap();

    // Author Messenger.
    let (test_client, mut test_receptor) = delegate.create(messenger_type).await.unwrap();

    // Send top level message from the Messenger.
    let mut reply_receptor =
        test_client.message(ORIGINAL, Audience::Address(TestAddress::Foo(1))).send();

    let captured_signature = Arc::new(Mutex::new(None));

    // Verify target messenger received message and capture Signature.
    verify_payload(ORIGINAL, &mut target_receptor, {
        let captured_signature = captured_signature.clone();
        Some(Box::new(move |client| -> BoxFuture<'_, ()> {
            Box::pin(async move {
                let mut author = captured_signature.lock().await;
                *author = Some(client.get_author());
                client.reply(REPLY).send().ack();
            })
        }))
    })
    .await;

    // Verify messenger received reply on the message receptor.
    verify_payload(REPLY, &mut reply_receptor, None).await;

    let messenger_signature =
        captured_signature.lock().await.take().expect("signature should be populated");

    // Send top level message to Messenger.
    target_client.message(ORIGINAL, Audience::Messenger(messenger_signature)).send().ack();

    // Verify Messenger received message.
    verify_payload(ORIGINAL, &mut test_receptor, None).await;
}

// Ensures unbound messengers operate properly
#[fuchsia_async::run_until_stalled(test)]
async fn test_unbound_messenger() {
    let delegate = test::MessageHub::create_hub();

    let (unbound_messenger_1, _) = delegate.create(MessengerType::Unbound).await.unwrap();

    let (_, mut unbound_receptor) =
        delegate.create(MessengerType::Unbound).await.expect("messenger should be created");

    let mut reply_receptor = unbound_messenger_1
        .message(ORIGINAL, Audience::Messenger(unbound_receptor.get_signature()))
        .send();

    // Verify target messenger received message and send response.
    verify_payload(
        ORIGINAL,
        &mut unbound_receptor,
        Some(Box::new(move |client| -> BoxFuture<'_, ()> {
            Box::pin(async move {
                client.reply(REPLY).send().ack();
            })
        })),
    )
    .await;

    verify_payload(REPLY, &mut reply_receptor, None).await;
}

// Ensures next_payload returns the correct values.
#[fuchsia_async::run_until_stalled(test)]
async fn test_next_payload() {
    let delegate = test::MessageHub::create_hub();
    let (unbound_messenger_1, _) = delegate.create(MessengerType::Unbound).await.unwrap();

    let (_, mut unbound_receptor_2) =
        delegate.create(MessengerType::Unbound).await.expect("should create messenger");

    unbound_messenger_1
        .message(ORIGINAL, Audience::Messenger(unbound_receptor_2.get_signature()))
        .send()
        .ack();

    let receptor_result = unbound_receptor_2.next_payload().await;

    let (payload, _) = receptor_result.unwrap();
    assert_eq!(payload, ORIGINAL);

    {
        let mut receptor =
            unbound_messenger_1.message(REPLY, Audience::Address(TestAddress::Foo(1))).send();
        // Should return an error
        let receptor_result = receptor.next_payload().await;
        assert!(receptor_result.is_err());
    }
}

// Exercises basic action fuse behavior.
#[fuchsia_async::run_until_stalled(test)]
async fn test_action_fuse() {
    // Channel to send the message from the fuse.
    let (tx, mut rx) = futures::channel::mpsc::unbounded::<()>();

    {
        let _ = ActionFuseBuilder::new()
            .add_action(Box::new(move || {
                tx.unbounded_send(()).unwrap();
            }))
            .build();
    }

    assert!(rx.next().await.is_some());
}

// Exercises chained action fuse behavior
#[fuchsia_async::run_until_stalled(test)]
async fn test_chained_action_fuse() {
    // Channel to send the message from the fuse.
    let (tx, mut rx) = futures::channel::mpsc::unbounded::<()>();
    let (tx2, mut rx2) = futures::channel::mpsc::unbounded::<()>();

    {
        let _ = ActionFuseBuilder::new()
            .add_action(Box::new(move || {
                tx.unbounded_send(()).unwrap();
            }))
            .chain_fuse(
                ActionFuseBuilder::new()
                    .add_action(Box::new(move || {
                        tx2.unbounded_send(()).unwrap();
                    }))
                    .build(),
            )
            .build();
    }

    // Root should fire first
    assert!(rx.next().await.is_some());

    // Then chain reaction
    assert!(rx2.next().await.is_some());
}

// Exercises timestamp value.
#[fuchsia_async::run_until_stalled(test)]
async fn test_message_timestamp() {
    let delegate = test::MessageHub::create_hub();

    let (messenger, _) = delegate.create(MessengerType::Unbound).await.unwrap();
    let (_, mut receptor) = delegate.create(MessengerType::Unbound).await.unwrap();

    let init_time = now();
    messenger.message(ORIGINAL, Audience::Broadcast).send().ack();
    let post_send_time = now();

    while let Some(message_event) = receptor.next().await {
        if let MessageEvent::Message(incoming_payload, client) = message_event {
            assert_eq!(ORIGINAL, incoming_payload);
            let current_time = now();
            let send_time = client.get_timestamp();
            // Ensures the event timestamp was not taken before the event
            assert!(init_time <= send_time);
            // Compared against time right after message was sent to ensure that
            // timestamp was from the actual send time and not from when the
            // message was posted in the message hub.
            assert!(send_time <= post_send_time);
            // Make sure the time stamp was captured before the request for it.
            assert!(post_send_time <= current_time);
            return;
        } else {
            panic!("Should have received the broadcast first");
        }
    }
}

// Verifies that the proper signal is fired when a receptor disappears.
#[fuchsia_async::run_until_stalled(test)]
async fn test_bind_to_recipient() {
    let delegate = test::MessageHub::create_hub();
    let (tx, mut rx) = futures::channel::mpsc::unbounded::<()>();

    let (_, mut receptor) =
        delegate.create(MessengerType::Unbound).await.expect("should create messenger");

    {
        let (scoped_messenger, _scoped_receptor) =
            delegate.create(MessengerType::Unbound).await.unwrap();
        scoped_messenger
            .message(ORIGINAL, Audience::Messenger(receptor.get_signature()))
            .send()
            .ack();

        if let Some(MessageEvent::Message(payload, mut client)) = receptor.next().await {
            assert_eq!(payload, ORIGINAL);
            client
                .bind_to_recipient(
                    ActionFuseBuilder::new()
                        .add_action(Box::new(move || {
                            tx.unbounded_send(()).unwrap();
                        }))
                        .build(),
                )
                .await;
        } else {
            panic!("Should have received message");
        }
    }

    // Receptor has fallen out of scope, should receive callback.
    assert!(rx.next().await.is_some());
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_reply_propagation() {
    let delegate = test::MessageHub::create_hub();

    // Create messenger to send source message.
    let (sending_messenger, _) =
        delegate.create(MessengerType::Unbound).await.expect("sending messenger should be created");

    // Create broker to propagate a derived message.
    let (_, mut broker) = delegate
        .create(MessengerType::Broker(Some(filter::Builder::single(filter::Condition::Custom(
            Arc::new(move |message| *message.payload() == REPLY),
        )))))
        .await
        .expect("broker should be created");

    // Create messenger to be target of source message.
    let (_, mut target_receptor) =
        delegate.create(MessengerType::Unbound).await.expect("target messenger should be created");

    // Send top level message.
    let mut result_receptor = sending_messenger
        .message(ORIGINAL, Audience::Messenger(target_receptor.get_signature()))
        .send();

    // Ensure target receives message and reply back.
    verify_payload(
        ORIGINAL,
        &mut target_receptor,
        Some(Box::new(move |client| -> BoxFuture<'_, ()> {
            Box::pin(async move {
                client.reply(REPLY).send().ack();
            })
        })),
    )
    .await;

    // Ensure broker receives reply and propagate modified message.
    verify_payload(
        REPLY,
        &mut broker,
        Some(Box::new(move |client| -> BoxFuture<'_, ()> {
            Box::pin(async move {
                client.propagate(MODIFIED).send().ack();
            })
        })),
    )
    .await;

    // Ensure original sender gets reply.
    verify_payload(MODIFIED, &mut result_receptor, None).await;
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_propagation() {
    let delegate = test::MessageHub::create_hub();

    // Create messenger to send source message.
    let (sending_messenger, sending_receptor) =
        delegate.create(MessengerType::Unbound).await.expect("sending messenger should be created");
    let sending_signature = sending_receptor.get_signature();

    // Create brokers to propagate a derived message.
    let (_, mut broker_1) =
        delegate.create(MessengerType::Broker(None)).await.expect("broker should be created");
    let modifier_1_signature = broker_1.get_signature();

    let (_, mut broker_2) =
        delegate.create(MessengerType::Broker(None)).await.expect("broker should be created");
    let modifier_2_signature = broker_2.get_signature();

    // Create messenger to be target of source message.
    let (_, mut target_receptor) =
        delegate.create(MessengerType::Unbound).await.expect("target messenger should be created");

    // Send top level message.
    let mut result_receptor = sending_messenger
        .message(ORIGINAL, Audience::Messenger(target_receptor.get_signature()))
        .send();

    // Ensure broker 1 receives original message and propagate modified message.
    verify_payload(
        ORIGINAL,
        &mut broker_1,
        Some(Box::new(move |client| -> BoxFuture<'_, ()> {
            Box::pin(async move {
                client.propagate(MODIFIED).send().ack();
            })
        })),
    )
    .await;

    // Ensure broker 2 receives modified message and propagates a differen
    // modified message.
    verify_payload(
        MODIFIED,
        &mut broker_2,
        Some(Box::new(move |client| -> BoxFuture<'_, ()> {
            Box::pin(async move {
                client.propagate(MODIFIED_2).send().ack();
            })
        })),
    )
    .await;

    // Ensure target receives message and reply back.
    verify_payload(
        MODIFIED_2,
        &mut target_receptor,
        Some(Box::new(move |client| -> BoxFuture<'_, ()> {
            Box::pin(async move {
                // ensure the original author is attributed to the message.
                assert_eq!(client.get_author(), sending_signature);
                // ensure the modifiers are present.
                assert!(client.get_modifiers().contains(&modifier_1_signature));
                assert!(client.get_modifiers().contains(&modifier_2_signature));
                // ensure the message author has not been modified.
                client.reply(REPLY).send().ack();
            })
        })),
    )
    .await;

    // Ensure original sender gets reply.
    verify_payload(REPLY, &mut result_receptor, None).await;
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_broker_filter_audience_broadcast() {
    // Prepare a message hub with a sender, broker, and target.
    let delegate = test::MessageHub::create_hub();

    // Messenger to send broadcast message and targeted message.
    let (messenger, _) = delegate
        .create(MessengerType::Unbound)
        .await
        .expect("broadcast messenger should be created");
    // Receptor to receive both broadcast and targeted messages.
    let (_, mut receptor) =
        delegate.create(MessengerType::Unbound).await.expect("target receptor should be created");
    // Filter to target only broadcasts.
    let filter = filter::Builder::single(filter::Condition::Audience(Audience::Broadcast));
    // Broker to receive broadcast. It should not receive targeted messages.
    let (_, mut broker_receptor) = delegate
        .create(MessengerType::Broker(Some(filter)))
        .await
        .expect("broker should be created");

    // Send targeted message.
    messenger.message(ORIGINAL, Audience::Messenger(receptor.get_signature())).send().ack();
    // Verify receptor gets message.
    verify_payload(ORIGINAL, &mut receptor, None).await;

    // Broadcast message.
    messenger.message(BROADCAST, Audience::Broadcast).send().ack();
    // Ensure broker gets broadcast. If the targeted message was received, this
    // will fail.
    verify_payload(BROADCAST, &mut broker_receptor, None).await;
    // Ensure receptor gets broadcast.
    verify_payload(BROADCAST, &mut receptor, None).await;
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_broker_filter_audience_messenger() {
    // Prepare a message hub with a sender, broker, and target.
    let delegate = test::MessageHub::create_hub();

    // Messenger to send broadcast message and targeted message.
    let (messenger, _) = delegate
        .create(MessengerType::Unbound)
        .await
        .expect("broadcast messenger should be created");
    // Receptor to receive both broadcast and targeted messages.
    let (_, mut receptor) =
        delegate.create(MessengerType::Unbound).await.expect("target messenger should be created");
    // Filter to target only messenger.
    let filter = filter::Builder::single(filter::Condition::Audience(Audience::Messenger(
        receptor.get_signature(),
    )));
    // Broker that should only target messages for a given messenger.
    let (_, mut broker_receptor) = delegate
        .create(MessengerType::Broker(Some(filter)))
        .await
        .expect("broker should be created");

    // Send broadcast message.
    messenger.message(BROADCAST, Audience::Broadcast).send().ack();
    // Verify receptor gets message.
    verify_payload(BROADCAST, &mut receptor, None).await;

    // Send targeted message.
    messenger.message(ORIGINAL, Audience::Messenger(receptor.get_signature())).send().ack();
    // Ensure broker gets message. If the broadcast message was received, this
    // will fail.
    verify_payload(ORIGINAL, &mut broker_receptor, None).await;
    // Ensure receptor gets broadcast.
    verify_payload(ORIGINAL, &mut receptor, None).await;
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_broker_filter_audience_address() {
    // Prepare a message hub with a sender, broker, and target.
    let delegate = test::MessageHub::create_hub();

    // Messenger to send broadcast message and targeted message.
    let (messenger, _) = delegate
        .create(MessengerType::Unbound)
        .await
        .expect("broadcast messenger should be created");
    // Receptor to receive both broadcast and targeted messages.
    let target_address = TestAddress::Foo(2);
    let (_, mut receptor) = delegate
        .create(MessengerType::Addressable(target_address))
        .await
        .expect("target receptor should be created");
    // Filter to target only messenger.
    let filter =
        filter::Builder::single(filter::Condition::Audience(Audience::Address(target_address)));
    // Broker that should only target messages for a given messenger.
    let (_, mut broker_receptor) = delegate
        .create(MessengerType::Broker(Some(filter)))
        .await
        .expect("broker should be created");

    // Send broadcast message.
    messenger.message(BROADCAST, Audience::Broadcast).send().ack();
    // Verify receptor gets message.
    verify_payload(BROADCAST, &mut receptor, None).await;

    // Send targeted message.
    messenger.message(ORIGINAL, Audience::Address(target_address)).send().ack();
    // Ensure broker gets message. If the broadcast message was received, this
    // will fail.
    verify_payload(ORIGINAL, &mut broker_receptor, None).await;
    // Ensure receptor gets broadcast.
    verify_payload(ORIGINAL, &mut receptor, None).await;
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_broker_filter_author() {
    // Prepare a message hub with a sender, broker, and target.
    let delegate = test::MessageHub::create_hub();

    // Messenger to send targeted message.
    let author_address = TestAddress::Foo(1);
    let (messenger, _) = delegate
        .create(MessengerType::Addressable(author_address))
        .await
        .expect("messenger should be created");

    // Receptor to receive targeted message.
    let target_address = TestAddress::Foo(2);
    let (_, mut receptor) = delegate
        .create(MessengerType::Addressable(target_address))
        .await
        .expect("target receptor should be created");

    // Filter to target only messages with a particular author.
    let filter = filter::Builder::single(filter::Condition::Author(
        <test::MessageHub as MessageHubUtil>::Signature::Address(author_address),
    ));

    // Broker that should only target messages for a given author.
    let (_, mut broker_receptor) = delegate
        .create(MessengerType::Broker(Some(filter)))
        .await
        .expect("broker should be created");

    // Send targeted message.
    messenger.message(ORIGINAL, Audience::Address(target_address)).send().ack();
    // Ensure broker gets message.
    verify_payload(ORIGINAL, &mut broker_receptor, None).await;
    // Ensure receptor gets message.
    verify_payload(ORIGINAL, &mut receptor, None).await;
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_broker_filter_custom() {
    // Prepare a message hub with a sender, broker, and target.
    let delegate = test::MessageHub::create_hub();

    // Messenger to send broadcast message and targeted message.
    let (messenger, _) = delegate
        .create(MessengerType::Unbound)
        .await
        .expect("broadcast messenger should be created");
    // Filter to target only the ORIGINAL message.
    let filter = filter::Builder::single(filter::Condition::Custom(Arc::new(|message| {
        *message.payload() == ORIGINAL
    })));
    // Broker that should only target ORIGINAL messages.
    let (_, mut broker_receptor) = delegate
        .create(MessengerType::Broker(Some(filter)))
        .await
        .expect("broker should be created");

    // Send broadcast message.
    messenger.message(BROADCAST, Audience::Broadcast).send().ack();

    // Send original message.
    messenger.message(ORIGINAL, Audience::Broadcast).send().ack();
    // Ensure broker gets message. If the broadcast message was received, this
    // will fail.
    verify_payload(ORIGINAL, &mut broker_receptor, None).await;
}

// Verify that using a closure that captures a variable for a custom filter works, since it can't
// be used in place of an function pointer.
#[fuchsia_async::run_until_stalled(test)]
async fn test_broker_filter_caputring_closure() {
    // Prepare a message hub with a sender, broker, and target.
    let delegate = test::MessageHub::create_hub();

    // Messenger to send broadcast message and targeted message.
    let (messenger, _) = delegate
        .create(MessengerType::Unbound)
        .await
        .expect("broadcast messenger should be created");
    // Filter to target only the Foo message.
    let expected_payload = TestMessage::Foo;
    let filter = filter::Builder::single(filter::Condition::Custom(Arc::new(move |message| {
        *message.payload() == expected_payload
    })));
    // Broker that should only target Foo messages.
    let (_, mut broker_receptor) = delegate
        .create(MessengerType::Broker(Some(filter)))
        .await
        .expect("broker should be created");

    // Send broadcast message.
    messenger.message(BROADCAST, Audience::Broadcast).send().ack();

    // Send foo message.
    messenger.message(expected_payload, Audience::Broadcast).send().ack();
    // Ensure broker gets message. If the broadcast message was received, this
    // will fail.
    verify_payload(expected_payload, &mut broker_receptor, None).await;
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_broker_filter_combined_any() {
    // Prepare a message hub with a sender, broker, and target.
    let delegate = test::MessageHub::create_hub();

    // Messenger to send broadcast message and targeted message.
    let (messenger, _) = delegate
        .create(MessengerType::Unbound)
        .await
        .expect("broadcast messenger should be created");
    // Receptor for messages.
    let target_address = TestAddress::Foo(2);
    let (_, mut receptor) = delegate
        .create(MessengerType::Addressable(target_address))
        .await
        .expect("addressable messenger should be created");

    // Filter to target only the ORIGINAL message.
    let filter = filter::Builder::new(
        filter::Condition::Custom(Arc::new(|message| *message.payload() == ORIGINAL)),
        filter::Conjugation::Any,
    )
    .append(filter::Condition::Filter(filter::Builder::single(filter::Condition::Audience(
        Audience::Broadcast,
    ))))
    .build();

    // Broker that should only target ORIGINAL messages and broadcast audiences.
    let (_, mut broker_receptor) = delegate
        .create(MessengerType::Broker(Some(filter)))
        .await
        .expect("broker should be created");

    // Send broadcast message.
    messenger.message(BROADCAST, Audience::Broadcast).send().ack();
    // Receptor should receive match based on broadcast audience
    verify_payload(BROADCAST, &mut broker_receptor, None).await;
    // Other receptors should receive the broadcast as well.
    verify_payload(BROADCAST, &mut receptor, None).await;

    // Send original message to target.
    messenger.message(ORIGINAL, Audience::Address(target_address)).send().ack();
    // Ensure broker gets message.
    verify_payload(ORIGINAL, &mut broker_receptor, None).await;
    // Ensure target gets message as well.
    verify_payload(ORIGINAL, &mut receptor, None).await;
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_broker_filter_combined_all() {
    // Prepare a message hub with a sender, broker, and target.
    let delegate = test::MessageHub::create_hub();

    // Messenger to send broadcast message and targeted message.
    let (messenger, _) =
        delegate.create(MessengerType::Unbound).await.expect("sending messenger should be created");
    // Receptor for messages.
    let target_address = TestAddress::Foo(2);
    let (_, mut receptor) = delegate
        .create(MessengerType::Addressable(target_address))
        .await
        .expect("receiving messenger should be created");

    // Filter to target only the ORIGINAL message.
    let filter = filter::Builder::new(
        filter::Condition::Custom(Arc::new(|message| *message.payload() == ORIGINAL)),
        filter::Conjugation::All,
    )
    .append(filter::Condition::Filter(filter::Builder::single(filter::Condition::Audience(
        Audience::Address(target_address),
    ))))
    .build();

    // Broker that should only target ORIGINAL messages and broadcast audiences.
    let (_, mut broker_receptor) = delegate
        .create(MessengerType::Broker(Some(filter)))
        .await
        .expect("broker should be created");

    // Send REPLY message. Should not match broker since content does not match.
    messenger.message(REPLY, Audience::Address(target_address)).send().ack();
    // Other receptors should receive the broadcast as well.
    verify_payload(REPLY, &mut receptor, None).await;

    // Send ORIGINAL message to target.
    messenger.message(ORIGINAL, Audience::Address(target_address)).send().ack();
    // Ensure broker gets message.
    verify_payload(ORIGINAL, &mut broker_receptor, None).await;
    // Ensure target gets message as well.
    verify_payload(ORIGINAL, &mut receptor, None).await;
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_group_message() {
    // Prepare a message hub with a sender and multiple targets.
    let delegate = test::MessageHub::create_hub();

    // Messenger to send message.
    let (messenger, _) = delegate.create(MessengerType::Unbound).await.unwrap();
    // Receptors for messages.
    let target_address_1 = TestAddress::Foo(1);
    let (_, mut receptor_1) =
        delegate.create(MessengerType::Addressable(target_address_1)).await.unwrap();
    let target_address_2 = TestAddress::Foo(2);
    let (_, mut receptor_2) =
        delegate.create(MessengerType::Addressable(target_address_2)).await.unwrap();
    let (_, mut receptor_3) = delegate.create(MessengerType::Unbound).await.unwrap();

    let audience = Audience::Group(
        group::Builder::new()
            .add(Audience::Address(target_address_1))
            .add(Audience::Address(target_address_2))
            .build(),
    );
    // Send message targeting both receptors.
    messenger.message(ORIGINAL, audience).send().ack();
    // Receptors should both receive the message.
    verify_payload(ORIGINAL, &mut receptor_1, None).await;
    verify_payload(ORIGINAL, &mut receptor_2, None).await;

    // Broadcast and ensure the untargeted receptor gets that message next
    messenger.message(BROADCAST, Audience::Broadcast).send().ack();
    verify_payload(BROADCAST, &mut receptor_3, None).await;
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_group_message_redundant_targets() {
    // Prepare a message hub with a sender, broker, and target.
    let delegate = test::MessageHub::create_hub();

    // Messenger to send broadcast message and targeted message.
    let (messenger, _) = delegate.create(MessengerType::Unbound).await.unwrap();
    // Receptors for messages.
    let target_address = TestAddress::Foo(1);
    let (_, mut receptor) = delegate
        .create(MessengerType::Addressable(target_address))
        .await
        .expect("messenger should be created");
    // Create audience with multiple references to same messenger.
    let audience = Audience::Group(
        group::Builder::new()
            .add(Audience::Address(target_address))
            .add(Audience::Messenger(receptor.get_signature()))
            .add(Audience::Broadcast)
            .build(),
    );

    // Send Original message.
    messenger.message(ORIGINAL, audience.clone()).send().ack();
    // Receptor should receive message.
    verify_payload(ORIGINAL, &mut receptor, None).await;

    // Send Reply message.
    messenger.message(REPLY, audience).send().ack();
    // Receptor should receive Reply message and not another Original message.
    verify_payload(REPLY, &mut receptor, None).await;
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_audience_matching() {
    let target_audience: Audience<TestAddress> = Audience::Address(TestAddress::Foo(1));
    // An audience should contain itself.
    assert!(target_audience.contains(&target_audience));
    // An audience with only broadcast should not match.
    #[allow(clippy::bool_assert_comparison)]
    {
        let audience = Audience::Group(group::Builder::new().add(Audience::Broadcast).build());
        assert_eq!(audience.contains(&target_audience), false);
    }
    // An audience group with the target audience should match.
    {
        let audience = Audience::Group(group::Builder::new().add(target_audience.clone()).build());
        assert!(audience.contains(&target_audience));
    }
    // An audience group with the target audience nested should match.
    {
        let audience = Audience::Group(
            group::Builder::new()
                .add(Audience::Group(group::Builder::new().add(target_audience.clone()).build()))
                .build(),
        );
        assert!(audience.contains(&target_audience));
    }
    // An a subset should be contained within a superset and a superset should
    // not be contained in a subset.
    {
        let target_audience_2 = Audience::Address(TestAddress::Foo(2));
        let target_audience_3 = Audience::Address(TestAddress::Foo(3));

        let audience_subset = Audience::Group(
            group::Builder::new()
                .add(target_audience.clone())
                .add(target_audience_2.clone())
                .build(),
        );

        let audience_set = Audience::Group(
            group::Builder::new()
                .add(target_audience)
                .add(target_audience_2)
                .add(target_audience_3)
                .build(),
        );

        assert!(audience_set.contains(&audience_subset));

        #[allow(clippy::bool_assert_comparison)]
        {
            assert_eq!(audience_subset.contains(&audience_set), false);
        }
    }
}

// Ensures all members of a role receive messages.
#[fuchsia_async::run_until_stalled(test)]
async fn test_roles_membership() {
    // Prepare a message hub.
    let delegate = test::MessageHub::create_hub();

    // Create messengers who participate in roles
    let (_, mut foo_role_receptor) = delegate
        .messenger_builder(MessengerType::Unbound)
        .add_role(role::Signature::role(TestRole::Foo))
        .build()
        .await
        .expect("recipient messenger should be created");
    let (_, mut foo_role_receptor_2) = delegate
        .messenger_builder(MessengerType::Unbound)
        .add_role(role::Signature::role(TestRole::Foo))
        .build()
        .await
        .expect("recipient messenger should be created");

    // Create messenger to send a message to the given participant.
    let (sender, _) = delegate
        .messenger_builder(MessengerType::Unbound)
        .build()
        .await
        .expect("sending messenger should be created");

    let message = TestMessage::Foo;
    let audience = Audience::Role(role::Signature::role(TestRole::Foo));
    sender.message(message, audience).send().ack();

    // Verify payload received by role members.
    verify_payload(message, &mut foo_role_receptor, None).await;
    verify_payload(message, &mut foo_role_receptor_2, None).await;
}

// Ensures roles don't receive each other's messages.
#[fuchsia_async::run_until_stalled(test)]
async fn test_roles_exclusivity() {
    // Prepare a message hub.
    let delegate = test::MessageHub::create_hub();

    // Create messengers who participate in roles
    let (_, mut foo_role_receptor) = delegate
        .messenger_builder(MessengerType::Unbound)
        .add_role(role::Signature::role(TestRole::Foo))
        .build()
        .await
        .expect("recipient messenger should be created");
    let (_, mut bar_role_receptor) = delegate
        .messenger_builder(MessengerType::Unbound)
        .add_role(role::Signature::role(TestRole::Bar))
        .build()
        .await
        .expect("recipient messenger should be created");

    // Create messenger to send a message to the given participant.
    let (sender, _) = delegate
        .messenger_builder(MessengerType::Unbound)
        .build()
        .await
        .expect("sending messenger should be created");

    // Send messages to roles.
    {
        let message = TestMessage::Bar;
        let audience = Audience::Role(role::Signature::role(TestRole::Bar));
        sender.message(message, audience).send().ack();

        // Verify payload received by role members.
        verify_payload(message, &mut bar_role_receptor, None).await;
    }
    {
        let message = TestMessage::Foo;
        let audience = Audience::Role(role::Signature::role(TestRole::Foo));
        sender.message(message, audience).send().ack();

        // Verify payload received by role members.
        verify_payload(message, &mut foo_role_receptor, None).await;
    }
}

// Ensures only role members receive messages directed to the role.
#[fuchsia_async::run_until_stalled(test)]
async fn test_roles_audience() {
    // Prepare a message hub.
    let delegate = test::MessageHub::create_hub();

    // Create messenger who participate in a role
    let (_, mut foo_role_receptor) = delegate
        .messenger_builder(MessengerType::Unbound)
        .add_role(role::Signature::role(TestRole::Foo))
        .build()
        .await
        .expect("recipient messenger should be created");

    // Create another messenger with no role to ensure messages are not routed
    // improperly to other messengers.
    let (_, mut outside_receptor) = delegate
        .messenger_builder(MessengerType::Unbound)
        .build()
        .await
        .expect("other messenger should be created");
    let outside_signature = outside_receptor.get_signature();

    // Create messenger to send a message to the given participant.
    let (sender, _) = delegate
        .messenger_builder(MessengerType::Unbound)
        .build()
        .await
        .expect("sending messenger should be created");

    // Send message to role.
    {
        let message = TestMessage::Foo;
        let audience = Audience::Role(role::Signature::role(TestRole::Foo));
        sender.message(message, audience).send().ack();

        // Verify payload received by role members.
        verify_payload(message, &mut foo_role_receptor, None).await;
    }

    // Send message to outside messenger.
    {
        let message = TestMessage::Baz;
        let audience = Audience::Messenger(outside_signature);
        sender.message(message, audience).send().ack();

        // Since outside messenger isn't part of the role, the next message should
        // be the one sent directly to it, rather than the role.
        verify_payload(message, &mut outside_receptor, None).await;
    }
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_anonymous_roles() {
    // Prepare a message hub.
    let delegate = test::MessageHub::create_hub();

    // Create anonymous role.
    let role = delegate.create_role().await.expect("Role should be returned");

    // Create messenger who participates in role.
    let (_, mut role_receptor) = delegate
        .messenger_builder(MessengerType::Unbound)
        .add_role(role)
        .build()
        .await
        .expect("recipient messenger should be created");

    // Create messenger to send a message to the given participant.
    let (sender, _) = delegate
        .messenger_builder(MessengerType::Unbound)
        .build()
        .await
        .expect("sending messenger should be created");

    // Send messages to role.
    let message = TestMessage::Bar;
    let audience = Audience::Role(role);
    sender.message(message, audience).send().ack();

    // Verify payload received by role member.
    verify_payload(message, &mut role_receptor, None).await;
}

// Ensures targeted messengers deliver payload to intended audience.
#[fuchsia_async::run_until_stalled(test)]
async fn test_targeted_messenger_client() {
    let test_message = TestMessage::Foo;

    // Prepare a message hub for sender and target.
    let delegate = test::MessageHub::create_hub();

    // Create target messenger.
    let (_, mut target_receptor) = delegate
        .create(MessengerType::Unbound)
        .await
        .expect("receiving messenger should be created");

    // Create targeted messenger.
    let targeted_messenger = TargetedMessengerClient::new(
        delegate
            .create(MessengerType::Unbound)
            .await
            .expect("sending messenger should be created")
            .0,
        Audience::Messenger(target_receptor.get_signature()),
    );

    // Send message.
    targeted_messenger.message(test_message).send().ack();

    // Receptor should receive the test message.
    verify_payload(test_message, &mut target_receptor, None).await;
}
