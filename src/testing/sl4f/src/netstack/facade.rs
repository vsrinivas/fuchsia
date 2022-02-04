// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use once_cell::sync::OnceCell;
use serde::Serialize;

#[derive(Serialize)]
#[cfg_attr(test, derive(Debug, PartialEq))]
pub struct AddressWithPrefix<T> {
    address: T,
    prefix_length: u8,
}

fn serialize_ipv4<S: serde::Serializer>(
    addresses: &Vec<AddressWithPrefix<std::net::Ipv4Addr>>,
    serializer: S,
) -> Result<S::Ok, S::Error> {
    serializer.collect_seq(
        addresses.iter().map(|AddressWithPrefix { address, prefix_length: _ }| address.octets()),
    )
}

fn serialize_ipv6<S: serde::Serializer>(
    addresses: &Vec<AddressWithPrefix<std::net::Ipv6Addr>>,
    serializer: S,
) -> Result<S::Ok, S::Error> {
    serializer.collect_seq(
        addresses.iter().map(|AddressWithPrefix { address, prefix_length: _ }| address.octets()),
    )
}

fn serialize_mac<S: serde::Serializer>(
    mac: &Option<fidl_fuchsia_net_ext::MacAddress>,
    serializer: S,
) -> Result<S::Ok, S::Error> {
    match mac {
        None => serializer.serialize_none(),
        Some(fidl_fuchsia_net_ext::MacAddress { octets }) => serializer.collect_seq(octets.iter()),
    }
}

#[derive(Serialize)]
enum DeviceClass {
    Loopback,
    Virtual,
    Ethernet,
    Wlan,
    Ppp,
    Bridge,
    WlanAp,
}

#[derive(Serialize)]
pub struct Properties {
    id: u64,
    name: String,
    device_class: DeviceClass,
    online: bool,
    #[serde(serialize_with = "serialize_ipv4")]
    ipv4_addresses: Vec<AddressWithPrefix<std::net::Ipv4Addr>>,
    #[serde(serialize_with = "serialize_ipv6")]
    ipv6_addresses: Vec<AddressWithPrefix<std::net::Ipv6Addr>>,
    #[serde(serialize_with = "serialize_mac")]
    mac: Option<fidl_fuchsia_net_ext::MacAddress>,
}

impl From<(fidl_fuchsia_net_interfaces_ext::Properties, Option<fidl_fuchsia_net::MacAddress>)>
    for Properties
{
    fn from(
        t: (fidl_fuchsia_net_interfaces_ext::Properties, Option<fidl_fuchsia_net::MacAddress>),
    ) -> Self {
        use itertools::Itertools as _;

        let (
            fidl_fuchsia_net_interfaces_ext::Properties {
                id,
                name,
                device_class,
                online,
                addresses,
                has_default_ipv4_route: _,
                has_default_ipv6_route: _,
            },
            mac,
        ) = t;
        let device_class = match device_class {
            fidl_fuchsia_net_interfaces::DeviceClass::Loopback(
                fidl_fuchsia_net_interfaces::Empty {},
            ) => DeviceClass::Loopback,
            fidl_fuchsia_net_interfaces::DeviceClass::Device(device) => match device {
                fidl_fuchsia_hardware_network::DeviceClass::Virtual => DeviceClass::Virtual,
                fidl_fuchsia_hardware_network::DeviceClass::Ethernet => DeviceClass::Ethernet,
                fidl_fuchsia_hardware_network::DeviceClass::Wlan => DeviceClass::Wlan,
                fidl_fuchsia_hardware_network::DeviceClass::Ppp => DeviceClass::Ppp,
                fidl_fuchsia_hardware_network::DeviceClass::Bridge => DeviceClass::Bridge,
                fidl_fuchsia_hardware_network::DeviceClass::WlanAp => DeviceClass::WlanAp,
            },
        };
        let (ipv4_addresses, ipv6_addresses) = addresses.into_iter().partition_map(
            |fidl_fuchsia_net_interfaces_ext::Address { addr, valid_until: _ }| {
                let fidl_fuchsia_net_ext::Subnet { addr, prefix_len } = addr.into();
                let fidl_fuchsia_net_ext::IpAddress(addr) = addr;
                match addr {
                    std::net::IpAddr::V4(address) => itertools::Either::Left(AddressWithPrefix {
                        address,
                        prefix_length: prefix_len,
                    }),
                    std::net::IpAddr::V6(address) => itertools::Either::Right(AddressWithPrefix {
                        address,
                        prefix_length: prefix_len,
                    }),
                }
            },
        );
        Self {
            id,
            name,
            device_class,
            online,
            ipv4_addresses,
            ipv6_addresses,
            mac: mac.map(Into::into),
        }
    }
}

