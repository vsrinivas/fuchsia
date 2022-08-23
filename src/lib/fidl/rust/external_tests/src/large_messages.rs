// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This file tests the public APIs of FIDL large message support

use {
    fidl::{endpoints::ServerEnd, Channel},
    fidl_fidl_rust_test_external::{
        LargeMessageTable, LargeMessageUnion, LargeMessageUnionUnknown, OverflowingProtocolEvent,
        OverflowingProtocolMarker, OverflowingProtocolProxy, OverflowingProtocolRequest,
        OverflowingProtocolSynchronousProxy, OverflowingProtocolTwoWayRequestOnlyResponse,
        OverflowingProtocolTwoWayResponseOnlyRequest,
    },
    fuchsia_async as fasync,
    fuchsia_zircon::{Duration, Time},
    futures::StreamExt,
    std::future::Future,
};

/// A very big string that is definitely larger that 64KiB. Including it in a payload will
/// necessarily make that payload into a large message.
const LARGE_STR: &'static str = include_str!("./large_string.txt");

/// A tiny string that ensures that any layout containing only it as a member will be smaller than
/// 64KiB, thereby guaranteeing that any payload using that layout is not a large message.
const SMALL_STR: &'static str = "I'm a very small string.";

/// The time to wait for the triggered event on sync clients
const WAIT_FOR_EVENT: Duration = Duration::from_seconds(1);

fn server_runner(expected_str: &'static str, server_end: Channel) {
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
            Ok(OverflowingProtocolRequest::TwoWayBothRequestAndResponse { str, responder }) => {
                assert_eq!(&str, expected_str);

                responder.send(&mut expected_str.to_string().as_str()).unwrap();
            }
            Ok(OverflowingProtocolRequest::OneWayCall { payload, control_handle }) => {
                assert_eq!(&payload.str.unwrap().as_str(), &expected_str);

                control_handle
                    .send_on_one_way_reply_event(&mut LargeMessageUnion::Str(
                        expected_str.to_string(),
                    ))
                    .unwrap();
            }
            Err(_) => panic!("unexpected err"),
        }
    })
}

