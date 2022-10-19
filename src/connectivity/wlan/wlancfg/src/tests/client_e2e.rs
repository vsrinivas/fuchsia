// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        client::{network_selection, scan, serve_provider_requests, types},
        config_management::{SavedNetworksManager, SavedNetworksManagerApi},
        legacy,
        mode_management::{
            create_iface_manager, device_monitor,
            iface_manager_api::IfaceManagerApi,
            phy_manager::{PhyManager, PhyManagerApi},
        },
        telemetry::{TelemetryEvent, TelemetrySender},
        util::listener,
        util::testing::{
            create_inspect_persistence_channel, create_wlan_hasher,
            sme_stream::poll_for_and_validate_sme_scan_request_and_send_results,
        },
    },
    anyhow::{format_err, Error},
    fidl::endpoints::{create_proxy, create_request_stream},
    fidl_fuchsia_stash as fidl_stash, fidl_fuchsia_wlan_common as fidl_common,
    fidl_fuchsia_wlan_common_security as fidl_common_security,
    fidl_fuchsia_wlan_device_service::DeviceWatcherEvent,
    fidl_fuchsia_wlan_policy as fidl_policy, fidl_fuchsia_wlan_sme as fidl_sme,
    fuchsia_async as fasync,
    fuchsia_async::TestExecutor,
    fuchsia_inspect::{self as inspect},
    fuchsia_zircon as zx,
    futures::{
        channel::mpsc,
        future::{join_all, JoinAll},
        lock::Mutex,
        prelude::*,
        stream::StreamExt,
        task::Poll,
    },
    hex,
    lazy_static::lazy_static,
    log::info,
    pin_utils::pin_mut,
    std::{
        convert::{Infallible, TryFrom},
        pin::Pin,
        sync::Arc,
    },
    test_case::test_case,
    wlan_common::{assert_variant, random_fidl_bss_description},
};

pub const TEST_CLIENT_IFACE_ID: u16 = 42;
pub const TEST_PHY_ID: u16 = 41;
lazy_static! {
    pub static ref TEST_SSID: types::Ssid = types::Ssid::try_from("test_ssid").unwrap();
}

#[derive(Clone)]
pub struct TestCredentials {
    policy: fidl_policy::Credential,
    sme: Option<Box<fidl_common_security::Credentials>>,
}
pub struct TestCredentialVariants {
    pub none: TestCredentials,
    pub wep_64_hex: TestCredentials,
    pub wep_64_ascii: TestCredentials,
    pub wep_128_hex: TestCredentials,
    pub wep_128_ascii: TestCredentials,
    pub wpa_pass_min: TestCredentials,
    pub wpa_pass_max: TestCredentials,
    pub wpa_psk: TestCredentials,
}

lazy_static! {
    pub static ref TEST_CREDS: TestCredentialVariants = TestCredentialVariants {
        none: TestCredentials {
            policy: fidl_policy::Credential::None(fidl_policy::Empty),
            sme: None
        },
        wep_64_hex: TestCredentials {
            policy: fidl_policy::Credential::Password(b"7465737431".to_vec()),
            sme: Some(Box::new(fidl_common_security::Credentials::Wep(
                fidl_common_security::WepCredentials { key: b"test1".to_vec() }
            )))
        },
        wep_64_ascii: TestCredentials {
            policy: fidl_policy::Credential::Password(b"test1".to_vec()),
            sme: Some(Box::new(fidl_common_security::Credentials::Wep(
                fidl_common_security::WepCredentials { key: b"test1".to_vec() }
            )))
        },
        wep_128_hex: TestCredentials {
            policy: fidl_policy::Credential::Password(b"74657374317465737432333435".to_vec()),
            sme: Some(Box::new(fidl_common_security::Credentials::Wep(
                fidl_common_security::WepCredentials { key: b"test1test2345".to_vec() }
            )))
        },
        wep_128_ascii: TestCredentials {
            policy: fidl_policy::Credential::Password(b"test1test2345".to_vec()),
            sme: Some(Box::new(fidl_common_security::Credentials::Wep(
                fidl_common_security::WepCredentials { key: b"test1test2345".to_vec() }
            )))
        },
        wpa_pass_min: TestCredentials {
            policy: fidl_policy::Credential::Password(b"eight111".to_vec()),
            sme: Some(Box::new(fidl_common_security::Credentials::Wpa(
                fidl_common_security::WpaCredentials::Passphrase(b"eight111".to_vec())
            )))
        },
        wpa_pass_max: TestCredentials {
            policy: fidl_policy::Credential::Password(
                b"thisIs63CharactersLong!!!#$#%thisIs63CharactersLong!!!#$#%00009".to_vec()
            ),
            sme: Some(Box::new(fidl_common_security::Credentials::Wpa(
                fidl_common_security::WpaCredentials::Passphrase(
                    b"thisIs63CharactersLong!!!#$#%thisIs63CharactersLong!!!#$#%00009".to_vec()
                )
            )))
        },
        wpa_psk: TestCredentials {
            policy: fidl_policy::Credential::Psk(
                hex::decode(b"f10aedbb0ea29c928b06997ed305a697706ddad220ff5a98f252558a470a748f")
                    .unwrap()
            ),
            sme: Some(Box::new(fidl_common_security::Credentials::Wpa(
                fidl_common_security::WpaCredentials::Psk(
                    hex::decode(
                        b"f10aedbb0ea29c928b06997ed305a697706ddad220ff5a98f252558a470a748f"
                    )
                    .unwrap()
                    .try_into()
                    .unwrap()
                )
            )))
        }
    };
}

struct TestValues {
    internal_objects: InternalObjects,
    external_interfaces: ExternalInterfaces,
}

