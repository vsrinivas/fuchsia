// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The [`StorageAgent`] is responsible for all reads and writes to storage for the
//! settings service.

use std::borrow::Borrow;
use std::sync::Arc;

use fuchsia_async as fasync;
use futures::future::BoxFuture;
use futures::stream::{FuturesUnordered, StreamFuture};
use futures::StreamExt;

use crate::accessibility::types::AccessibilityInfo;
use crate::agent::{self, Context, Lifespan};
use crate::audio::policy as audio_policy;
use crate::audio::types::AudioInfo;
#[cfg(test)]
use crate::base::UnknownInfo;
use crate::base::{SettingInfo, SettingType};
use crate::display::types::DisplayInfo;
use crate::do_not_disturb::types::DoNotDisturbInfo;
use crate::factory_reset::types::FactoryResetInfo;
use crate::handler::device_storage::{DeviceStorageConvertible, DeviceStorageFactory};
use crate::handler::setting_handler::persist::UpdateState;
use crate::input::types::InputInfoSources;
use crate::intl::types::IntlInfo;
use crate::light::types::LightInfo;
use crate::message::base::{MessageEvent, MessengerType};
use crate::message::receptor::Receptor;
use crate::night_mode::types::NightModeInfo;
use crate::payload_convert;
#[cfg(test)]
use crate::policy;
use crate::policy::{PolicyInfo, PolicyType};
use crate::privacy::types::PrivacyInfo;
use crate::service::{self, Address};
use crate::setup::types::SetupInfo;
use crate::storage::{Error, Payload, StorageInfo, StorageRequest, StorageResponse, StorageType};
use crate::trace::TracingNonce;
use crate::Role;
use crate::{trace, trace_guard};

/// `Blueprint` struct for managing the state needed to construct a [`StorageAgent`].
pub(crate) struct Blueprint<T>
where
    T: DeviceStorageFactory,
{
    storage_factory: Arc<T>,
}

impl<T> Blueprint<T>
where
    T: DeviceStorageFactory,
{
    pub(crate) fn new(storage_factory: Arc<T>) -> Self {
        Self { storage_factory }
    }
}

impl<T> agent::Blueprint for Blueprint<T>
where
    T: DeviceStorageFactory + Send + Sync + 'static,
{
    fn debug_id(&self) -> &'static str {
        "StorageAgent"
    }

    fn create(&self, context: Context) -> BoxFuture<'static, ()> {
        let storage_factory = Arc::clone(&self.storage_factory);
        Box::pin(async move {
            StorageAgent::create(context, storage_factory).await;
        })
    }
}

pub(crate) struct StorageAgent<T>
where
    T: DeviceStorageFactory + Send + Sync + 'static,
{
    /// The factory for creating a messenger to receive messages.
    delegate: service::message::Delegate,
    storage_factory: Arc<T>,
}

impl<T> StorageAgent<T>
where
    T: DeviceStorageFactory + Send + Sync + 'static,
{
    async fn create(context: Context, storage_factory: Arc<T>) {
        let mut storage_agent = StorageAgent { delegate: context.delegate, storage_factory };

        let unordered = FuturesUnordered::new();
        unordered.push(context.receptor.into_future());
        fasync::Task::spawn(async move {
            let nonce = fuchsia_trace::generate_nonce();
            trace!(nonce, "storage_agent");
            storage_agent.handle_messages(nonce, unordered).await
        })
        .detach();
    }

    async fn handle_messages(
        &mut self,
        nonce: TracingNonce,
        mut unordered: FuturesUnordered<
            StreamFuture<Receptor<service::Payload, service::Address, Role>>,
        >,
    ) {
        let storage_management =
            StorageManagement { storage_factory: Arc::clone(&self.storage_factory) };
        while let Some((event, stream)) = unordered.next().await {
            let event = if let Some(event) = event {
                event
            } else {
                continue;
            };

            match event {
                MessageEvent::Message(
                    service::Payload::Agent(agent::Payload::Invocation(invocation)),
                    client,
                ) => {
                    trace!(nonce, "agent event");
                    // Only initialize the message receptor once during Initialization.
                    if let Lifespan::Initialization = invocation.lifespan {
                        let receptor = self
                            .delegate
                            .create(MessengerType::Addressable(Address::Storage))
                            .await
                            .expect("should acquire messenger")
                            .1;

                        unordered.push(receptor.into_future());
                    }

                    // Always reply with an Ok for invocations.
                    client.reply(service::Payload::Agent(agent::Payload::Complete(Ok(())))).send();
                }
                MessageEvent::Message(
                    service::Payload::Storage(Payload::Request(storage_request)),
                    responder,
                ) => {
                    trace!(nonce, "storage event");
                    storage_management.handle_request(storage_request, responder).await;
                }
                _ => {} // Other messages are ignored
            }

            // When we have received an event, we need to make sure to add the rest of the events
            // back onto the unordered list.
            unordered.push(stream.into_future());
        }
    }
}

