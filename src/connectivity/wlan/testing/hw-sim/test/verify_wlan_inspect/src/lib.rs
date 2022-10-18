// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    diagnostics_hierarchy::{self, DiagnosticsHierarchy},
    diagnostics_reader::{ArchiveReader, ComponentSelector, Inspect},
    fidl_fuchsia_wlan_ieee80211 as fidl_ieee80211, fidl_fuchsia_wlan_policy as fidl_policy,
    fidl_fuchsia_wlan_tap::{self as wlantap, WlantapPhyProxy},
    fuchsia_inspect::testing::{assert_data_tree, AnyProperty},
    fuchsia_zircon::DurationNum,
    ieee80211::{Bssid, Ssid},
    pin_utils::pin_mut,
    wlan_common::{
        bss::Protection,
        channel::{Cbw, Channel},
        format::MacFmt as _,
        mac,
    },
    wlan_hw_sim::*,
};

const BSSID: Bssid = Bssid([0x62, 0x73, 0x73, 0x66, 0x6f, 0x6f]);

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

// The moniker must match the component name as defined in the manifest
const DEVICEMONITOR_MONIKER: &'static str = "wlandevicemonitor";
const WLANSTACK_MONIKER: &'static str = "wlanstack";
const POLICY_MONIKER: &'static str = "wlancfg";

fn build_event_handler<'a>(
    ssid: &'a Ssid,
    bssid: Bssid,
    phy: &'a WlantapPhyProxy,
) -> impl FnMut(wlantap::WlantapPhyEvent) + 'a {
    EventHandlerBuilder::new()
        .on_start_scan(ScanResults::new(
            phy,
            vec![BeaconInfo {
                channel: Channel::new(1, Cbw::Cbw20),
                bssid,
                ssid: ssid.clone(),
                protection: Protection::Open,
                rssi_dbm: -10,
                beacon_or_probe: BeaconOrProbeResp::ProbeResp { wsc_ie: Some(&WSC_IE_BODY) },
            }],
        ))
        .on_tx(MatchTx::new().on_mgmt(move |frame: &Vec<u8>| {
            match mac::MacFrame::parse(&frame[..], false) {
                Some(mac::MacFrame::Mgmt { mgmt_hdr, body, .. }) => {
                    match mac::MgmtBody::parse({ mgmt_hdr.frame_ctrl }.mgmt_subtype(), body) {
                        Some(mac::MgmtBody::Authentication { .. }) => {
                            send_open_authentication_success(
                                &Channel::new(1, Cbw::Cbw20),
                                &bssid,
                                &phy,
                            )
                            .expect("Error sending fake authentication frame.");
                        }
                        Some(mac::MgmtBody::AssociationReq { .. }) => {
                            send_association_response(
                                &Channel::new(1, Cbw::Cbw20),
                                &bssid,
                                fidl_ieee80211::StatusCode::Success.into(),
                                &phy,
                            )
                            .expect("Error sending fake association response frame");
                        }
                        _ => (),
                    }
                }
                _ => (),
            }
        }))
        .build()
}

