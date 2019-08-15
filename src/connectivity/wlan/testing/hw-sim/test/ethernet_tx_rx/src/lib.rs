// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use {
    failure::ensure,
    fidl_fuchsia_wlan_service::WlanMarker,
    fidl_fuchsia_wlan_tap::{WlantapPhyEvent, WlantapPhyProxy},
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon::DurationNum,
    futures::StreamExt,
    pin_utils::pin_mut,
    wlan_common::{
        appendable::Appendable, big_endian::BigEndianU16, buffer_reader::BufferReader, mac,
    },
    wlan_hw_sim::*,
};

const BSS: [u8; 6] = [0x65, 0x74, 0x68, 0x6e, 0x65, 0x74];
const SSID: &[u8] = b"ethernet";
const PAYLOAD: &[u8] = &[1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15];

fn handle_eth_tx(event: WlantapPhyEvent, actual: &mut Vec<u8>, phy: &WlantapPhyProxy) {
    if let WlantapPhyEvent::Tx { args } = event {
        if let Some(msdus) = mac::MsduIterator::from_raw_data_frame(&args.packet.data[..], false) {
            for mac::Msdu { dst_addr, src_addr, llc_frame } in msdus {
                if dst_addr == ETH_DST_MAC && src_addr == HW_MAC_ADDR {
                    assert_eq!(llc_frame.hdr.protocol_id.to_native(), mac::ETHER_TYPE_IPV4);
                    actual.clear();
                    actual.extend_from_slice(llc_frame.body);
                    rx_wlan_data_frame(
                        &CHANNEL,
                        &HW_MAC_ADDR,
                        &BSS,
                        &ETH_DST_MAC,
                        &PAYLOAD,
                        mac::ETHER_TYPE_IPV4,
                        phy,
                    )
                    .expect("sending wlan data frame");
                }
            }
        }
    }
}

async fn send_and_receive<'a>(
    client: &'a mut ethernet::Client,
    buf: &'a Vec<u8>,
) -> Result<(mac::EthernetIIHdr, Vec<u8>), failure::Error> {
    let mut client_stream = client.get_stream();
    client.send(&buf);
    loop {
        let event = client_stream.next().await.expect("receiving ethernet event")?;
        match event {
            ethernet::Event::StatusChanged => {
                client.get_status().await.expect("getting status");
            }
            ethernet::Event::Receive(rx_buffer, flags) => {
                ensure!(flags.intersects(ethernet::EthernetQueueFlags::RX_OK), "RX_OK not set");
                let mut buf = vec![0; rx_buffer.len()];
                rx_buffer.read(&mut buf);
                let mut buf_reader = BufferReader::new(&buf[..]);
                let header = buf_reader
                    .read::<mac::EthernetIIHdr>()
                    .expect("bytes received too short for ethernet header");
                let payload = buf_reader.into_remaining().to_vec();
                return Ok((*header, payload));
            }
        }
    }
}

async fn verify_tx_and_rx(client: &mut ethernet::Client, helper: &mut test_utils::TestHelper) {
    let mut buf: Vec<u8> = Vec::new();
    buf.append_value(&mac::EthernetIIHdr {
        da: ETH_DST_MAC,
        sa: HW_MAC_ADDR,
        ether_type: BigEndianU16::from_native(mac::ETHER_TYPE_IPV4),
    })
    .expect("error creating fake ethernet header");
    buf.append_bytes(PAYLOAD).expect("buffer too small for ethernet payload");

    let eth_tx_rx_fut = send_and_receive(client, &buf);
    pin_mut!(eth_tx_rx_fut);

    let phy = helper.proxy();
    let mut actual = Vec::new();
    let (header, payload) = helper
        .run_until_complete_or_timeout(
            5.seconds(),
            "verify ethernet_tx_rx",
            |event| {
                handle_eth_tx(event, &mut actual, &phy);
            },
            eth_tx_rx_fut,
        )
        .await
        .expect("send and receive eth");
    assert_eq!(&actual[..], PAYLOAD);
    assert_eq!(header.da, HW_MAC_ADDR);
    assert_eq!(header.sa, ETH_DST_MAC);
    assert_eq!(header.ether_type.to_native(), mac::ETHER_TYPE_IPV4);
    assert_eq!(&payload[..], PAYLOAD);
}

/// Test an ethernet device backed by WLAN device and send and receive data frames by verifying
/// frames are delivered without any change in both directions.
#[fuchsia_async::run_singlethreaded(test)]
async fn ethernet_tx_rx() {
    let mut helper =
        test_utils::TestHelper::begin_test(create_wlantap_config_client(HW_MAC_ADDR)).await;
    let () = loop_until_iface_is_found().await;

    let wlan_service =
        connect_to_service::<WlanMarker>().expect("Failed to connect to wlan service");

    let proxy = helper.proxy();
    connect(&wlan_service, &proxy, &mut helper, SSID, &BSS, None).await;

    let mut client = create_eth_client(&HW_MAC_ADDR)
        .await
        .expect("cannot create ethernet client")
        .expect(&format!("ethernet client not found {:?}", &HW_MAC_ADDR));

    verify_tx_and_rx(&mut client, &mut helper).await;
}
