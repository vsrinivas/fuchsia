// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod ctap_agent;

use {
    anyhow::Error, fidl_fuchsia_identity_ctap as fidl_ctap, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs, futures::prelude::*,
};

pub use crate::ctap_agent::CtapAgent;

enum IncomingService {
    Authenticator(fidl_ctap::AuthenticatorRequestStream),
}

// [START main]
#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingService::Authenticator);
    fs.take_and_serve_directory_handle()?;

    let agent = CtapAgent::new();

    // TODO(fxb/108424): Add a device watcher to watch for usb ctap devices as they are connected,
    // so that we don't need to check for them and get their capabilities once a request is made.

    // Listen for incoming requests to connect to Authenticator, and call run_server
    // on each one.
    println!("Listening for incoming connections...");
    fs.for_each(|IncomingService::Authenticator(stream)| {
        agent.handle_request_for_stream(stream).unwrap_or_else(|e| println!("{:?}", e))
    })
    .await;

    Ok(())
}
// [END main]
