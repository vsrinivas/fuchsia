// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fdio,
    files_async::readdir,
    fuchsia_async, fuchsia_zircon as zx,
    futures::{poll, StreamExt},
    std::path::Path,
    wlan_common::{
        appendable::Appendable, big_endian::BigEndianU16, buffer_reader::BufferReader, mac,
    },
    wlan_dev::IsolatedDeviceEnv,
};

const ETH_BUF_FRAME_COUNT: u64 = 256;

/// loop through all ethernet interface and return the one with matching MAC addresss.
/// Returns Ok(None) if no matching interface is found,
/// Returns Err(e) if there is an error.
pub async fn create_eth_client(mac: &[u8; 6]) -> Result<Option<ethernet::Client>, anyhow::Error> {
    const ETH_PATH: &str = "class/ethernet";
    let eth_dir = IsolatedDeviceEnv::open_dir(ETH_PATH).expect("opening ethernet dir");
    let directory_proxy = fidl_fuchsia_io::DirectoryProxy::new(
        fuchsia_async::Channel::from_channel(fdio::clone_channel(&eth_dir)?)?,
    );
    let files = readdir(&directory_proxy).await?;
    for file in files {
        let vmo = zx::Vmo::create(ETH_BUF_FRAME_COUNT * ethernet::DEFAULT_BUFFER_SIZE as u64)?;

        let path = Path::new(ETH_PATH).join(file.name);
        let dev = IsolatedDeviceEnv::open_file(path)?;
        if let Ok(client) =
            ethernet::Client::from_file(dev, vmo, ethernet::DEFAULT_BUFFER_SIZE, "wlan-hw-sim")
                .await
        {
            if let Ok(info) = client.info().await {
                if &info.mac.octets == mac {
                    println!("ethernet client created: {:?}", client);
                    client.start().await.expect("error starting ethernet device");
                    // must call get_status() after start() to clear zx::Signals::USER_0 otherwise
                    // there will be a stream of infinite StatusChanged events that blocks
                    // fasync::Interval
                    println!(
                        "info: {:?} status: {:?}",
                        client.info().await.expect("calling client.info()"),
                        client.get_status().await.expect("getting client status()")
                    );
                    let mut stream = client.get_stream();
                    // The rust Ethernet client makes one single rx buffer available
                    // to the Ethernet driver on every poll.
                    // Without this loop, the Ethernet driver may complain about no RX buffer
                    // available and drop the packet, leaving our Ethernet client sitting forever.
                    for _ in 0..ETH_BUF_FRAME_COUNT {
                        let _ = poll!(stream.next());
                    }
                    return Ok(Some(client));
                }
            }
        }
    }
    Ok(None)
}

pub fn write_fake_eth_frame<B: Appendable>(da: [u8; 6], sa: [u8; 6], payload: &[u8], buf: &mut B) {
    buf.append_value(&mac::EthernetIIHdr {
        da,
        sa,
        ether_type: BigEndianU16::from_native(mac::ETHER_TYPE_IPV4),
    })
    .expect("error creating fake ethernet header");
    buf.append_bytes(payload).expect("buffer too small for ethernet payload");
}

pub async fn send_fake_eth_frame(
    da: [u8; 6],
    sa: [u8; 6],
    payload: &[u8],
    eth: &mut ethernet::Client,
) {
    let mut buf = Vec::<u8>::new();
    write_fake_eth_frame(da, sa, payload, &mut buf);
    eth.send(&buf);
}

/// Block until the first ethernet frame is received from the ethernet client.
/// Block forever if no frame is received. Caller needs to consider timeout if desired.
pub async fn get_next_frame(client: &mut ethernet::Client) -> (mac::EthernetIIHdr, Vec<u8>) {
    let mut client_stream = client.get_stream();
    loop {
        let event = client_stream
            .next()
            .await
            .expect("receiving ethernet event")
            .expect("ethernet client_stream ended unexpectedly");
        match event {
            ethernet::Event::StatusChanged => {
                client.get_status().await.expect("getting status");
            }
            ethernet::Event::Receive(rx_buffer, flags) => {
                assert!(flags.intersects(ethernet::EthernetQueueFlags::RX_OK), "RX_OK not set");
                let mut buf = vec![0; rx_buffer.len()];
                rx_buffer.read(&mut buf);
                let mut buf_reader = BufferReader::new(&buf[..]);
                let header = buf_reader
                    .read::<mac::EthernetIIHdr>()
                    .expect("bytes received too short for ethernet header");
                let payload = buf_reader.into_remaining().to_vec();
                return (*header, payload);
            }
        }
    }
}
