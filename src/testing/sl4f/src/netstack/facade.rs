// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::types::AddressDto;
use crate::common_utils::common::macros::{fx_err_and_bail, with_line};
use crate::netstack::types::CustomInterfaceInfo;
use anyhow::{Context as _, Error};
use fidl::endpoints::create_proxy;
use fidl_fuchsia_net::IpAddress;
use fidl_fuchsia_net_interfaces::{
    Address, Event, StateMarker, StateProxy, WatcherMarker, WatcherOptions, WatcherProxy,
};
use fidl_fuchsia_net_stack::{StackMarker, StackProxy};
use fuchsia_component as app;
use fuchsia_syslog::{self, fx_log_err, fx_log_info};
use parking_lot::RwLock;

#[derive(Debug)]
struct InnerNestackFacade {
    /// The current Netstack Proxy
    netstack_proxy: Option<StackProxy>,
    /// The proxy to access the lowpan State service.
    state_proxy: Option<StateProxy>,
}

/// Perform Netstack operations.
///
/// Note this object is shared among all threads created by server.
///
#[derive(Debug)]
pub struct NetstackFacade {
    inner: RwLock<InnerNestackFacade>,
}

impl NetstackFacade {
    pub fn new() -> NetstackFacade {
        NetstackFacade {
            inner: RwLock::new(InnerNestackFacade { netstack_proxy: None, state_proxy: None }),
        }
    }

    /// Creates a Netstack Proxy.
    pub fn create_netstack_proxy(&self) -> Result<StackProxy, Error> {
        let tag = "NestackFacade::create_netstack_proxy";
        match self.inner.read().netstack_proxy.clone() {
            Some(netstack_proxy) => {
                fx_log_info!(tag: &with_line!(tag), "Current netstack proxy: {:?}", netstack_proxy);
                Ok(netstack_proxy)
            }
            None => {
                fx_log_info!(tag: &with_line!(tag), "Setting new netstack proxy");
                let netstack_proxy = app::client::connect_to_service::<StackMarker>();
                if let Err(err) = netstack_proxy {
                    fx_err_and_bail!(
                        &with_line!(tag),
                        format_err!("Failed to create profile server proxy: {}", err)
                    );
                }
                netstack_proxy
            }
        }
    }

    /// Creates a Netstack Proxy.
    pub fn create_state_proxy(&self) -> Result<StateProxy, Error> {
        let tag = "NestackFacade::create_state_proxy";
        match self.inner.read().state_proxy.clone() {
            Some(state_proxy) => {
                fx_log_info!(tag: &with_line!(tag), "Current state proxy: {:?}", state_proxy);
                Ok(state_proxy)
            }
            None => {
                fx_log_info!(tag: &with_line!(tag), "Setting new state proxy");
                let state_proxy = app::client::connect_to_service::<StateMarker>();
                if let Err(err) = state_proxy {
                    fx_err_and_bail!(
                        &with_line!(tag),
                        format_err!("Failed to create profile server proxy: {}", err)
                    );
                }
                state_proxy
            }
        }
    }

    pub fn init_netstack_proxy(&self) -> Result<(), Error> {
        self.inner.write().netstack_proxy = Some(self.create_netstack_proxy()?);
        self.inner.write().state_proxy = Some(self.create_state_proxy()?);
        Ok(())
    }

    /// Returns the PairingStateWatcher proxy provided on instantiation.
    fn watcher(&self) -> Result<WatcherProxy, Error> {
        let (watching_proxy, watching_server_end) = create_proxy::<WatcherMarker>()?;
        match self.inner.read().state_proxy.as_ref() {
            Some(state) => {
                state.get_watcher(WatcherOptions::EMPTY, watching_server_end)?;
            }
            None => bail!("State proxy is not set!"),
        }
        Ok(watching_proxy)
    }

    pub async fn list_interfaces(&self) -> Result<Vec<CustomInterfaceInfo>, Error> {
        let tag = "NestackFacade::list_interfaces";
        let raw_interface_list = match &self.inner.read().netstack_proxy {
            Some(proxy) => proxy.list_interfaces().await?,
            None => fx_err_and_bail!(&with_line!(tag), "No Server Proxy created."),
        };

        let mut interface_list: Vec<CustomInterfaceInfo> = Vec::new();
        for interface in raw_interface_list {
            interface_list.push(CustomInterfaceInfo::new(&interface));
        }

        Ok(interface_list)
    }

    pub async fn get_interface_info(&self, id: u64) -> Result<CustomInterfaceInfo, Error> {
        let tag = "NestackFacade::get_interface_info";
        match &self.inner.read().netstack_proxy {
            Some(proxy) => {
                match proxy.get_interface_info(id).await.context("failed to get interface info")? {
                    Ok(info) => Ok(CustomInterfaceInfo::new(&info)),
                    Err(e) => {
                        let err_msg =
                            format!("Unable to get interface info for id {:?}: {:?}", id, e);
                        fx_err_and_bail!(&with_line!(tag), err_msg)
                    }
                }
            }
            None => fx_err_and_bail!(&with_line!(tag), "No Server Proxy created."),
        }
    }

