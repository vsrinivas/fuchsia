// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    async_trait::async_trait,
    fidl_fuchsia_virtualization::{GuestManagerProxy, GuestMarker, GuestProxy},
    guest_cli_args::GuestType,
};

#[cfg(target_os = "fuchsia")]
mod fuchsia;
#[cfg(target_os = "fuchsia")]
pub use fuchsia::*;

#[cfg(not(target_os = "fuchsia"))]
mod host;
#[cfg(not(target_os = "fuchsia"))]
pub use host::*;

#[async_trait(?Send)]
pub trait PlatformServices {
    async fn connect_to_manager(&self, guest_type: GuestType) -> Result<GuestManagerProxy>;

    async fn connect_to_guest(&self, guest_type: GuestType) -> Result<GuestProxy> {
        let guest_manager = self.connect_to_manager(guest_type).await?;
        let (guest, guest_server_end) =
            fidl::endpoints::create_proxy::<GuestMarker>().context("Failed to create Guest")?;
        guest_manager.connect(guest_server_end).await?.map_err(|err| anyhow!("{:?}", err))?;
        Ok(guest)
    }
}