// Internal policy objects, used for manipulating state within tests
#[allow(clippy::type_complexity)]
struct InternalObjects {
    internal_futures: JoinAll<Pin<Box<dyn Future<Output = Result<Infallible, Error>>>>>,
    _saved_networks: Arc<dyn SavedNetworksManagerApi>,
    phy_manager: Arc<Mutex<dyn PhyManagerApi + Send>>,
    iface_manager: Arc<Mutex<dyn IfaceManagerApi + Send>>,
}

struct ExternalInterfaces {
    monitor_service_proxy: fidl_fuchsia_wlan_device_service::DeviceMonitorProxy,
    monitor_service_stream: fidl_fuchsia_wlan_device_service::DeviceMonitorRequestStream,
    stash_server: fidl_stash::StoreAccessorRequestStream,
    client_provider_proxy: fidl_policy::ClientProviderProxy,
    _telemetry_receiver: mpsc::Receiver<TelemetryEvent>,
}

// setup channels and proxies needed for the tests
fn test_setup(exec: &mut TestExecutor) -> TestValues {
    let (monitor_service_proxy, monitor_service_requests) =
        create_proxy::<fidl_fuchsia_wlan_device_service::DeviceMonitorMarker>()
            .expect("failed to create SeviceService proxy");
    let monitor_service_stream =
        monitor_service_requests.into_stream().expect("failed to create stream");

    let (saved_networks, stash_server) =
        exec.run_singlethreaded(SavedNetworksManager::new_and_stash_server());
    let saved_networks = Arc::new(saved_networks);
    let (persistence_req_sender, _persistence_stream) = create_inspect_persistence_channel();
    let (telemetry_sender, telemetry_receiver) = mpsc::channel::<TelemetryEvent>(100);
    let telemetry_sender = TelemetrySender::new(telemetry_sender);
    let (scan_request_sender, scan_request_receiver) =
        mpsc::channel::<scan::ScanRequest>(scan::SCAN_REQUEST_BUFFER_SIZE);
    let scan_requester = Arc::new(scan::ScanRequester { sender: scan_request_sender });
    let network_selector = Arc::new(network_selection::NetworkSelector::new(
        saved_networks.clone(),
        scan_requester.clone(),
        create_wlan_hasher(),
        inspect::Inspector::new().root().create_child("network_selector"),
        persistence_req_sender,
        telemetry_sender.clone(),
    ));
    let (client_provider_proxy, client_provider_requests) =
        create_proxy::<fidl_policy::ClientProviderMarker>()
            .expect("failed to create ClientProvider proxy");
    let client_provider_requests =
        client_provider_requests.into_stream().expect("failed to create stream");

    let (client_update_sender, client_update_receiver) = mpsc::unbounded();
    let (ap_update_sender, _ap_update_receiver) = mpsc::unbounded();

    let phy_manager = Arc::new(Mutex::new(PhyManager::new(
        monitor_service_proxy.clone(),
        inspect::Inspector::new().root().create_child("phy_manager"),
        telemetry_sender.clone(),
    )));
    let (iface_manager, iface_manager_service) = create_iface_manager(
        phy_manager.clone(),
        client_update_sender.clone(),
        ap_update_sender.clone(),
        monitor_service_proxy.clone(),
        saved_networks.clone(),
        network_selector.clone(),
        telemetry_sender.clone(),
    );
    let iface_manager_service = Box::pin(iface_manager_service);
    let scan_manager_service = Box::pin(
        scan::serve_scanning_loop(
            iface_manager.clone(),
            saved_networks.clone(),
            telemetry_sender.clone(),
            scan_request_receiver,
        )
        // Map the output type of this future to match the other ones we want to combine with it
        .map(|_| {
            let result: Result<Infallible, Error> =
                Err(format_err!("scan_manager_service future exited unexpectedly"));
            result
        }),
    );

    let client_provider_lock = Arc::new(Mutex::new(()));

    let serve_fut: Pin<Box<dyn Future<Output = Result<Infallible, Error>>>> = Box::pin(
        serve_provider_requests(
            iface_manager.clone(),
            client_update_sender,
            saved_networks.clone(),
            scan_requester,
            client_provider_lock,
            client_provider_requests,
            telemetry_sender,
        )
        // Map the output type of this future to match the other ones we want to combine with it
        .map(|_| {
            let result: Result<Infallible, Error> =
                Err(format_err!("serve_provider_requests future exited unexpectedly"));
            result
        }),
    );

    let serve_client_policy_listeners = Box::pin(
        listener::serve::<
            fidl_policy::ClientStateUpdatesProxy,
            fidl_policy::ClientStateSummary,
            listener::ClientStateUpdate,
        >(client_update_receiver)
        // Map the output type of this future to match the other ones we want to combine with it
        .map(|_| {
            let result: Result<Infallible, Error> =
                Err(format_err!("serve_client_policy_listeners future exited unexpectedly"));
            result
        })
        .fuse(),
    );

    // Combine all our "internal" futures into one, since we don't care about their individual progress
    let internal_futures = join_all(vec![
        serve_fut,
        iface_manager_service,
        scan_manager_service,
        serve_client_policy_listeners,
    ]);

    let internal_objects = InternalObjects {
        internal_futures,
        _saved_networks: saved_networks,
        phy_manager,
        iface_manager,
    };

    let external_interfaces = ExternalInterfaces {
        monitor_service_proxy,
        monitor_service_stream,
        client_provider_proxy,
        stash_server,
        _telemetry_receiver: telemetry_receiver,
    };

    TestValues { internal_objects, external_interfaces }
}