    pub async fn enable_interface(&self, id: u64) -> Result<(), Error> {
        let tag = "NestackFacade::enable_interface";
        match &self.inner.read().netstack_proxy {
            Some(proxy) => {
                let res = proxy.enable_interface(id).await?;
                match res {
                    Ok(()) => (),
                    Err(e) => {
                        let err_msg = format!("Unable to enable interface id {:?}: {:?}", id, e);
                        fx_err_and_bail!(&with_line!(tag), err_msg)
                    }
                }
            }
            None => fx_err_and_bail!(&with_line!(tag), "No Server Proxy created."),
        };
        Ok(())
    }

    pub async fn disable_interface(&self, id: u64) -> Result<(), Error> {
        let tag = "NestackFacade::disable_interface";
        match &self.inner.read().netstack_proxy {
            Some(proxy) => {
                let res = proxy.disable_interface(id).await?;
                match res {
                    Ok(()) => (),
                    Err(e) => {
                        let err_msg = format!("Unable to disable interface id {:?}: {:?}", id, e);
                        fx_err_and_bail!(&with_line!(tag), err_msg)
                    }
                }
            }
            None => fx_err_and_bail!(&with_line!(tag), "No Server Proxy created."),
        };
        Ok(())
    }

    /// Returns the ipv6 addresses that are registered with the PairingStateWatcher.
    pub async fn get_ipv6_addresses(&self) -> Result<Vec<AddressDto>, Error> {
        let mut addresses: Vec<AddressDto> = Vec::new();
        let watcher = self.watcher()?;
        loop {
            match watcher.watch().await {
                Ok(Event::Existing(existing)) => {
                    addresses.append(
                        &mut existing
                            .addresses
                            .unwrap()
                            .into_iter()
                            .filter(|addr: &Address| match addr.addr {
                                Some(subnet) => match subnet.addr {
                                    IpAddress::Ipv6(_ipv6) => true,
                                    _ => false,
                                },
                                None => false,
                            })
                            .map(|addr: Address| addr.into())
                            .collect(),
                    );
                }
                _ => {
                    break;
                }
            }
        }
        Ok(addresses)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::netstack::types::AddressDto;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_net::{IpAddress, Ipv4Address, Ipv6Address, Subnet};
    use fidl_fuchsia_net_interfaces::{Address, Properties, StateRequest, WatcherRequest};
    use fuchsia_async as fasync;
    use futures::prelude::*;

    struct MockStateTester {
        expected_state: Vec<Box<dyn FnOnce(WatcherRequest) + Send + 'static>>,
    }

    impl MockStateTester {
        fn new() -> Self {
            Self { expected_state: vec![] }
        }

        pub fn create_facade_and_serve_state(self) -> (NetstackFacade, impl Future<Output = ()>) {
            let (state_proxy, stream_future) = self.build_state_and_watcher();
            (
                NetstackFacade {
                    inner: RwLock::new(InnerNestackFacade { netstack_proxy: None, state_proxy }),
                },
                stream_future,
            )
        }

        fn push_state(mut self, request: impl FnOnce(WatcherRequest) + Send + 'static) -> Self {
            self.expected_state.push(Box::new(request));
            self
        }

        fn build_state_and_watcher(self) -> (Option<StateProxy>, impl Future<Output = ()>) {
            let (proxy, mut stream) = create_proxy_and_stream::<StateMarker>().unwrap();
            let stream_fut = async move {
                match stream.next().await {
                    Some(Ok(StateRequest::GetWatcher { watcher, .. })) => {
                        let mut into_stream = watcher.into_stream().unwrap();
                        for expected in self.expected_state {
                            expected(into_stream.next().await.unwrap().unwrap());
                        }
                    }
                    err => panic!("Error in request handler: {:?}", err),
                }
            };
            (Some(proxy), stream_fut)
        }

        fn expect_get_ipv6_addresses(self, result: Vec<Address>) -> Self {
            self.push_state(move |req| match req {
                WatcherRequest::Watch { responder } => {
                    responder
                        .send(&mut Event::Existing(Properties {
                            addresses: Some(result),
                            ..Properties::EMPTY
                        }))
                        .unwrap();
                }
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
            let result_address = facade.get_ipv6_addresses().await.unwrap();
            assert_eq!(result_address.len(), ipv6_addresses.len());
            assert!(result_address
                .into_iter()
                .zip(ipv6_addresses.iter())
                .all(|a: (AddressDto, &Address)| Into::<Address>::into(a.0) == (*a.1)));
        };
        future::join(facade_fut, stream_fut).await;
    }
}