macro_rules! into_storage_info {
    ($ty:ty => $intermediate_ty:ty) => {
        impl From<$ty> for StorageInfo {
            fn from(info: $ty) -> StorageInfo {
                let info: $intermediate_ty = info.into();
                info.into()
            }
        }
    };
}

#[cfg(test)]
into_storage_info!(UnknownInfo => SettingInfo);
#[cfg(test)]
into_storage_info!(policy::UnknownInfo => PolicyInfo);
into_storage_info!(AccessibilityInfo => SettingInfo);
into_storage_info!(AudioInfo => SettingInfo);
into_storage_info!(DisplayInfo => SettingInfo);
into_storage_info!(FactoryResetInfo => SettingInfo);
into_storage_info!(LightInfo => SettingInfo);
into_storage_info!(DoNotDisturbInfo => SettingInfo);
into_storage_info!(InputInfoSources => SettingInfo);
into_storage_info!(IntlInfo => SettingInfo);
into_storage_info!(NightModeInfo => SettingInfo);
into_storage_info!(PrivacyInfo => SettingInfo);
into_storage_info!(SetupInfo => SettingInfo);
into_storage_info!(audio_policy::State => PolicyInfo);

struct StorageManagement<T>
where
    T: DeviceStorageFactory,
{
    storage_factory: Arc<T>,
}

impl<T> StorageManagement<T>
where
    T: DeviceStorageFactory,
{
    async fn read<S>(&self, nonce: TracingNonce, responder: service::message::MessageClient)
    where
        S: DeviceStorageConvertible + Into<StorageInfo>,
    {
        let guard = trace_guard!(nonce, "get store");
        let store = self.storage_factory.get_store().await;
        drop(guard);

        let guard = trace_guard!(nonce, "get data");
        let storable: S = store.get::<S::Storable>().await.into();
        drop(guard);

        let guard = trace_guard!(nonce, "reply");
        responder.reply(Payload::Response(StorageResponse::Read(storable.into())).into()).send();
        drop(guard);
    }

    async fn write<S>(&self, data: S, flush: bool, responder: service::message::MessageClient)
    where
        S: DeviceStorageConvertible,
    {
        let update_result = {
            let store = self.storage_factory.get_store().await;
            let storable_value = data.get_storable();
            let storable_value: &S::Storable = storable_value.borrow();
            if storable_value == &store.get::<S::Storable>().await {
                Ok(UpdateState::Unchanged)
            } else {
                store
                    .write::<S::Storable>(storable_value, flush)
                    .await
                    .map_err(|e| Error { message: format!("{:?}", e) })
            }
        };

        responder
            .reply(service::Payload::Storage(Payload::Response(StorageResponse::Write(
                update_result,
            ))))
            .send();
    }

    async fn handle_request(
        &self,
        storage_request: StorageRequest,
        responder: service::message::MessageClient,
    ) {
        match storage_request {
            StorageRequest::Read(StorageType::SettingType(setting_type), nonce) => {
                match setting_type {
                    #[cfg(test)]
                    SettingType::Unknown => self.read::<UnknownInfo>(nonce, responder).await,
                    SettingType::Accessibility => {
                        self.read::<AccessibilityInfo>(nonce, responder).await
                    }
                    SettingType::Account => panic!("SettingType::Account does not support storage"),
                    SettingType::Audio => {
                        trace!(nonce, "audio storage read");
                        self.read::<AudioInfo>(nonce, responder).await
                    }
                    SettingType::Display => self.read::<DisplayInfo>(nonce, responder).await,
                    SettingType::DoNotDisturb => {
                        self.read::<DoNotDisturbInfo>(nonce, responder).await
                    }
                    SettingType::FactoryReset => {
                        self.read::<FactoryResetInfo>(nonce, responder).await
                    }
                    SettingType::Input => self.read::<InputInfoSources>(nonce, responder).await,
                    SettingType::Intl => self.read::<IntlInfo>(nonce, responder).await,
                    SettingType::Light => self.read::<LightInfo>(nonce, responder).await,
                    SettingType::LightSensor => {
                        panic!("SettingType::LightSensor does not support storage")
                    }
                    SettingType::NightMode => self.read::<NightModeInfo>(nonce, responder).await,
                    SettingType::Privacy => self.read::<PrivacyInfo>(nonce, responder).await,
                    SettingType::Setup => self.read::<SetupInfo>(nonce, responder).await,
                }
            }
            StorageRequest::Read(StorageType::PolicyType(policy_type), nonce) => {
                match policy_type {
                    #[cfg(test)]
                    PolicyType::Unknown => self.read::<policy::UnknownInfo>(nonce, responder).await,
                    PolicyType::Audio => self.read::<audio_policy::State>(nonce, responder).await,
                }
            }
            StorageRequest::Write(StorageInfo::SettingInfo(setting_info), flush, nonce) => {
                match setting_info {
                    #[cfg(test)]
                    SettingInfo::Unknown(info) => self.write(info, flush, responder).await,
                    SettingInfo::Accessibility(info) => self.write(info, flush, responder).await,
                    SettingInfo::Audio(info) => {
                        trace!(nonce, "audio storage write");
                        self.write(info, flush, responder).await
                    }
                    SettingInfo::Brightness(info) => self.write(info, flush, responder).await,
                    SettingInfo::DoNotDisturb(info) => self.write(info, flush, responder).await,
                    SettingInfo::FactoryReset(info) => self.write(info, flush, responder).await,
                    SettingInfo::Input(info) => self.write(info, flush, responder).await,
                    SettingInfo::Intl(info) => self.write(info, flush, responder).await,
                    SettingInfo::Light(info) => self.write(info, flush, responder).await,
                    SettingInfo::LightSensor(_) => {
                        panic!("SettingInfo::LightSensor does not support storage")
                    }
                    SettingInfo::NightMode(info) => self.write(info, flush, responder).await,
                    SettingInfo::Privacy(info) => self.write(info, flush, responder).await,
                    SettingInfo::Setup(info) => self.write(info, flush, responder).await,
                }
            }
            StorageRequest::Write(StorageInfo::PolicyInfo(policy_info), flush, _nonce) => {
                match policy_info {
                    #[cfg(test)]
                    PolicyInfo::Unknown(info) => self.write(info, flush, responder).await,
                    PolicyInfo::Audio(info) => self.write(info, flush, responder).await,
                }
            }
        }
    }
}

