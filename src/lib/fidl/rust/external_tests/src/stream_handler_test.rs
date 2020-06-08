// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints,
    fidl_fidl_examples_echo::{EchoProxy, EchoRequest},
};

#[fuchsia_async::run_singlethreaded(test)]
async fn test_spawn_local_stream_handler() {
    let f = |req| {
        let EchoRequest::EchoString { value, responder } = req;
        async move {
            responder.send(Some(&value.unwrap())).expect("responder failed");
        }
    };
    let proxy: EchoProxy =
        endpoints::spawn_local_stream_handler(f).expect("could not spawn handler");
    let res = proxy.echo_string(Some("hello world")).await.expect("echo failed");
    assert_eq!(res, Some("hello world".to_string()));
    let res = proxy.echo_string(Some("goodbye world")).await.expect("echo failed");
    assert_eq!(res, Some("goodbye world".to_string()));
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_spawn_stream_handler() {
    let f = |req| {
        let EchoRequest::EchoString { value, responder } = req;
        async move {
            responder.send(Some(&value.unwrap())).expect("responder failed");
        }
    };
    let proxy: EchoProxy = endpoints::spawn_stream_handler(f).expect("could not spawn handler");
    let res = proxy.echo_string(Some("hello world")).await.expect("echo failed");
    assert_eq!(res, Some("hello world".to_string()));
    let res = proxy.echo_string(Some("goodbye world")).await.expect("echo failed");
    assert_eq!(res, Some("goodbye world".to_string()));
}
