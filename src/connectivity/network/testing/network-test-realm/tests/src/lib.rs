// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use anyhow::Result;
use fidl_fuchsia_component as fcomponent;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_interfaces as fnet_interfaces;
use fidl_fuchsia_net_interfaces_ext as fnet_interfaces_ext;
use fidl_fuchsia_net_test_realm as fntr;
use net_declare::fidl_mac;
use netemul::Endpoint as _;
use netstack_testing_common::realms::{KnownServiceProvider, Netstack2, TestSandboxExt as _};
use std::collections::HashMap;

const ETH1_MAC_ADDRESS: fnet::MacAddress = fidl_mac!("02:03:04:05:06:07");
const ETH2_MAC_ADDRESS: fnet::MacAddress = fidl_mac!("05:06:07:08:09:10");
const ETH1_INTERFACE_NAME: &'static str = "eth1";
const ETH2_INTERFACE_NAME: &'static str = "eth2";
const EXPECTED_INTERFACE_NAME: &'static str = "added-interface";

/// Creates a `netemul::TestRealm` with a Netstack2 instance and the Network
/// Test Realm.
fn create_netstack_realm<'a>(
    name: &'a str,
    sandbox: &'a netemul::TestSandbox,
) -> Result<netemul::TestRealm<'a>> {
    // NOTE: To simplify the tests and reduce the number of dependencies, netcfg
    // is intentionally omitted from the `KnownServiceProvider` list below.
    // Instead, it is expected that tests will manually register interfaces with
    // the system's Netstack as needed.
    sandbox.create_netstack_realm_with::<Netstack2, _, _>(
        name,
        &[KnownServiceProvider::NetworkTestRealm],
    )
}

/// Returns the online status of the interface with `expected_name`.
///
/// If the interface is not found, then `None` is returned
async fn get_interface_online_status<'a>(
    interface_name: &'a str,
    state_proxy: &'a fnet_interfaces::StateProxy,
) -> Option<bool> {
    let stream = fnet_interfaces_ext::event_stream_from_state(&state_proxy)
        .expect("failed to get interface stream");
    let interfaces = fnet_interfaces_ext::existing(stream, HashMap::new())
        .await
        .expect("failed to get existing interfaces");
    interfaces.values().find_map(
        |fidl_fuchsia_net_interfaces_ext::Properties {
             name,
             online,
             id: _,
             device_class: _,
             addresses: _,
             has_default_ipv4_route: _,
             has_default_ipv6_route: _,
         }| if interface_name == name { Some(*online) } else { None },
    )
}

/// Connects to a protocol within the hermetic network realm.
async fn connect_to_hermetic_network_realm_protocol<
    P: fidl::endpoints::DiscoverableProtocolMarker,
>(
    realm: &netemul::TestRealm<'_>,
) -> P::Proxy {
    let directory_proxy = open_hermetic_network_realm_exposed_directory(realm).await;
    fuchsia_component::client::connect_to_protocol_at_dir_root::<P>(&directory_proxy)
        .unwrap_or_else(|e| {
            panic!(
                "failed to connect to hermetic network realm protocol {} with error: {:?}",
                P::NAME,
                e
            )
        })
}

