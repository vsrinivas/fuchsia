// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::switchboard::base::*, failure::Error, fuchsia_async as fasync, futures::lock::Mutex,
    futures::stream::StreamExt, std::marker::PhantomData, std::sync::Arc,
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
        HangingGetController {
            last_sent_value: None,
            change_function: Box::new(|_old: &T, _new: &T| true),
            should_send: true,
        }
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
    switchboard_handle: SwitchboardHandle,
    _listen_session: Box<dyn ListenSession + Send + Sync>,
    hanging_get: Option<ST>,
    data_type: PhantomData<T>,
    setting_type: SettingType,
    controller: HangingGetController<T>,
    command_tx: futures::channel::mpsc::UnboundedSender<ListenCommand>,
}

/// Trait that should be implemented to send data to the hanging get watcher.
pub trait Sender<T> {
    fn send_response(self, data: T);
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
        switchboard_handle: SwitchboardHandle,
        setting_type: SettingType,
    ) -> Arc<Mutex<HangingGetHandler<T, ST>>> {
        let (on_command_sender, mut on_command_receiver) =
            futures::channel::mpsc::unbounded::<ListenCommand>();

        let on_command_sender_clone = on_command_sender.clone();
        let hanging_get_handler = Arc::new(Mutex::new(HangingGetHandler::<T, ST> {
            switchboard_handle: switchboard_handle.clone(),
            _listen_session: switchboard_handle
                .clone()
                .lock()
                .await
                .listen(
                    setting_type,
                    Arc::new(move |setting| {
                        on_command_sender_clone.unbounded_send(ListenCommand::Change(setting)).ok();
                    }),
                )
                .expect("started listening successfully"),
            hanging_get: None,
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
    pub async fn watch(&mut self, responder: ST) {
        self.watch_with_change_fn(Box::new(|_old: &T, _new: &T| true), responder).await;
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
    ) {
        self.controller.set_change_function(change_function);

        if let None = self.hanging_get {
            self.hanging_get = Some(responder);
            if self.controller.on_watch() {
                let response = self.get_response().await.expect("got current value");
                self.send_if_needed(response).await;
            }
        } else {
            panic!("Inconsistent state; watcher already registered");
        }
    }

    /// Called when receiving a notification that value has changed.
    async fn on_change(&mut self) {
        let response = self.get_response().await.expect("got current value");
        if self.controller.on_change(&T::from(response.clone())) {
            self.send_if_needed(response).await;
        }
    }

    /// Called when receiving a notification that value has changed.
    async fn send_if_needed(&mut self, response: SettingResponse) {
        let mut responder_swap = None;
        std::mem::swap(&mut self.hanging_get, &mut responder_swap);

        if let Some(responder) = responder_swap {
            responder.send_response(T::from(response.clone()));
            self.hanging_get = None;
            self.controller.on_send(T::from(response));
        }
    }

    async fn get_response(&self) -> Result<SettingResponse, Error> {
        let (response_tx, response_rx) =
            futures::channel::oneshot::channel::<SettingResponseResult>();
        {
            let mut switchboard = self.switchboard_handle.lock().await;
            switchboard
                .request(self.setting_type, SettingRequest::Get, response_tx)
                .expect("made request");
        }
        if let Ok(Some(setting_response)) = response_rx.await.expect("got result") {
            Ok(setting_response)
        } else {
            panic!("Couldn't get value");
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use failure::Error;
    use fuchsia_async::DurationExt;
    use fuchsia_zircon as zx;
    use futures::channel::mpsc::UnboundedSender;
    use parking_lot::RwLock;

    const ID1: f32 = 1.0;
    const ID2: f32 = 2.0;

    const SETTING_TYPE: SettingType = SettingType::Display;

    struct TestStruct {
        id: f32,
    }

    struct TestSender {
        sender: UnboundedSender<TestStruct>,
    }

    struct TestListenSession {}

    impl ListenSession for TestListenSession {
        fn close(&mut self) {}
    }

    impl Drop for TestListenSession {
        fn drop(&mut self) {}
    }

    struct TestSwitchboard {
        id_to_send: Arc<RwLock<f32>>,
        setting_type: Option<SettingType>,
        listener: Option<ListenCallback>,
    }

    impl TestSwitchboard {
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
            self.sender.unbounded_send(data).unwrap();
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
            callback: futures::channel::oneshot::Sender<Result<Option<SettingResponse>, Error>>,
        ) -> Result<(), Error> {
            assert_eq!(setting_type, SETTING_TYPE);
            assert_eq!(request, SettingRequest::Get);

            let value = *self.id_to_send.read();
            callback
                .send(Ok(Some(SettingResponse::Brightness(DisplayInfo::new(false, value)))))
                .unwrap();
            Ok(())
        }

        fn listen(
            &mut self,
            setting_type: SettingType,
            listener: ListenCallback,
        ) -> Result<Box<dyn ListenSession + Send + Sync>, Error> {
            self.setting_type = Some(setting_type);
            self.listener = Some(listener);
            Ok(Box::new(TestListenSession {}))
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_change_after_watch() {
        let current_id = Arc::new(RwLock::new(ID1));
        let switchboard_handle = Arc::new(Mutex::new(TestSwitchboard {
            id_to_send: current_id.clone(),
            listener: None,
            setting_type: None,
        }));

        let hanging_get_handler: Arc<Mutex<HangingGetHandler<TestStruct, TestSender>>> =
            HangingGetHandler::create(switchboard_handle.clone(), SettingType::Display).await;

        let (hanging_get_responder, mut hanging_get_listener) =
            futures::channel::mpsc::unbounded::<TestStruct>();

        {
            let mut hanging_get_lock = hanging_get_handler.lock().await;
            hanging_get_lock.watch(TestSender { sender: hanging_get_responder.clone() }).await;
        }

        // First get should return immediately
        let data = hanging_get_listener.next().await.unwrap();
        assert_eq!(data.id, ID1);

        // Subsequent one should wait until new value is notified
        {
            let mut hanging_get_lock = hanging_get_handler.lock().await;
            hanging_get_lock.watch(TestSender { sender: hanging_get_responder.clone() }).await;
        }

        {
            *current_id.write() = ID2;
        }

        {
            let switchboard = switchboard_handle.lock().await;
            switchboard.notify_listener();
        }

        let data = hanging_get_listener.next().await.unwrap();
        assert_eq!(data.id, ID2);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_watch_after_change() {
        let current_id = Arc::new(RwLock::new(ID1));
        let switchboard_handle = Arc::new(Mutex::new(TestSwitchboard {
            id_to_send: current_id.clone(),
            listener: None,
            setting_type: None,
        }));

        let hanging_get_handler: Arc<Mutex<HangingGetHandler<TestStruct, TestSender>>> =
            HangingGetHandler::create(switchboard_handle.clone(), SettingType::Display).await;

        let (hanging_get_responder, mut hanging_get_listener) =
            futures::channel::mpsc::unbounded::<TestStruct>();

        {
            let mut hanging_get_lock = hanging_get_handler.lock().await;
            hanging_get_lock.watch(TestSender { sender: hanging_get_responder.clone() }).await;
        }

        // First get should return immediately
        let data = hanging_get_listener.next().await.unwrap();
        assert_eq!(data.id, ID1);

        {
            *current_id.write() = ID2;
        }

        // Subsequent one should wait until new value is notified
        {
            let mut hanging_get_lock = hanging_get_handler.lock().await;
            hanging_get_lock.watch(TestSender { sender: hanging_get_responder.clone() }).await;
        }

        {
            let switchboard = switchboard_handle.lock().await;
            switchboard.notify_listener();
        }

        let data = hanging_get_listener.next().await.unwrap();
        assert_eq!(data.id, ID2);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_watch_with_change_function() {
        let current_id = Arc::new(RwLock::new(ID1));
        let switchboard_handle = Arc::new(Mutex::new(TestSwitchboard {
            id_to_send: current_id.clone(),
            listener: None,
            setting_type: None,
        }));

        let hanging_get_handler: Arc<Mutex<HangingGetHandler<TestStruct, TestSender>>> =
            HangingGetHandler::create(switchboard_handle.clone(), SettingType::Display).await;

        let (hanging_get_responder, mut hanging_get_listener) =
            futures::channel::mpsc::unbounded::<TestStruct>();

        let min_difference = 2.0;
        let change_function =
            move |old: &TestStruct, new: &TestStruct| -> bool { new.id - old.id > min_difference };

        {
            let mut hanging_get_lock = hanging_get_handler.lock().await;
            hanging_get_lock
                .watch_with_change_fn(
                    Box::new(change_function),
                    TestSender { sender: hanging_get_responder.clone() },
                )
                .await;
        }

        // First get should return immediately even with change function
        let data = hanging_get_listener.next().await.unwrap();
        assert_eq!(data.id, ID1);

        {
            *current_id.write() = ID2;
        }

        {
            let switchboard = switchboard_handle.lock().await;
            switchboard.notify_listener();
        }

        // Subsequent watch should return ignoring change function
        {
            let mut hanging_get_lock = hanging_get_handler.lock().await;
            hanging_get_lock.watch(TestSender { sender: hanging_get_responder.clone() }).await;
        }

        let data = hanging_get_listener.next().await.unwrap();
        assert_eq!(data.id, ID2);

        // Subsequent watch with change function should only return if change is big enough
        {
            *current_id.write() = ID2 + 1.0;
        }

        {
            let switchboard = switchboard_handle.lock().await;
            switchboard.notify_listener();
        }

        {
            let mut hanging_get_lock = hanging_get_handler.lock().await;
            hanging_get_lock
                .watch_with_change_fn(
                    Box::new(change_function),
                    TestSender { sender: hanging_get_responder.clone() },
                )
                .await;
        }

        // Must wait for some a short time to allow change to propagate.
        let sleep_duration = zx::Duration::from_millis(1);
        fasync::Timer::new(sleep_duration.after_now()).await;

        {
            *current_id.write() = ID2 + 3.0;
        }

        {
            let switchboard = switchboard_handle.lock().await;
            switchboard.notify_listener();
        }

        let data = hanging_get_listener.next().await.unwrap();
        assert_eq!(data.id, ID2 + 3.0);
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
