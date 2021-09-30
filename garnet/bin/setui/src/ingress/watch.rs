// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Watch request handling.
//!
//! This mod defines the components for handling hanging-get, or "watch", [Requests](Request). These
//! requests return a value to the requester when a value different from the previously returned /
//! value is available. This pattern is common across the various setting service interfaces.
//! Since there is context involved between watch requests, these job workloads are [Sequential].
//!
//! Users of these components define three implementations to create "watch"-related jobs. First,
//! implementations of [From<SettingInfo>] and [From<Error>] are needed. Since these requests will
//! always return a value on success, the request handling automatically converts the [SettingInfo].
//! The built-in conversion to the user type with the [From] trait implementation helps reduce the
//! explicit conversion in the responding code. Lastly, the user must implement [Responder], which
//! returns a [Result] converted from the [Response](crate::handler::base::Response) returned from
//! the setting service.

use crate::base::{SettingInfo, SettingType};
use crate::handler::base::Error;
use crate::handler::base::{Payload, Request};
use crate::job::data::{self, Data, Key};
use crate::job::work::{Load, Sequential};
use crate::job::Job;
use crate::job::Signature;
use crate::message::base::Audience;
use crate::service::{message, Address};
use crate::trace;
use crate::trace::TracingNonce;
use async_trait::async_trait;
use fuchsia_syslog::fx_log_warn;
use std::collections::HashMap;
use std::marker::PhantomData;

/// The key used to store the last value sent. This cache is scoped to the
/// [Job's Signature](Signature).
const LAST_VALUE_KEY: &str = "LAST_VALUE";

/// A custom function used to compare an existing setting value with a new one to determine if
/// listeners should be notified. If true is returned, listeners will be notified.
type ChangeFunction = Box<dyn Fn(&SettingInfo, &SettingInfo) -> bool + Send + Sync + 'static>;

/// [Responder] is a trait for handing back results of a watch request. It is unique from other
/// work responders, since [Work] consumers expect a value to be present on success. The Responder
/// specifies the conversions for [Response](crate::handler::base::Response).
pub trait Responder<
    R: From<SettingInfo> + Send + Sync + 'static,
    E: From<Error> + Send + Sync + 'static,
>
{
    fn respond(self, response: Result<R, E>);
}

pub struct Work<
    R: From<SettingInfo> + Send + Sync + 'static,
    E: From<Error> + Send + Sync + 'static,
    T: Responder<R, E> + Send + Sync + 'static,
> {
    setting_type: SettingType,
    signature: Signature,
    responder: T,
    change_function: Option<ChangeFunction>,
    _response_type: PhantomData<R>,
    _error_type: PhantomData<E>,
}

impl<
        R: From<SettingInfo> + Send + Sync + 'static,
        E: From<Error> + Send + Sync + 'static,
        T: Responder<R, E> + Send + Sync + 'static,
    > Work<R, E, T>
{
    pub(crate) fn new(setting_type: SettingType, responder: T) -> Self
    where
        T: 'static,
    {
        Self {
            setting_type,
            signature: Signature::new::<T>(),
            responder,
            change_function: None,
            _response_type: PhantomData,
            _error_type: PhantomData,
        }
    }

    // TODO(fxbug.dev/79044): remove allow dead_code once used
    #[allow(dead_code)]
    pub(crate) fn with_change_function(
        setting_type: SettingType,
        responder: T,
        change_function: ChangeFunction,
    ) -> Self {
        Self {
            setting_type,
            signature: Signature::new::<T>(),
            responder,
            change_function: Some(change_function),
            _response_type: PhantomData,
            _error_type: PhantomData,
        }
    }

    /// Returns a non-empty value when the last response should be returned to the caller. The lack
    /// of a response indicates the watched value has not changed and watching will continue.
    fn process_response(
        &self,
        response: Result<Payload, anyhow::Error>,
        store: &mut HashMap<Key, Data>,
    ) -> Option<Result<SettingInfo, Error>> {
        match response {
            Ok(Payload::Response(Ok(Some(setting_info)))) => {
                let key = Key::Identifier(LAST_VALUE_KEY);

                let return_val = match (store.get(&key), self.change_function.as_ref()) {
                    // Apply the change function to determine if we should notify listeners.
                    (Some(Data::SettingInfo(info)), Some(change_function))
                        if !(change_function)(info, &setting_info) =>
                    {
                        None
                    }
                    // No change function used, compare the new info with the old.
                    (Some(Data::SettingInfo(info)), None) if *info == setting_info => None,
                    _ => Some(Ok(setting_info)),
                };

                if let Some(Ok(ref info)) = return_val {
                    let _ = store.insert(key, Data::SettingInfo(info.clone()));
                }

                return_val
            }
            Ok(Payload::Response(Err(error))) => Some(Err(error)),
            Err(error) => {
                fx_log_warn!(
                    "An error occurred while watching {:?}:{:?}",
                    self.setting_type,
                    error
                );
                Some(Err(match error.root_cause().downcast_ref::<Error>() {
                    Some(error) => error.clone(),
                    _ => crate::handler::base::Error::CommunicationError,
                }))
            }
            _ => {
                panic!("invalid variant {:?}", response);
            }
        }
    }
}

