// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::switchboard::base::*,
    anyhow::{format_err, Error},
    fuchsia_async as fasync,
    futures::channel::mpsc::UnboundedSender,
    futures::lock::Mutex,
    futures::stream::StreamExt,
    std::marker::PhantomData,
    std::sync::Arc,
};

type ChangeFunction<T> = Box<dyn Fn(&T, &T) -> bool + Send + Sync + 'static>;

/// Controller that determines whether or not a change should be sent to the
/// hanging get.
struct HangingGetController<T> {
    /// The last value that was sent to the client
    last_sent_value: Option<T>,
    /// Function called on change. If function returns true, tells the
    /// handler that it should send to the hanging get.
    change_function: ChangeFunction<T>,
    /// If true, should send value next time watch
    /// is called or if there is a hanging watch.
    should_send: bool,
}

impl<T> HangingGetController<T> {
    fn new() -> HangingGetController<T> {
        let mut controller = HangingGetController {
            last_sent_value: None,
            change_function: Box::new(|_old: &T, _new: &T| true),
            should_send: true,
        };

        // Initialize with same method use for resetting.
        controller.initialize();

        controller
    }

    fn initialize(&mut self) {
        self.last_sent_value = None;
        self.should_send = true;
        self.change_function = Box::new(|_old: &T, _new: &T| true);
    }

    fn on_error(&mut self) {
        self.initialize();
    }

