// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This file tests how FIDL handles unknown interactions.

use assert_matches::assert_matches;
use {
    fidl::{endpoints::RequestStream, AsyncChannel, Channel},
    fidl_fidl_rust_test_external::{
        UnknownInteractionsProtocolProxy, UnknownInteractionsProtocolRequest,
        UnknownInteractionsProtocolRequestStream, UnknownInteractionsProtocolSynchronousProxy,
    },
    fuchsia_async as fasync,
    fuchsia_async::futures::stream::StreamExt,
    fuchsia_zircon::{AsHandleRef, DurationNum, MessageBuf, Signals, Time},
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

#[test]
fn one_way_strict_sync_send() {
    let (client_end, server_end) = Channel::create().unwrap();
    let client = UnknownInteractionsProtocolSynchronousProxy::new(client_end);
    client.strict_one_way().expect("failed to send");

    let mut buf = MessageBuf::new();

    server_end.read(&mut buf).expect("failed to read");

    assert_eq!(buf.n_handles(), 0);
    assert_eq!(
        buf.bytes(),
        &[
            0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
            0x5f, 0xce, 0xfc, 0xca, 0x42, 0x86, 0xf2, 0x6d, //
        ]
    );
}

#[test]
fn one_way_flexible_sync_send() {
    let (client_end, server_end) = Channel::create().unwrap();
    let client = UnknownInteractionsProtocolSynchronousProxy::new(client_end);
    client.flexible_one_way().expect("failed to send");

    let mut buf = MessageBuf::new();

    server_end.read(&mut buf).expect("failed to read");

    assert_eq!(buf.n_handles(), 0);
    assert_eq!(
        buf.bytes(),
        &[
            0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
            0xb1, 0x0a, 0xb9, 0x03, 0x36, 0x78, 0xf1, 0x64, //
        ]
    );
}

#[test]
fn two_way_strict_sync_send() {
    let (client_end, server_end) = Channel::create().unwrap();
    let client = UnknownInteractionsProtocolSynchronousProxy::new(client_end);

    let t = std::thread::spawn(move || {
        client.strict_two_way(Time::after(2.seconds())).expect("failed to send");
    });

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
    assert_eq!(
        &buf.bytes()[4..],
        &[
            0x02, 0x00, 0x00, 0x01, //
            0xe6, 0xf0, 0x37, 0x60, 0x2a, 0x7a, 0xd6, 0x2d, //
        ]
    );

    // Send a reply so we can assert that the client doesn't fail.
    let mut reply = [
        0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
        0xe6, 0xf0, 0x37, 0x60, 0x2a, 0x7a, 0xd6, 0x2d, //
    ];
    reply[..4].copy_from_slice(&buf.bytes()[..4]);
    server_end.write(&reply[..], &mut []).expect("failed to write reply");

    t.join().unwrap_or_resume_unwind();
}

#[test]
fn two_way_flexible_sync_send() {
    let (client_end, server_end) = Channel::create().unwrap();
    let client = UnknownInteractionsProtocolSynchronousProxy::new(client_end);

    let t = std::thread::spawn(move || {
        client.flexible_two_way(Time::after(2.seconds())).expect("failed to send");
    });

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
    assert_eq!(
        &buf.bytes()[4..],
        &[
            0x02, 0x00, 0x80, 0x01, //
            0x34, 0xa0, 0xd0, 0x6d, 0x01, 0xb8, 0x0a, 0x1a, //
        ]
    );

    // Send a reply so we can assert that the client doesn't fail.
    let mut reply = [
        0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
        0x34, 0xa0, 0xd0, 0x6d, 0x01, 0xb8, 0x0a, 0x1a, //
    ];
    reply[..4].copy_from_slice(&buf.bytes()[..4]);
    server_end.write(&reply[..], &mut []).expect("failed to write reply");

    t.join().unwrap_or_resume_unwind();
}

#[fasync::run_singlethreaded(test)]
async fn one_way_strict_async_send() {
    let (client_end, server_end) = Channel::create().unwrap();
    let client_end = AsyncChannel::from_channel(client_end).unwrap();
    let client = UnknownInteractionsProtocolProxy::new(client_end);
    client.strict_one_way().expect("failed to send");

    let mut buf = MessageBuf::new();

    server_end.read(&mut buf).expect("failed to read");

    assert_eq!(buf.n_handles(), 0);
    assert_eq!(
        buf.bytes(),
        &[
            0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
            0x5f, 0xce, 0xfc, 0xca, 0x42, 0x86, 0xf2, 0x6d, //
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn one_way_flexible_async_send() {
    let (client_end, server_end) = Channel::create().unwrap();
    let client_end = AsyncChannel::from_channel(client_end).unwrap();
    let client = UnknownInteractionsProtocolProxy::new(client_end);
    client.flexible_one_way().expect("failed to send");

    let mut buf = MessageBuf::new();

    server_end.read(&mut buf).expect("failed to read");

    assert_eq!(buf.n_handles(), 0);
    assert_eq!(
        buf.bytes(),
        &[
            0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
            0xb1, 0x0a, 0xb9, 0x03, 0x36, 0x78, 0xf1, 0x64, //
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn two_way_strict_async_send() {
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
        assert_eq!(
            &buf.bytes()[4..],
            &[
                0x02, 0x00, 0x00, 0x01, //
                0xe6, 0xf0, 0x37, 0x60, 0x2a, 0x7a, 0xd6, 0x2d, //
            ]
        );

        // Send a reply so we can assert that the client doesn't fail.
        let mut reply = [
            0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
            0xe6, 0xf0, 0x37, 0x60, 0x2a, 0x7a, 0xd6, 0x2d, //
        ];
        reply[..4].copy_from_slice(&buf.bytes()[..4]);
        server_end.write(&reply[..], &mut []).expect("failed to write reply");
    });

    client.strict_two_way().await.expect("failed to send");

    t.join().unwrap_or_resume_unwind();
}

#[fasync::run_singlethreaded(test)]
async fn two_way_flexible_async_send() {
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
        assert_eq!(
            &buf.bytes()[4..],
            &[
                0x02, 0x00, 0x80, 0x01, //
                0x34, 0xa0, 0xd0, 0x6d, 0x01, 0xb8, 0x0a, 0x1a, //
            ]
        );

        // Send a reply so we can assert that the client doesn't fail.
        let mut reply = [
            0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
            0x34, 0xa0, 0xd0, 0x6d, 0x01, 0xb8, 0x0a, 0x1a, //
        ];
        reply[..4].copy_from_slice(&buf.bytes()[..4]);
        server_end.write(&reply[..], &mut []).expect("failed to write reply");
    });

    client.flexible_two_way().await.expect("failed to send");

    t.join().unwrap_or_resume_unwind();
}

#[fasync::run_singlethreaded(test)]
async fn send_strict_event() {
    let (client_end, server_end) = Channel::create().unwrap();
    let server_end = AsyncChannel::from_channel(server_end).unwrap();
    let server =
        UnknownInteractionsProtocolRequestStream::from_channel(server_end).control_handle();

    let t = std::thread::spawn(move || {
        let mut buf = MessageBuf::new();

        client_end
            .wait_handle(
                Signals::CHANNEL_READABLE | Signals::CHANNEL_PEER_CLOSED,
                Time::after(5.seconds()),
            )
            .expect("failed to wait for channel readable");
        client_end.read(&mut buf).expect("failed to read");

        assert_eq!(buf.n_handles(), 0);
        assert_eq!(
            buf.bytes(),
            &[
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
                0xb0, 0x09, 0x80, 0xe1, 0x4a, 0x9d, 0xfd, 0x6a, //
            ]
        );
    });

    server.send_strict_event().expect("failed to send event");

    t.join().unwrap_or_resume_unwind();
}

#[fasync::run_singlethreaded(test)]
async fn send_flexible_event() {
    let (client_end, server_end) = Channel::create().unwrap();
    let server_end = AsyncChannel::from_channel(server_end).unwrap();
    let server =
        UnknownInteractionsProtocolRequestStream::from_channel(server_end).control_handle();

    let t = std::thread::spawn(move || {
        let mut buf = MessageBuf::new();

        client_end
            .wait_handle(
                Signals::CHANNEL_READABLE | Signals::CHANNEL_PEER_CLOSED,
                Time::after(5.seconds()),
            )
            .expect("failed to wait for channel readable");
        client_end.read(&mut buf).expect("failed to read");

        assert_eq!(buf.n_handles(), 0);
        assert_eq!(
            buf.bytes(),
            &[
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                0xfd, 0xe7, 0xdc, 0xf1, 0x33, 0xd8, 0x28, 0x53, //
            ]
        );
    });

    server.send_flexible_event().expect("failed to send event");

    t.join().unwrap_or_resume_unwind();
}

#[fasync::run_singlethreaded(test)]
async fn strict_two_way_response() {
    let (client_end, server_end) = Channel::create().unwrap();
    let server_end = AsyncChannel::from_channel(server_end).unwrap();
    let mut server = UnknownInteractionsProtocolRequestStream::from_channel(server_end);

    let t = std::thread::spawn(move || {
        client_end
            .write(
                &[
                    0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
                    0xe6, 0xf0, 0x37, 0x60, 0x2a, 0x7a, 0xd6, 0x2d, //
                ],
                &mut [],
            )
            .expect("failed to write request");

        let mut buf = MessageBuf::new();

        client_end
            .wait_handle(
                Signals::CHANNEL_READABLE | Signals::CHANNEL_PEER_CLOSED,
                Time::after(5.seconds()),
            )
            .expect("failed to wait for channel readable");
        client_end.read(&mut buf).expect("failed to read");

        assert_eq!(buf.n_handles(), 0);
        assert_eq!(
            buf.bytes(),
            &[
                0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
                0xe6, 0xf0, 0x37, 0x60, 0x2a, 0x7a, 0xd6, 0x2d, //
            ],
        );
    });

    let request = server.next().await.expect("stream ended").expect("failed to read request");
    let responder = assert_matches!(request, UnknownInteractionsProtocolRequest::StrictTwoWay { responder } => responder);

    responder.send().expect("failed to send response");

    t.join().unwrap_or_resume_unwind();
}

#[fasync::run_singlethreaded(test)]
async fn flexible_two_way_response() {
    let (client_end, server_end) = Channel::create().unwrap();
    let server_end = AsyncChannel::from_channel(server_end).unwrap();
    let mut server = UnknownInteractionsProtocolRequestStream::from_channel(server_end);

    let t = std::thread::spawn(move || {
        client_end
            .write(
                &[
                    0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                    0x34, 0xa0, 0xd0, 0x6d, 0x01, 0xb8, 0x0a, 0x1a, //
                ],
                &mut [],
            )
            .expect("failed to write request");

        let mut buf = MessageBuf::new();

        client_end
            .wait_handle(
                Signals::CHANNEL_READABLE | Signals::CHANNEL_PEER_CLOSED,
                Time::after(5.seconds()),
            )
            .expect("failed to wait for channel readable");
        client_end.read(&mut buf).expect("failed to read");

        assert_eq!(buf.n_handles(), 0);
        assert_eq!(
            buf.bytes(),
            &[
                0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                0x34, 0xa0, 0xd0, 0x6d, 0x01, 0xb8, 0x0a, 0x1a, //
            ],
        );
    });

    let request = server.next().await.expect("stream ended").expect("failed to read request");
    let responder = assert_matches!(request, UnknownInteractionsProtocolRequest::FlexibleTwoWay { responder } => responder);

    responder.send().expect("failed to send response");

    t.join().unwrap_or_resume_unwind();
}
