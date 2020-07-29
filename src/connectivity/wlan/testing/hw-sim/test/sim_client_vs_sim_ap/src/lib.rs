// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_service::{ConnectConfig, ErrCode, WlanMarker, WlanProxy},
    fidl_fuchsia_wlan_tap::{WlantapPhyEvent, WlantapPhyProxy},
    fuchsia_async::{Time, Timer},
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon::DurationNum,
    futures::{channel::oneshot, join, TryFutureExt},
    log::info,
    pin_utils::pin_mut,
    std::panic,
    wlan_common::bss::Protection::Wpa2Personal,
    wlan_hw_sim::*,
};

const SSID: &[u8] = b"fuchsia";
const PASS_PHRASE: &str = "wpa2duel";

fn packet_forwarder<'a>(
    peer_phy: &'a WlantapPhyProxy,
    context: &'a str,
) -> impl Fn(WlantapPhyEvent) + 'a {
    move |event| {
        if let WlantapPhyEvent::Tx { args } = event {
            let frame = &args.packet.data;
            peer_phy
                .rx(0, frame, &mut create_rx_info(&WLANCFG_DEFAULT_AP_CHANNEL, 0))
                .expect(context);
        }
    }
}

// Connect stage

async fn intiate_connect(
    wlancfg_svc: &WlanProxy,
    config: &mut ConnectConfig,
    sender: oneshot::Sender<()>,
) {
    let error = wlancfg_svc.connect(config).await.expect("connecting via wlancfg");
    // Make sure Message4 reaches the AP.
    let () = Timer::new(Time::after(500.millis())).await;
    assert_eq!(error.code, ErrCode::Ok, "failed to connect: {:?}", error);

    let status = wlancfg_svc.status().await.expect("getting final client status");
    let is_secure = true;
    assert_associated_state(status, &AP_MAC_ADDR, SSID, &WLANCFG_DEFAULT_AP_CHANNEL, is_secure);

    sender.send(()).expect("done connecting, sending message to the other future");
}

/// At this stage client communicates with AP only, in order to establish connection
async fn verify_client_connects_to_ap(
    client_proxy: &WlantapPhyProxy,
    ap_proxy: &WlantapPhyProxy,
    client_helper: &mut test_utils::TestHelper,
    ap_helper: &mut test_utils::TestHelper,
) {
    let wlancfg_svc = connect_to_service::<WlanMarker>().expect("connecting to wlancfg service");

    let (sender, connect_confirm_receiver) = oneshot::channel();
    let mut connect_config = create_connect_config(SSID, PASS_PHRASE);

    let connect_fut = intiate_connect(&wlancfg_svc, &mut connect_config, sender);
    pin_mut!(connect_fut);

    let client_fut = client_helper.run_until_complete_or_timeout(
        10.seconds(),
        "connecting to AP",
        |event| match event {
            WlantapPhyEvent::SetChannel { args } => {
                if args.chan.primary == WLANCFG_DEFAULT_AP_CHANNEL.primary {
                    // TODO(35337): use beacon frame from configure_beacon
                    send_beacon(
                        &WLANCFG_DEFAULT_AP_CHANNEL,
                        &AP_MAC_ADDR,
                        SSID,
                        &Wpa2Personal,
                        &client_proxy,
                        0,
                    )
                    .expect("sending beacon");
                }
            }
            evt => packet_forwarder(&ap_proxy, "frame client -> ap")(evt),
        },
        connect_fut,
    );

    pin_mut!(connect_confirm_receiver);
    let ap_fut = ap_helper
        .run_until_complete_or_timeout(
            10.seconds(),
            "serving as an AP",
            packet_forwarder(&client_proxy, "frame ap ->  client"),
            connect_confirm_receiver,
        )
        .unwrap_or_else(|oneshot::Canceled| panic!("waiting for connect confirmation"));

    // Spawns 2 tasks:
    // 1. The client trying to connect to AP
    // 2. The AP trying to accept connection attempts which allows the client to connect.
    // When the client connects successfully, it notify the AP task to finish.
    // Both tasks need to be running at the same time to ensure "packets" can reach each other.

    join!(client_fut, ap_fut);
    // TODO(35339): Once AP supports status query, verify from the AP side that client associated.
}

// Data transfer stage

struct PeerInfo<'a> {
    addr: [u8; 6],
    payload: &'a [u8],
    name: &'a str,
}

