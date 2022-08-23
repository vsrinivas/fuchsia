// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This file tests the public APIs of FIDL large message support

use {
    fidl::{endpoints::ServerEnd, Channel},
    fidl_fidl_rust_test_external::{
        LargeMessageTable, LargeMessageUnion, LargeMessageUnionUnknown, OverflowingProtocolMarker,
        OverflowingProtocolRequest, OverflowingProtocolSynchronousProxy,
        OverflowingProtocolTwoWayRequestOnlyResponse, OverflowingProtocolTwoWayResponseOnlyRequest,
    },
    fuchsia_async as fasync,
    fuchsia_zircon::Time,
    futures::StreamExt,
};

/// A very big string that is definitely larger that 64KiB. Including it in a payload will
/// necessarily make that payload into a large message.
const LARGE_STR: &'static str = include_str!("./large_string.txt");

/// A tiny string that ensures that any layout containing only it as a member will be smaller than
/// 64KiB, thereby guaranteeing that any payload using that layout is not a large message.
const SMALL_STR: &'static str = "I'm a very small string.";

#[track_caller]
fn run_two_way_sync<F, R>(expected_str: &str, client_runner: F)
where
    F: 'static + FnOnce(OverflowingProtocolSynchronousProxy) -> R + Send,
    R: 'static + Send,
{
    let (client_end, server_end) = Channel::create().unwrap();
    let client = OverflowingProtocolSynchronousProxy::new(client_end);

    // Use a scope so that we may borrow |expected_str|.
    std::thread::scope(move |s| {
        // Server thread.
        s.spawn(move || {
            fasync::LocalExecutor::new().unwrap().run_singlethreaded(async move {
                let mut stream =
                    ServerEnd::<OverflowingProtocolMarker>::new(server_end).into_stream().unwrap();
                match stream.next().await.unwrap() {
                    Ok(OverflowingProtocolRequest::TwoWayRequestOnly { payload, responder }) => {
                        match payload {
                            LargeMessageUnion::Str(str) => {
                                assert_eq!(&str, &expected_str);

                                responder
                                    .send(OverflowingProtocolTwoWayRequestOnlyResponse::EMPTY)
                                    .unwrap();
                            }
                            LargeMessageUnionUnknown!() => panic!("unknown data"),
                        }
                    }
                    Ok(OverflowingProtocolRequest::TwoWayResponseOnly { responder, .. }) => {
                        responder
                            .send(LargeMessageTable {
                                str: Some(expected_str.to_string()),
                                ..LargeMessageTable::EMPTY
                            })
                            .unwrap();
                    }
                    Ok(OverflowingProtocolRequest::TwoWayBothRequestAndResponse {
                        str,
                        responder,
                    }) => {
                        assert_eq!(&str, expected_str);

                        responder.send(&mut expected_str.to_string().as_str()).unwrap();
                    }
                    Err(_) => panic!("unexpected err"),
                }
            });
        });

        // Client thread.
        s.spawn(move || client_runner(client));
    });
}

#[test]
fn overflowing_two_way_request_only_large_sync() {
    run_two_way_sync(LARGE_STR, |client| {
        client
            .two_way_request_only(
                &mut LargeMessageUnion::Str(LARGE_STR.to_string()),
                Time::INFINITE,
            )
            .unwrap();
    })
}

#[test]
fn overflowing_two_way_request_only_small_sync() {
    run_two_way_sync(SMALL_STR, |client| {
        client
            .two_way_request_only(
                &mut LargeMessageUnion::Str(SMALL_STR.to_string()),
                Time::INFINITE,
            )
            .unwrap();
    })
}

#[test]
fn overflowing_two_way_response_only_large_sync() {
    run_two_way_sync(LARGE_STR, |client| {
        let payload = client
            .two_way_response_only(
                OverflowingProtocolTwoWayResponseOnlyRequest::EMPTY,
                Time::INFINITE,
            )
            .unwrap();

        assert_eq!(&payload.str.unwrap(), LARGE_STR);
    })
}

#[test]
fn overflowing_two_way_response_only_small_sync() {
    run_two_way_sync(SMALL_STR, |client| {
        let payload = client
            .two_way_response_only(
                OverflowingProtocolTwoWayResponseOnlyRequest::EMPTY,
                Time::INFINITE,
            )
            .unwrap();

        assert_eq!(&payload.str.unwrap(), SMALL_STR);
    })
}

#[test]
fn overflowing_two_way_both_request_and_response_large_sync() {
    run_two_way_sync(LARGE_STR.to_string().as_str(), |client| {
        let str = client.two_way_both_request_and_response(LARGE_STR, Time::INFINITE).unwrap();

        assert_eq!(&str, LARGE_STR);
    })
}

#[test]
fn overflowing_two_way_both_request_and_response_small_sync() {
    run_two_way_sync(SMALL_STR.to_string().as_str(), |client| {
        let str = client.two_way_both_request_and_response(SMALL_STR, Time::INFINITE).unwrap();

        assert_eq!(&str, SMALL_STR);
    })
}
