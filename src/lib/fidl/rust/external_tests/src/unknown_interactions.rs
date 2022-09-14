// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This file tests how FIDL handles unknown interactions.

use assert_matches::assert_matches;
use {
    fidl::{
        endpoints::{Proxy, RequestStream},
        AsyncChannel, Channel, Error,
    },
    fidl_test_unknown_interactions::{
        UnknownInteractionsAjarProtocolEvent, UnknownInteractionsAjarProtocolProxy,
        UnknownInteractionsAjarProtocolRequest, UnknownInteractionsAjarProtocolRequestStream,
        UnknownInteractionsAjarProtocolSynchronousProxy, UnknownInteractionsClosedProtocolProxy,
        UnknownInteractionsClosedProtocolRequestStream,
        UnknownInteractionsClosedProtocolSynchronousProxy,
        UnknownInteractionsProtocolControlHandle, UnknownInteractionsProtocolEvent,
        UnknownInteractionsProtocolProxy, UnknownInteractionsProtocolRequest,
        UnknownInteractionsProtocolRequestStream, UnknownInteractionsProtocolSynchronousProxy,
    },
    fuchsia_async as fasync,
    fuchsia_zircon::{AsHandleRef, DurationNum, MessageBuf, Signals, Time},
    futures::stream::StreamExt,
    std::future::Future,
};

trait ExpectResumeUnwind {
    type Result;

    fn unwrap_or_resume_unwind(self) -> Self::Result;
}

impl<T> ExpectResumeUnwind for std::thread::Result<T> {
    type Result = T;

    fn unwrap_or_resume_unwind(self) -> Self::Result {
        match self {
            Ok(result) => result,
            Err(err) => std::panic::resume_unwind(err),
        }
    }
}

#[track_caller]
fn assert_nonzero_txid(buf: &[u8]) {
    assert!(buf.len() >= 4);
    let bytes = [buf[0], buf[1], buf[2], buf[3]];
    let txid = u32::from_le_bytes(bytes);
    assert_ne!(txid, 0);
}

/// Runs a synchronous one way client call.
///
/// -   `client_runner` make the one-way call.
/// -   `expected_client_message` is the bytes that the client message should
///     produce, including the transaction id.
#[track_caller]
fn run_one_way_sync<F: FnOnce(UnknownInteractionsProtocolSynchronousProxy)>(
    client_runner: F,
    expected_client_message: &[u8],
) {
    let (client_end, server_end) = Channel::create().unwrap();
    let client = UnknownInteractionsProtocolSynchronousProxy::new(client_end);

    client_runner(client);

    let mut buf = MessageBuf::new();

    server_end.read(&mut buf).expect("server end failed to read");

    assert_eq!(buf.n_handles(), 0);
    assert_eq!(buf.bytes(), expected_client_message);
}

#[test]
fn one_way_strict_sync_send() {
    run_one_way_sync(
        |client| client.strict_one_way().expect("client failed to send"),
        &[
            0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
            0xd5, 0x82, 0xb3, 0x4c, 0x50, 0x81, 0xa5, 0x1f, //
        ],
    );
}

#[test]
fn one_way_flexible_sync_send() {
    run_one_way_sync(
        |client| client.flexible_one_way().expect("client failed to send"),
        &[
            0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
            0xfc, 0x90, 0xbb, 0xe2, 0x7a, 0x27, 0x93, 0x27, //
        ],
    );
}

/// Run a test of a synchronous client call.
///
/// -   `client_call`: sends the client request.
/// -   `expected_client_message`: data the server should expect from the client,
///     excluding the transaction ID which will be checked automatically.
/// -   `server_reply`: the data the server should send back to the client,
///     excluding the transaction ID which will be added automatically.
#[track_caller]
fn run_two_way_sync<F, R>(
    client_runner: F,
    expected_client_message: &[u8],
    server_reply: &[u8],
) -> R
where
    F: 'static + FnOnce(UnknownInteractionsProtocolSynchronousProxy) -> R + Send,
    R: 'static + Send,
{
    let (client_end, server_end) = Channel::create().unwrap();
    let client = UnknownInteractionsProtocolSynchronousProxy::new(client_end);

    let t = std::thread::spawn(move || client_runner(client));

    let mut buf = MessageBuf::new();

    server_end
        .wait_handle(
            Signals::CHANNEL_READABLE | Signals::CHANNEL_PEER_CLOSED,
            Time::after(5.seconds()),
        )
        .expect("server end failed to wait for channel readable");
    server_end.read(&mut buf).expect("server end failed to read");

    assert_eq!(buf.n_handles(), 0);
    assert_nonzero_txid(buf.bytes());
    assert_eq!(&buf.bytes()[4..], expected_client_message);

    // Send a reply so we can assert that the client doesn't fail.
    let mut reply = Vec::with_capacity(4 + server_reply.len());
    reply.extend_from_slice(&buf.bytes()[..4]);
    reply.extend_from_slice(server_reply);
    server_end.write(&reply[..], &mut []).expect("server end failed to write reply");

    t.join().unwrap_or_resume_unwind()
}

#[test]
fn two_way_strict_sync_send() {
    run_two_way_sync(
        |client| client.strict_two_way(Time::after(2.seconds())),
        &[
            0x02, 0x00, 0x00, 0x01, //
            0xdc, 0xb0, 0x55, 0x70, 0x95, 0x6f, 0xba, 0x73, //
        ],
        &[
            0x02, 0x00, 0x00, 0x01, //
            0xdc, 0xb0, 0x55, 0x70, 0x95, 0x6f, 0xba, 0x73, //
        ],
    )
    .expect("client call failed");
}

