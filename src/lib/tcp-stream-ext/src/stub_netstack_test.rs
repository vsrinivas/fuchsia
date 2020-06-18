// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    fidl_fuchsia_io::{NodeInfo, StreamSocket},
    fidl_fuchsia_posix_socket::{StreamSocketMarker, StreamSocketRequest},
    fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, HandleBased as _},
    futures::{future, stream::StreamExt as _},
    matches::assert_matches,
    std::{net::TcpStream, thread},
    tcp_stream_ext::TcpStreamExt as _,
};

const TCP_KEEPCNT_OPTION_VALUE: i32 = -11;
const TCP_KEEPINTVL_OPTION_VALUE: i32 = -12;
const TCP_USER_TIMEOUT_OPTION_VALUE: i32 = -13;

fn with_tcp_stream(f: impl FnOnce(TcpStream) -> ()) {
    let (client, server) = fidl::endpoints::create_endpoints::<StreamSocketMarker>()
        .expect("create stream socket endpoints");

    // fdio::create_fd isn't async, so we need a dedicated thread for FIDL dispatch.
    let handle = thread::spawn(|| {
        fasync::Executor::new().expect("new executor").run_singlethreaded(
            server.into_stream().expect("endpoint into stream").for_each(move |request| {
                future::ready(match request.expect("stream socket request stream") {
                    StreamSocketRequest::Describe { responder } => {
                        let (s0, _s1) =
                            zx::Socket::create(zx::SocketOpts::STREAM).expect("create zx socket");
                        let () = responder
                            .send(&mut NodeInfo::StreamSocket(StreamSocket { socket: s0 }))
                            .expect("send Describe response");
                    }
                    StreamSocketRequest::GetSockOpt { level, optname, responder } => {
                        let () = assert_eq!(i32::from(level), libc::IPPROTO_TCP);
                        let option_value = match i32::from(optname) {
                            libc::TCP_KEEPCNT => TCP_KEEPCNT_OPTION_VALUE,
                            libc::TCP_KEEPINTVL => TCP_KEEPINTVL_OPTION_VALUE,
                            libc::TCP_USER_TIMEOUT => TCP_USER_TIMEOUT_OPTION_VALUE,
                            optname => panic!("unhandled GetSockOpt option name: {}", optname),
                        };
                        let () = responder
                            .send(&mut Ok(option_value.to_le_bytes().to_vec()))
                            .expect("send GetSockOpt response");
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
fn keepalive_interval_errors_on_negative_duration() {
    with_tcp_stream(|stream| {
        assert_matches!(
            stream.keepalive_interval(),
            Err(tcp_stream_ext::Error::NegativeDuration(TCP_KEEPINTVL_OPTION_VALUE))
        )
    })
}

#[test]
fn user_timeout_errors_on_negative_duration() {
    with_tcp_stream(|stream| {
        assert_matches!(
            stream.user_timeout(),
            Err(tcp_stream_ext::Error::NegativeDuration(TCP_USER_TIMEOUT_OPTION_VALUE))
        )
    })
}

#[test]
fn keepalive_count() {
    with_tcp_stream(|stream| {
        assert_matches!(stream.keepalive_count(), Ok(TCP_KEEPCNT_OPTION_VALUE))
    })
}
