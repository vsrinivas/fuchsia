// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fidl_fuchsia_bluetooth_control::{ControlMarker, ControlProxy};
use fuchsia_bluetooth::error::Error as BTError;
use fuchsia_component as component;
use fuchsia_syslog::macros::*;

use parking_lot::RwLock;
use std::collections::HashMap;

use crate::bluetooth::types::CustomRemoteDevice;
use crate::server::sl4f::macros::with_line;

static ERR_NO_CONTROL_PROXY_DETECTED: &'static str = "No Bluetooth Control Proxy detected.";

#[derive(Debug)]
struct InnerBluetoothControlFacade {
    /// The current Bluetooth Control Interface Proxy
    control_interface_proxy: Option<ControlProxy>,

    /// Discovered device list
    discovered_device_list: HashMap<String, CustomRemoteDevice>,
}

#[derive(Debug)]
pub struct BluetoothControlFacade {
    inner: RwLock<InnerBluetoothControlFacade>,
}

/// Perform Bluetooth Control operations.
///
/// Note this object is shared among all threads created by server.
///
impl BluetoothControlFacade {
    pub fn new() -> BluetoothControlFacade {
        BluetoothControlFacade {
            inner: RwLock::new(InnerBluetoothControlFacade {
                control_interface_proxy: None,
                discovered_device_list: HashMap::new(),
            }),
        }
    }

    pub fn create_control_interface_proxy(&self) -> Result<ControlProxy, Error> {
        let tag = "BluetoothControlFacade::create_control_interface_proxy";
        match self.inner.read().control_interface_proxy.clone() {
            Some(control_interface_proxy) => {
                fx_log_info!(
                    tag: &with_line!(tag),
                    "Current control interface proxy: {:?}",
                    control_interface_proxy
                );
                Ok(control_interface_proxy)
            }
            None => {
                fx_log_info!(tag: &with_line!(tag), "Setting new control interface proxy");
                let control_interface_proxy =
                    component::client::connect_to_service::<ControlMarker>();
                if let Err(err) = control_interface_proxy {
                    fx_log_err!(
                        tag: &with_line!(tag),
                        "Failed to create control interface proxy: {:?}",
                        err
                    );
                    bail!("Failed to create control interface proxy: {:?}", err);
                }
                control_interface_proxy
            }
        }
    }

    /// Sets a control proxy to use if one is not already in use.
    pub async fn init_control_interface_proxy(&self) -> Result<(), Error> {
        self.inner.write().control_interface_proxy = Some(self.create_control_interface_proxy()?);
        Ok(())
    }

    /// Sets the current control proxy to be discoverable.
    ///
    /// # Arguments
    /// * 'discoverable' - A bool object for setting Bluetooth device discoverable or not.
    pub async fn set_discoverable(&self, discoverable: bool) -> Result<(), Error> {
        let tag = "BluetoothControlFacade::set_discoverable";
        match &self.inner.read().control_interface_proxy {
            Some(proxy) => {
                let resp = await!(proxy.set_discoverable(discoverable))?;
                match resp.error {
                    Some(err) => {
                        fx_log_err!(tag: &with_line!(tag), "Error: {:?}", err);
                        bail!(BTError::from(*err))
                    }
                    None => Ok(()),
                }
            }
            None => {
                fx_log_err!(
                    tag: &with_line!(tag),
                    "{:?}",
                    ERR_NO_CONTROL_PROXY_DETECTED.to_string()
                );
                bail!(ERR_NO_CONTROL_PROXY_DETECTED.to_string())
            }
        }
    }

    /// Sets the current control proxy name.
    ///
    /// # Arguments
    /// * 'name' - A String object representing the name to set.
    pub async fn set_name(&self, name: String) -> Result<(), Error> {
        let tag = "BluetoothControlFacade::set_name";
        match &self.inner.read().control_interface_proxy {
            Some(proxy) => {
                let resp = await!(proxy.set_name(Some(&name)))?;
                match resp.error {
                    Some(err) => {
                        fx_log_err!(tag: &with_line!(tag), "Error: {:?}", err);
                        bail!(BTError::from(*err))
                    }
                    None => Ok(()),
                }
            }
            None => {
                fx_log_err!(
                    tag: &with_line!(tag),
                    "{:?}",
                    ERR_NO_CONTROL_PROXY_DETECTED.to_string()
                );
                bail!(ERR_NO_CONTROL_PROXY_DETECTED.to_string())
            }
        }
    }

    /// Requests discovery on the Bluetooth Control Proxy.
    ///
    /// # Arguments
    /// * 'discovery' - A bool representing starting and stopping discovery.
    pub async fn request_discovery(&self, discovery: bool) -> Result<(), Error> {
        let tag = "BluetoothControlFacade::request_discovery";
        match &self.inner.read().control_interface_proxy {
            Some(proxy) => {
                let resp = await!(proxy.request_discovery(discovery))?;
                match resp.error {
                    Some(err) => {
                        fx_log_err!(tag: &with_line!(tag), "Error: {:?}", err);
                        bail!(BTError::from(*err))
                    }
                    None => Ok(()),
                }
            }
            None => {
                fx_log_err!(
                    tag: &with_line!(tag),
                    "{:?}",
                    ERR_NO_CONTROL_PROXY_DETECTED.to_string()
                );
                bail!(ERR_NO_CONTROL_PROXY_DETECTED.to_string())
            }
        }
    }