fn add_phy(exec: &mut TestExecutor, test_values: &mut TestValues) {
    // Use the "legacy" module to mimic the wlancfg main module. When the main module
    // is refactored to remove the "legacy" module, we can also refactor this section.
    let legacy_client = legacy::IfaceRef::new();
    let listener = device_monitor::Listener::new(
        test_values.external_interfaces.monitor_service_proxy.clone(),
        legacy_client.clone(),
        test_values.internal_objects.phy_manager.clone(),
        test_values.internal_objects.iface_manager.clone(),
    );
    let add_phy_event = DeviceWatcherEvent::OnPhyAdded { phy_id: TEST_PHY_ID };
    let add_phy_fut = device_monitor::handle_event(&listener, add_phy_event);
    pin_mut!(add_phy_fut);
    assert_variant!(exec.run_until_stalled(&mut add_phy_fut), Poll::Pending);

    assert_variant!(
        exec.run_until_stalled(&mut test_values.external_interfaces.monitor_service_stream.next()),
        Poll::Ready(Some(Ok(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::GetSupportedMacRoles {
            phy_id: TEST_PHY_ID, responder
        }))) => {
            // Send back a positive acknowledgement.
            assert!(responder.send(&mut Ok(vec![fidl_common::WlanMacRole::Client])).is_ok());
        }
    );
    assert_variant!(exec.run_until_stalled(&mut add_phy_fut), Poll::Ready(()));
}

fn security_support_with_wpa3() -> fidl_common::SecuritySupport {
    fidl_common::SecuritySupport {
        mfp: fidl_common::MfpFeature { supported: true },
        sae: fidl_common::SaeFeature {
            driver_handler_supported: true,
            sme_handler_supported: true,
        },
    }
}

/// Adds a phy and prepares client interfaces by turning on client connections
fn prepare_client_interface(
    exec: &mut TestExecutor,
    controller: &fidl_policy::ClientControllerProxy,
    test_values: &mut TestValues,
) -> fidl_sme::ClientSmeRequestStream {
    // Add the phy
    add_phy(exec, test_values);

    // Use the Policy API to start client connections
    let start_connections_fut = controller.start_client_connections();
    pin_mut!(start_connections_fut);
    assert_variant!(exec.run_until_stalled(&mut start_connections_fut), Poll::Pending);
    assert_variant!(
        exec.run_until_stalled(&mut test_values.internal_objects.internal_futures),
        Poll::Pending
    );

    // Expect an interface creation request
    assert_variant!(
        exec.run_until_stalled(&mut test_values.external_interfaces.monitor_service_stream.next()),
        Poll::Ready(Some(Ok(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::CreateIface {
            req: fidl_fuchsia_wlan_device_service::CreateIfaceRequest {
                phy_id: TEST_PHY_ID, role: fidl_common::WlanMacRole::Client, sta_addr: [0, 0, 0, 0, 0, 0]
            },
            responder
        }))) => {
            assert!(responder.send(
                zx::sys::ZX_OK,
                Some(&mut fidl_fuchsia_wlan_device_service::CreateIfaceResponse {iface_id: TEST_CLIENT_IFACE_ID})
            ).is_ok());
        }
    );

    assert_variant!(
        exec.run_until_stalled(&mut test_values.internal_objects.internal_futures),
        Poll::Pending
    );

    // Expect a feature support query as part of the interface creation
    assert_variant!(
        exec.run_until_stalled(&mut test_values.external_interfaces.monitor_service_stream.next()),
        Poll::Ready(Some(Ok(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::GetFeatureSupport {
            iface_id: TEST_CLIENT_IFACE_ID, feature_support_server, responder
        }))) => {
            assert!(responder.send(&mut Ok(())).is_ok());

            let (mut stream, _handle) = feature_support_server.into_stream_and_control_handle().unwrap();
            assert_variant!(
                exec.run_until_stalled(&mut test_values.internal_objects.internal_futures),
                Poll::Pending
            );

            // Send back feature support information
            assert_variant!(
                exec.run_until_stalled(&mut stream.next()),
                Poll::Ready(Some(Ok(fidl_sme::FeatureSupportRequest::QuerySecuritySupport {
                    responder
                }))) => {
                    assert!(responder.send(
                        &mut fidl_sme::FeatureSupportQuerySecuritySupportResult::Ok(
                            security_support_with_wpa3()
                        )
                    ).is_ok());
                }
            );

        }
    );

    assert_variant!(
        exec.run_until_stalled(&mut test_values.internal_objects.internal_futures),
        Poll::Pending
    );

    // Expect an interface query and notify that this is a client interface.
    assert_variant!(
        exec.run_until_stalled(&mut test_values.external_interfaces.monitor_service_stream.next()),
        Poll::Ready(Some(Ok(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::QueryIface {
            iface_id: TEST_CLIENT_IFACE_ID, responder
        }))) => {
            let response = fidl_fuchsia_wlan_device_service::QueryIfaceResponse {
                role: fidl_fuchsia_wlan_common::WlanMacRole::Client,
                id: TEST_CLIENT_IFACE_ID,
                phy_id: 0,
                phy_assigned_id: 0,
                sta_addr: [0; 6],
            };
            responder
                .send(&mut Ok(response))
                .expect("Sending iface response");
        }
    );

    assert_variant!(
        exec.run_until_stalled(&mut test_values.internal_objects.internal_futures),
        Poll::Pending
    );

    // Expect that we have requested a client SME proxy as part of interface creation
    let sme_server = assert_variant!(
        exec.run_until_stalled(&mut test_values.external_interfaces.monitor_service_stream.next()),
            Poll::Ready(Some(Ok(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::GetClientSme {
            iface_id: TEST_CLIENT_IFACE_ID, sme_server, responder
        }))) => {
            // Send back a positive acknowledgement.
            assert!(responder.send(&mut Ok(())).is_ok());
            sme_server
        }
    );

    let iface_sme_stream =
        sme_server.into_stream().expect("failed to create ClientSmeRequestStream");

    assert_variant!(
        exec.run_until_stalled(&mut test_values.internal_objects.internal_futures),
        Poll::Pending
    );

    // There will be another security support query as part of adding the interface to iface_manager
    assert_variant!(
        exec.run_until_stalled(&mut test_values.external_interfaces.monitor_service_stream.next()),
        Poll::Ready(Some(Ok(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::GetFeatureSupport {
            iface_id: TEST_CLIENT_IFACE_ID, feature_support_server, responder
        }))) => {
            assert!(responder.send(&mut Ok(())).is_ok());

            let (mut stream, _handle) = feature_support_server.into_stream_and_control_handle().unwrap();
            assert_variant!(
                exec.run_until_stalled(&mut test_values.internal_objects.internal_futures),
                Poll::Pending
            );

            // Send back feature support information
            assert_variant!(
                exec.run_until_stalled(&mut stream.next()),
                Poll::Ready(Some(Ok(fidl_sme::FeatureSupportRequest::QuerySecuritySupport {
                    responder
                }))) => {
                    assert!(responder.send(
                        &mut fidl_sme::FeatureSupportQuerySecuritySupportResult::Ok(
                            security_support_with_wpa3()
                        )
                    ).is_ok());
                }
            );

        }
    );

    assert_variant!(
        exec.run_until_stalled(&mut test_values.internal_objects.internal_futures),
        Poll::Pending
    );

    // Expect to get an SME request for the state machine creation
    let sme_server = assert_variant!(
        exec.run_until_stalled(&mut test_values.external_interfaces.monitor_service_stream.next()),
            Poll::Ready(Some(Ok(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::GetClientSme {
            iface_id: TEST_CLIENT_IFACE_ID, sme_server, responder
        }))) => {
            // Send back a positive acknowledgement.
            assert!(responder.send(&mut Ok(())).is_ok());
            sme_server
        }
    );
    let mut sme_stream = sme_server.into_stream().expect("failed to create ClientSmeRequestStream");

    assert_variant!(
        exec.run_until_stalled(&mut test_values.internal_objects.internal_futures),
        Poll::Pending
    );

    // State machine does an initial disconnect
    assert_variant!(
        exec.run_until_stalled(&mut sme_stream.next()),
        Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Disconnect {
            reason, responder
        }))) => {
            assert_eq!(fidl_sme::UserDisconnectReason::Startup, reason);
            assert!(responder.send().is_ok());
        }
    );

    assert_variant!(
        exec.run_until_stalled(&mut test_values.internal_objects.internal_futures),
        Poll::Pending
    );

    assert_variant!(
        exec.run_until_stalled(&mut start_connections_fut),
        Poll::Ready(Ok(fidl_common::RequestStatus::Acknowledged))
    );

    iface_sme_stream
}

