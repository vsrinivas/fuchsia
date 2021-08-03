// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::{SettingInfo, SettingType, UnknownInfo};
use crate::handler::device_storage::testing::InMemoryStorageFactory;
use crate::handler::device_storage::{
    DeviceStorage, DeviceStorageAccess, DeviceStorageCompatible, DeviceStorageFactory,
};
use crate::handler::setting_handler::persist::UpdateState;
use crate::message::base::{Audience, MessengerType};
use crate::service::{self, Address};
use crate::storage::{Payload, StorageInfo, StorageRequest, StorageResponse};
use crate::EnvironmentBuilder;
use matches::assert_matches;
use std::sync::Arc;

const ENV_NAME: &str = "storage_agent_test_environment";
const ORIGINAL_VALUE: bool = true;
struct TestAccess;
impl DeviceStorageAccess for TestAccess {
    const STORAGE_KEYS: &'static [&'static str] = &[UnknownInfo::KEY];
}

async fn create_test_environment() -> (service::message::Delegate, Arc<DeviceStorage>) {
    let storage_factory = Arc::new(InMemoryStorageFactory::new());
    storage_factory
        .initialize::<TestAccess>()
        .await
        .expect("Should be able to initialize unknown info");
    let env =
        EnvironmentBuilder::new(Arc::clone(&storage_factory)).spawn_nested(ENV_NAME).await.unwrap();
    let store = storage_factory.get_store().await;
    store.write(&UnknownInfo(ORIGINAL_VALUE), true).await.expect("Write should succeed");
    (env.delegate, store)
}

// Assert that we can read values by sending messages to the storage agent and receive a response.
#[fuchsia_async::run_until_stalled(test)]
async fn test_read() {
    let (delegate, _) = create_test_environment().await;
    let (messenger, _) =
        delegate.create(MessengerType::Unbound).await.expect("should be able to get messenger");
    let mut receptor = messenger
        .message(
            service::Payload::Storage(Payload::Request(StorageRequest::Read(
                SettingType::Unknown.into(),
                0,
            ))),
            Audience::Address(Address::Storage),
        )
        .send();

    assert_matches!(receptor.next_of::<Payload>().await,
        Ok((
            Payload::Response(StorageResponse::Read(StorageInfo::SettingInfo(
                SettingInfo::Unknown(UnknownInfo(value))
            ))),
            _
        )) if value == ORIGINAL_VALUE);
}

// Assert that we can write values by sending messages to the storage agent and seeing a response
// and the value in the in memory storage.
#[fuchsia_async::run_until_stalled(test)]
async fn test_write() {
    const CHANGED_VALUE: bool = false;

    let (service_delegate, store) = create_test_environment().await;

    // Validate original value before the write request.
    let UnknownInfo(value) = store.get::<UnknownInfo>().await;
    assert_eq!(ORIGINAL_VALUE, value);

    let (messenger, _) = service_delegate
        .create(MessengerType::Unbound)
        .await
        .expect("should be able to get messenger");
    let mut receptor = messenger
        .message(
            service::Payload::Storage(Payload::Request(StorageRequest::Write(
                SettingInfo::Unknown(UnknownInfo(CHANGED_VALUE)).into(),
                true,
                0,
            ))),
            Audience::Address(Address::Storage),
        )
        .send();

    assert_matches!(
        receptor.next_of::<Payload>().await,
        Ok((Payload::Response(StorageResponse::Write(Ok(UpdateState::Updated)),), _))
    );

    let UnknownInfo(value) = store.get::<UnknownInfo>().await;
    assert_eq!(CHANGED_VALUE, value);
}
