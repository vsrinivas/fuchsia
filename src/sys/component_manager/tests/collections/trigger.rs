// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This program hosts the `Trigger` service, which echoes the command-line arguments when invoked.
use {
    fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::{client, server::ServiceFs},
    futures::StreamExt,
    trigger_capability::TriggerCapability,
};

fn main() {
    let mut executor = fasync::Executor::new().expect("error creating executor");
    let mut fs = ServiceFs::new_local();
    let (capability, mut receiver) = TriggerCapability::new();
    fs.dir("svc").add_fidl_service(move |stream| {
        capability.clone().serve_async(stream);
    });
    fs.take_and_serve_directory_handle().expect("failed to serve outgoing directory");
    let fut = async move {
        fasync::spawn_local(async move {
            fs.collect::<()>().await;
        });
        let echo =
            client::connect_to_service::<fecho::EchoMarker>().expect("error connecting to echo");
        while let Some(trigger) = receiver.next().await {
            let args: Vec<_> = std::env::args().collect();
            let echo_str = args[1..].join(" ");
            let out = echo.echo_string(Some(&echo_str)).await.expect("echo_string failed");
            let out = out.expect("empty echo result");
            println!("{}", out);
            trigger.resume();
        }
    };
    executor.run_singlethreaded(fut);
}
