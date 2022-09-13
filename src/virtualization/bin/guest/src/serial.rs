// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::services, anyhow::Error, fidl_fuchsia_virtualization::GuestProxy};

pub async fn handle_serial(guest: GuestProxy) -> Result<(), Error> {
    let serial = guest.get_serial().await?;
    let io = services::GuestConsole::new(serial)?;
    io.run_with_stdio().await.map_err(From::from)
}
