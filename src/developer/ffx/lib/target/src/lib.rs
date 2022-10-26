// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::FfxError,
    fidl::{endpoints::create_proxy, prelude::*},
    fidl_fuchsia_developer_ffx::{
        DaemonError, DaemonProxy, TargetCollectionMarker, TargetMarker, TargetProxy, TargetQuery,
    },
    fidl_fuchsia_developer_remotecontrol::{RemoteControlMarker, RemoteControlProxy},
    futures::{select, Future, FutureExt},
    std::time::Duration,
    timeout::timeout,
};

#[derive(Debug, Clone)]
pub enum TargetKind {
    Normal(String),
    FastbootInline(String),
}

impl ToString for TargetKind {
    fn to_string(&self) -> String {
        match self {
            Self::Normal(target) => target.to_string(),
            Self::FastbootInline(serial) => serial.to_string(),
        }
    }
}

/// Attempt to connect to RemoteControl on a target device using a connection to a daemon.
///
/// The optional |target| is a string matcher as defined in fuchsia.developer.ffx.TargetQuery
/// fidl table.
#[tracing::instrument(level = "info")]
pub async fn get_remote_proxy(
    target: Option<TargetKind>,
    is_default_target: bool,
    daemon_proxy: DaemonProxy,
    proxy_timeout: Duration,
) -> Result<RemoteControlProxy> {
    let (target_proxy, target_proxy_fut) =
        open_target_with_fut(target.clone(), is_default_target, daemon_proxy, proxy_timeout)?;
    let mut target_proxy_fut = target_proxy_fut.boxed_local().fuse();
    let (remote_proxy, remote_server_end) = create_proxy::<RemoteControlMarker>()?;
    let mut open_remote_control_fut =
        target_proxy.open_remote_control(remote_server_end).boxed_local().fuse();
    let res = loop {
        select! {
            res = open_remote_control_fut => {
                match res {
                    Err(e) => {
                        // Getting here is most likely the result of a PEER_CLOSED error, which
                        // may be because the target_proxy closure has propagated faster than
                        // the error (which can happen occasionally). To counter this, wait for
                        // the target proxy to complete, as it will likely only need to be
                        // polled once more (open_remote_control_fut partially depends on it).
                        target_proxy_fut.await?;
                        return Err(e.into());
                    }
                    Ok(r) => break(r),
                }
            }
            res = target_proxy_fut => {
                res?
            }
        }
    };
    let target = target.as_ref().map(ToString::to_string);
    match res {
        Ok(_) => Ok(remote_proxy),
        Err(err) => Err(anyhow::Error::new(FfxError::TargetConnectionError {
            err,
            target,
            is_default_target,
            logs: Some(target_proxy.get_ssh_logs().await?),
        })),
    }
}

/// Attempt to connect to a target given a connection to a daemon.
///
/// The returned future must be polled to completion. It is returned separately
/// from the TargetProxy to enable immediately pushing requests onto the TargetProxy
/// before connecting to the target completes.
///
/// The optional |target| is a string matcher as defined in fuchsia.developer.ffx.TargetQuery
/// fidl table.
#[tracing::instrument(level = "info")]
pub fn open_target_with_fut<'a>(
    target: Option<TargetKind>,
    is_default_target: bool,
    daemon_proxy: DaemonProxy,
    target_timeout: Duration,
) -> Result<(TargetProxy, impl Future<Output = Result<()>> + 'a)> {
    let (tc_proxy, tc_server_end) = create_proxy::<TargetCollectionMarker>()?;
    let (target_proxy, target_server_end) = create_proxy::<TargetMarker>()?;
    let target_kind = target.clone();
    let target = target.as_ref().map(ToString::to_string);
    let t_clone = target.clone();
    let target_collection_fut = async move {
        daemon_proxy
            .connect_to_protocol(
                TargetCollectionMarker::PROTOCOL_NAME,
                tc_server_end.into_channel(),
            )
            .await?
            .map_err(|err| FfxError::DaemonError { err, target: t_clone, is_default_target })?;
        Result::<()>::Ok(())
    };
    let t_clone = target.clone();
    let target_handle_fut = async move {
        if let Some(TargetKind::FastbootInline(serial_number)) = target_kind {
            tracing::trace!("got serial number: {}", serial_number);
            timeout(target_timeout, tc_proxy.add_inline_fastboot_target(&serial_number)).await??;
        }
        timeout(
            target_timeout,
            tc_proxy.open_target(
                TargetQuery { string_matcher: t_clone.clone(), ..TargetQuery::EMPTY },
                target_server_end,
            ),
        )
        .await
        .map_err(|_| FfxError::DaemonError {
            err: DaemonError::Timeout,
            target: t_clone,
            is_default_target,
        })??
        .map_err(|err| FfxError::OpenTargetError { err, target, is_default_target })?;
        Result::<()>::Ok(())
    };
    let fut = async move {
        let ((), ()) = futures::try_join!(target_collection_fut, target_handle_fut)?;
        Ok(())
    };

    Ok((target_proxy, fut))
}
