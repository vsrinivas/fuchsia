// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The [`StorageAgent`] is responsible for all reads and writes to storage for the
//! settings service.

use crate::accessibility::types::AccessibilityInfo;
use crate::agent::{self, Context, Lifespan};
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
use crate::privacy::types::PrivacyInfo;
use crate::service::{self, Address};
use crate::setup::types::SetupInfo;
use crate::storage::{Error, Payload, StorageRequest, StorageResponse};
use crate::Role;
use fuchsia_async as fasync;
use futures::future::BoxFuture;
use futures::stream::{FuturesUnordered, StreamFuture};
use futures::StreamExt;
use std::borrow::Borrow;
use std::sync::Arc;

/// `Blueprint` struct for managing the state needed to construct a [`StorageAgent`].
pub struct Blueprint<T>
where
    T: DeviceStorageFactory,
{
    storage_factory: Arc<T>,
}

impl<T> Blueprint<T>
where
    T: DeviceStorageFactory,
{
    pub fn new(storage_factory: Arc<T>) -> Self {
        Self { storage_factory }
    }
}

impl<T> agent::Blueprint for Blueprint<T>
where
    T: DeviceStorageFactory + Send + Sync + 'static,
{
    fn create(&self, context: Context) -> BoxFuture<'static, ()> {
        let storage_factory = Arc::clone(&self.storage_factory);
        Box::pin(async move {
            StorageAgent::create(context, storage_factory).await;
        })
    }
}

pub struct StorageAgent<T>
where
    T: DeviceStorageFactory + Send + Sync + 'static,
{
    receptor: Option<service::message::Receptor>,
    storage_factory: Arc<T>,
}

