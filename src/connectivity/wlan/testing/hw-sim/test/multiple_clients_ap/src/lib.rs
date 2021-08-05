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
    fidl_fuchsia_wlan_common as fidl_common,
    fidl_fuchsia_wlan_device_service::DeviceServiceMarker,
    fidl_fuchsia_wlan_sme::{
        self as fidl_sme, ClientSmeProxy, ConnectRequest, ConnectResultCode, Credential,
    },
    fuchsia_component::client::connect_to_protocol,
    fuchsia_zircon::DurationNum,
    futures::{channel::oneshot, future, join, stream::TryStreamExt, FutureExt, TryFutureExt},
    pin_utils::pin_mut,
    std::{panic, thread, time},
    wlan_common::{
        bss::Protection::Open,
        channel::{Cbw, Phy},
        fake_fidl_bss_description, RadioConfig,
    },
    wlan_hw_sim::*,
};

const SSID: &[u8] = b"fuchsia";

pub const CLIENT1_MAC_ADDR: [u8; 6] = [0x68, 0x62, 0x6f, 0x6e, 0x69, 0x6c];
pub const CLIENT2_MAC_ADDR: [u8; 6] = [0x68, 0x62, 0x6f, 0x6e, 0x69, 0x6d];

async fn connect(
    client_sme: &ClientSmeProxy,
    req: &mut ConnectRequest,
) -> Result<(), anyhow::Error> {
    let (local, remote) = fidl::endpoints::create_proxy()?;
    client_sme.connect(req, Some(remote))?;
    let mut stream = local.take_event_stream();
    while let Some(event) = stream.try_next().await? {
        match event {
            fidl_sme::ConnectTransactionEvent::OnConnectResult { code, .. } => {
                if code == ConnectResultCode::Success {
                    return Ok(());
                }
                return Err(format_err!("connect failed with error code: {:?}", code));
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

/// Spawn two client and one AP wlantap devices. Verify that both clients connect to the AP by
/// sending ethernet frames.
#[fuchsia_async::run_singlethreaded(test)]
async fn multiple_clients_ap() {
    init_syslog();

    let wlanstack_svc =
        connect_to_protocol::<DeviceServiceMarker>().expect("connecting to wlanstack service");

    let network_config = NetworkConfigBuilder::open().ssid(&SSID.to_vec());

    let mut dc = CreateDeviceHelper::new(&wlanstack_svc);

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
    let client1_sme = get_client_sme(&wlanstack_svc, client1_iface_id).await;
    let (client1_confirm_sender, client1_confirm_receiver) = oneshot::channel();

    let (mut client2_helper, client2_iface_id) = dc
        .create_device(wlantap_config_client(format!("wlantap-client-2"), CLIENT2_MAC_ADDR), None)
        .await
        .expect("create client2");
    let client2_proxy = client2_helper.proxy();
    let client2_sme = get_client_sme(&wlanstack_svc, client2_iface_id).await;
    let (client2_confirm_sender, client2_confirm_receiver) = oneshot::channel();

    let ap_fut = ap_helper
        .run_until_complete_or_timeout(
            std::i64::MAX.nanos(),
            "serving as an AP",
            EventHandlerBuilder::new()
                .on_tx(
                    Sequence::start()
                        .then(Rx::send(&client1_proxy, WLANCFG_DEFAULT_AP_CHANNEL))
                        .then(Rx::send(&client2_proxy, WLANCFG_DEFAULT_AP_CHANNEL)),
                )
                .build(),
            future::join(client1_confirm_receiver, client2_confirm_receiver)
                .then(|_| future::ok(())),
        )
        .unwrap_or_else(|oneshot::Canceled| panic!("waiting for connect confirmation"));

    // Start client 1
    let mut client1_connect_req = ConnectRequest {
        ssid: SSID.to_vec(),
        bss_description: fake_fidl_bss_description!(
            Open,
            ssid: SSID.to_vec(),
            bssid: AP_MAC_ADDR.0,
            channel: WLANCFG_DEFAULT_AP_CHANNEL,
        ),
        credential: Credential::None(fidl_sme::Empty {}),
        radio_cfg: RadioConfig::new(Phy::Ht, Cbw::Cbw20, WLANCFG_DEFAULT_AP_CHANNEL.primary)
            .to_fidl(),
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
                .on_set_channel(
                    Beacon::send_on_primary_channel(
                        WLANCFG_DEFAULT_AP_CHANNEL.primary,
                        &client1_proxy,
                    )
                    .bssid(AP_MAC_ADDR)
                    .ssid(SSID.to_vec())
                    .protection(Open),
                )
                .on_tx(Rx::send(&ap_proxy, WLANCFG_DEFAULT_AP_CHANNEL))
                .build(),
            client1_connect_fut.and_then(|()| {
                client1_confirm_sender.send(()).expect("sending confirmation");
                future::ok(())
            }),
        )
        .unwrap_or_else(|e| panic!("waiting for connect confirmation: {:?}", e));

    // Start client 2
    let mut client2_connect_req = ConnectRequest {
        ssid: SSID.to_vec(),
        bss_description: fake_fidl_bss_description!(
            Open,
            ssid: SSID.to_vec(),
            bssid: AP_MAC_ADDR.0,
            channel: WLANCFG_DEFAULT_AP_CHANNEL,
        ),
        credential: Credential::None(fidl_sme::Empty {}),
        radio_cfg: RadioConfig::new(Phy::Ht, Cbw::Cbw20, WLANCFG_DEFAULT_AP_CHANNEL.primary)
            .to_fidl(),
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
                .on_set_channel(
                    Beacon::send_on_primary_channel(
                        WLANCFG_DEFAULT_AP_CHANNEL.primary,
                        &client2_proxy,
                    )
                    .bssid(AP_MAC_ADDR)
                    .ssid(SSID.to_vec())
                    .protection(Open),
                )
                .on_tx(Rx::send(&ap_proxy, WLANCFG_DEFAULT_AP_CHANNEL))
                .build(),
            client2_connect_fut.and_then(|()| {
                client2_confirm_sender.send(()).expect("sending confirmation");
                future::ok(())
            }),
        )
        .unwrap_or_else(|e| panic!("waiting for connect confirmation: {:?}", e));

    join!(ap_fut, client1_fut, client2_fut);
    client1_helper.stop().await;
    client2_helper.stop().await;
    ap_helper.stop().await;
}
