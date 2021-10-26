// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Fuchsia netdevice client tun test
use fidl::endpoints;
use fidl_fuchsia_hardware_network as netdev;
use fidl_fuchsia_net_tun as tun;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_protocol;
use fuchsia_zircon as zx;
use futures::{
    future::{Future, FutureExt as _},
    stream::TryStreamExt as _,
};
use matches::assert_matches;
use netdevice_client::{Client, Error, Session};
use std::{
    convert::TryInto as _,
    io::{Read as _, Write as _},
};

const DEFAULT_PORT_ID: u8 = 0;
const DEFAULT_MTU: u32 = 1500;
const DATA_BYTE: u8 = 42;
const DATA_LEN: usize = 4;
const SESSION_BUFFER_LEN: usize = 2048;

#[fasync::run_singlethreaded(test)]
async fn test_rx() {
    let (tun, _port) = create_tun_device_and_port();
    let client = create_netdev_client(&tun);
    let () = with_netdev_session(client, "test_rx", |session, _client| async move {
        let frame = tun::Frame {
            frame_type: Some(netdev::FrameType::Ethernet),
            data: Some(vec![DATA_BYTE; DATA_LEN]),
            port: Some(DEFAULT_PORT_ID),
            ..tun::Frame::EMPTY
        };
        let () = tun.write_frame(frame).await.unwrap().expect("failed to write frame");
        let buff = session.recv().await.expect("failed to recv buffer");
        let mut bytes = [0u8; DATA_LEN];
        assert_eq!(
            buff.read_at(0, &mut bytes[..]).expect("failed to read from the buffer"),
            DATA_LEN
        );
        for i in bytes.iter() {
            assert_eq!(*i, DATA_BYTE);
        }
    })
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn test_tx() {
    let (tun, _port) = create_tun_device_and_port();
    let client = create_netdev_client(&tun);
    let () = with_netdev_session(client, "test_tx", |session, _client| async move {
        let mut buffer =
            session.alloc_tx_buffer(DATA_LEN).await.expect("failed to alloc tx buffer");
        assert_eq!(
            buffer.write(&[DATA_BYTE; DATA_LEN][..]).expect("failed to write into the buffer"),
            DATA_LEN
        );
        buffer.set_frame_type(netdev::FrameType::Ethernet);
        session.send(buffer).await.expect("failed to send the buffer");
        let frame = tun
            .read_frame()
            .await
            .unwrap()
            .map_err(zx::Status::from_raw)
            .expect("failed to read frame from the tun device");
        assert_eq!(frame.data, Some(vec![DATA_BYTE; DATA_LEN]));
    })
    .await;
}

// Receives buffer from session and echoes back. It copies the content from
// half of the buffers, round robin on index.
async fn echo(session: Session, frame_count: u32) {
    for i in 0..frame_count {
        let mut buffer = session.recv().await.expect("failed to recv from session");
        assert_eq!(buffer.cap(), DATA_LEN);
        let mut bytes = [0u8; DATA_LEN];
        assert_eq!(buffer.read(&mut bytes[..]).unwrap(), DATA_LEN);
        assert_eq!(u32::from_le_bytes(bytes), i);
        if i % 2 == 0 {
            let buffer = buffer.into_tx().await;
            session
                .send(buffer)
                .await
                .expect("failed to send the buffer back on the zero-copy path");
        } else {
            let mut buffer =
                session.alloc_tx_buffer(DATA_LEN).await.expect("no tx buffer available");
            buffer.set_frame_type(netdev::FrameType::Ethernet);
            assert_eq!(buffer.write(&bytes).unwrap(), DATA_LEN);
            session.send(buffer).await.expect("failed to send the buffer back on the copying path");
        }
    }
}

#[fasync::run_singlethreaded(test)]
async fn test_echo_tun() {
    const FRAME_TOTAL_COUNT: u32 = 512;
    let (tun, _port) = create_tun_device_and_port();
    let client = create_netdev_client(&tun);
    with_netdev_session(client, "test_echo_tun", |session, _client| async {
        let echo_fut = echo(session, FRAME_TOTAL_COUNT);
        let main_fut = async move {
            for i in 0..FRAME_TOTAL_COUNT {
                let frame = tun::Frame {
                    frame_type: Some(netdev::FrameType::Ethernet),
                    data: Some(Vec::from(i.to_le_bytes())),
                    port: Some(DEFAULT_PORT_ID),
                    ..tun::Frame::EMPTY
                };
                let () = tun
                    .write_frame(frame)
                    .await
                    .unwrap()
                    .map_err(zx::Status::from_raw)
                    .expect("cannot write frame");
                let frame = tun
                    .read_frame()
                    .await
                    .unwrap()
                    .map_err(zx::Status::from_raw)
                    .expect("failed to read frame");
                let data = frame.data.unwrap();
                assert_eq!(data.len(), DATA_LEN);
                let bytes: [u8; DATA_LEN] = data.try_into().unwrap();
                assert_eq!(u32::from_le_bytes(bytes), i);
            }
        };
        let ((), ()) = futures::join!(echo_fut, main_fut);
    })
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn test_echo_pair() {
    const FRAME_TOTAL_COUNT: u32 = 512;
    let pair = create_tun_device_pair();
    let (client1, client2) = create_netdev_client_pair(&pair).await;
    let () = with_netdev_session(client1, "test_echo_pair_1", |session1, client1| async move {
        let () = with_netdev_session(client2, "test_echo_pair_2", |session2, client2| async move {
            // Wait for the ports to be online before we send anything.
            assert_matches!(
                client1.wait_online(DEFAULT_PORT_ID.into()).await,
                Ok(netdevice_client::PortStatus {
                    flags: netdev::StatusFlags::Online,
                    mtu: DEFAULT_MTU
                })
            );
            assert_matches!(
                client2.wait_online(DEFAULT_PORT_ID.into()).await,
                Ok(netdevice_client::PortStatus {
                    flags: netdev::StatusFlags::Online,
                    mtu: DEFAULT_MTU
                })
            );
            let echo_fut = echo(session1, FRAME_TOTAL_COUNT);
            let main_fut = async {
                for i in 0..FRAME_TOTAL_COUNT {
                    let mut buffer = session2
                        .alloc_tx_buffer(DATA_LEN)
                        .await
                        .expect("failed to alloc tx buffer");
                    buffer.set_frame_type(netdev::FrameType::Ethernet);
                    let mut bytes = i.to_le_bytes();
                    assert_eq!(
                        buffer.write(&bytes[..]).expect("failed to write into the buffer"),
                        DATA_LEN
                    );
                    session2.send(buffer).await.expect("failed to send the buffer");

                    let mut buffer =
                        session2.recv().await.expect("failed to recv from the session");
                    assert_eq!(
                        buffer.read(&mut bytes[..]).expect("failed to read from the buffer"),
                        DATA_LEN
                    );
                    assert_eq!(u32::from_le_bytes(bytes), i);
                }
            };
            futures::join!(echo_fut, main_fut);
        })
        .await;
    })
    .await;
}

#[test]
fn test_session_task_dropped() {
    let mut executor = fasync::TestExecutor::new().expect("failed to create executor");
    let session = executor.run_singlethreaded(async {
        let (tun, _port) = create_tun_device_and_port();
        let client = create_netdev_client(&tun);
        let (session, _task) = client
            .primary_session("test_session_task_dropped", SESSION_BUFFER_LEN)
            .await
            .expect("failed to create session");
        let () = session
            .attach(DEFAULT_PORT_ID.into(), vec![netdev::FrameType::Ethernet])
            .await
            .expect("failed to attach session");
        session
    });
    executor.run_singlethreaded(async move {
        assert_matches!(
            session
                .alloc_tx_buffer(DATA_LEN)
                .await
                .expect_err("alloc_tx_buffer should fail after the session is detached"),
            Error::NoProgress
        );
        assert_matches!(
            session.recv().await.expect_err("recv should fail after the session is detached"),
            Error::NoProgress
        );
    })
}

#[fasync::run_singlethreaded(test)]
async fn test_status_stream() {
    const TOGGLE_COUNT: usize = 3;
    let (tun, port) = create_tun_device_and_port();
    let client = create_netdev_client(&tun);
    let mut watcher = client
        .port_status_stream(DEFAULT_PORT_ID.into())
        .expect("failed to create a status watcher");

    for _ in 0..TOGGLE_COUNT {
        assert_matches!(
            watcher.try_next().await,
            Ok(Some(netdevice_client::PortStatus {
                flags: netdev::StatusFlags::Online,
                mtu: DEFAULT_MTU
            }))
        );
        port.set_online(false).await.expect("failed to flip online flag");
        assert_eq!(
            watcher.try_next().await.expect("failed to get next status update"),
            Some(netdevice_client::PortStatus {
                flags: netdev::StatusFlags::empty(),
                mtu: DEFAULT_MTU
            })
        );
        port.set_online(true).await.expect("failed to flip online flag");
    }
}

#[fasync::run_singlethreaded(test)]
async fn test_port_stream() {
    let mut stream = {
        let (tun, _port) = create_tun_device_and_port();
        let client = create_netdev_client(&tun);
        let mut stream = client.device_port_event_stream().expect("failed to create port stream");
        assert_matches!(
            stream.try_next().await.expect("failed to get next event"),
            Some(netdev::DevicePortEvent::Existing(DEFAULT_PORT_ID))
        );
        assert_matches!(
            stream.try_next().await.expect("failed to get next event"),
            Some(netdev::DevicePortEvent::Idle(netdev::Empty { .. }))
        );
        stream
    };
    assert_matches!(
        stream.try_next().await.expect("failed to get next event"),
        Some(netdev::DevicePortEvent::Removed(DEFAULT_PORT_ID))
    );
}

fn default_base_port_config() -> tun::BasePortConfig {
    tun::BasePortConfig {
        id: Some(DEFAULT_PORT_ID),
        mtu: Some(DEFAULT_MTU),
        rx_types: Some(vec![netdev::FrameType::Ethernet]),
        tx_types: Some(vec![netdev::FrameTypeSupport {
            type_: netdev::FrameType::Ethernet,
            features: netdev::FRAME_FEATURES_RAW,
            supported_flags: netdev::TxFlags::empty(),
        }]),
        ..tun::BasePortConfig::EMPTY
    }
}

fn create_tun_device_and_port() -> (tun::DeviceProxy, tun::PortProxy) {
    let ctrl =
        connect_to_protocol::<tun::ControlMarker>().expect("failed to connect to tun.Control");
    let (device, server) = endpoints::create_proxy::<tun::DeviceMarker>()
        .expect("failed to create proxy for tun::Device");
    ctrl.create_device(
        tun::DeviceConfig { blocking: Some(true), ..tun::DeviceConfig::EMPTY },
        server,
    )
    .expect("failed to create device");
    let (port, server) =
        endpoints::create_proxy::<tun::PortMarker>().expect("failed to create proxy for tun::Port");
    device
        .add_port(
            tun::DevicePortConfig {
                base: Some(default_base_port_config()),
                online: Some(true),
                ..tun::DevicePortConfig::EMPTY
            },
            server,
        )
        .expect("failed to add port to device");
    (device, port)
}

fn create_tun_device_pair() -> tun::DevicePairProxy {
    let ctrl =
        connect_to_protocol::<tun::ControlMarker>().expect("failed to connect to tun.Control");
    let (pair, server) = endpoints::create_proxy::<tun::DevicePairMarker>()
        .expect("failed to create proxy for tun::DevicePair");
    ctrl.create_pair(tun::DevicePairConfig::EMPTY, server).expect("create device pair");
    pair
}

fn create_netdev_client_and_server() -> (Client, endpoints::ServerEnd<netdev::DeviceMarker>) {
    let (device, server) =
        endpoints::create_proxy::<netdev::DeviceMarker>().expect("failed to create proxy");
    (Client::new(device), server)
}

fn create_netdev_client(tun: &tun::DeviceProxy) -> Client {
    let (client, server) = create_netdev_client_and_server();
    tun.get_device(server).expect("failed to connect device to tun");
    client
}

async fn create_netdev_client_pair(pair: &tun::DevicePairProxy) -> (Client, Client) {
    let (device1, left) = endpoints::create_proxy::<netdev::DeviceMarker>()
        .expect("failed to create left device proxy");
    let (device2, right) = endpoints::create_proxy::<netdev::DeviceMarker>()
        .expect("failed to create right device proxy");
    pair.get_left(left).expect("failed to connect left");
    pair.get_right(right).expect("failed to connect right");
    pair.add_port(tun::DevicePairPortConfig {
        base: Some(default_base_port_config()),
        ..tun::DevicePairPortConfig::EMPTY
    })
    .await
    .unwrap()
    .expect("failed to create the default logical port");
    (Client::new(device1), Client::new(device2))
}

async fn with_netdev_session<F, Fut>(client: Client, name: &str, f: F)
where
    F: FnOnce(Session, Client) -> Fut,
    Fut: Future<Output = ()>,
{
    let (session, task) =
        client.primary_session(name, SESSION_BUFFER_LEN).await.expect("failed to create session");
    let () = session
        .attach(DEFAULT_PORT_ID.into(), vec![netdev::FrameType::Ethernet])
        .await
        .expect("failed to attach session");
    futures::select! {
        () = f(session, client).fuse() => {},
        res = task.fuse() => panic!("the background task for session terminated with {:?}", res),
    }
}
