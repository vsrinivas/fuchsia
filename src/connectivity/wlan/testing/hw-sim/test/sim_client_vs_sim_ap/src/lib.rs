// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_policy as fidl_policy,
    fidl_fuchsia_wlan_tap::{WlantapPhyEvent, WlantapPhyProxy},
    fuchsia_zircon::DurationNum as _,
    futures::{channel::oneshot, join, TryFutureExt},
    pin_utils::pin_mut,
    tracing::{info, warn},
    wlan_common::bss::Protection::Wpa2Personal,
    wlan_hw_sim::*,
};

const PASS_PHRASE: &str = "wpa2duel";

// TODO(fxbug.dev/73871): Encode constants like this in the type system.
const WAIT_FOR_PAYLOAD_INTERVAL: i64 = 500; // milliseconds
const WAIT_FOR_ACK_INTERVAL: i64 = 500; // milliseconds

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

async fn initiate_connect(
    client_controller: &fidl_policy::ClientControllerProxy,
    mut update_stream: fidl_policy::ClientStateUpdatesRequestStream,
    config: &mut fidl_policy::NetworkIdentifier,
    sender: oneshot::Sender<()>,
) {
    // Issue the connect request.
    let response = client_controller.connect(config).await.expect("connecting via wlancfg");
    assert_eq!(response, fidl_common::RequestStatus::Acknowledged);

    // Monitor the update stream for the connected notification.
    wait_until_client_state(&mut update_stream, |update| {
        has_id_and_state(update, config, fidl_policy::ConnectionState::Connected)
    })
    .await;
    sender.send(()).expect("done connecting, sending message to the other future");
}

/// At this stage client communicates with AP only, in order to establish connection
async fn verify_client_connects_to_ap(
    client_proxy: &WlantapPhyProxy,
    ap_proxy: &WlantapPhyProxy,
    client_helper: &mut test_utils::TestHelper,
    ap_helper: &mut test_utils::TestHelper,
) {
    let (client_controller, update_stream) = wlan_hw_sim::init_client_controller().await;

    let (sender, connect_confirm_receiver) = oneshot::channel();
    let network_config = NetworkConfigBuilder::protected(
        fidl_policy::SecurityType::Wpa2,
        &PASS_PHRASE.as_bytes().to_vec(),
    )
    .ssid(&AP_SSID);

    // The credentials need to be stored before attempting to connect.
    client_controller
        .save_network(fidl_policy::NetworkConfig::from(network_config.clone()))
        .await
        .expect("sending save network request")
        .expect("saving network config.");

    let network_config = fidl_policy::NetworkConfig::from(network_config);
    let mut network_id = network_config.id.unwrap();

    let connect_fut = initiate_connect(&client_controller, update_stream, &mut network_id, sender);
    pin_mut!(connect_fut);

    let client_fut = client_helper.run_until_complete_or_timeout(
        10.seconds(),
        "connecting to AP",
        |event| match event {
            WlantapPhyEvent::StartScan { args } => {
                Beacon {
                    channel: WLANCFG_DEFAULT_AP_CHANNEL.clone(),
                    bssid: AP_MAC_ADDR,
                    ssid: AP_SSID.clone(),
                    protection: Wpa2Personal,
                    rssi_dbm: -30,
                }
                .send(client_proxy)
                .expect("failed to send beacon");
                send_scan_complete(args.scan_id, 0, &client_proxy)
                    .expect("failed to send scan complete");
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
    // TODO(fxbug.dev/35339): Once AP supports status query, verify from the AP side that client associated.
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
    let mut sender_to_peer_ptr = Some(sender_to_peer);
    let mut receiver_from_peer_ptr = Some(receiver_from_peer);

    // This loop has three parts.
    //
    // 1. Send me.payload.
    // 2. Wait to receive peer.payload.
    // 3. Wait for receiver_from_peer to complete.
    //
    // Step 1 is assumed to complete on every iteration. If either of Step 2 or Step 3 timeout,
    // it will be retried on the next iteration. Once Steps 1 through 3 are complete, the
    // loop terminates.
    while sender_to_peer_ptr.is_some() || receiver_from_peer_ptr.is_some() {
        if receiver_from_peer_ptr.is_some() {
            info!("{} sending payload to {}", me.name, peer.name);
            send_fake_eth_frame(peer.addr, me.addr, me.payload, eth).await;
        }

        match sender_to_peer_ptr.take() {
            None => info!(
                "{} already received payload from {}. Skipping wait for payload.",
                me.name, peer.name
            ),
            Some(sender_to_peer) => {
                info!("{} awaiting payload from {}", me.name, peer.name);
                let get_next_frame_fut = get_next_frame(eth);
                pin_mut!(get_next_frame_fut);
                match test_utils::timeout_after(
                    WAIT_FOR_PAYLOAD_INTERVAL.millis(),
                    &mut get_next_frame_fut,
                )
                .await
                {
                    Err(()) => {
                        warn!("{} timed out waiting for payload from {}.", me.name, peer.name);
                        sender_to_peer_ptr = Some(sender_to_peer);
                    }
                    Ok((header, payload)) => {
                        assert_eq!(header.da, me.addr);
                        assert_eq!(header.sa, peer.addr);

                        if &payload[..] == peer.payload {
                            info!("{} received packet from {}. Acknowledging receipt through channel...", me.name, peer.name);
                            sender_to_peer.send(()).expect(&format!("confirming as {}", me.name));
                        } else {
                            panic!("Unexpected payload received: {:?}", &payload[..]);
                        }
                    }
                }
            }
        }

        // Peer packets cannot be received unless the incoming packet_forwarder is running, so this
        // function must wait until the peer receives the payload.
        match receiver_from_peer_ptr.take() {
            None => info!(
                "{} already received acknowledgement from {}. Skipping wait for acknowledgement.",
                me.name, peer.name
            ),
            Some(mut receiver_from_peer) => {
                info!("{} awaiting acknowledgement of payload receipt from {}", me.name, peer.name);
                match test_utils::timeout_after(
                    WAIT_FOR_ACK_INTERVAL.millis(),
                    &mut receiver_from_peer,
                )
                .await
                {
                    Err(()) => {
                        warn!(
                            "{} timed out waiting for acknowledgement from {}.",
                            me.name, peer.name
                        );
                        receiver_from_peer_ptr = Some(receiver_from_peer);
                    }
                    Ok(receiver_result) => {
                        receiver_result.expect(&format!(
                            "{} waiting for {} acknowledgement",
                            me.name, peer.name
                        ));
                        info!("{} received acknowledgement from {}", me.name, peer.name,);
                    }
                }
            }
        }
    }
    info!("{} exiting send_then_receive.", me.name);
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
        // TODO(fxbug.dev/73821): This time should be reduced to 5 seconds
        // once Policy no longer mistakenly schedules unneeded scans.
        60.seconds(),
        "client trying to exchange data with a peer behind AP",
        packet_forwarder(&ap_proxy, "frame client -> ap"),
        client_fut,
    );
    let peer_behind_ap_with_timeout = ap_helper.run_until_complete_or_timeout(
        // TODO(fxbug.dev/73821): This time should be reduced to 5 seconds
        // once Policy no longer mistakenly schedules unneeded scans.
        60.seconds(),
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

    let network_config = NetworkConfigBuilder::protected(
        fidl_policy::SecurityType::Wpa2,
        &PASS_PHRASE.as_bytes().to_vec(),
    )
    .ssid(&AP_SSID);

    let mut client_helper =
        test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;
    let client_proxy = client_helper.proxy();
    let () = loop_until_iface_is_found(&mut client_helper).await;

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