#[test]
fn two_way_strict_err_sync_send() {
    run_two_way_sync(
        |client| client.strict_two_way_err(Time::after(2.seconds())),
        &[
            0x02, 0x00, 0x00, 0x01, //
            0xbb, 0x58, 0xe0, 0x08, 0x4e, 0xeb, 0x9b, 0x2e, //
        ],
        &[
            0x02, 0x00, 0x00, 0x01, //
            0xbb, 0x58, 0xe0, 0x08, 0x4e, 0xeb, 0x9b, 0x2e, //
            // Result union with success envelope to satisfy client side:
            // ordinal  ---------------------------------|
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
        ],
    )
    .expect("client call failed")
    .expect("client received application error");
}

#[test]
fn two_way_flexible_sync_send() {
    run_two_way_sync(
        |client| client.flexible_two_way(Time::after(2.seconds())),
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
        ],
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
            // Result union with success envelope to satisfy client side:
            // ordinal  ---------------------------------|
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
        ],
    )
    .expect("client call failed");
}

#[test]
fn two_way_flexible_sync_send_unknown_response() {
    let err = run_two_way_sync(
        |client| client.flexible_two_way(Time::after(2.seconds())),
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
        ],
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
            // Result union with transport_err
            // ordinal  ---------------------------------|
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0xfe, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, //
        ],
    )
    .expect_err("client call unexpectedly succeeded");
    assert_matches!(
        err,
        Error::UnsupportedMethod {
            method_name: "flexible_two_way",
            protocol_name: "(anonymous) UnknownInteractionsProtocol",
        }
    );
}

#[test]
fn two_way_flexible_sync_send_other_transport_error() {
    let err = run_two_way_sync(
        |client| client.flexible_two_way(Time::after(2.seconds())),
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
        ],
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
            // Result union with transport_err
            // ordinal  ---------------------------------|
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, //
        ],
    )
    .expect_err("client call unexpectedly succeeded");
    assert_matches!(err, Error::InvalidEnumValue);
}

#[test]
fn two_way_flexible_sync_send_error_variant() {
    let err = run_two_way_sync(
        |client| client.flexible_two_way(Time::after(2.seconds())),
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
        ],
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
            // Result union with err
            // ordinal  ---------------------------------|
            0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
        ],
    )
    .expect_err("client call unexpectedly succeeded");
    assert_matches!(err, Error::UnknownUnionTag);
}

#[test]
fn two_way_flexible_err_sync_send() {
    run_two_way_sync(
        |client| client.flexible_two_way_err(Time::after(2.seconds())),
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
        ],
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
            // Result union with success envelope to satisfy client side:
            // ordinal  ---------------------------------|
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
        ],
    )
    .expect("client call failed")
    .expect("client received application error");
}

#[test]
fn two_way_flexible_err_sync_send_unknown_response() {
    let err = run_two_way_sync(
        |client| client.flexible_two_way_err(Time::after(2.seconds())),
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
        ],
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
            // Result union with transport_err
            // ordinal  ---------------------------------|
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0xfe, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, //
        ],
    )
    .expect_err("client call unexpectedly succeeded");
    assert_matches!(
        err,
        Error::UnsupportedMethod {
            method_name: "flexible_two_way_err",
            protocol_name: "(anonymous) UnknownInteractionsProtocol",
        }
    );
}

#[test]
fn two_way_flexible_err_sync_send_other_transport_error() {
    let err = run_two_way_sync(
        |client| client.flexible_two_way_err(Time::after(2.seconds())),
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
        ],
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
            // Result union with transport_err
            // ordinal  ---------------------------------|
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, //
        ],
    )
    .expect_err("client call unexpectedly succeeded");
    assert_matches!(err, Error::InvalidEnumValue);
}

#[test]
fn two_way_flexible_err_sync_send_error_variant() {
    let err = run_two_way_sync(
        |client| client.flexible_two_way_err(Time::after(2.seconds())),
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
        ],
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
            // Result union with err
            // ordinal  ---------------------------------|
            0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
        ],
    )
    .expect("client call failed")
    .expect_err("client call unexpectedly missing application error");
    assert_eq!(err, 256);
}

#[test]
fn recieve_unknown_event_strict_sync() {
    let (client_end, server_end) = Channel::create().unwrap();
    let client = UnknownInteractionsProtocolSynchronousProxy::new(client_end);

    server_end
        .write(
            &[
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
                0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ],
            &mut [],
        )
        .expect("server end failed to write event");

    let err = client
        .wait_for_event(Time::after(2.seconds()))
        .expect_err("unknown event unexpectedly succeeded");

    assert_matches!(
        err,
        Error::UnknownOrdinal {
            ordinal: 0xff10ff10ff10ff10,
            protocol_name: "(anonymous) UnknownInteractionsProtocol",
        }
    );
}

#[test]
fn recieve_unknown_event_flexible_sync() {
    let (client_end, server_end) = Channel::create().unwrap();
    let client = UnknownInteractionsProtocolSynchronousProxy::new(client_end);

    server_end
        .write(
            &[
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ],
            &mut [],
        )
        .expect("server end failed to write event");

    let event = client
        .wait_for_event(Time::after(2.seconds()))
        .expect("unknown flexible event unexpectedly failed");

    assert_matches!(
        event,
        UnknownInteractionsProtocolEvent::_UnknownEvent { ordinal: 0xff10ff10ff10ff10 }
    );
}

