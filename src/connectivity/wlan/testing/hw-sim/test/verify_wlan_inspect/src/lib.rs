// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_wlan_policy as fidl_policy,
    fidl_fuchsia_wlan_tap::{self as wlantap, WlantapPhyProxy},
    fuchsia_inspect::testing::{assert_inspect_tree, AnyProperty},
    fuchsia_inspect_contrib::reader::{ArchiveReader, ComponentSelector},
    fuchsia_inspect_node_hierarchy::{self, NodeHierarchy, Property, PropertyEntry},
    fuchsia_zircon::DurationNum,
    pin_utils::pin_mut,
    selectors,
    std::collections::HashSet,
    wlan_common::{
        assert_variant,
        bss::Protection,
        format::MacFmt,
        mac::{self, Bssid},
    },
    wlan_hw_sim::*,
};

const BSSID: Bssid = Bssid([0x62, 0x73, 0x73, 0x66, 0x6f, 0x6f]);
const SSID: &[u8] = b"open";

#[rustfmt::skip]
const WSC_IE_BODY: &'static [u8] = &[
    0x10, 0x4a, 0x00, 0x01, 0x10, // Version
    0x10, 0x44, 0x00, 0x01, 0x02, // WiFi Protected Setup State
    0x10, 0x57, 0x00, 0x01, 0x01, // AP Setup Locked
    0x10, 0x3b, 0x00, 0x01, 0x03, // Response Type
    // UUID-E
    0x10, 0x47, 0x00, 0x10,
    0x3b, 0x3b, 0xe3, 0x66, 0x80, 0x84, 0x4b, 0x03,
    0xbb, 0x66, 0x45, 0x2a, 0xf3, 0x00, 0x59, 0x22,
    // Manufacturer
    0x10, 0x21, 0x00, 0x15,
    0x41, 0x53, 0x55, 0x53, 0x54, 0x65, 0x6b, 0x20, 0x43, 0x6f, 0x6d, 0x70,
    0x75, 0x74, 0x65, 0x72, 0x20, 0x49, 0x6e, 0x63, 0x2e,
    // Model name
    0x10, 0x23, 0x00, 0x08, 0x52, 0x54, 0x2d, 0x41, 0x43, 0x35, 0x38, 0x55,
    // Model number
    0x10, 0x24, 0x00, 0x03, 0x31, 0x32, 0x33,
    // Serial number
    0x10, 0x42, 0x00, 0x05, 0x31, 0x32, 0x33, 0x34, 0x35,
    // Primary device type
    0x10, 0x54, 0x00, 0x08, 0x00, 0x06, 0x00, 0x50, 0xf2, 0x04, 0x00, 0x01,
    // Device name
    0x10, 0x11, 0x00, 0x0b,
    0x41, 0x53, 0x55, 0x53, 0x20, 0x52, 0x6f, 0x75, 0x74, 0x65, 0x72,
    // Config methods
    0x10, 0x08, 0x00, 0x02, 0x20, 0x0c,
    // Vendor extension
    0x10, 0x49, 0x00, 0x06, 0x00, 0x37, 0x2a, 0x00, 0x01, 0x20,
];

const DISCONNECT_CTX_SELECTOR: &'static str =
    "wlanstack.cmx:root/iface-0/state_events/*/disconnect_ctx";

fn build_event_handler(
    ssid: Vec<u8>,
    bssid: Bssid,
    phy: &WlantapPhyProxy,
) -> impl FnMut(wlantap::WlantapPhyEvent) + '_ {
    EventHandlerBuilder::new()
        .on_tx(MatchTx::new().on_mgmt(move |frame: &Vec<u8>| {
            match mac::MacFrame::parse(&frame[..], false) {
                Some(mac::MacFrame::Mgmt { mgmt_hdr, body, .. }) => {
                    match mac::MgmtBody::parse({ mgmt_hdr.frame_ctrl }.mgmt_subtype(), body) {
                        Some(mac::MgmtBody::Authentication { .. }) => {
                            send_authentication(&CHANNEL, &bssid, &phy)
                                .expect("Error sending fake authentication frame.");
                        }
                        Some(mac::MgmtBody::AssociationReq { .. }) => {
                            send_association_response(
                                &CHANNEL,
                                &bssid,
                                mac::StatusCode::SUCCESS,
                                &phy,
                            )
                            .expect("Error sending fake association response frame");
                        }
                        Some(mac::MgmtBody::ProbeReq { .. }) => {
                            // Normally, the AP would only send probe response on the channel it's
                            // on, but our TestHelper doesn't have that feature yet and it
                            // does not affect this test.
                            send_probe_resp(
                                &CHANNEL,
                                &bssid,
                                &ssid[..],
                                &Protection::Open,
                                Some(WSC_IE_BODY),
                                &phy,
                            )
                            .expect("Error sending fake probe response frame");
                        }
                        _ => (),
                    }
                }
                _ => (),
            }
        }))
        .build()
}