/// Opens the exposed directory that corresponds to the hermetic network realm.
///
/// An error will be returned if the realm does not exist.
async fn open_hermetic_network_realm_exposed_directory(
    realm: &netemul::TestRealm<'_>,
) -> fio::DirectoryProxy {
    let realm_proxy = realm
        .connect_to_protocol::<fcomponent::RealmMarker>()
        .expect("failed to connect to realm protocol");
    let (directory_proxy, server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
        .expect("failed to create Directory proxy");
    let mut child_ref = network_test_realm_common::create_hermetic_network_relam_child_ref();
    realm_proxy
        .open_exposed_dir(&mut child_ref, server_end)
        .await
        .expect("open_exposed_dir failed")
        .expect("open_exposed_dir error");
    directory_proxy
}

/// Returns true if the hermetic network realm exists.
async fn has_hermetic_network_realm(realm: &netemul::TestRealm<'_>) -> bool {
    let realm_proxy = realm
        .connect_to_protocol::<fcomponent::RealmMarker>()
        .expect("failed to connect to realm protocol");
    network_test_realm_common::has_hermetic_network_realm(&realm_proxy)
        .await
        .expect("failed to check for hermetic network realm")
}

/// Verifies that the system interface with `name` has the provided
/// `expected_online_status`.
async fn verify_system_interface_online_status(
    name: &str,
    realm: &netemul::TestRealm<'_>,
    expected_online_status: bool,
) {
    let system_stack_proxy = realm
        .connect_to_protocol::<fnet_interfaces::StateMarker>()
        .expect("failed to connect to state");
    let online_status =
        get_interface_online_status(name, &system_stack_proxy).await.unwrap_or_else(|| {
            panic!("failed to find system interface with name {}", name);
        });

    assert_eq!(expected_online_status, online_status);
}

async fn add_interface_to_system_netstack<'a>(
    mac_address: fnet::MacAddress,
    name: &'a str,
    sandbox: &'a netemul::TestSandbox,
    realm: &'a netemul::TestRealm<'a>,
) -> netemul::TestInterface<'a> {
    let endpoint = sandbox
        .create_endpoint_with(
            name,
            netemul::Ethernet::make_config(netemul::DEFAULT_MTU, Some(mac_address)),
        )
        .await
        .expect("failed to create endpoint");
    realm
        .install_endpoint(endpoint, &netemul::InterfaceConfig::None, Some(name.to_string()))
        .await
        .expect("failed to install endpoint")
}

async fn add_interface_to_devfs<'a>(
    name: &'a str,
    endpoint: &'a netemul::TestEndpoint<'a>,
    realm: &'a netemul::TestRealm<'a>,
) {
    let endpoint_mount_path = netemul::Ethernet::dev_path(name);
    let endpoint_mount_path = endpoint_mount_path.as_path();
    realm
        .add_virtual_device(endpoint, endpoint_mount_path)
        .await
        .expect("failed to add interface to devfs");
}

/// Adds an enabled interface with `mac_address` and `name` to the provided
/// `realm`.
async fn add_enabled_interface_to_realm<'a>(
    mac_address: fnet::MacAddress,
    name: &'a str,
    sandbox: &'a netemul::TestSandbox,
    realm: &'a netemul::TestRealm<'a>,
) -> netemul::TestInterface<'a> {
    let interface = add_interface_to_system_netstack(mac_address, name, sandbox, realm).await;
    interface.enable_interface().await.expect("failed to enable interface on system netstack");
    add_interface_to_devfs(name, interface.endpoint(), realm).await;
    interface
}