fn request_controller(
    provider: &fidl_policy::ClientProviderProxy,
) -> (fidl_policy::ClientControllerProxy, fidl_policy::ClientStateUpdatesRequestStream) {
    let (controller, requests) = create_proxy::<fidl_policy::ClientControllerMarker>()
        .expect("failed to create ClientController proxy");
    let (update_sink, update_stream) =
        create_request_stream::<fidl_policy::ClientStateUpdatesMarker>()
            .expect("failed to create ClientStateUpdates proxy");
    provider.get_controller(requests, update_sink).expect("error getting controller");
    (controller, update_stream)
}

#[track_caller]
fn process_stash_write(
    exec: &mut fasync::TestExecutor,
    stash_server: &mut fidl_stash::StoreAccessorRequestStream,
) {
    assert_variant!(
        exec.run_until_stalled(&mut stash_server.try_next()),
        Poll::Ready(Ok(Some(fidl_stash::StoreAccessorRequest::SetValue { .. })))
    );
    assert_variant!(
        exec.run_until_stalled(&mut stash_server.try_next()),
        Poll::Ready(Ok(Some(fidl_stash::StoreAccessorRequest::Flush{responder}))) => {
            responder.send(&mut Ok(())).expect("failed to send stash response");
        }
    );
    info!("finished stash writing")
}

#[track_caller]
fn get_client_state_update(
    exec: &mut TestExecutor,
    client_listener_update_requests: &mut fidl_policy::ClientStateUpdatesRequestStream,
    // test_values: &mut TestValues,
) -> fidl_policy::ClientStateSummary {
    let update_request = assert_variant!(
        exec.run_until_stalled(&mut client_listener_update_requests.next()),
        Poll::Ready(Some(Ok(update_request))) => {
            update_request
        }
    );
    let (update, responder) = update_request.into_on_client_state_update().unwrap();
    let _ = responder.send();
    update
}

/// Gets a set of security protocols that describe the protection of a BSS.
///
/// This function does **not** consider hardware and driver support. Returns an empty `Vec` if
/// there are no corresponding security protocols for the given BSS protection.
fn security_protocols_from_protection(
    protection: fidl_sme::Protection,
) -> Vec<fidl_common_security::Protocol> {
    use fidl_sme::Protection::*;

    match protection {
        Open => vec![fidl_common_security::Protocol::Open],
        Wep => vec![fidl_common_security::Protocol::Wep],
        Wpa1 => vec![fidl_common_security::Protocol::Wpa1],
        Wpa1Wpa2PersonalTkipOnly | Wpa1Wpa2Personal => {
            vec![fidl_common_security::Protocol::Wpa2Personal, fidl_common_security::Protocol::Wpa1]
        }
        Wpa2PersonalTkipOnly | Wpa2Personal => vec![fidl_common_security::Protocol::Wpa2Personal],
        Wpa2Wpa3Personal => vec![
            fidl_common_security::Protocol::Wpa3Personal,
            fidl_common_security::Protocol::Wpa2Personal,
        ],
        Wpa3Personal => vec![fidl_common_security::Protocol::Wpa3Personal],
        Wpa2Enterprise | Wpa3Enterprise | Unknown => vec![],
    }
}

