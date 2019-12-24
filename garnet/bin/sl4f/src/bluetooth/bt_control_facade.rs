// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl::endpoints::RequestStream;
use fidl_fuchsia_bluetooth_control::{
    ControlMarker, ControlProxy, PairingDelegateMarker, PairingDelegateRequest,
    PairingDelegateRequestStream, PairingMethod,
};
use fuchsia_async::{self as fasync, DurationExt, TimeoutExt};
use fuchsia_component as component;
use fuchsia_syslog::macros::*;
use fuchsia_zircon::{self as zx, DurationNum};

use parking_lot::RwLock;
use std::collections::HashMap;

use crate::bluetooth::types::CustomRemoteDevice;
use crate::common_utils::common::macros::with_line;
use crate::common_utils::error::Sl4fError;

use futures::channel::mpsc;

use futures::stream::StreamExt;

static ERR_NO_CONTROL_PROXY_DETECTED: &'static str = "No Bluetooth Control Proxy detected.";

#[derive(Debug)]
struct InnerBluetoothControlFacade {
    /// The current Bluetooth Control Interface Proxy
    control_interface_proxy: Option<ControlProxy>,

    /// The MPSC Sender object for sending the pin to the pairing delegate.
    client_pin_sender: Option<mpsc::Sender<String>>,

