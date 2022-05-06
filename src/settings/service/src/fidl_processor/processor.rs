// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl::endpoints::{ProtocolMarker, Request};
use futures::future::LocalBoxFuture;
use futures::{FutureExt, StreamExt, TryStreamExt};

use crate::base::{SettingInfo, SettingType};
use crate::fidl_processor::policy::{
    PolicyProcessingUnit, RequestCallback as PolicyRequestCallback,
};
use crate::fidl_processor::settings::{
    RequestCallback as SettingsRequestCallback, SettingProcessingUnit,
};
use crate::hanging_get_handler::Sender;
use crate::ExitSender;
use crate::{service, trace};
use std::hash::Hash;

pub type RequestResultCreator<'a, P> = LocalBoxFuture<'a, Result<Option<Request<P>>, Error>>;
pub type RequestStream<P> = <P as ProtocolMarker>::RequestStream;

/// `ProcessingUnit` is an entity that is able to process a stream request and indicate whether the
/// request was consumed.
pub(crate) trait ProcessingUnit<P>
where
    P: ProtocolMarker,
{
    /// Processes a request provided by the fidl processor.
    ///
    /// If the request is not processed, this method should return the original `request` that was
    /// passed in. If the request was successfully processed, `None` should be returned. If an error
    /// was encountered, an appropriate error should be returned.
    ///
    /// Parameters:
    /// `messenger`: a message hub connection that can be used to send and receive messages
    /// `service_messenger`: a MessageHub messenger to send messages.
    /// `request`: the request to process
    /// `exit_tx`: a channel to indicate that the connection is closed and for processing to stop
    fn process(
        &self,
        service_messenger: service::message::Messenger,
        request: Request<P>,
        exit_tx: ExitSender,
    ) -> RequestResultCreator<'static, P>;
}

/// `BaseFidlProcessor` delegates request processing for setting requests across a number of
/// processing units. There should be a single FidlProcessor per stream.
pub struct BaseFidlProcessor<P>
where
    P: ProtocolMarker,
{
    request_stream: RequestStream<P>,
    service_messenger: service::message::Messenger,
    processing_units: Vec<Box<dyn ProcessingUnit<P>>>,
}

impl<P> BaseFidlProcessor<P>
where
    P: ProtocolMarker,
{
    pub(crate) fn new(
        request_stream: RequestStream<P>,
        service_messenger: service::message::Messenger,
    ) -> Self {
        Self { request_stream, service_messenger, processing_units: Vec::new() }
    }

    #[cfg(test)]
    pub(crate) fn with_processing_units(
        request_stream: RequestStream<P>,
        service_messenger: service::message::Messenger,
        processing_units: Vec<Box<dyn ProcessingUnit<P>>>,
    ) -> Self {
        Self { request_stream, service_messenger, processing_units }
    }

    // Process the stream. Note that we pass in the processor here as it cannot
    // be used again afterwards.
    pub(crate) async fn process(mut self) {
        let nonce = fuchsia_trace::generate_nonce();
        trace!(nonce, "fidl processor");
        let (exit_tx, mut exit_rx) = futures::channel::mpsc::unbounded::<()>();
        loop {
            // Note that we create a fuse outside the select! to prevent it from
            // being called from outside the select! macro.
            let fused_stream = self.request_stream.try_next().fuse();
            futures::pin_mut!(fused_stream);

            futures::select! {
                request = fused_stream => {
                    trace!(nonce, "request");
                    if let Ok(Some(mut req)) = request {
                        for processing_unit in &self.processing_units {
                            // If the processing unit consumes the request (an empty
                            // result is returned) or an error occurs, exit processing this
                            // request. Otherwise, hand the request to the next processing
                            // unit
                            match processing_unit.process(
                                    self.service_messenger.clone(),
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
                _ = exit_rx.next() => {
                    return;
                }
            }
        }
    }
}

/// Wraps [`BaseFidlProcessor`] for use with FIDL APIs in the fuchsia.settings namespace that send
/// and receive messages through the service MessageHub.
///
/// [`BaseFidlProcessor`]: struct.BaseFidlProcessor.html
pub struct SettingsFidlProcessor<P>
where
    P: ProtocolMarker,
{
    base_processor: BaseFidlProcessor<P>,
}

impl<P> SettingsFidlProcessor<P>
where
    P: ProtocolMarker,
{
    pub(crate) async fn new(
        stream: RequestStream<P>,
        service_messenger: service::message::Messenger,
    ) -> Self {
        Self { base_processor: BaseFidlProcessor::new(stream, service_messenger) }
    }

    /// Registers a fidl processing unit for setting requests.
    pub(crate) async fn register<V, SV, K>(
        &mut self,
        setting_type: SettingType,
        callback: SettingsRequestCallback<P, V, SV, K>,
    ) where
        V: From<SettingInfo> + Send + Sync + 'static,
        SV: Sender<V> + Send + Sync + 'static,
        K: Eq + Hash + Clone + Send + Sync + 'static,
    {
        let processing_unit = Box::new(
            SettingProcessingUnit::<P, V, SV, K>::new(
                setting_type,
                self.base_processor.service_messenger.clone(),
                callback,
            )
            .await,
        );
        self.base_processor.processing_units.push(processing_unit);
    }

    pub(crate) async fn process(self) {
        self.base_processor.process().await
    }
}

/// Wraps [`BaseFidlProcessor`] for use with FIDL APIs in the fuchsia.settings.policy namespace that
/// send and receive messages through the policy message hub.
///
/// [`BaseFidlProcessor`]: struct.BaseFidlProcessor.html
pub struct PolicyFidlProcessor<P>
where
    P: ProtocolMarker,
{
    base_processor: BaseFidlProcessor<P>,
}

impl<P> PolicyFidlProcessor<P>
where
    P: ProtocolMarker,
{
    pub(crate) async fn new(
        stream: RequestStream<P>,
        service_messenger: service::message::Messenger,
    ) -> Self {
        Self { base_processor: BaseFidlProcessor::new(stream, service_messenger) }
    }

    /// Registers a fidl processing unit for policy requests.
    pub(crate) async fn register(&mut self, callback: PolicyRequestCallback<P>) {
        let processing_unit = Box::new(PolicyProcessingUnit::<P>::new(callback));
        self.base_processor.processing_units.push(processing_unit);
    }

    pub(crate) async fn process(self) {
        self.base_processor.process().await
    }
}
