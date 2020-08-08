// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::ensure,
    fidl_fuchsia_wlan_service::WlanMarker,
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon::DurationNum,
    futures::StreamExt,
    pin_utils::pin_mut,
    wlan_common::{buffer_reader::BufferReader, mac},
    wlan_hw_sim::*,
};

const BSS: mac::Bssid = mac::Bssid([0x65, 0x74, 0x68, 0x6e, 0x65, 0x74]);
const SSID: &[u8] = b"ethernet";
const PAYLOAD: &[u8] = &[1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15];

async fn send_and_receive<'a>(
    client: &'a mut ethernet::Client,
    buf: &'a Vec<u8>,
) -> Result<(mac::EthernetIIHdr, Vec<u8>), anyhow::Error> {
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
                let mut buf = vec![0; rx_buffer.len() as usize];
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
    write_fake_eth_frame(ETH_DST_MAC, CLIENT_MAC_ADDR, PAYLOAD, &mut buf);

    let eth_tx_rx_fut = send_and_receive(client, &buf);
    pin_mut!(eth_tx_rx_fut);

    let phy = helper.proxy();
    let mut actual = Vec::new();
    let (header, payload) = helper
        .run_until_complete_or_timeout(
            5.seconds(),
            "verify ethernet_tx_rx",
            EventHandlerBuilder::new()
                .on_tx(MatchTx::new().on_msdu(|msdu: &mac::Msdu<&[u8]>| {
                    let mac::Msdu { dst_addr, src_addr, llc_frame } = msdu;
                    if *dst_addr == ETH_DST_MAC && *src_addr == CLIENT_MAC_ADDR {
                        assert_eq!(llc_frame.hdr.protocol_id.to_native(), mac::ETHER_TYPE_IPV4);
                        actual.clear();
                        actual.extend_from_slice(llc_frame.body);
                        rx_wlan_data_frame(
                            &CHANNEL,
                            &CLIENT_MAC_ADDR,
                            &BSS.0,
                            &ETH_DST_MAC,
                            &PAYLOAD,
                            mac::ETHER_TYPE_IPV4,
                            &phy,
                        )
                        .expect("sending wlan data frame");
                    }
                }))
                .build(),
            eth_tx_rx_fut,
        )
        .await
        .expect("send and receive eth");
    assert_eq!(&actual[..], PAYLOAD);
    assert_eq!(header.da, CLIENT_MAC_ADDR);
    assert_eq!(header.sa, ETH_DST_MAC);
    assert_eq!(header.ether_type.to_native(), mac::ETHER_TYPE_IPV4);
    assert_eq!(&payload[..], PAYLOAD);
}

/// Test an ethernet device backed by WLAN device and send and receive data frames by verifying
/// frames are delivered without any change in both directions.
#[fuchsia_async::run_singlethreaded(test)]
async fn ethernet_tx_rx() {
    init_syslog();

    let mut helper = test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;
    let () = loop_until_iface_is_found().await;

    let wlan_service =
        connect_to_service::<WlanMarker>().expect("Failed to connect to wlan service");

    connect_to_open_ap(&wlan_service, &mut helper, SSID, &BSS).await;

    let mut client = create_eth_client(&CLIENT_MAC_ADDR)
        .await
        .expect("cannot create ethernet client")
        .expect(&format!("ethernet client not found {:?}", &CLIENT_MAC_ADDR));

    verify_tx_and_rx(&mut client, &mut helper).await;
    helper.stop().await;
}
