// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#[cfg(test)]
use {
    crate::agent::restore_agent::RestoreAgent,
    crate::internal::handler::{
        create_message_hub as create_setting_handler_message_hub, Address, Payload,
    },
    crate::message::base::{Audience, MessageEvent, MessengerType},
    crate::registry::base::{Command, ContextBuilder, State},
    crate::registry::device_storage::testing::*,
    crate::registry::setting_handler::{
        controller, persist::controller as data_controller,
        persist::ClientProxy as DataClientProxy, persist::Handler as DataHandler, persist::Storage,
        BoxedController, ClientImpl, ClientProxy, ControllerError, GenerateController, Handler,
    },
    crate::switchboard::base::{
        get_all_setting_types, DoNotDisturbInfo, SettingRequest, SettingResponseResult,
        SettingType, SwitchboardError,
    },
    crate::EnvironmentBuilder,
    async_trait::async_trait,
    futures::channel::mpsc::{unbounded, UnboundedSender},
    futures::StreamExt,
    std::marker::PhantomData,
    std::sync::Arc,
};

const ENV_NAME: &str = "settings_service_setting_handler_test_environment";

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

    async fn change_state(&mut self, _: State) {}
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

    async fn change_state(&mut self, _: State) {}
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_spawn() {
    // Exercises successful spawn of a simple controller.
    verify_handler::<SucceedControl>(true).await;
    // Exercises failed spawn of a simple controller.
    verify_handler::<FailControl>(false).await;
    // Exercises successful spawn of a data controller.
    verify_data_handler::<SucceedControl>(true).await;
    // Exercises failed spawn of a data controller.
    verify_data_handler::<FailControl>(false).await;
}

async fn verify_handler<C: Control + Sync + Send + 'static>(should_succeed: bool) {
    assert_eq!(
        should_succeed,
        EnvironmentBuilder::new(InMemoryStorageFactory::create())
            .handler(SettingType::Unknown, Box::new(Handler::<Controller<C>>::spawn))
            .agents(&[Arc::new(RestoreAgent::create)])
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
            .agents(&[Arc::new(RestoreAgent::create)])
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
}

impl StateController {
    pub fn create_generator(reporter: UnboundedSender<State>) -> GenerateController {
        Box::new(move |_| {
            let reporter = reporter.clone();
            Box::pin(async move {
                Ok(Box::new(StateController { state_reporter: reporter.clone() })
                    as BoxedController)
            })
        })
    }
}

#[async_trait]
impl controller::Handle for StateController {
    async fn handle(&self, _: SettingRequest) -> Option<SettingResponseResult> {
        return None;
    }

    async fn change_state(&mut self, state: State) {
        self.state_reporter.unbounded_send(state).ok();
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_event_propagation() {
    let factory = create_setting_handler_message_hub();
    let setting_type = SettingType::Unknown;

    let (messenger, _) =
        factory.create(MessengerType::Addressable(Address::Registry)).await.unwrap();
    let (event_tx, mut event_rx) = unbounded::<State>();
    let (handler_messenger, handler_receptor) =
        factory.create(MessengerType::Unbound).await.unwrap();
    let signature = handler_messenger.get_signature();
    let context = ContextBuilder::new(
        setting_type,
        InMemoryStorageFactory::create(),
        handler_messenger,
        handler_receptor,
    )
    .build();

    assert!(ClientImpl::create(context, StateController::create_generator(event_tx)).await.is_ok());

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

    async fn change_state(&mut self, _: State) {}
}

/// Ensures that the correct unimplemented error is returned when the controller
/// doesn't properly handle a given command.
#[fuchsia_async::run_singlethreaded(test)]
async fn test_unimplemented_error() {
    for setting_type in get_all_setting_types() {
        let factory = create_setting_handler_message_hub();

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
        )
        .build();

        assert!(ClientImpl::create(context, StubController::create_generator()).await.is_ok());

        let mut receptor = messenger
            .message(
                Payload::Command(Command::HandleRequest(SettingRequest::Get)),
                Audience::Messenger(signature),
            )
            .send();

        while let Ok(message_event) = receptor.watch().await {
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
