// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#[cfg(test)]
use {
    crate::agent::restore_agent,
    crate::internal::handler::{message, Address, Payload},
    crate::message::base::{Audience, MessageEvent, MessengerType},
    crate::registry::base::{Command, ContextBuilder, State},
    crate::registry::device_storage::DeviceStorageFactory,
    crate::registry::device_storage::{testing::*, DeviceStorageCompatible},
    crate::registry::setting_handler::persist::WriteResult,
    crate::registry::setting_handler::{
        controller, persist, persist::controller as data_controller, persist::write,
        persist::ClientProxy as DataClientProxy, persist::Handler as DataHandler, persist::Storage,
        BoxedController, ClientImpl, ClientProxy, ControllerError, GenerateController, Handler,
    },
    crate::switchboard::accessibility_types::AccessibilityInfo,
    crate::switchboard::base::{
        get_all_setting_types, ControllerStateResult, DoNotDisturbInfo, SettingRequest,
        SettingResponseResult, SettingType, SwitchboardError,
    },
    crate::EnvironmentBuilder,
    async_trait::async_trait,
    futures::channel::mpsc::{unbounded, UnboundedSender},
    futures::StreamExt,
    std::collections::HashMap,
    std::marker::PhantomData,
};

const ENV_NAME: &str = "settings_service_setting_handler_test_environment";
const CONTEXT_ID: u64 = 0;

/// The Control trait provides static functions that control the behavior of
/// test controllers. Since controllers are created from a trait themselves,
/// we must specify this functionality as a trait so that the impl types can
/// be supplied as generic parameters.
trait Control {
    fn should_init_succeed() -> bool;
}

/// SucceedControl provides a Control implementation that will succeed on
/// initialization.
struct SucceedControl {}

impl Control for SucceedControl {
    fn should_init_succeed() -> bool {
        true
    }
}

/// FailControl provides a Control implementation that will fail on
/// initialization.
struct FailControl {}

impl Control for FailControl {
    fn should_init_succeed() -> bool {
        false
    }
}

/// Controller is a simple controller test implementation that refers to a
/// Control type for how to behave.
struct Controller<C: Control + Sync + Send + 'static> {
    _data: PhantomData<C>,
}

#[async_trait]
impl<C: Control + Sync + Send + 'static> controller::Create for Controller<C> {
    async fn create(_: ClientProxy) -> Result<Self, ControllerError> {
        if C::should_init_succeed() {
            Ok(Self { _data: PhantomData })
        } else {
            Err(ControllerError::InitFailure { description: "failure".to_string() })
        }
    }
}

#[async_trait]
impl<C: Control + Sync + Send + 'static> controller::Handle for Controller<C> {
    async fn handle(&self, _: SettingRequest) -> Option<SettingResponseResult> {
        return None;
    }

    async fn change_state(&mut self, _: State) -> Option<ControllerStateResult> {
        return None;
    }
}

/// The DataController is a controller implementation with storage that
/// defers to a Control type for how to behave.
struct DataController<C: Control + Sync + Send + 'static, S: Storage> {
    _control: PhantomData<C>,
    _storage: PhantomData<S>,
}

#[async_trait]
impl<C: Control + Sync + Send + 'static, S: Storage> data_controller::Create<S>
    for DataController<C, S>
{
    async fn create(_: DataClientProxy<S>) -> Result<Self, ControllerError> {
        if C::should_init_succeed() {
            Ok(Self { _control: PhantomData, _storage: PhantomData })
        } else {
            Err(ControllerError::InitFailure { description: "failure".to_string() })
        }
    }
}

