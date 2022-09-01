// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_identity_ctap as fidl_ctap,
    futures::prelude::*,
};

pub struct CtapAgent {
    // TODO(fxb/108425): add a device field to keep track of the connected device.
}

impl CtapAgent {
    pub fn new() -> Self {
        CtapAgent {}
    }

    // An implementation of the Authenticator stream, which handles a stream of
    // AuthenticatorRequests
    // TODO(fxb/108425): look for a device to talk to and check it's capabilities before executing
    // the request.
    pub async fn handle_request_for_stream(
        &self,
        stream: fidl_ctap::AuthenticatorRequestStream,
    ) -> Result<(), Error> {
        stream
            .map(|result| result.context("failed to handle request"))
            .try_for_each(|request| async move {
                match request {
                    fidl_ctap::AuthenticatorRequest::MakeCredential { responder, .. } => {
                        responder.send(&mut Result::Err(fidl_ctap::CtapError::Unimplemented))?;
                    }

                    fidl_ctap::AuthenticatorRequest::GetAssertion { responder, .. } => {
                        responder.send(&mut Result::Err(fidl_ctap::CtapError::Unimplemented))?;
                    }

                    fidl_ctap::AuthenticatorRequest::EnumerateKeys { responder } => {
                        responder.send(&mut Result::Err(fidl_ctap::CtapError::Unimplemented))?;
                    }

                    fidl_ctap::AuthenticatorRequest::IdentifyKey { responder, .. } => {
                        responder.send(&mut Result::Err(fidl_ctap::CtapError::Unimplemented))?;
                    }
                }
                Ok(())
            })
            .await
    }
}
