// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::sl4f::macros::dtag;
use failure::{Error};
use fidl_fuchsia_bluetooth_gatt::{Server_Marker, Server_Proxy};
use fuchsia_app as app;
use fuchsia_syslog::{self, fx_log_err, fx_log_info};
use parking_lot::RwLock;
use regex::Regex;
use serde_json::value::Value;

#[derive(Debug)]
struct InnerGattServerFacade {
    // The current Gatt Server Proxy
    server_proxy: Option<Server_Proxy>,
}

/// Perform Gatt Server operations.
///
/// Note this object is shared among all threads created by server.
///
#[derive(Debug)]
pub struct GattServerFacade {
    inner: RwLock<InnerGattServerFacade>,
}

impl GattServerFacade {
    pub fn new() -> GattServerFacade {
        GattServerFacade {
            inner: RwLock::new(InnerGattServerFacade { server_proxy: None }),
        }
    }

    pub fn create_server_proxy(&self) -> Result<Server_Proxy, Error> {
        match self.inner.read().server_proxy.clone() {
            Some(service) => {
                fx_log_info!(tag: &dtag!(), "Current service proxy: {:?}", service);
                Ok(service)
            }
            None => {
                fx_log_info!(tag: &dtag!(), "Setting new server proxy");
                let service = app::client::connect_to_service::<Server_Marker>();
                if let Err(err) = service {
                    fx_log_err!(tag: &dtag!(), "Failed to create server proxy: {:?}", err);
                    bail!("Failed to create server proxy: {:?}", err);
                }
                service
            }
        }
    }

    pub async fn publish_server(&self, args: Value) -> Result<(), Error> {
        fx_log_info!(tag: &dtag!(), "Publishing server");
        let server_proxy = self.create_server_proxy()?;
        self.inner.write().server_proxy = Some(server_proxy);
        // This log line will be removed when Gatt Server database is created.
        // TODO: BT-665
        fx_log_info!(tag: &dtag!(), "Service to Publish: {:?}", args);
        Ok(())
    }

    // GattServerFacade for cleaning up objects in use.
    pub fn cleanup(&self) {
        fx_log_info!(tag: &dtag!(), "Unimplemented cleanup function");
    }

    // GattServerFacade for printing useful information pertaining to the facade for
    // debug purposes.
    pub fn print(&self) {
        fx_log_info!(tag: &dtag!(), "Unimplemented print function");
    }
}