payload_convert!(Storage, Payload);
#[cfg(test)]
mod tests {
    use crate::async_property_test;
    use crate::display::types::LightData;
    use crate::handler::device_storage::testing::InMemoryStorageFactory;
    use crate::message::base::Audience;
    use crate::message::MessageHubUtil;

    use super::*;

    enum Setting {
        Info(SettingInfo),
        Type(SettingType),
    }

    async_property_test!(unsupported_types_panic_on_read_and_write => [
        #[should_panic(expected = "SettingType::Account does not support storage")]
        account_read(Setting::Type(SettingType::Account)),

        #[should_panic(expected = "SettingType::LightSensor does not support storage")]
        light_sensor_read(Setting::Type(SettingType::LightSensor)),

        #[should_panic(expected = "SettingInfo::LightSensor does not support storage")]
        light_sensor_write(Setting::Info(SettingInfo::LightSensor(LightData {
            illuminance: 0.0f32,
            color: fidl_fuchsia_ui_types::ColorRgb {
                red: 0.0f32,
                green: 0.0f32,
                blue: 0.0f32,
            },
        }))),
    ]);

    async fn unsupported_types_panic_on_read_and_write(setting: Setting) {
        let storage_manager =
            StorageManagement { storage_factory: Arc::new(InMemoryStorageFactory::new()) };

        // This section is just to get a responder. We don't need it to actually respond to anything.
        let delegate = service::MessageHub::create_hub();
        let (messenger, _) =
            delegate.create(MessengerType::Unbound).await.expect("messenger created");
        let (_, mut receptor) =
            delegate.create(MessengerType::Unbound).await.expect("receptor created");
        messenger
            .message(
                service::Payload::Storage(Payload::Request(StorageRequest::Read(
                    SettingType::Unknown.into(),
                    0,
                ))),
                Audience::Messenger(receptor.get_signature()),
            )
            .send();

        // Will stall if not encountered.
        let responder =
            if let Ok((Payload::Request(_), responder)) = receptor.next_of::<Payload>().await {
                responder
            } else {
                panic!("Test setup is broken, should have received a storage request")
            };

        match setting {
            Setting::Type(setting_type) => {
                storage_manager
                    .handle_request(StorageRequest::Read(setting_type.into(), 0), responder)
                    .await;
            }
            Setting::Info(setting_info) => {
                storage_manager
                    .handle_request(StorageRequest::Write(setting_info.into(), true, 0), responder)
                    .await;
            }
        }
    }
}