#[async_trait]
impl<
        R: From<SettingInfo> + Send + Sync + 'static,
        E: From<Error> + Send + Sync + 'static,
        T: Responder<R, E> + Send + Sync + 'static,
    > Sequential for Work<R, E, T>
{
    async fn execute(
        self: Box<Self>,
        messenger: message::Messenger,
        store_handle: data::StoreHandle,
        nonce: TracingNonce,
    ) {
        trace!(nonce, "Sequential Work execute");
        // Lock store for Job signature group.
        let mut store = store_handle.lock().await;

        // Begin listening for changes before fetching current value to ensure no changes are
        // missed.
        let mut listen_receptor = messenger
            .message(
                Payload::Request(Request::Listen).into(),
                Audience::Address(Address::Handler(self.setting_type)),
            )
            .send();

        // Get current value.
        let mut get_receptor = messenger
            .message(
                Payload::Request(Request::Get).into(),
                Audience::Address(Address::Handler(self.setting_type)),
            )
            .send();

        // If a value was returned from the get call and considered updated (no existing or
        // different), return new value immediately.
        trace!(nonce, "Get first response");
        if let Some(response) = self.process_response(
            get_receptor.next_of::<Payload>().await.map(|(payload, _)| payload),
            &mut store,
        ) {
            self.responder.respond(response.map(R::from).map_err(E::from));
            return;
        }

        // Otherwise, loop a watch until an updated value is available
        loop {
            trace!(nonce, "Get looped response");
            if let Some(response) = self.process_response(
                listen_receptor.next_of::<Payload>().await.map(|(payload, _)| payload),
                &mut store,
            ) {
                self.responder.respond(response.map(R::from).map_err(E::from));
                return;
            }
        }
    }
}

