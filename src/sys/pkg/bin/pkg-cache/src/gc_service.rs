// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_space::{
        ErrorCode as SpaceErrorCode, ManagerRequest as SpaceManagerRequest,
        ManagerRequestStream as SpaceManagerRequestStream,
    },
    fuchsia_syslog::{fx_log_err, fx_log_info},
    futures::prelude::*,
};

pub async fn serve(
    pkgfs_ctl: pkgfs::control::Client,
    mut stream: SpaceManagerRequestStream,
) -> Result<(), Error> {
    while let Some(event) = stream.try_next().await? {
        let SpaceManagerRequest::Gc { responder } = event;
        responder.send(&mut gc(&pkgfs_ctl).await)?;
    }
    Ok(())
}

async fn gc(pkgfs_ctl: &pkgfs::control::Client) -> Result<(), SpaceErrorCode> {
    fx_log_info!("triggering pkgfs gc");
    pkgfs_ctl.gc().await.map_err(|err| {
        fx_log_err!("error unlinking /pkgfs/ctl/garbage: {:?}", err);
        SpaceErrorCode::Internal
    })
}
