// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;

use fidl::endpoints::{Request, ServiceMarker};
use fuchsia_async as fasync;
use futures::lock::Mutex;

use crate::fidl_processor::processor::{ProcessingUnit, RequestResultCreator};
use crate::internal::switchboard::{self, Action, Address, Payload};
use crate::message::base::{self, Audience};
use crate::switchboard::base::{
    SettingRequest, SettingResponse, SettingResponseResult, SettingType, SwitchboardError,
};
use crate::switchboard::hanging_get_handler::{HangingGetHandler, Sender};
use crate::ExitSender;
use std::hash::Hash;

/// Convenience macro to make a switchboard request and send the result to a responder.
#[macro_export]
macro_rules! request_respond {
    (
        $context:ident,
        $responder:ident,
        $setting_type:expr,
        $request:expr,
        $success:expr,
        $error:expr,
        $marker:ty $(,)?
    ) => {{
        use ::fidl::endpoints::ServiceMarker;
        use $crate::switchboard::base::FidlResponseErrorLogger;

        match $context.request($setting_type, $request).await {
            Ok(_) => $responder.send(&mut $success),
            _ => $responder.send(&mut $error),
        }
        .log_fidl_response_error(<$marker as ServiceMarker>::DEBUG_NAME);
    }};
}

/// `RequestCallback` closures are handed a request and the surrounding
/// context. They are expected to hand back a future that returns when the
/// request is processed. The returned value is a result with an optional
/// request, containing None if not processed and the original request
/// otherwise.
pub type RequestCallback<S, T, ST, K, P, A> =
    Box<dyn Fn(RequestContext<T, ST, K, P, A>, Request<S>) -> RequestResultCreator<'static, S>>;

type ChangeFunction<T> = Box<dyn Fn(&T, &T) -> bool + Send + Sync + 'static>;

/// `RequestContext` is passed to each request callback to provide resources,
/// such as the switchboard and hanging get functionality. Note that we do not
/// directly expose the hanging get handler so that we can better control its
/// lifetime.
pub struct RequestContext<T, ST, K = String, P = Payload, A = Address>
where
    T: From<SettingResponse> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
    K: Eq + Hash + Clone + Send + Sync + 'static,
    P: base::Payload + 'static,
    A: base::Address + 'static,
{
    switchboard_messenger: crate::message::messenger::MessengerClient<P, A>,
    hanging_get_handler: Arc<Mutex<HangingGetHandler<T, ST, K>>>,
    exit_tx: ExitSender,
}

impl<T, ST, K> RequestContext<T, ST, K, Payload, Address>
where
    T: From<SettingResponse> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
    K: Eq + Hash + Clone + Send + Sync + 'static,
{
    pub async fn request(
        &self,
        setting_type: SettingType,
        request: SettingRequest,
    ) -> SettingResponseResult {
        let mut receptor = self
            .switchboard_messenger
            .message(
                Payload::Action(Action::Request(setting_type, request)),
                Audience::Address(Address::Switchboard),
            )
            .send();

        if let Ok((Payload::Action(Action::Response(result)), _)) = receptor.next_payload().await {
            return result;
        }

        Err(SwitchboardError::CommunicationError)
    }

    pub async fn watch(&self, responder: ST, close_on_error: bool) {
        let mut hanging_get_lock = self.hanging_get_handler.lock().await;
        hanging_get_lock
            .watch(responder, if close_on_error { Some(self.exit_tx.clone()) } else { None })
            .await;
    }

    pub async fn watch_with_change_fn(
        &self,
        change_function_key: K,
        change_function: ChangeFunction<T>,
        responder: ST,
        close_on_error: bool,
    ) {
        let mut hanging_get_lock = self.hanging_get_handler.lock().await;
        hanging_get_lock
            .watch_with_change_fn(
                Some(change_function_key),
                change_function,
                responder,
                if close_on_error { Some(self.exit_tx.clone()) } else { None },
            )
            .await;
    }
}

impl<T, ST, K, P, A> Clone for RequestContext<T, ST, K, P, A>
where
    T: From<SettingResponse> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
    K: Eq + Hash + Clone + Send + Sync + 'static,
    P: base::Payload + 'static,
    A: base::Address + 'static,
{
    fn clone(&self) -> RequestContext<T, ST, K, P, A> {
        RequestContext {
            switchboard_messenger: self.switchboard_messenger.clone(),
            hanging_get_handler: self.hanging_get_handler.clone(),
            exit_tx: self.exit_tx.clone(),
        }
    }
}

/// `SettingProcessingUnit` is a concrete implementation of the ProcessingUnit
/// trait, allowing a [`RequestCallback`] to participate in stream request
/// processing. The SettingProcessingUnit maintains a hanging get handler keyed
/// to the constructed type.
///
/// [`RequestCallback`]: type.RequestCallback.html
pub struct SettingProcessingUnit<S, T, ST, K, P = Payload, A = Address>
where
    S: ServiceMarker,
    T: From<SettingResponse> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
    K: Eq + Hash + Clone + Send + Sync + 'static,
    P: base::Payload + 'static,
    A: base::Address + 'static,
{
    callback: RequestCallback<S, T, ST, K, P, A>,
    hanging_get_handler: Arc<Mutex<HangingGetHandler<T, ST, K>>>,
}

impl<S, T, ST, K> SettingProcessingUnit<S, T, ST, K, Payload, Address>
where
    S: ServiceMarker,
    T: From<SettingResponse> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
    K: Eq + Hash + Clone + Send + Sync + 'static,
{
    pub(crate) async fn new(
        setting_type: SettingType,
        switchboard_messenger: switchboard::message::Messenger,
        callback: RequestCallback<S, T, ST, K, Payload, Address>,
    ) -> Self {
        Self {
            callback,
            hanging_get_handler: HangingGetHandler::create(
                switchboard_messenger.clone(),
                setting_type,
            )
            .await,
        }
    }
}

impl<S, T, ST, K, P, A> Drop for SettingProcessingUnit<S, T, ST, K, P, A>
where
    S: ServiceMarker,
    T: From<SettingResponse> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
    K: Eq + Hash + Clone + Send + Sync + 'static,
    P: base::Payload + 'static,
    A: base::Address + 'static,
{
    fn drop(&mut self) {
        let hanging_get_handler = self.hanging_get_handler.clone();
        fasync::Task::local(async move {
            hanging_get_handler.lock().await.close();
        })
        .detach();
    }
}

impl<S, T, ST, K, P, A> ProcessingUnit<S, P, A> for SettingProcessingUnit<S, T, ST, K, P, A>
where
    S: ServiceMarker,
    T: From<SettingResponse> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
    K: Eq + Hash + Clone + Send + Sync + 'static,
    P: base::Payload + 'static,
    A: base::Address + 'static,
{
    fn process(
        &self,
        switchboard_messenger: crate::message::messenger::MessengerClient<P, A>,
        request: Request<S>,
        exit_tx: ExitSender,
    ) -> RequestResultCreator<'static, S> {
        let context = RequestContext {
            switchboard_messenger: switchboard_messenger.clone(),
            hanging_get_handler: self.hanging_get_handler.clone(),
            exit_tx: exit_tx.clone(),
        };

        return (self.callback)(context.clone(), request);
    }
}
