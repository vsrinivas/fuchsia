// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl_fuchsia_net_stack::{StackMarker, StackProxy};
use fuchsia_component as app;
use fuchsia_syslog::{self, fx_log_err, fx_log_info};
use parking_lot::RwLock;

use crate::common_utils::common::macros::{fx_err_and_bail, with_line};
use crate::netstack::types::CustomInterfaceInfo;

#[derive(Debug)]
struct InnerNestackFacade {
    /// The current Netstack Proxy
    netstack_proxy: Option<StackProxy>,
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
        NetstackFacade { inner: RwLock::new(InnerNestackFacade { netstack_proxy: None }) }
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

    pub fn init_netstack_proxy(&self) -> Result<(), Error> {
        self.inner.write().netstack_proxy = Some(self.create_netstack_proxy()?);
        Ok(())
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
}
