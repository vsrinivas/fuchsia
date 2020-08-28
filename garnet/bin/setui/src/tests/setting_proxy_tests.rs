// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;
use std::sync::Arc;

use fuchsia_async as fasync;
use futures::channel::mpsc::{UnboundedReceiver, UnboundedSender};
use futures::channel::oneshot;
use futures::lock::Mutex;
use futures::StreamExt;

use async_trait::async_trait;

use crate::handler::base::{
    Command, SettingHandlerFactory, SettingHandlerFactoryError, SettingHandlerResult, State,
};
use crate::handler::setting_handler::ControllerError;
use crate::handler::setting_proxy::SettingProxy;
use crate::internal::core::message::{create_hub, Messenger, Receptor};
use crate::internal::core::{self, Address, Payload};
use crate::internal::event;
use crate::internal::handler;
use crate::message::base::{Audience, MessageEvent, MessengerType};
use crate::message::receptor::Receptor as BaseReceptor;
use crate::switchboard::base::{
    SettingAction, SettingActionData, SettingEvent, SettingRequest, SettingType,
};

const SETTING_PROXY_MAX_ATTEMPTS: u64 = 3;

pub type SwitchboardReceptor = BaseReceptor<Payload, Address>;

struct SettingHandler {
    setting_type: SettingType,
    messenger: handler::message::Messenger,
    state_tx: UnboundedSender<State>,
    responses: Vec<(SettingRequest, SettingHandlerResult)>,
    done_tx: Option<oneshot::Sender<()>>,
    proxy_signature: handler::message::Signature,
}

impl SettingHandler {
    fn process_state(&mut self, state: State) -> SettingHandlerResult {
        self.state_tx.unbounded_send(state).ok();
        Ok(None)
    }

    pub fn queue_response(&mut self, request: SettingRequest, response: SettingHandlerResult) {
        self.responses.push((request, response));
    }

    pub fn notify(&self) {
        self.messenger
            .message(
                handler::Payload::Changed(self.setting_type),
                Audience::Messenger(self.proxy_signature),
            )
            .send()
            .ack();
    }

    fn process_request(&mut self, request: SettingRequest) -> SettingHandlerResult {
        if let Some((match_request, result)) = self.responses.pop() {
            if request == match_request {
                return result;
            }
        }

        Err(ControllerError::UnimplementedRequest(self.setting_type, request))
    }

    fn create(
        messenger: handler::message::Messenger,
        mut receptor: handler::message::Receptor,
        proxy_signature: handler::message::Signature,
        setting_type: SettingType,
        state_tx: UnboundedSender<State>,
        done_tx: Option<oneshot::Sender<()>>,
    ) -> Arc<Mutex<Self>> {
        let handler = Arc::new(Mutex::new(Self {
            messenger,
            setting_type,
            state_tx,
            responses: vec![],
            done_tx,
            proxy_signature,
        }));

        let handler_clone = handler.clone();
        fasync::Task::spawn(async move {
            while let Some(event) = receptor.next().await {
                match event {
                    MessageEvent::Message(
                        handler::Payload::Command(Command::HandleRequest(request)),
                        client,
                    ) => {
                        handler::reply(client, handler_clone.lock().await.process_request(request));
                    }
                    MessageEvent::Message(
                        handler::Payload::Command(Command::ChangeState(state)),
                        client,
                    ) => {
                        handler::reply(client, handler_clone.lock().await.process_state(state));
                    }
                    _ => {}
                }
            }

            if let Some(done_tx) = handler_clone.lock().await.done_tx.take() {
                done_tx.send(()).ok();
            }
        })
        .detach();

        handler
    }
}

struct FakeFactory {
    handlers: HashMap<SettingType, handler::message::Signature>,
    request_counts: HashMap<SettingType, u64>,
    messenger_factory: handler::message::Factory,
}

impl FakeFactory {
    pub fn new(messenger_factory: handler::message::Factory) -> Self {
        FakeFactory {
            handlers: HashMap::new(),
            request_counts: HashMap::new(),
            messenger_factory: messenger_factory,
        }
    }

