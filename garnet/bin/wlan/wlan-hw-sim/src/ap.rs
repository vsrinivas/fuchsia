// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
pub mod tests {
    use crate::{config, mac_frames, test_utils};
    use {
        fidl, fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_device as wlan_device,
        fidl_fuchsia_wlan_device_service as fidl_wlan_service, fidl_fuchsia_wlan_sme as fidl_sme,
        fidl_fuchsia_wlan_tap as wlantap, fuchsia_app as app,
        fuchsia_async::{self as fasync, temp::TempStreamExt, TimeoutExt},
        fuchsia_zircon::{self as zx, prelude::*},
        futures::channel::mpsc,
        futures::prelude::*,
        hex,
        std::io::Cursor,
        std::panic,
        wlan_common::{
            channel::{Cbw, Phy},
            RadioConfig,
        },
    };

    pub fn test_open_ap_connect() {
        // --- start test data block
        const HW_MAC_ADDR: [u8; 6] = [0x70, 0xf1, 0x1c, 0x05, 0x2d, 0x7f];

        // frame 1 and 3 from ios12.1-connect-open-ap.pcapng
        #[rustfmt::skip]
        const AUTH_REQ_HEX: &str = "b0003a0170f11c052d7fdca90435e58c70f11c052d7ff07d000001000000dd0b0017f20a00010400000000dd09001018020000100000ed7895e7";
        let auth_req = hex::decode(AUTH_REQ_HEX).expect("fail to parse auth req hex");

        #[rustfmt::skip]
        const ASSOC_REQ_HEX: &str = "00003a0170f11c052d7fdca90435e58c70f11c052d7f007e210414000014465543485349412d544553542d4b4945542d4150010882848b962430486c32040c121860210202142402010ddd0b0017f20a00010400000000dd09001018020000100000debda9bb";
        let assoc_req = hex::decode(ASSOC_REQ_HEX).expect("fail to parse assoc req hex");
        // -- end test data block

        let mut exec = fasync::Executor::new().expect("Failed to create an executor");

        // Connect to WLAN device service and start watching for new device
        let wlan_service =
            app::client::connect_to_service::<fidl_wlan_service::DeviceServiceMarker>()
                .expect("Failed to connect to wlan service");
        let (watcher_proxy, watcher_server_end) =
            fidl::endpoints::create_proxy().expect("fail to create fidl endpoints");
        wlan_service.watch_devices(watcher_server_end).expect("wlan watch_devices call fails");

        // Create wlantap PHY
        let mut helper =
            test_utils::TestHelper::begin_test(&mut exec, create_wlantap_config(HW_MAC_ADDR));

        // Wait until iface is created from wlantap PHY
        let mut watcher_event_stream = watcher_proxy.take_event_stream();
        let fut = get_new_added_iface(&mut watcher_event_stream);
        let iface_id = exec.run_singlethreaded(
            fut.on_timeout(10.seconds().after_now(), || panic!("no iface added")),
        );

        let sme_fut = get_ap_sme(&wlan_service, iface_id)
            .on_timeout(5.seconds().after_now(), || panic!("timeout retrieving ap sme"));
        let sme = exec.run_singlethreaded(sme_fut);

        // Stop AP in case it was already started
        let _ = exec.run_singlethreaded(sme.stop());

        // Start AP
        let mut config = fidl_sme::ApConfig {
            ssid: String::from("fuchsia").into_bytes(),
            password: vec![],
            radio_cfg: RadioConfig::new(Phy::Ht, Cbw::Cbw20, 11).to_fidl(),
        };
        let result_code =
            exec.run_singlethreaded(sme.start(&mut config)).expect("expect start ap result code");
        assert_eq!(result_code, fidl_sme::StartApResultCode::Success);

        // (client->ap) send a mock auth req
        let proxy = helper.proxy();
        proxy
            .rx(0, &mut auth_req.iter().cloned(), &mut create_rx_info())
            .expect("cannot send auth req frame");

        // (ap->client) verify auth response frame was sent
        verify_auth_resp(&mut helper, &mut exec);

        // (client->ap) send a mock assoc req
        let proxy = helper.proxy();
        proxy
            .rx(0, &mut assoc_req.iter().cloned(), &mut create_rx_info())
            .expect("cannot send assoc req frame");

        // (ap->client) verify assoc response frame was sent
        verify_assoc_resp(&mut helper, &mut exec);
    }