/// Network stack operations.
#[derive(Debug, Default)]
pub struct NetstackFacade {
    interfaces_state: OnceCell<fidl_fuchsia_net_interfaces::StateProxy>,
    debug_interfaces: OnceCell<fidl_fuchsia_net_debug::InterfacesProxy>,
}

// Path to hub-v2 netstack exposed directory.
const NETSTACK_EXPOSED_DIR: &str =
    "/hub-v2/children/core/children/network/children/netstack/exec/expose";

impl NetstackFacade {
    fn get_interfaces_state(&self) -> Result<&fidl_fuchsia_net_interfaces::StateProxy, Error> {
        let Self { interfaces_state, debug_interfaces: _ } = self;
        interfaces_state.get_or_try_init(|| {
            fuchsia_component::client::connect_to_protocol_at::<
                fidl_fuchsia_net_interfaces::StateMarker,
            >(NETSTACK_EXPOSED_DIR)
        })
    }

    fn get_debug_interfaces(&self) -> Result<&fidl_fuchsia_net_debug::InterfacesProxy, Error> {
        let Self { interfaces_state: _, debug_interfaces } = self;
        debug_interfaces.get_or_try_init(|| {
            fuchsia_component::client::connect_to_protocol_at::<
                fidl_fuchsia_net_debug::InterfacesMarker,
            >(NETSTACK_EXPOSED_DIR)
        })
    }

    fn get_control(
        &self,
        id: u64,
    ) -> Result<fidl_fuchsia_net_interfaces_ext::admin::Control, Error> {
        let debug_interfaces = self.get_debug_interfaces()?;
        let (control, server_end) =
            fidl_fuchsia_net_interfaces_ext::admin::Control::create_endpoints()
                .context("create admin control endpoints")?;
        let () = debug_interfaces.get_admin(id, server_end).context("send get admin request")?;
        Ok(control)
    }

    pub async fn enable_interface(&self, id: u64) -> Result<(), Error> {
        let control = self.get_control(id)?;
        let _did_enable: bool = control
            .enable()
            .await
            .map_err(anyhow::Error::new)
            .and_then(|res| {
                res.map_err(|e: fidl_fuchsia_net_interfaces_admin::ControlEnableError| {
                    anyhow::anyhow!("{:?}", e)
                })
            })
            .with_context(|| format!("failed to enable interface {}", id))?;
        Ok(())
    }

    pub async fn disable_interface(&self, id: u64) -> Result<(), Error> {
        let control = self.get_control(id)?;
        let _did_disable: bool = control
            .disable()
            .await
            .map_err(anyhow::Error::new)
            .and_then(|res| {
                res.map_err(|e: fidl_fuchsia_net_interfaces_admin::ControlDisableError| {
                    anyhow::anyhow!("{:?}", e)
                })
            })
            .with_context(|| format!("failed to disable interface {}", id))?;
        Ok(())
    }

    pub async fn list_interfaces(&self) -> Result<Vec<Properties>, Error> {
        let interfaces_state = self.get_interfaces_state()?;
        let debug_interfaces = self.get_debug_interfaces()?;
        let stream = fidl_fuchsia_net_interfaces_ext::event_stream_from_state(interfaces_state)?;
        let response =
            fidl_fuchsia_net_interfaces_ext::existing(stream, std::collections::HashMap::new())
                .await?;
        let response = response.into_values().map(|properties| async {
            let fidl_fuchsia_net_interfaces_ext::Properties { id, .. } = &properties;
            let mac = debug_interfaces.get_mac(*id).await?;
            let mac = mac.map_err(|e| match e {
                fidl_fuchsia_net_debug::InterfacesGetMacError::NotFound => {
                    anyhow::anyhow!("interface with id={} not found", id)
                }
            })?;
            let mac = mac.map(|box_| *box_);
            let view = (properties, mac).into();
            Ok::<_, Error>(view)
        });
        let mut response = futures::future::try_join_all(response).await?;
        let () = response.sort_by_key(|Properties { id, .. }| *id);
        Ok(response)
    }