#[async_trait]
impl<C: Control + Sync + Send + 'static, S: Storage> controller::Handle for DataController<C, S> {
    async fn handle(&self, _: SettingRequest) -> Option<SettingResponseResult> {
        return None;
    }

    async fn change_state(&mut self, _: State) -> Option<ControllerStateResult> {
        return None;
    }
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_spawn() {
    // Exercises successful spawn of a simple controller.
    verify_handler::<SucceedControl>(true).await;
    // Exercises failed spawn of a simple controller.
    verify_handler::<FailControl>(true).await;
    // Exercises successful spawn of a data controller.
    verify_data_handler::<SucceedControl>(true).await;
    // Exercises failed spawn of a data controller.
    verify_data_handler::<FailControl>(true).await;
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_write_notify() {
    let factory = message::create_hub();
    let (handler_messenger, handler_receptor) =
        factory.create(MessengerType::Unbound).await.unwrap();
    let context = ContextBuilder::new(
        SettingType::Accessibility,
        InMemoryStorageFactory::create(),
        handler_messenger,
        handler_receptor,
        CONTEXT_ID,
    )
    .build();

    let (client_tx, mut client_rx) =
        futures::channel::mpsc::unbounded::<persist::ClientProxy<AccessibilityInfo>>();

    let storage = context
        .environment
        .storage_factory_handle
        .lock()
        .await
        .get_store::<AccessibilityInfo>(context.id);
    let setting_type = context.setting_type;

    ClientImpl::create(
        context,
        Box::new(move |proxy| {
            let client_tx = client_tx.clone();
            let storage = storage.clone();
            Box::pin(async move {
                client_tx
                    .unbounded_send(
                        persist::ClientProxy::<AccessibilityInfo>::new(
                            proxy,
                            storage,
                            setting_type,
                        )
                        .await,
                    )
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

async fn verify_write_behavior<S: DeviceStorageCompatible + Send + Sync>(
    proxy: &mut persist::ClientProxy<S>,
    value: S,
    notified: bool,
) {
    let result = write(proxy, value, false).await;

    assert_eq!(notified, result.notified());
    assert!(result.is_ok() && result.into_response_result().is_ok());
}

async fn verify_handler<C: Control + Sync + Send + 'static>(should_succeed: bool) {
    assert_eq!(
        should_succeed,
        EnvironmentBuilder::new(InMemoryStorageFactory::create())
            .handler(SettingType::Unknown, Box::new(Handler::<Controller<C>>::spawn))
            .agents(&[restore_agent::blueprint::create()])
            .settings(&[SettingType::Unknown])
            .spawn_nested(ENV_NAME)
            .await
            .is_ok()
    );
}

async fn verify_data_handler<C: Control + Sync + Send + 'static>(should_succeed: bool) {
    assert_eq!(
        should_succeed,
        EnvironmentBuilder::new(InMemoryStorageFactory::create())
            .handler(
                SettingType::Unknown,
                Box::new(
                    DataHandler::<DoNotDisturbInfo, DataController<C, DoNotDisturbInfo>>::spawn,
                ),
            )
            .agents(&[restore_agent::blueprint::create()])
            .settings(&[SettingType::Unknown])
            .spawn_nested(ENV_NAME)
            .await
            .is_ok()
    );
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
    async fn handle(&self, _: SettingRequest) -> Option<SettingResponseResult> {
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
    async fn handle(&self, _: SettingRequest) -> Option<SettingResponseResult> {
        return None;
    }

    async fn change_state(&mut self, _: State) -> Option<ControllerStateResult> {
        return None;
    }
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_event_propagation() {
    let factory = message::create_hub();
    let setting_type = SettingType::Unknown;

    let (messenger, _) =
        factory.create(MessengerType::Addressable(Address::Registry)).await.unwrap();
    let (event_tx, mut event_rx) = unbounded::<State>();
    let (invocations_tx, _invocations_rx) = unbounded::<HashMap<State, u8>>();
    let (handler_messenger, handler_receptor) =
        factory.create(MessengerType::Unbound).await.unwrap();
    let signature = handler_messenger.get_signature();
    let context = ContextBuilder::new(
        setting_type,
        InMemoryStorageFactory::create(),
        handler_messenger,
        handler_receptor,
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
            Payload::Command(Command::ChangeState(State::Startup)),
            Audience::Messenger(signature.clone()),
        )
        .send()
        .ack();

    assert_eq!(Some(State::Startup), event_rx.next().await);

    messenger
        .message(
            Payload::Command(Command::ChangeState(State::Listen)),
            Audience::Messenger(signature.clone()),
        )
        .send()
        .ack();

    assert_eq!(Some(State::Listen), event_rx.next().await);

    messenger
        .message(
            Payload::Command(Command::ChangeState(State::EndListen)),
            Audience::Messenger(signature.clone()),
        )
        .send()
        .ack();

    assert_eq!(Some(State::EndListen), event_rx.next().await);

    messenger
        .message(
            Payload::Command(Command::ChangeState(State::Teardown)),
            Audience::Messenger(signature.clone()),
        )
        .send()
        .ack();

    assert_eq!(Some(State::Teardown), event_rx.next().await);
}

// Test that the controller state is entered [n] times.
async fn verify_controller_state(state: State, n: u8) {
    let factory = message::create_hub();
    let setting_type = SettingType::Audio;

    let (messenger, _) =
        factory.create(MessengerType::Addressable(Address::Registry)).await.unwrap();
    let (event_tx, mut event_rx) = unbounded::<State>();
    let (invocations_tx, mut invocations_rx) = unbounded::<HashMap<State, u8>>();
    let (handler_messenger, handler_receptor) =
        factory.create(MessengerType::Unbound).await.unwrap();
    let signature = handler_messenger.get_signature();
    let context = ContextBuilder::new(
        setting_type,
        InMemoryStorageFactory::create(),
        handler_messenger,
        handler_receptor,
        CONTEXT_ID,
    )
    .build();

    ClientImpl::create(context, StateController::create_generator(event_tx, invocations_tx))
        .await
        .expect("Unable to create ClientImpl");

    messenger
        .message(
            Payload::Command(Command::ChangeState(state)),
            Audience::Messenger(signature.clone()),
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

/// Empty controller that handles no commands or events.
/// TODO(fxb/50217): Clean up test controllers.
struct StubController {}

impl StubController {
    pub fn create_generator() -> GenerateController {
        Box::new(move |_| {
            Box::pin(async move { Ok(Box::new(StubController {}) as BoxedController) })
        })
    }
}

#[async_trait]
impl controller::Handle for StubController {
    async fn handle(&self, _: SettingRequest) -> Option<SettingResponseResult> {
        return None;
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
        let factory = message::create_hub();

        let (messenger, _) =
            factory.create(MessengerType::Addressable(Address::Registry)).await.unwrap();
        let (handler_messenger, handler_receptor) =
            factory.create(MessengerType::Unbound).await.unwrap();
        let signature = handler_messenger.get_signature();
        let context = ContextBuilder::new(
            setting_type,
            InMemoryStorageFactory::create(),
            handler_messenger,
            handler_receptor,
            CONTEXT_ID,
        )
        .build();

        assert!(ClientImpl::create(context, StubController::create_generator()).await.is_ok());

        let mut receptor = messenger
            .message(
                Payload::Command(Command::HandleRequest(SettingRequest::Get)),
                Audience::Messenger(signature),
            )
            .send();

        while let Some(message_event) = receptor.next().await {
            if let MessageEvent::Message(incoming_payload, _) = message_event {
                if let Payload::Result(Err(SwitchboardError::UnimplementedRequest {
                    setting_type: incoming_type,
                    request: _,
                })) = incoming_payload
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
