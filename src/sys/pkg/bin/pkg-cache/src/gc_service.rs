// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error},
    fidl_fuchsia_space::{
        ErrorCode as SpaceErrorCode, ManagerRequest as SpaceManagerRequest,
        ManagerRequestStream as SpaceManagerRequestStream,
    },
    fidl_fuchsia_update::CommitStatusProviderProxy,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    fuchsia_zircon::{self as zx, AsHandleRef},
    futures::prelude::*,
};

pub async fn serve(
    pkgfs_ctl: pkgfs::control::Client,
    commit_status_provider: CommitStatusProviderProxy,
    mut stream: SpaceManagerRequestStream,
) -> Result<(), Error> {
    let event_pair = commit_status_provider
        .is_current_system_committed()
        .await
        .context("while getting event pair")?;

    while let Some(event) = stream.try_next().await? {
        let SpaceManagerRequest::Gc { responder } = event;
        responder.send(&mut gc(&pkgfs_ctl, &event_pair).await)?;
    }
    Ok(())
}

async fn gc(
    pkgfs_ctl: &pkgfs::control::Client,
    event_pair: &zx::EventPair,
) -> Result<(), SpaceErrorCode> {
    fx_log_info!("triggering pkgfs gc");

    event_pair.wait_handle(zx::Signals::USER_0, zx::Time::INFINITE_PAST).map_err(|e| {
        match e {
            zx::Status::TIMED_OUT => {
                fx_log_info!("GC is blocked pending update.");
            }
            zx::Status::CANCELED => {
                fx_log_info!("Commit handle is closed, likely because we are rebooting.");
            }
            other => {
                fx_log_err!("Got unexpected status {:?} while waiting on handle.", other);
            }
        }
        SpaceErrorCode::PendingCommit
    })?;

    pkgfs_ctl.gc().await.map_err(|err| {
        fx_log_err!("error unlinking /pkgfs/ctl/garbage: {:#}", anyhow!(err));
        SpaceErrorCode::Internal
    })
}
