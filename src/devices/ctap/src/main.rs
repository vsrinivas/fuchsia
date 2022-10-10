// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod ctap_agent;
mod ctap_device;
mod ctap_hid;

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_identity_ctap as fidl_ctap, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::component,
    futures::prelude::*,
};

pub use crate::ctap_agent::CtapAgent;
pub use crate::ctap_device::CtapDevice;

// An implementation of the Authenticator stream, which handles a stream of
// AuthenticatorRequests.
// TODO(fxb/108425): look for a device to talk to and check it's capabilities before executing
// the request.
pub async fn handle_request_for_stream(
    _agent: &CtapAgent,
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

enum IncomingService {
    Authenticator(fidl_ctap::AuthenticatorRequestStream),
}

#[fasync::run_singlethreaded]
#[fuchsia::main(logging = true)]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingService::Authenticator);
    fs.take_and_serve_directory_handle()?;

    inspect_runtime::serve(component::inspector(), &mut fs)?;

    // Add a device watcher to watch for usb ctap devices as they are connected, so that we don't
    // need to check for their capabilities once a request is made.
    let agent = CtapAgent::new(component::inspector().root().create_child("ctap_agent_service"));

    // Listen for incoming requests to connect to Authenticator, and call run_server
    // on each one.
    println!("Listening for incoming connections...");
    fs.for_each(|IncomingService::Authenticator(stream)| {
        handle_request_for_stream(&agent, stream).unwrap_or_else(|e| println!("{:?}", e))
    })
    .await;

    Ok(())
}
