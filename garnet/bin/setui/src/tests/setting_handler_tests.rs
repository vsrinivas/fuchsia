// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::accessibility::types::AccessibilityInfo,
    crate::agent::restore_agent,
    crate::base::{get_all_setting_types, SettingInfo, SettingType, UnknownInfo},
    crate::do_not_disturb::types::DoNotDisturbInfo,
    crate::handler::base::{ContextBuilder, Request},
    crate::handler::device_storage::testing::InMemoryStorageFactory,
    crate::handler::device_storage::{DeviceStorageCompatible, DeviceStorageFactory},
    crate::handler::setting_handler::persist::WriteResult,
    crate::handler::setting_handler::{
        controller, persist, persist::controller as data_controller, persist::write,
        persist::ClientProxy as DataClientProxy, persist::Handler as DataHandler, persist::Storage,
        BoxedController, ClientImpl, ClientProxy, Command, ControllerError, ControllerStateResult,
        Event, GenerateController, Handler, Payload, SettingHandlerResult, State,
    },
    crate::message::base::{Audience, MessageEvent, MessengerType},
    crate::service,
    crate::tests::message_utils::verify_payload,
    crate::EnvironmentBuilder,
    async_trait::async_trait,
    futures::channel::mpsc::{unbounded, UnboundedSender},
    futures::StreamExt,
    std::collections::HashMap,
    std::convert::TryFrom,
    std::marker::PhantomData,
    std::sync::Arc,
};

const ENV_NAME: &str = "settings_service_setting_handler_test_environment";
const CONTEXT_ID: u64 = 0;

macro_rules! gen_controller {
    ($name:ident, $succeed:expr) => {
        /// Controller is a simple controller test implementation that refers to a
        /// Control type for how to behave.
        struct $name {}

        #[async_trait]
        impl controller::Create for $name {
            async fn create(_: ClientProxy) -> Result<Self, ControllerError> {
                if $succeed {
                    Ok($name {})
                } else {
                    Err(ControllerError::InitFailure("failure".into()))
                }
            }
        }

        #[async_trait]
        impl controller::Handle for $name {
            async fn handle(&self, _: Request) -> Option<SettingHandlerResult> {
                return None;
            }

            async fn change_state(&mut self, _: State) -> Option<ControllerStateResult> {
                return None;
            }
        }
    };
}

gen_controller!(SucceedController, true);
gen_controller!(FailController, false);

macro_rules! gen_data_controller {
    ($name:ident, $succeed:expr) => {
        /// The DataController is a controller implementation with storage that
        /// defers to a Control type for how to behave.
        struct $name<S: Storage> {
            _storage: PhantomData<S>,
        }

        #[async_trait]
        impl<S: Storage> data_controller::Create for $name<S> {
            async fn create(_: DataClientProxy) -> Result<Self, ControllerError> {
                if $succeed {
                    Ok(Self { _storage: PhantomData })
                } else {
                    Err(ControllerError::InitFailure("failure".into()))
                }
            }
        }

        #[async_trait]
        impl<S: Storage> controller::Handle for $name<S> {
            async fn handle(&self, _: Request) -> Option<SettingHandlerResult> {
                return None;
            }

            async fn change_state(&mut self, _: State) -> Option<ControllerStateResult> {
                return None;
            }
        }
    };
}

gen_data_controller!(SucceedDataController, true);
gen_data_controller!(FailDataController, false);