    /// The MPSC Receiver object for sending the pin out from the pairing delegate.
    client_pin_receiver: Option<mpsc::Receiver<String>>,

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
                client_pin_sender: None,
                client_pin_receiver: None,
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
                    return Err(format_err!("Failed to create control interface proxy: {:?}", err));
                }
                control_interface_proxy
            }
        }
    }

    pub async fn monitor_pairing_delegate_request_stream(
        mut stream: PairingDelegateRequestStream,
        mut pin_receiver: mpsc::Receiver<String>,
        mut pin_sender: mpsc::Sender<String>,
    ) -> Result<(), Error> {
        let tag = "BluetoothControlFacade::monitor_pairing_delegate_request_stream";
        while let Some(request) = stream.next().await {
            match request {
                Ok(r) => match r {
                    PairingDelegateRequest::OnPairingComplete {
                        device_id,
                        status,
                        control_handle: _,
                    } => {
                        fx_log_info!(
                            tag: &with_line!(tag),
                            "Pairing complete for peer (id: {}, status: {})",
                            device_id,
                            match status.error {
                                None => format!("{:?}", "success"),
                                Some(error) => format!("{:?}", error),
                            }
                        );
                    }
                    PairingDelegateRequest::OnPairingRequest {
                        device,
                        method,
                        displayed_passkey,
                        responder,
                    } => {
                        if let Some(key) = displayed_passkey {
                            let _res = pin_sender.try_send(key);
                        }
                        fx_log_info!(
                            tag: &with_line!(tag),
                            "Pairing request from peer: {}",
                            match &device.name {
                                Some(name) => format!("{} ({})", name, &device.address),
                                None => device.address,
                            }
                        );
                        let consent = true;
                        let default_passkey = "000000".to_string();
                        let (confirm, entered_passkey) = match method {
                            PairingMethod::Consent => (consent, None),
                            PairingMethod::PasskeyComparison => (consent, None),
                            PairingMethod::PasskeyDisplay => (consent, None),
                            PairingMethod::PasskeyEntry => {
                                let timeout = 10.seconds();
                                let pin = match pin_receiver
                                    .next()
                                    .on_timeout(timeout.after_now(), || None)
                                    .await
                                {
                                    Some(p) => p,
                                    _ => {
                                        fx_log_err!(
                                            tag: &with_line!(tag),
                                            "No pairing pin found from remote host."
                                        );
                                        default_passkey
                                    }
                                };

                                (consent, Some(pin))
                            }
                        };
                        let _ =
                            responder.send(confirm, entered_passkey.as_ref().map(String::as_ref));
                    }
                    PairingDelegateRequest::OnRemoteKeypress {
                        device_id,
                        keypress,
                        control_handle: _,
                    } => {
                        fx_log_info!(
                            tag: &with_line!(tag),
                            "Unhandled OnRemoteKeypress for Device: {} | {:?}",
                            device_id,
                            keypress
                        );
                    }
                },
                Err(r) => return Err(format_err!("Error during handling request stream: {:?}", r)),
            };
        }
        Ok(())
    }

    pub async fn accept_pairing(&self) -> Result<(), Error> {
        let tag = "BluetoothControlFacade::accept_pairing";
        fx_log_info!(tag: &with_line!(tag), "Accepting pairing");
        let (delegate_local, delegate_remote) = zx::Channel::create()?;
        let delegate_local = fasync::Channel::from_channel(delegate_local)?;
        let delegate_ptr =
            fidl::endpoints::ClientEnd::<PairingDelegateMarker>::new(delegate_remote);
        let pairing_delegate_result = match &self.inner.read().control_interface_proxy {
            Some(p) => p.set_pairing_delegate(Some(delegate_ptr)),
            None => {
                let err_str = "No Bluetooth Control Interface Proxy Set.";
                fx_log_err!(tag: &with_line!(tag), "{:?}", err_str);
                return Err(format_err!("{:?}", err_str));
            }
        };
        let delegate_request_stream = PairingDelegateRequestStream::from_channel(delegate_local);

        let (sender, pin_receiver) = mpsc::channel(10);
        let (pin_sender, receiever) = mpsc::channel(10);
        let pairing_delegate_fut = BluetoothControlFacade::monitor_pairing_delegate_request_stream(
            delegate_request_stream,
            pin_receiver,
            pin_sender,
        );

        self.inner.write().client_pin_sender = Some(sender);
        self.inner.write().client_pin_receiver = Some(receiever);

        let monitor_pairing_delegate_future = async {
            let result = pairing_delegate_result.await;
            if let Err(err) = result {
                fx_log_err!(
                    tag: &with_line!("BluetoothControlFacade::accept_pairing"),
                    "Failed to take ownership of Bluetooth Pairing: {:?}",
                    err
                );
            }
        };
        fasync::spawn(monitor_pairing_delegate_future);

        let fut = async {
            let result = pairing_delegate_fut.await;
            if let Err(err) = result {
                fx_log_err!(
                    tag: &with_line!("BluetoothControlFacade::accept_pairing"),
                    "Failed to create or monitor the pairing service delegate: {:?}",
                    err
                );
            }
        };
        fasync::spawn(fut);

        Ok(())
    }

    /// Sets a control proxy to use if one is not already in use.
    pub async fn init_control_interface_proxy(&self) -> Result<(), Error> {
        self.inner.write().control_interface_proxy = Some(self.create_control_interface_proxy()?);
        Ok(())
    }

    pub async fn input_pairing_pin(&self, pin: String) -> Result<(), Error> {
        let tag = "BluetoothControlFacade::input_pairing_pin";
        match self.inner.read().client_pin_sender.clone() {
            Some(mut sender) => sender.try_send(pin)?,
            None => {
                let err_str = "No sender setup for pairing delegate.".to_string();
                fx_log_err!(tag: &with_line!(tag), "{}", err_str);
                bail!(err_str)
            }
        };
        Ok(())
    }

    pub async fn get_pairing_pin(&self) -> Result<String, Error> {
        let tag = "BluetoothControlFacade::get_pairing_pin";
        let pin = match &mut self.inner.write().client_pin_receiver {
            Some(receiever) => match receiever.try_next() {
                Ok(value) => match value {
                    Some(v) => v,
                    None => return Err(format_err!("Error getting pin from pairing delegate.")),
                },
                Err(_e) => {
                    let err_str = "No pairing pin sent from the pairing delegate.".to_string();
                    fx_log_err!(tag: &with_line!(tag), "{}", err_str);
                    return Err(format_err!("No pairing pin sent from the pairing delegate."));
                }
            },
            None => {
                let err_str = "No receiever setup for pairing delegate.".to_string();
                fx_log_err!(tag: &with_line!(tag), "{}", err_str);
                bail!(err_str)
            }
        };
        Ok(pin)
    }

    /// Sets the current control proxy to be discoverable.
    ///
    /// # Arguments
    /// * 'discoverable' - A bool object for setting Bluetooth device discoverable or not.
    pub async fn set_discoverable(&self, discoverable: bool) -> Result<(), Error> {
        let tag = "BluetoothControlFacade::set_discoverable";
        match &self.inner.read().control_interface_proxy {
            Some(proxy) => {
                let resp = proxy.set_discoverable(discoverable).await?;
                match resp.error {
                    Some(err) => {
                        fx_log_err!(tag: &with_line!(tag), "Error: {:?}", err);
                        bail!(Sl4fError::from(*err))
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
                let resp = proxy.set_name(Some(&name)).await?;
                match resp.error {
                    Some(err) => {
                        fx_log_err!(tag: &with_line!(tag), "Error: {:?}", err);
                        bail!(Sl4fError::from(*err))
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
                let resp = proxy.request_discovery(discovery).await?;
                match resp.error {
                    Some(err) => {
                        fx_log_err!(tag: &with_line!(tag), "Error: {:?}", err);
                        bail!(Sl4fError::from(*err))
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
            Some(proxy) => proxy.get_known_remote_devices().await?,
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
                let resp = proxy.forget(&id).await?;
                match resp.error {
                    Some(err) => {
                        fx_log_err!(tag: &with_line!(tag), "Error: {:?}", err);
                        bail!(Sl4fError::from(*err))
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
                let resp = proxy.connect(&id).await?;
                match resp.error {
                    Some(err) => {
                        fx_log_err!(tag: &with_line!(tag), "Error: {:?}", err);
                        bail!(Sl4fError::from(*err))
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
                let resp = proxy.disconnect(&id).await?;
                match resp.error {
                    Some(err) => {
                        fx_log_err!(tag: &with_line!(tag), "Error: {:?}", err);
                        bail!(Sl4fError::from(*err))
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
                if let Some(adapter) = proxy.get_active_adapter_info().await? {
                    adapter.address
                } else {
                    return Err(format_err!("No Active Adapter"));
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
