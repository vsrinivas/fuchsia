// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context as _};
use async_utils::{hanging_get::client::HangingGetStream, stream::FlattenUnorderedExt as _};
use fidl::endpoints::Proxy as _;
use fidl_fuchsia_hardware_network as fhardware_network;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_debug as fnet_debug;
use fidl_fuchsia_net_ext as fnet_ext;
use fidl_fuchsia_net_interfaces as fnet_interfaces;
use fidl_fuchsia_net_interfaces_admin as fnet_interfaces_admin;
use fidl_fuchsia_net_interfaces_ext as fnet_interfaces_ext;
use fidl_fuchsia_net_stack as fnet_stack;
use fidl_fuchsia_netemul as fnetemul;
use fidl_fuchsia_netemul_network as fnetemul_network;
use fidl_fuchsia_netstack as fnetstack;
use fuchsia_component::{
    client::connect_to_protocol,
    server::{ServiceFs, ServiceFsDir},
};
use fuchsia_zircon_status as zx;
use futures_util::{StreamExt as _, TryStreamExt as _};
use log::{error, info, warn};

#[fuchsia_async::run_singlethreaded]
async fn main() {
    diagnostics_log::init!();
    info!("started");

    let mut fs = ServiceFs::new_local();
    let _: &mut ServiceFsDir<'_, _> =
        fs.dir("svc").add_fidl_service(|s: fnetemul::ConfigurableNetstackRequestStream| s);
    let _: &mut ServiceFs<_> = fs.take_and_serve_directory_handle().expect("take startup handle");
    fs.fuse()
        .flatten_unordered()
        .and_then(handle_request)
        .for_each_concurrent(None, |r| async {
            r.unwrap_or_else(|e| error!("failed to handle configurable netstack request: {:?}", e))
        })
        .await;
    unreachable!("service fs ended unexpectedly")
}

async fn handle_request(request: fnetemul::ConfigurableNetstackRequest) -> Result<(), fidl::Error> {
    match request {
        fnetemul::ConfigurableNetstackRequest::ConfigureInterface { payload, responder } => {
            let mut result = match configure_interface(payload).await {
                Ok(()) => Ok(()),
                Err(e) => {
                    error!("error configuring interface: {:?}", e);
                    Err(e.into())
                }
            };
            match responder.send(&mut result) {
                Err(e) if e.is_closed() => {
                    warn!("channel was closed before response was sent");
                    Ok(())
                }
                other => other,
            }
        }
    }
}

