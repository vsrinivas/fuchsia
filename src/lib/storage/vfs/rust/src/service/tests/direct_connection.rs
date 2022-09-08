// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tests that connect to the service provided by the node.

use super::{endpoint, host};

use crate::file::test_utils::run_server_client;

use {
    assert_matches::assert_matches,
    fidl::{
        endpoints::{Proxy, RequestStream},
        Error,
    },
    fidl_fuchsia_io as fio,
    fidl_test_placeholders::{EchoProxy, EchoRequest, EchoRequestStream},
    fuchsia_zircon::Status,
    futures::{
        channel::{mpsc, oneshot},
        stream::StreamExt,
    },
    std::sync::Mutex,
};

async fn echo_server(
    mut requests: EchoRequestStream,
    on_message: Option<mpsc::UnboundedSender<Option<String>>>,
    done: Option<oneshot::Sender<()>>,
) {
    loop {
        match requests.next().await {
            None => break,
            Some(Err(err)) => {
                panic!("echo_server(): failed to receive a message: {}", err);
            }
            Some(Ok(EchoRequest::EchoString { value, responder })) => {
                responder
                    .send(value.as_ref().map(|s| &**s))
                    .expect("echo_server(): responder.send() failed");
                if let Some(on_message) = &on_message {
                    on_message.unbounded_send(value).expect("on_message.unbounded_send() failed");
                }
            }
        }
    }
    if let Some(done) = done {
        done.send(()).expect("done.send() failed");
    }
}

const READ_WRITE: fio::OpenFlags = fio::OpenFlags::empty()
    .union(fio::OpenFlags::RIGHT_READABLE)
    .union(fio::OpenFlags::RIGHT_WRITABLE);

#[test]
fn construction() {
    run_server_client(READ_WRITE, endpoint(|_scope, _channel| ()), |_proxy| {
        async move {
            // NOOP.  Can not even call `Close` as it is part of the `Node` interface and we
            // did not connect to a service that speaks `Node`.
        }
    });
}

#[test]
fn simple_endpoint() {
    run_server_client(
        READ_WRITE,
        endpoint(|scope, channel| {
            scope.spawn(async move {
                echo_server(RequestStream::from_channel(channel), None, None).await;
            });
        }),
        |node_proxy| async move {
            let proxy = EchoProxy::from_channel(node_proxy.into_channel().unwrap());

            let response = proxy.echo_string(Some("test")).await.unwrap();

            assert_eq!(response.as_deref(), Some("test"));
        },
    );
}

#[test]
fn simple_host() {
    run_server_client(
        READ_WRITE,
        host(|requests| echo_server(requests, None, None)),
        |node_proxy| async move {
            let proxy = EchoProxy::from_channel(node_proxy.into_channel().unwrap());

            let response = proxy.echo_string(Some("test")).await.unwrap();

            assert_eq!(response.as_deref(), Some("test"));
        },
    );
}

#[test]
fn server_state_checking() {
    let (done_tx, done_rx) = oneshot::channel();
    let (on_message_tx, mut on_message_rx) = mpsc::unbounded();

    let done_tx = Mutex::new(Some(done_tx));
    let on_message_tx = Mutex::new(Some(on_message_tx));

    run_server_client(
        READ_WRITE,
        host(move |requests| {
            echo_server(
                requests,
                Some(on_message_tx.lock().unwrap().take().unwrap()),
                Some(done_tx.lock().unwrap().take().unwrap()),
            )
        }),
        |node_proxy| {
            async move {
                let proxy = EchoProxy::from_channel(node_proxy.into_channel().unwrap());

                let response = proxy.echo_string(Some("message 1")).await.unwrap();

                // `next()` wraps in `Option` and our value is `Option<String>`, hence double
                // `Option`.
                assert_eq!(on_message_rx.next().await, Some(Some("message 1".to_string())));
                assert_eq!(response.as_deref(), Some("message 1"));

                let response = proxy.echo_string(Some("message 2")).await.unwrap();

                // `next()` wraps in `Option` and our value is `Option<String>`, hence double
                // `Option`.
                assert_eq!(on_message_rx.next().await, Some(Some("message 2".to_string())));
                assert_eq!(response.as_deref(), Some("message 2"));

                drop(proxy);

                assert_eq!(done_rx.await, Ok(()));
            }
        },
    );
}

#[test]
fn test_describe() {
    run_server_client(
        READ_WRITE | fio::OpenFlags::DESCRIBE,
        host(|requests| echo_server(requests, None, None)),
        |node_proxy| async move {
            let (status, node_info) = node_proxy
                .take_event_stream()
                .next()
                .await
                .expect("Channel closed")
                .expect("Expected event")
                .into_on_open_()
                .expect("Expected OnOpen");
            assert_eq!(Status::from_raw(status), Status::OK);
            assert_matches!(
                node_info.as_deref(),
                Some(fio::NodeInfoDeprecated::Service(fio::Service))
            );

            let proxy = EchoProxy::from_channel(node_proxy.into_channel().unwrap());

            let response = proxy.echo_string(Some("test")).await.unwrap();

            assert_eq!(response.as_deref(), Some("test"));
        },
    );
}

#[test]
fn test_describe_error() {
    run_server_client(
        READ_WRITE | fio::OpenFlags::DIRECTORY | fio::OpenFlags::DESCRIBE,
        host(|requests| echo_server(requests, None, None)),
        |node_proxy| async move {
            let mut event_stream = node_proxy.take_event_stream();

            let (status, node_info) = event_stream
                .next()
                .await
                .expect("Channel closed")
                .expect("Expected event")
                .into_on_open_()
                .expect("Expected OnOpen");
            assert_eq!(Status::from_raw(status), Status::NOT_DIR);
            assert_eq!(node_info, None);

            // And we should also get an epitaph.
            assert_matches!(
                event_stream.next().await,
                Some(Err(Error::ClientChannelClosed { status: Status::NOT_DIR, .. }))
            );

            assert_matches!(event_stream.next().await, None);
        },
    );
}

#[test]
fn test_epitaph() {
    run_server_client(
        READ_WRITE | fio::OpenFlags::DIRECTORY,
        host(|requests| echo_server(requests, None, None)),
        |node_proxy| async move {
            let mut event_stream = node_proxy.take_event_stream();
            assert_matches!(
                event_stream.next().await,
                Some(Err(Error::ClientChannelClosed { status: Status::NOT_DIR, .. }))
            );

            assert_matches!(event_stream.next().await, None);
        },
    );
}