impl<T> StorageAgent<T>
where
    T: DeviceStorageFactory + Send + Sync + 'static,
{
    async fn create(context: Context, storage_factory: Arc<T>) {
        let (_, receptor) = context
            .messenger_factory
            .create(MessengerType::Addressable(Address::Storage))
            .await
            .expect("should acquire messenger");
        let mut storage_agent = StorageAgent { receptor: Some(receptor), storage_factory };

        let unordered = FuturesUnordered::new();
        unordered.push(context.receptor.into_future());
        fasync::Task::spawn(async move { storage_agent.handle_messages(unordered).await }).detach();
    }

    async fn handle_messages(
        &mut self,
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
                    // Only initialize the message receptor once during Initialization.
                    if let Lifespan::Initialization = invocation.lifespan {
                        unordered.push(
                            self.receptor
                                .take()
                                .expect("Can only initialize storage agent once")
                                .into_future(),
                        );
                    }

                    // Always reply with an Ok for invocations.
                    client.reply(service::Payload::Agent(agent::Payload::Complete(Ok(())))).send();
                }
                MessageEvent::Message(
                    service::Payload::Storage(Payload::Request(storage_request)),
                    responder,
                ) => {
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
    async fn read<S>(&self, responder: service::message::MessageClient)
    where
        S: DeviceStorageConvertible + Into<SettingInfo>,
    {
        let store = self.storage_factory.get_store(0).await;
        let setting: S = store.get::<S::Storable>().await.into();
        responder.reply(Payload::Response(StorageResponse::Read(setting.into())).into()).send();
    }

    async fn write<S>(&self, data: S, flush: bool, responder: service::message::MessageClient)
    where
        S: DeviceStorageConvertible,
    {
        let update_result = {
            let store = self.storage_factory.get_store(0).await;
            let storable_value = data.get_storable();
            let storable_value: &S::Storable = storable_value.borrow();
            if storable_value == &store.get::<S::Storable>().await {
                Ok(UpdateState::Unchanged)
            } else {
                store
                    .write::<S::Storable>(storable_value, flush)
                    .await
                    .map(|_| UpdateState::Updated)
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
            StorageRequest::Read(setting_type) => match setting_type {
                #[cfg(test)]
                SettingType::Unknown => self.read::<UnknownInfo>(responder).await,
                SettingType::Accessibility => self.read::<AccessibilityInfo>(responder).await,
                SettingType::Account => panic!("SettingType::Account does not support storage"),
                SettingType::Audio => self.read::<AudioInfo>(responder).await,
                SettingType::Device => panic!("SettingType::Device does not support storage"),
                SettingType::Display => self.read::<DisplayInfo>(responder).await,
                SettingType::DoNotDisturb => self.read::<DoNotDisturbInfo>(responder).await,
                SettingType::FactoryReset => self.read::<FactoryResetInfo>(responder).await,
                SettingType::Input => self.read::<InputInfoSources>(responder).await,
                SettingType::Intl => self.read::<IntlInfo>(responder).await,
                SettingType::Light => self.read::<LightInfo>(responder).await,
                SettingType::LightSensor => {
                    panic!("SettingType::LightSensor does not support storage")
                }
                SettingType::NightMode => self.read::<NightModeInfo>(responder).await,
                SettingType::Power => panic!("SettingType::Power does not support storage"),
                SettingType::Privacy => self.read::<PrivacyInfo>(responder).await,
                SettingType::Setup => self.read::<SetupInfo>(responder).await,
            },
            StorageRequest::Write(setting_info, flush) => match setting_info {
                #[cfg(test)]
                SettingInfo::Unknown(info) => self.write(info, flush, responder).await,
                SettingInfo::Accessibility(info) => self.write(info, flush, responder).await,
                SettingInfo::Audio(info) => self.write(info, flush, responder).await,
                SettingInfo::Brightness(info) => self.write(info, flush, responder).await,
                SettingInfo::Device(_) => panic!("SettingInfo::Device does not support storage"),
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
            },
        }
    }
}

payload_convert!(Storage, Payload);
#[cfg(test)]
mod tests {
    use super::*;
    use crate::async_property_test;
    use crate::device::types::DeviceInfo;
    use crate::display::types::LightData;
    use crate::handler::device_storage::testing::InMemoryStorageFactory;
    use crate::message::base::Audience;

    enum Setting {
        Info(SettingInfo),
        Type(SettingType),
    }

    async_property_test!(unsupported_types_panic_on_read_and_write => [
        #[should_panic(expected = "SettingType::Account does not support storage")]
        account_read(Setting::Type(SettingType::Account)),

        #[should_panic(expected = "SettingType::Device does not support storage")]
        device_read(Setting::Type(SettingType::Device)),

        #[should_panic(expected = "SettingType::LightSensor does not support storage")]
        light_sensor_read(Setting::Type(SettingType::LightSensor)),

        #[should_panic(expected = "SettingType::Power does not support storage")]
        power_read(Setting::Type(SettingType::Power)),

        #[should_panic(expected = "SettingInfo::Device does not support storage")]
        device_write(Setting::Info(SettingInfo::Device(DeviceInfo::new("abc".into())))),

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
        let messenger_factory = service::message::create_hub();
        let (messenger, _) =
            messenger_factory.create(MessengerType::Unbound).await.expect("messenger created");
        let (_, mut receptor) =
            messenger_factory.create(MessengerType::Unbound).await.expect("receptor created");
        messenger
            .message(
                service::Payload::Storage(Payload::Request(StorageRequest::Read(
                    SettingType::Unknown,
                ))),
                Audience::Messenger(receptor.get_signature()),
            )
            .send();

        // Will stall if not encountered.
        let responder = if let Some(MessageEvent::Message(
            service::Payload::Storage(Payload::Request(_)),
            responder,
        )) = receptor.next().await
        {
            responder
        } else {
            panic!("Test setup is broken, should have received a storage request")
        };

        match setting {
            Setting::Type(setting_type) => {
                storage_manager.handle_request(StorageRequest::Read(setting_type), responder).await;
            }
            Setting::Info(setting_info) => {
                storage_manager
                    .handle_request(StorageRequest::Write(setting_info, true), responder)
                    .await;
            }
        }
    }
}