#[test]
fn recieve_unknown_event_strict_ajar_sync() {
    let (client_end, server_end) = Channel::create().unwrap();
    let client = UnknownInteractionsAjarProtocolSynchronousProxy::new(client_end);

    server_end
        .write(
            &[
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
                0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ],
            &mut [],
        )
        .expect("server end failed to write event");

    let err = client
        .wait_for_event(Time::after(2.seconds()))
        .expect_err("unknown event unexpectedly succeeded");

    assert_matches!(
        err,
        Error::UnknownOrdinal {
            ordinal: 0xff10ff10ff10ff10,
            protocol_name: "(anonymous) UnknownInteractionsAjarProtocol",
        }
    );
}

#[test]
fn recieve_unknown_event_flexible_ajar_sync() {
    let (client_end, server_end) = Channel::create().unwrap();
    let client = UnknownInteractionsAjarProtocolSynchronousProxy::new(client_end);

    server_end
        .write(
            &[
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ],
            &mut [],
        )
        .expect("server end failed to write event");

    let event = client
        .wait_for_event(Time::after(2.seconds()))
        .expect("unknown flexible event unexpectedly failed");

    assert_matches!(
        event,
        UnknownInteractionsAjarProtocolEvent::_UnknownEvent { ordinal: 0xff10ff10ff10ff10 }
    );
}

#[test]
fn recieve_unknown_event_strict_closed_sync() {
    let (client_end, server_end) = Channel::create().unwrap();
    let client = UnknownInteractionsClosedProtocolSynchronousProxy::new(client_end);

    server_end
        .write(
            &[
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
                0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ],
            &mut [],
        )
        .expect("server end failed to write event");

    let err = client
        .wait_for_event(Time::after(2.seconds()))
        .expect_err("unknown event unexpectedly succeeded");

    assert_matches!(
        err,
        Error::UnknownOrdinal {
            ordinal: 0xff10ff10ff10ff10,
            protocol_name: "(anonymous) UnknownInteractionsClosedProtocol",
        }
    );
}

#[test]
fn recieve_unknown_event_flexible_closed_sync() {
    let (client_end, server_end) = Channel::create().unwrap();
    let client = UnknownInteractionsClosedProtocolSynchronousProxy::new(client_end);

    server_end
        .write(
            &[
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ],
            &mut [],
        )
        .expect("server end failed to write event");

    let err = client
        .wait_for_event(Time::after(2.seconds()))
        .expect_err("unknown event unexpectedly succeeded");

    assert_matches!(
        err,
        Error::UnknownOrdinal {
            ordinal: 0xff10ff10ff10ff10,
            protocol_name: "(anonymous) UnknownInteractionsClosedProtocol",
        }
    );
}

/// Runs an one way client call using the async client.
///
/// Even though this goes through the async client, it doesn't actually need to
/// be async, since one-way calls don't need to wait for anything.
///
/// -   `client_runner` make the one-way call.
/// -   `expected_client_message` is the bytes that the client message should
///     produce, including the transaction id.
#[track_caller]
fn run_one_way_async<F>(client_runner: F, expected_client_message: &[u8])
where
    F: FnOnce(UnknownInteractionsProtocolProxy),
{
    let (client_end, server_end) = Channel::create().unwrap();
    let client_end = AsyncChannel::from_channel(client_end).unwrap();
    let client = UnknownInteractionsProtocolProxy::from_channel(client_end);

    client_runner(client);

    let mut buf = MessageBuf::new();

    server_end.read(&mut buf).expect("server end failed to read");

    assert_eq!(buf.n_handles(), 0);
    assert_eq!(buf.bytes(), expected_client_message,);
}

#[fasync::run_singlethreaded(test)]
async fn one_way_strict_async_send() {
    run_one_way_async(
        |client| client.strict_one_way().expect("client failed to send"),
        &[
            0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
            0xd5, 0x82, 0xb3, 0x4c, 0x50, 0x81, 0xa5, 0x1f, //
        ],
    );
}

#[fasync::run_singlethreaded(test)]
async fn one_way_flexible_async_send() {
    run_one_way_async(
        |client| client.flexible_one_way().expect("client failed to send"),
        &[
            0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
            0xfc, 0x90, 0xbb, 0xe2, 0x7a, 0x27, 0x93, 0x27, //
        ],
    );
}

/// Run a test of an asynchronous client call.
///
/// -   `client_call`: sends the client request.
/// -   `expected_client_message`: data the server should expect from the client,
///     excluding the transaction ID which will be checked automatically.
/// -   `server_reply`: the data the server should send back to the client,
///     excluding the transaction ID which will be added automatically.
#[track_caller]
async fn run_two_way_async<C, F, R>(
    client_runner: C,
    expected_client_message: &'static [u8],
    server_reply: &'static [u8],
) -> R
where
    C: FnOnce(UnknownInteractionsProtocolProxy) -> F,
    F: Future<Output = R>,
{
    let (client_end, server_end) = Channel::create().unwrap();
    let client_end = AsyncChannel::from_channel(client_end).unwrap();
    let client = UnknownInteractionsProtocolProxy::new(client_end);

    let t = std::thread::spawn(move || {
        let mut buf = MessageBuf::new();

        server_end
            .wait_handle(
                Signals::CHANNEL_READABLE | Signals::CHANNEL_PEER_CLOSED,
                Time::after(5.seconds()),
            )
            .expect("failed to wait for channel readable");
        server_end.read(&mut buf).expect("failed to read");

        assert_eq!(buf.n_handles(), 0);
        assert_nonzero_txid(buf.bytes());
        assert_eq!(&buf.bytes()[4..], expected_client_message);

        // Send a reply so we can assert that the client doesn't fail.
        let mut reply = Vec::with_capacity(4 + server_reply.len());
        reply.extend_from_slice(&buf.bytes()[..4]);
        reply.extend_from_slice(server_reply);
        server_end.write(&reply[..], &mut []).expect("failed to write reply");
    });

    let res = client_runner(client).await;

    t.join().unwrap_or_resume_unwind();

    res
}

