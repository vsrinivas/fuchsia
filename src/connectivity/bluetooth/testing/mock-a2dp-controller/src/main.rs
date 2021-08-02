// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl::endpoints::RequestStream;
use fidl_fuchsia_bluetooth_internal_a2dp::{ControllerRequest, ControllerRequestStream};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::StreamExt;

async fn handle_controller_requests(mut stream: ControllerRequestStream) {
    while let Some(request) = stream.next().await {
        if let Ok(ControllerRequest::Suspend { token, responder, .. }) = request {
            if let Ok(token_stream) = token.into_stream() {
                let _ = token_stream.control_handle().send_on_suspended();
                // Keeps the suspend active for as long as the client wants.
                fasync::Task::spawn(async move {
                    token_stream.map(|_| ()).collect::<()>().await;
                    let _ = responder.send();
                })
                .detach();
            } else {
                // There is some error with the suspend request token - the mock doesn't really care
                // and we can just terminate the request.
                let _ = responder.send();
            }
        }
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Outgoing `svc` directory provides the `internal.a2dp.Controller` capability.
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(|stream: ControllerRequestStream| {
        fasync::Task::local(handle_controller_requests(stream)).detach();
    });
    fs.take_and_serve_directory_handle().expect("Unable to serve ServiceFs requests");
    fs.collect::<()>().await;
    Ok(())
}
