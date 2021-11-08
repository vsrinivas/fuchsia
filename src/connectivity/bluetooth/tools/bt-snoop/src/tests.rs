// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        packet_logs::{append_pcap, write_pcap_header},
        *,
    },
    async_utils::PollExt,
    fidl::{endpoints::RequestStream, Error as FidlError},
    fidl_fuchsia_bluetooth_snoop::{PacketType, SnoopMarker, SnoopProxy, SnoopRequestStream},
    fuchsia_async::{Channel, TestExecutor},
    fuchsia_inspect::{assert_data_tree, Inspector},
    fuchsia_zircon as zx,
    futures::pin_mut,
    std::task::Poll,
};

fn setup() -> (
    TestExecutor,
    ConcurrentSnooperPacketFutures,
    PacketLogs,
    SubscriptionManager,
    ConcurrentClientRequestFutures,
    Inspector,
) {
    let inspect = Inspector::new();
    (
        TestExecutor::new().unwrap(),
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

#[fasync::run_until_stalled(test)]
async fn test_packet_logs_inspect() {
    // This is a test that basic inspect data is plumbed through from the inspect root.
    // More comprehensive testing of possible permutations of packet log inspect data
    // is found in bounded_queue.rs
    let inspect = Inspector::new();
    let runtime_metrics_node = inspect.root().create_child("runtime_metrics");
    let mut packet_logs =
        PacketLogs::new(2, 256, 256, Duration::from_secs(60), runtime_metrics_node);

    assert_data_tree!(inspect, root: {
        runtime_metrics: {
            logging_active_for_devices: "",
        }
    });

    let id_1 = String::from("001");

    assert!(packet_logs.add_device(id_1.clone()).is_none(), "shouldn't have evicted a log");

    let mut expected_data = vec![];
    write_pcap_header(&mut expected_data).expect("write to succeed");

    assert_data_tree!(inspect, root: {
        runtime_metrics: {
            logging_active_for_devices: "\"001\"",
            device_0: {
                hci_device_name: "001",
                byte_len: 0u64,
                number_of_items: 0u64,
                data: expected_data,
            },
        }
    });

    let ts = zx::Time::from_nanos(123 * 1_000_000_000);
    let packet = snooper::SnoopPacket::new(false, PacketType::Data, ts, vec![3, 2, 1]);

    // write pcap header and packet data to expected_data buffer
    let mut expected_data = vec![];
    write_pcap_header(&mut expected_data).expect("write to succeed");
    append_pcap(&mut expected_data, &packet, None).expect("write to succeed");

    packet_logs.log_packet(&id_1, packet).await;

    assert_data_tree!(inspect, root: {
        runtime_metrics: {
            logging_active_for_devices: "\"001\"",
            device_0: {
                hci_device_name: "001",
                byte_len: 51u64,
                number_of_items: 1u64,
                data: expected_data,
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
    assert_data_tree!(inspect, root: {
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

// Helper that pumps the request stream to get back a single request, panicking if the stream
// stalls before a request is returned.
fn pump_request_stream(
    exec: &mut TestExecutor,
    mut request_stream: SnoopRequestStream,
    id: ClientId,
) -> ClientRequest {
    let request = unwrap_request(exec.run_until_stalled(&mut request_stream.next()));
    (id, (Some(Ok(request)), request_stream))
}

// Helper that pumps the the handle_client_request until stalled, panicking if the future
// stalls in a pending state or returns an error.
fn pump_handle_client_request(
    exec: &mut TestExecutor,
    request: ClientRequest,
    client_requests: &mut ConcurrentClientRequestFutures,
    subscribers: &mut SubscriptionManager,
    packet_logs: &PacketLogs,
) {
    let handler = handle_client_request(request, client_requests, subscribers, packet_logs);
    pin_mut!(handler);
    exec.run_until_stalled(&mut handler)
        .expect("Handler future to complete")
        .expect("Client channel to accept response");
}

#[test]
fn test_handle_client_request() {
    let (mut exec, mut _snoopers, mut logs, mut subscribers, mut requests, _inspect) = setup();

    // unrecognized device returns an error to the client
    let (proxy, request_stream) = fidl_endpoints();
    let mut client_fut = proxy.start(true, Some(""));
    let _ = exec.run_until_stalled(&mut client_fut);
    let request = pump_request_stream(&mut exec, request_stream, ClientId(0));
    pump_handle_client_request(&mut exec, request, &mut requests, &mut subscribers, &mut logs);
    let response = unwrap_response(exec.run_until_stalled(&mut client_fut));
    assert!(response.error.is_some());
    assert_eq!(subscribers.number_of_subscribers(), 0);

    // valid device returns no errors to a client subscribed to that device
    let (proxy, request_stream) = fidl_endpoints();
    assert!(logs.add_device(String::new()).is_none(), "shouldn't have evicted a device log");
    let mut client_fut = proxy.start(true, Some(""));
    let _ = exec.run_until_stalled(&mut client_fut);
    let request = pump_request_stream(&mut exec, request_stream, ClientId(1));
    pump_handle_client_request(&mut exec, request, &mut requests, &mut subscribers, &mut logs);
    let response = unwrap_response(exec.run_until_stalled(&mut client_fut));
    assert!(response.error.is_none());
    assert_eq!(subscribers.number_of_subscribers(), 1);

    // valid device returns no errors to a client subscribed globally
    let (proxy, request_stream) = fidl_endpoints();
    let mut client_fut = proxy.start(true, None);
    let _ = exec.run_until_stalled(&mut client_fut);
    let request = pump_request_stream(&mut exec, request_stream, ClientId(2));
    pump_handle_client_request(&mut exec, request, &mut requests, &mut subscribers, &mut logs);
    let response = unwrap_response(exec.run_until_stalled(&mut client_fut));
    assert!(response.error.is_none());
    assert_eq!(subscribers.number_of_subscribers(), 2);

    // second request by the same client returns an error
    let (proxy, request_stream) = fidl_endpoints();
    let mut client_fut = proxy.start(true, None);
    let _ = exec.run_until_stalled(&mut client_fut);
    let request = pump_request_stream(&mut exec, request_stream, ClientId(2));
    pump_handle_client_request(&mut exec, request, &mut requests, &mut subscribers, &mut logs);
    let response = unwrap_response(exec.run_until_stalled(&mut client_fut));
    assert!(response.error.is_some());
    assert_eq!(subscribers.number_of_subscribers(), 2);

    // valid device returns no errors to a client requesting a dump
    let (proxy, request_stream) = fidl_endpoints();
    let mut client_fut = proxy.start(false, None);
    let _ = exec.run_until_stalled(&mut client_fut);
    let request = pump_request_stream(&mut exec, request_stream, ClientId(3));
    pump_handle_client_request(&mut exec, request, &mut requests, &mut subscribers, &mut logs);
    let response = unwrap_response(exec.run_until_stalled(&mut client_fut));
    assert!(response.error.is_none());
    assert_eq!(subscribers.number_of_subscribers(), 2);
}

#[test]
fn test_handle_bad_client_request() {
    let (mut exec, mut _snoopers, mut logs, mut subscribers, mut requests, _inspect) = setup();

    let id = ClientId(0);
    let err = Some(Err(FidlError::Invalid));
    let (_proxy, req_stream) = fidl_endpoints();
    let handle = req_stream.control_handle();
    let request = (id, (err, req_stream));
    subscribers.register(id, handle, None, None).unwrap();
    assert!(subscribers.is_registered(&id));
    pump_handle_client_request(&mut exec, request, &mut requests, &mut subscribers, &mut logs);
    assert!(!subscribers.is_registered(&id));

    let id = ClientId(1);
    let err = Some(Err(FidlError::Invalid));
    let (_proxy, req_stream) = fidl_endpoints();
    let handle = req_stream.control_handle();
    let request = (id, (err, req_stream));
    subscribers.register(id, handle, None, None).unwrap();
    assert!(subscribers.is_registered(&id));
    pump_handle_client_request(&mut exec, request, &mut requests, &mut subscribers, &mut logs);
    assert!(!subscribers.is_registered(&id));
}
