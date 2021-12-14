// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fasync::TimeoutExt,
    fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{StreamExt, TryStreamExt},
    rand::{thread_rng, Rng},
    std::time::Duration,
};

#[fasync::run_singlethreaded]
pub async fn main() {
    let mut rng = thread_rng();
    let timeout = rng.gen_range(0..10);
    let timeout = Duration::from_secs(timeout);
    run_echo_service().on_timeout(timeout, || ()).await;
}

async fn run_echo_service() {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |mut stream: fecho::EchoRequestStream| {
        fasync::Task::local(async move {
            while let Ok(Some(fecho::EchoRequest::EchoString { value, responder })) =
                stream.try_next().await
            {
                let _ = responder.send(value.as_ref().map(|s| &**s));
            }
        })
        .detach();
    });
    fs.take_and_serve_directory_handle().expect("failed to serve outgoing directory");
    fs.collect::<()>().await;
}
