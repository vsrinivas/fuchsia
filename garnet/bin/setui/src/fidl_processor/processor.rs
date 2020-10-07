// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl::endpoints::{Request, ServiceMarker};
use futures::future::LocalBoxFuture;
use futures::{FutureExt, StreamExt, TryStreamExt};

use crate::fidl_processor::policy::{
    PolicyProcessingUnit, RequestCallback as PolicyRequestCallback,
};
use crate::fidl_processor::settings::{
    RequestCallback as SettingsRequestCallback, SettingProcessingUnit,
};
use crate::internal::policy;
use crate::internal::switchboard;
use crate::message::base::{Address, Payload};
use crate::message::messenger::MessengerClient;
use crate::switchboard::base::{SettingResponse, SettingType};
use crate::switchboard::hanging_get_handler::Sender;
use crate::ExitSender;
use std::hash::Hash;

pub type RequestResultCreator<'a, S> = LocalBoxFuture<'a, Result<Option<Request<S>>, Error>>;
pub type RequestStream<S> = <S as ServiceMarker>::RequestStream;

/// `ProcessingUnit` is an entity that is able to process a stream request and indicate whether the
/// request was consumed.
pub trait ProcessingUnit<S, P, A>
where
    S: ServiceMarker,
    P: Payload,
    A: Address,
{
    /// Processes a request provided by the fidl processor.
    ///
    /// If the request is not processed, this method should return the original `request` that was
    /// passed in. If the request was successfully processed, `None` should be returned. If an error
    /// was encountered, an appropriate error should be returned.
    ///
    /// Parameters:
    /// `messenger`: a message hub connection that can be used to send and receive messages
    /// `request`: the request to process
    /// `exit_tx`: a channel to indicate that the connection is closed and for processing to stop
    fn process(
        &self,
        messenger: MessengerClient<P, A>,
        request: Request<S>,
        exit_tx: ExitSender,
    ) -> RequestResultCreator<'static, S>;
}

/// `BaseFidlProcessor` delegates request processing for setting requests across a number of
/// processing units. There should be a single FidlProcessor per stream.
// TODO(fxbug.dev/61243): write tests for this class
pub struct BaseFidlProcessor<S, P, A>
where
    S: ServiceMarker,
    P: Payload + 'static,
    A: Address + 'static,
{
    request_stream: RequestStream<S>,
    messenger: MessengerClient<P, A>,
    processing_units: Vec<Box<dyn ProcessingUnit<S, P, A>>>,
}

impl<S, P, A> BaseFidlProcessor<S, P, A>
where
    S: ServiceMarker,
    P: Payload,
    A: Address,
{
    pub fn new(request_stream: RequestStream<S>, messenger: MessengerClient<P, A>) -> Self {
        Self { request_stream, messenger, processing_units: Vec::new() }
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
                            // If the processing unit consumes the request (an empty
                            // result is returned) or an error occurs, exit processing this
                            // request. Otherwise, hand the request to the next processing
                            // unit
                            match processing_unit.process(
                                    self.messenger.clone(),
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

/// Wraps [`BaseFidlProcessor`] for use with FIDL APIs in the fuchsia.settings namespace that send
/// and receive messages through the switchboard.
///
/// [`BaseFidlProcessor`]: struct.BaseFidlProcessor.html
pub struct SettingsFidlProcessor<S>
where
    S: ServiceMarker,
{
    base_processor: BaseFidlProcessor<S, switchboard::Payload, switchboard::Address>,
}

impl<S> SettingsFidlProcessor<S>
where
    S: ServiceMarker,
{
    pub async fn new(stream: RequestStream<S>, messenger: switchboard::message::Messenger) -> Self {
        Self { base_processor: BaseFidlProcessor::new(stream, messenger) }
    }

    /// Registers a fidl processing unit for setting requests.
    pub async fn register<V, SV, K>(
        &mut self,
        setting_type: SettingType,
        callback: SettingsRequestCallback<S, V, SV, K, switchboard::Payload, switchboard::Address>,
    ) where
        V: From<SettingResponse> + Send + Sync + 'static,
        SV: Sender<V> + Send + Sync + 'static,
        K: Eq + Hash + Clone + Send + Sync + 'static,
    {
        let processing_unit = Box::new(
            SettingProcessingUnit::<S, V, SV, K>::new(
                setting_type,
                self.base_processor.messenger.clone(),
                callback,
            )
            .await,
        );
        self.base_processor.processing_units.push(processing_unit);
    }

    pub async fn process(self) {
        self.base_processor.process().await
    }
}

/// Wraps [`BaseFidlProcessor`] for use with FIDL APIs in the fuchsia.settings.policy namespace that
/// send and receive messages through the policy message hub.
///
/// [`BaseFidlProcessor`]: struct.BaseFidlProcessor.html
pub struct PolicyFidlProcessor<S>
where
    S: ServiceMarker,
{
    base_processor: BaseFidlProcessor<S, policy::Payload, policy::Address>,
}

impl<S> PolicyFidlProcessor<S>
where
    S: ServiceMarker,
{
    // TODO(fxb/59705): remove annotation once used.
    #[allow(dead_code)]
    pub async fn new(stream: RequestStream<S>, messenger: policy::message::Messenger) -> Self {
        Self { base_processor: BaseFidlProcessor::new(stream, messenger) }
    }

    /// Registers a fidl processing unit for policy requests.
    // TODO(fxb/59705): remove annotation once used.
    #[allow(dead_code)]
    pub async fn register(
        &mut self,
        callback: PolicyRequestCallback<S, policy::Payload, policy::Address>,
    ) {
        let processing_unit = Box::new(PolicyProcessingUnit::<S>::new(callback));
        self.base_processor.processing_units.push(processing_unit);
    }

    // TODO(fxb/59705): remove annotation once used.
    #[allow(dead_code)]
    pub async fn process(self) {
        self.base_processor.process().await
    }
}
