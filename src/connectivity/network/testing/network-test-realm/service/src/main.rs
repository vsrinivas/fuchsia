// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error, Result};
use async_utils::stream::FlattenUnorderedExt as _;
use fidl::endpoints::Proxy as _;
use fidl_fuchsia_component as fcomponent;
use fidl_fuchsia_component_decl as fdecl;
use fidl_fuchsia_hardware_ethernet as fethernet;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_net_debug as fnet_debug;
use fidl_fuchsia_net_ext as fnet_ext;
use fidl_fuchsia_net_interfaces_ext as fnet_interfaces_ext;
use fidl_fuchsia_net_stack as fstack;
use fidl_fuchsia_net_test_realm as fntr;
use fidl_fuchsia_netstack as fnetstack;
use fuchsia_async::futures::StreamExt as _;
use futures::TryFutureExt as _;
use log::{error, warn};
use std::path;

/// URL for the realm that contains the hermetic network components with a
/// Netstack2 instance.
const HERMETIC_NETWORK_V2_URL: &'static str = "#meta/hermetic_network_v2.cm";

/// Values for creating an interface on the hermetic Netstack.
///
/// Note that the topological path and the file path are not used by the
/// underlying Netstack. Consequently, fake values are defined here. Similarly,
/// the metric only needs to be a sensible value.
const DEFAULT_METRIC: u32 = 100;
const DEFAULT_INTERFACE_TOPOLOGICAL_PATH: &'static str = "/dev/fake/topological/path";
const DEFAULT_INTERFACE_FILE_PATH: &'static str = "/dev/fake/file/path";

/// Returns a `fidl::endpoints::ClientEnd` for the device that matches
/// `mac_address`.
///
/// If a matching device is not found, then an error is returned.
async fn find_device_client_end(
    expected_mac_address: fnet_ext::MacAddress,
) -> Result<fidl::endpoints::ClientEnd<fethernet::DeviceMarker>, fntr::Error> {
    // TODO(https://fxbug.dev/89648): Replace this function with a call to
    // fuchsia.net.debug. As an intermediate solution, the
    // `fidl::endpoints::ClientEnd` is obtained by enumerating the ethernet
    // devices in devfs.
    const ETHERNET_DIRECTORY_PATH: &'static str = "/dev/class/ethernet";

    let (directory_proxy, directory_server_end) =
        fidl::endpoints::create_proxy::<fio::DirectoryMarker>().map_err(|e| {
            error!("failed to create directory endpoint: {:?}", e);
            fntr::Error::Internal
        })?;
    fdio::service_connect(ETHERNET_DIRECTORY_PATH, directory_server_end.into_channel().into())
        .map_err(|e| {
            error!(
                "failed to connect to directory service at {} with error: {:?}",
                ETHERNET_DIRECTORY_PATH, e
            );
            fntr::Error::Internal
        })?;
    let files = files_async::readdir(&directory_proxy).await.map_err(|e| {
        error!("failed to read files in {} with error {:?}", ETHERNET_DIRECTORY_PATH, e);
        fntr::Error::Internal
    })?;

    for file in files {
        let filepath = path::Path::new(ETHERNET_DIRECTORY_PATH).join(&file.name);
        let filepath = filepath.to_str().ok_or_else(|| {
            error!("failed to convert file path to string");
            fntr::Error::Internal
        })?;
        let (device_proxy, device_server_end) =
            fidl::endpoints::create_proxy::<fethernet::DeviceMarker>().map_err(|e| {
                error!("failed to create device endpoint: {:?}", e);
                fntr::Error::Internal
            })?;
        fdio::service_connect(filepath, device_server_end.into_channel().into()).map_err(|e| {
            error!("failed to connect to device service: {:?}", e);
            fntr::Error::Internal
        })?;

        let info = device_proxy.get_info().await.map_err(|e| {
            error!("failed to read ethernet device info: {:?}", e);
            fntr::Error::Internal
        })?;

        if info.mac.octets == expected_mac_address.octets {
            let channel = fidl::endpoints::ClientEnd::<fethernet::DeviceMarker>::new(
                device_proxy
                    .into_channel()
                    .map_err(|e| {
                        error!("failed to convert device proxy to channel: {:?}", e);
                        fntr::Error::Internal
                    })?
                    .into_zx_channel(),
            );
            return Ok(channel);
        }
    }

    warn!(
        "failed to find interface with MAC address: {} in {}",
        expected_mac_address, ETHERNET_DIRECTORY_PATH
    );
    Err(fntr::Error::InterfaceNotFound)
}

