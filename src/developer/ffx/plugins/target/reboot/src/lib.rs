// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Result},
    ffx_core::{ffx_bail, ffx_plugin},
    ffx_reboot_args::RebootCommand,
    fidl::Error as FidlError,
    fidl_fuchsia_hardware_power_statecontrol::{AdminProxy, RebootReason},
};

#[ffx_plugin(AdminProxy = "core/appmgr:out:fuchsia.hardware.power.statecontrol.Admin")]
pub async fn reboot(admin_proxy: AdminProxy, cmd: RebootCommand) -> Result<()> {
    if cmd.bootloader && cmd.recovery {
        ffx_bail!("Cannot specify booth bootloader and recovery switches at the same time.");
    }
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

        let result = reboot(admin_proxy, cmd).await.unwrap();
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
        let result = reboot(admin_proxy, cmd).await;
        assert!(result.is_err());
        Ok(())
    }
}