    async fn get_addresses<T, F: Copy + FnMut(fidl_fuchsia_net::Subnet) -> Option<T>>(
        &self,
        f: F,
    ) -> Result<Vec<T>, Error> {
        let mut output = Vec::new();

        let interfaces_state = self.get_interfaces_state()?;
        let (watcher, server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces::WatcherMarker>()?;
        let () = interfaces_state
            .get_watcher(fidl_fuchsia_net_interfaces::WatcherOptions::EMPTY, server)?;

        loop {
            match watcher.watch().await? {
                fidl_fuchsia_net_interfaces::Event::Existing(
                    fidl_fuchsia_net_interfaces::Properties { addresses, .. },
                ) => {
                    let addresses = addresses.unwrap();
                    let () = output.extend(
                        addresses
                            .into_iter()
                            .map(|fidl_fuchsia_net_interfaces::Address { addr, .. }| addr.unwrap())
                            .filter_map(f),
                    );
                }
                fidl_fuchsia_net_interfaces::Event::Idle(fidl_fuchsia_net_interfaces::Empty {}) => {
                    break
                }
                event => unreachable!("{:?}", event),
            }
        }

        Ok(output)
    }

    pub fn get_ipv6_addresses(
        &self,
    ) -> impl std::future::Future<Output = Result<Vec<AddressWithPrefix<std::net::Ipv6Addr>>, Error>> + '_
    {
        self.get_addresses(|addr| {
            let fidl_fuchsia_net_ext::Subnet { addr, prefix_len } = addr.into();
            let fidl_fuchsia_net_ext::IpAddress(addr) = addr;
            match addr {
                std::net::IpAddr::V4(address) => {
                    let _: std::net::Ipv4Addr = address;
                    None
                }
                std::net::IpAddr::V6(address) => {
                    Some(AddressWithPrefix { address, prefix_length: prefix_len })
                }
            }
        })
    }

    pub fn get_link_local_ipv6_addresses(
        &self,
    ) -> impl std::future::Future<Output = Result<Vec<AddressWithPrefix<std::net::Ipv6Addr>>, Error>> + '_
    {
        self.get_addresses(|addr| {
            let fidl_fuchsia_net_ext::Subnet { addr, prefix_len } = addr.into();
            let fidl_fuchsia_net_ext::IpAddress(addr) = addr;
            match addr {
                std::net::IpAddr::V4(address) => {
                    let _: std::net::Ipv4Addr = address;
                    None
                }
                std::net::IpAddr::V6(address) => (address.octets()[..2] == [0xfe, 0x80])
                    .then(|| AddressWithPrefix { address, prefix_length: prefix_len }),
            }
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_net as fnet;
    use fidl_fuchsia_net_interfaces as finterfaces;
    use fuchsia_async as fasync;
    use futures::StreamExt as _;

    struct MockStateTester {
        expected_state: Vec<Box<dyn FnOnce(finterfaces::WatcherRequest) + Send + 'static>>,
    }

    impl MockStateTester {
        fn new() -> Self {
            Self { expected_state: vec![] }
        }

        pub fn create_facade_and_serve_state(
            self,
        ) -> (NetstackFacade, impl std::future::Future<Output = ()>) {
            let (interfaces_state, stream_future) = self.build_state_and_watcher();
            (
                NetstackFacade { interfaces_state: interfaces_state.into(), ..Default::default() },
                stream_future,
            )
        }

        fn push_state(
            mut self,
            request: impl FnOnce(finterfaces::WatcherRequest) + Send + 'static,
        ) -> Self {
            self.expected_state.push(Box::new(request));
            self
        }

        fn build_state_and_watcher(
            self,
        ) -> (finterfaces::StateProxy, impl std::future::Future<Output = ()>) {
            let (proxy, mut stream) =
                fidl::endpoints::create_proxy_and_stream::<finterfaces::StateMarker>().unwrap();
            let stream_fut = async move {
                match stream.next().await {
                    Some(Ok(finterfaces::StateRequest::GetWatcher { watcher, .. })) => {
                        let mut into_stream = watcher.into_stream().unwrap();
                        for expected in self.expected_state {
                            let () = expected(into_stream.next().await.unwrap().unwrap());
                        }
                        let finterfaces::WatcherRequest::Watch { responder } =
                            into_stream.next().await.unwrap().unwrap();
                        let () = responder
                            .send(&mut finterfaces::Event::Idle(finterfaces::Empty {}))
                            .unwrap();
                    }
                    err => panic!("Error in request handler: {:?}", err),
                }
            };
            (proxy, stream_fut)
        }

        fn expect_get_ipv6_addresses(self, result: Vec<fnet::Subnet>) -> Self {
            let addresses = result
                .into_iter()
                .map(|addr| finterfaces::Address {
                    addr: Some(addr),
                    ..finterfaces::Address::EMPTY
                })
                .collect();
            self.push_state(move |req| match req {
                finterfaces::WatcherRequest::Watch { responder } => responder
                    .send(&mut finterfaces::Event::Existing(finterfaces::Properties {
                        addresses: Some(addresses),
                        ..finterfaces::Properties::EMPTY
                    }))
                    .unwrap(),
            })
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_ipv6_addresses() {
        let ipv6_octets = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15];
        let prefix_length = 2;

        let ipv6_address_with_prefix =
            AddressWithPrefix { address: ipv6_octets.into(), prefix_length };
        let ipv6_address = fnet::Subnet {
            addr: fnet::IpAddress::Ipv6(fnet::Ipv6Address { addr: ipv6_octets }),
            prefix_len: prefix_length,
        };
        let ipv4_address = fnet::Subnet {
            addr: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [0, 1, 2, 3] }),
            prefix_len: 2,
        };
        let all_addresses = [ipv6_address.clone(), ipv4_address.clone()];
        let (facade, stream_fut) = MockStateTester::new()
            .expect_get_ipv6_addresses(all_addresses.to_vec())
            .create_facade_and_serve_state();
        let facade_fut = async move {
            let result_address: Vec<_> = facade.get_ipv6_addresses().await.unwrap();
            assert_eq!(result_address, [ipv6_address_with_prefix]);
        };
        futures::future::join(facade_fut, stream_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_link_local_ipv6_addresses() {
        let ipv6_address = fnet::Subnet {
            addr: fnet::IpAddress::Ipv6(fnet::Ipv6Address {
                addr: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15],
            }),
            prefix_len: 2,
        };
        let link_local_ipv6_octets = [0xfe, 0x80, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15];
        let prefix_length = 2;
        let link_local_ipv6_address_with_prefix =
            AddressWithPrefix { address: link_local_ipv6_octets.into(), prefix_length };
        let link_local_ipv6_address = fnet::Subnet {
            addr: fnet::IpAddress::Ipv6(fnet::Ipv6Address { addr: link_local_ipv6_octets }),
            prefix_len: prefix_length,
        };
        let ipv4_address = fnet::Subnet {
            addr: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [0, 1, 2, 3] }),
            prefix_len: 2,
        };
        let all_addresses =
            [ipv6_address.clone(), link_local_ipv6_address.clone(), ipv4_address.clone()];
        let (facade, stream_fut) = MockStateTester::new()
            .expect_get_ipv6_addresses(all_addresses.to_vec())
            .create_facade_and_serve_state();
        let facade_fut = async move {
            let result_address: Vec<_> = facade.get_link_local_ipv6_addresses().await.unwrap();
            assert_eq!(result_address, [link_local_ipv6_address_with_prefix]);
        };
        futures::future::join(facade_fut, stream_fut).await;
    }
}
