// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::types::AddressDto;
use crate::netstack::types::CustomInterfaceInfo;
use anyhow::{anyhow, Error};
use once_cell::sync::OnceCell;

/// Network stack operations.
#[derive(Debug, Default)]
pub struct NetstackFacade {
    net_stack: OnceCell<fidl_fuchsia_net_stack::StackProxy>,
    interfaces_state: OnceCell<fidl_fuchsia_net_interfaces::StateProxy>,
}

impl NetstackFacade {
    fn get_net_stack(&self) -> Result<&fidl_fuchsia_net_stack::StackProxy, Error> {
        let Self { net_stack, interfaces_state: _ } = self;
        net_stack.get_or_try_init(
            fuchsia_component::client::connect_to_protocol::<fidl_fuchsia_net_stack::StackMarker>,
        )
    }

    fn get_interfaces_state(&self) -> Result<&fidl_fuchsia_net_interfaces::StateProxy, Error> {
        let Self { net_stack: _, interfaces_state } = self;
        interfaces_state.get_or_try_init(
            fuchsia_component::client::connect_to_protocol::<
                fidl_fuchsia_net_interfaces::StateMarker,
            >,
        )
    }

    pub async fn list_interfaces(&self) -> Result<Vec<CustomInterfaceInfo>, Error> {
        let net_stack = self.get_net_stack()?;
        let interface_list = net_stack.list_interfaces().await?;
        Ok(interface_list.iter().map(CustomInterfaceInfo::new).collect())
    }

    pub async fn get_interface_info(&self, id: u64) -> Result<CustomInterfaceInfo, Error> {
        let net_stack = self.get_net_stack()?;
        let info = net_stack.get_interface_info(id).await?.map_err(
            |err: fidl_fuchsia_net_stack::Error| {
                anyhow!("failed to get interface {} info: {:?}", id, err)
            },
        )?;
        Ok(CustomInterfaceInfo::new(&info))
    }

    pub async fn enable_interface(&self, id: u64) -> Result<(), Error> {
        let net_stack = self.get_net_stack()?;
        net_stack.enable_interface(id).await?.map_err(|err: fidl_fuchsia_net_stack::Error| {
            anyhow!("failed to enable interface {}: {:?}", id, err)
        })
    }

    pub async fn disable_interface(&self, id: u64) -> Result<(), Error> {
        let net_stack = self.get_net_stack()?;
        net_stack.disable_interface(id).await?.map_err(|err: fidl_fuchsia_net_stack::Error| {
            anyhow!("failed to disable interface {}: {:?}", id, err)
        })
    }

