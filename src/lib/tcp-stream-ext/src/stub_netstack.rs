// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implements just enough of fuchsia.posix.socket.Provider and fuchsia.posix.socket.StreamSocket
//! to bind a TcpListener and call getsockopt on it. Handles the TCP_KEEPINTVL and TCP_USER_TIMEOUT
//! options.

use {
    fidl::endpoints::{create_request_stream, RequestStream as _},
    fidl_fuchsia_io::{NodeInfo, StreamSocket},
    fidl_fuchsia_posix_socket::{
        BaseSocketMarker, ProviderRequest, ProviderRequestStream, StreamSocketRequest,
        StreamSocketRequestStream,
    },
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon as zx,
    futures::StreamExt as _,
};

const TCP_KEEPCNT_OPTION_VALUE: i32 = -11;
const TCP_KEEPINTVL_OPTION_VALUE: i32 = -12;
const TCP_USER_TIMEOUT_OPTION_VALUE: i32 = -13;

#[fasync::run_singlethreaded]
async fn main() {
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |stream| fasync::spawn(serve_provider(stream)));
    fs.take_and_serve_directory_handle().unwrap();
    let () = fs.collect().await;
}

async fn serve_provider(mut stream: ProviderRequestStream) {
    while let Some(event) = stream.next().await {
        match event.unwrap() {
            ProviderRequest::Socket2 { responder, .. } => {
                let (client, server) = create_request_stream::<BaseSocketMarker>().unwrap();
                fasync::spawn(serve_stream_socket(server.cast_stream()));
                responder.send(&mut Ok(client)).unwrap();
            }
            r => {
                panic!("Unhandled ProviderRequest: {:?}", r);
            }
        }
    }
}

async fn serve_stream_socket(mut stream: StreamSocketRequestStream) {
    while let Some(event) = stream.next().await {
        match event.unwrap() {
            StreamSocketRequest::Describe { responder } => {
                let (s0, _) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();
                responder.send(&mut NodeInfo::StreamSocket(StreamSocket { socket: s0 })).unwrap();
            }
            StreamSocketRequest::SetSockOpt { responder, .. } => {
                responder.send(&mut Ok(())).unwrap();
            }
            StreamSocketRequest::Bind { responder, .. } => {
                responder.send(&mut Ok(())).unwrap();
            }
            StreamSocketRequest::Listen { responder, .. } => {
                responder.send(&mut Ok(())).unwrap();
            }
            StreamSocketRequest::GetSockOpt { level, optname, responder, .. } => {
                assert_eq!(i32::from(level), libc::IPPROTO_TCP);
                let option_value = match i32::from(optname) {
                    libc::TCP_KEEPCNT => TCP_KEEPCNT_OPTION_VALUE,
                    libc::TCP_KEEPINTVL => TCP_KEEPINTVL_OPTION_VALUE,
                    libc::TCP_USER_TIMEOUT => TCP_USER_TIMEOUT_OPTION_VALUE,
                    o => panic!("Unhandled GetSockOpt option name: {}", o),
                };
                responder.send(&mut Ok(option_value.to_le_bytes().to_vec())).unwrap();
            }
            r => {
                panic!("Unhandled StreamSocketRequest: {:?}", r);
            }
        }
    }
}
