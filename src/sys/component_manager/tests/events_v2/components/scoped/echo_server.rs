// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fidl_examples_routing_echo as fecho, fuchsia_component::server::ServiceFs,
    futures::StreamExt,
};

enum ExposedServices {
    Echo(fecho::EchoRequestStream),
}

#[fuchsia::main]
async fn main() {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(ExposedServices::Echo);
    fs.take_and_serve_directory_handle().expect("failed to serve outgoing directory");
    let ExposedServices::Echo(mut stream) = fs.next().await.unwrap();
    let fecho::EchoRequest::EchoString { value, responder } = stream.next().await.unwrap().unwrap();
    responder.send(value.as_ref().map(|s| &**s)).expect("failed to send echo response");
}