    /// Sets the function that will be called on_change. Note that this will
    /// not be called on watch, so if a connection was already marked for
    /// sending, this wouldn't affect that.
    fn set_change_function(&mut self, change_function: ChangeFunction<T>) {
        self.change_function = change_function;
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
pub struct HangingGetHandler<T, ST>
where
    T: From<SettingResponse> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
{
    switchboard_client: SwitchboardClient,
    listen_session: Option<Box<dyn ListenSession + Send + Sync>>,
    pending_responders: Vec<(ST, Option<UnboundedSender<()>>)>,
    data_type: PhantomData<T>,
    setting_type: SettingType,
    controller: HangingGetController<T>,
    command_tx: UnboundedSender<ListenCommand>,
}

/// Trait that should be implemented to send data to the hanging get watcher.
pub trait Sender<T> {
    fn send_response(self, data: T);
    fn on_error(self);
}

enum ListenCommand {
    Change(SettingType),
    Exit,
}

impl<T, ST> Drop for HangingGetHandler<T, ST>
where
    T: From<SettingResponse> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
{
    fn drop(&mut self) {
        self.close();
    }
}

impl<T, ST> HangingGetHandler<T, ST>
where
    T: From<SettingResponse> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
{
    pub async fn create(
        switchboard_client: SwitchboardClient,
        setting_type: SettingType,
    ) -> Arc<Mutex<HangingGetHandler<T, ST>>> {
        let (on_command_sender, mut on_command_receiver) =
            futures::channel::mpsc::unbounded::<ListenCommand>();
        let hanging_get_handler = Arc::new(Mutex::new(HangingGetHandler::<T, ST> {
            switchboard_client: switchboard_client.clone(),
            listen_session: None,
            pending_responders: Vec::new(),
            data_type: PhantomData,
            setting_type: setting_type,
            controller: HangingGetController::new(),
            command_tx: on_command_sender.clone(),
        }));

        {
            let hanging_get_handler_clone = hanging_get_handler.clone();
            fasync::spawn(async move {
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
            });
        }

        hanging_get_handler
    }

    pub fn close(&mut self) {
        self.command_tx.unbounded_send(ListenCommand::Exit).ok();
    }

    /// Park a new hanging get in the handler
    pub async fn watch(&mut self, responder: ST, error_sender: Option<UnboundedSender<()>>) {
        self.watch_with_change_fn(Box::new(|_old: &T, _new: &T| true), responder, error_sender)
            .await;
    }

    /// Park a new hanging get in the handler, along with a change function.
    /// The hanging get will only return when the change function evaluates
    /// to true when comparing the last value sent to the client and the current
    /// value obtained by the hanging_get_handler.
    /// A change function is applied on change only, and not on the current state.
    pub async fn watch_with_change_fn(
        &mut self,
        change_function: ChangeFunction<T>,
        responder: ST,
        error_sender: Option<UnboundedSender<()>>,
    ) {
        self.controller.set_change_function(change_function);

        self.pending_responders.push((responder, error_sender));

        if self.listen_session.is_none() {
            let command_tx_clone = self.command_tx.clone();
            if let Ok(session) = self
                .switchboard_client
                .listen(
                    self.setting_type,
                    Arc::new(move |setting| {
                        command_tx_clone.unbounded_send(ListenCommand::Change(setting)).ok();
                    }),
                )
                .await
            {
                self.listen_session = Some(session);
            } else {
                self.on_error();
                return;
            }
        }

        if self.controller.on_watch() {
            if let Ok(response) = self.get_response().await {
                self.send_if_needed(response).await
            } else {
                self.on_error();
            }
        }
    }

    /// Called when receiving a notification that value has changed.
    async fn on_change(&mut self) {
        if let Ok(response) = self.get_response().await {
            if self.controller.on_change(&T::from(response.clone())) {
                self.send_if_needed(response).await;
            }
        } else {
            self.on_error();
        }
    }

    fn on_error(&mut self) {
        self.listen_session = None;
        self.controller.on_error();
        if !self.pending_responders.is_empty() {
            while let Some((responder, optional_exit_tx)) = self.pending_responders.pop() {
                responder.on_error();
                if let Some(exit_tx) = optional_exit_tx {
                    exit_tx.unbounded_send(()).ok();
                }
            }
        }
    }

    /// Called when receiving a notification that value has changed.
    async fn send_if_needed(&mut self, response: SettingResponse) {
        if !self.pending_responders.is_empty() {
            while let Some((responder, _)) = self.pending_responders.pop() {
                responder.send_response(T::from(response.clone()));
            }
            self.controller.on_send(T::from(response));
        }
    }

    async fn get_response(&self) -> Result<SettingResponse, Error> {
        if let Ok(response_rx) =
            self.switchboard_client.request(self.setting_type, SettingRequest::Get).await
        {
            if let Ok(Some(setting_response)) = response_rx.await.expect("got result") {
                Ok(setting_response)
            } else {
                Err(format_err!("Couldn't get value"))
            }
        } else {
            Err(format_err!("Couldn't make request"))
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use anyhow::{format_err, Error};
    use fuchsia_async::DurationExt;
    use fuchsia_zircon as zx;
    use futures::channel::mpsc::UnboundedSender;
    use parking_lot::RwLock;

    const ID1: f32 = 1.0;
    const ID2: f32 = 2.0;

    const SETTING_TYPE: SettingType = SettingType::Display;

    #[derive(PartialEq, Debug, Clone)]
    struct TestStruct {
        id: f32,
    }

    #[derive(PartialEq, Debug, Clone)]
    enum Event {
        Data(TestStruct),
        Error,
    }

    struct TestSender {
        sender: UnboundedSender<Event>,
    }

    struct TestListenSession {}

    impl ListenSession for TestListenSession {
        fn close(&mut self) {}
    }

    impl Drop for TestListenSession {
        fn drop(&mut self) {}
    }

    struct TestSwitchboardBuilder {
        id_to_send: Arc<RwLock<Option<f32>>>,
        always_fail: bool,
    }

    impl TestSwitchboardBuilder {
        fn new() -> Self {
            Self { id_to_send: Arc::new(RwLock::new(None)), always_fail: false }
        }

        fn set_initial_id(mut self, id: f32) -> Self {
            self.id_to_send = Arc::new(RwLock::new(Some(id)));
            self
        }

        fn set_always_fail(mut self, always_fail: bool) -> Self {
            self.always_fail = always_fail;
            self
        }

        fn build(self) -> Arc<Mutex<TestSwitchboard>> {
            Arc::new(Mutex::new(TestSwitchboard {
                id_to_send: self.id_to_send,
                setting_type: None,
                listener: None,
                always_fail: self.always_fail,
            }))
        }
    }

    struct TestSwitchboard {
        id_to_send: Arc<RwLock<Option<f32>>>,
        setting_type: Option<SettingType>,
        listener: Option<ListenCallback>,
        always_fail: bool,
    }

    impl TestSwitchboard {
        fn set_id(&mut self, id: f32) {
            self.id_to_send = Arc::new(RwLock::new(Some(id)));
        }

        fn notify_listener(&self) {
            if let Some(setting_type_value) = self.setting_type {
                if let Some(listener_sender) = self.listener.clone() {
                    (listener_sender)(setting_type_value);
                    return;
                }
            }
            panic!("Missing listener to notify");
        }
    }

    impl Sender<TestStruct> for TestSender {
        fn send_response(self, data: TestStruct) {
            self.sender.unbounded_send(Event::Data(data)).unwrap();
        }

        fn on_error(self) {
            self.sender.unbounded_send(Event::Error).unwrap();
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

    impl Switchboard for TestSwitchboard {
        fn request(
            &mut self,
            setting_type: SettingType,
            request: SettingRequest,
            callback: futures::channel::oneshot::Sender<SettingResponseResult>,
        ) -> Result<(), Error> {
            assert_eq!(setting_type, SETTING_TYPE);
            assert_eq!(request, SettingRequest::Get);

            if self.always_fail {
                callback
                    .send(Err(SwitchboardError::UnexpectedError {
                        description: "set failure".to_string(),
                    }))
                    .unwrap();
            } else if let Some(value) = *self.id_to_send.read() {
                callback
                    .send(Ok(Some(SettingResponse::Brightness(DisplayInfo::new(
                        false,
                        value,
                        LowLightMode::Disable,
                    )))))
                    .unwrap();
            }
            Ok(())
        }

        fn listen(
            &mut self,
            setting_type: SettingType,
            listener: ListenCallback,
        ) -> Result<Box<dyn ListenSession + Send + Sync>, Error> {
            if self.always_fail {
                return Err(format_err!("failure"));
            }

            self.setting_type = Some(setting_type);
            self.listener = Some(listener);
            Ok(Box::new(TestListenSession {}))
        }
    }

    fn verify_id(event: Event, id: f32) {
        if let Event::Data(data) = event {
            assert_eq!(data.id, id);
        } else {
            panic!("Should be data");
        }
    }

    /// Ensures errors are gracefully handed back by the hanging_get
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_error_resolution() {
        let switchboard_handle = TestSwitchboardBuilder::new().set_always_fail(true).build();

        let hanging_get_handler: Arc<Mutex<HangingGetHandler<TestStruct, TestSender>>> =
            HangingGetHandler::create(
                SwitchboardClient::new(&(switchboard_handle.clone() as SwitchboardHandle)),
                SettingType::Display,
            )
            .await;

        let (hanging_get_responder, mut hanging_get_listener) =
            futures::channel::mpsc::unbounded::<Event>();

        let (exit_tx, mut exit_rx) = futures::channel::mpsc::unbounded::<()>();

        {
            let mut hanging_get_lock = hanging_get_handler.lock().await;
            hanging_get_lock
                .watch(TestSender { sender: hanging_get_responder.clone() }, Some(exit_tx))
                .await;
        }

        // The responder should receive an error
        assert_eq!(hanging_get_listener.next().await.unwrap(), Event::Error);

        // When set, the exit sender should also be fired
        assert_eq!(exit_rx.next().await, Some(()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_change_after_watch() {
        let switchboard_handle = TestSwitchboardBuilder::new().set_initial_id(ID1).build();

        let hanging_get_handler: Arc<Mutex<HangingGetHandler<TestStruct, TestSender>>> =
            HangingGetHandler::create(
                SwitchboardClient::new(&(switchboard_handle.clone() as SwitchboardHandle)),
                SettingType::Display,
            )
            .await;

        let (hanging_get_responder, mut hanging_get_listener) =
            futures::channel::mpsc::unbounded::<Event>();

        {
            let mut hanging_get_lock = hanging_get_handler.lock().await;
            hanging_get_lock
                .watch(TestSender { sender: hanging_get_responder.clone() }, None)
                .await;
        }

        // First get should return immediately
        verify_id(hanging_get_listener.next().await.unwrap(), ID1);

        // Subsequent one should wait until new value is notified
        {
            let mut hanging_get_lock = hanging_get_handler.lock().await;
            hanging_get_lock
                .watch(TestSender { sender: hanging_get_responder.clone() }, None)
                .await;
        }

        switchboard_handle.lock().await.set_id(ID2);

        switchboard_handle.lock().await.notify_listener();

        verify_id(hanging_get_listener.next().await.unwrap(), ID2);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_watch_after_change() {
        let switchboard_handle = TestSwitchboardBuilder::new().set_initial_id(ID1).build();

        let hanging_get_handler: Arc<Mutex<HangingGetHandler<TestStruct, TestSender>>> =
            HangingGetHandler::create(
                SwitchboardClient::new(&(switchboard_handle.clone() as SwitchboardHandle)),
                SettingType::Display,
            )
            .await;

        let (hanging_get_responder, mut hanging_get_listener) =
            futures::channel::mpsc::unbounded::<Event>();

        {
            let mut hanging_get_lock = hanging_get_handler.lock().await;
            hanging_get_lock
                .watch(TestSender { sender: hanging_get_responder.clone() }, None)
                .await;
        }

        // First get should return immediately
        verify_id(hanging_get_listener.next().await.unwrap(), ID1);

        switchboard_handle.lock().await.set_id(ID2);

        // Subsequent one should wait until new value is notified
        {
            let mut hanging_get_lock = hanging_get_handler.lock().await;
            hanging_get_lock
                .watch(TestSender { sender: hanging_get_responder.clone() }, None)
                .await;
        }

        switchboard_handle.lock().await.notify_listener();

        verify_id(hanging_get_listener.next().await.unwrap(), ID2);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_watch_with_change_function() {
        let switchboard_handle = TestSwitchboardBuilder::new().set_initial_id(ID1).build();

        let hanging_get_handler: Arc<Mutex<HangingGetHandler<TestStruct, TestSender>>> =
            HangingGetHandler::create(
                SwitchboardClient::new(&(switchboard_handle.clone() as SwitchboardHandle)),
                SettingType::Display,
            )
            .await;

        let (hanging_get_responder, mut hanging_get_listener) =
            futures::channel::mpsc::unbounded::<Event>();

        let min_difference = 2.0;
        let change_function =
            move |old: &TestStruct, new: &TestStruct| -> bool { new.id - old.id > min_difference };

        {
            let mut hanging_get_lock = hanging_get_handler.lock().await;
            hanging_get_lock
                .watch_with_change_fn(
                    Box::new(change_function),
                    TestSender { sender: hanging_get_responder.clone() },
                    None,
                )
                .await;
        }

        // First get should return immediately even with change function
        verify_id(hanging_get_listener.next().await.unwrap(), ID1);

        switchboard_handle.lock().await.set_id(ID2);

        switchboard_handle.lock().await.notify_listener();

        // Subsequent watch should return ignoring change function
        {
            let mut hanging_get_lock = hanging_get_handler.lock().await;
            hanging_get_lock
                .watch(TestSender { sender: hanging_get_responder.clone() }, None)
                .await;
        }

        verify_id(hanging_get_listener.next().await.unwrap(), ID2);

        // Subsequent watch with change function should only return if change is big enough
        switchboard_handle.lock().await.set_id(ID2 + 1.0);

        switchboard_handle.lock().await.notify_listener();

        {
            let mut hanging_get_lock = hanging_get_handler.lock().await;
            hanging_get_lock
                .watch_with_change_fn(
                    Box::new(change_function),
                    TestSender { sender: hanging_get_responder.clone() },
                    None,
                )
                .await;
        }

        // Must wait for some a short time to allow change to propagate.
        let sleep_duration = zx::Duration::from_millis(1);
        fasync::Timer::new(sleep_duration.after_now()).await;

        switchboard_handle.lock().await.set_id(ID2 + 3.0);

        switchboard_handle.lock().await.notify_listener();

        verify_id(hanging_get_listener.next().await.unwrap(), ID2 + 3.0);
    }

    #[test]
    fn test_hanging_get_controller() {
        let mut controller: HangingGetController<i32> = HangingGetController::new();

        // Should send change on launch
        assert_eq!(controller.on_watch(), true);
        assert_eq!(controller.on_change(&1), true);
        controller.on_send(1);

        // After sent, without change, shouldn't send
        assert_eq!(controller.on_watch(), false);
        assert_eq!(controller.on_change(&2), true);
        controller.on_send(2);

        // Setting change function should change when sending occurs
        controller.set_change_function(Box::new(|old, new| old < new));
        assert_eq!(controller.on_change(&1), false);
        assert_eq!(controller.on_watch(), false);
        assert_eq!(controller.on_change(&3), true);
        assert_eq!(controller.on_watch(), true);
    }
}
