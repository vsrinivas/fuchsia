// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::internal::switchboard,
    crate::message::base::Audience,
    crate::message::receptor::extract_payload,
    crate::switchboard::base::{SettingRequest, SettingResponse, SettingType, SwitchboardError},
    anyhow::Error,
    fuchsia_async as fasync,
    futures::channel::mpsc::UnboundedSender,
    futures::lock::Mutex,
    futures::stream::StreamExt,
    futures::FutureExt,
    std::collections::HashMap,
    std::hash::Hash,
    std::marker::PhantomData,
    std::sync::Arc,
};

type ChangeFunction<T> = Box<dyn Fn(&T, &T) -> bool + Send + Sync + 'static>;

/// Controller that determines whether or not a change should be sent to the
/// hanging get. T is the type of the value to be watched and sent to back to
/// the client via the sender ST.
struct HangingGetController<T, ST>
where
    T: From<SettingResponse> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
{
    /// The last value that was sent to the client
    last_sent_value: Option<T>,
    /// Function called on change. If function returns true, tells the
    /// handler that it should send to the hanging get.
    change_function: ChangeFunction<T>,
    /// If true, should send value next time watch
    /// is called or if there is a hanging watch.
    should_send: bool,
    /// List of responders to send the changed value to.
    pending_responders: Vec<(ST, Option<UnboundedSender<()>>)>,
}

impl<T, ST> HangingGetController<T, ST>
where
    T: From<SettingResponse> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
{
    fn new(change_function: ChangeFunction<T>) -> HangingGetController<T, ST> {
        let controller = HangingGetController {
            last_sent_value: None,
            change_function,
            should_send: true,
            pending_responders: Vec::new(),
        };

        controller
    }

    fn initialize(&mut self) {
        self.last_sent_value = None;
        self.should_send = true;
        self.change_function = Box::new(|_old: &T, _new: &T| true);
    }

    /// Add a pending responder that wants to be notified of the next value change.
    fn add_pending_responder(&mut self, responder: ST, error_sender: Option<UnboundedSender<()>>) {
        self.pending_responders.push((responder, error_sender));
    }

    fn on_error(&mut self, error: &Error) {
        self.initialize();

        // Notify responders of error.
        while let Some((responder, optional_exit_tx)) = self.pending_responders.pop() {
            responder.on_error(&error);
            if let Some(exit_tx) = optional_exit_tx {
                exit_tx.unbounded_send(()).ok();
            }
        }
    }

    /// Should be called whenever the underlying value changes.
    fn on_change(&mut self, new_value: &T) -> bool {
        self.should_send = match self.last_sent_value.as_ref() {
            Some(last_value) => (self.change_function)(&last_value, new_value),
            None => true,
        };
        self.should_send
    }

    /// Should be called to check if we should immediately return the hanging
    /// get.
    fn on_watch(&self) -> bool {
        self.should_send
    }

    /// Called when receiving a notification that value has changed.
    async fn send_if_needed(&mut self, response: SettingResponse) {
        if !self.pending_responders.is_empty() {
            while let Some((responder, _)) = self.pending_responders.pop() {
                responder.send_response(T::from(response.clone()));
            }
            self.on_send(T::from(response));
        }
    }

    /// Should be called whenever a value is sent to the hanging get.
    fn on_send(&mut self, new_value: T) {
        self.last_sent_value = Some(new_value);
        self.should_send = false;
    }
}

/// Handler for hanging gets within the switchboard.
/// We never use the data type T directly, but it is used to constrain ST as the sender
/// for that type.
/// To use, one should implement a sender, as well as a way to convert SettingResponse into
/// something that sender can use.
/// K is the type of the key for the change_function.
pub struct HangingGetHandler<T, ST, K>
where
    T: From<SettingResponse> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
    K: Eq + Hash + Clone + Send + Sync + 'static,
{
    switchboard_messenger: switchboard::message::Messenger,
    listen_exit_tx: Option<UnboundedSender<()>>,
    data_type: PhantomData<T>,
    setting_type: SettingType,

    // This controller is used for generic watch calls without type parameters.
    default_controller: HangingGetController<T, ST>,

    // Controllers for watch calls with type parameters are stored here.
    controllers_by_key: HashMap<K, HangingGetController<T, ST>>,
    command_tx: UnboundedSender<ListenCommand>,
}