    pub async fn create(
        &mut self,
        setting_type: SettingType,
    ) -> (handler::message::Messenger, handler::message::Receptor) {
        let (client, receptor) =
            self.messenger_factory.create(MessengerType::Unbound).await.unwrap();
        self.handlers.insert(setting_type, client.get_signature());

        (client, receptor)
    }

    pub fn get_request_count(&mut self, setting_type: SettingType) -> u64 {
        if let Some(count) = self.request_counts.get(&setting_type) {
            *count
        } else {
            0
        }
    }
}

#[async_trait]
impl SettingHandlerFactory for FakeFactory {
    async fn generate(
        &mut self,
        setting_type: SettingType,
        _: handler::message::Factory,
        _: handler::message::Signature,
    ) -> Result<handler::message::Signature, SettingHandlerFactoryError> {
        let existing_count = self.get_request_count(setting_type);

        Ok(self
            .handlers
            .get(&setting_type)
            .copied()
            .map(|signature| {
                self.request_counts.insert(setting_type, existing_count + 1);
                signature
            })
            .unwrap())
    }
}

pub struct TestEnvironment {
    proxy_signature: core::message::Signature,
    proxy_handler_signature: handler::message::Signature,
    messenger_client: Messenger,
    messenger_receptor: Receptor,
    handler_factory: Arc<Mutex<FakeFactory>>,
    setting_handler_rx: UnboundedReceiver<State>,
    setting_handler: Arc<Mutex<SettingHandler>>,
    setting_type: SettingType,
    event_factory: event::message::Factory,
}

impl TestEnvironment {
    async fn regenerate_handler(&mut self, done_tx: Option<oneshot::Sender<()>>) {
        let (handler_messenger, handler_receptor) =
            self.handler_factory.lock().await.create(self.setting_type).await;
        let (state_tx, state_rx) = futures::channel::mpsc::unbounded::<State>();
        self.setting_handler = SettingHandler::create(
            handler_messenger,
            handler_receptor,
            self.proxy_handler_signature,
            self.setting_type,
            state_tx,
            done_tx,
        );

        self.setting_handler_rx = state_rx;
    }
}

