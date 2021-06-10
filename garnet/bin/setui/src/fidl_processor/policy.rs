// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints::{Request, ServiceMarker};

use crate::fidl_processor::processor::{ProcessingUnit, RequestResultCreator};
use crate::message::base::Audience;
use crate::policy::{Payload, PolicyType};
use crate::service;
use crate::ExitSender;
use fuchsia_syslog::fx_log_err;

/// Convenience macro to make a policy request and send the result to a responder.
#[macro_export]
macro_rules! policy_request_respond {
    ($context:ident, $responder:ident, $policy_type:expr, $request:expr) => {
        match $context.request($policy_type, $request).await {
            Ok(response) => $responder.send_response(response.into()),
            Err(err) => $responder.on_error(&anyhow::Error::new(err)),
        };
    };
}

/// `RequestCallback` closures are handed a request and the surrounding
/// context. They are expected to hand back a future that returns when the
/// request is processed. The returned value is a result with an optional
/// request, containing None if not processed and the original request
/// otherwise.
pub type RequestCallback<S> =
    Box<dyn Fn(RequestContext, Request<S>) -> RequestResultCreator<'static, S>>;

/// `RequestContext` is passed to each request callback to provide resources for policy requests.
#[derive(Clone)]
pub struct RequestContext {
    service_messenger: service::message::Messenger,
}

impl RequestContext {
    pub(crate) async fn request(
        &self,
        policy_type: PolicyType,
        request: crate::policy::Request,
    ) -> crate::policy::response::Response {
        let mut receptor = self
            .service_messenger
            .message(
                service::Payload::Policy(Payload::Request(request)),
                Audience::Address(service::Address::PolicyHandler(policy_type)),
            )
            .send();

        let response_payload = receptor.next_of::<Payload>().await;
        if let Ok((Payload::Response(result), _)) = response_payload {
            return result;
        } else if let Err(err) = response_payload {
            fx_log_err!("Failed to get policy response: {}", err);
        }

        Err(crate::policy::response::Error::CommunicationError)
    }
}

/// `PolicyProcessingUnit` is a concrete implementation of the ProcessingUnit
/// trait, allowing a [`RequestCallback`] to participate in stream request
/// processing. The SettingProcessingUnit maintains a hanging get handler keyed
/// to the constructed type.
///
/// [`RequestCallback`]: type.RequestCallback.html
pub struct PolicyProcessingUnit<S>
where
    S: ServiceMarker,
{
    callback: RequestCallback<S>,
}

impl<S> PolicyProcessingUnit<S>
where
    S: ServiceMarker,
{
    pub(crate) fn new(callback: RequestCallback<S>) -> Self {
        Self { callback }
    }
}

impl<S> ProcessingUnit<S> for PolicyProcessingUnit<S>
where
    S: ServiceMarker,
{
    fn process(
        &self,
        service_messenger: service::message::Messenger,
        request: Request<S>,
        // Policy requests don't use hanging gets, so the exit sender is unused.
        _exit_tx: ExitSender,
    ) -> RequestResultCreator<'static, S> {
        let context = RequestContext { service_messenger };
        (self.callback)(context, request)
    }
}
