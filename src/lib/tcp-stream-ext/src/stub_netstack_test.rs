// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, HandleBased as _},
    futures::stream::StreamExt as _,
    tcp_stream_ext::TcpStreamExt as _,
};

const TCP_USER_TIMEOUT_OPTION_VALUE: i32 = -13;

fn with_tcp_stream(f: impl FnOnce(std::net::TcpStream) -> ()) {
    let (client, server) =
        fidl::endpoints::create_endpoints::<fidl_fuchsia_posix_socket::StreamSocketMarker>()
            .expect("create stream socket endpoints");

    // fdio::create_fd isn't async, so we need a dedicated thread for FIDL dispatch.
    let handle = std::thread::spawn(|| {
        fasync::LocalExecutor::new().expect("new executor").run_singlethreaded(
            server.into_stream().expect("endpoint into stream").for_each(move |request| {
                futures::future::ready(match request.expect("stream socket request stream") {
                    fidl_fuchsia_posix_socket::StreamSocketRequest::Close { responder } => {
                        let () = responder.control_handle().shutdown();
                        let () =
                            responder.send(zx::Status::OK.into_raw()).expect("send Close response");
                    }
                    fidl_fuchsia_posix_socket::StreamSocketRequest::Close2 { responder } => {
                        let () = responder.control_handle().shutdown();
                        let () = responder.send(&mut Ok(())).expect("send Close response");
                    }
                    fidl_fuchsia_posix_socket::StreamSocketRequest::Describe { responder } => {
                        let (s0, _s1) =
                            zx::Socket::create(zx::SocketOpts::STREAM).expect("create zx socket");
                        let () = responder
                            .send(&mut fidl_fuchsia_io::NodeInfo::StreamSocket(
                                fidl_fuchsia_io::StreamSocket { socket: s0 },
                            ))
                            .expect("send Describe response");
                    }
                    fidl_fuchsia_posix_socket::StreamSocketRequest::GetTcpUserTimeout {
                        responder,
                    } => {
                        let () = responder
                            .send(&mut Ok(TCP_USER_TIMEOUT_OPTION_VALUE as u32))
                            .expect("send TcpUserTimeout response");
                    }
                    request => panic!("unhandled StreamSocketRequest: {:?}", request),
                })
            }),
        )
    });
    let () = f(fdio::create_fd(client.into_handle()).expect("endpoint into handle"));
    handle.join().expect("thread join")
}

#[test]
fn user_timeout_errors_on_negative_duration() {
    with_tcp_stream(|stream| {
        matches::assert_matches!(
            stream.user_timeout(),
            Err(tcp_stream_ext::Error::NegativeDuration(TCP_USER_TIMEOUT_OPTION_VALUE))
        )
    })
}
