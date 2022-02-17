// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Result},
    errors::ffx_bail,
    ffx_core::ffx_plugin,
    ffx_reboot_args::RebootCommand,
    fidl_fuchsia_developer_bridge::{TargetProxy, TargetRebootError, TargetRebootState},
};

const NETSVC_NOT_FOUND: &str = "The Fuchsia target's netsvc address could not be determined.\n\
                                If this problem persists, try running `ffx doctor` for diagnostics";
const NETSVC_COMM_ERR: &str = "There was a communication error using netsvc to reboot.\n\
                               If the problem persists, try running `ffx doctor` for further diagnostics";
const BOOT_TO_ZED: &str = "Cannot reboot from Bootloader state to Recovery state.";
const REBOOT_TO_PRODUCT: &str = "\nReboot to Product state with `ffx target reboot` and try again.";
const COMM_ERR: &str = "There was a communication error with the device. Please try again. \n\
                        If the problem persists, try running `ffx doctor` for further diagnostics";

#[ffx_plugin()]
pub async fn reboot(target_proxy: TargetProxy, cmd: RebootCommand) -> Result<()> {
    let state = reboot_state(&cmd)?;
    let res = target_proxy.reboot(state).await;
    match res {
        Ok(Ok(_)) => Ok(()),
        Ok(Err(TargetRebootError::NetsvcCommunication)) => {
            ffx_bail!("{}", NETSVC_COMM_ERR)
        }
        Ok(Err(TargetRebootError::NetsvcAddressNotFound)) => {
            ffx_bail!("{}", NETSVC_NOT_FOUND)
        }
        Ok(Err(TargetRebootError::FastbootToRecovery)) => {
            ffx_bail!("{}{}", BOOT_TO_ZED, REBOOT_TO_PRODUCT)
        }
        Ok(Err(TargetRebootError::TargetCommunication))
        | Ok(Err(TargetRebootError::FastbootCommunication)) => ffx_bail!("{}", COMM_ERR),
        Err(e) => bail!(e),
    }
}

fn reboot_state(cmd: &RebootCommand) -> Result<TargetRebootState> {
    match (cmd.bootloader, cmd.recovery) {
        (true, true) => {
            ffx_bail!("Cannot specify booth bootloader and recovery switches at the same time.")
        }
        (true, false) => Ok(TargetRebootState::Bootloader),
        (false, true) => Ok(TargetRebootState::Recovery),
        (false, false) => Ok(TargetRebootState::Product),
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests
#[cfg(test)]
mod test {
    use {super::*, fidl_fuchsia_developer_bridge::TargetRequest};

    fn setup_fake_target_server(cmd: RebootCommand) -> TargetProxy {
        setup_fake_target_proxy(move |req| match req {
            TargetRequest::Reboot { state: _, responder } => {
                assert!(!(cmd.bootloader && cmd.recovery));
                responder.send(&mut Ok(())).unwrap();
            }
            r => panic!("unexpected request: {:?}", r),
        })
    }

    async fn run_reboot_test(cmd: RebootCommand) -> Result<()> {
        let target_proxy = setup_fake_target_server(cmd);
        reboot(target_proxy, cmd).await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_reboot() -> Result<()> {
        run_reboot_test(RebootCommand { bootloader: false, recovery: false }).await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_bootloader() -> Result<()> {
        run_reboot_test(RebootCommand { bootloader: true, recovery: false }).await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_recovery() -> Result<()> {
        run_reboot_test(RebootCommand { bootloader: false, recovery: true }).await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_error() {
        assert!(run_reboot_test(RebootCommand { bootloader: true, recovery: true }).await.is_err())
    }
}