#[fasync::run_singlethreaded(test)]
async fn two_way_strict_async_send() {
    run_two_way_async(
        |client| client.strict_two_way(),
        &[
            0x02, 0x00, 0x00, 0x01, //
            0xdc, 0xb0, 0x55, 0x70, 0x95, 0x6f, 0xba, 0x73, //
        ],
        &[
            0x02, 0x00, 0x00, 0x01, //
            0xdc, 0xb0, 0x55, 0x70, 0x95, 0x6f, 0xba, 0x73, //
        ],
    )
    .await
    .expect("client call failed");
}

#[fasync::run_singlethreaded(test)]
async fn two_way_strict_err_async_send() {
    run_two_way_async(
        |client| client.strict_two_way_err(),
        &[
            0x02, 0x00, 0x00, 0x01, //
            0xbb, 0x58, 0xe0, 0x08, 0x4e, 0xeb, 0x9b, 0x2e, //
        ],
        &[
            0x02, 0x00, 0x00, 0x01, //
            0xbb, 0x58, 0xe0, 0x08, 0x4e, 0xeb, 0x9b, 0x2e, //
            // Result union with success envelope to satisfy client side:
            // ordinal  ---------------------------------|
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
        ],
    )
    .await
    .expect("client call failed")
    .expect("client received application error");
}

#[fasync::run_singlethreaded(test)]
async fn two_way_flexible_async_send() {
    run_two_way_async(
        |client| client.flexible_two_way(),
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
        ],
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
            // Result union with success envelope to satisfy client side:
            // ordinal  ---------------------------------|
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
        ],
    )
    .await
    .expect("client call failed");
}

#[fasync::run_singlethreaded(test)]
async fn two_way_flexible_async_send_unknown_response() {
    let err = run_two_way_async(
        |client| client.flexible_two_way(),
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
        ],
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
            // Result union with transport_err
            // ordinal  ---------------------------------|
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0xfe, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, //
        ],
    )
    .await
    .expect_err("client call unexpectedly succeeded");
    assert_matches!(
        err,
        Error::UnsupportedMethod {
            method_name: "flexible_two_way",
            protocol_name: "(anonymous) UnknownInteractionsProtocol",
        }
    );
}

#[fasync::run_singlethreaded(test)]
async fn two_way_flexible_async_send_other_transport_error() {
    let err = run_two_way_async(
        |client| client.flexible_two_way(),
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
        ],
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
            // Result union with transport_err
            // ordinal  ---------------------------------|
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, //
        ],
    )
    .await
    .expect_err("client call unexpectedly succeeded");
    assert_matches!(err, Error::InvalidEnumValue);
}

#[fasync::run_singlethreaded(test)]
async fn two_way_flexible_async_send_error_variant() {
    let err = run_two_way_async(
        |client| client.flexible_two_way(),
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
        ],
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
            // Result union with err
            // ordinal  ---------------------------------|
            0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
        ],
    )
    .await
    .expect_err("client call unexpectedly succeeded");
    assert_matches!(err, Error::UnknownUnionTag);
}

#[fasync::run_singlethreaded(test)]
async fn two_way_flexible_err_async_send() {
    run_two_way_async(
        |client| client.flexible_two_way_err(),
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
        ],
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
            // Result union with success envelope to satisfy client side:
            // ordinal  ---------------------------------|
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
        ],
    )
    .await
    .expect("client call failed")
    .expect("client received application error");
}

#[fasync::run_singlethreaded(test)]
async fn two_way_flexible_err_async_send_unknown_response() {
    let err = run_two_way_async(
        |client| client.flexible_two_way_err(),
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
        ],
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
            // Result union with transport_err
            // ordinal  ---------------------------------|
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0xfe, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, //
        ],
    )
    .await
    .expect_err("client call unexpectedly succeeded");
    assert_matches!(
        err,
        Error::UnsupportedMethod {
            method_name: "flexible_two_way_err",
            protocol_name: "(anonymous) UnknownInteractionsProtocol",
        }
    );
}

#[fasync::run_singlethreaded(test)]
async fn two_way_flexible_err_async_send_other_transport_error() {
    let err = run_two_way_async(
        |client| client.flexible_two_way_err(),
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
        ],
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
            // Result union with transport_err
            // ordinal  ---------------------------------|
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, //
        ],
    )
    .await
    .expect_err("client call unexpectedly succeeded");
    assert_matches!(err, Error::InvalidEnumValue);
}

#[fasync::run_singlethreaded(test)]
async fn two_way_flexible_err_async_send_error_variant() {
    let err = run_two_way_async(
        |client| client.flexible_two_way_err(),
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
        ],
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
            // Result union with err
            // ordinal  ---------------------------------|
            0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
        ],
    )
    .await
    .expect("client call failed")
    .expect_err("client call unexpectedly missing application error");
    assert_eq!(err, 256);
}

