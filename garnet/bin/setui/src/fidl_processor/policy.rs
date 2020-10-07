// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints::{Request, ServiceMarker};

use crate::fidl_processor::processor::{ProcessingUnit, RequestResultCreator};
use crate::internal::policy::{Address, Payload};
use crate::message::base::{self, Audience};
use crate::switchboard::base::SettingType;
use crate::ExitSender;
use fuchsia_syslog::fx_log_err;

/// `RequestCallback` closures are handed a request and the surrounding
/// context. They are expected to hand back a future that returns when the
/// request is processed. The returned value is a result with an optional
/// request, containing None if not processed and the original request
/// otherwise.
pub type RequestCallback<S, P, A> =
    Box<dyn Fn(RequestContext<P, A>, Request<S>) -> RequestResultCreator<'static, S>>;

/// `RequestContext` is passed to each request callback to provide resources for policy requests.
#[derive(Clone)]
pub struct RequestContext<P, A>
where
    P: base::Payload + 'static,
    A: base::Address + 'static,
{
    messenger: crate::message::messenger::MessengerClient<P, A>,
}

impl RequestContext<Payload, Address> {
    // TODO(fxb/59705): remove annotation once used.
    #[allow(dead_code)]
    pub async fn request(
        &self,
        setting_type: SettingType,
        request: crate::policy::base::Request,
    ) -> crate::policy::base::response::Response {
        let mut receptor = self
            .messenger
            .message(Payload::Request(request), Audience::Address(Address::Policy(setting_type)))
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
pub struct PolicyProcessingUnit<S, P = Payload, A = Address>
where
    S: ServiceMarker,
    // Messenger type for the message hub this processing unit talks to.
    P: base::Payload + 'static,
    A: base::Address + 'static,
{
    callback: RequestCallback<S, P, A>,
}

impl<S> PolicyProcessingUnit<S, Payload, Address>
where
    S: ServiceMarker,
{
    pub(crate) fn new(callback: RequestCallback<S, Payload, Address>) -> Self {
        Self { callback }
    }
}

impl<S, P, A> ProcessingUnit<S, P, A> for PolicyProcessingUnit<S, P, A>
where
    S: ServiceMarker,
    P: base::Payload + 'static,
    A: base::Address + 'static,
{
    fn process(
        &self,
        messenger: crate::message::messenger::MessengerClient<P, A>,
        request: Request<S>,
        // Policy requests don't use hanging gets, so the exit sender is unused.
        _exit_tx: ExitSender,
    ) -> RequestResultCreator<'static, S> {
        let context = RequestContext { messenger };

        return (self.callback)(context, request);
    }
}
