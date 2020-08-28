// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tests that connect to the service provided by the node.

use super::{endpoint, host};

use crate::file::test_utils::run_server_client;

use {
    fidl::endpoints::{Proxy, RequestStream},
    fidl_fidl_examples_echo::{EchoProxy, EchoRequest, EchoRequestStream},
    fidl_fuchsia_io::{OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    futures::{
        channel::{mpsc, oneshot},
        stream::StreamExt,
    },
    parking_lot::Mutex,
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

const READ_WRITE: u32 = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE;

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

            assert_eq!(response, Some("test".to_string()));
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

            assert_eq!(response, Some("test".to_string()));
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
                Some(on_message_tx.lock().take().unwrap()),
                Some(done_tx.lock().take().unwrap()),
            )
        }),
        |node_proxy| {
            async move {
                let proxy = EchoProxy::from_channel(node_proxy.into_channel().unwrap());

                let response = proxy.echo_string(Some("message 1")).await.unwrap();

                // `next()` wraps in `Option` and our value is `Option<String>`, hence double
                // `Option`.
                assert_eq!(on_message_rx.next().await, Some(Some("message 1".to_string())));
                assert_eq!(response, Some("message 1".to_string()));

                let response = proxy.echo_string(Some("message 2")).await.unwrap();

                // `next()` wraps in `Option` and our value is `Option<String>`, hence double
                // `Option`.
                assert_eq!(on_message_rx.next().await, Some(Some("message 2".to_string())));
                assert_eq!(response, Some("message 2".to_string()));

                drop(proxy);

                assert_eq!(done_rx.await, Ok(()));
            }
        },
    );
}