#[fasync::run_singlethreaded(test)]
async fn receive_unknown_event_strict_async() {
    let (client_end, server_end) = Channel::create().unwrap();
    let client_end = AsyncChannel::from_channel(client_end).unwrap();
    let mut client = UnknownInteractionsProtocolProxy::from_channel(client_end).take_event_stream();

    server_end
        .write(
            &[
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
                0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ],
            &mut [],
        )
        .expect("server end failed to write event");

    let err = client
        .next()
        .await
        .expect("client event stream ended")
        .expect_err("client expected error for unknown strict method.");

    assert_matches!(
        err,
        Error::UnknownOrdinal {
            ordinal: 0xff10ff10ff10ff10,
            protocol_name: "(anonymous) UnknownInteractionsProtocol",
        }
    );
}

#[fasync::run_singlethreaded(test)]
async fn receive_unknown_event_flexible_async() {
    let (client_end, server_end) = Channel::create().unwrap();
    let client_end = AsyncChannel::from_channel(client_end).unwrap();
    let mut client = UnknownInteractionsProtocolProxy::from_channel(client_end).take_event_stream();

    server_end
        .write(
            &[
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ],
            &mut [],
        )
        .expect("server end failed to write event");

    let event = client
        .next()
        .await
        .expect("client event stream ended")
        .expect("unknown flexible event unexpectedly failed");

    assert_matches!(
        event,
        UnknownInteractionsProtocolEvent::_UnknownEvent { ordinal: 0xff10ff10ff10ff10 }
    );
}

#[fasync::run_singlethreaded(test)]
async fn receive_unknown_event_strict_ajar_async() {
    let (client_end, server_end) = Channel::create().unwrap();
    let client_end = AsyncChannel::from_channel(client_end).unwrap();
    let mut client =
        UnknownInteractionsAjarProtocolProxy::from_channel(client_end).take_event_stream();

    server_end
        .write(
            &[
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
                0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ],
            &mut [],
        )
        .expect("server end failed to write event");

    let err = client
        .next()
        .await
        .expect("client event stream ended")
        .expect_err("client expected error for unknown strict method.");

    assert_matches!(
        err,
        Error::UnknownOrdinal {
            ordinal: 0xff10ff10ff10ff10,
            protocol_name: "(anonymous) UnknownInteractionsAjarProtocol",
        }
    );
}

#[fasync::run_singlethreaded(test)]
async fn receive_unknown_event_flexible_ajar_async() {
    let (client_end, server_end) = Channel::create().unwrap();
    let client_end = AsyncChannel::from_channel(client_end).unwrap();
    let mut client =
        UnknownInteractionsAjarProtocolProxy::from_channel(client_end).take_event_stream();

    server_end
        .write(
            &[
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ],
            &mut [],
        )
        .expect("server end failed to write event");

    let event = client
        .next()
        .await
        .expect("client event stream ended")
        .expect("unknown flexible event unexpectedly failed");

    assert_matches!(
        event,
        UnknownInteractionsAjarProtocolEvent::_UnknownEvent { ordinal: 0xff10ff10ff10ff10 }
    );
}

#[fasync::run_singlethreaded(test)]
async fn receive_unknown_event_strict_closed_async() {
    let (client_end, server_end) = Channel::create().unwrap();
    let client_end = AsyncChannel::from_channel(client_end).unwrap();
    let mut client =
        UnknownInteractionsClosedProtocolProxy::from_channel(client_end).take_event_stream();

    server_end
        .write(
            &[
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
                0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ],
            &mut [],
        )
        .expect("server end failed to write event");

    let err = client
        .next()
        .await
        .expect("client event stream ended")
        .expect_err("client expected error for unknown event on closed protocol.");

    assert_matches!(
        err,
        Error::UnknownOrdinal {
            ordinal: 0xff10ff10ff10ff10,
            protocol_name: "(anonymous) UnknownInteractionsClosedProtocol",
        }
    );
}

#[fasync::run_singlethreaded(test)]
async fn receive_unknown_event_flexible_closed_async() {
    let (client_end, server_end) = Channel::create().unwrap();
    let client_end = AsyncChannel::from_channel(client_end).unwrap();
    let mut client =
        UnknownInteractionsClosedProtocolProxy::from_channel(client_end).take_event_stream();

    server_end
        .write(
            &[
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ],
            &mut [],
        )
        .expect("server end failed to write event");

    let err = client
        .next()
        .await
        .expect("client event stream ended")
        .expect_err("client expected error for unknown event on closed protocol.");

    assert_matches!(
        err,
        Error::UnknownOrdinal {
            ordinal: 0xff10ff10ff10ff10,
            protocol_name: "(anonymous) UnknownInteractionsClosedProtocol",
        }
    );
}

/// Test sending an event from the server.
///
/// -   `server_runner` action to send the event from the server.
/// -   `expected_server_message` is the bytes that the server should produce,
///     including the transaction id.
#[track_caller]
fn run_send_event<F>(server_runner: F, expected_server_message: &[u8])
where
    F: FnOnce(UnknownInteractionsProtocolControlHandle),
{
    let (client_end, server_end) = Channel::create().unwrap();
    let server_end = AsyncChannel::from_channel(server_end).unwrap();
    let server =
        UnknownInteractionsProtocolRequestStream::from_channel(server_end).control_handle();

    server_runner(server);

    let mut buf = MessageBuf::new();

    client_end
        .wait_handle(
            Signals::CHANNEL_READABLE | Signals::CHANNEL_PEER_CLOSED,
            Time::after(5.seconds()),
        )
        .expect("failed to wait for channel readable");
    client_end.read(&mut buf).expect("failed to read");

    assert_eq!(buf.n_handles(), 0);
    assert_eq!(buf.bytes(), expected_server_message);
}

