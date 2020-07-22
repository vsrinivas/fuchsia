// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::tests::fakes::fake_hanging_get_types::Clone, fuchsia_async as fasync,
    futures::channel::mpsc::UnboundedReceiver, futures::lock::Mutex, futures::stream::StreamExt,
    std::sync::Arc,
};

type ChangeFunction<T> = Box<dyn Fn(&T, &T) -> bool + Send + Sync + 'static>;

/// Controller that determines whether or not a change should be sent to the
/// hanging get.
struct HangingGetController<T> {
    /// The last value that was sent to the client.
    last_sent_value: Option<T>,

    /// Function called on change. If function returns true, tells the
    /// handler that it should send to the hanging get.
    change_function: ChangeFunction<T>,

    /// If true, should send value next time watch
    /// is called or if there is a hanging watch.
    should_send: bool,

    /// If present, represents the updated value waiting to be sent on the next watch.
    pending_value: Option<T>,
}

impl<T: Clone> HangingGetController<T> {
    fn new() -> HangingGetController<T> {
        HangingGetController {
            last_sent_value: None,
            change_function: Box::new(|_old: &T, _new: &T| true),
            should_send: true,
            pending_value: None,
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
        self.pending_value = Some((*new_value).clone());
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
        self.pending_value = None;
    }
}

/// Trait that should be implemented to send data to the hanging get watcher.
pub trait Sender<T> {
    fn send_response(self, data: T);
}

/// Handler for hanging gets from fakes.
pub struct HangingGetHandler<T, ST>
where
    T: Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
{
    /// The responders that need to be notified of the updates.
    pending_responders: Vec<ST>,

    /// The corresponding controller for the handler.
    controller: HangingGetController<T>,
}

impl<T: Clone, ST> HangingGetHandler<T, ST>
where
    T: Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
{
    pub async fn create(
        mut on_update_receiver: UnboundedReceiver<T>,
    ) -> Arc<Mutex<HangingGetHandler<T, ST>>> {
        // Create the hanging get handler instance.
        let hanging_get_handler = Arc::new(Mutex::new(HangingGetHandler::<T, ST> {
            pending_responders: Vec::new(),
            controller: HangingGetController::new(),
        }));

        // Start listening on the fake for the changes and call the on_change when it comes through.
        let hanging_get_handler_clone = hanging_get_handler.clone();
        fasync::Task::spawn(async move {
            while let Some(updated_data) = on_update_receiver.next().await {
                let mut handler_lock = hanging_get_handler_clone.lock().await;
                handler_lock.on_change(updated_data).await;
            }
        })
        .detach();

        hanging_get_handler
    }

    /// Park a new hanging get in the handler.
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
        self.pending_responders.push(responder);

        let pending_value_clone = match &self.controller.pending_value {
            Some(val) => Some(val.clone()),
            None => None,
        };
        if self.controller.on_watch() && pending_value_clone.is_some() {
            self.send_if_needed(pending_value_clone.unwrap()).await;
        }
    }

    /// Called when receiving a notification that value has changed.
    async fn on_change(&mut self, updated_data: T) {
        if self.controller.on_change(&updated_data.clone()) {
            self.send_if_needed(updated_data).await;
        }
    }

    /// Called when receiving a notification that value has changed.
    async fn send_if_needed(&mut self, response: T) {
        if !self.pending_responders.is_empty() {
            while let Some(responder) = self.pending_responders.pop() {
                responder.send_response(response.clone());
            }
            self.controller.on_send(response);
        }
    }
}