#[derive(thiserror::Error, Debug)]
enum InterfaceConfigError {
    #[error("name not provided")]
    NameNotProvided,
    #[error("device connection not provided")]
    DeviceConnectionNotProvided,
    #[error("error communicating with the netstack or network-tun: {0:?}")]
    Fidl(#[from] fidl::Error),
    #[error("error from netstack: {0:?}")]
    Netstack(#[from] NetstackError),
    #[error("internal error: {0:?}")]
    Internal(#[from] anyhow::Error),
}

#[derive(thiserror::Error, Debug)]
enum NetstackError {
    #[error("failed to add ethernet device with config {config:?}: {status}")]
    AddEthernetDevice { status: zx::Status, config: fnetstack::InterfaceConfig },
    #[error("the interface control channel was closed: {0:?}")]
    InterfaceControl(
        fnet_interfaces_ext::admin::TerminalError<fnet_interfaces_admin::InterfaceRemovedReason>,
    ),
    #[error("failed to enable interface: {0:?}")]
    EnableInterface(fnet_interfaces_admin::ControlEnableError),
    #[error("failed to remove address {addr:?} from interface: {error:?}")]
    RemoveAddress { error: fnet_interfaces_admin::ControlRemoveAddressError, addr: fnet::Subnet },
    #[error("failed to set interface configuration: {0:?}")]
    SetConfiguration(fnet_interfaces_admin::ControlSetConfigurationError),
    #[error("error waiting for address assignment: {0:?}")]
    WaitForAddressAssignment(fnet_interfaces_ext::admin::AddressStateProviderError),
    #[error("failed to add forwarding entry: {0:?}")]
    AddForwardingEntry(fnet_stack::Error),
}

impl Into<fnetemul::ConfigurationError> for InterfaceConfigError {
    fn into(self) -> fnetemul::ConfigurationError {
        match self {
            InterfaceConfigError::NameNotProvided
            | InterfaceConfigError::DeviceConnectionNotProvided => {
                fnetemul::ConfigurationError::InvalidArguments
            }
            // TODO(https://fxbug.dev/95738, https://fxbug.dev/95844): map invalid arguments
            // errors from the netstack to `ConfigurationError.INVALID_ARGUMENTS` rather
            // than `ConfigurationError.REJECTED_BY_NETSTACK`.
            InterfaceConfigError::Netstack(_) => fnetemul::ConfigurationError::RejectedByNetstack,
            InterfaceConfigError::Fidl(_) | InterfaceConfigError::Internal(_) => {
                fnetemul::ConfigurationError::Internal
            }
        }
    }
}

// The default metric used in netemul endpoint configurations.
const DEFAULT_METRIC: u32 = 100;

async fn configure_interface(
    fnetemul::InterfaceOptions {
        name,
        device,
        without_autogenerated_addresses,
        static_ips,
        gateway,
        enable_ipv4_forwarding,
        enable_ipv6_forwarding,
        ..
    }: fnetemul::InterfaceOptions,
) -> Result<(), InterfaceConfigError> {
    let name = name.ok_or(InterfaceConfigError::NameNotProvided)?;
    let device = device.ok_or(InterfaceConfigError::DeviceConnectionNotProvided)?;

    info!("installing and configuring interface '{}'", name);

    let (control, server_end) =
        fnet_interfaces_ext::admin::Control::create_endpoints().context("create endpoints")?;
    let nicid = match device {
        fnetemul_network::DeviceConnection::Ethernet(device) => {
            let netstack = connect_to_protocol::<fnetstack::NetstackMarker>()
                .context("connect to protocol")?;
            let mut config = fnetstack::InterfaceConfig {
                name: name.clone(),
                filepath: format!("/dev/{}", name),
                metric: DEFAULT_METRIC,
            };
            let nicid = netstack
                .add_ethernet_device(&format!("/dev/{}", name), &mut config, device)
                .await?
                .map_err(zx::Status::from_raw)
                .map_err(|status| NetstackError::AddEthernetDevice { status, config })?
                .into();

            let debug_interfaces = connect_to_protocol::<fnet_debug::InterfacesMarker>()
                .context("connect to protocol")?;
            debug_interfaces.get_admin(nicid, server_end)?;
            nicid
        }
        fnetemul_network::DeviceConnection::NetworkDevice(device_instance) => {
            let device = {
                let (proxy, server_end) =
                    fidl::endpoints::create_proxy::<fhardware_network::DeviceMarker>()
                        .context("create proxy")?;
                device_instance
                    .into_proxy()
                    .context("client end into proxy")?
                    .get_device(server_end)?;
                proxy
            };
            let mut port_events = {
                let (proxy, server_end) =
                    fidl::endpoints::create_proxy::<fhardware_network::PortWatcherMarker>()
                        .context("create proxy")?;
                device.get_port_watcher(server_end)?;
                HangingGetStream::new(proxy, fhardware_network::PortWatcherProxy::watch)
            };
            let mut port_id = loop {
                let port_event = port_events.next().await.context("get port event")??;
                match port_event {
                    fhardware_network::DevicePortEvent::Existing(port_id)
                    | fhardware_network::DevicePortEvent::Added(port_id) => {
                        break port_id;
                    }
                    fhardware_network::DevicePortEvent::Idle(fhardware_network::Empty {}) => {
                        info!(
                            "failed to observe port on network device for interface '{}', \
                            waiting...",
                            name
                        );
                    }
                    fhardware_network::DevicePortEvent::Removed(port_id) => {
                        return Err(anyhow!(
                            "unexpected removal of device port {:?} for interface '{}'",
                            port_id,
                            name,
                        )
                        .into());
                    }
                }
            };
            let device_control = {
                let (proxy, server_end) =
                    fidl::endpoints::create_proxy::<fnet_interfaces_admin::DeviceControlMarker>()
                        .context("create proxy")?;
                let device = fidl::endpoints::ClientEnd::new(
                    device.into_channel().expect("extract channel from proxy").into_zx_channel(),
                );
                let installer = connect_to_protocol::<fnet_interfaces_admin::InstallerMarker>()
                    .context("connect to protocol")?;
                installer.install_device(device, server_end)?;
                proxy
            };
            device_control.create_interface(
                &mut port_id,
                server_end,
                fnet_interfaces_admin::Options {
                    name: Some(name.clone()),
                    metric: Some(DEFAULT_METRIC),
                    ..fnet_interfaces_admin::Options::EMPTY
                },
            )?;

            // Ensure the interface won't be removed when we drop the control handles.
            device_control.detach()?;
            control.detach().map_err(NetstackError::InterfaceControl)?;

            control.get_id().await.map_err(NetstackError::InterfaceControl)?
        }
    };

    let _enabled: bool = control
        .enable()
        .await
        .map_err(NetstackError::InterfaceControl)?
        .map_err(NetstackError::EnableInterface)?;

    info!("interface '{}': installed with nicid {}", name, nicid);

    let interface_state =
        connect_to_protocol::<fnet_interfaces::StateMarker>().context("connect to protocol")?;
    fnet_interfaces_ext::wait_interface_with_id(
        fnet_interfaces_ext::event_stream_from_state(&interface_state)
            .context("get interface state watcher events")?,
        &mut fnet_interfaces_ext::InterfaceState::Unknown(nicid),
        |fnet_interfaces_ext::Properties { online, .. }| online.then(|| ()),
    )
    .await
    .context("wait for interface online")?;

    info!("interface '{}': online", name);

    // TODO(fxbug.dev/74595): preempt address autogeneration instead of removing
    // them after they're generated.
    if without_autogenerated_addresses.unwrap_or_default() {
        info!("interface '{}': waiting for link-local address generation", name);
        let mut addresses = fnet_interfaces_ext::wait_interface_with_id(
            fnet_interfaces_ext::event_stream_from_state(&interface_state)
                .context("get interface state watcher events")?,
            &mut fnet_interfaces_ext::InterfaceState::Unknown(nicid),
            |fnet_interfaces_ext::Properties { addresses, .. }| {
                if addresses.is_empty() {
                    None
                } else {
                    Some(addresses.clone())
                }
            },
        )
        .await
        .context("wait for link-local address generation")?;

        let mut interface_addr = match addresses.as_mut_slice() {
            [fnet_interfaces_ext::Address { addr, valid_until: _ }] => Ok(addr),
            addrs => {
                Err(anyhow!("found more than one autogenerated link-local address: {:?}", addrs))
            }
        }?;

        let _removed: bool = control
            .remove_address(&mut interface_addr)
            .await
            .map_err(NetstackError::InterfaceControl)?
            .map_err(|error| NetstackError::RemoveAddress {
                error,
                addr: interface_addr.clone(),
            })?;

        info!("interface '{}': removed auto-generated link-local address", name);
    }

    if let Some(static_ips) = static_ips {
        for mut interface_address in static_ips {
            let address_state_provider = {
                let (address_state_provider, server_end) = fidl::endpoints::create_proxy::<
                    fnet_interfaces_admin::AddressStateProviderMarker,
                >()
                .context("create proxy")?;
                control
                    .add_address(
                        &mut interface_address,
                        fnet_interfaces_admin::AddressParameters::EMPTY,
                        server_end,
                    )
                    .map_err(NetstackError::InterfaceControl)?;
                fnet_interfaces_ext::admin::wait_assignment_state(
                    &mut fnet_interfaces_ext::admin::assignment_state_stream(
                        address_state_provider.clone(),
                    ),
                    fnet_interfaces_admin::AddressAssignmentState::Assigned,
                )
                .await
                .map_err(NetstackError::WaitForAddressAssignment)?;

                address_state_provider
            };

            info!("interface '{}': assigned static IP address {:?}", name, interface_address);

            // Ensure this address won't be removed when we drop the state provider handle.
            address_state_provider.detach()?;
            let subnet = fnet_ext::apply_subnet_mask(interface_address);
            let stack =
                connect_to_protocol::<fnet_stack::StackMarker>().context("connect to protocol")?;
            stack
                .add_forwarding_entry(&mut fnet_stack::ForwardingEntry {
                    subnet,
                    device_id: nicid,
                    next_hop: None,
                    metric: 0,
                })
                .await?
                .map_err(NetstackError::AddForwardingEntry)?;

            info!("interface '{}': added subnet route for {:?}", name, subnet);
        }
    }

    if let Some(gateway) = gateway {
        let unspecified_address = fnet_ext::IpAddress(match gateway {
            fnet::IpAddress::Ipv4(_) => std::net::IpAddr::V4(std::net::Ipv4Addr::UNSPECIFIED),
            fnet::IpAddress::Ipv6(_) => std::net::IpAddr::V6(std::net::Ipv6Addr::UNSPECIFIED),
        })
        .into();
        let stack =
            connect_to_protocol::<fnet_stack::StackMarker>().context("connect to protocol")?;
        stack
            .add_forwarding_entry(&mut fnet_stack::ForwardingEntry {
                subnet: fnet::Subnet { addr: unspecified_address, prefix_len: 0 },
                device_id: nicid,
                next_hop: Some(Box::new(gateway)),
                metric: 0,
            })
            .await?
            .map_err(NetstackError::AddForwardingEntry)?;

        info!("interface '{}': configured default route with gateway address {:?}", name, gateway);
    }

    let ipv4 = enable_ipv4_forwarding.unwrap_or_default();
    let ipv6 = enable_ipv6_forwarding.unwrap_or_default();
    if ipv4 || ipv6 {
        let _prev_config = control
            .set_configuration(fnet_interfaces_admin::Configuration {
                ipv4: Some(fnet_interfaces_admin::Ipv4Configuration {
                    forwarding: ipv4.then(|| true),
                    ..fnet_interfaces_admin::Ipv4Configuration::EMPTY
                }),
                ipv6: Some(fnet_interfaces_admin::Ipv6Configuration {
                    forwarding: ipv6.then(|| true),
                    ..fnet_interfaces_admin::Ipv6Configuration::EMPTY
                }),
                ..fnet_interfaces_admin::Configuration::EMPTY
            })
            .await
            .map_err(NetstackError::InterfaceControl)?
            .map_err(NetstackError::SetConfiguration)?;

        let which = match (ipv4, ipv6) {
            (true, false) => "IPv4",
            (false, true) => "IPv6",
            (true, true) => "IPv4 and IPv6",
            (false, false) => unreachable!(),
        };
        info!("interface '{}': enabled {} forwarding", name, which);
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn missing_name() {
        let result = configure_interface(fnetemul::InterfaceOptions {
            name: None,
            ..fnetemul::InterfaceOptions::EMPTY
        })
        .await;

        assert_matches!(result, Err(InterfaceConfigError::NameNotProvided));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn missing_device() {
        let result = configure_interface(fnetemul::InterfaceOptions {
            name: Some("ep".to_string()),
            device: None,
            ..fnetemul::InterfaceOptions::EMPTY
        })
        .await;

        assert_matches!(result, Err(InterfaceConfigError::DeviceConnectionNotProvided));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn broken_device_instance() {
        let (client_end, server_end) =
            fidl::endpoints::create_endpoints::<fhardware_network::DeviceInstanceMarker>()
                .expect("create endpoints");
        drop(server_end);

        let result = configure_interface(fnetemul::InterfaceOptions {
            name: Some("ep".to_string()),
            device: Some(fnetemul_network::DeviceConnection::NetworkDevice(client_end)),
            ..fnetemul::InterfaceOptions::EMPTY
        })
        .await;

        assert_matches!(result, Err(InterfaceConfigError::Fidl(e)) if e.is_closed());
    }
}
