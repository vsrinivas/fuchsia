// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::{SettingInfo, SettingType};
use crate::handler::base::{Error, Payload, Request};
use crate::message::base::Audience;
use crate::service;
use crate::service::TryFromWithClient;
use crate::trace;
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_warn;
use futures::channel::mpsc::UnboundedSender;
use futures::lock::Mutex;
use futures::stream::StreamExt;
use std::collections::HashMap;
use std::convert::TryFrom;
use std::hash::Hash;
use std::marker::PhantomData;
use std::sync::Arc;

type ChangeFunction<T> = Box<dyn Fn(&T, &T) -> bool + Send + Sync + 'static>;

/// Controller that determines whether or not a change should be sent to the
/// hanging get. T is the type of the value to be watched and sent to back to
/// the client via the sender ST.
struct HangingGetController<T, ST>
where
    T: From<SettingInfo> + Send + Sync + 'static,
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
    T: From<SettingInfo> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
{
    fn new(change_function: ChangeFunction<T>) -> Self {
        Self {
            last_sent_value: None,
            change_function,
            should_send: true,
            pending_responders: Vec::new(),
        }
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

    fn on_error(&mut self, error: &anyhow::Error) {
        self.initialize();

        // Notify responders of error.
        while let Some((responder, optional_exit_tx)) = self.pending_responders.pop() {
            responder.on_error(&error);
            if let Some(exit_tx) = optional_exit_tx {
                // Panic if send failed, otherwise, spawn might not be ended.
                exit_tx
                    .unbounded_send(())
                    .expect("HangingGetController::on_error, exit_tx failed to send exit signal");
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
    async fn send_if_needed(&mut self, response: SettingInfo) {
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

/// Handler for hanging gets.
/// We never use the data type T directly, but it is used to constrain ST as the sender
/// for that type.
/// To use, one should implement a sender, as well as a way to convert SettingInfo into
/// something that sender can use.
/// K is the type of the key for the change_function.
pub struct HangingGetHandler<T, ST, K>
where
    T: From<SettingInfo> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
    K: Eq + Hash + Clone + Send + Sync + 'static,
{
    messenger: service::message::Messenger,
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
    fn on_error(self, error: &anyhow::Error);
}

enum ListenCommand {
    Change(SettingInfo),
    Exit,
}

impl<T, ST, K> Drop for HangingGetHandler<T, ST, K>
where
    T: From<SettingInfo> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
    K: Eq + Hash + Clone + Send + Sync + 'static,
{
    fn drop(&mut self) {
        self.close();
    }
}

impl<T, ST, K> HangingGetHandler<T, ST, K>
where
    T: From<SettingInfo> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
    K: Eq + Hash + Clone + Send + Sync + 'static,
{
    pub(super) async fn create(
        messenger: service::message::Messenger,
        setting_type: SettingType,
    ) -> Arc<Mutex<HangingGetHandler<T, ST, K>>> {
        let (on_command_sender, mut on_command_receiver) =
            futures::channel::mpsc::unbounded::<ListenCommand>();
        let hanging_get_handler = Arc::new(Mutex::new(HangingGetHandler::<T, ST, K> {
            messenger,
            listen_exit_tx: None,
            data_type: PhantomData,
            setting_type,
            default_controller: HangingGetController::new(Box::new(|_old: &T, _new: &T| true)),
            controllers_by_key: Default::default(),
            command_tx: on_command_sender,
        }));

        {
            let hanging_get_handler_clone = hanging_get_handler.clone();
            fasync::Task::spawn(async move {
                let nonce = fuchsia_trace::generate_nonce();
                trace!(nonce, "hanging_get_handler");
                while let Some(command) = on_command_receiver.next().await {
                    match command {
                        ListenCommand::Change(setting_info) => {
                            trace!(nonce, "change");
                            let mut handler_lock = hanging_get_handler_clone.lock().await;
                            handler_lock.on_change(setting_info).await;
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

    pub(crate) fn close(&mut self) {
        // This method has been called in drop methods. Only log warn in case the receiving end has
        // been dropped already.
        if let Some(exit_tx) = self.listen_exit_tx.take() {
            if !exit_tx.is_closed() {
                exit_tx.unbounded_send(()).unwrap_or_else(|_| {
                    fx_log_warn!(
                        "HangingGetHandler::close, listen_exit_tx failed to send exit signal"
                    )
                });
            }
        }

        if !self.command_tx.is_closed() {
            self.command_tx.unbounded_send(ListenCommand::Exit).unwrap_or_else(|_| {
                fx_log_warn!("HangingGetHandler::close, command_tx failed to send Exit command")
            });
        }
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
                .or_insert_with(|| HangingGetController::new(change_function)),
        };

        controller.add_pending_responder(responder, error_sender);

        if self.listen_exit_tx.is_none() {
            let command_tx_clone = self.command_tx.clone();
            let receptor = self
                .messenger
                .message(
                    Payload::Request(Request::Listen).into(),
                    Audience::Address(service::Address::Handler(self.setting_type)),
                )
                .send();

            let (exit_tx, mut exit_rx) = futures::channel::mpsc::unbounded::<()>();
            self.listen_exit_tx = Some(exit_tx);

            fasync::Task::spawn(async move {
                let nonce = fuchsia_trace::generate_nonce();
                trace!(nonce, "watch change fn");
                let receptor_fuse = receptor.fuse();
                futures::pin_mut!(receptor_fuse);

                loop {
                    futures::select! {
                        update = receptor_fuse.select_next_some() => {
                            trace!(
                                nonce,
                                "change",
                                "payload" => format!("{:?}", update).as_str()
                            );
                            if let Ok((Payload::Response(Ok(Some(setting_info))), _)) =
                                Payload::try_from_with_client(update) {
                                    command_tx_clone.unbounded_send(
                                        ListenCommand::Change(setting_info))
                                        .expect("HangingGetHandler::watch_with_change_fn, \
                                        command_tx failed to send Change command");
                            }
                        }
                        _ = exit_rx.next() => {
                            return;
                        }
                    }
                }
            })
            .detach();
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
    async fn on_change(&mut self, setting_info: SettingInfo) {
        let value = T::from(setting_info.clone());
        for controller in self.controllers_by_key.values_mut() {
            if controller.on_change(&value) {
                controller.send_if_needed(setting_info.clone()).await;
            }
        }

        if self.default_controller.on_change(&value) {
            self.default_controller.send_if_needed(setting_info).await;
        }
    }

    fn on_error(&mut self, error: &anyhow::Error) {
        if let Some(exit_tx) = self.listen_exit_tx.take() {
            // Panic if send failed, otherwise, spawn might not be ended.
            exit_tx
                .unbounded_send(())
                .expect("HangingGetHandler::on_error, exit_tx failed to send exit signal");
        }

        self.default_controller.on_error(&error);
        for controller in self.controllers_by_key.values_mut() {
            controller.on_error(&error);
        }
    }

    async fn get_response(&self) -> Result<SettingInfo, anyhow::Error> {
        let mut receptor = self
            .messenger
            .message(
                Payload::Request(Request::Get).into(),
                Audience::Address(service::Address::Handler(self.setting_type)),
            )
            .send();

        match Payload::try_from(
            receptor
                .next_payload()
                .await
                .map_err(|_| anyhow::Error::new(Error::UnhandledType(self.setting_type)))?
                .0,
        )
        .map_err(|_| {
            anyhow::Error::new(Error::UnexpectedError("Could not convert payload".into()))
        })? {
            Payload::Response(Ok(Some(setting_response))) => Ok(setting_response),
            Payload::Response(Err(err)) => Err(anyhow::Error::new(err)),
            _ => Err(anyhow::Error::new(Error::UnexpectedError("Unexpected payload".into()))),
        }
    }
}

#[cfg(test)]
mod tests {
    use fuchsia_async::DurationExt;
    use fuchsia_zircon as zx;
    use futures::channel::mpsc::UnboundedSender;
    use std::borrow::Cow;

    use crate::base::SettingInfo;
    use crate::display::types::{DisplayInfo, LowLightMode};
    use crate::message::base::MessengerType;
    use crate::message::MessageHubUtil;

    use super::*;

    const ID1: f32 = 1.0;
    const ID2: f32 = 2.0;

    const SET_ERROR: &str = "set failure";
    const DEFAULT_AUTO_BRIGHTNESS_VALUE: f32 = 0.5;

    #[derive(PartialEq, Debug, Clone)]
    struct TestStruct {
        id: f32,
    }

    #[derive(PartialEq, Debug, Clone)]
    enum Event {
        Data(TestStruct),
        Error(Error),
        UnknownError,
    }

    impl<C: Into<Cow<'static, str>>> From<C> for Error {
        fn from(c: C) -> Self {
            Error::UnexpectedError(c.into())
        }
    }

    struct TestSender {
        sender: UnboundedSender<Event>,
    }

    struct TestSettingHandlerBuilder {
        id_to_send: Option<f32>,
        always_fail: bool,
        delegate: service::message::Delegate,
        setting_type: SettingType,
    }

    impl TestSettingHandlerBuilder {
        fn new(delegate: service::message::Delegate, setting_type: SettingType) -> Self {
            Self { delegate, id_to_send: None, always_fail: false, setting_type }
        }

        fn set_initial_id(mut self, id: f32) -> Self {
            self.id_to_send = Some(id);
            self
        }

        fn set_always_fail(mut self, always_fail: bool) -> Self {
            self.always_fail = always_fail;
            self
        }

        async fn build(self) -> Arc<Mutex<TestSettingHandler>> {
            TestSettingHandler::create(
                self.delegate,
                self.id_to_send,
                self.always_fail,
                self.setting_type,
            )
            .await
        }
    }

    struct TestSettingHandler {
        id_to_send: Option<f32>,
        listener: Option<service::message::MessageClient>,
        always_fail: bool,
    }

    impl TestSettingHandler {
        async fn create(
            delegate: service::message::Delegate,
            id_to_send: Option<f32>,
            always_fail: bool,
            setting_type: SettingType,
        ) -> Arc<Mutex<TestSettingHandler>> {
            let handler = Arc::new(Mutex::new(TestSettingHandler {
                id_to_send,
                listener: None,
                always_fail,
            }));

            let (_, mut receptor) = delegate
                .create(MessengerType::Addressable(service::Address::Handler(setting_type)))
                .await
                .expect("messenger should have been created");

            let handler_clone = handler.clone();
            fasync::Task::spawn(async move {
                while let (Payload::Request(request), client) = receptor
                    .next_of::<Payload>()
                    .await
                    .unwrap_or_else(|err| panic!("could not get payload: {:?}", err))
                {
                    handler_clone.lock().await.request(client, request);
                }
            })
            .detach();

            handler
        }

        fn set_id(&mut self, id: f32) {
            self.id_to_send = Some(id);
        }

        fn notify_listener(&self, value: f32) {
            if let Some(listener) = self.listener.clone() {
                listener
                    .reply(
                        Payload::Response(Ok(Some(SettingInfo::Brightness(DisplayInfo::new(
                            false,
                            value,
                            DEFAULT_AUTO_BRIGHTNESS_VALUE,
                            true,
                            LowLightMode::Disable,
                            None,
                        )))))
                        .into(),
                    )
                    .send();
                return;
            }
            panic!("Missing listener to notify");
        }

        fn listen(&mut self, listener: service::message::MessageClient) {
            self.listener = Some(listener);
        }

        fn request(&mut self, requestor: service::message::MessageClient, request: Request) {
            match request {
                Request::Listen => {
                    self.listen(requestor);
                }
                Request::Get => {
                    let mut response = None;
                    if self.always_fail {
                        response = Some(Err(Error::from(SET_ERROR)));
                    } else if let Some(value) = self.id_to_send {
                        response = Some(Ok(Some(SettingInfo::Brightness(DisplayInfo::new(
                            false,
                            value,
                            DEFAULT_AUTO_BRIGHTNESS_VALUE,
                            true,
                            LowLightMode::Disable,
                            None,
                        )))));
                    }

                    if let Some(response) = response.take() {
                        requestor.reply(Payload::Response(response).into()).send();
                    }
                }
                _ => {
                    panic!("Unexpected request type");
                }
            }
        }
    }

    impl Sender<TestStruct> for TestSender {
        fn send_response(self, data: TestStruct) {
            self.sender.unbounded_send(Event::Data(data)).unwrap();
        }

        fn on_error(self, error: &anyhow::Error) {
            let error = match error.root_cause().downcast_ref::<Error>() {
                Some(request_error) => Event::Error(request_error.clone()),
                _ => Event::UnknownError,
            };
            self.sender.unbounded_send(error).unwrap();
        }
    }

    impl From<SettingInfo> for TestStruct {
        fn from(response: SettingInfo) -> Self {
            if let SettingInfo::Brightness(info) = response {
                return TestStruct { id: info.manual_brightness_value };
            }
            panic!("bad response:{:?}", response);
        }
    }

    // Direct float comparisons are ok for testing here.
    #[allow(clippy::float_cmp)]
    fn verify_id(event: Event, id: f32) {
        if let Event::Data(data) = event {
            assert_eq!(data.id, id);
        } else {
            panic!("Should be data {:?}", event);
        }
    }

    // Ensures errors are gracefully handed back by the hanging_get
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_error_resolution() {
        let delegate = service::MessageHub::create_hub();
        let setting_type = SettingType::Display;
        let _ = TestSettingHandlerBuilder::new(delegate.clone(), setting_type)
            .set_always_fail(true)
            .build()
            .await;

        let hanging_get_handler: Arc<Mutex<HangingGetHandler<TestStruct, TestSender, String>>> =
            HangingGetHandler::create(
                delegate.create(MessengerType::Unbound).await.unwrap().0,
                setting_type,
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
            Event::Error(Error::from(SET_ERROR))
        );

        // When set, the exit sender should also be fired
        assert_eq!(exit_rx.next().await, Some(()));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_change_after_watch() {
        let delegate = service::MessageHub::create_hub();
        let setting_type = SettingType::Display;

        let setting_handler_handle = TestSettingHandlerBuilder::new(delegate.clone(), setting_type)
            .set_initial_id(ID1)
            .build()
            .await;

        let hanging_get_handler: Arc<Mutex<HangingGetHandler<TestStruct, TestSender, String>>> =
            HangingGetHandler::create(
                delegate.create(MessengerType::Unbound).await.unwrap().0,
                setting_type,
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

        setting_handler_handle.lock().await.set_id(ID2);

        setting_handler_handle.lock().await.notify_listener(ID2);

        verify_id(hanging_get_listener.next().await.unwrap(), ID2);
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_watch_after_change() {
        let delegate = service::MessageHub::create_hub();
        let setting_type = SettingType::Display;

        let setting_handler_handle = TestSettingHandlerBuilder::new(delegate.clone(), setting_type)
            .set_initial_id(ID1)
            .build()
            .await;

        let hanging_get_handler: Arc<Mutex<HangingGetHandler<TestStruct, TestSender, String>>> =
            HangingGetHandler::create(
                delegate.create(MessengerType::Unbound).await.unwrap().0,
                setting_type,
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

        setting_handler_handle.lock().await.set_id(ID2);

        setting_handler_handle.lock().await.notify_listener(ID2);

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
        let delegate = service::MessageHub::create_hub();
        let setting_type = SettingType::Display;
        let setting_handler_handle = TestSettingHandlerBuilder::new(delegate.clone(), setting_type)
            .set_initial_id(ID1)
            .build()
            .await;

        let hanging_get_handler: Arc<Mutex<HangingGetHandler<TestStruct, TestSender, String>>> =
            HangingGetHandler::create(
                delegate.create(MessengerType::Unbound).await.unwrap().0,
                setting_type,
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

        setting_handler_handle.lock().await.set_id(ID2);

        setting_handler_handle.lock().await.notify_listener(ID2);

        // Subsequent watch should return ignoring change function
        hanging_get_handler
            .lock()
            .await
            .watch(TestSender { sender: hanging_get_responder.clone() }, None)
            .await;

        verify_id(hanging_get_listener.next().await.unwrap(), ID2);

        // Subsequent watch with change function should only return if change is big enough
        setting_handler_handle.lock().await.set_id(ID2 + 1.0);

        setting_handler_handle.lock().await.notify_listener(ID2 + 1.0);

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

        setting_handler_handle.lock().await.set_id(ID2 + 3.0);

        setting_handler_handle.lock().await.notify_listener(ID2 + 3.0);

        verify_id(hanging_get_listener.next().await.unwrap(), ID2 + 3.0);
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_watch_with_change_function_multiple() {
        let delegate = service::MessageHub::create_hub();
        let setting_type = SettingType::Display;

        let setting_handler_handle = TestSettingHandlerBuilder::new(delegate.clone(), setting_type)
            .set_initial_id(ID1)
            .build()
            .await;

        let hanging_get_handler: Arc<Mutex<HangingGetHandler<TestStruct, TestSender, String>>> =
            HangingGetHandler::create(
                delegate.create(MessengerType::Unbound).await.unwrap().0,
                setting_type,
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
        setting_handler_handle.lock().await.set_id(ID2);
        setting_handler_handle.lock().await.notify_listener(ID2);

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
        setting_handler_handle.lock().await.set_id(big_value);
        setting_handler_handle.lock().await.notify_listener(big_value);

        // Both hanging gets got the value.
        verify_id(hanging_get_listener.next().await.unwrap(), big_value);
        verify_id(hanging_get_listener2.next().await.unwrap(), big_value);
    }

    #[test]
    fn test_hanging_get_controller() {
        let mut controller: HangingGetController<TestStruct, TestSender> =
            HangingGetController::new(Box::new(|_old: &TestStruct, _new: &TestStruct| true));

        // Should send change on launch
        assert!(controller.on_watch());
        assert!(controller.on_change(&TestStruct { id: 1.0 }));
        controller.on_send(TestStruct { id: 1.0 });

        // After sent, without change, shouldn't send
        #[allow(clippy::bool_assert_comparison)]
        {
            assert_eq!(controller.on_watch(), false);
        }
        assert!(controller.on_change(&TestStruct { id: 2.0 }));
        controller.on_send(TestStruct { id: 2.0 });
    }

    #[test]
    fn test_hanging_get_controller_with_change_function() {
        let mut controller: HangingGetController<TestStruct, TestSender> =
            HangingGetController::new(Box::new(Box::new(|old: &TestStruct, new: &TestStruct| {
                old.id < new.id
            })));

        // Should send change on launch.
        assert!(controller.on_watch());
        assert!(controller.on_change(&TestStruct { id: 1.0 }));
        controller.on_send(TestStruct { id: 1.0 });

        // Won't send if change function not triggered.
        #[allow(clippy::bool_assert_comparison)]
        {
            assert_eq!(controller.on_change(&TestStruct { id: 1.0 }), false);
            assert_eq!(controller.on_watch(), false);
        }

        // Will send once we get a change.
        assert!(controller.on_change(&TestStruct { id: 2.0 }));
        assert!(controller.on_watch());
    }
}
