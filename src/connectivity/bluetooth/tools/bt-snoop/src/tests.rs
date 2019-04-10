// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::{endpoints::RequestStream, Error as FidlError},
    fidl_fuchsia_bluetooth_snoop::{SnoopMarker, SnoopProxy, SnoopRequestStream},
    fuchsia_async::{Channel, Executor},
    fuchsia_zircon as zx,
    std::task::Poll,
};

use super::*;

fn setup() -> (
    Executor,
    ConcurrentSnooperPacketFutures,
    PacketLogs,
    SubscriptionManager,
    ConcurrentClientRequestFutures,
) {
    (
        fasync::Executor::new().unwrap(),
        ConcurrentSnooperPacketFutures::new(),
        PacketLogs::new(10, 10, Duration::new(10, 0)),
        SubscriptionManager::new(),
        ConcurrentClientRequestFutures::new(),
    )
}

#[test]
fn test_id_generator() {
    let mut id_gen = IdGenerator::new();
    assert_eq!(id_gen.next(), ClientId(0));
    assert_eq!(id_gen.next(), ClientId(1));
}

#[test]
fn test_register_new_client() {
    let (_exec, _snoopers, _logs, _subscribers, mut requests) = setup();
    assert_eq!(requests.len(), 0);

    let (_tx, rx) = zx::Channel::create().unwrap();
    let stream = SnoopRequestStream::from_channel(Channel::from_channel(rx).unwrap());
    register_new_client(stream, &mut requests, ClientId(0));
    assert_eq!(requests.len(), 1);
}

fn fidl_endpoints() -> (SnoopProxy, SnoopRequestStream) {
    let (proxy, server) = fidl::endpoints::create_proxy::<SnoopMarker>().unwrap();
    let request_stream = server.into_stream().unwrap();
    (proxy, request_stream)
}

fn unwrap_request<T, E>(request: Poll<Option<Result<T, E>>>) -> T {
    if let Poll::Ready(Some(Ok(request))) = request {
        return request;
    }
    panic!("Failed to receive request");
}

fn unwrap_response<T, E>(response: Poll<Result<T, E>>) -> T {
    if let Poll::Ready(Ok(response)) = response {
        return response;
    }
    panic!("Failed to receive response");
}

#[test]
fn test_handle_client_request() {
    let (mut exec, mut _snoopers, mut logs, mut subscribers, mut requests) = setup();

    // unrecognized device returns an error to the client
    let (proxy, mut request_stream) = fidl_endpoints();
    let mut client_fut = proxy.start(true, Some(""));
    let _ = exec.run_until_stalled(&mut client_fut);
    let request = unwrap_request(exec.run_until_stalled(&mut request_stream.next()));
    let request = (ClientId(0), (Some(Ok(request)), request_stream));
    handle_client_request(request, &mut requests, &mut subscribers, &mut logs).unwrap();
    let response = unwrap_response(exec.run_until_stalled(&mut client_fut));
    assert!(response.error.is_some());
    assert_eq!(subscribers.number_of_subscribers(), 0);

    // valid device returns no errors to a client subscribed to that device
    let (proxy, mut request_stream) = fidl_endpoints();
    logs.add_device(String::new());
    let mut client_fut = proxy.start(true, Some(""));
    let _ = exec.run_until_stalled(&mut client_fut);
    let request = unwrap_request(exec.run_until_stalled(&mut request_stream.next()));
    let request = (ClientId(1), (Some(Ok(request)), request_stream));
    handle_client_request(request, &mut requests, &mut subscribers, &mut logs).unwrap();
    let response = unwrap_response(exec.run_until_stalled(&mut client_fut));
    assert!(response.error.is_none());
    assert_eq!(subscribers.number_of_subscribers(), 1);

    // valid device returns no errors to a client subscribed globally
    let (proxy, mut request_stream) = fidl_endpoints();
    let mut client_fut = proxy.start(true, None);
    let _ = exec.run_until_stalled(&mut client_fut);
    let request = unwrap_request(exec.run_until_stalled(&mut request_stream.next()));
    let request = (ClientId(2), (Some(Ok(request)), request_stream));
    handle_client_request(request, &mut requests, &mut subscribers, &mut logs).unwrap();
    let response = unwrap_response(exec.run_until_stalled(&mut client_fut));
    println!("{:?}", response.error);
    assert!(response.error.is_none());
    assert_eq!(subscribers.number_of_subscribers(), 2);

    // second request by the same client returns an error
    let (proxy, mut request_stream) = fidl_endpoints();
    let mut client_fut = proxy.start(true, None);
    let _ = exec.run_until_stalled(&mut client_fut);
    let request = unwrap_request(exec.run_until_stalled(&mut request_stream.next()));
    let request = (ClientId(2), (Some(Ok(request)), request_stream));
    handle_client_request(request, &mut requests, &mut subscribers, &mut logs).unwrap();
    let response = unwrap_response(exec.run_until_stalled(&mut client_fut));
    println!("{:?}", response.error);
    assert!(response.error.is_some());
    assert_eq!(subscribers.number_of_subscribers(), 2);

    // valid device returns no errors to a client requesting a dump
    let (proxy, mut request_stream) = fidl_endpoints();
    let mut client_fut = proxy.start(false, None);
    let _ = exec.run_until_stalled(&mut client_fut);
    let request = unwrap_request(exec.run_until_stalled(&mut request_stream.next()));
    let request = (ClientId(3), (Some(Ok(request)), request_stream));
    handle_client_request(request, &mut requests, &mut subscribers, &mut logs).unwrap();
    let response = unwrap_response(exec.run_until_stalled(&mut client_fut));
    println!("{:?}", response.error);
    assert!(response.error.is_none());
    assert_eq!(subscribers.number_of_subscribers(), 2);
}

#[test]
fn test_handle_bad_client_request() {
    let (_exec, mut _snoopers, mut logs, mut subscribers, mut requests) = setup();

    let id = ClientId(0);
    let err = Some(Err(FidlError::Invalid));
    let (_proxy, req_stream) = fidl_endpoints();
    let handle = req_stream.control_handle();
    let request = (id, (err, req_stream));
    subscribers.register(id, handle, None).unwrap();
    assert!(subscribers.is_registered(&id));
    handle_client_request(request, &mut requests, &mut subscribers, &mut logs).unwrap();
    assert!(!subscribers.is_registered(&id));

    let id = ClientId(1);
    let err = Some(Err(FidlError::Invalid));
    let (_proxy, req_stream) = fidl_endpoints();
    let handle = req_stream.control_handle();
    let request = (id, (err, req_stream));
    subscribers.register(id, handle, None).unwrap();
    assert!(subscribers.is_registered(&id));
    handle_client_request(request, &mut requests, &mut subscribers, &mut logs).unwrap();
    assert!(!subscribers.is_registered(&id));
}