/// Returns the id for the enabled interface that matches `mac_address`.
///
/// If an interface matching `mac_address` is not found, then an error is
/// returned. If a matching interface is found, but it is not enabled, then
/// `Option::None` is returned.
async fn find_enabled_interface_id(
    expected_mac_address: fnet_ext::MacAddress,
) -> Result<Option<u64>, fntr::Error> {
    // TODO(https://fxbug.dev/89648): Replace this function with a call to
    // fuchsia.net.debug. As an intermediate solution, the deprecated
    // ListInterfaces method is used to obtain the matching interface id (which
    // isn't available via the devfs route).
    connect_to_system_protocol::<fstack::StackMarker>()?
        .list_interfaces()
        .await
        .map_err(|e| {
            error!("list_interfaces failed: {:?}", e);
            fntr::Error::Internal
        })?
        .into_iter()
        .find_map(
            |fstack::InterfaceInfo {
                 id,
                 properties: fstack::InterfaceProperties { mac, administrative_status, .. },
             }| {
                mac.map_or(false, move |mac| mac.octets == expected_mac_address.octets)
                    .then(move || (administrative_status, id))
            },
        )
        .ok_or_else(|| {
            warn!(
                "failed to find interface with MAC address {} in the system netstack",
                expected_mac_address
            );
            fntr::Error::InterfaceNotFound
        })
        .map(|(administrative_status, id)| {
            // The `Controller` only needs to mutate this system interface if it
            // is enabled. As a result, disabled interfaces are not returned
            // here.
            if administrative_status == fstack::AdministrativeStatus::Enabled {
                Some(id)
            } else {
                None
            }
        })
}

/// Returns a `fnet_interfaces_admin::ControlProxy` that can be used to
/// manipulate the interface that has the provided `id`.
async fn connect_to_interface_admin_control(
    id: u64,
    debug_interfaces_proxy: &fnet_debug::InterfacesProxy,
) -> Result<fnet_interfaces_ext::admin::Control, fntr::Error> {
    let (control, server) =
        fnet_interfaces_ext::admin::Control::create_endpoints().map_err(|e| {
            error!("create_proxy failure: {:?}", e);
            fntr::Error::Internal
        })?;
    debug_interfaces_proxy.get_admin(id, server).map_err(|e| {
        error!("get_admin failure: {:?}", e);
        fntr::Error::Internal
    })?;
    Ok(control)
}

/// Enables the interface with `id` using the provided `debug_interfaces_proxy`.
async fn enable_interface(
    id: u64,
    debug_interfaces_proxy: &fnet_debug::InterfacesProxy,
) -> Result<(), fntr::Error> {
    let control_proxy = connect_to_interface_admin_control(id, debug_interfaces_proxy).await?;
    let _did_enable: bool = control_proxy
        .enable()
        .await
        .map_err(|e| {
            error!("enable interface id: {} failure: {:?}", id, e);
            fntr::Error::Internal
        })?
        .map_err(|e| {
            error!("enable interface id: {} error: {:?}", id, e);
            fntr::Error::Internal
        })?;
    Ok(())
}

