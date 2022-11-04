// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/74991): This test should be implemented using the Policy API (instead of directly
//                        interacting with SME). However, Policy cannot yet manage multiple client
//                        interfaces. Parts of the test pause so that Policy can detect the SME
//                        clients and request startup disconnects before the test continues to
//                        manipulate their state. When the Policy API can be used, reimplement this
//                        test with it (and remove the pauses).

use {
    anyhow::format_err,
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_common_security as fidl_security,
    fidl_fuchsia_wlan_device_service::DeviceMonitorMarker,
    fidl_fuchsia_wlan_ieee80211 as fidl_ieee80211,
    fidl_fuchsia_wlan_sme::{self as fidl_sme, ClientSmeProxy, ConnectRequest},
    fidl_fuchsia_wlan_tap as wlantap, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_zircon::DurationNum,
    futures::{
        channel::oneshot, future, join, stream::TryStreamExt, FutureExt, StreamExt, TryFutureExt,
    },
    pin_utils::pin_mut,
    std::{panic, thread, time},
    wlan_common::{bss::Protection::Open, fake_fidl_bss_description, TimeUnit},
    wlan_hw_sim::*,
};

pub const CLIENT1_MAC_ADDR: [u8; 6] = [0x68, 0x62, 0x6f, 0x6e, 0x69, 0x6c];
pub const CLIENT2_MAC_ADDR: [u8; 6] = [0x68, 0x62, 0x6f, 0x6e, 0x69, 0x6d];

async fn connect(
    client_sme: &ClientSmeProxy,
    req: &mut ConnectRequest,
) -> Result<(), anyhow::Error> {
    let (local, remote) = fidl::endpoints::create_proxy()?;
    client_sme.connect(req, Some(remote))?;
    let mut stream = local.take_event_stream();
    if let Some(event) = stream.try_next().await? {
        match event {
            fidl_sme::ConnectTransactionEvent::OnConnectResult { result } => {
                if result.code == fidl_ieee80211::StatusCode::Success {
                    return Ok(());
                }
                return Err(format_err!("connect failed with error code: {:?}", result.code));
            }
            other => {
                return Err(format_err!(
                    "Expected ConnectTransactionEvent::OnConnectResult event, got {:?}",
                    other
                ))
            }
        }
    }
    Err(format_err!("Server closed the ConnectTransaction channel before sending a response"))
}

// TODO(fxbug.dev/91118) - Added to help investigate hw-sim test. Remove later
async fn canary(mut finish_receiver: oneshot::Receiver<()>) {
    let mut interval_stream = fasync::Interval::new(fasync::Duration::from_seconds(1));
    loop {
        futures::select! {
            _ = interval_stream.next() => {
                tracing::info!("1 second canary");
            }
            _ = finish_receiver => {
                return;
            }
        }
    }
}