    fn verify_auth_resp(helper: &mut test_utils::TestHelper, exec: &mut fasync::Executor) {
        let (mut sender, receiver) = mpsc::channel::<()>(1);
        let event_handler = move |event| match event {
            wlantap::WlantapPhyEvent::Tx { args } => {
                // ignore any stray data frame (likely some multicast frames by netstack)
                if args.packet.data.len() < 2
                    || args.packet.data[1] == mac_frames::FrameControlType::Data as u8
                {
                    return;
                }
                let mut reader = Cursor::new(&args.packet.data);
                let header = mac_frames::MgmtHeader::from_reader(&mut reader)
                    .expect("frame does not have valid mgmt header");
                assert_eq!(header.frame_control.typ(), mac_frames::FrameControlType::Mgmt as u16);
                assert_eq!(
                    header.frame_control.subtype(),
                    mac_frames::MgmtSubtype::Authentication as u16
                );
                let body = mac_frames::AuthenticationFields::from_reader(&mut reader)
                    .expect("not a valid auth frame");
                assert_eq!(body.status_code, mac_frames::StatusCode::Success as u16);
                sender.try_send(()).unwrap();
            }
            _ => {}
        };
        helper
            .run(
                exec,
                5.seconds(),
                "waiting for authentication response",
                event_handler,
                receiver.map(Ok).try_into_future(),
            )
            .unwrap_or_else(|()| unreachable!());
    }

    fn verify_assoc_resp(helper: &mut test_utils::TestHelper, exec: &mut fasync::Executor) {
        let (mut sender, receiver) = mpsc::channel::<()>(1);
        let event_handler = move |event| match event {
            wlantap::WlantapPhyEvent::Tx { args } => {
                // ignore any stray data frame (likely some multicast frames by netstack)
                if args.packet.data.len() < 2
                    || args.packet.data[1] == mac_frames::FrameControlType::Data as u8
                {
                    return;
                }
                let mut reader = Cursor::new(&args.packet.data);
                let header = mac_frames::MgmtHeader::from_reader(&mut reader)
                    .expect("frame does not have valid mgmt header");
                assert_eq!(header.frame_control.typ(), mac_frames::FrameControlType::Mgmt as u16);
                if header.frame_control.subtype() == mac_frames::MgmtSubtype::Action as u16 {
                    return;
                }
                assert_eq!(
                    header.frame_control.subtype(),
                    mac_frames::MgmtSubtype::AssociationResponse as u16
                );
                let body = mac_frames::AssociationResponseFields::from_reader(&mut reader)
                    .expect("not a valid assoc frame");
                assert_eq!(body.status_code, mac_frames::StatusCode::Success as u16);
                sender.try_send(()).unwrap();
            }
            _ => {}
        };
        helper
            .run(
                exec,
                5.seconds(),
                "waiting for association response",
                event_handler,
                receiver.map(Ok).try_into_future(),
            )
            .unwrap_or_else(|()| unreachable!());
    }

    async fn get_new_added_iface(
        device_watch_stream: &mut fidl_wlan_service::DeviceWatcherEventStream,
    ) -> u16 {
        loop {
            match await!(device_watch_stream.next()) {
                Some(Ok(event)) => match event {
                    fidl_wlan_service::DeviceWatcherEvent::OnIfaceAdded { iface_id } => {
                        return iface_id;
                    }
                    _ => (),
                },
                _ => panic!("failed to get watch stream event"),
            }
        }
    }

    async fn get_ap_sme(
        wlan_service: &fidl_wlan_service::DeviceServiceProxy,
        iface_id: u16,
    ) -> fidl_sme::ApSmeProxy {
        let (proxy, remote) =
            fidl::endpoints::create_proxy().expect("fail to create fidl endpoints");
        let status = await!(wlan_service.get_ap_sme(iface_id, remote)).expect("fail get_ap_sme");
        if status != zx::sys::ZX_OK {
            panic!("fail getting ap sme; status: {}", status);
        }
        proxy
    }

    fn create_wlantap_config(hw_mac_address: [u8; 6]) -> wlantap::WlantapPhyConfig {
        config::create_wlantap_config(hw_mac_address, wlan_device::MacRole::Ap)
    }

    fn create_rx_info() -> wlantap::WlanRxInfo {
        wlantap::WlanRxInfo {
            rx_flags: 0,
            valid_fields: 0,
            phy: 0,
            data_rate: 0,
            chan: fidl_common::WlanChan {
                // TODO(FIDL-54): use clone()
                primary: 11,
                cbw: fidl_common::Cbw::Cbw20,
                secondary80: 0,
            },
            mcs: 0,
            rssi_dbm: 0,
            rcpi_dbmh: 0,
            snr_dbh: 0,
        }
    }
}