/// Disables the interface with `id` using the provided
/// `debug_interfaces_proxy`.
async fn disable_interface(
    id: u64,
    debug_interfaces_proxy: &fnet_debug::InterfacesProxy,
) -> Result<(), fntr::Error> {
    let control_proxy = connect_to_interface_admin_control(id, debug_interfaces_proxy).await?;
    let _did_disable: bool = control_proxy
        .disable()
        .await
        .map_err(|e| {
            error!("disable interface id: {} failure: {:?}", id, e);
            fntr::Error::Internal
        })?
        .map_err(|e| {
            error!("disable interface id: {} error: {:?}", id, e);
            fntr::Error::Internal
        })?;
    Ok(())
}

/// Connects to a protocol within the "hermetic-network" realm.
async fn connect_to_hermetic_network_realm_protocol<
    P: fidl::endpoints::DiscoverableProtocolMarker,
>() -> Result<P::Proxy, fntr::Error> {
    Ok(fuchsia_component::client::connect_to_childs_protocol::<P>(
        network_test_realm_common::HERMETIC_NETWORK_REALM_NAME.to_string(),
        Some(network_test_realm_common::HERMETIC_NETWORK_COLLECTION_NAME.to_string()),
    )
    .await
    .map_err(|e| {
        error!(
            "failed to connect to hermetic network realm protocol {} with error: {:?}",
            P::NAME,
            e
        );
        fntr::Error::Internal
    })?)
}

fn connect_to_system_protocol<P: fidl::endpoints::DiscoverableProtocolMarker>(
) -> Result<P::Proxy, fntr::Error> {
    Ok(fuchsia_component::client::connect_to_protocol::<P>().map_err(|e| {
        error!("failed to connect to {} with error: {:?}", P::NAME, e);
        fntr::Error::Internal
    })?)
}

async fn has_hermetic_network_realm() -> Result<bool, fntr::Error> {
    let realm_proxy = connect_to_system_protocol::<fcomponent::RealmMarker>()?;
    Ok(network_test_realm_common::has_hermetic_network_realm(&realm_proxy).await.map_err(|e| {
        error!("failed to check for hermetic network realm: {:?}", e);
        fntr::Error::Internal
    })?)
}

/// A controller for creating and manipulating the Network Test Realm.
///
/// The Network Test Realm corresponds to a hermetic network realm with a
/// Netstack under test. The `Controller` is responsible for configuring
/// this realm, the Netstack under test, and the system's Netstack. Once
/// configured, the controller is expected to yield the following component
/// topology:
///
/// ```
///            network-test-realm (this controller component)
///                    |
///             enclosed-network (a collection)
///                    |
///             hermetic-network
///            /       |        \
///      netstack     ...      stubs (a collection)
///                              |
///                          test-stub (a configurable component)
/// ```
///
/// This topology enables the `Controller` to interact with the system's
/// Netstack and the Netstack in the "hermetic-network" realm. Additionally, it
/// enables capabilities to be routed from the hermetic Netstack to siblings
/// (via the "hermetic-network" parent) while remaining isolated from the rest
/// of the system.
struct Controller {
    /// Interface IDs that have been mutated on the system's Netstack.
    mutated_interface_ids: Vec<u64>,
}

impl Controller {
    fn new() -> Self {
        Self { mutated_interface_ids: Vec::<u64>::new() }
    }

    async fn handle_request(
        &mut self,
        request: fntr::ControllerRequest,
    ) -> Result<(), fidl::Error> {
        match request {
            fntr::ControllerRequest::StartHermeticNetworkRealm { netstack, responder } => {
                let mut result = self.start_hermetic_network_realm(netstack).await;
                responder.send(&mut result)?;
            }
            fntr::ControllerRequest::StopHermeticNetworkRealm { responder } => {
                let mut result = self.stop_hermetic_network_realm().await;
                responder.send(&mut result)?;
            }
            fntr::ControllerRequest::AddInterface { mac_address, name, responder } => {
                let mac_address = fnet_ext::MacAddress::from(mac_address);
                let mut result = self.add_interface(mac_address, &name).await;
                responder.send(&mut result)?;
            }
        }
        Ok(())
    }