/// Test a client can connect to a network with no protection by simulating an AP that sends out
/// hard coded authentication and association response frames.
#[fuchsia_async::run_singlethreaded(test)]
async fn verify_wlan_inspect() {
    init_syslog();

    let mut helper = test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;
    let () = loop_until_iface_is_found(&mut helper).await;

    let (client_controller, mut client_state_update_stream) =
        wlan_hw_sim::init_client_controller().await;
    let security_type = fidl_policy::SecurityType::None;
    {
        let connect_fut = async {
            save_network(
                &client_controller,
                &AP_SSID,
                security_type,
                password_or_psk_to_policy_credential::<String>(None),
            )
            .await;
            let id =
                fidl_policy::NetworkIdentifier { ssid: AP_SSID.to_vec(), type_: security_type };
            wait_until_client_state(&mut client_state_update_stream, |update| {
                has_id_and_state(update, &id, fidl_policy::ConnectionState::Connected)
            })
            .await;
        };
        pin_mut!(connect_fut);

        let proxy = helper.proxy();
        let () = helper
            .run_until_complete_or_timeout(
                240.seconds(),
                format!("connecting to {} ({:02X?})", AP_SSID.to_string_not_redactable(), BSSID),
                build_event_handler(&AP_SSID, BSSID, &proxy),
                connect_fut,
            )
            .await;
    }

    let wlanstack_hierarchy =
        get_inspect_hierarchy(WLANSTACK_MONIKER).await.expect("expect Inspect data");
    assert_data_tree!(wlanstack_hierarchy, root: contains {
        latest_active_client_iface: 0u64,
        device_events: contains {
            "0": contains {},
        },
        "iface-0": contains {
            last_pulse: contains {
                status: contains {
                    status_str: "connected",
                    connected_to: contains {
                        bssid: BSSID.0.to_mac_string(),
                        bssid_hash: AnyProperty,
                        ssid: AP_SSID.to_string(),
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

    let policy_hierarchy =
        get_inspect_hierarchy(POLICY_MONIKER).await.expect("expect Inspect data");
    assert_data_tree!(policy_hierarchy, root: contains {
        external: {
            client_stats: contains {
                disconnect_events: {},
                connection_status: contains {
                    connected_network: {
                        rssi_dbm: AnyProperty,
                        snr_db: AnyProperty,
                        wsc: {
                            device_name: "ASUS Router",
                            manufacturer: "ASUSTek Computer Inc.",
                            model_name: "RT-AC58U",
                            model_number: "123",
                        }
                    }
                }
            },
        },
    });

    let monitor_hierarchy =
        get_inspect_hierarchy(DEVICEMONITOR_MONIKER).await.expect("expect Inspect data");
    assert_data_tree!(monitor_hierarchy, root: contains {
        device_events: contains {
            "0": contains {},
        },
    });

    remove_network(
        &client_controller,
        &AP_SSID,
        fidl_policy::SecurityType::None,
        password_or_psk_to_policy_credential::<String>(None),
    )
    .await;

    let id = fidl_policy::NetworkIdentifier { ssid: AP_SSID.to_vec(), type_: security_type };
    wait_until_client_state(&mut client_state_update_stream, |update| {
        has_id_and_state(update, &id, fidl_policy::ConnectionState::Disconnected)
    })
    .await;

    let wlanstack_hierarchy =
        get_inspect_hierarchy(WLANSTACK_MONIKER).await.expect("expect Inspect data");
    assert_data_tree!(wlanstack_hierarchy, root: contains {
        latest_active_client_iface: 0u64,
        device_events: contains {
            "0": contains {},
        },
        "iface-0": contains {
            last_pulse: contains {
                status: contains {
                    status_str: "idle",
                    prev_connected_to: contains {
                        bssid: BSSID.0.to_mac_string(),
                        bssid_hash: AnyProperty,
                        ssid: AP_SSID.to_string(),
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

    let policy_hierarchy =
        get_inspect_hierarchy(POLICY_MONIKER).await.expect("expect Inspect data");
    assert_data_tree!(policy_hierarchy, root: contains {
        external: {
            client_stats: contains {
                disconnect_events: {
                    "0": {
                        "@time": AnyProperty,
                        network: {
                            channel: {
                                primary: 1u64,
                            },
                        },
                        flattened_reason_code: AnyProperty,
                        locally_initiated: true,
                    }
                },
                connection_status: contains {},
            }
        },
    });

    let monitor_hierarchy =
        get_inspect_hierarchy(DEVICEMONITOR_MONIKER).await.expect("expect Inspect data");
    assert_data_tree!(monitor_hierarchy, root: contains {
        device_events: contains {
            "0": contains {},
        },
    });

    helper.stop().await;
}

async fn get_inspect_hierarchy(component: &str) -> Result<DiagnosticsHierarchy, Error> {
    ArchiveReader::new()
        .add_selector(ComponentSelector::new(vec![component.to_string()]))
        .snapshot::<Inspect>()
        .await?
        .into_iter()
        .next()
        .and_then(|result| result.payload)
        .ok_or(format_err!("expected one inspect hierarchy"))
}
