// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::FfxError,
    ffx_core::ffx_plugin,
    ffx_wait_args::WaitCommand,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_developer_ffx::{DaemonError, TargetProxy},
    fidl_fuchsia_developer_remotecontrol::RemoteControlMarker,
    std::time::Duration,
    timeout::timeout,
};

#[ffx_plugin()]
pub async fn wait_for_device(target_proxy: TargetProxy, cmd: WaitCommand) -> Result<()> {
    let mut elapsed_seconds = 0;
    let ffx: ffx_lib_args::Ffx = argh::from_env();
    let is_default_target = ffx.target.is_none();

    while elapsed_seconds < cmd.timeout {
        let (_, remote_server_end) = create_proxy::<RemoteControlMarker>()?;
        let result =
            timeout(Duration::from_secs(1), target_proxy.open_remote_control(remote_server_end))
                .await;

        match result {
            Ok(_) => break,
            Err(_) => {
                if elapsed_seconds + 1 < cmd.timeout {
                    elapsed_seconds += 1;
                } else {
                    result.map_err(|_timeout_err| FfxError::DaemonError {
                        err: DaemonError::Timeout,
                        target: ffx.target.clone(),
                        is_default_target,
                    })??;
                }
            }
        };
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_developer_ffx::{TargetMarker, TargetRequest},
        fuchsia_async::futures::TryStreamExt,
    };

    async fn setup_fake_target_server(responsive_rcs: bool) -> TargetProxy {
        let (proxy, mut stream) = create_proxy_and_stream::<TargetMarker>().unwrap();
        fuchsia_async::Task::local(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    TargetRequest::OpenRemoteControl { responder, .. } => {
                        if responsive_rcs {
                            responder.send().expect("failed open rcs")
                        }
                        std::future::pending::<()>().await;
                    }
                    _ => panic!("got unexpected request: {:?}", req),
                }
            }
        })
        .detach();
        proxy
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn able_to_connect_to_device() {
        assert!(wait_for_device(setup_fake_target_server(true).await, WaitCommand { timeout: 1 })
            .await
            .is_ok());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn unable_to_connect_to_device() {
        assert!(wait_for_device(setup_fake_target_server(false).await, WaitCommand { timeout: 1 })
            .await
            .is_err());
    }
}