/// Trait that should be implemented to send data to the hanging get watcher.
pub trait Sender<T> {
    fn send_response(self, data: T);
    fn on_error(self, error: &Error);
}

enum ListenCommand {
    Change(SettingType),
    Exit,
}

// Generates a pattern that pulls a response out of a SwitchBoardAction.
macro_rules! switchboard_action_response {
    ($action:pat) => {
        Ok((switchboard::Payload::Action(switchboard::Action::Response($action)), _))
    };
}

impl<T, ST, K> Drop for HangingGetHandler<T, ST, K>
where
    T: From<SettingResponse> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
    K: Eq + Hash + Clone + Send + Sync + 'static,
{
    fn drop(&mut self) {
        self.close();
    }
}

impl<T, ST, K> HangingGetHandler<T, ST, K>
where
    T: From<SettingResponse> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
    K: Eq + Hash + Clone + Send + Sync + 'static,
{
    pub async fn create(
        switchboard_messenger: switchboard::message::Messenger,
        setting_type: SettingType,
    ) -> Arc<Mutex<HangingGetHandler<T, ST, K>>> {
        let (on_command_sender, mut on_command_receiver) =
            futures::channel::mpsc::unbounded::<ListenCommand>();
        let hanging_get_handler = Arc::new(Mutex::new(HangingGetHandler::<T, ST, K> {
            switchboard_messenger,
            listen_exit_tx: None,
            data_type: PhantomData,
            setting_type,
            default_controller: HangingGetController::new(Box::new(|_old: &T, _new: &T| true)),
            controllers_by_key: Default::default(),
            command_tx: on_command_sender.clone(),
        }));

        {
            let hanging_get_handler_clone = hanging_get_handler.clone();
            fasync::Task::spawn(async move {
                while let Some(command) = on_command_receiver.next().await {
                    match command {
                        ListenCommand::Change(setting_type) => {
                            assert_eq!(setting_type, setting_type);
                            let mut handler_lock = hanging_get_handler_clone.lock().await;
                            handler_lock.on_change().await;
                        }
                        ListenCommand::Exit => {
                            return;
                        }
                    }
                }
            })
            .detach();
        }

        hanging_get_handler
    }

    pub fn close(&mut self) {
        if let Some(exit_tx) = self.listen_exit_tx.take() {
            exit_tx.unbounded_send(()).ok();
        }

        self.command_tx.unbounded_send(ListenCommand::Exit).ok();
    }

    /// Park a new hanging get in the handler
    pub async fn watch(&mut self, responder: ST, error_sender: Option<UnboundedSender<()>>) {
        self.watch_with_change_fn(
            None,
            Box::new(|_old: &T, _new: &T| true),
            responder,
            error_sender,
        )
        .await;
    }

    /// Park a new hanging get in the handler, along with a change function.
    /// The hanging get will only return when the change function evaluates
    /// to true when comparing the last value sent to the client and the current
    /// value obtained by the hanging_get_handler.
    /// A change function is applied on change only, and not on the current state.
    pub async fn watch_with_change_fn(
        &mut self,
        change_function_key: Option<K>,
        change_function: ChangeFunction<T>,
        responder: ST,
        error_sender: Option<UnboundedSender<()>>,
    ) {
        let controller = match change_function_key.clone() {
            None => &mut self.default_controller,
            Some(key) => self
                .controllers_by_key
                .entry(key)
                .or_insert(HangingGetController::new(change_function)),
        };

        controller.add_pending_responder(responder, error_sender);

        if self.listen_exit_tx.is_none() {
            let command_tx_clone = self.command_tx.clone();
            let mut receptor = self
                .switchboard_messenger
                .message(
                    switchboard::Payload::Listen(switchboard::Listen::Request(self.setting_type)),
                    Audience::Address(switchboard::Address::Switchboard),
                )
                .send();

            let (exit_tx, mut exit_rx) = futures::channel::mpsc::unbounded::<()>();
            self.listen_exit_tx = Some(exit_tx);

            fasync::Task::spawn(async move {
                loop {
                    let receptor_fuse = receptor.next().fuse();
                    futures::pin_mut!(receptor_fuse);

                    futures::select! {
                        update = receptor_fuse => {
                            if let Some(switchboard::Payload::Listen(switchboard::Listen::Update(setting))) = extract_payload(update) {
                                command_tx_clone.unbounded_send(ListenCommand::Change(setting)).ok();
                            }
                        }
                        exit = exit_rx.next() => {
                            return;
                        }
                    }
                }
            }).detach();
        }

        if !controller.on_watch() {
            // Value hasn't changed, no need to send to responder.
            return;
        }

        match self.get_response().await {
            Ok(response) => {
                // We have to borrow the controllers again since
                // self.get_response expects an immutable borrow, so we can't
                // use it in between uses of the local variable controller.
                match change_function_key {
                    None => self.default_controller.send_if_needed(response).await,
                    Some(key) => {
                        self.controllers_by_key
                            .get_mut(&key)
                            .unwrap()
                            .send_if_needed(response)
                            .await
                    }
                };
            }
            Err(error) => {
                self.on_error(&error);
            }
        }
    }

    /// Called when receiving a notification that value has changed.
    async fn on_change(&mut self) {
        match self.get_response().await {
            Ok(response) => {
                for controller in self.controllers_by_key.values_mut() {
                    if controller.on_change(&T::from(response.clone())) {
                        controller.send_if_needed(response.clone()).await;
                    }
                }
                if self.default_controller.on_change(&T::from(response.clone())) {
                    self.default_controller.send_if_needed(response).await;
                }
            }
            Err(error) => {
                self.on_error(&error);
            }
        }
    }

    fn on_error(&mut self, error: &Error) {
        if let Some(exit_tx) = self.listen_exit_tx.take() {
            exit_tx.unbounded_send(()).ok();
        }

        self.default_controller.on_error(&error);
        for controller in self.controllers_by_key.values_mut() {
            controller.on_error(&error);
        }
    }

    async fn get_response(&self) -> Result<SettingResponse, Error> {
        let mut receptor = self
            .switchboard_messenger
            .message(
                switchboard::Payload::Action(switchboard::Action::Request(
                    self.setting_type,
                    SettingRequest::Get,
                )),
                Audience::Address(switchboard::Address::Switchboard),
            )
            .send();

        match receptor.next_payload().await {
            switchboard_action_response!(Ok(Some(setting_response))) => Ok(setting_response),
            switchboard_action_response!(Err(err)) => Err(Error::new(err)),
            _ => Err(Error::new(SwitchboardError::UnexpectedError("Unexpected error".into()))),
        }
    }
}