async fn connect_future(
    wlan_controller: &fidl_policy::ClientControllerProxy,
    listener_stream: &mut fidl_policy::ClientStateUpdatesRequestStream,
    security_type: fidl_policy::SecurityType,
    password: Option<&str>,
) {
    save_network_and_connect(wlan_controller, SSID, security_type, password).await;
    assert_connecting(
        listener_stream,
        fidl_policy::NetworkIdentifier { ssid: SSID.to_vec(), type_: security_type },
    )
    .await;
    assert_next_client_listener_update(
        listener_stream,
        vec![fidl_policy::NetworkState {
            id: Some(fidl_policy::NetworkIdentifier { ssid: SSID.to_vec(), type_: security_type }),
            state: Some(fidl_policy::ConnectionState::Connected),
            status: None,
        }],
    )
    .await;
}

fn select_properties(hierarchy: NodeHierarchy, selector: &str) -> Vec<PropertyEntry> {
    let parsed_selector = selectors::parse_selector(selector).expect("expect valid selector.");
    fuchsia_inspect_node_hierarchy::select_from_node_hierarchy(hierarchy, parsed_selector)
        .expect("Selecting from hierarchy should succeed.")
}

/// Test a client can connect to a network with no protection by simulating an AP that sends out
/// hard coded authentication and association response frames.
#[fuchsia_async::run_singlethreaded(test)]
async fn verify_wlan_inspect() {
    init_syslog();

    let mut helper = test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;
    let () = loop_until_iface_is_found().await;

    let (wlan_controller, mut listener_stream) = wlan_hw_sim::init_client_controller().await;
    {
        let connect_fut = connect_future(
            &wlan_controller,
            &mut listener_stream,
            fidl_policy::SecurityType::None,
            None,
        );
        pin_mut!(connect_fut);

        let proxy = helper.proxy();
        let () = helper
            .run_until_complete_or_timeout(
                240.seconds(),
                format!("connecting to {} ({:02X?})", String::from_utf8_lossy(SSID), BSSID),
                build_event_handler(SSID.to_vec(), BSSID, &proxy),
                connect_fut,
            )
            .await;
    }

    let hierarchy = get_inspect_hierarchy().await.expect("expect Inspect data");
    assert_inspect_tree!(hierarchy, root: contains {
        latest_active_client_iface: 0u64,
        device_events: contains {
            "0": contains {},
            "1": contains {},
        },
        "iface-0": contains {
            join_scan_events: contains {
                "0": contains {},
            },
            last_pulse: contains {
                status: contains {
                    status_str: "connected",
                    connected_to: contains {
                        bssid: BSSID.0.to_mac_str(),
                        bssid_hash: AnyProperty,
                        ssid: String::from_utf8_lossy(SSID).to_string(),
                        ssid_hash: AnyProperty,
                        wsc: {
                            device_name: "ASUS Router",
                            manufacturer: "ASUSTek Computer Inc.",
                            model_name: "RT-AC58U",
                            model_number: "123",
                        }
                    }
                }
            },
            state_events: contains {
                "0": contains {},
                "1": contains {},
            },
        }
    });

    let properties = select_properties(hierarchy, DISCONNECT_CTX_SELECTOR);
    assert!(properties.is_empty(), "there should not be a disconnect_ctx yet");

    remove_network(&wlan_controller, SSID, fidl_policy::SecurityType::None, None).await;
    assert_next_client_listener_update(
        &mut listener_stream,
        vec![fidl_policy::NetworkState {
            id: Some(fidl_policy::NetworkIdentifier {
                ssid: SSID.to_vec(),
                type_: fidl_policy::SecurityType::None,
            }),
            state: Some(fidl_policy::ConnectionState::Disconnected),
            status: Some(fidl_policy::DisconnectStatus::ConnectionStopped),
        }],
    )
    .await;

    let hierarchy = get_inspect_hierarchy().await.expect("expect Inspect data");
    assert_inspect_tree!(hierarchy, root: contains {
        latest_active_client_iface: 0u64,
        device_events: contains {
            "0": contains {},
            "1": contains {},
        },
        "iface-0": contains {
            join_scan_events: contains {
                "0": contains {},
            },
            last_pulse: contains {
                status: contains {
                    status_str: "idle",
                }
            },
            state_events: contains {
                "0": contains {},
                "1": contains {},
            },
        }
    });

    let properties = select_properties(hierarchy, DISCONNECT_CTX_SELECTOR);
    let node_paths: HashSet<&str> =
        properties.iter().map(|p| p.property_node_path.as_str()).collect();
    assert_eq!(node_paths.len(), 1, "only one disconnect ctx is expected");
    for p in properties {
        match p.property.key().as_str() {
            "reason_code" => (),
            "locally_initiated" => assert_variant!(p.property, Property::Bool(_, true)),
            _ => panic!("expect `reason_code` or `locally_initiated` property"),
        }
    }

    helper.stop().await;
}

async fn get_inspect_hierarchy() -> Result<NodeHierarchy, Error> {
    ArchiveReader::new()
        .add_selector(ComponentSelector::new(vec!["wlanstack.cmx".to_string()]))
        .get()
        .await?
        .into_iter()
        .next()
        .and_then(|result| result.payload)
        .ok_or(format_err!("expected one inspect hierarchy"))
}
