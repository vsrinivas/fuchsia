// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl::endpoints::create_proxy, fidl_fidl_examples_echo as fidl_echo};

#[allow(deprecated)]
#[fuchsia_async::run_until_stalled(test)]
async fn test_server_sender() {
    let (proxy, server) = create_proxy::<fidl_echo::EchoMarker>().expect("failed creating proxy");
    let channel = server.into_channel();

    let resp_fut = proxy.echo_string(Some("hello"));

    let mut bytes = Vec::with_capacity(1024);
    let mut handles = Vec::with_capacity(1024);
    channel.read_split(&mut bytes, &mut handles).expect("failed reading request");

    let msg = fidl_echo::EchoRequestMessage::decode(&bytes[..], &mut handles[..])
        .expect("error deserializing request");
    let fidl_echo::EchoRequestMessage::EchoString { value, tx_id } = msg;
    assert_eq!(value, Some("hello".to_string()));

    let server = fidl_echo::EchoServerSender::new(&channel);
    server.send_echo_string_response(tx_id, Some("goodbye")).expect("error writing response");

    let res = resp_fut.await.expect("reading response failed");
    assert_eq!(res, Some("goodbye".to_string()));
}
