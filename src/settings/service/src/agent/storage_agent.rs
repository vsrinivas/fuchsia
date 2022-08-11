// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The [StorageAgent](storage_agent::StorageAgent) is responsible for all reads and writes to
//! storage for the settings service.

use std::borrow::Borrow;
use std::sync::Arc;

use fuchsia_async as fasync;
use fuchsia_trace as ftrace;
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
use crate::input::types::InputInfoSources;
use crate::intl::types::IntlInfo;
use crate::keyboard::types::KeyboardInfo;
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
use crate::Role;
use crate::{trace, trace_guard};
use settings_storage::device_storage::{DeviceStorage, DeviceStorageConvertible};
use settings_storage::fidl_storage::{FidlStorage, FidlStorageConvertible};
use settings_storage::storage_factory::StorageFactory;
use settings_storage::UpdateState;

/// `Blueprint` struct for managing the state needed to construct a [`StorageAgent`].
pub(crate) struct Blueprint<T, F>
where
    T: StorageFactory<Storage = DeviceStorage>,
    F: StorageFactory<Storage = FidlStorage>,
{
    device_storage_factory: Arc<T>,
    fidl_storage_factory: Arc<F>,
}

impl<T, F> Blueprint<T, F>
where
    T: StorageFactory<Storage = DeviceStorage>,
    F: StorageFactory<Storage = FidlStorage>,
{
    pub(crate) fn new(device_storage_factory: Arc<T>, fidl_storage_factory: Arc<F>) -> Self {
        Self { device_storage_factory, fidl_storage_factory }
    }
}

impl<T, F> agent::Blueprint for Blueprint<T, F>
where
    T: StorageFactory<Storage = DeviceStorage> + Send + Sync + 'static,
    F: StorageFactory<Storage = FidlStorage> + Send + Sync + 'static,
{
    fn debug_id(&self) -> &'static str {
        "StorageAgent"
    }

    fn create(&self, context: Context) -> BoxFuture<'static, ()> {
        let device_storage_factory = Arc::clone(&self.device_storage_factory);
        let fidl_storage_factory = Arc::clone(&self.fidl_storage_factory);
        Box::pin(async move {
            StorageAgent::create(context, device_storage_factory, fidl_storage_factory).await;
        })
    }
}

pub(crate) struct StorageAgent<T, F>
where
    T: StorageFactory<Storage = DeviceStorage> + Send + Sync + 'static,
    F: StorageFactory<Storage = FidlStorage> + Send + Sync + 'static,
{
    /// The factory for creating a messenger to receive messages.
    delegate: service::message::Delegate,
    device_storage_factory: Arc<T>,
    fidl_storage_factory: Arc<F>,
}