async fn send_then_receive(
    eth: &mut ethernet::Client,
    me: &PeerInfo<'_>,
    peer: &PeerInfo<'_>,
    sender_to_peer: oneshot::Sender<()>,
    receiver_from_peer: oneshot::Receiver<()>,
) {
    // send packet and wait for confirmation
    send_fake_eth_frame(peer.addr, me.addr, me.payload, eth).await;

    // wait for packet and send confirmation
    let (header, payload) = get_next_frame(eth).await;
    assert_eq!(header.da, me.addr);
    assert_eq!(header.sa, peer.addr);
    assert_eq!(&payload[..], peer.payload);

    sender_to_peer.send(()).expect(&format!("confirming as {}", me.name));
    // this function must not return unless peer receives our frame because packet_forwarder
    // will stop "transmitting" the packet after the future finishes.
    receiver_from_peer.await.expect(&format!("waiting for {} confirmation", peer.name));
    info!("{} received packet from {}", me.name, peer.name);
}

/// At this stage the client communicates with an imaginary peer that is connected to the same AP.
/// But we are observing the packets from the AP's WLAN interface using a Ethernet client.
/// Client <-------> AP <-  -  -  -  -> peer behind AP
///   ^               ^
///   |               |
/// client-eth      ap-eth
/// Pretend that
/// 1. The AP has received an Ethernet frame from the imaginary peer and is sending it to
/// the client via WLAN.
/// 2. At the same time, the AP is receiving an Ethernet frame via WLAN from the client and will
/// forward it to the imaginary peer next.

async fn verify_ethernet_in_both_directions(
    client_proxy: &WlantapPhyProxy,
    ap_proxy: &WlantapPhyProxy,
    client_helper: &mut test_utils::TestHelper,
    ap_helper: &mut test_utils::TestHelper,
) {
    let mut client_eth = create_eth_client(&CLIENT_MAC_ADDR)
        .await
        .expect("creating client side ethernet")
        .expect("looking for client side ethernet");
    let mut ap_eth = create_eth_client(&AP_MAC_ADDR.0)
        .await
        .expect("creating ap side ethernet")
        .expect("looking for ap side ethernet");

    let (sender_ap_to_client, receiver_client_from_ap) = oneshot::channel();
    let (sender_client_to_ap, receiver_ap_from_client) = oneshot::channel();

    const CLIENT_PAYLOAD: &[u8] = b"from client to peer_behind_ap";
    const ETH_PEER_PAYLOAD: &[u8] = b"from peer_behind_ap to client but longer";

    let client_info = PeerInfo { addr: CLIENT_MAC_ADDR, payload: CLIENT_PAYLOAD, name: "client" };
    let peer_behind_ap_info =
        PeerInfo { addr: ETH_DST_MAC, payload: ETH_PEER_PAYLOAD, name: "peer_behind_ap" };

    let client_fut = send_then_receive(
        &mut client_eth,
        &client_info,
        &peer_behind_ap_info,
        sender_client_to_ap,
        receiver_client_from_ap,
    );
    let peer_behind_ap_fut = send_then_receive(
        &mut ap_eth,
        &peer_behind_ap_info,
        &client_info,
        sender_ap_to_client,
        receiver_ap_from_client,
    );

    pin_mut!(client_fut);
    pin_mut!(peer_behind_ap_fut);

    let client_with_timeout = client_helper.run_until_complete_or_timeout(
        10.seconds(),
        "client trying to exchange data with a peer behind AP",
        packet_forwarder(&ap_proxy, "frame client -> ap"),
        client_fut,
    );
    let peer_behind_ap_with_timeout = ap_helper.run_until_complete_or_timeout(
        10.seconds(),
        "AP forwarding data between client and its peer",
        packet_forwarder(&client_proxy, "frame ap ->  client"),
        peer_behind_ap_fut,
    );

    join!(client_with_timeout, peer_behind_ap_with_timeout);
}

/// Spawn two wlantap devices, one as client, the other AP. Verify the client connects to the AP
/// and ethernet frames can reach each other from both ends.
#[fuchsia_async::run_singlethreaded(test)]
async fn sim_client_vs_sim_ap() {
    init_syslog();

    let network_config =
        NetworkConfigBuilder::protected(&PASS_PHRASE.as_bytes().to_vec()).ssid(&SSID.to_vec());

    let mut client_helper =
        test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;
    let client_proxy = client_helper.proxy();
    let () = loop_until_iface_is_found().await;

    let mut ap_helper =
        test_utils::TestHelper::begin_ap_test(default_wlantap_config_ap(), network_config).await;
    let ap_proxy = ap_helper.proxy();

    verify_client_connects_to_ap(&client_proxy, &ap_proxy, &mut client_helper, &mut ap_helper)
        .await;

    verify_ethernet_in_both_directions(
        &client_proxy,
        &ap_proxy,
        &mut client_helper,
        &mut ap_helper,
    )
    .await;
    client_helper.stop().await;
    ap_helper.stop().await;
}
