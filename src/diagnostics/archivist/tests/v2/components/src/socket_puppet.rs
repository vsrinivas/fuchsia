// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints::create_request_stream;
use fidl_fuchsia_archivist_tests::{
    SocketPuppetControllerMarker, SocketPuppetMarker, SocketPuppetRequest,
};
use fidl_fuchsia_logger::LogSinkMarker;
use fuchsia_component::client::connect_to_protocol;
use fuchsia_zircon as zx;
use futures::StreamExt;

#[fuchsia_async::run_singlethreaded]
async fn main() {
    let controller = connect_to_protocol::<SocketPuppetControllerMarker>().unwrap();
    let (client, mut requests) = create_request_stream::<SocketPuppetMarker>().unwrap();
    controller.control_puppet(client).unwrap();

    let (send, recv) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
    let mut recv = Some(recv); // so we can send it to LogSink
    while let Some(Ok(next)) = requests.next().await {
        match next {
            SocketPuppetRequest::ConnectToLogSink { responder } => {
                connect_to_protocol::<LogSinkMarker>()
                    .unwrap()
                    .connect(recv.take().unwrap())
                    .unwrap();
                responder.send().unwrap();
            }
            SocketPuppetRequest::EmitPacket { packet, responder } => {
                send.write(&packet).unwrap();
                responder.send().unwrap();
            }
        }
    }
}