#[fasync::run_singlethreaded(test)]
async fn send_strict_event() {
    run_send_event(
        |server| server.send_strict_event().expect("server failed to send event"),
        &[
            0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
            0x38, 0x27, 0xa3, 0x91, 0x98, 0x41, 0x4b, 0x58, //
        ],
    );
}

#[fasync::run_singlethreaded(test)]
async fn send_flexible_event() {
    run_send_event(
        |server| server.send_flexible_event().expect("server failed to send event"),
        &[
            0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
            0x6c, 0x2c, 0x80, 0x0b, 0x8e, 0x1a, 0x7a, 0x31, //
        ],
    );
}

#[fasync::run_singlethreaded(test)]
async fn send_strict_err_event() {
    run_send_event(
        |server| server.send_strict_event_err(&mut Ok(())).expect("server failed to send event"),
        &[
            0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
            0xe8, 0xc1, 0x96, 0x8e, 0x1e, 0x34, 0x7c, 0x53, //
            // Result union with success:
            // ordinal  ---------------------------------|
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
        ],
    );
}

#[fasync::run_singlethreaded(test)]
async fn send_flexible_err_event_success() {
    run_send_event(
        |server| server.send_flexible_event_err(&mut Ok(())).expect("server failed to send event"),
        &[
            0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
            0xca, 0xa7, 0x49, 0xfa, 0x0e, 0x90, 0xe7, 0x41, //
            // Result union with success:
            // ordinal  ---------------------------------|
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
        ],
    );
}

#[fasync::run_singlethreaded(test)]
async fn send_flexible_err_event_error() {
    run_send_event(
        |server| server.send_flexible_event_err(&mut Err(15)).expect("server failed to send event"),
        &[
            0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
            0xca, 0xa7, 0x49, 0xfa, 0x0e, 0x90, 0xe7, 0x41, //
            // Result union with err:
            // ordinal  ---------------------------------|
            0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
        ],
    );
}

/// Run a test of a server's two-way request handler.
///
/// -   `serve_requests`: actions to take on the server end.
/// -   `client_message`: Message to send from the client to initiate the server
///     handling. Must include the transaction ID.
/// -   `expected_server_reply`: Message to expect back from the server. Must
///     include the expected transaction ID.
#[track_caller]
async fn run_two_way_response<S, F>(
    server_runner: S,
    client_message: &'static [u8],
    expected_server_reply: &'static [u8],
) where
    S: FnOnce(UnknownInteractionsProtocolRequestStream) -> F,
    F: Future<Output = ()>,
{
    let (client_end, server_end) = Channel::create().unwrap();
    let server_end = AsyncChannel::from_channel(server_end).unwrap();
    let server = UnknownInteractionsProtocolRequestStream::from_channel(server_end);

    let t = std::thread::spawn(move || {
        client_end.write(client_message, &mut []).expect("client end failed to write request");

        let mut buf = MessageBuf::new();

        client_end
            .wait_handle(
                Signals::CHANNEL_READABLE | Signals::CHANNEL_PEER_CLOSED,
                Time::after(5.seconds()),
            )
            .expect("client end failed to wait for channel readable");
        client_end.read(&mut buf).expect("client end failed to read");

        assert_eq!(buf.n_handles(), 0);
        assert_eq!(buf.bytes(), expected_server_reply,);
    });

    server_runner(server).await;
    t.join().unwrap_or_resume_unwind();
}