use fidl_policy::SecurityType as Saved;
use fidl_sme::Protection as Scanned;
#[test_case(Saved::None, Scanned::Open, TEST_CREDS.none.clone())]
// Saved credential: WEP 40/64 bit
#[test_case(Saved::Wep, Scanned::Wep, TEST_CREDS.wep_64_ascii.clone())]
#[test_case(Saved::Wep, Scanned::Wep, TEST_CREDS.wep_64_hex.clone())]
// Saved credential: WEP 104/128 bit
#[test_case(Saved::Wep, Scanned::Wep, TEST_CREDS.wep_128_ascii.clone())]
#[test_case(Saved::Wep, Scanned::Wep, TEST_CREDS.wep_128_hex.clone())]
// Saved credential: WPA1
#[test_case(Saved::Wpa, Scanned::Wpa1, TEST_CREDS.wpa_pass_min.clone())]
#[test_case(Saved::Wpa, Scanned::Wpa1, TEST_CREDS.wpa_pass_max.clone())]
#[test_case(Saved::Wpa, Scanned::Wpa1, TEST_CREDS.wpa_psk.clone())]
#[test_case(Saved::Wpa, Scanned::Wpa1Wpa2Personal, TEST_CREDS.wpa_pass_min.clone())]
#[test_case(Saved::Wpa, Scanned::Wpa1Wpa2Personal, TEST_CREDS.wpa_pass_max.clone())]
#[test_case(Saved::Wpa, Scanned::Wpa1Wpa2Personal, TEST_CREDS.wpa_psk.clone())]
#[test_case(Saved::Wpa, Scanned::Wpa1Wpa2PersonalTkipOnly, TEST_CREDS.wpa_pass_min.clone())]
#[test_case(Saved::Wpa, Scanned::Wpa1Wpa2PersonalTkipOnly, TEST_CREDS.wpa_pass_max.clone())]
#[test_case(Saved::Wpa, Scanned::Wpa1Wpa2PersonalTkipOnly, TEST_CREDS.wpa_psk.clone())]
#[test_case(Saved::Wpa, Scanned::Wpa2Personal, TEST_CREDS.wpa_pass_min.clone())]
#[test_case(Saved::Wpa, Scanned::Wpa2Personal, TEST_CREDS.wpa_pass_max.clone())]
#[test_case(Saved::Wpa, Scanned::Wpa2Personal, TEST_CREDS.wpa_psk.clone())]
#[test_case(Saved::Wpa, Scanned::Wpa2PersonalTkipOnly, TEST_CREDS.wpa_pass_min.clone())]
#[test_case(Saved::Wpa, Scanned::Wpa2PersonalTkipOnly, TEST_CREDS.wpa_pass_max.clone())]
#[test_case(Saved::Wpa, Scanned::Wpa2PersonalTkipOnly, TEST_CREDS.wpa_psk.clone())]
// TODO(fxbug.dev/85817): reenable credential upgrading
// #[test_case(Saved::Wpa, Scanned::Wpa2Wpa3Personal, TEST_CREDS.wpa_pass_min.clone())]
// #[test_case(Saved::Wpa, Scanned::Wpa2Wpa3Personal, TEST_CREDS.wpa_pass_max.clone())]
// #[test_case(Saved::Wpa, Scanned::Wpa2Wpa3Personal, TEST_CREDS.wpa_psk.clone())]
// #[test_case(Saved::Wpa, Scanned::Wpa3Personal, TEST_CREDS.wpa_pass_min.clone())]
// #[test_case(Saved::Wpa, Scanned::Wpa3Personal, TEST_CREDS.wpa_pass_max.clone())]
// Saved credential: WPA2
#[test_case(Saved::Wpa2, Scanned::Wpa2Personal, TEST_CREDS.wpa_pass_min.clone())]
#[test_case(Saved::Wpa2, Scanned::Wpa2Personal, TEST_CREDS.wpa_pass_max.clone())]
#[test_case(Saved::Wpa2, Scanned::Wpa2Personal, TEST_CREDS.wpa_psk.clone())]
#[test_case(Saved::Wpa2, Scanned::Wpa2PersonalTkipOnly, TEST_CREDS.wpa_pass_min.clone())]
#[test_case(Saved::Wpa2, Scanned::Wpa2PersonalTkipOnly, TEST_CREDS.wpa_pass_max.clone())]
#[test_case(Saved::Wpa2, Scanned::Wpa2PersonalTkipOnly, TEST_CREDS.wpa_psk.clone())]
#[test_case(Saved::Wpa2, Scanned::Wpa2Wpa3Personal, TEST_CREDS.wpa_pass_min.clone())]
#[test_case(Saved::Wpa2, Scanned::Wpa2Wpa3Personal, TEST_CREDS.wpa_pass_max.clone())]
#[test_case(Saved::Wpa2, Scanned::Wpa2Wpa3Personal, TEST_CREDS.wpa_psk.clone())]
#[test_case(Saved::Wpa2, Scanned::Wpa3Personal, TEST_CREDS.wpa_pass_min.clone())]
#[test_case(Saved::Wpa2, Scanned::Wpa3Personal, TEST_CREDS.wpa_pass_max.clone())]
// Saved credential: WPA3
#[test_case(Saved::Wpa3, Scanned::Wpa2Wpa3Personal, TEST_CREDS.wpa_pass_min.clone())]
#[test_case(Saved::Wpa3, Scanned::Wpa2Wpa3Personal, TEST_CREDS.wpa_pass_max.clone())]
#[test_case(Saved::Wpa3, Scanned::Wpa3Personal, TEST_CREDS.wpa_pass_min.clone())]
#[test_case(Saved::Wpa3, Scanned::Wpa3Personal, TEST_CREDS.wpa_pass_max.clone())]
#[fuchsia::test(add_test_attr = false)]
/// Tests saving and connecting across various security types
fn save_and_connect(
    saved_security: fidl_policy::SecurityType,
    scanned_security: fidl_sme::Protection,
    test_credentials: TestCredentials,
) {
    let saved_credential = test_credentials.policy.clone();
    let expected_credential = test_credentials.sme.clone();

    let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
    let mut test_values = test_setup(&mut exec);

    // No request has been sent yet. Future should be idle.
    assert_variant!(
        exec.run_until_stalled(&mut test_values.internal_objects.internal_futures),
        Poll::Pending
    );

    // Request a new controller.
    let (controller, mut client_listener_update_requests) =
        request_controller(&test_values.external_interfaces.client_provider_proxy);
    assert_variant!(
        exec.run_until_stalled(&mut test_values.internal_objects.internal_futures),
        Poll::Pending
    );

    // Initial update should reflect client connections are disabled
    let fidl_policy::ClientStateSummary { state, networks, .. } =
        get_client_state_update(&mut exec, &mut client_listener_update_requests);
    assert_eq!(state.unwrap(), fidl_policy::WlanClientState::ConnectionsDisabled);
    assert_eq!(networks.unwrap().len(), 0);

    // Get ready for client connections
    let mut iface_sme_stream = prepare_client_interface(&mut exec, &controller, &mut test_values);

    // Check for a listener update saying client connections are enabled
    let fidl_policy::ClientStateSummary { state, networks, .. } =
        get_client_state_update(&mut exec, &mut client_listener_update_requests);
    assert_eq!(state.unwrap(), fidl_policy::WlanClientState::ConnectionsEnabled);
    assert_eq!(networks.unwrap().len(), 0);

    // Generate network ID
    let network_id =
        fidl_policy::NetworkIdentifier { ssid: TEST_SSID.clone().into(), type_: saved_security };
    let network_config = fidl_policy::NetworkConfig {
        id: Some(network_id.clone()),
        credential: Some(saved_credential),
        ..fidl_policy::NetworkConfig::EMPTY
    };

    // Save the network
    let save_fut = controller.save_network(network_config);
    pin_mut!(save_fut);

    // Begin processing the save request
    assert_variant!(
        exec.run_until_stalled(&mut test_values.internal_objects.internal_futures),
        Poll::Pending
    );

    // Process the stash write from the save
    process_stash_write(&mut exec, &mut test_values.external_interfaces.stash_server);

    // Continue processing the save request. Auto-connection process starts
    assert_variant!(
        exec.run_until_stalled(&mut test_values.internal_objects.internal_futures),
        Poll::Pending
    );

    // Save request returns once the scan has been queued.
    assert_variant!(exec.run_until_stalled(&mut save_fut), Poll::Ready(Ok(Ok(()))));

    // Check for a listener update saying we're connecting
    let fidl_policy::ClientStateSummary { state, networks, .. } =
        get_client_state_update(&mut exec, &mut client_listener_update_requests);
    assert_eq!(state.unwrap(), fidl_policy::WlanClientState::ConnectionsEnabled);
    let mut networks = networks.unwrap();
    assert_eq!(networks.len(), 1);
    let network = networks.pop().unwrap();
    assert_eq!(network.id.unwrap(), network_id.clone());
    assert_eq!(network.state.unwrap(), types::ConnectionState::Connecting);

    // BSS selection scans occur for requested network. Return scan results.
    let expected_scan_request = fidl_sme::ScanRequest::Active(fidl_sme::ActiveScanRequest {
        ssids: vec![TEST_SSID.clone().into()],
        channels: vec![],
    });
    let mutual_security_protocols = security_protocols_from_protection(scanned_security);
    assert!(!mutual_security_protocols.is_empty(), "no mutual security protocols");
    let mock_scan_results = vec![fidl_sme::ScanResult {
        compatibility: Some(Box::new(fidl_sme::Compatibility { mutual_security_protocols })),
        timestamp_nanos: zx::Time::get_monotonic().into_nanos(),
        bss_description: random_fidl_bss_description!(
            protection =>  wlan_common::test_utils::fake_stas::FakeProtectionCfg::from(scanned_security),
            bssid: [0, 0, 0, 0, 0, 0],
            ssid: TEST_SSID.clone(),
            rssi_dbm: 10,
            snr_db: 10,
            channel: types::WlanChan::new(1, types::Cbw::Cbw20),
        ),
    }];
    poll_for_and_validate_sme_scan_request_and_send_results(
        &mut exec,
        &mut test_values.internal_objects.internal_futures,
        &mut iface_sme_stream,
        &expected_scan_request,
        mock_scan_results,
    );

    assert_variant!(
        exec.run_until_stalled(&mut test_values.internal_objects.internal_futures),
        Poll::Pending
    );

    // Expect to get an SME request for state machine creation.
    let sme_server = assert_variant!(
        exec.run_until_stalled(&mut test_values.external_interfaces.monitor_service_stream.next()),
            Poll::Ready(Some(Ok(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::GetClientSme {
            iface_id: TEST_CLIENT_IFACE_ID, sme_server, responder
        }))) => {
            // Send back a positive acknowledgement.
            assert!(responder.send(&mut Ok(())).is_ok());
            sme_server
        }
    );
    let mut sme_stream = sme_server.into_stream().expect("failed to create ClientSmeRequestStream");

    assert_variant!(
        exec.run_until_stalled(&mut test_values.internal_objects.internal_futures),
        Poll::Pending
    );

    // State machine does an initial disconnect. Ack.
    assert_variant!(
        exec.run_until_stalled(&mut sme_stream.next()),
        Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Disconnect {
            reason, responder
        }))) => {
            assert_eq!(fidl_sme::UserDisconnectReason::Startup, reason);
            assert!(responder.send().is_ok());
        }
    );

    assert_variant!(
        exec.run_until_stalled(&mut test_values.internal_objects.internal_futures),
        Poll::Pending
    );

    // State machine connects
    let connect_txn_handle = assert_variant!(
        exec.run_until_stalled(&mut sme_stream.next()),
        Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::Connect {
            req, txn, control_handle: _
        }))) => {
            assert_eq!(req.ssid, TEST_SSID.clone());
            assert_eq!(expected_credential, req.authentication.credentials);
            let (_stream, ctrl) = txn.expect("connect txn unused")
                .into_stream_and_control_handle().expect("error accessing control handle");
            ctrl
        }
    );
    connect_txn_handle
        .send_on_connect_result(&mut fidl_sme::ConnectResult {
            code: fidl_fuchsia_wlan_ieee80211::StatusCode::Success,
            is_credential_rejected: false,
            is_reconnect: false,
        })
        .expect("failed to send connection completion");

    assert_variant!(
        exec.run_until_stalled(&mut test_values.internal_objects.internal_futures),
        Poll::Pending
    );

    // Process stash write for the recording of connect results
    process_stash_write(&mut exec, &mut test_values.external_interfaces.stash_server);

    assert_variant!(
        exec.run_until_stalled(&mut test_values.internal_objects.internal_futures),
        Poll::Pending
    );

    // Check for a listener update saying we're connected
    let fidl_policy::ClientStateSummary { state, networks, .. } =
        get_client_state_update(&mut exec, &mut client_listener_update_requests);
    assert_eq!(state.unwrap(), fidl_policy::WlanClientState::ConnectionsEnabled);
    let mut networks = networks.unwrap();
    assert_eq!(networks.len(), 1);
    let network = networks.pop().unwrap();
    assert_eq!(network.id.unwrap(), network_id.clone());
    assert_eq!(network.state.unwrap(), types::ConnectionState::Connected);
}