/// Spawn two client and one AP wlantap devices. Verify that both clients connect to the AP by
/// sending ethernet frames.
#[fuchsia_async::run_singlethreaded(test)]
async fn multiple_clients_ap() {
    init_syslog();

    let wlan_monitor_svc =
        connect_to_protocol::<DeviceMonitorMarker>().expect("connecting to device monitor service");

    let network_config = NetworkConfigBuilder::open().ssid(&AP_SSID);

    let mut dc = CreateDeviceHelper::new(&wlan_monitor_svc);

    let (mut ap_helper, _) = dc
        .create_device(default_wlantap_config_ap(), Some(network_config))
        .await
        .expect("create ap");
    let ap_proxy = ap_helper.proxy();

    let (mut client1_helper, client1_iface_id) = dc
        .create_device(wlantap_config_client(format!("wlantap-client-1"), CLIENT1_MAC_ADDR), None)
        .await
        .expect("create client1");
    let client1_proxy = client1_helper.proxy();
    let client1_sme = get_client_sme(&wlan_monitor_svc, client1_iface_id).await;
    let (client1_confirm_sender, client1_confirm_receiver) = oneshot::channel();

    let (mut client2_helper, client2_iface_id) = dc
        .create_device(wlantap_config_client(format!("wlantap-client-2"), CLIENT2_MAC_ADDR), None)
        .await
        .expect("create client2");
    let client2_proxy = client2_helper.proxy();
    let client2_sme = get_client_sme(&wlan_monitor_svc, client2_iface_id).await;
    let (client2_confirm_sender, client2_confirm_receiver) = oneshot::channel();

    let (finish_sender, finish_receiver) = oneshot::channel();
    let ap_fut = ap_helper
        .run_until_complete_or_timeout(
            std::i64::MAX.nanos(),
            "serving as an AP",
            EventHandlerBuilder::new()
                .on_debug_name("ap")
                .on_tx(|tx_args: &wlantap::TxArgs| {
                    client1_proxy
                        .rx(
                            0,
                            &tx_args.packet.data,
                            &mut create_rx_info(&WLANCFG_DEFAULT_AP_CHANNEL, 0),
                        )
                        .expect("client 1 rx failed");
                    client2_proxy
                        .rx(
                            0,
                            &tx_args.packet.data,
                            &mut create_rx_info(&WLANCFG_DEFAULT_AP_CHANNEL, 0),
                        )
                        .expect("client 2 rx failed");
                })
                .build(),
            future::join(client1_confirm_receiver, client2_confirm_receiver).then(|_| {
                finish_sender.send(()).expect("sending finish notification");
                future::ok(())
            }),
        )
        .unwrap_or_else(|oneshot::Canceled| panic!("waiting for connect confirmation"));

    // Start client 1
    let mut client1_connect_req = ConnectRequest {
        ssid: AP_SSID.to_vec(),
        bss_description: fake_fidl_bss_description!(
            Open,
            ssid: AP_SSID.clone(),
            bssid: AP_MAC_ADDR.0,
            // Unrealistically long beacon period so that connect doesn't timeout on slow bots.
            beacon_period: TimeUnit::DEFAULT_BEACON_INTERVAL.0 * 20u16,
            channel: WLANCFG_DEFAULT_AP_CHANNEL.into(),
        ),
        authentication: fidl_security::Authentication {
            protocol: fidl_security::Protocol::Open,
            credentials: None,
        },
        deprecated_scan_type: fidl_common::ScanType::Passive,
        multiple_bss_candidates: false, // only used for metrics, select arbitrary value
    };
    let client1_connect_fut = connect(&client1_sme, &mut client1_connect_req);
    pin_mut!(client1_connect_fut);
    thread::sleep(time::Duration::from_secs(3)); // Wait for the policy layer. See fxbug.dev/74991.
    let client1_fut = client1_helper
        .run_until_complete_or_timeout(
            std::i64::MAX.nanos(),
            "connecting to AP",
            EventHandlerBuilder::new()
                .on_debug_name("client1")
                .on_start_scan(start_scan_handler(
                    &client1_proxy,
                    Ok(vec![BeaconInfo {
                        channel: WLANCFG_DEFAULT_AP_CHANNEL.clone(),
                        bssid: AP_MAC_ADDR,
                        ssid: AP_SSID.clone(),
                        protection: Open,
                        rssi_dbm: 0,
                        beacon_or_probe: BeaconOrProbeResp::ProbeResp { wsc_ie: None },
                    }]),
                ))
                .on_tx(|tx_args: &wlantap::TxArgs| {
                    ap_proxy
                        .rx(
                            0,
                            &tx_args.packet.data,
                            &mut create_rx_info(&WLANCFG_DEFAULT_AP_CHANNEL, 0),
                        )
                        .expect("ap rx failed")
                })
                .build(),
            client1_connect_fut.and_then(|()| {
                client1_confirm_sender.send(()).expect("sending confirmation");
                future::ok(())
            }),
        )
        .unwrap_or_else(|e| panic!("waiting for connect confirmation: {:?}", e));

    // Start client 2
    let mut client2_connect_req = ConnectRequest {
        ssid: AP_SSID.to_vec(),
        bss_description: fake_fidl_bss_description!(
            Open,
            ssid: AP_SSID.clone(),
            bssid: AP_MAC_ADDR.0,
            // Unrealistically long beacon period so that connect doesn't timeout on slow bots.
            beacon_period: TimeUnit::DEFAULT_BEACON_INTERVAL.0 * 20u16,
            channel: WLANCFG_DEFAULT_AP_CHANNEL.into(),
        ),
        authentication: fidl_security::Authentication {
            protocol: fidl_security::Protocol::Open,
            credentials: None,
        },
        deprecated_scan_type: fidl_common::ScanType::Passive,
        multiple_bss_candidates: false, // only used for metrics, select arbitrary value
    };
    let client2_connect_fut = connect(&client2_sme, &mut client2_connect_req);
    pin_mut!(client2_connect_fut);
    thread::sleep(time::Duration::from_secs(3)); // Wait for the policy layer. See fxbug.dev/74991.
    let client2_fut = client2_helper
        .run_until_complete_or_timeout(
            std::i64::MAX.nanos(),
            "connecting to AP",
            EventHandlerBuilder::new()
                .on_debug_name("client2")
                .on_start_scan(start_scan_handler(
                    &client2_proxy,
                    Ok(vec![BeaconInfo {
                        channel: WLANCFG_DEFAULT_AP_CHANNEL.clone(),
                        bssid: AP_MAC_ADDR,
                        ssid: AP_SSID.clone(),
                        protection: Open,
                        rssi_dbm: 0,
                        beacon_or_probe: BeaconOrProbeResp::ProbeResp { wsc_ie: None },
                    }]),
                ))
                .on_tx(|tx_args: &wlantap::TxArgs| {
                    ap_proxy
                        .rx(
                            0,
                            &tx_args.packet.data,
                            &mut create_rx_info(&WLANCFG_DEFAULT_AP_CHANNEL, 0),
                        )
                        .expect("ap rx failed")
                })
                .build(),
            client2_connect_fut.and_then(|()| {
                client2_confirm_sender.send(()).expect("sending confirmation");
                future::ok(())
            }),
        )
        .unwrap_or_else(|e| panic!("waiting for connect confirmation: {:?}", e));

    join!(ap_fut, client1_fut, client2_fut, canary(finish_receiver));
    client1_helper.stop().await;
    client2_helper.stop().await;
    ap_helper.stop().await;
}
