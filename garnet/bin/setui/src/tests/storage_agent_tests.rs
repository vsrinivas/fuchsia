// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::{SettingInfo, SettingType, UnknownInfo};
use crate::handler::device_storage::testing::InMemoryStorageFactory;
use crate::handler::device_storage::{
    DeviceStorage, DeviceStorageAccess, DeviceStorageCompatible, DeviceStorageFactory,
};
use crate::handler::setting_handler::persist::UpdateState;
use crate::message::base::{Audience, MessageEvent, MessengerType, Status};
use crate::service::{self, Address};
use crate::storage::{Payload, StorageRequest, StorageResponse};
use crate::EnvironmentBuilder;
use futures::StreamExt;
use matches::assert_matches;
use std::sync::Arc;

const ENV_NAME: &str = "storage_agent_test_environment";
const ORIGINAL_VALUE: bool = true;
struct TestAccess;
impl DeviceStorageAccess for TestAccess {
    const STORAGE_KEYS: &'static [&'static str] = &[UnknownInfo::KEY];
}

async fn create_test_environment() -> (service::message::Factory, Arc<DeviceStorage>) {
    let storage_factory = Arc::new(InMemoryStorageFactory::new());
    storage_factory
        .initialize::<TestAccess>()
        .await
        .expect("Should be able to initialize unknown info");
    let env = EnvironmentBuilder::new(Arc::clone(&storage_factory))
        .settings(&[SettingType::Unknown])
        .spawn_nested(ENV_NAME)
        .await
        .unwrap();
    let store = storage_factory.get_store(0).await;
    store.write(&UnknownInfo(ORIGINAL_VALUE), true).await.expect("Write should succeed");
    (env.messenger_factory, store)
}

// Assert that we can read values by sending messages to the storage agent and receive a response.
#[fuchsia_async::run_until_stalled(test)]
async fn test_read() {
    let (messenger_factory, _) = create_test_environment().await;
    let (messenger, _) = messenger_factory
        .create(MessengerType::Unbound)
        .await
        .expect("should be able to get messenger");
    let mut receptor = messenger
        .message(
            service::Payload::Storage(Payload::Request(StorageRequest::Read(SettingType::Unknown))),
            Audience::Address(Address::Storage),
        )
        .send();

    while let Some(response) = receptor.next().await {
        match response {
            MessageEvent::Status(Status::Received) => {} // no-op
            MessageEvent::Message(
                service::Payload::Storage(Payload::Response(StorageResponse::Read(setting_info))),
                _,
            ) => {
                assert_matches!(setting_info,
                    SettingInfo::Unknown(UnknownInfo(value)) if value == ORIGINAL_VALUE);
                break;
            }
            _ => panic!("Did not receive expected response: {:?}", response),
        }
    }
}

// Assert that we can write values by sending messages to the storage agent and seeing a response
// and the value in the in memory storage.
#[fuchsia_async::run_until_stalled(test)]
async fn test_write() {
    const CHANGED_VALUE: bool = false;

    let (service_messenger_factory, store) = create_test_environment().await;

    // Validate original value before the write request.
    let UnknownInfo(value) = store.get::<UnknownInfo>().await;
    assert_eq!(ORIGINAL_VALUE, value);

    let (messenger, _) = service_messenger_factory
        .create(MessengerType::Unbound)
        .await
        .expect("should be able to get messenger");
    let mut receptor = messenger
        .message(
            service::Payload::Storage(Payload::Request(StorageRequest::Write(
                SettingInfo::Unknown(UnknownInfo(CHANGED_VALUE)),
                true,
            ))),
            Audience::Address(Address::Storage),
        )
        .send();

    while let Some(response) = receptor.next().await {
        match response {
            MessageEvent::Status(Status::Received) => {} // no-op
            MessageEvent::Message(
                service::Payload::Storage(Payload::Response(StorageResponse::Write(result))),
                _,
            ) => {
                assert_matches!(result, Ok(UpdateState::Updated));
                break;
            }
            _ => panic!("Did not receive expected response: {:?}", response),
        }
    }

    let UnknownInfo(value) = store.get::<UnknownInfo>().await;
    assert_eq!(CHANGED_VALUE, value);
}
