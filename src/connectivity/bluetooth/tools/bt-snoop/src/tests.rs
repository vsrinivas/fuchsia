// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::{endpoints::RequestStream, Error as FidlError},
    fidl_fuchsia_bluetooth_snoop::{
        PacketType, SnoopMarker, SnoopPacket, SnoopProxy, SnoopRequestStream, Timestamp,
    },
    fuchsia_async::{Channel, Executor},
    fuchsia_inspect::{assert_inspect_tree, Inspector},
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
    Inspector,
) {
    let inspect = Inspector::new();
    (
        fasync::Executor::new().unwrap(),
        ConcurrentSnooperPacketFutures::new(),
        PacketLogs::new(
            10,
            10,
            10,
            Duration::new(10, 0),
            inspect.root().create_child("packet_log"),
        ),
        SubscriptionManager::new(),
        ConcurrentClientRequestFutures::new(),
        inspect,
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
    let (_exec, _snoopers, _logs, _subscribers, mut requests, _inspect) = setup();
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
fn test_snoop_default_command_line_args() {
    let args = Args::from_args(&["bt-snoop.cmx"], &[]).expect("Args created from empty args");
    assert_eq!(args.log_size_soft_kib, 32);
    assert_eq!(args.log_size_hard_kib, 256);
    assert_eq!(args.log_time_seconds, 60);
    assert_eq!(args.max_device_count, 8);
    assert_eq!(args.truncate_payload, None);
    assert_eq!(args.verbosity, 0);
}

#[test]
fn test_snoop_command_line_args() {
    let log_size_kib = 1;
    let log_time_seconds = 2;
    let max_device_count = 3;
    let truncate_payload = 4;
    let verbosity = 2;
    let raw_args = &[
        "--log-size-soft-kib",
        &log_size_kib.to_string(),
        "--log-size-hard-kib",
        &log_size_kib.to_string(),
        "--log-time-seconds",
        &log_time_seconds.to_string(),
        "--max-device-count",
        &max_device_count.to_string(),
        "--truncate-payload",
        &truncate_payload.to_string(),
        "-v",
        "-v",
    ];
    let args = Args::from_args(&["bt-snoop.cmx"], raw_args).expect("Args created from args");
    assert_eq!(args.log_size_soft_kib, log_size_kib);
    assert_eq!(args.log_size_hard_kib, log_size_kib);
    assert_eq!(args.log_time_seconds, log_time_seconds);
    assert_eq!(args.max_device_count, max_device_count);
    assert_eq!(args.truncate_payload, Some(truncate_payload));
    assert_eq!(args.verbosity, verbosity);
}

#[test]
fn test_packet_logs_inspect() {
    // This is a test that basic inspect data is plumbed through from the inspect root.
    // More comprehensive testing of possible permutations of packet log inspect data
    // is found in bounded_queue.rs
    let inspect = Inspector::new();
    let runtime_metrics_node = inspect.root().create_child("runtime_metrics");
    let mut packet_logs =
        PacketLogs::new(2, 256, 256, Duration::from_secs(60), runtime_metrics_node);

    assert_inspect_tree!(inspect, root: {
        runtime_metrics: {
            logging_active_for_devices: "",
        }
    });

    let id_1 = String::from("001");

    packet_logs.add_device(id_1.clone());

    assert_inspect_tree!(inspect, root: {
        runtime_metrics: {
            logging_active_for_devices: "\"001\"",
            device_001: {
                size_in_bytes: 0u64,
                number_of_items: 0u64,
            },
        }
    });

    let packet = SnoopPacket {
        is_received: false,
        type_: PacketType::Data,
        timestamp: Timestamp { subsec_nanos: 0, seconds: 123 },
        original_len: 3,
        payload: vec![3, 2, 1],
    };

    packet_logs.log_packet(&id_1, packet);

    assert_inspect_tree!(inspect, root: {
        runtime_metrics: {
            logging_active_for_devices: "\"001\"",
            device_001: {
                size_in_bytes: 75u64,
                number_of_items: 1u64,
                "0": vec![0u8, 0, 0, 123, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 8, 0, 0, 0, 0, 2, 3, 2, 1],
            },
        }
    });

    drop(packet_logs);
}

#[test]
fn test_snoop_config_inspect() {
    let args = Args {
        log_size_soft_kib: 1,
        log_size_hard_kib: 1,
        log_time_seconds: 2,
        max_device_count: 3,
        truncate_payload: Some(4),
        verbosity: 5,
    };
    let inspect = Inspector::new();
    let snoop_config_node = inspect.root().create_child("configuration");
    let config = SnoopConfig::from_args(args, snoop_config_node);
    assert_inspect_tree!(inspect, root: {
        configuration: {
            log_size_soft_max_bytes: 1024u64,
            log_size_hard_max_bytes: "1024",
            log_time: 2u64,
            max_device_count: 3u64,
            truncate_payload: "4 bytes",
            hci_dir: HCI_DEVICE_CLASS_PATH,
        }
    });
    drop(config);
}

#[test]
fn test_handle_client_request() {
    let (mut exec, mut _snoopers, mut logs, mut subscribers, mut requests, _inspect) = setup();

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
    let (_exec, mut _snoopers, mut logs, mut subscribers, mut requests, _inspect) = setup();

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