    /// Returns a hash of the known devices on the Bluetooth Control proxy.
    pub async fn get_known_remote_devices(
        &self,
    ) -> Result<HashMap<String, CustomRemoteDevice>, Error> {
        let tag = "BluetoothControlFacade::get_known_remote_devices";
        &self.inner.write().discovered_device_list.clear();

        let discovered_devices = match &self.inner.read().control_interface_proxy {
            Some(proxy) => await!(proxy.get_known_remote_devices())?,
            None => {
                fx_log_err!(
                    tag: &with_line!(tag),
                    "{:?}",
                    ERR_NO_CONTROL_PROXY_DETECTED.to_string()
                );
                bail!(ERR_NO_CONTROL_PROXY_DETECTED.to_string())
            }
        };
        self.inner.write().discovered_device_list =
            discovered_devices.iter().map(|d| (d.identifier.clone(), d.into())).collect();
        Ok(self.inner.read().discovered_device_list.clone())
    }

    /// Forgets (Unbonds) an input device ID.
    ///
    /// # Arguments
    /// * 'id' - A String representing the device ID.
    pub async fn forget(&self, id: String) -> Result<(), Error> {
        let tag = "BluetoothControlFacade::forget";
        match &self.inner.read().control_interface_proxy {
            Some(proxy) => {
                let resp = await!(proxy.forget(&id))?;
                match resp.error {
                    Some(err) => {
                        fx_log_err!(tag: &with_line!(tag), "Error: {:?}", err);
                        bail!(BTError::from(*err))
                    }
                    None => Ok(()),
                }
            }
            None => {
                fx_log_err!(
                    tag: &with_line!(tag),
                    "{:?}",
                    ERR_NO_CONTROL_PROXY_DETECTED.to_string()
                );
                bail!(ERR_NO_CONTROL_PROXY_DETECTED.to_string())
            }
        }
    }

    /// Connects over BR/EDR to an input device ID.
    ///
    /// # Arguments
    /// * 'id' - A String representing the device ID.
    pub async fn connect(&self, id: String) -> Result<(), Error> {
        let tag = "BluetoothControlFacade::connect";
        match &self.inner.read().control_interface_proxy {
            Some(proxy) => {
                let resp = await!(proxy.connect(&id))?;
                match resp.error {
                    Some(err) => {
                        fx_log_err!(tag: &with_line!(tag), "Error: {:?}", err);
                        bail!(BTError::from(*err))
                    }
                    None => Ok(()),
                }
            }
            None => {
                fx_log_err!(
                    tag: &with_line!(tag),
                    "{:?}",
                    ERR_NO_CONTROL_PROXY_DETECTED.to_string()
                );
                bail!(ERR_NO_CONTROL_PROXY_DETECTED.to_string())
            }
        }
    }

    /// Disconnects an active BR/EDR connection by input device ID.
    ///
    /// # Arguments
    /// * 'id' - A String representing the device ID.
    pub async fn disconnect(&self, id: String) -> Result<(), Error> {
        let tag = "BluetoothControlFacade::disconnect";
        match &self.inner.read().control_interface_proxy {
            Some(proxy) => {
                let resp = await!(proxy.disconnect(&id))?;
                match resp.error {
                    Some(err) => {
                        fx_log_err!(tag: &with_line!(tag), "Error: {:?}", err);
                        bail!(BTError::from(*err))
                    }
                    None => Ok(()),
                }
            }
            None => {
                fx_log_err!(
                    tag: &with_line!(tag),
                    "{:?}",
                    ERR_NO_CONTROL_PROXY_DETECTED.to_string()
                );
                bail!(ERR_NO_CONTROL_PROXY_DETECTED.to_string())
            }
        }
    }

    /// Returns the current Active Adapter's Address.
    pub async fn get_active_adapter_address(&self) -> Result<String, Error> {
        let result = match &self.inner.read().control_interface_proxy {
            Some(proxy) => {
                if let Some(adapter) = await!(proxy.get_active_adapter_info())? {
                    adapter.address
                } else {
                    bail!("No Active Adapter")
                }
            }
            None => bail!(ERR_NO_CONTROL_PROXY_DETECTED.to_string()),
        };
        Ok(result)
    }

    /// Cleans up objects in use.
    pub fn cleanup(&self) {
        self.inner.write().control_interface_proxy = None;
        self.inner.write().discovered_device_list.clear();
    }

    /// Prints useful information.
    pub fn print(&self) {
        let tag = "BluetoothControlFacade::print:";
        fx_log_info!(
            tag: &with_line!(tag),
            "control_interface_proxy: {:?}",
            self.inner.read().control_interface_proxy
        );
        fx_log_info!(
            tag: &with_line!(tag),
            "discovered_device_list: {:?}",
            self.inner.read().discovered_device_list
        );
    }
}
