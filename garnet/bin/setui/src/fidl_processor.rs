// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::sync::Arc;

use anyhow::Error;
use fidl::endpoints::{Request, ServiceMarker};
use fuchsia_async as fasync;
use futures::future::LocalBoxFuture;
use futures::lock::Mutex;
use futures::{FutureExt, StreamExt, TryStreamExt};

use crate::internal::switchboard;
use crate::message::base::Audience;
use crate::switchboard::base::{
    SettingRequest, SettingResponse, SettingResponseResult, SettingType, SwitchboardError,
};
use crate::switchboard::hanging_get_handler::{HangingGetHandler, Sender};
use crate::ExitSender;
use std::hash::Hash;

pub type RequestResultCreator<'a, S> = LocalBoxFuture<'a, Result<Option<Request<S>>, Error>>;

type ChangeFunction<T> = Box<dyn Fn(&T, &T) -> bool + Send + Sync + 'static>;

/// The RequestContext is passed to each request callback to provide resources,
/// such as the switchboard and hanging get functionality. Note that we do not
/// directly expose the hanging get handler so that we can better control its
/// lifetime.
pub struct RequestContext<T, ST, K = String>
where
    T: From<SettingResponse> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
    K: Eq + Hash + Clone + Send + Sync + 'static,
{
    switchboard_messenger: switchboard::message::Messenger,
    hanging_get_handler: Arc<Mutex<HangingGetHandler<T, ST, K>>>,
    exit_tx: ExitSender,
}