// TODO(fxbug.dev/85817): reenable credential upgrading, which will make these cases connect
#[test_case(Saved::Wpa, Scanned::Wpa2Wpa3Personal, TEST_CREDS.wpa_pass_min.clone())]
#[test_case(Saved::Wpa, Scanned::Wpa2Wpa3Personal, TEST_CREDS.wpa_pass_max.clone())]
#[test_case(Saved::Wpa, Scanned::Wpa2Wpa3Personal, TEST_CREDS.wpa_psk.clone())]
#[test_case(Saved::Wpa, Scanned::Wpa3Personal, TEST_CREDS.wpa_pass_min.clone())]
#[test_case(Saved::Wpa, Scanned::Wpa3Personal, TEST_CREDS.wpa_pass_max.clone())]
// WPA credentials should never be used for WEP or Open networks
#[test_case(Saved::Wpa, Scanned::Open, TEST_CREDS.wpa_pass_min.clone())]
#[test_case(Saved::Wpa, Scanned::Open, TEST_CREDS.wpa_psk.clone())]
#[test_case(Saved::Wpa, Scanned::Wep, TEST_CREDS.wep_64_hex.clone())] // Use credentials which are valid len for WEP and WPA
#[test_case(Saved::Wpa, Scanned::Wep, TEST_CREDS.wpa_psk.clone())]
#[test_case(Saved::Wpa2, Scanned::Open, TEST_CREDS.wpa_pass_min.clone())]
#[test_case(Saved::Wpa2, Scanned::Open, TEST_CREDS.wpa_psk.clone())]
#[test_case(Saved::Wpa2, Scanned::Wep, TEST_CREDS.wep_64_hex.clone())] // Use credentials which are valid len for WEP and WPA
#[test_case(Saved::Wpa2, Scanned::Wep, TEST_CREDS.wpa_psk.clone())]
#[test_case(Saved::Wpa3, Scanned::Open, TEST_CREDS.wpa_pass_min.clone())]
#[test_case(Saved::Wpa3, Scanned::Open, TEST_CREDS.wpa_psk.clone())]
#[test_case(Saved::Wpa3, Scanned::Wep, TEST_CREDS.wep_64_hex.clone())] // Use credentials which are valid len for WEP and WPA
#[test_case(Saved::Wpa3, Scanned::Wep, TEST_CREDS.wpa_psk.clone())]
// PSKs should never be used for WPA3
#[test_case(Saved::Wpa, Scanned::Wpa3Personal, TEST_CREDS.wpa_psk.clone())]
#[test_case(Saved::Wpa2, Scanned::Wpa3Personal, TEST_CREDS.wpa_psk.clone())]
// Saved credential: WPA2: downgrades are disallowed
#[test_case(Saved::Wpa2, Scanned::Wpa1, TEST_CREDS.wpa_pass_min.clone())]
#[test_case(Saved::Wpa2, Scanned::Wpa1, TEST_CREDS.wpa_pass_max.clone())]
#[test_case(Saved::Wpa2, Scanned::Wpa1, TEST_CREDS.wpa_psk.clone())]
// Saved credential: WPA3: downgrades are disallowed
#[test_case(Saved::Wpa3, Scanned::Wpa1, TEST_CREDS.wpa_pass_min.clone())]
#[test_case(Saved::Wpa3, Scanned::Wpa1, TEST_CREDS.wpa_pass_max.clone())]
#[test_case(Saved::Wpa3, Scanned::Wpa1, TEST_CREDS.wpa_psk.clone())]
#[test_case(Saved::Wpa3, Scanned::Wpa1Wpa2Personal, TEST_CREDS.wpa_pass_min.clone())]
#[test_case(Saved::Wpa3, Scanned::Wpa1Wpa2Personal, TEST_CREDS.wpa_pass_max.clone())]
#[test_case(Saved::Wpa3, Scanned::Wpa1Wpa2Personal, TEST_CREDS.wpa_psk.clone())]
#[test_case(Saved::Wpa3, Scanned::Wpa1Wpa2PersonalTkipOnly, TEST_CREDS.wpa_pass_min.clone())]
#[test_case(Saved::Wpa3, Scanned::Wpa1Wpa2PersonalTkipOnly, TEST_CREDS.wpa_pass_max.clone())]
#[test_case(Saved::Wpa3, Scanned::Wpa1Wpa2PersonalTkipOnly, TEST_CREDS.wpa_psk.clone())]
#[test_case(Saved::Wpa3, Scanned::Wpa2Personal, TEST_CREDS.wpa_pass_min.clone())]
#[test_case(Saved::Wpa3, Scanned::Wpa2Personal, TEST_CREDS.wpa_pass_max.clone())]
#[test_case(Saved::Wpa3, Scanned::Wpa2Personal, TEST_CREDS.wpa_psk.clone())]
#[test_case(Saved::Wpa3, Scanned::Wpa2PersonalTkipOnly, TEST_CREDS.wpa_pass_min.clone())]
#[test_case(Saved::Wpa3, Scanned::Wpa2PersonalTkipOnly, TEST_CREDS.wpa_pass_max.clone())]
#[test_case(Saved::Wpa3, Scanned::Wpa2PersonalTkipOnly, TEST_CREDS.wpa_psk.clone())]
#[fuchsia::test(add_test_attr = false)]
/// Tests saving and connecting across various security types, where the connection is expected to fail
fn save_and_fail_to_connect(
    saved_security: fidl_policy::SecurityType,
    scanned_security: fidl_sme::Protection,
    test_credentials: TestCredentials,
) {
    let saved_credential = test_credentials.policy.clone();

    let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
    let mut test_values = test_setup(&mut exec);

    // No request has been sent yet. Future should be idle.
    assert_variant!(
        exec.run_until_stalled(&mut test_values.internal_objects.internal_futures),
        Poll::Pending
    );

    // Request a new controller.
    let (controller, mut client_listener_update_requests) =
        request_controller(&test_values.external_interfaces.client_provider_proxy);
    assert_variant!(
        exec.run_until_stalled(&mut test_values.internal_objects.internal_futures),
        Poll::Pending
    );

    // Initial update should reflect client connections are disabled
    let fidl_policy::ClientStateSummary { state, networks, .. } =
        get_client_state_update(&mut exec, &mut client_listener_update_requests);
    assert_eq!(state.unwrap(), fidl_policy::WlanClientState::ConnectionsDisabled);
    assert_eq!(networks.unwrap().len(), 0);

    // Get ready for client connections
    let mut iface_sme_stream = prepare_client_interface(&mut exec, &controller, &mut test_values);

    // Check for a listener update saying client connections are enabled
    let fidl_policy::ClientStateSummary { state, networks, .. } =
        get_client_state_update(&mut exec, &mut client_listener_update_requests);
    assert_eq!(state.unwrap(), fidl_policy::WlanClientState::ConnectionsEnabled);
    assert_eq!(networks.unwrap().len(), 0);

    // Generate network ID
    let network_id =
        fidl_policy::NetworkIdentifier { ssid: TEST_SSID.clone().into(), type_: saved_security };
    let network_config = fidl_policy::NetworkConfig {
        id: Some(network_id.clone()),
        credential: Some(saved_credential),
        ..fidl_policy::NetworkConfig::EMPTY
    };

    // Save the network
    let save_fut = controller.save_network(network_config);
    pin_mut!(save_fut);
    // Begin processing the save request
    assert_variant!(
        exec.run_until_stalled(&mut test_values.internal_objects.internal_futures),
        Poll::Pending
    );

    // Process the stash write from the save
    process_stash_write(&mut exec, &mut test_values.external_interfaces.stash_server);

    // Continue processing the save request. Auto-connection process starts
    assert_variant!(
        exec.run_until_stalled(&mut test_values.internal_objects.internal_futures),
        Poll::Pending
    );

    // Get update saying we are connecting.
    let fidl_policy::ClientStateSummary { state, networks, .. } =
        get_client_state_update(&mut exec, &mut client_listener_update_requests);
    assert_eq!(state.unwrap(), fidl_policy::WlanClientState::ConnectionsEnabled);
    let mut networks = networks.unwrap();
    assert_eq!(networks.len(), 1);
    let network = networks.pop().unwrap();
    assert_eq!(network.state.unwrap(), types::ConnectionState::Connecting);
    assert_eq!(network.id.unwrap(), network_id.clone());

    for i in 0..3 {
        // On first iteration, save request returns once the scan has been queued.
        if i == 0 {
            assert_variant!(exec.run_until_stalled(&mut save_fut), Poll::Ready(Ok(Ok(()))));
        }

        // BSS selection scans occur for requested network. Return scan results.
        let expected_scan_request = fidl_sme::ScanRequest::Active(fidl_sme::ActiveScanRequest {
            ssids: vec![TEST_SSID.clone().into()],
            channels: vec![],
        });
        let mutual_security_protocols = security_protocols_from_protection(scanned_security);
        assert!(!mutual_security_protocols.is_empty(), "no mutual security protocols");
        let mock_scan_results = vec![fidl_sme::ScanResult {
            compatibility: Some(Box::new(fidl_sme::Compatibility { mutual_security_protocols })),
            timestamp_nanos: zx::Time::get_monotonic().into_nanos(),
            bss_description: random_fidl_bss_description!(
                protection =>  wlan_common::test_utils::fake_stas::FakeProtectionCfg::from(scanned_security),
                bssid: [0, 0, 0, 0, 0, 0],
                ssid: TEST_SSID.clone(),
                rssi_dbm: 10,
                snr_db: 10,
                channel: types::WlanChan::new(1, types::Cbw::Cbw20),
            ),
        }];
        poll_for_and_validate_sme_scan_request_and_send_results(
            &mut exec,
            &mut test_values.internal_objects.internal_futures,
            &mut iface_sme_stream,
            &expected_scan_request,
            mock_scan_results.clone(),
        );

        assert_variant!(
            exec.run_until_stalled(&mut test_values.internal_objects.internal_futures),
            Poll::Pending
        );
    }

    // Check for a listener update saying we failed to connect
    let fidl_policy::ClientStateSummary { state, networks, .. } =
        get_client_state_update(&mut exec, &mut client_listener_update_requests);
    assert_eq!(state.unwrap(), fidl_policy::WlanClientState::ConnectionsEnabled);
    let mut networks = networks.unwrap();
    assert_eq!(networks.len(), 1);
    let network = networks.pop().unwrap();
    assert_eq!(network.state.unwrap(), types::ConnectionState::Failed);
    assert_eq!(network.id.unwrap(), network_id.clone());
}
