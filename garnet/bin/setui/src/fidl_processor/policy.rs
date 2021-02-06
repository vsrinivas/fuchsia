// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints::{Request, ServiceMarker};

use crate::fidl_processor::processor::{ProcessingUnit, RequestResultCreator};
use crate::message::base::{self, Audience};
use crate::policy::base::{Address, Payload, PolicyType, Role};
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
pub type RequestCallback<S, P, A, R> =
    Box<dyn Fn(RequestContext<P, A, R>, Request<S>) -> RequestResultCreator<'static, S>>;

/// `RequestContext` is passed to each request callback to provide resources for policy requests.
#[derive(Clone)]
pub struct RequestContext<P, A, R>
where
    P: base::Payload + 'static,
    A: base::Address + 'static,
    R: base::Role + 'static,
{
    messenger: crate::message::messenger::MessengerClient<P, A, R>,
}

impl RequestContext<Payload, Address, Role> {
    pub async fn request(
        &self,
        policy_type: PolicyType,
        request: crate::policy::base::Request,
    ) -> crate::policy::base::response::Response {
        let mut receptor = self
            .messenger
            .message(Payload::Request(request), Audience::Address(Address::Policy(policy_type)))
            .send();

        let response_payload = receptor.next_payload().await;
        if let Ok((Payload::Response(result), _)) = response_payload {
            return result;
        } else if let Err(err) = response_payload {
            fx_log_err!("Failed to get policy response: {}", err);
        }

        Err(crate::policy::base::response::Error::CommunicationError)
    }
}

/// `PolicyProcessingUnit` is a concrete implementation of the ProcessingUnit
/// trait, allowing a [`RequestCallback`] to participate in stream request
/// processing. The SettingProcessingUnit maintains a hanging get handler keyed
/// to the constructed type.
///
/// [`RequestCallback`]: type.RequestCallback.html
pub struct PolicyProcessingUnit<S, P = Payload, A = Address, R = Role>
where
    S: ServiceMarker,
    // Messenger type for the message hub this processing unit talks to.
    P: base::Payload + 'static,
    A: base::Address + 'static,
    R: base::Role + 'static,
{
    callback: RequestCallback<S, P, A, R>,
}

impl<S> PolicyProcessingUnit<S, Payload, Address>
where
    S: ServiceMarker,
{
    pub(crate) fn new(callback: RequestCallback<S, Payload, Address, Role>) -> Self {
        Self { callback }
    }
}

impl<S, P, A, R> ProcessingUnit<S, P, A, R> for PolicyProcessingUnit<S, P, A, R>
where
    S: ServiceMarker,
    P: base::Payload + 'static,
    A: base::Address + 'static,
    R: base::Role + 'static,
{
    fn process(
        &self,
        messenger: crate::message::messenger::MessengerClient<P, A, R>,
        request: Request<S>,
        // Policy requests don't use hanging gets, so the exit sender is unused.
        _exit_tx: ExitSender,
    ) -> RequestResultCreator<'static, S> {
        let context = RequestContext { messenger };

        return (self.callback)(context, request);
    }
}
