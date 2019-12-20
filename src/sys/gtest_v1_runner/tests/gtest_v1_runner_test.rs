// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::ResultExt;
use fidl_fidl_examples_echo::{EchoRequest, EchoRequestStream};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::channel::mpsc;
use futures::prelude::*;

fn echo_server((send, stream): (mpsc::Sender<()>, EchoRequestStream)) -> impl Future<Output = ()> {
    stream
        .err_into::<failure::Error>()
        .try_for_each(move |EchoRequest::EchoString { value, responder }| {
            let mut send = send.clone();
            async move {
                responder.send(value.as_ref().map(|s| &**s)).context("sending response")?;
                send.send(()).await.expect("failed to send signal");
                Ok(())
            }
        })
        .unwrap_or_else(|e: failure::Error| panic!("error running echo server: {:?}", e))
}

#[fuchsia_async::run_singlethreaded(test)]
async fn can_launch_test_with_gtest_v1_runner() {
    let mut fs = ServiceFs::new();

    let (send, mut recv) = mpsc::channel(0);

    fs.add_fidl_service(move |stream| (send.clone(), stream));

    let (_new_env_controller, _child_app) = fs
        .launch_component_in_nested_environment(
            "fuchsia-pkg://fuchsia.com/gtest_v1_runner_test#meta/echo_test.cmx".to_string(),
            None,
            "env",
        )
        .expect("Cannot launch test client.");
    fasync::spawn(fs.for_each_concurrent(None, echo_server));

    recv.next().await.expect("client failed to connect to echo service");
}