#[fasync::run_singlethreaded(test)]
async fn strict_two_way_response() {
    run_two_way_response(
        |mut server| async move {
            let request = server
                .next()
                .await
                .expect("server request stream ended")
                .expect("server failed to read request");
            let responder = assert_matches!(
                request,
                UnknownInteractionsProtocolRequest::StrictTwoWay { responder } => responder
            );
            responder.send().expect("server failed to send response");
        },
        &[
            0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
            0xdc, 0xb0, 0x55, 0x70, 0x95, 0x6f, 0xba, 0x73, //
        ],
        &[
            0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
            0xdc, 0xb0, 0x55, 0x70, 0x95, 0x6f, 0xba, 0x73, //
        ],
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn strict_two_way_err_response_success() {
    run_two_way_response(
        |mut server| async move {
            let request = server
                .next()
                .await
                .expect("server request stream ended")
                .expect("server failed to read request");
            let responder = assert_matches!(
                request,
                UnknownInteractionsProtocolRequest::StrictTwoWayErr { responder } => responder
            );
            responder.send(&mut Ok(())).expect("server failed to send response");
        },
        &[
            0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
            0xbb, 0x58, 0xe0, 0x08, 0x4e, 0xeb, 0x9b, 0x2e, //
        ],
        &[
            0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
            0xbb, 0x58, 0xe0, 0x08, 0x4e, 0xeb, 0x9b, 0x2e, //
            // Result union with success:
            // ordinal  ---------------------------------|
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
        ],
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn strict_two_way_err_response_failure() {
    run_two_way_response(
        |mut server| async move {
            let request = server
                .next()
                .await
                .expect("server request stream ended")
                .expect("server failed to read request");
            let responder = assert_matches!(
                request,
                UnknownInteractionsProtocolRequest::StrictTwoWayErr { responder } => responder
            );
            responder.send(&mut Err(32)).expect("server failed to send response");
        },
        &[
            0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
            0xbb, 0x58, 0xe0, 0x08, 0x4e, 0xeb, 0x9b, 0x2e, //
        ],
        &[
            0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
            0xbb, 0x58, 0xe0, 0x08, 0x4e, 0xeb, 0x9b, 0x2e, //
            // Result union with err:
            // ordinal  ---------------------------------|
            0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
        ],
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn flexible_two_way_response() {
    run_two_way_response(
        |mut server| async move {
            let request = server
                .next()
                .await
                .expect("server request stream ended")
                .expect("server failed to read request");
            let responder = assert_matches!(
                request,
                UnknownInteractionsProtocolRequest::FlexibleTwoWay { responder } => responder
            );
            responder.send().expect("server failed to send response");
        },
        &[
            0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
            0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
        ],
        &[
            0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
            0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f, //
            // Result union with success:
            // ordinal  ---------------------------------|
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
        ],
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn flexible_two_way_err_response_success() {
    run_two_way_response(
        |mut server| async move {
            let request = server
                .next()
                .await
                .expect("server request stream ended")
                .expect("server failed to read request");
            let responder = assert_matches!(
                request,
                UnknownInteractionsProtocolRequest::FlexibleTwoWayErr { responder } => responder
            );
            responder.send(&mut Ok(())).expect("server failed to send response");
        },
        &[
            0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
            0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
        ],
        &[
            0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
            0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
            // Result union with success:
            // ordinal  ---------------------------------|
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
        ],
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn flexible_two_way_err_response_error() {
    run_two_way_response(
        |mut server| async move {
            let request = server
                .next()
                .await
                .expect("server request stream ended")
                .expect("server failed to read request");
            let responder = assert_matches!(
                request,
                UnknownInteractionsProtocolRequest::FlexibleTwoWayErr { responder } => responder
            );
            responder.send(&mut Err(15)).expect("server failed to send response");
        },
        &[
            0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
            0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
        ],
        &[
            0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
            0x62, 0xbd, 0x20, 0xcb, 0xde, 0x05, 0x69, 0x70, //
            // Result union with error:
            // ordinal  ---------------------------------|
            0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
        ],
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn receive_unknown_one_way_strict() {
    let (client_end, server_end) = Channel::create().unwrap();
    let server_end = AsyncChannel::from_channel(server_end).unwrap();
    let mut server = UnknownInteractionsProtocolRequestStream::from_channel(server_end);

    client_end
        .write(
            &[
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
                0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ],
            &mut [],
        )
        .expect("client end failed to write request");

    let err = server
        .next()
        .await
        .expect("server request stream ended")
        .expect_err("server expected error for unknown strict method.");

    assert_matches!(
        err,
        Error::UnknownOrdinal {
            ordinal: 0xff10ff10ff10ff10,
            protocol_name: "(anonymous) UnknownInteractionsProtocol",
        }
    );
}

#[fasync::run_singlethreaded(test)]
async fn receive_unknown_one_way_flexible() {
    let (client_end, server_end) = Channel::create().unwrap();
    let server_end = AsyncChannel::from_channel(server_end).unwrap();
    let mut server = UnknownInteractionsProtocolRequestStream::from_channel(server_end);

    client_end
        .write(
            &[
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ],
            &mut [],
        )
        .expect("client end failed to write request");

    let request = server
        .next()
        .await
        .expect("server request stream ended")
        .expect("server failed to read request");

    assert_matches!(
        request,
        UnknownInteractionsProtocolRequest::_UnknownMethod {
            ordinal: 0xff10ff10ff10ff10,
            direction: fidl::endpoints::UnknownMethodDirection::OneWay,
            control_handle: _,
        }
    );
}

#[fasync::run_singlethreaded(test)]
async fn receive_unknown_two_way_strict() {
    let (client_end, server_end) = Channel::create().unwrap();
    let server_end = AsyncChannel::from_channel(server_end).unwrap();
    let mut server = UnknownInteractionsProtocolRequestStream::from_channel(server_end);

    client_end
        .write(
            &[
                0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
                0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ],
            &mut [],
        )
        .expect("client end failed to write request");

    let err = server
        .next()
        .await
        .expect("server request stream ended")
        .expect_err("server expected error for unknown strict method.");

    assert_matches!(
        err,
        Error::UnknownOrdinal {
            ordinal: 0xff10ff10ff10ff10,
            protocol_name: "(anonymous) UnknownInteractionsProtocol",
        }
    );
}

#[fasync::run_singlethreaded(test)]
async fn receive_unknown_two_way_flexible() {
    run_two_way_response(
        |mut server| async move {
            let request = server
                .next()
                .await
                .expect("server request stream ended")
                .expect("server failed to read request");
            assert_matches!(
                request,
                UnknownInteractionsProtocolRequest::_UnknownMethod {
                    ordinal: 0xff10ff10ff10ff10,
                    direction: fidl::endpoints::UnknownMethodDirection::TwoWay,
                    control_handle: _,
                }
            );
        },
        &[
            0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
            0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
        ],
        &[
            0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
            0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            // Result union with transport_err:
            // ordinal  ---------------------------------|
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0xfe, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, //
        ],
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn receive_unknown_one_way_ajar_strict() {
    let (client_end, server_end) = Channel::create().unwrap();
    let server_end = AsyncChannel::from_channel(server_end).unwrap();
    let mut server = UnknownInteractionsAjarProtocolRequestStream::from_channel(server_end);

    client_end
        .write(
            &[
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
                0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ],
            &mut [],
        )
        .expect("client end failed to write request");

    let err = server
        .next()
        .await
        .expect("server request stream ended")
        .expect_err("server expected error for unknown strict method.");

    assert_matches!(
        err,
        Error::UnknownOrdinal {
            ordinal: 0xff10ff10ff10ff10,
            protocol_name: "(anonymous) UnknownInteractionsAjarProtocol",
        }
    );
}

#[fasync::run_singlethreaded(test)]
async fn receive_unknown_one_way_ajar_flexible() {
    let (client_end, server_end) = Channel::create().unwrap();
    let server_end = AsyncChannel::from_channel(server_end).unwrap();
    let mut server = UnknownInteractionsAjarProtocolRequestStream::from_channel(server_end);

    client_end
        .write(
            &[
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ],
            &mut [],
        )
        .expect("client end failed to write request");

    let request = server
        .next()
        .await
        .expect("server request stream ended")
        .expect("server failed to read request");

    assert_matches!(
        request,
        UnknownInteractionsAjarProtocolRequest::_UnknownMethod {
            ordinal: 0xff10ff10ff10ff10,
            control_handle: _,
        }
    );
}

#[fasync::run_singlethreaded(test)]
async fn receive_unknown_two_way_ajar_strict() {
    let (client_end, server_end) = Channel::create().unwrap();
    let server_end = AsyncChannel::from_channel(server_end).unwrap();
    let mut server = UnknownInteractionsAjarProtocolRequestStream::from_channel(server_end);

    client_end
        .write(
            &[
                0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
                0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ],
            &mut [],
        )
        .expect("client end failed to write request");

    let err = server
        .next()
        .await
        .expect("server request stream ended")
        .expect_err("server expected error for unknown two-way method in ajar protocol.");

    assert_matches!(
        err,
        Error::UnknownOrdinal {
            ordinal: 0xff10ff10ff10ff10,
            protocol_name: "(anonymous) UnknownInteractionsAjarProtocol",
        }
    );
}

#[fasync::run_singlethreaded(test)]
async fn receive_unknown_two_way_ajar_flexible() {
    let (client_end, server_end) = Channel::create().unwrap();
    let server_end = AsyncChannel::from_channel(server_end).unwrap();
    let mut server = UnknownInteractionsAjarProtocolRequestStream::from_channel(server_end);

    client_end
        .write(
            &[
                0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ],
            &mut [],
        )
        .expect("client end failed to write request");

    let err = server
        .next()
        .await
        .expect("server request stream ended")
        .expect_err("server expected error for unknown two-way method in ajar protocol.");

    assert_matches!(
        err,
        Error::UnknownOrdinal {
            ordinal: 0xff10ff10ff10ff10,
            protocol_name: "(anonymous) UnknownInteractionsAjarProtocol",
        }
    );
}

#[fasync::run_singlethreaded(test)]
async fn receive_unknown_one_way_closed_strict() {
    let (client_end, server_end) = Channel::create().unwrap();
    let server_end = AsyncChannel::from_channel(server_end).unwrap();
    let mut server = UnknownInteractionsClosedProtocolRequestStream::from_channel(server_end);

    client_end
        .write(
            &[
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
                0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ],
            &mut [],
        )
        .expect("client end failed to write request");

    let err = server
        .next()
        .await
        .expect("server request stream ended")
        .expect_err("server expected error for unknown method on closed protocol.");

    assert_matches!(
        err,
        Error::UnknownOrdinal {
            ordinal: 0xff10ff10ff10ff10,
            protocol_name: "(anonymous) UnknownInteractionsClosedProtocol",
        }
    );
}

#[fasync::run_singlethreaded(test)]
async fn receive_unknown_one_way_closed_flexible() {
    let (client_end, server_end) = Channel::create().unwrap();
    let server_end = AsyncChannel::from_channel(server_end).unwrap();
    let mut server = UnknownInteractionsClosedProtocolRequestStream::from_channel(server_end);

    client_end
        .write(
            &[
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ],
            &mut [],
        )
        .expect("client end failed to write request");

    let err = server
        .next()
        .await
        .expect("server request stream ended")
        .expect_err("server expected error for unknown method on closed protocol.");

    assert_matches!(
        err,
        Error::UnknownOrdinal {
            ordinal: 0xff10ff10ff10ff10,
            protocol_name: "(anonymous) UnknownInteractionsClosedProtocol",
        }
    );
}

#[fasync::run_singlethreaded(test)]
async fn receive_unknown_two_way_closed_strict() {
    let (client_end, server_end) = Channel::create().unwrap();
    let server_end = AsyncChannel::from_channel(server_end).unwrap();
    let mut server = UnknownInteractionsClosedProtocolRequestStream::from_channel(server_end);

    client_end
        .write(
            &[
                0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
                0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ],
            &mut [],
        )
        .expect("client end failed to write request");

    let err = server
        .next()
        .await
        .expect("server request stream ended")
        .expect_err("server expected error for unknown strict method.");

    assert_matches!(
        err,
        Error::UnknownOrdinal {
            ordinal: 0xff10ff10ff10ff10,
            protocol_name: "(anonymous) UnknownInteractionsClosedProtocol",
        }
    );
}

#[fasync::run_singlethreaded(test)]
async fn receive_unknown_two_way_closed_flexible() {
    let (client_end, server_end) = Channel::create().unwrap();
    let server_end = AsyncChannel::from_channel(server_end).unwrap();
    let mut server = UnknownInteractionsClosedProtocolRequestStream::from_channel(server_end);

    client_end
        .write(
            &[
                0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ],
            &mut [],
        )
        .expect("client end failed to write request");

    let err = server
        .next()
        .await
        .expect("server request stream ended")
        .expect_err("server expected error for unknown strict method.");

    assert_matches!(
        err,
        Error::UnknownOrdinal {
            ordinal: 0xff10ff10ff10ff10,
            protocol_name: "(anonymous) UnknownInteractionsClosedProtocol",
        }
    );
}