    /// Adds an interface to the hermetic Netstack.
    ///
    /// An interface will only be added if the system has an interface with a
    /// matching `mac_address`. The added interface will have the provided
    /// `name`. Additionally, the matching interface will be disabled on the
    /// system's Netstack.
    async fn add_interface(
        &mut self,
        mac_address: fnet_ext::MacAddress,
        name: &str,
    ) -> Result<(), fntr::Error> {
        if !has_hermetic_network_realm().await? {
            // A hermetic Netstack must be running for an interface to be
            // added.
            return Err(fntr::Error::HermeticNetworkRealmNotRunning);
        }

        let device_client_end = find_device_client_end(mac_address).await?;
        let interface_id_to_disable = find_enabled_interface_id(mac_address).await?;

        // TODO(https://fxbug.dev/89651): Support Netstack3. Currently, an
        // interface name cannot be specified when adding an interface via
        // fuchsia.net.stack.Stack. As a result, the Network Test Realm
        // currently does not support Netstack3.
        let id: u32 = connect_to_hermetic_network_realm_protocol::<fnetstack::NetstackMarker>()
            .await?
            .add_ethernet_device(
                DEFAULT_INTERFACE_TOPOLOGICAL_PATH,
                &mut fnetstack::InterfaceConfig {
                    name: name.to_string(),
                    filepath: DEFAULT_INTERFACE_FILE_PATH.to_string(),
                    metric: DEFAULT_METRIC,
                },
                device_client_end,
            )
            .await
            .map_err(|e| {
                error!("add_ethernet_device failed: {:?}", e);
                fntr::Error::Internal
            })?
            .map_err(|e| {
                error!("add_ethernet_device error: {:?}", e);
                fntr::Error::Internal
            })?;

        let hermetic_network_interfaces_proxy =
            connect_to_hermetic_network_realm_protocol::<fnet_debug::InterfacesMarker>().await?;
        // Enable the interface that was newly added to the hermetic Netstack.
        // It is not enabled by default.
        enable_interface(id.into(), &hermetic_network_interfaces_proxy).await?;

        if let Some(interface_id_to_disable) = interface_id_to_disable {
            // Disable the matching interface on the system's Netstack.
            let interfaces_proxy = connect_to_system_protocol::<fnet_debug::InterfacesMarker>()?;
            disable_interface(interface_id_to_disable, &interfaces_proxy).await?;
            self.mutated_interface_ids.push(interface_id_to_disable);
        }
        Ok(())
    }

    /// Tears down the "hermetic-network" realm.
    ///
    /// Any interfaces that were previously disabled by the controller on the
    /// system's Netstack will be re-enabled. Returns an error if there is not
    /// a running "hermetic-network" realm.
    async fn stop_hermetic_network_realm(&mut self) -> Result<(), fntr::Error> {
        let mut child_ref = network_test_realm_common::create_hermetic_network_relam_child_ref();
        connect_to_system_protocol::<fcomponent::RealmMarker>()?
            .destroy_child(&mut child_ref)
            .await
            .map_err(|e| {
                error!("destroy_child failed: {:?}", e);
                fntr::Error::Internal
            })?
            .map_err(|e| {
                match e {
                    // Variants that may be returned by the `DestroyChild`
                    // method. `CollectionNotFound` and `InstanceNotFound`
                    // mean that the hermetic network realm does not exist. All
                    // other errors are propagated as internal errors.
                    fcomponent::Error::CollectionNotFound
                    | fcomponent::Error::InstanceNotFound =>
                        fntr::Error::HermeticNetworkRealmNotRunning,
                    fcomponent::Error::InstanceDied
                    | fcomponent::Error::InvalidArguments
                    // Variants that are not returned by the `DestroyChild`
                    // method.
                    | fcomponent::Error::AccessDenied
                    | fcomponent::Error::InstanceAlreadyExists
                    | fcomponent::Error::InstanceCannotResolve
                    | fcomponent::Error::InstanceCannotStart
                    | fcomponent::Error::Internal
                    | fcomponent::Error::ResourceNotFound
                    | fcomponent::Error::ResourceUnavailable
                    | fcomponent::Error::Unsupported => {
                        error!("destroy_child error: {:?}", e);
                        fntr::Error::Internal
                    }
            }
            })?;

        let interfaces_proxy = connect_to_system_protocol::<fnet_debug::InterfacesMarker>()?;

        // Attempt to re-enable all previously disabled interfaces on the
        // system's Netstack. If the controller fails to re-enable any of them,
        // then an error is logged but not returned. Re-enabling interfaces is
        // done on a best-effort basis.
        futures::stream::iter(self.mutated_interface_ids.drain(..))
            .for_each_concurrent(None, |id| {
                let interfaces_proxy = &interfaces_proxy;
                async move {
                    enable_interface(id, &interfaces_proxy).await.unwrap_or_else(|e| {
                        warn!("failed to re-enable interface id: {} with erorr: {:?}", id, e)
                    })
                }
            })
            .await;
        Ok(())
    }

