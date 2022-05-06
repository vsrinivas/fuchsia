// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingType;
use crate::event::Publisher;
use crate::message::base::MessengerType;
use crate::message::delegate::Delegate;
use crate::message::messenger::MessengerClient;
use crate::message::receptor::Receptor;
use crate::message::MessageHubUtil;
use crate::service::{
    Address as ServiceAddress, MessageHub, Payload as ServicePayload, Role as ServiceRole,
};

use fuchsia_async::TestExecutor;
use futures::pin_mut;
use futures::task::Poll;

/// Run the provided `future` via the `executor`.
pub fn move_executor_forward(
    executor: &mut TestExecutor,
    future: impl futures::Future<Output = ()>,
    panic_msg: &str,
) {
    pin_mut!(future);
    match executor.run_until_stalled(&mut future) {
        Poll::Ready(res) => res,
        _ => panic!("{}", panic_msg),
    }
}

/// Run the provided `future` via the `executor` and return the result of the future.
pub fn move_executor_forward_and_get<T>(
    executor: &mut TestExecutor,
    future: impl futures::Future<Output = T>,
    panic_msg: &str,
) -> T {
    pin_mut!(future);
    match executor.run_until_stalled(&mut future) {
        Poll::Ready(res) => res,
        _ => panic!("{}", panic_msg),
    }
}

// Create a messenger hub, returning an unbound messenger and publisher.
pub async fn create_messenger_and_publisher(
) -> (MessengerClient<ServicePayload, ServiceAddress, ServiceRole>, Publisher) {
    let message_hub = MessageHub::create_hub();
    let publisher = Publisher::create(&message_hub, MessengerType::Unbound).await;

    let messenger =
        message_hub.create(MessengerType::Unbound).await.expect("Unable to create messenger").0;

    return (messenger, publisher);
}

// Create and return an unbound messenger and publisher from a given `message_hub`.
pub async fn create_messenger_and_publisher_from_hub(
    message_hub: &Delegate<ServicePayload, ServiceAddress, ServiceRole>,
) -> (MessengerClient<ServicePayload, ServiceAddress, ServiceRole>, Publisher) {
    let publisher = Publisher::create(message_hub, MessengerType::Unbound).await;
    let messenger =
        message_hub.create(MessengerType::Unbound).await.expect("Unable to create messenger").0;

    return (messenger, publisher);
}

// Given a `setting_type` and `message_hub`, creates a receptor from the message hub with the address
// of the setting type.
pub async fn create_receptor_for_setting_type(
    message_hub: &Delegate<ServicePayload, ServiceAddress, ServiceRole>,
    setting_type: SettingType,
) -> Receptor<ServicePayload, ServiceAddress, ServiceRole> {
    message_hub
        .create(MessengerType::Addressable(ServiceAddress::Handler(setting_type)))
        .await
        .expect("Unable to create receptor")
        .1
}
