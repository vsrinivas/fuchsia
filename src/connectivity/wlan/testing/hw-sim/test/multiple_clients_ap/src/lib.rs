// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err,
    fidl_fuchsia_wlan_common as fidl_common,
    fidl_fuchsia_wlan_device_service::DeviceServiceMarker,
    fidl_fuchsia_wlan_sme::{
        self as fidl_sme, ApConfig, ClientSmeProxy, ConnectRequest, ConnectResultCode, Credential,
        StartApResultCode,
    },
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon::DurationNum,
    futures::{channel::oneshot, future, join, stream::TryStreamExt, FutureExt, TryFutureExt},
    pin_utils::pin_mut,
    std::panic,
    wlan_common::{
        bss::Protection::Open,
        channel::{Cbw, Phy},
        RadioConfig,
    },
    wlan_hw_sim::*,
};

const SSID: &[u8] = b"fuchsia";

pub const CLIENT1_MAC_ADDR: [u8; 6] = [0x67, 0x62, 0x6f, 0x6e, 0x69, 0x6c];
pub const CLIENT2_MAC_ADDR: [u8; 6] = [0x67, 0x62, 0x6f, 0x6e, 0x69, 0x6d];

async fn connect(
    client_sme: &ClientSmeProxy,
    req: &mut ConnectRequest,
) -> Result<(), anyhow::Error> {
    let (local, remote) = fidl::endpoints::create_proxy()?;
    client_sme.connect(req, Some(remote))?;
    let mut stream = local.take_event_stream();
    while let Some(event) = stream.try_next().await? {
        match event {
            fidl_sme::ConnectTransactionEvent::OnFinished { code } => {
                if code == ConnectResultCode::Success {
                    return Ok(());
                }
                return Err(format_err!("connect failed with error code: {:?}", code));
            }
        }
    }
    Err(format_err!("Server closed the ConnectTransaction channel before sending a response"))
}

/// Spawn two wlantap devices, one as client, the other AP. Verify the client connects to the AP
/// and ethernet frames can reach each other from both ends.
#[fuchsia_async::run_singlethreaded(test)]
async fn multiple_clients_ap() {
    let wlanstack_svc =
        connect_to_service::<DeviceServiceMarker>().expect("connecting to wlanstack service");

    let mut dc = CreateDeviceHelper::new(&wlanstack_svc);

    let (mut ap_helper, ap_iface_id) =
        dc.create_device(default_wlantap_config_ap()).await.expect("create ap");
    let ap_proxy = ap_helper.proxy();
    let ap_sme = get_ap_sme(&wlanstack_svc, ap_iface_id).await;

    let (mut client1_helper, client1_iface_id) = dc
        .create_device(wlantap_config_client(format!("wlantap-client-1"), CLIENT1_MAC_ADDR))
        .await
        .expect("create client1");
    let client1_proxy = client1_helper.proxy();
    let client1_sme = get_client_sme(&wlanstack_svc, client1_iface_id).await;
    let (client1_confirm_sender, client1_confirm_receiver) = oneshot::channel();

    let (mut client2_helper, client2_iface_id) = dc
        .create_device(wlantap_config_client(format!("wlantap-client-2"), CLIENT2_MAC_ADDR))
        .await
        .expect("create client2");
    let client2_proxy = client2_helper.proxy();
    let client2_sme = get_client_sme(&wlanstack_svc, client2_iface_id).await;
    let (client2_confirm_sender, client2_confirm_receiver) = oneshot::channel();

    // Start AP
    assert_eq!(
        ap_sme
            .start(&mut ApConfig {
                ssid: SSID.to_vec(),
                password: vec![],
                radio_cfg: RadioConfig::new(Phy::Ht, Cbw::Cbw20, CHANNEL.primary).to_fidl(),
            })
            .await
            .expect("expect start ap result code"),
        StartApResultCode::Success
    );

    let ap_fut = ap_helper
        .run_until_complete_or_timeout(
            10.seconds(),
            "serving as an AP",
            EventHandlerBuilder::new()
                .on_tx(
                    Sequence::start()
                        .then(Rx::send(&client1_proxy, CHANNEL))
                        .then(Rx::send(&client2_proxy, CHANNEL)),
                )
                .build(),
            future::join(client1_confirm_receiver, client2_confirm_receiver)
                .then(|_| future::ok(())),
        )
        .unwrap_or_else(|oneshot::Canceled| panic!("waiting for connect confirmation"));
    pin_mut!(ap_fut);

    // Start client 1
    let mut client1_connect_req = ConnectRequest {
        ssid: SSID.to_vec(),
        credential: Credential::None(fidl_sme::Empty {}),
        radio_cfg: RadioConfig::new(Phy::Ht, Cbw::Cbw20, CHANNEL.primary).to_fidl(),
        scan_type: fidl_common::ScanType::Passive,
    };
    let client1_connect_fut = connect(&client1_sme, &mut client1_connect_req);
    pin_mut!(client1_connect_fut);
    let client1_fut = client1_helper
        .run_until_complete_or_timeout(
            10.seconds(),
            "connecting to AP",
            EventHandlerBuilder::new()
                .on_set_channel(
                    Beacon::send(&client1_proxy)
                        .bssid(AP_MAC_ADDR)
                        .ssid(SSID.to_vec())
                        .protection(Open),
                )
                .on_tx(Rx::send(&ap_proxy, CHANNEL))
                .build(),
            client1_connect_fut.and_then(|()| {
                client1_confirm_sender.send(()).expect("sending confirmation");
                future::ok(())
            }),
        )
        .unwrap_or_else(|e| panic!("waiting for connect confirmation: {:?}", e));
    pin_mut!(client1_fut);

    // Start client 2
    let mut client2_connect_req = ConnectRequest {
        ssid: SSID.to_vec(),
        credential: Credential::None(fidl_sme::Empty {}),
        radio_cfg: RadioConfig::new(Phy::Ht, Cbw::Cbw20, CHANNEL.primary).to_fidl(),
        scan_type: fidl_common::ScanType::Passive,
    };
    let client2_connect_fut = connect(&client2_sme, &mut client2_connect_req);
    pin_mut!(client2_connect_fut);
    let client2_fut = client2_helper
        .run_until_complete_or_timeout(
            10.seconds(),
            "connecting to AP",
            EventHandlerBuilder::new()
                .on_set_channel(
                    Beacon::send(&client2_proxy)
                        .bssid(AP_MAC_ADDR)
                        .ssid(SSID.to_vec())
                        .protection(Open),
                )
                .on_tx(Rx::send(&ap_proxy, CHANNEL))
                .build(),
            client2_connect_fut.and_then(|()| {
                client2_confirm_sender.send(()).expect("sending confirmation");
                future::ok(())
            }),
        )
        .unwrap_or_else(|e| panic!("waiting for connect confirmation: {:?}", e));
    pin_mut!(client2_fut);

    join!(ap_fut, client1_fut, client2_fut);
}