#[track_caller]
fn run_client_sync<C, R>(expected_str: &'static str, client_runner: C)
where
    C: 'static + FnOnce(OverflowingProtocolSynchronousProxy) -> R + Send,
    R: 'static + Send,
{
    let (client_end, server_end) = Channel::create().unwrap();
    let client = OverflowingProtocolSynchronousProxy::new(client_end);

    let s = std::thread::spawn(move || server_runner(expected_str, server_end));
    client_runner(client);
    s.join().unwrap();
}

#[track_caller]
async fn run_client_async<C, F>(expected_str: &'static str, client_runner: C)
where
    C: 'static + FnOnce(OverflowingProtocolProxy) -> F + Send,
    F: Future<Output = ()> + 'static + Send,
{
    let (client_end, server_end) = Channel::create().unwrap();
    let client_end = fasync::Channel::from_channel(client_end).unwrap();
    let client = OverflowingProtocolProxy::new(client_end);

    let s = std::thread::spawn(move || server_runner(expected_str, server_end));
    client_runner(client).await;
    s.join().unwrap();
}

#[test]
fn overflowing_two_way_request_only_large_sync() {
    run_client_sync(LARGE_STR, |client| {
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
    run_client_sync(SMALL_STR, |client| {
        client
            .two_way_request_only(
                &mut LargeMessageUnion::Str(SMALL_STR.to_string()),
                Time::INFINITE,
            )
            .unwrap();
    })
}

#[fasync::run_singlethreaded(test)]
async fn overflowing_two_way_request_only_large_async() {
    run_client_async(LARGE_STR, |client| async move {
        client
            .two_way_request_only(&mut LargeMessageUnion::Str(LARGE_STR.to_string()))
            .await
            .unwrap();
    })
    .await
}

#[fasync::run_singlethreaded(test)]
async fn overflowing_two_way_request_only_small_async() {
    run_client_async(SMALL_STR, |client| async move {
        client
            .two_way_request_only(&mut LargeMessageUnion::Str(SMALL_STR.to_string()))
            .await
            .unwrap();
    })
    .await
}

#[test]
fn overflowing_two_way_response_only_large_sync() {
    run_client_sync(LARGE_STR, |client| {
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
    run_client_sync(SMALL_STR, |client| {
        let payload = client
            .two_way_response_only(
                OverflowingProtocolTwoWayResponseOnlyRequest::EMPTY,
                Time::INFINITE,
            )
            .unwrap();

        assert_eq!(&payload.str.unwrap(), SMALL_STR);
    })
}

#[fasync::run_singlethreaded(test)]
async fn overflowing_two_way_response_only_large_async() {
    run_client_async(LARGE_STR, |client| async move {
        let payload = client
            .two_way_response_only(OverflowingProtocolTwoWayResponseOnlyRequest::EMPTY)
            .await
            .unwrap();

        assert_eq!(&payload.str.unwrap(), LARGE_STR);
    })
    .await
}

#[fasync::run_singlethreaded(test)]
async fn overflowing_two_way_response_only_small_async() {
    run_client_async(SMALL_STR, |client| async move {
        let payload = client
            .two_way_response_only(OverflowingProtocolTwoWayResponseOnlyRequest::EMPTY)
            .await
            .unwrap();

        assert_eq!(&payload.str.unwrap(), SMALL_STR);
    })
    .await
}

#[test]
fn overflowing_two_way_both_request_and_response_large_sync() {
    run_client_sync(LARGE_STR, |client| {
        let str = client.two_way_both_request_and_response(LARGE_STR, Time::INFINITE).unwrap();

        assert_eq!(&str, LARGE_STR.to_string().as_str());
    })
}

#[test]
fn overflowing_two_way_both_request_and_response_small_sync() {
    run_client_sync(SMALL_STR, |client| {
        let str = client.two_way_both_request_and_response(SMALL_STR, Time::INFINITE).unwrap();

        assert_eq!(&str, SMALL_STR.to_string().as_str());
    })
}

#[fasync::run_singlethreaded(test)]
async fn overflowing_two_way_both_request_and_response_large_async() {
    run_client_async(LARGE_STR, |client| async move {
        let str = client.two_way_both_request_and_response(LARGE_STR).await.unwrap();

        assert_eq!(&str, LARGE_STR.to_string().as_str());
    })
    .await
}

#[fasync::run_singlethreaded(test)]
async fn overflowing_two_way_both_request_and_response_small_async() {
    run_client_async(SMALL_STR, |client| async move {
        let str = client.two_way_both_request_and_response(SMALL_STR).await.unwrap();

        assert_eq!(&str, SMALL_STR.to_string().as_str());
    })
    .await
}

#[test]
fn overflowing_one_way_large_sync() {
    run_client_sync(LARGE_STR, |client| {
        client
            .one_way_call(LargeMessageTable {
                str: Some(LARGE_STR.to_string()),
                ..LargeMessageTable::EMPTY
            })
            .unwrap();
        let event = client
            .wait_for_event(Time::after(WAIT_FOR_EVENT))
            .unwrap()
            .into_on_one_way_reply_event()
            .unwrap();

        match event {
            LargeMessageUnion::Str(str) => {
                assert_eq!(&str, LARGE_STR.to_string().as_str());
            }
            LargeMessageUnionUnknown!() => panic!("unknown event data"),
        }
    })
}

#[test]
fn overflowing_one_way_small_sync() {
    run_client_sync(SMALL_STR, |client| {
        client
            .one_way_call(LargeMessageTable {
                str: Some(SMALL_STR.to_string()),
                ..LargeMessageTable::EMPTY
            })
            .unwrap();
        let event = client
            .wait_for_event(Time::after(WAIT_FOR_EVENT))
            .unwrap()
            .into_on_one_way_reply_event()
            .unwrap();

        match event {
            LargeMessageUnion::Str(str) => {
                assert_eq!(&str, SMALL_STR.to_string().as_str());
            }
            LargeMessageUnionUnknown!() => panic!("unknown event data"),
        }
    })
}

#[fasync::run_singlethreaded(test)]
async fn overflowing_one_way_large_async() {
    run_client_async(LARGE_STR, |client| async move {
        client
            .one_way_call(LargeMessageTable {
                str: Some(LARGE_STR.to_string()),
                ..LargeMessageTable::EMPTY
            })
            .unwrap();
        let OverflowingProtocolEvent::OnOneWayReplyEvent { payload } =
            client.take_event_stream().next().await.unwrap().unwrap();

        match payload {
            LargeMessageUnion::Str(str) => {
                assert_eq!(&str, LARGE_STR.to_string().as_str());
            }
            LargeMessageUnionUnknown!() => panic!("unknown event data"),
        }
    })
    .await
}

#[fasync::run_singlethreaded(test)]
async fn overflowing_one_way_small_async() {
    run_client_async(SMALL_STR, |client| async move {
        client
            .one_way_call(LargeMessageTable {
                str: Some(SMALL_STR.to_string()),
                ..LargeMessageTable::EMPTY
            })
            .unwrap();
        let OverflowingProtocolEvent::OnOneWayReplyEvent { payload } =
            client.take_event_stream().next().await.unwrap().unwrap();

        match payload {
            LargeMessageUnion::Str(str) => {
                assert_eq!(&str, SMALL_STR.to_string().as_str());
            }
            LargeMessageUnionUnknown!() => panic!("unknown event data"),
        }
    })
    .await
}