impl<
        R: From<SettingInfo> + Send + Sync + 'static,
        E: From<Error> + Send + Sync + 'static,
        T: Responder<R, E> + Send + Sync + 'static,
    > From<Work<R, E, T>> for Job
{
    fn from(work: Work<R, E, T>) -> Job {
        let signature = work.signature;
        Job::new(Load::Sequential(Box::new(work), signature))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::base::{SettingInfo, UnknownInfo};
    use crate::message::base::MessengerType;
    use crate::message::MessageHubUtil;
    use crate::service::{Address, MessageHub};
    use fuchsia_async as fasync;
    use futures::channel::oneshot::Sender;
    use futures::lock::Mutex;
    use matches::assert_matches;
    use std::sync::Arc;

    struct TestResponder {
        sender: Sender<Result<SettingInfo, Error>>,
    }

    impl TestResponder {
        pub(crate) fn new(sender: Sender<Result<SettingInfo, Error>>) -> Self {
            Self { sender }
        }
    }

    impl Responder<SettingInfo, Error> for TestResponder {
        fn respond(self, response: Result<SettingInfo, Error>) {
            self.sender.send(response).expect("send should succeed");
        }
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_watch_basic_functionality() {
        // Create store for job.
        let store_handle = Arc::new(Mutex::new(HashMap::new()));

        let get_info = SettingInfo::Unknown(UnknownInfo(true));
        let listen_info = SettingInfo::Unknown(UnknownInfo(false));

        // Make sure the first job execution returns the initial value (retrieved through get).
        verify_watch(
            store_handle.clone(),
            listen_info.clone(),
            get_info.clone(),
            get_info.clone(),
            None,
        )
        .await;
        // Make sure the second job execution returns the value returned through watching (listen
        // value).
        verify_watch(
            store_handle.clone(),
            listen_info.clone(),
            get_info.clone(),
            listen_info.clone(),
            None,
        )
        .await;
    }

    async fn verify_watch(
        store_handle: data::StoreHandle,
        listen_info: SettingInfo,
        get_info: SettingInfo,
        expected_info: SettingInfo,
        change_function: Option<ChangeFunction>,
    ) {
        // Create MessageHub for communication between components.
        let message_hub_delegate = MessageHub::create_hub();

        // Create mock handler endpoint to receive request.
        let mut handler_receiver = message_hub_delegate
            .create(MessengerType::Addressable(Address::Handler(SettingType::Unknown)))
            .await
            .expect("handler messenger should be created")
            .1;

        let (response_tx, response_rx) =
            futures::channel::oneshot::channel::<Result<SettingInfo, Error>>();

        let work = match change_function {
            None => Box::new(Work::new(SettingType::Unknown, TestResponder::new(response_tx))),
            Some(change_function) => Box::new(Work::with_change_function(
                SettingType::Unknown,
                TestResponder::new(response_tx),
                change_function,
            )),
        };

        // Execute work on async task.
        let work_messenger = message_hub_delegate
            .create(MessengerType::Unbound)
            .await
            .expect("messenger should be created")
            .0;

        let work_messenger_signature = work_messenger.get_signature();
        fasync::Task::spawn(work.execute(work_messenger, store_handle, 0)).detach();

        // Ensure the listen request is received from the right sender.
        let (listen_request, listen_client) = handler_receiver
            .next_of::<Payload>()
            .await
            .expect("should successfully receive a listen request");
        assert_matches!(listen_request, Payload::Request(Request::Listen));
        assert!(listen_client.get_author() == work_messenger_signature);

        // Listen should be followed by a get request.
        let (get_request, get_client) = handler_receiver
            .next_of::<Payload>()
            .await
            .expect("should successfully receive a get request");
        assert_matches!(get_request, Payload::Request(Request::Get));
        assert!(get_client.get_author() == work_messenger_signature);

        // Reply to the get request.
        let _ = get_client.reply(Payload::Response(Ok(Some(get_info))).into()).send();
        let _ = listen_client.reply(Payload::Response(Ok(Some(listen_info))).into()).send();

        assert_matches!(response_rx.await.expect("should receive successful response"),
                Ok(x) if x == expected_info);
    }

    // This test verifies that custom change functions work by using a custom change function that
    // always says a new value is different, even if the actual value is unchanged.
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_custom_change_function() {
        // Create store for job.
        let store_handle = Arc::new(Mutex::new(HashMap::new()));

        // Pre-fill the storage with the value so that the initial get will not trigger a response.
        let unchanged_info = SettingInfo::Unknown(UnknownInfo(true));
        let _ = store_handle
            .lock()
            .await
            .insert(Key::Identifier(LAST_VALUE_KEY), Data::SettingInfo(unchanged_info.clone()));

        verify_watch(
            store_handle,
            // Send the same value on both the get and listen requests so that the default change
            // function would not trigger a response to the client.
            unchanged_info.clone(),
            unchanged_info.clone(),
            unchanged_info,
            // Use a custom change function that always reports a change.
            Some(Box::new(move |_old: &SettingInfo, _new: &SettingInfo| true)),
        )
        .await;
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_error_propagation() {
        // Create MessageHub for communication between components.
        let message_hub_delegate = MessageHub::create_hub();

        let (response_tx, response_rx) =
            futures::channel::oneshot::channel::<Result<SettingInfo, Error>>();

        // Create a listen request to a non-existent end-point.
        let work = Box::new(Work::new(SettingType::Unknown, TestResponder::new(response_tx)));

        let work_messenger = message_hub_delegate
            .create(MessengerType::Unbound)
            .await
            .expect("messenger should be created")
            .0;

        // Execute work on async task.
        fasync::Task::spawn(work.execute(work_messenger, Arc::new(Mutex::new(HashMap::new())), 0))
            .detach();

        // Ensure an error is returned by the executed work.
        assert_matches!(response_rx.await.expect("should receive successful response"),
                Err(x) if x == crate::handler::base::Error::CommunicationError);
    }
}
