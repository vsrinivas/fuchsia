// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error},
    ffx_core::ffx_plugin,
    ffx_reboot_args::RebootCommand,
    fidl_fuchsia_hardware_power_statecontrol as fpower,
};

#[ffx_plugin(fpower::AdminProxy = "core/appmgr:out:fuchsia.hardware.power.statecontrol.Admin")]
pub async fn reboot(admin_proxy: fpower::AdminProxy, cmd: RebootCommand) -> Result<(), Error> {
    if cmd.bootloader && cmd.recovery {
        println!("Cannot specify booth bootloader and recovery switches at the same time.");
        return Err(anyhow!("Invalid options"));
    }
    if cmd.bootloader {
        admin_proxy.reboot_to_bootloader().await?.unwrap();
    } else if cmd.recovery {
        admin_proxy.reboot_to_recovery().await?.unwrap();
    } else {
        admin_proxy.reboot(fpower::RebootReason::UserRequest).await?.unwrap();
    }
    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {super::*, fidl_fuchsia_hardware_power_statecontrol::AdminRequest, futures::TryStreamExt};
    fn setup_fake_admin_server(cmd: RebootCommand) -> fpower::AdminProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<fpower::AdminMarker>().unwrap();

        fuchsia_async::Task::spawn(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    AdminRequest::Reboot {
                        reason: fpower::RebootReason::UserRequest,
                        responder,
                    } => {
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
                }
                // We should only get one request per stream. We want subsequent calls to fail if more are
                // made.
                break;
            }
        })
        .detach();

        proxy
    }

    async fn run_reboot_test(cmd: RebootCommand) {
        let admin_proxy = setup_fake_admin_server(cmd);

        let result = reboot(admin_proxy, cmd).await.unwrap();
        assert_eq!(result, ());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_reboot() -> Result<(), Error> {
        run_reboot_test(RebootCommand { bootloader: false, recovery: false }).await;
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_bootloader() -> Result<(), Error> {
        Ok(run_reboot_test(RebootCommand { bootloader: true, recovery: false }).await)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_recovery() -> Result<(), Error> {
        Ok(run_reboot_test(RebootCommand { bootloader: false, recovery: true }).await)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_error() -> Result<(), Error> {
        let cmd = RebootCommand { bootloader: true, recovery: true };
        let admin_proxy = setup_fake_admin_server(cmd);
        let result = reboot(admin_proxy, cmd).await;
        assert!(result.is_err());
        Ok(())
    }
}