impl<T, F> StorageAgent<T, F>
where
    T: StorageFactory<Storage = DeviceStorage> + Send + Sync + 'static,
    F: StorageFactory<Storage = FidlStorage> + Send + Sync + 'static,
{
    async fn create(
        context: Context,
        device_storage_factory: Arc<T>,
        fidl_storage_factory: Arc<F>,
    ) {
        let mut storage_agent = StorageAgent {
            delegate: context.delegate,
            device_storage_factory,
            fidl_storage_factory,
        };

        let unordered = FuturesUnordered::new();
        unordered.push(context.receptor.into_future());
        fasync::Task::spawn(async move {
            let id = ftrace::Id::new();
            trace!(id, "storage_agent");
            storage_agent.handle_messages(id, unordered).await
        })
        .detach();
    }

    async fn handle_messages(
        &mut self,
        id: ftrace::Id,
        mut unordered: FuturesUnordered<
            StreamFuture<Receptor<service::Payload, service::Address, Role>>,
        >,
    ) {
        let storage_management = StorageManagement {
            device_storage_factory: Arc::clone(&self.device_storage_factory),
            fidl_storage_factory: Arc::clone(&self.fidl_storage_factory),
        };
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
                    trace!(id, "agent event");
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

                    // Always reply with an Ok for invocations. Ignore the receptor result.
                    let _ = client
                        .reply(service::Payload::Agent(agent::Payload::Complete(Ok(()))))
                        .send();
                }
                MessageEvent::Message(
                    service::Payload::Storage(Payload::Request(storage_request)),
                    responder,
                ) => {
                    trace!(id, "storage event");
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
into_storage_info!(KeyboardInfo => SettingInfo);
into_storage_info!(NightModeInfo => SettingInfo);
into_storage_info!(PrivacyInfo => SettingInfo);
into_storage_info!(SetupInfo => SettingInfo);
into_storage_info!(audio_policy::State => PolicyInfo);

struct StorageManagement<T, F>
where
    T: StorageFactory<Storage = DeviceStorage>,
    F: StorageFactory<Storage = FidlStorage>,
{
    device_storage_factory: Arc<T>,
    fidl_storage_factory: Arc<F>,
}

impl<T, F> StorageManagement<T, F>
where
    T: StorageFactory<Storage = DeviceStorage>,
    F: StorageFactory<Storage = FidlStorage>,
{
    async fn read<S>(&self, id: ftrace::Id, responder: service::message::MessageClient)
    where
        S: DeviceStorageConvertible + Into<StorageInfo>,
    {
        let guard = trace_guard!(id, "get store");
        let store = self.device_storage_factory.get_store().await;
        drop(guard);

        let guard = trace_guard!(id, "get data");
        let storable: S = store.get::<S::Storable>().await.into();
        drop(guard);

        let guard = trace_guard!(id, "reply");
        // Ignore the receptor result.
        let _ = responder
            .reply(Payload::Response(StorageResponse::Read(storable.into())).into())
            .send();
        drop(guard);
    }

    async fn write<S>(&self, data: S, responder: service::message::MessageClient)
    where
        S: DeviceStorageConvertible,
    {
        let update_result = {
            let store = self.device_storage_factory.get_store().await;
            let storable_value = data.get_storable();
            let storable_value: &S::Storable = storable_value.borrow();
            if storable_value == &store.get::<S::Storable>().await {
                Ok(UpdateState::Unchanged)
            } else {
                store
                    .write::<S::Storable>(storable_value)
                    .await
                    .map_err(|e| Error { message: format!("{:?}", e) })
            }
        };

        // Ignore the receptor result.
        let _ = responder
            .reply(service::Payload::Storage(Payload::Response(StorageResponse::Write(
                update_result,
            ))))
            .send();
    }

    async fn fidl_read<S>(&self, id: ftrace::Id, responder: service::message::MessageClient)
    where
        S: FidlStorageConvertible + Into<StorageInfo>,
    {
        let guard = trace_guard!(id, "get fidl store");
        let store = self.fidl_storage_factory.get_store().await;
        drop(guard);

        let guard = trace_guard!(id, "get data");
        let storable: S = store.get::<S>().await;
        drop(guard);

        let guard = trace_guard!(id, "reply");
        // Ignore the receptor result.
        let _ = responder
            .reply(Payload::Response(StorageResponse::Read(storable.into())).into())
            .send();
        drop(guard);
    }

    async fn fidl_write<S>(&self, data: S, responder: service::message::MessageClient)
    where
        S: FidlStorageConvertible,
    {
        let update_result = {
            let store = self.fidl_storage_factory.get_store().await;
            store.write::<S>(data).await.map_err(|e| Error { message: format!("{:?}", e) })
        };

        // Ignore the receptor result.
        let _ = responder
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
            StorageRequest::Read(StorageType::SettingType(setting_type), id) => {
                match setting_type {
                    #[cfg(test)]
                    SettingType::Unknown => self.read::<UnknownInfo>(id, responder).await,
                    SettingType::Accessibility => {
                        self.read::<AccessibilityInfo>(id, responder).await
                    }
                    SettingType::Audio => {
                        trace!(id, "audio storage read");
                        self.read::<AudioInfo>(id, responder).await
                    }
                    SettingType::Display => self.read::<DisplayInfo>(id, responder).await,
                    SettingType::DoNotDisturb => self.read::<DoNotDisturbInfo>(id, responder).await,
                    SettingType::FactoryReset => self.read::<FactoryResetInfo>(id, responder).await,
                    SettingType::Input => self.read::<InputInfoSources>(id, responder).await,
                    SettingType::Intl => self.read::<IntlInfo>(id, responder).await,
                    SettingType::Keyboard => self.read::<KeyboardInfo>(id, responder).await,
                    SettingType::Light => self.fidl_read::<LightInfo>(id, responder).await,
                    SettingType::LightSensor => {
                        panic!("SettingType::LightSensor does not support storage")
                    }
                    SettingType::NightMode => self.read::<NightModeInfo>(id, responder).await,
                    SettingType::Privacy => self.read::<PrivacyInfo>(id, responder).await,
                    SettingType::Setup => self.read::<SetupInfo>(id, responder).await,
                }
            }
            StorageRequest::Read(StorageType::PolicyType(policy_type), id) => match policy_type {
                #[cfg(test)]
                PolicyType::Unknown => self.read::<policy::UnknownInfo>(id, responder).await,
                PolicyType::Audio => self.read::<audio_policy::State>(id, responder).await,
            },
            StorageRequest::Write(StorageInfo::SettingInfo(setting_info), id) => match setting_info
            {
                #[cfg(test)]
                SettingInfo::Unknown(info) => self.write(info, responder).await,
                SettingInfo::Accessibility(info) => self.write(info, responder).await,
                SettingInfo::Audio(info) => {
                    trace!(id, "audio storage write");
                    self.write(info, responder).await
                }
                SettingInfo::Brightness(info) => self.write(info, responder).await,
                SettingInfo::DoNotDisturb(info) => self.write(info, responder).await,
                SettingInfo::FactoryReset(info) => self.write(info, responder).await,
                SettingInfo::Input(info) => self.write(info, responder).await,
                SettingInfo::Intl(info) => self.write(info, responder).await,
                SettingInfo::Keyboard(info) => self.write(info, responder).await,
                SettingInfo::Light(info) => self.fidl_write(info, responder).await,
                SettingInfo::LightSensor(_) => {
                    panic!("SettingInfo::LightSensor does not support storage")
                }
                SettingInfo::NightMode(info) => self.write(info, responder).await,
                SettingInfo::Privacy(info) => self.write(info, responder).await,
                SettingInfo::Setup(info) => self.write(info, responder).await,
            },
            StorageRequest::Write(StorageInfo::PolicyInfo(policy_info), _id) => match policy_info {
                #[cfg(test)]
                PolicyInfo::Unknown(info) => self.write(info, responder).await,
                PolicyInfo::Audio(info) => self.write(info, responder).await,
            },
        }
    }
}

payload_convert!(Storage, Payload);
#[cfg(test)]
mod tests {
    use super::*;
    use crate::async_property_test;
    use crate::display::types::LightData;
    use crate::message::base::Audience;
    use crate::message::MessageHubUtil;
    use crate::storage::testing::InMemoryStorageFactory;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_io::DirectoryMarker;
    use settings_storage::storage_factory::FidlStorageFactory;

    enum Setting {
        Info(SettingInfo),
        Type(SettingType),
    }

    async_property_test!(unsupported_types_panic_on_read_and_write => [
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
        let (directory_proxy, _stream) =
            create_proxy_and_stream::<DirectoryMarker>().expect("success");
        let storage_manager = StorageManagement {
            device_storage_factory: Arc::new(InMemoryStorageFactory::new()),
            fidl_storage_factory: Arc::new(FidlStorageFactory::new(1, directory_proxy)),
        };

        // This section is just to get a responder. We don't need it to actually respond to anything.
        let delegate = service::MessageHub::create_hub();
        let (messenger, _) =
            delegate.create(MessengerType::Unbound).await.expect("messenger created");
        let (_, mut receptor) =
            delegate.create(MessengerType::Unbound).await.expect("receptor created");
        let _ = messenger
            .message(
                service::Payload::Storage(Payload::Request(StorageRequest::Read(
                    SettingType::Unknown.into(),
                    0.into(),
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
                    .handle_request(StorageRequest::Read(setting_type.into(), 0.into()), responder)
                    .await;
            }
            Setting::Info(setting_info) => {
                storage_manager
                    .handle_request(StorageRequest::Write(setting_info.into(), 0.into()), responder)
                    .await;
            }
        }
    }
}