macro_rules! verify_handle {
    ($spawn:expr) => {
        assert!(EnvironmentBuilder::new(Arc::new(InMemoryStorageFactory::new()))
            .handler(SettingType::Unknown, Box::new($spawn))
            .agents(&[restore_agent::blueprint::create()])
            .settings(&[SettingType::Unknown])
            .spawn_nested(ENV_NAME)
            .await
            .is_ok());
    };
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_spawn() {
    // Exercises successful spawn of a simple controller.
    verify_handle!(Handler::<SucceedController>::spawn);
    // Exercises failed spawn of a simple controller.
    verify_handle!(Handler::<FailController>::spawn);
    // Exercises successful spawn of a data controller.
    verify_handle!(DataHandler::<SucceedDataController<DoNotDisturbInfo>>::spawn);
    // Exercises failed spawn of a data controller.
    verify_handle!(DataHandler::<FailDataController<DoNotDisturbInfo>>::spawn);
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_write_notify() {
    let factory = service::message::create_hub();
    let (handler_messenger, handler_receptor) =
        factory.create(MessengerType::Unbound).await.unwrap();
    let signature = factory
        .create(MessengerType::Unbound)
        .await
        .expect("messenger should be created")
        .1
        .get_signature();
    let storage_factory = Arc::new(InMemoryStorageFactory::new());
    storage_factory.initialize_storage::<AccessibilityInfo>().await;
    let context = ContextBuilder::new(
        SettingType::Accessibility,
        Arc::clone(&storage_factory),
        handler_messenger,
        handler_receptor,
        signature,
        CONTEXT_ID,
    )
    .build();

    let (client_tx, mut client_rx) = futures::channel::mpsc::unbounded::<persist::ClientProxy>();

    let storage = storage_factory.get_store(context.id).await;
    let setting_type = context.setting_type;

    ClientImpl::create(
        context,
        Box::new(move |proxy| {
            let client_tx = client_tx.clone();
            let storage = storage.clone();
            Box::pin(async move {
                client_tx
                    .unbounded_send(persist::ClientProxy::new(proxy, storage, setting_type).await)
                    .ok();
                Ok(Box::new(BlankController {}) as BoxedController)
            })
        }),
    )
    .await
    .expect("client should be created");

    // Get the proxy.
    let mut proxy = client_rx.next().await.unwrap();

    verify_write_behavior(
        &mut proxy,
        AccessibilityInfo {
            audio_description: None,
            screen_reader: None,
            color_inversion: None,
            enable_magnification: None,
            color_correction: None,
            captions_settings: None,
        },
        false,
    )
    .await;

    verify_write_behavior(
        &mut proxy,
        AccessibilityInfo {
            audio_description: Some(true),
            screen_reader: None,
            color_inversion: None,
            enable_magnification: None,
            color_correction: None,
            captions_settings: None,
        },
        true,
    )
    .await;
}

async fn verify_write_behavior<S: DeviceStorageCompatible + Into<SettingInfo> + Send + Sync>(
    proxy: &mut persist::ClientProxy,
    value: S,
    notified: bool,
) {
    let result = write(proxy, value, false).await;

    assert_eq!(notified, result.notified());
    assert!(result.is_ok() && result.into_handler_result().is_ok());
}

/// StateController allows for exposing incoming handler state to an outside
/// listener.
struct StateController {
    state_reporter: UnboundedSender<State>,
    invocation_counts: HashMap<State, u8>,
    invocation_counts_reporter: UnboundedSender<HashMap<State, u8>>,
}

impl StateController {
    pub fn create_generator(
        reporter: UnboundedSender<State>,
        invocation_counts_reporter: UnboundedSender<HashMap<State, u8>>,
    ) -> GenerateController {
        Box::new(move |_| {
            let reporter = reporter.clone();
            let invocation_counts_reporter = invocation_counts_reporter.clone();
            Box::pin(async move {
                let mut invocation_counts = HashMap::new();
                invocation_counts.insert(State::Startup, 0);
                invocation_counts.insert(State::Listen, 0);
                invocation_counts.insert(State::EndListen, 0);
                invocation_counts.insert(State::Teardown, 0);
                Ok(Box::new(StateController {
                    state_reporter: reporter.clone(),
                    invocation_counts,
                    invocation_counts_reporter: invocation_counts_reporter.clone(),
                }) as BoxedController)
            })
        })
    }
}

#[async_trait]
impl controller::Handle for StateController {
    async fn handle(&self, _: Request) -> Option<SettingHandlerResult> {
        None
    }

    async fn change_state(&mut self, state: State) -> Option<ControllerStateResult> {
        self.invocation_counts.entry(state).and_modify(|e| *e += 1);
        self.invocation_counts_reporter.unbounded_send(self.invocation_counts.clone()).ok();
        self.state_reporter.unbounded_send(state).ok();
        None
    }
}

struct BlankController {}

#[async_trait]
impl controller::Handle for BlankController {
    async fn handle(&self, _: Request) -> Option<SettingHandlerResult> {
        return None;
    }

    async fn change_state(&mut self, _: State) -> Option<ControllerStateResult> {
        return None;
    }
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_event_propagation() {
    let factory = service::message::create_hub();
    let setting_type = SettingType::Unknown;

    let (messenger, receptor) = factory.create(MessengerType::Unbound).await.unwrap();
    let (event_tx, mut event_rx) = unbounded::<State>();
    let (invocations_tx, _invocations_rx) = unbounded::<HashMap<State, u8>>();
    let (handler_messenger, handler_receptor) =
        factory.create(MessengerType::Unbound).await.unwrap();
    let signature = handler_receptor.get_signature();
    let context = ContextBuilder::new(
        setting_type,
        Arc::new(InMemoryStorageFactory::new()),
        handler_messenger,
        handler_receptor,
        receptor.get_signature(),
        CONTEXT_ID,
    )
    .build();

    assert!(ClientImpl::create(
        context,
        StateController::create_generator(event_tx, invocations_tx)
    )
    .await
    .is_ok());

    messenger
        .message(
            Payload::Command(Command::ChangeState(State::Startup)).into(),
            Audience::Messenger(signature),
        )
        .send()
        .ack();

    assert_eq!(Some(State::Startup), event_rx.next().await);

    messenger
        .message(
            Payload::Command(Command::ChangeState(State::Listen)).into(),
            Audience::Messenger(signature),
        )
        .send()
        .ack();

    assert_eq!(Some(State::Listen), event_rx.next().await);

    messenger
        .message(
            Payload::Command(Command::ChangeState(State::EndListen)).into(),
            Audience::Messenger(signature),
        )
        .send()
        .ack();

    assert_eq!(Some(State::EndListen), event_rx.next().await);

    messenger
        .message(
            Payload::Command(Command::ChangeState(State::Teardown)).into(),
            Audience::Messenger(signature),
        )
        .send()
        .ack();

    assert_eq!(Some(State::Teardown), event_rx.next().await);

    // Deleting the signature of the messenger ensures the client event loop is stopped.
    factory.delete(signature);

    assert_eq!(None, event_rx.next().await);
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_rebroadcast() {
    let factory = service::message::create_hub();
    let setting_type = SettingType::Unknown;

    // This messenger represents the outside client for the setting controller, which would be
    // the setting proxy in most cases.
    let (messenger, mut receptor) = factory.create(MessengerType::Unbound).await.unwrap();

    // The handler messenger is handed to controllers to communicate with the wrapping handler
    // logic, which listens on counterpart receptor.
    let (handler_messenger, handler_receptor) =
        factory.create(MessengerType::Unbound).await.unwrap();

    let signature = handler_receptor.get_signature();

    let context = ContextBuilder::new(
        setting_type,
        Arc::new(InMemoryStorageFactory::new()),
        handler_messenger,
        handler_receptor,
        receptor.get_signature(),
        CONTEXT_ID,
    )
    .build();

    ClientImpl::create(
        context,
        StubControllerBuilder::new()
            .add_request_mapping(Request::Get, Ok(Some(UnknownInfo(true).into())))
            .build(),
    )
    .await
    .expect("creating controller should succeed");

    // Begin listening, enabling notifications.
    messenger
        .message(
            Payload::Command(Command::ChangeState(State::Listen)).into(),
            Audience::Messenger(signature),
        )
        .send()
        .ack();

    // Request rebroadcast of data.
    messenger
        .message(
            Payload::Command(Command::HandleRequest(Request::Rebroadcast)).into(),
            Audience::Messenger(signature),
        )
        .send()
        .ack();

    verify_payload(
        Payload::Event(Event::Changed(UnknownInfo(true).into())).into(),
        &mut receptor,
        None,
    )
    .await
}

// Test that the controller state is entered [n] times.
async fn verify_controller_state(state: State, n: u8) {
    let factory = service::message::create_hub();
    let setting_type = SettingType::Audio;

    let (messenger, receptor) = factory.create(MessengerType::Unbound).await.unwrap();
    let (event_tx, mut event_rx) = unbounded::<State>();
    let (invocations_tx, mut invocations_rx) = unbounded::<HashMap<State, u8>>();
    let (handler_messenger, handler_receptor) =
        factory.create(MessengerType::Unbound).await.unwrap();
    let signature = handler_receptor.get_signature();
    let context = ContextBuilder::new(
        setting_type,
        Arc::new(InMemoryStorageFactory::new()),
        handler_messenger,
        handler_receptor,
        receptor.get_signature(),
        CONTEXT_ID,
    )
    .build();

    ClientImpl::create(context, StateController::create_generator(event_tx, invocations_tx))
        .await
        .expect("Unable to create ClientImpl");

    messenger
        .message(
            Payload::Command(Command::ChangeState(state)).into(),
            Audience::Messenger(signature),
        )
        .send()
        .ack();

    assert_eq!(Some(state), event_rx.next().await);

    if let Some(invocation_counts) = invocations_rx.next().await {
        assert_eq!(*invocation_counts.get(&state).unwrap(), n);
    } else {
        panic!("Should have receiced message from receptor");
    }
}

#[fuchsia_async::run_until_stalled(test)]
// Test that the setting handler calls ChangeState(State::Startup) on controller.
async fn test_startup_state() {
    verify_controller_state(State::Startup, 1).await;
}

#[fuchsia_async::run_until_stalled(test)]
// Test that the setting handler calls ChangeState(State::Teardown) on controller.
async fn test_teardown_state() {
    verify_controller_state(State::Teardown, 1).await;
}

struct StubControllerBuilder {
    request_mapping: Vec<(Request, SettingHandlerResult)>,
}

impl StubControllerBuilder {
    pub fn new() -> Self {
        Self { request_mapping: Vec::new() }
    }

    /// Maps a preset [`SettingHandlerResult`] to return when the specified [`Request`] is
    /// encountered.
    pub fn add_request_mapping(mut self, request: Request, result: SettingHandlerResult) -> Self {
        self.request_mapping.retain(|(target_request, _)| *target_request != request);
        self.request_mapping.push((request, result));

        self
    }

    pub fn build(self) -> GenerateController {
        let request_mapping = self.request_mapping;
        Box::new(move |_| {
            let request_mapping = request_mapping.clone();
            Box::pin(
                async move { Ok(Box::new(StubController { request_mapping }) as BoxedController) },
            )
        })
    }
}

/// Controller that responds to requests based on a mapping to preset responses.
struct StubController {
    request_mapping: Vec<(Request, SettingHandlerResult)>,
}

#[async_trait]
impl controller::Handle for StubController {
    async fn handle(&self, request: Request) -> Option<SettingHandlerResult> {
        self.request_mapping
            .iter()
            .find(|(key, _)| *key == request)
            .map_or(None, |x| Some(x.1.clone()))
    }

    async fn change_state(&mut self, _: State) -> Option<ControllerStateResult> {
        None
    }
}

/// Ensures that the correct unimplemented error is returned when the controller
/// doesn't properly handle a given command.
#[fuchsia_async::run_until_stalled(test)]
async fn test_unimplemented_error() {
    for setting_type in get_all_setting_types() {
        let factory = service::message::create_hub();

        let (messenger, receptor) = factory.create(MessengerType::Unbound).await.unwrap();
        let (handler_messenger, handler_receptor) =
            factory.create(MessengerType::Unbound).await.unwrap();
        let signature = handler_receptor.get_signature();
        let context = ContextBuilder::new(
            setting_type,
            Arc::new(InMemoryStorageFactory::new()),
            handler_messenger,
            handler_receptor,
            receptor.get_signature(),
            CONTEXT_ID,
        )
        .build();

        assert!(ClientImpl::create(context, StubControllerBuilder::new().build()).await.is_ok());

        let mut reply_receptor = messenger
            .message(
                Payload::Command(Command::HandleRequest(Request::Get)).into(),
                Audience::Messenger(signature),
            )
            .send();

        while let Some(message_event) = reply_receptor.next().await {
            if let MessageEvent::Message(incoming_payload, _) = message_event {
                if let Ok(Payload::Result(Err(ControllerError::UnimplementedRequest(
                    incoming_type,
                    _,
                )))) = Payload::try_from(incoming_payload)
                {
                    assert_eq!(incoming_type, setting_type);
                    return;
                } else {
                    panic!("should have received a result");
                }
            }
        }
    }
}