#[cfg(test)]
mod tests {
    use fuchsia_async::DurationExt;
    use fuchsia_zircon as zx;
    use futures::channel::mpsc::UnboundedSender;
    use std::borrow::Cow;

    use crate::message::base::MessengerType;
    use crate::switchboard::base::{DisplayInfo, LowLightMode};

    use super::*;

    const ID1: f32 = 1.0;
    const ID2: f32 = 2.0;

    const SETTING_TYPE: SettingType = SettingType::Display;
    const SET_ERROR: &str = "set failure";

    #[derive(PartialEq, Debug, Clone)]
    struct TestStruct {
        id: f32,
    }

    #[derive(PartialEq, Debug, Clone)]
    enum Event {
        Data(TestStruct),
        SwitchboardError(SwitchboardError),
        UnknownError,
    }

    impl<C: Into<Cow<'static, str>>> From<C> for SwitchboardError {
        fn from(c: C) -> Self {
            SwitchboardError::UnexpectedError(c.into())
        }
    }

    struct TestSender {
        sender: UnboundedSender<Event>,
    }

    struct TestSwitchboardBuilder {
        id_to_send: Option<f32>,
        always_fail: bool,
        messenger_factory: switchboard::message::Factory,
    }

    impl TestSwitchboardBuilder {
        fn new(messenger_factory: switchboard::message::Factory) -> Self {
            Self { messenger_factory, id_to_send: None, always_fail: false }
        }

        fn set_initial_id(mut self, id: f32) -> Self {
            self.id_to_send = Some(id);
            self
        }

        fn set_always_fail(mut self, always_fail: bool) -> Self {
            self.always_fail = always_fail;
            self
        }

        async fn build(self) -> Arc<Mutex<TestSwitchboard>> {
            TestSwitchboard::create(self.messenger_factory, self.id_to_send, self.always_fail).await
        }
    }

    struct TestSwitchboard {
        id_to_send: Option<f32>,
        setting_type: Option<SettingType>,
        listener: Option<switchboard::message::Client>,
        always_fail: bool,
    }

    impl TestSwitchboard {
        async fn create(
            messenger_factory: switchboard::message::Factory,
            id_to_send: Option<f32>,
            always_fail: bool,
        ) -> Arc<Mutex<TestSwitchboard>> {
            let switchboard = Arc::new(Mutex::new(TestSwitchboard {
                id_to_send,
                setting_type: None,
                listener: None,
                always_fail: always_fail,
            }));

            let (_, mut receptor) = messenger_factory
                .create(MessengerType::Addressable(switchboard::Address::Switchboard))
                .await
                .unwrap();

            let switchboard_clone = switchboard.clone();
            fasync::Task::spawn(async move {
                while let Ok((payload, client)) = receptor.next_payload().await {
                    let mut switchboard = switchboard_clone.lock().await;
                    match payload {
                        switchboard::Payload::Action(switchboard::Action::Request(
                            setting_type,
                            request,
                        )) => {
                            switchboard.request(client, setting_type, request);
                        }
                        switchboard::Payload::Listen(switchboard::Listen::Request(
                            setting_type,
                        )) => {
                            switchboard.listen(client, setting_type);
                        }
                        _ => {
                            panic!("unexpected payload");
                        }
                    }
                }
            })
            .detach();

            switchboard
        }

        fn set_id(&mut self, id: f32) {
            self.id_to_send = Some(id);
        }

        fn notify_listener(&self) {
            if let Some(setting_type_value) = self.setting_type {
                if let Some(listener) = self.listener.clone() {
                    listener
                        .reply(switchboard::Payload::Listen(switchboard::Listen::Update(
                            setting_type_value,
                        )))
                        .send();
                    return;
                }
            }
            panic!("Missing listener to notify");
        }

        fn listen(&mut self, listener: switchboard::message::Client, setting_type: SettingType) {
            self.setting_type = Some(setting_type);
            self.listener = Some(listener);
        }

        fn request(
            &mut self,
            requestor: switchboard::message::Client,
            setting_type: SettingType,
            request: SettingRequest,
        ) {
            assert_eq!(setting_type, SETTING_TYPE);
            assert_eq!(request, SettingRequest::Get);

            let mut response = None;
            if self.always_fail {
                response = Some(Err(SwitchboardError::from(SET_ERROR)));
            } else if let Some(value) = self.id_to_send {
                response = Some(Ok(Some(SettingResponse::Brightness(DisplayInfo::new(
                    false,
                    value,
                    true,
                    LowLightMode::Disable,
                    None,
                )))));
            }

            if let Some(response) = response.take() {
                requestor
                    .reply(switchboard::Payload::Action(switchboard::Action::Response(response)))
                    .send();
            }
        }
    }

    impl Sender<TestStruct> for TestSender {
        fn send_response(self, data: TestStruct) {
            self.sender.unbounded_send(Event::Data(data)).unwrap();
        }

        fn on_error(self, error: &Error) {
            let error = match error.root_cause().downcast_ref::<SwitchboardError>() {
                Some(switchboard_error) => Event::SwitchboardError(switchboard_error.clone()),
                _ => Event::UnknownError,
            };
            self.sender.unbounded_send(error).unwrap();
        }
    }

    impl From<SettingResponse> for TestStruct {
        fn from(response: SettingResponse) -> Self {
            if let SettingResponse::Brightness(info) = response {
                return TestStruct { id: info.manual_brightness_value };
            }
            panic!("bad response");
        }
    }

    fn verify_id(event: Event, id: f32) {
        if let Event::Data(data) = event {
            assert_eq!(data.id, id);
        } else {
            panic!("Should be data {:?}", event);
        }
    }

    /// Ensures errors are gracefully handed back by the hanging_get
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_error_resolution() {
        let switchboard_messenger_factory = switchboard::message::create_hub();
        let _ = TestSwitchboardBuilder::new(switchboard_messenger_factory.clone())
            .set_always_fail(true)
            .build()
            .await;

        let hanging_get_handler: Arc<Mutex<HangingGetHandler<TestStruct, TestSender, String>>> =
            HangingGetHandler::create(
                switchboard_messenger_factory.create(MessengerType::Unbound).await.unwrap().0,
                SettingType::Display,
            )
            .await;

        let (hanging_get_responder, mut hanging_get_listener) =
            futures::channel::mpsc::unbounded::<Event>();

        let (exit_tx, mut exit_rx) = futures::channel::mpsc::unbounded::<()>();

        hanging_get_handler
            .lock()
            .await
            .watch(TestSender { sender: hanging_get_responder.clone() }, Some(exit_tx))
            .await;

        // The responder should receive an error
        assert_eq!(
            hanging_get_listener.next().await.unwrap(),
            Event::SwitchboardError(SwitchboardError::from(SET_ERROR))
        );

        // When set, the exit sender should also be fired
        assert_eq!(exit_rx.next().await, Some(()));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_change_after_watch() {
        let switchboard_messenger_factory = switchboard::message::create_hub();

        let switchboard_handle = TestSwitchboardBuilder::new(switchboard_messenger_factory.clone())
            .set_initial_id(ID1)
            .build()
            .await;

        let hanging_get_handler: Arc<Mutex<HangingGetHandler<TestStruct, TestSender, String>>> =
            HangingGetHandler::create(
                switchboard_messenger_factory.create(MessengerType::Unbound).await.unwrap().0,
                SettingType::Display,
            )
            .await;

        let (hanging_get_responder, mut hanging_get_listener) =
            futures::channel::mpsc::unbounded::<Event>();

        hanging_get_handler
            .lock()
            .await
            .watch(TestSender { sender: hanging_get_responder.clone() }, None)
            .await;

        // First get should return immediately
        verify_id(hanging_get_listener.next().await.unwrap(), ID1);

        // Subsequent one should wait until new value is notified
        hanging_get_handler
            .lock()
            .await
            .watch(TestSender { sender: hanging_get_responder.clone() }, None)
            .await;

        switchboard_handle.lock().await.set_id(ID2);

        switchboard_handle.lock().await.notify_listener();

        verify_id(hanging_get_listener.next().await.unwrap(), ID2);
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_watch_after_change() {
        let switchboard_messenger_factory = switchboard::message::create_hub();
        let switchboard_handle = TestSwitchboardBuilder::new(switchboard_messenger_factory.clone())
            .set_initial_id(ID1)
            .build()
            .await;

        let hanging_get_handler: Arc<Mutex<HangingGetHandler<TestStruct, TestSender, String>>> =
            HangingGetHandler::create(
                switchboard_messenger_factory.create(MessengerType::Unbound).await.unwrap().0,
                SettingType::Display,
            )
            .await;

        let (hanging_get_responder, mut hanging_get_listener) =
            futures::channel::mpsc::unbounded::<Event>();

        hanging_get_handler
            .lock()
            .await
            .watch(TestSender { sender: hanging_get_responder.clone() }, None)
            .await;

        // First get should return immediately
        verify_id(hanging_get_listener.next().await.unwrap(), ID1);

        switchboard_handle.lock().await.set_id(ID2);

        switchboard_handle.lock().await.notify_listener();

        // Subsequent one should wait until new value is notified
        hanging_get_handler
            .lock()
            .await
            .watch(TestSender { sender: hanging_get_responder.clone() }, None)
            .await;

        verify_id(hanging_get_listener.next().await.unwrap(), ID2);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_watch_with_change_function() {
        let switchboard_messenger_factory = switchboard::message::create_hub();
        let switchboard_handle = TestSwitchboardBuilder::new(switchboard_messenger_factory.clone())
            .set_initial_id(ID1)
            .build()
            .await;

        let hanging_get_handler: Arc<Mutex<HangingGetHandler<TestStruct, TestSender, String>>> =
            HangingGetHandler::create(
                switchboard_messenger_factory.create(MessengerType::Unbound).await.unwrap().0,
                SettingType::Display,
            )
            .await;

        let (hanging_get_responder, mut hanging_get_listener) =
            futures::channel::mpsc::unbounded::<Event>();

        let min_difference = 2.0;
        let change_function =
            move |old: &TestStruct, new: &TestStruct| -> bool { new.id - old.id > min_difference };

        hanging_get_handler
            .lock()
            .await
            .watch_with_change_fn(
                Some(min_difference.to_string()),
                Box::new(change_function),
                TestSender { sender: hanging_get_responder.clone() },
                None,
            )
            .await;

        // First get should return immediately even with change function
        verify_id(hanging_get_listener.next().await.unwrap(), ID1);

        switchboard_handle.lock().await.set_id(ID2);

        switchboard_handle.lock().await.notify_listener();

        // Subsequent watch should return ignoring change function
        hanging_get_handler
            .lock()
            .await
            .watch(TestSender { sender: hanging_get_responder.clone() }, None)
            .await;

        verify_id(hanging_get_listener.next().await.unwrap(), ID2);

        // Subsequent watch with change function should only return if change is big enough
        switchboard_handle.lock().await.set_id(ID2 + 1.0);

        switchboard_handle.lock().await.notify_listener();

        hanging_get_handler
            .lock()
            .await
            .watch_with_change_fn(
                Some(min_difference.to_string()),
                Box::new(change_function),
                TestSender { sender: hanging_get_responder.clone() },
                None,
            )
            .await;

        // Must wait for some a short time to allow change to propagate.
        let sleep_duration = zx::Duration::from_millis(1);
        fasync::Timer::new(sleep_duration.after_now()).await;

        switchboard_handle.lock().await.set_id(ID2 + 3.0);

        switchboard_handle.lock().await.notify_listener();

        verify_id(hanging_get_listener.next().await.unwrap(), ID2 + 3.0);
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_watch_with_change_function_multiple() {
        let switchboard_messenger_factory = switchboard::message::create_hub();
        let switchboard_handle = TestSwitchboardBuilder::new(switchboard_messenger_factory.clone())
            .set_initial_id(ID1)
            .build()
            .await;

        let hanging_get_handler: Arc<Mutex<HangingGetHandler<TestStruct, TestSender, String>>> =
            HangingGetHandler::create(
                switchboard_messenger_factory.create(MessengerType::Unbound).await.unwrap().0,
                SettingType::Display,
            )
            .await;

        // Register first change function with a large min difference.
        let (hanging_get_responder, mut hanging_get_listener) =
            futures::channel::mpsc::unbounded::<Event>();
        let min_difference = 10.0;
        let change_function =
            move |old: &TestStruct, new: &TestStruct| -> bool { new.id - old.id >= min_difference };

        hanging_get_handler
            .lock()
            .await
            .watch_with_change_fn(
                Some(min_difference.to_string()),
                Box::new(change_function),
                TestSender { sender: hanging_get_responder.clone() },
                None,
            )
            .await;

        // Register second change function with a smaller min difference.
        let (hanging_get_responder2, mut hanging_get_listener2) =
            futures::channel::mpsc::unbounded::<Event>();
        let min_difference2 = 1.0;
        let change_function2 = move |old: &TestStruct, new: &TestStruct| -> bool {
            new.id - old.id >= min_difference2
        };

        hanging_get_handler
            .lock()
            .await
            .watch_with_change_fn(
                Some(min_difference2.to_string()),
                Box::new(change_function2),
                TestSender { sender: hanging_get_responder2.clone() },
                None,
            )
            .await;

        // First get should return immediately even with change function
        verify_id(hanging_get_listener.next().await.unwrap(), ID1);
        verify_id(hanging_get_listener2.next().await.unwrap(), ID1);

        // Register listeners again.
        hanging_get_handler
            .lock()
            .await
            .watch_with_change_fn(
                Some(min_difference.to_string()),
                Box::new(change_function),
                TestSender { sender: hanging_get_responder.clone() },
                None,
            )
            .await;
        hanging_get_handler
            .lock()
            .await
            .watch_with_change_fn(
                Some(min_difference2.to_string()),
                Box::new(change_function2),
                TestSender { sender: hanging_get_responder2.clone() },
                None,
            )
            .await;

        // Send a value big enough to trigger the smaller change function but not the larger one.
        switchboard_handle.lock().await.set_id(ID2);
        switchboard_handle.lock().await.notify_listener();

        verify_id(hanging_get_listener2.next().await.unwrap(), ID2);

        // Re-register the listener that just finished.
        hanging_get_handler
            .lock()
            .await
            .watch_with_change_fn(
                Some(min_difference2.to_string()),
                Box::new(change_function2),
                TestSender { sender: hanging_get_responder2.clone() },
                None,
            )
            .await;

        // Send a value big enough to trigger both change functions.
        let big_value = ID1 + min_difference;
        switchboard_handle.lock().await.set_id(big_value);
        switchboard_handle.lock().await.notify_listener();

        // Both hanging gets got the value.
        verify_id(hanging_get_listener.next().await.unwrap(), big_value);
        verify_id(hanging_get_listener2.next().await.unwrap(), big_value);
    }

    #[test]
    fn test_hanging_get_controller() {
        let mut controller: HangingGetController<TestStruct, TestSender> =
            HangingGetController::new(Box::new(|_old: &TestStruct, _new: &TestStruct| true));

        // Should send change on launch
        assert_eq!(controller.on_watch(), true);
        assert_eq!(controller.on_change(&TestStruct { id: 1.0 }), true);
        controller.on_send(TestStruct { id: 1.0 });

        // After sent, without change, shouldn't send
        assert_eq!(controller.on_watch(), false);
        assert_eq!(controller.on_change(&TestStruct { id: 2.0 }), true);
        controller.on_send(TestStruct { id: 2.0 });
    }

    #[test]
    fn test_hanging_get_controller_with_change_function() {
        let mut controller: HangingGetController<TestStruct, TestSender> =
            HangingGetController::new(Box::new(Box::new(|old: &TestStruct, new: &TestStruct| {
                old.id < new.id
            })));

        // Should send change on launch.
        assert_eq!(controller.on_watch(), true);
        assert_eq!(controller.on_change(&TestStruct { id: 1.0 }), true);
        controller.on_send(TestStruct { id: 1.0 });

        // Won't send if change function not triggered.
        assert_eq!(controller.on_change(&TestStruct { id: 1.0 }), false);
        assert_eq!(controller.on_watch(), false);

        // Will send once we get a change.
        assert_eq!(controller.on_change(&TestStruct { id: 2.0 }), true);
        assert_eq!(controller.on_watch(), true);
    }
}