    /// Starts the "hermetic-network" realm with the provided `netstack`.
    ///
    /// Adds the "hermetic-network" component to the "enclosed-network"
    /// collection.
    async fn start_hermetic_network_realm(
        &mut self,
        netstack: fntr::Netstack,
    ) -> Result<(), fntr::Error> {
        if has_hermetic_network_realm().await? {
            // The `Controller` only configures one hermetic network realm
            // at a time. As a result, any existing realm must be stopped before
            // a new one is started.
            self.stop_hermetic_network_realm().await.map_err(|e| match e {
                fntr::Error::HermeticNetworkRealmNotRunning => {
                    panic!("attempted to stop hermetic network realm that was not running")
                }
                fntr::Error::Internal | fntr::Error::InterfaceNotFound => e,
            })?;
        }

        let url = match netstack {
            fntr::Netstack::V2 => HERMETIC_NETWORK_V2_URL,
        };

        connect_to_system_protocol::<fcomponent::RealmMarker>()?
            .create_child(
                &mut fdecl::CollectionRef {
                    name: network_test_realm_common::HERMETIC_NETWORK_COLLECTION_NAME.to_string(),
                },
                fdecl::Child {
                    name: Some(network_test_realm_common::HERMETIC_NETWORK_REALM_NAME.to_string()),
                    url: Some(url.to_string()),
                    startup: Some(fdecl::StartupMode::Lazy),
                    ..fdecl::Child::EMPTY
                },
                fcomponent::CreateChildArgs::EMPTY,
            )
            .await
            .map_err(|e| {
                error!("create_child failed: {:?}", e);
                fntr::Error::Internal
            })?
            .map_err(|e| {
                error!("create_child error: {:?}", e);
                fntr::Error::Internal
            })?;

        Ok(())
    }
}

#[fuchsia::component]
async fn main() -> Result<(), Error> {
    let mut fs = fuchsia_component::server::ServiceFs::new_local();
    let _: &mut fuchsia_component::server::ServiceFsDir<'_, _> =
        fs.dir("svc").add_fidl_service(|s: fntr::ControllerRequestStream| s);

    let _: &mut fuchsia_component::server::ServiceFs<_> =
        fs.take_and_serve_directory_handle().context("failed to serve ServiceFs directory")?;

    let mut controller = Controller::new();

    let mut requests = fs.fuse().flatten_unordered();

    while let Some(controller_request) = requests.next().await {
        futures::future::ready(controller_request)
            .and_then(|req| controller.handle_request(req))
            .await
            .unwrap_or_else(|e| {
                if !fidl::Error::is_closed(&e) {
                    error!("handle_request failed: {:?}", e);
                } else {
                    warn!("handle_request closed: {:?}", e);
                }
            });
    }

    unreachable!("Stopped serving requests");
}
