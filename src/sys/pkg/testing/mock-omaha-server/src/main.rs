// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]

use {
    anyhow::Context as _,
    argh::FromArgs,
    async_net::{TcpListener, TcpStream},
    fuchsia_async as fasync,
    futures::prelude::*,
    hyper::{
        server::{accept::from_stream, Server},
        service::{make_service_fn, service_fn},
    },
    mock_omaha_server::{
        handle_request, OmahaServerBuilder, PrivateKeyAndId, PrivateKeys, ResponseAndMetadata,
    },
    parking_lot::Mutex,
    std::{
        collections::HashMap,
        convert::Infallible,
        convert::TryInto,
        io,
        net::{Ipv6Addr, SocketAddr},
        pin::Pin,
        sync::Arc,
        task::{Context, Poll},
    },
};

#[derive(FromArgs)]
/// Arguments for mock-omaha-server.
struct Args {
    /// A hashmap from appid to response metadata struct.
    /// Example JSON argument:
    ///     {
    ///         "appid_01": {
    ///             "response": "NoUpdate",
    ///             "merkle": "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
    ///             "check_assertion": "UpdatesEnabled",
    ///             "version": "0.1.2.3",
    ///         },
    ///         ...
    ///     }
    #[argh(
        option,
        description = "responses and metadata keyed by appid",
        from_str_fn(parse_responses_by_appid),
        default = "HashMap::new()"
    )]
    responses_by_appid: HashMap<String, ResponseAndMetadata>,

    #[argh(
        option,
        description = "private key ID",
        default = "DEFAULT_PRIVATE_KEY_ID.try_into().expect(\"key parse\")"
    )]
    key_id: u64,

    #[argh(
        option,
        description = "path to private key",
        default = "\"testing_keys/text_private_key.pem\".to_string()"
    )]
    key_path: String,

    #[argh(option, description = "which port to serve on", default = "0")]
    port: u16,

    #[argh(
        option,
        description = "which IP address to listen on. One of '::', '::1', or anything Ipv6Addr::from_str() can interpret.",
        default = "Ipv6Addr::UNSPECIFIED"
    )]
    listen_on: Ipv6Addr,

    #[argh(switch, description = "if 'true', will only accept requests with CUP enabled.")]
    require_cup: bool,
}

fn parse_responses_by_appid(value: &str) -> Result<HashMap<String, ResponseAndMetadata>, String> {
    serde_json::from_str(value).map_err(|e| format!("Parsing failed: {:?}", e))
}

/// Adapt [async_net::TcpStream] to work with hyper.
#[derive(Debug)]
pub enum ConnectionStream {
    Tcp(TcpStream),
    Socket(fasync::Socket),
}

impl tokio::io::AsyncRead for ConnectionStream {
    fn poll_read(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &mut tokio::io::ReadBuf<'_>,
    ) -> Poll<Result<(), std::io::Error>> {
        match &mut *self {
            ConnectionStream::Tcp(t) => Pin::new(t).poll_read(cx, buf.initialize_unfilled()),
            ConnectionStream::Socket(t) => Pin::new(t).poll_read(cx, buf.initialize_unfilled()),
        }
        .map_ok(|sz| {
            buf.advance(sz);
            ()
        })
    }
}

impl tokio::io::AsyncWrite for ConnectionStream {
    fn poll_write(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &[u8],
    ) -> Poll<io::Result<usize>> {
        match &mut *self {
            ConnectionStream::Tcp(t) => Pin::new(t).poll_write(cx, buf),
            ConnectionStream::Socket(t) => Pin::new(t).poll_write(cx, buf),
        }
    }

    fn poll_flush(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        match &mut *self {
            ConnectionStream::Tcp(t) => Pin::new(t).poll_flush(cx),
            ConnectionStream::Socket(t) => Pin::new(t).poll_flush(cx),
        }
    }

    fn poll_shutdown(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        match &mut *self {
            ConnectionStream::Tcp(t) => Pin::new(t).poll_close(cx),
            ConnectionStream::Socket(t) => Pin::new(t).poll_close(cx),
        }
    }
}

pub const DEFAULT_PRIVATE_KEY_ID: i32 = 42;

#[fasync::run(10)]
async fn main() -> Result<(), anyhow::Error> {
    let args: Args = argh::from_env();

    let server = OmahaServerBuilder::default()
        .responses_by_appid(args.responses_by_appid)
        .private_keys(PrivateKeys {
            latest: PrivateKeyAndId {
                id: args.key_id.try_into().expect("key id parse"),
                key: std::fs::read_to_string(args.key_path)
                    .expect("read from key_path failed")
                    .parse()
                    .expect("failed to parse key"),
            },
            historical: vec![],
        })
        .require_cup(args.require_cup)
        .build()
        .expect("omaha server build");

    let arc_server = Arc::new(Mutex::new(server));

    let addr = SocketAddr::new(args.listen_on.into(), args.port);
    let listener = TcpListener::bind(&addr).await.context("binding to addr")?;
    println!("listening on {}", listener.local_addr()?);
    let connections = listener.incoming().map_ok(ConnectionStream::Tcp);

    let make_svc = make_service_fn(move |_socket| {
        let arc_server = Arc::clone(&arc_server);
        async move {
            Ok::<_, Infallible>(service_fn(move |req| {
                println!("received req: {req:?}");
                let arc_server = Arc::clone(&arc_server);
                async move { handle_request(req, &*arc_server).await }
            }))
        }
    });

    Server::builder(from_stream(connections))
        .executor(fuchsia_hyper::Executor)
        .serve(make_svc)
        .await
        .context("error serving omaha server")
}
