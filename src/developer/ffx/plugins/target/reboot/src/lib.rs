// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Result},
    chrono::Utc,
    ffx_core::{ffx_bail, ffx_plugin},
    ffx_reboot_args::RebootCommand,
    fidl::endpoints::create_endpoints,
    fidl::Error as FidlError,
    fidl_fuchsia_developer_bridge::{FastbootProxy, RebootListenerMarker, RebootListenerRequest},
    fidl_fuchsia_hardware_power_statecontrol::{AdminProxy, RebootReason},
    futures::{try_join, TryFutureExt, TryStreamExt},
    termion::{color, style},
};

#[ffx_plugin(AdminProxy = "core/appmgr:out:fuchsia.hardware.power.statecontrol.Admin")]
pub async fn reboot(
    admin_proxy: Option<AdminProxy>,
    fastboot_proxy: Option<FastbootProxy>,
    cmd: RebootCommand,
) -> Result<()> {
    if cmd.bootloader && cmd.recovery {
        ffx_bail!("Cannot specify booth bootloader and recovery switches at the same time.");
    }
    match admin_proxy {
        Some(a) => reboot_target(a, cmd).await,
        None => match fastboot_proxy {
            Some(f) => reboot_fastboot(f, cmd).await,
            None => ffx_bail!(
                "Could not connect with target.\n\
                    Make sure a target exists in `ffx target list` and try again. \n\
                    If the problem persists, try `ffx doctor` to correct the issue."
            ),
        },
    }
}

async fn reboot_fastboot(fastboot_proxy: FastbootProxy, cmd: RebootCommand) -> Result<()> {
    if cmd.recovery {
        ffx_bail!("Cannot reboot a fastboot target into recovery.");
    }
    if cmd.bootloader {
        let (reboot_client, reboot_server) = create_endpoints::<RebootListenerMarker>()?;
        let mut stream = reboot_server.into_stream()?;
        let start_time = Utc::now();
        try_join!(
            fastboot_proxy
                .reboot_bootloader(reboot_client)
                .map_err(|e| anyhow!("fidl error when rebooting to bootloader: {:?}", e)),
            async move {
                if let Some(RebootListenerRequest::OnReboot { control_handle: _ }) =
                    stream.try_next().await?
                {
                    Ok(())
                } else {
                    bail!("Did not receive reboot signal");
                }
            }
        )
        .and_then(|(reboot, _)| {
            let d = Utc::now().signed_duration_since(start_time);
            log::debug!("Reboot duration: {:.2}s", (d.num_milliseconds() / 1000));
            println!(
                "{}Done{} [{}{:.2}s{}]",
                color::Fg(color::Green),
                style::Reset,
                color::Fg(color::Blue),
                (d.num_milliseconds() as f32) / (1000 as f32),
                style::Reset
            );
            reboot.map_err(|e| anyhow!("failed booting to bootloader: {:?}", e))
        })
        .map(|_| ())
    } else {
        println!("Rebooting... this could take a while");
        fastboot_proxy.continue_boot().await?.map_err(|_| anyhow!("Could not reboot device"))
    }
}

async fn reboot_target(admin_proxy: AdminProxy, cmd: RebootCommand) -> Result<()> {
    let res = if cmd.bootloader {
        admin_proxy.reboot_to_bootloader().await
    } else if cmd.recovery {
        admin_proxy.reboot_to_recovery().await
    } else {
        admin_proxy.reboot(RebootReason::UserRequest).await
    };
    match res {
        Ok(Ok(_)) => Ok(()),
        Ok(Err(e)) => bail!(e),
        Err(e) => match e {
            FidlError::ClientChannelClosed { .. } => {
                log::warn!(
                    "Reboot returned a client channel closed - assuming reboot succeeded: {:?}",
                    e
                );
                Ok(())
            }
            _ => bail!(e),
        },
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {super::*, fidl_fuchsia_hardware_power_statecontrol::AdminRequest};

    fn setup_fake_admin_server(cmd: RebootCommand) -> AdminProxy {
        setup_fake_admin_proxy(move |req| match req {
            AdminRequest::Reboot { reason: RebootReason::UserRequest, responder } => {
                assert!(!cmd.bootloader && !cmd.recovery);
                responder.send(&mut Ok(())).unwrap();
            }
            AdminRequest::RebootToBootloader { responder } => {
                assert!(cmd.bootloader && !cmd.recovery);
                responder.send(&mut Ok(())).unwrap();
            }
            AdminRequest::RebootToRecovery { responder } => {
                assert!(!cmd.bootloader && cmd.recovery);
                responder.send(&mut Ok(())).unwrap();
            }
            _ => assert!(false),
        })
    }

    async fn run_reboot_test(cmd: RebootCommand) {
        let admin_proxy = setup_fake_admin_server(cmd);

        let result = reboot(Some(admin_proxy), None, cmd).await.unwrap();
        assert_eq!(result, ());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_reboot() -> Result<()> {
        run_reboot_test(RebootCommand { bootloader: false, recovery: false }).await;
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_bootloader() -> Result<()> {
        Ok(run_reboot_test(RebootCommand { bootloader: true, recovery: false }).await)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_recovery() -> Result<()> {
        Ok(run_reboot_test(RebootCommand { bootloader: false, recovery: true }).await)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_error() -> Result<()> {
        let cmd = RebootCommand { bootloader: true, recovery: true };
        let admin_proxy = setup_fake_admin_server(cmd);
        let result = reboot(Some(admin_proxy), None, cmd).await;
        assert!(result.is_err());
        Ok(())
    }
}