#[fuchsia_async::run_singlethreaded(test)]
async fn start_hermetic_network_realm() {
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let realm = create_netstack_realm("start_hermetic_network_realm", &sandbox)
        .expect("failed to create netstack realm");

    let network_test_realm = realm
        .connect_to_protocol::<fntr::ControllerMarker>()
        .expect("failed to connect to network test realm controller");

    network_test_realm
        .start_hermetic_network_realm(fntr::Netstack::V2)
        .await
        .expect("start_hermetic_network_realm failed")
        .expect("start_hermetic_network_realm error");

    assert!(has_hermetic_network_realm(&realm).await);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn start_hermetic_network_realm_replaces_existing_realm() {
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let realm =
        create_netstack_realm("start_hermetic_network_realm_replaces_existing_realm", &sandbox)
            .expect("failed to create netstack realm");

    let network_test_realm = realm
        .connect_to_protocol::<fntr::ControllerMarker>()
        .expect("failed to connect to network test realm controller");

    network_test_realm
        .start_hermetic_network_realm(fntr::Netstack::V2)
        .await
        .expect("start_hermetic_network_realm failed")
        .expect("start_hermetic_network_realm error");

    let _interface: netemul::TestInterface<'_> =
        add_enabled_interface_to_realm(ETH1_MAC_ADDRESS, ETH1_INTERFACE_NAME, &sandbox, &realm)
            .await;

    network_test_realm
        .add_interface(&mut ETH1_MAC_ADDRESS.clone(), EXPECTED_INTERFACE_NAME)
        .await
        .expect("add_interface failed")
        .expect("add_interface error");

    network_test_realm
        .start_hermetic_network_realm(fntr::Netstack::V2)
        .await
        .expect("start_hermetic_network_realm failed")
        .expect("start_hermetic_network_realm error");

    // The interface on the system's Netstack should be re-enabled (it was
    // disabled when an interface was added above).
    verify_system_interface_online_status(
        ETH1_INTERFACE_NAME,
        &realm,
        true, /* expected_online_status */
    )
    .await;

    let hermetic_network_state_proxy =
        connect_to_hermetic_network_realm_protocol::<fnet_interfaces::StateMarker>(&realm).await;
    // The Netstack in the replaced hermetic network realm should not have the
    // previously attached interface.
    assert_eq!(
        get_interface_online_status(EXPECTED_INTERFACE_NAME, &hermetic_network_state_proxy).await,
        None
    );

    assert!(has_hermetic_network_realm(&realm).await);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn add_interface() {
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let realm =
        create_netstack_realm("add_interface", &sandbox).expect("failed to create netstack realm");

    let network_test_realm = realm
        .connect_to_protocol::<fntr::ControllerMarker>()
        .expect("failed to connect to network test realm controller");

    network_test_realm
        .start_hermetic_network_realm(fntr::Netstack::V2)
        .await
        .expect("start_hermetic_network_realm failed")
        .expect("start_hermetic_network_realm error");

    let _interface: netemul::TestInterface<'_> =
        add_enabled_interface_to_realm(ETH1_MAC_ADDRESS, ETH1_INTERFACE_NAME, &sandbox, &realm)
            .await;

    let _interface: netemul::TestInterface<'_> =
        add_enabled_interface_to_realm(ETH2_MAC_ADDRESS, ETH2_INTERFACE_NAME, &sandbox, &realm)
            .await;

    network_test_realm
        .add_interface(&mut ETH1_MAC_ADDRESS.clone(), EXPECTED_INTERFACE_NAME)
        .await
        .expect("add_interface failed")
        .expect("add_interface error");

    // The corresponding interface on the system's Netstack should be disabled
    // when an interface is added to the hermetic Netstack.
    verify_system_interface_online_status(
        ETH1_INTERFACE_NAME,
        &realm,
        false, /* expected_online_status */
    )
    .await;

    let hermetic_network_state_proxy =
        connect_to_hermetic_network_realm_protocol::<fnet_interfaces::StateMarker>(&realm).await;

    let online_status =
        get_interface_online_status(EXPECTED_INTERFACE_NAME, &hermetic_network_state_proxy)
            .await
            .unwrap_or_else(|| {
                panic!(
                    "failed to find hermetic network interface with: name {}",
                    EXPECTED_INTERFACE_NAME
                );
            });

    // An interface with a name of `EXPECTED_INTERFACE_NAME` should be enabled and
    // present in the hermetic Netstack.
    assert!(online_status);
}

// Tests the case where the MAC address provided to `Controller.AddInterface`
// does not match any of the interfaces on the system.
#[fuchsia_async::run_singlethreaded(test)]
async fn add_interface_with_no_matching_interface() {
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let realm = create_netstack_realm("add_interface_with_no_matching_interface", &sandbox)
        .expect("failed to create netstack realm");

    let network_test_realm = realm
        .connect_to_protocol::<fntr::ControllerMarker>()
        .expect("failed to connect to network test realm controller");

    network_test_realm
        .start_hermetic_network_realm(fntr::Netstack::V2)
        .await
        .expect("start_hermetic_network_realm failed")
        .expect("start_hermetic_network_realm error");

    let _interface: netemul::TestInterface<'_> =
        add_enabled_interface_to_realm(ETH1_MAC_ADDRESS, ETH1_INTERFACE_NAME, &sandbox, &realm)
            .await;

    // `non_matching_mac_address` doesn't match any of the MAC addresses for
    // interfaces owned by the system's Netstack.
    let mut non_matching_mac_address = fidl_mac!("aa:bb:cc:dd:ee:ff");
    assert_eq!(
        network_test_realm
            .add_interface(&mut non_matching_mac_address, EXPECTED_INTERFACE_NAME)
            .await
            .expect("failed to add interface to hermetic netstack"),
        Err(fntr::Error::InterfaceNotFound)
    );
}

// Tests the case where the MAC address provided to `Controller.AddInterface`
// matches an interface on the system Netstack, but not in devfs.
#[fuchsia_async::run_singlethreaded(test)]
async fn add_interface_with_no_matching_interface_in_devfs() {
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let realm =
        create_netstack_realm("add_interface_with_no_matching_interface_in_devfs", &sandbox)
            .expect("failed to create netstack realm");

    let network_test_realm = realm
        .connect_to_protocol::<fntr::ControllerMarker>()
        .expect("failed to connect to network test realm controller");

    network_test_realm
        .start_hermetic_network_realm(fntr::Netstack::V2)
        .await
        .expect("start_hermetic_network_realm failed")
        .expect("start_hermetic_network_realm error");

    let _: netemul::TestInterface<'_> =
        add_interface_to_system_netstack(ETH1_MAC_ADDRESS, ETH1_INTERFACE_NAME, &sandbox, &realm)
            .await;

    // The Network Test Realm requires that the matching interface be present in
    // both the system's Netstack and devfs. In this case, it is only present in
    // the system's Netstack.
    assert_eq!(
        network_test_realm
            .add_interface(&mut ETH1_MAC_ADDRESS.clone(), EXPECTED_INTERFACE_NAME)
            .await
            .expect("failed to add interface to hermetic netstack"),
        Err(fntr::Error::InterfaceNotFound)
    );
}

// Tests the case where the MAC address provided to `Controller.AddInterface`
// matches an interface in devfs, but not in the system Netstack.
#[fuchsia_async::run_singlethreaded(test)]
async fn add_interface_with_no_matching_interface_in_netstack() {
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let realm =
        create_netstack_realm("add_interface_with_no_matching_interface_in_netstack", &sandbox)
            .expect("failed to create netstack realm");

    let network_test_realm = realm
        .connect_to_protocol::<fntr::ControllerMarker>()
        .expect("failed to connect to network test realm controller");

    network_test_realm
        .start_hermetic_network_realm(fntr::Netstack::V2)
        .await
        .expect("start_hermetic_network_realm failed")
        .expect("start_hermetic_network_realm error");

    let endpoint = sandbox
        .create_endpoint_with(
            ETH1_INTERFACE_NAME,
            netemul::Ethernet::make_config(netemul::DEFAULT_MTU, Some(ETH1_MAC_ADDRESS)),
        )
        .await
        .expect("failed to create endpoint");
    add_interface_to_devfs(ETH1_INTERFACE_NAME, &endpoint, &realm).await;

    // The Network Test Realm requires that the matching interface be present in
    // both the system's Netstack and devfs. In this case, it is only present in
    // devfs.
    assert_eq!(
        network_test_realm
            .add_interface(&mut ETH1_MAC_ADDRESS.clone(), EXPECTED_INTERFACE_NAME)
            .await
            .expect("failed to add interface to hermetic netstack"),
        Err(fntr::Error::InterfaceNotFound)
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn stop_hermetic_network_realm() {
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let realm = create_netstack_realm("stop_hermetic_network_realm", &sandbox)
        .expect("failed to create netstack realm");

    let network_test_realm = realm
        .connect_to_protocol::<fntr::ControllerMarker>()
        .expect("failed to connect to network test realm controller");

    network_test_realm
        .start_hermetic_network_realm(fntr::Netstack::V2)
        .await
        .expect("start_hermetic_network_realm failed")
        .expect("start_hermetic_network_realm error");

    let _interface: netemul::TestInterface<'_> =
        add_enabled_interface_to_realm(ETH1_MAC_ADDRESS, ETH1_INTERFACE_NAME, &sandbox, &realm)
            .await;

    network_test_realm
        .add_interface(&mut ETH1_MAC_ADDRESS.clone(), EXPECTED_INTERFACE_NAME)
        .await
        .expect("add_interface failed")
        .expect("add_interface error");

    network_test_realm
        .stop_hermetic_network_realm()
        .await
        .expect("stop_hermetic_network_realm failed")
        .expect("stop_hermetic_network_realm error");

    verify_system_interface_online_status(
        ETH1_INTERFACE_NAME,
        &realm,
        true, /* expected_online_status */
    )
    .await;
    assert!(!has_hermetic_network_realm(&realm).await);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn stop_hermetic_network_realm_with_no_existing_realm() {
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let realm =
        create_netstack_realm("stop_hermetic_network_realm_with_no_existing_realm", &sandbox)
            .expect("failed to create netstack realm");

    let network_test_realm = realm
        .connect_to_protocol::<fntr::ControllerMarker>()
        .expect("failed to connect to network test realm controller");

    assert_eq!(
        network_test_realm
            .stop_hermetic_network_realm()
            .await
            .expect("failed to stop hermetic network realm"),
        Err(fntr::Error::HermeticNetworkRealmNotRunning),
    );
}