/// Generates a setting proxy, switchboard, and setting handler for testing.
///
/// If done_tx is provided, it will be passed to the created setting handler.
async fn create_test_environment(
    setting_type: SettingType,
    done_tx: Option<oneshot::Sender<()>>,
) -> TestEnvironment {
    let messenger_factory = create_hub();
    let handler_messenger_factory = handler::message::create_hub();
    let event_messenger_factory = event::message::create_hub();

    let handler_factory = Arc::new(Mutex::new(FakeFactory::new(handler_messenger_factory.clone())));

    let (proxy_signature, proxy_handler_signature) = SettingProxy::create(
        setting_type,
        handler_factory.clone(),
        messenger_factory.clone(),
        handler_messenger_factory,
        event_messenger_factory.clone(),
        SETTING_PROXY_MAX_ATTEMPTS,
    )
    .await
    .expect("proxy creation should succeed");
    let (messenger_client, receptor) =
        messenger_factory.create(MessengerType::Addressable(Address::Switchboard)).await.unwrap();

    let (handler_messenger, handler_receptor) =
        handler_factory.lock().await.create(setting_type).await;
    let (state_tx, state_rx) = futures::channel::mpsc::unbounded::<State>();
    let handler = SettingHandler::create(
        handler_messenger,
        handler_receptor,
        proxy_handler_signature,
        setting_type,
        state_tx,
        done_tx,
    );

    TestEnvironment {
        proxy_signature,
        proxy_handler_signature,
        messenger_client,
        messenger_receptor: receptor,
        handler_factory,
        setting_handler_rx: state_rx,
        setting_handler: handler,
        event_factory: event_messenger_factory,
        setting_type,
    }
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_notify() {
    let setting_type = SettingType::Unknown;
    let mut environment = create_test_environment(setting_type, None).await;

    // Send a listen state and make sure sink is notified.
    {
        assert!(environment
            .messenger_client
            .message(
                Payload::Action(SettingAction {
                    id: 1,
                    setting_type,
                    data: SettingActionData::Listen(1),
                }),
                Audience::Messenger(environment.proxy_signature),
            )
            .send()
            .wait_for_acknowledge()
            .await
            .is_ok());

        environment.setting_handler.lock().await.notify();

        while let Some(event) = environment.messenger_receptor.next().await {
            if let MessageEvent::Message(Payload::Event(SettingEvent::Changed(changed_type)), _) =
                event
            {
                assert_eq!(changed_type, setting_type);
                break;
            }
        }
    }

    if let Some(state) = environment.setting_handler_rx.next().await {
        assert_eq!(state, State::Listen);
    } else {
        panic!("should have received state update");
    }

    // Send an end listen state and make sure sink is notified.
    {
        environment
            .messenger_client
            .message(
                Payload::Action(SettingAction {
                    id: 1,
                    setting_type,
                    data: SettingActionData::Listen(0),
                }),
                Audience::Messenger(environment.proxy_signature),
            )
            .send()
            .ack();
    }

    if let Some(state) = environment.setting_handler_rx.next().await {
        assert_eq!(state, State::EndListen);
    } else {
        panic!("should have received EndListen state update");
    }

    if let Some(state) = environment.setting_handler_rx.next().await {
        assert_eq!(state, State::Teardown);
    } else {
        panic!("should have received Teardown state update");
    }
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_request() {
    let setting_type = SettingType::Unknown;
    let request_id = 42;
    let environment = create_test_environment(setting_type, None).await;

    environment.setting_handler.lock().await.queue_response(SettingRequest::Get, Ok(None));

    // Send initial request.
    let mut receptor = environment
        .messenger_client
        .message(
            Payload::Action(SettingAction {
                id: request_id,
                setting_type,
                data: SettingActionData::Request(SettingRequest::Get),
            }),
            Audience::Messenger(environment.proxy_signature),
        )
        .send();

    while let Some(event) = receptor.next().await {
        if let MessageEvent::Message(
            Payload::Event(SettingEvent::Response(response_id, response)),
            _,
        ) = event
        {
            assert_eq!(request_id, response_id);
            assert!(response.is_ok());
            assert_eq!(None, response.unwrap());
            return;
        }
    }
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_request_order() {
    let setting_type = SettingType::Unknown;
    let request_id_1 = 42;
    let request_id_2 = 43;
    let environment = create_test_environment(setting_type, None).await;

    environment.setting_handler.lock().await.queue_response(SettingRequest::Get, Ok(None));

    // Send multiple requests.
    let receptor_1 = environment
        .messenger_client
        .message(
            Payload::Action(SettingAction {
                id: request_id_1,
                setting_type,
                data: SettingActionData::Request(SettingRequest::Get),
            }),
            Audience::Messenger(environment.proxy_signature),
        )
        .send();
    let receptor_2 = environment
        .messenger_client
        .message(
            Payload::Action(SettingAction {
                id: request_id_2,
                setting_type,
                data: SettingActionData::Request(SettingRequest::Get),
            }),
            Audience::Messenger(environment.proxy_signature),
        )
        .send();

    // Wait for both requests to finish and add them to the list as they finish so we can verify the
    // order.
    let mut completed_request_ids = Vec::<u64>::new();
    let mut receptor_1_fuse = receptor_1.fuse();
    let mut receptor_2_fuse = receptor_2.fuse();
    loop {
        environment.setting_handler.lock().await.queue_response(SettingRequest::Get, Ok(None));
        futures::select! {
            payload_1 = receptor_1_fuse.next() => {
                if let Some(MessageEvent::Message(
                    Payload::Event(SettingEvent::Response(response_id, response)),
                    _,
                )) = payload_1
                {
                    assert_eq!(request_id_1, response_id);
                    // First request finishes first.
                    assert_eq!(completed_request_ids.len(), 0);
                    completed_request_ids.push(response_id);
                }
            },
            payload_2 = receptor_2_fuse.next() => {
                if let Some(MessageEvent::Message(
                    Payload::Event(SettingEvent::Response(response_id, response)),
                    _,
                )) = payload_2
                {
                    assert_eq!(request_id_2, response_id);
                    // Second request finishes second.
                    assert_eq!(completed_request_ids.len(), 1);
                    completed_request_ids.push(response_id);
                }
            },
            complete => break,
        }
    }

    assert_eq!(completed_request_ids.len(), 2);
    assert_eq!(completed_request_ids[0], request_id_1);
    assert_eq!(completed_request_ids[1], request_id_2);
}

/// Ensures setting handler is only generated once if never torn down.
#[fuchsia_async::run_until_stalled(test)]
async fn test_generation() {
    let setting_type = SettingType::Unknown;
    let request_id = 42;
    let environment = create_test_environment(setting_type, None).await;

    // Send initial request.
    let _ = get_response(
        environment
            .messenger_client
            .message(
                Payload::Action(SettingAction {
                    id: request_id,
                    setting_type,
                    data: SettingActionData::Listen(1),
                }),
                Audience::Messenger(environment.proxy_signature),
            )
            .send(),
    )
    .await;

    // Ensure the handler was only created once.
    assert_eq!(1, environment.handler_factory.lock().await.get_request_count(setting_type));

    // Send followup request.
    let _ = get_response(
        environment
            .messenger_client
            .message(
                Payload::Action(SettingAction {
                    id: request_id,
                    setting_type,
                    data: SettingActionData::Request(SettingRequest::Get),
                }),
                Audience::Messenger(environment.proxy_signature),
            )
            .send(),
    )
    .await;

    // Make sure no followup generation was invoked.
    assert_eq!(1, environment.handler_factory.lock().await.get_request_count(setting_type));
}

/// Ensures setting handler is generated multiple times successfully if torn down.
#[fuchsia_async::run_until_stalled(test)]
async fn test_regeneration() {
    let setting_type = SettingType::Unknown;
    let request_id = 42;
    let (done_tx, done_rx) = oneshot::channel();
    let mut environment = create_test_environment(setting_type, Some(done_tx)).await;

    // Send initial request.
    assert!(
        get_response(
            environment
                .messenger_client
                .message(
                    Payload::Action(SettingAction {
                        id: request_id,
                        setting_type,
                        data: SettingActionData::Request(SettingRequest::Get),
                    }),
                    Audience::Messenger(environment.proxy_signature),
                )
                .send(),
        )
        .await
        .is_some(),
        "A response was expected"
    );

    // Ensure the handler was only created once.
    assert_eq!(1, environment.handler_factory.lock().await.get_request_count(setting_type));

    // The subsequent teardown should happen here.
    done_rx.await.ok();
    let mut hit_teardown = false;
    loop {
        let state = environment.setting_handler_rx.next().await;
        match state {
            Some(State::Teardown) => {
                hit_teardown = true;
                break;
            }
            None => break,
            _ => {}
        }
    }
    assert!(hit_teardown, "Handler should have torn down");
    drop(environment.setting_handler);

    // Now that the handler is dropped, the setting_handler_tx should be dropped too and the rx end
    // will return none.
    assert!(
        environment.setting_handler_rx.next().await.is_none(),
        "There should be no more states after teardown"
    );

    let (handler_messenger, handler_receptor) =
        environment.handler_factory.lock().await.create(setting_type).await;
    let (state_tx, _) = futures::channel::mpsc::unbounded::<State>();
    let _handler = SettingHandler::create(
        handler_messenger,
        handler_receptor,
        environment.proxy_handler_signature,
        setting_type,
        state_tx,
        None,
    );

    // Send followup request.
    assert!(
        get_response(
            environment
                .messenger_client
                .message(
                    Payload::Action(SettingAction {
                        id: request_id,
                        setting_type,
                        data: SettingActionData::Request(SettingRequest::Get),
                    }),
                    Audience::Messenger(environment.proxy_signature),
                )
                .send(),
        )
        .await
        .is_some(),
        "A response was expected"
    );

    // Check that the handler was re-generated.
    assert_eq!(2, environment.handler_factory.lock().await.get_request_count(setting_type));
}

/// Exercises the retry flow, ensuring the setting proxy goes through the
/// defined number of tests and correctly reports back activity.
#[fuchsia_async::run_until_stalled(test)]
async fn test_retry() {
    let setting_type = SettingType::Unknown;
    let mut environment = create_test_environment(setting_type, None).await;

    let (_, mut event_receptor) = environment
        .event_factory
        .create(MessengerType::Unbound)
        .await
        .expect("Should be able to retrieve receptor");

    // Queue up external failure responses in the handler.
    for _ in 0..SETTING_PROXY_MAX_ATTEMPTS {
        environment.setting_handler.lock().await.queue_response(
            SettingRequest::Get,
            Err(ControllerError::ExternalFailure(
                setting_type,
                "test_commponent".into(),
                "connect".into(),
            )),
        );
    }

    let request_id = 2;
    let request = SettingRequest::Get;

    // Send request.
    let (returned_request_id, handler_result) = get_response(
        environment
            .messenger_client
            .message(
                Payload::Action(SettingAction {
                    id: request_id,
                    setting_type,
                    data: SettingActionData::Request(request.clone()),
                }),
                Audience::Messenger(environment.proxy_signature),
            )
            .send(),
    )
    .await
    .expect("result should be present");

    // Ensure returned request id matches the outgoing id
    assert_eq!(request_id, returned_request_id);

    // Make sure the result is an `ControllerError::IrrecoverableError`
    if let Err(error) = handler_result {
        assert_eq!(error, ControllerError::IrrecoverableError);
    } else {
        panic!("error should have been encountered");
    }

    // For each failed attempt, make sure a retry event was broadcasted
    for _ in 0..SETTING_PROXY_MAX_ATTEMPTS {
        verify_handler_event(
            event_receptor.next().await.expect("should be notified of external failure"),
            event::handler::Event::Retry(setting_type, request.clone()),
        );
    }

    // Ensure that the final event reports that attempts were exceeded
    verify_handler_event(
        event_receptor.next().await.expect("should be notified of external failure"),
        event::handler::Event::AttemptsExceeded(setting_type, request.clone()),
    );

    // Regenerate setting handler
    environment.regenerate_handler(None).await;

    // Queue successful response
    environment.setting_handler.lock().await.queue_response(SettingRequest::Get, Ok(None));

    // Ensure subsequent request succeeds
    assert!(get_response(
        environment
            .messenger_client
            .message(
                Payload::Action(SettingAction {
                    id: request_id,
                    setting_type,
                    data: SettingActionData::Request(request.clone()),
                }),
                Audience::Messenger(environment.proxy_signature),
            )
            .send(),
    )
    .await
    .expect("result should be present")
    .1
    .is_ok());
}

/// Checks that the supplied message event specifies the supplied handler event.
fn verify_handler_event(
    message_event: MessageEvent<event::Payload, event::Address>,
    event: event::handler::Event,
) {
    if let MessageEvent::Message(event::Payload::Event(event::Event::Handler(captured_event)), _) =
        message_event
    {
        assert_eq!(event, captured_event);
        return;
    }

    panic!("should have matched the provided event");
}

async fn get_response(mut receptor: SwitchboardReceptor) -> Option<(u64, SettingHandlerResult)> {
    while let Some(event) = receptor.next().await {
        if let MessageEvent::Message(
            Payload::Event(SettingEvent::Response(response_id, response)),
            _,
        ) = event
        {
            return Some((response_id, response));
        }
    }

    return None;
}
