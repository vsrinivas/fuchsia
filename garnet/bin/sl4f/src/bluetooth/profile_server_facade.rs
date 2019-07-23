// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fidl_fuchsia_bluetooth_bredr::{ProfileMarker, ProfileProxy};
use fuchsia_component as component;
use fuchsia_syslog::macros::*;
use parking_lot::RwLock;

use crate::server::sl4f::macros::{fx_err_and_bail, with_line};

#[derive(Debug)]
struct InnerProfileServerFacade {
    /// The current Profile Server Proxy
    profile_server_proxy: Option<ProfileProxy>,
}

/// Perform Profile Server operations.
///
/// Note this object is shared among all threads created by the server.
#[derive(Debug)]
pub struct ProfileServerFacade {
    inner: RwLock<InnerProfileServerFacade>,
}

impl ProfileServerFacade {
    pub fn new() -> ProfileServerFacade {
        ProfileServerFacade {
            inner: RwLock::new(InnerProfileServerFacade { profile_server_proxy: None }),
        }
    }

    /// Creates a Profile Server Proxy.
    pub fn create_profile_server_proxy(&self) -> Result<ProfileProxy, Error> {
        let tag = "ProfileServerFacade::create_profile_server_proxy";
        match self.inner.read().profile_server_proxy.clone() {
            Some(profile_server_proxy) => {
                fx_log_info!(
                    tag: &with_line!(tag),
                    "Current profile server proxy: {:?}",
                    profile_server_proxy
                );
                Ok(profile_server_proxy)
            }
            None => {
                fx_log_info!(tag: &with_line!(tag), "Setting new profile server proxy");
                let profile_server_proxy = component::client::connect_to_service::<ProfileMarker>();
                if let Err(err) = profile_server_proxy {
                    fx_err_and_bail!(
                        &with_line!(tag),
                        format_err!("Failed to create profile server proxy: {}", err)
                    );
                }
                profile_server_proxy
            }
        }
    }

    /// Initialize the ProfileServer proxy.
    pub async fn init_profile_server_proxy(&self) -> Result<(), Error> {
        self.inner.write().profile_server_proxy = Some(self.create_profile_server_proxy()?);
        Ok(())
    }

    /// Cleanup any Profile Server related objects.
    pub async fn cleanup(&self) -> Result<(), Error> {
        self.inner.write().profile_server_proxy = None;
        Ok(())
    }
}