impl<T, ST, K> RequestContext<T, ST, K>
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
                switchboard::Payload::Action(switchboard::Action::Request(setting_type, request)),
                Audience::Address(switchboard::Address::Switchboard),
            )
            .send();

        if let Ok((switchboard::Payload::Action(switchboard::Action::Response(result)), _)) =
            receptor.next_payload().await
        {
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

#[macro_export]
macro_rules! request_respond {
    ($context:ident, $responder:ident, $setting_type:expr, $request:expr, $success:expr, $error:expr, $marker:expr) => {
        match $context.request($setting_type, $request).await {
            Ok(_) => $responder.send(&mut $success),
            _ => $responder.send(&mut $error),
        }
        .log_fidl_response_error($marker);
    };
}

impl<T, ST, K> Clone for RequestContext<T, ST, K>
where
    T: From<SettingResponse> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
    K: Eq + Hash + Clone + Send + Sync + 'static,
{
    fn clone(&self) -> RequestContext<T, ST, K> {
        RequestContext {
            switchboard_messenger: self.switchboard_messenger.clone(),
            hanging_get_handler: self.hanging_get_handler.clone(),
            exit_tx: self.exit_tx.clone(),
        }
    }
}

/// RequestCallback closures are handed a request and the surrounding
/// context. They are expected to hand back a future that returns when the
/// request is processed. The returned value is a result with an optional
/// request, containing None if not processed and the original request
/// otherwise.
pub type RequestCallback<S, T, ST, K> =
    Box<dyn Fn(RequestContext<T, ST, K>, Request<S>) -> RequestResultCreator<'static, S>>;
pub type RequestStream<S> = <S as ServiceMarker>::RequestStream;

/// A processing unit is an entity that is able to process a stream request and
/// indicate whether the request was consumed.
trait ProcessingUnit<S>
where
    S: ServiceMarker,
{
    fn process(
        &self,
        switchboard_messenger: switchboard::message::Messenger,
        request: Request<S>,
        exit_tx: ExitSender,
    ) -> RequestResultCreator<'static, S>;
}

/// SettingProcessingUnit is a concrete implementation of the ProcessingUnit
/// trait, allowing a RequestCallback to participate in stream request
/// processing. The SettingProcessingUnit maintains a hanging get handler keyed
/// to the constructed type.
struct SettingProcessingUnit<S, T, ST, K>
where
    S: ServiceMarker,
    T: From<SettingResponse> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
    K: Eq + Hash + Clone + Send + Sync + 'static,
{
    callback: RequestCallback<S, T, ST, K>,
    hanging_get_handler: Arc<Mutex<HangingGetHandler<T, ST, K>>>,
}

impl<S, T, ST, K> SettingProcessingUnit<S, T, ST, K>
where
    S: ServiceMarker,
    T: From<SettingResponse> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
    K: Eq + Hash + Clone + Send + Sync + 'static,
{
    async fn new(
        setting_type: SettingType,
        switchboard_messenger: switchboard::message::Messenger,
        callback: RequestCallback<S, T, ST, K>,
    ) -> Self {
        Self {
            callback: callback,
            hanging_get_handler: HangingGetHandler::create(
                switchboard_messenger.clone(),
                setting_type,
            )
            .await,
        }
    }
}

impl<S, T, ST, K> Drop for SettingProcessingUnit<S, T, ST, K>
where
    S: ServiceMarker,
    T: From<SettingResponse> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
    K: Eq + Hash + Clone + Send + Sync + 'static,
{
    fn drop(&mut self) {
        let hanging_get_handler = self.hanging_get_handler.clone();
        fasync::spawn_local(async move {
            hanging_get_handler.lock().await.close();
        });
    }
}

impl<S, T, ST, K> ProcessingUnit<S> for SettingProcessingUnit<S, T, ST, K>
where
    S: ServiceMarker,
    T: From<SettingResponse> + Send + Sync + 'static,
    ST: Sender<T> + Send + Sync + 'static,
    K: Eq + Hash + Clone + Send + Sync + 'static,
{
    fn process(
        &self,
        switchboard_messenger: switchboard::message::Messenger,
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

/// The FidlProcessor delegates request processing across a number of processing
/// units. There should be a single FidlProcessor per stream.
pub struct FidlProcessor<S>
where
    S: ServiceMarker,
{
    request_stream: RequestStream<S>,
    switchboard_messenger: switchboard::message::Messenger,
    processing_units: Vec<Box<dyn ProcessingUnit<S>>>,
}

impl<S> FidlProcessor<S>
where
    S: ServiceMarker,
{
    pub async fn new(
        stream: RequestStream<S>,
        switchboard_messenger: switchboard::message::Messenger,
    ) -> Self {
        Self { request_stream: stream, switchboard_messenger, processing_units: Vec::new() }
    }

    pub async fn register<V, SV, K>(
        &mut self,
        setting_type: SettingType,
        callback: RequestCallback<S, V, SV, K>,
    ) where
        V: From<SettingResponse> + Send + Sync + 'static,
        SV: Sender<V> + Send + Sync + 'static,
        K: Eq + Hash + Clone + Send + Sync + 'static,
    {
        let processing_unit = Box::new(
            SettingProcessingUnit::<S, V, SV, K>::new(
                setting_type,
                self.switchboard_messenger.clone(),
                callback,
            )
            .await,
        );
        self.processing_units.push(processing_unit);
    }

    // Process the stream. Note that we pass in the processor here as it cannot
    // be used again afterwards.
    pub async fn process(mut self) {
        let (exit_tx, mut exit_rx) = futures::channel::mpsc::unbounded::<()>();
        loop {
            // Note that we create a fuse outside the select! to prevent it from
            // being called from outside the select! macro.
            let fused_stream = self.request_stream.try_next().fuse();
            futures::pin_mut!(fused_stream);

            futures::select! {
                request = fused_stream => {
                    if let Ok(Some(mut req)) = request {
                        for processing_unit in &self.processing_units {
                            // If the processing unit consumes the request (a non-empty
                            // result is returned) or an error occurs, exit processing this
                            // request. Otherwise, hand the request to the next processing
                            // unit
                            match processing_unit.process(
                                    self.switchboard_messenger.clone(),
                                    req, exit_tx.clone()).await {
                                Ok(Some(return_request)) => {
                                    req = return_request;
                                }
                                _ => {
                                    break
                                }
                            }
                        }
                    } else {
                       return;
                    }
                }
                exit = exit_rx.next() => {
                    return;
                }
            }
        }
    }
}
