// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use ethernet as eth;
use fidl_fuchsia_hardware_ethernet_ext::MacAddress;
use fuchsia_async::TimeoutExt as _;
use fuchsia_zircon as zx;
use futures::{FutureExt as _, StreamExt as _, TryStreamExt as _};
use std::convert::TryInto as _;
use std::fs::{self, File};
use std::str::FromStr;
use structopt::StructOpt;

const ETH_DIRECTORY: &str = "/dev/class/ethernet";
const NETDEV_DIRECTORY: &str = "/dev/class/network";

#[derive(StructOpt, Debug)]
struct Config {
    send_byte: u8,
    receive_byte: u8,
    length: usize,
    mac: String,
}

async fn find_ethernet_device(mac: MacAddress) -> Result<eth::Client, anyhow::Error> {
    let eth_devices = fs::read_dir(ETH_DIRECTORY)?;

    for device in eth_devices {
        let dev = File::open(device?.path().to_str().unwrap())?;
        let vmo = zx::Vmo::create(256 * eth::DEFAULT_BUFFER_SIZE as u64)?;

        let eth_client = eth::Client::from_file(dev, vmo, eth::DEFAULT_BUFFER_SIZE, "test").await?;

        eth_client.start().await?;

        let eth_info = eth_client.info().await?;
        if eth_info.mac == mac {
            return Ok(eth_client);
        }
    }

    return Err(anyhow::format_err!("Could not find {}", mac));
}

async fn find_network_device(
    mac: MacAddress,
) -> (netdevice_client::Client, netdevice_client::Port) {
    let (directory, directory_server) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_io::DirectoryMarker>().expect("create proxy");
    fdio::service_connect(NETDEV_DIRECTORY, directory_server.into_channel().into())
        .expect("connect to netdevice devfs");
    let devices =
        fuchsia_fs::directory::readdir(&directory).await.expect("readdir failed").into_iter().map(
            |file| {
                let filepath = std::path::Path::new(NETDEV_DIRECTORY).join(&file.name);
                let filepath = filepath
                    .to_str()
                    .unwrap_or_else(|| panic!("{} failed to convert to str", filepath.display()));
                let (netdevice, netdevice_server) = fidl::endpoints::create_proxy::<
                    fidl_fuchsia_hardware_network::DeviceInstanceMarker,
                >()
                .expect("create proxy");
                fdio::service_connect(filepath, netdevice_server.into_channel())
                    .expect("connect to service");
                netdevice
            },
        );
    let results = futures::stream::iter(devices).filter_map(|netdev_device| async move {
        let (device_proxy, device_server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_hardware_network::DeviceMarker>()
                .expect("create proxy");
        netdev_device.get_device(device_server).expect("get device");
        let device_proxy = &device_proxy;
        let client = netdevice_client::Client::new(Clone::clone(device_proxy));

        let mut port_id = match client
            .device_port_event_stream()
            .expect("failed to get port event stream")
            .try_next()
            .await
            .expect("error observing ports")
            .expect("port stream ended unexpectedly")
        {
            fidl_fuchsia_hardware_network::DevicePortEvent::Existing(port_id) => port_id,
            e @ fidl_fuchsia_hardware_network::DevicePortEvent::Removed(_)
            | e @ fidl_fuchsia_hardware_network::DevicePortEvent::Idle(_)
            | e @ fidl_fuchsia_hardware_network::DevicePortEvent::Added(_) => {
                unreachable!("unexpected event: {:?}", e);
            }
        };
        let (port, port_server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_hardware_network::PortMarker>()
                .expect("failed to create proxy");
        device_proxy.get_port(&mut port_id, port_server).expect("failed to get port");
        let (mac_addressing, mac_addressing_server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_hardware_network::MacAddressingMarker>()
                .expect("failed to create proxy");
        port.get_mac(mac_addressing_server).expect("failed to get mac addressing");
        let addr = mac_addressing.get_unicast_address().await.expect("failed to get address");

        (addr.octets == mac.octets).then(move || (client, port_id.try_into().expect("bad port id")))
    });
    futures::pin_mut!(results);
    results.next().await.unwrap_or_else(|| panic!("netdevice with mac {} not found", mac))
}

async fn ethernet_device_send(
    mut client: eth::Client,
    config: Config,
) -> Result<(), anyhow::Error> {
    let buf = vec![config.send_byte; config.length];
    client.send(&buf);
    let mut events = client.get_stream();

    // Wait for reply.
    let mut buf = vec![0; config.length];
    while let Some(evt) = events.try_next().await? {
        match evt {
            eth::Event::Receive(rx, flags) => {
                if flags == eth::EthernetQueueFlags::RX_OK && rx.len() == config.length {
                    rx.read(&mut buf);
                    break;
                }
            }
            _ => (),
        }
    }
    assert!(
        buf.iter().all(|b| *b == config.receive_byte),
        "expected entire buffer to be {:x}, found {:x?}",
        config.receive_byte,
        buf,
    );
    Ok(())
}

async fn network_device_send(
    client: netdevice_client::Client,
    port: netdevice_client::Port,
    config: Config,
) {
    let info = client.device_info().await.expect("get device info");
    let (session, task) = client
        .primary_session("test", info.max_buffer_length.unwrap().get() as usize)
        .await
        .expect("open primary session");
    let _task_handle = fuchsia_async::Task::spawn(task.map(|r| r.expect("session task failed")));
    session
        .attach(port, vec![fidl_fuchsia_hardware_network::FrameType::Ethernet])
        .await
        .expect("attach port");

    // It is possible that the device does not yet have rx buffers ready to receive frames, so we
    // loop to ensure that we try sending after the device has its rx buffers ready.
    let recv_buf = loop {
        let mut buffer = session.alloc_tx_buffer(config.length).await.expect("allocate tx buffer");
        buffer.set_frame_type(fidl_fuchsia_hardware_network::FrameType::Ethernet);
        buffer.set_port(port);
        let write_scratch = vec![config.send_byte; config.length];
        let written = buffer.write_at(0, &write_scratch).expect("write message");
        assert_eq!(written, config.length);
        session.send(buffer).expect("send");

        let recv_result = session
            .recv()
            .map(|result| Some(result.expect("recv failed")))
            .on_timeout(fuchsia_async::Time::after(zx::Duration::from_seconds(1)), || None)
            .await;
        match recv_result {
            Some(received_buffer) => {
                break received_buffer;
            }
            None => println!("didn't receive any data, trying to send again..."),
        }
    };

    let mut scratch = vec![0; config.length];
    let read = recv_buf.read_at(0, &mut scratch).expect("read from buffer");
    assert_eq!(read, config.length, "expected length: {}, found: {}", config.length, read);
    assert!(
        scratch.iter().all(|b| *b == config.receive_byte),
        "expected entire buffer to be {:x}, found {:x?}",
        config.receive_byte,
        scratch,
    );
}

#[fuchsia::main(logging_minimum_severity = "debug")]
async fn main() -> Result<(), anyhow::Error> {
    let config = Config::from_args();
    match find_ethernet_device(MacAddress::from_str(&config.mac)?).await {
        Ok(client) => {
            ethernet_device_send(client, config).await.expect("failed to send on ethernet device");
        }
        Err(_) => {
            let (client, port) = find_network_device(MacAddress::from_str(&config.mac)?).await;
            network_device_send(client, port, config).await;
        }
    }
    // Test output requires this print, do not remove.
    println!("PASS");
    Ok(())
}