    async fn get_addresses<F: Copy + FnMut(fidl_fuchsia_net::Subnet) -> Option<AddressDto>>(
        &self,
        f: F,
    ) -> Result<Vec<AddressDto>, Error> {
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
    ) -> impl std::future::Future<Output = Result<Vec<AddressDto>, Error>> + '_ {
        self.get_addresses(|subnet| {
            let fidl_fuchsia_net::Subnet { addr, prefix_len: _ } = subnet;
            match addr {
                fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address { addr: _ }) => {
                    None
                }
                fidl_fuchsia_net::IpAddress::Ipv6(fidl_fuchsia_net::Ipv6Address { addr: _ }) => {
                    Some(AddressDto { addr: Some(subnet.into()) })
                }
            }
        })
    }

    pub fn get_link_local_ipv6_addresses(
        &self,
    ) -> impl std::future::Future<Output = Result<Vec<AddressDto>, Error>> + '_ {
        self.get_addresses(|subnet| {
            let fidl_fuchsia_net::Subnet { addr, prefix_len: _ } = subnet;
            match addr {
                fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address { addr: _ }) => {
                    None
                }
                fidl_fuchsia_net::IpAddress::Ipv6(fidl_fuchsia_net::Ipv6Address {
                    addr: octets,
                }) => (octets[0] == 254 && octets[1] == 128)
                    .then(|| AddressDto { addr: Some(subnet.into()) }),
            }
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_net::{IpAddress, Ipv4Address, Ipv6Address, Subnet};
    use fidl_fuchsia_net_interfaces::{
        Address, Empty, Event, Properties, StateMarker, StateProxy, StateRequest, WatcherRequest,
    };
    use fuchsia_async as fasync;
    use futures::StreamExt as _;

    struct MockStateTester {
        expected_state: Vec<Box<dyn FnOnce(WatcherRequest) + Send + 'static>>,
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

        fn push_state(mut self, request: impl FnOnce(WatcherRequest) + Send + 'static) -> Self {
            self.expected_state.push(Box::new(request));
            self
        }

        fn build_state_and_watcher(self) -> (StateProxy, impl std::future::Future<Output = ()>) {
            let (proxy, mut stream) =
                fidl::endpoints::create_proxy_and_stream::<StateMarker>().unwrap();
            let stream_fut = async move {
                match stream.next().await {
                    Some(Ok(StateRequest::GetWatcher { watcher, .. })) => {
                        let mut into_stream = watcher.into_stream().unwrap();
                        for expected in self.expected_state {
                            let () = expected(into_stream.next().await.unwrap().unwrap());
                        }
                        let WatcherRequest::Watch { responder } =
                            into_stream.next().await.unwrap().unwrap();
                        let () = responder.send(&mut Event::Idle(Empty {})).unwrap();
                    }
                    err => panic!("Error in request handler: {:?}", err),
                }
            };
            (proxy, stream_fut)
        }

        fn expect_get_ipv6_addresses(self, result: Vec<Address>) -> Self {
            self.push_state(move |req| match req {
                WatcherRequest::Watch { responder } => responder
                    .send(&mut Event::Existing(Properties {
                        addresses: Some(result),
                        ..Properties::EMPTY
                    }))
                    .unwrap(),
            })
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_ipv6_addresses() {
        let ipv6_address = Address {
            addr: Some(Subnet {
                addr: IpAddress::Ipv6(Ipv6Address {
                    addr: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15],
                }),
                prefix_len: 2,
            }),
            ..Address::EMPTY
        };
        let ipv4_address = Address {
            addr: Some(Subnet {
                addr: IpAddress::Ipv4(Ipv4Address { addr: [0, 1, 2, 3] }),
                prefix_len: 2,
            }),
            ..Address::EMPTY
        };
        let ipv6_addresses = [ipv6_address.clone()];
        let all_addresses = [ipv6_address.clone(), ipv4_address.clone()];
        let (facade, stream_fut) = MockStateTester::new()
            .expect_get_ipv6_addresses(all_addresses.to_vec())
            .create_facade_and_serve_state();
        let facade_fut = async move {
            let result_address: Vec<Address> =
                facade.get_ipv6_addresses().await.unwrap().into_iter().map(Into::into).collect();
            assert_eq!(result_address, ipv6_addresses);
        };
        futures::future::join(facade_fut, stream_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_link_local_ipv6_addresses() {
        let ipv6_address = Address {
            addr: Some(Subnet {
                addr: IpAddress::Ipv6(Ipv6Address {
                    addr: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15],
                }),
                prefix_len: 2,
            }),
            ..Address::EMPTY
        };
        let link_local_ipv6_address = Address {
            addr: Some(Subnet {
                addr: IpAddress::Ipv6(Ipv6Address {
                    addr: [254, 128, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15],
                }),
                prefix_len: 2,
            }),
            ..Address::EMPTY
        };
        let ipv4_address = Address {
            addr: Some(Subnet {
                addr: IpAddress::Ipv4(Ipv4Address { addr: [0, 1, 2, 3] }),
                prefix_len: 2,
            }),
            ..Address::EMPTY
        };
        let link_local_ipv6_addresses = [link_local_ipv6_address.clone()];
        let all_addresses =
            [ipv6_address.clone(), link_local_ipv6_address.clone(), ipv4_address.clone()];
        let (facade, stream_fut) = MockStateTester::new()
            .expect_get_ipv6_addresses(all_addresses.to_vec())
            .create_facade_and_serve_state();
        let facade_fut = async move {
            let result_address: Vec<Address> = facade
                .get_link_local_ipv6_addresses()
                .await
                .unwrap()
                .into_iter()
                .map(Into::into)
                .collect();
            assert_eq!(result_address, link_local_ipv6_addresses);
        };
        futures::future::join(facade_fut, stream_fut).await;
    }
}
