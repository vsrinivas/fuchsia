// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;

use fidl::endpoints::{ProtocolMarker, Request as FidlRequest};
use fuchsia_async as fasync;
use futures::lock::Mutex;

use crate::base::{SettingInfo, SettingType};
use crate::fidl_processor::processor::{ProcessingUnit, RequestResultCreator};
use crate::handler::base::{Error, Payload as HandlerPayload, Request as SettingRequest, Response};
use crate::hanging_get_handler::{HangingGetHandler, Sender};
use crate::message::base::Audience;
use crate::service;
use crate::ExitSender;
use std::hash::Hash;

/// Convenience macro to make a setting request and send the result to a responder.
#[macro_export]
macro_rules! request_respond {
    (
        $context:ident,
        $responder:ident,
        $setting_type:expr,
        $request:expr,
        $success:expr,
        $error:expr,
        $marker:ty
        $(, $debug:expr)?
    ) => {{
        use ::fidl::endpoints::ProtocolMarker;
        #[allow(dead_code)]
        use $crate::fidl_common::FidlResponseErrorLogger;

        match $context.request($setting_type, $request).await {
            Ok(_) => $responder.send(&mut $success),
            _ => $responder.send(&mut $error),
        }
        .log_fidl_response_error(<$marker as ProtocolMarker>::DEBUG_NAME);
        $($debug;)?
    }};
}

/// `RequestCallback` closures are handed a request and the surrounding
/// context. They are expected to hand back a future that returns when the
/// request is processed. The returned value is a result with an optional
/// request, containing None if not processed and the original request
/// otherwise.
pub(crate) type RequestCallback<P, T, ST, K> =
    Box<dyn Fn(RequestContext<T, ST, K>, FidlRequest<P>) -> RequestResultCreator<'static, P>>;

type ChangeFunction<T> = Box<dyn Fn(&T, &T) -> bool + Send + Sync + 'static>;

/// `RequestContext` is passed to each request callback to provide resources,
/// Note that we do not directly expose the hanging get handler so that we can
/// better control its lifetime.
pub(crate) struct RequestContext<T, ST, K = String>
where
    T: From<SettingInfo> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
    K: Eq + Hash + Clone + Send + Sync + 'static,
{
    service_messenger: crate::service::message::Messenger,
    hanging_get_handler: Arc<Mutex<HangingGetHandler<T, ST, K>>>,
    exit_tx: ExitSender,
}

impl<T, ST, K> RequestContext<T, ST, K>
where
    T: From<SettingInfo> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
    K: Eq + Hash + Clone + Send + Sync + 'static,
{
    pub(crate) async fn request(
        &self,
        setting_type: SettingType,
        request: SettingRequest,
    ) -> Response {
        let mut receptor = self
            .service_messenger
            .message(
                HandlerPayload::Request(request).into(),
                Audience::Address(service::Address::Handler(setting_type)),
            )
            .send();

        if let Ok((HandlerPayload::Response(result), _)) =
            receptor.next_of::<HandlerPayload>().await
        {
            return result;
        }

        Err(Error::CommunicationError)
    }

    pub(crate) async fn watch(&self, responder: ST, close_on_error: bool) {
        let mut hanging_get_lock = self.hanging_get_handler.lock().await;
        hanging_get_lock
            .watch(responder, if close_on_error { Some(self.exit_tx.clone()) } else { None })
            .await;
    }

    pub(crate) async fn watch_with_change_fn(
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

impl<T, ST, K> Clone for RequestContext<T, ST, K>
where
    T: From<SettingInfo> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
    K: Eq + Hash + Clone + Send + Sync + 'static,
{
    fn clone(&self) -> RequestContext<T, ST, K> {
        RequestContext {
            service_messenger: self.service_messenger.clone(),
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
pub(crate) struct SettingProcessingUnit<P, T, ST, K>
where
    P: ProtocolMarker,
    T: From<SettingInfo> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
    K: Eq + Hash + Clone + Send + Sync + 'static,
{
    callback: RequestCallback<P, T, ST, K>,
    hanging_get_handler: Arc<Mutex<HangingGetHandler<T, ST, K>>>,
}

impl<P, T, ST, K> SettingProcessingUnit<P, T, ST, K>
where
    P: ProtocolMarker,
    T: From<SettingInfo> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
    K: Eq + Hash + Clone + Send + Sync + 'static,
{
    pub(crate) async fn new(
        setting_type: SettingType,
        messenger: service::message::Messenger,
        callback: RequestCallback<P, T, ST, K>,
    ) -> Self {
        Self {
            callback,
            hanging_get_handler: HangingGetHandler::create(messenger.clone(), setting_type).await,
        }
    }
}

impl<P, T, ST, K> Drop for SettingProcessingUnit<P, T, ST, K>
where
    P: ProtocolMarker,
    T: From<SettingInfo> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
    K: Eq + Hash + Clone + Send + Sync + 'static,
{
    fn drop(&mut self) {
        let hanging_get_handler = self.hanging_get_handler.clone();
        fasync::Task::local(async move {
            hanging_get_handler.lock().await.close();
        })
        .detach();
    }
}

impl<P, T, ST, K> ProcessingUnit<P> for SettingProcessingUnit<P, T, ST, K>
where
    P: ProtocolMarker,
    T: From<SettingInfo> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
    K: Eq + Hash + Clone + Send + Sync + 'static,
{
    fn process(
        &self,
        service_messenger: crate::service::message::Messenger,
        request: FidlRequest<P>,
        exit_tx: ExitSender,
    ) -> RequestResultCreator<'static, P> {
        let context = RequestContext {
            service_messenger,
            hanging_get_handler: self.hanging_get_handler.clone(),
            exit_tx,
        };

        (self.callback)(context, request)
    }
}
