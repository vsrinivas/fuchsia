// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Result},
    errors::{ffx_bail, FfxError},
    ffx_core::ffx_plugin,
    ffx_reboot_args::RebootCommand,
    fidl_fuchsia_developer_bridge::{
        TargetCollectionProxy, TargetHandleMarker, TargetRebootError, TargetRebootState,
    },
};

const NETSVC_NOT_FOUND: &str = "The Fuchsia target's netsvc address could not be determined.\n\
                                If this problem persists, try running `ffx doctor` for diagnostics";
const NETSVC_COMM_ERR: &str = "There was a communication error using netsvc to reboot.\n\
                               If the problem persists, try running `ffx doctor` for further diagnostics";
const BOOT_TO_ZED: &str = "Cannot reboot from Bootloader state to Recovery state.";
const REBOOT_TO_PRODUCT: &str = "\nReboot to Product state with `ffx target reboot` and try again.";
const COMM_ERR: &str = "There was a communication error with the device. Please try again. \n\
                        If the problem persists, try running `ffx doctor` for further diagnostics";

#[ffx_plugin(TargetCollectionProxy = "daemon::service")]
pub async fn reboot(target_collection: TargetCollectionProxy, cmd: RebootCommand) -> Result<()> {
    let default_target: Option<String> = ffx_config::get("target.default").await?;
    let state = reboot_state(&cmd)?;
    let ffx: ffx_lib_args::Ffx = argh::from_env();
    let is_default_target = ffx.target.is_none();
    let (target_handle, server) = fidl::endpoints::create_proxy::<TargetHandleMarker>()?;
    target_collection.open_target(default_target.as_deref(), server).await?.map_err(|err| {
        FfxError::OpenTargetError { err, target: default_target, is_default_target }
    })?;
    let res = target_handle.reboot(state).await;
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
    use {
        super::*,
        fidl_fuchsia_developer_bridge::{TargetCollectionRequest, TargetHandleRequest},
        fuchsia_async::futures::TryStreamExt,
    };

    fn setup_fake_target_server(cmd: RebootCommand) -> TargetCollectionProxy {
        setup_fake_target_collection(move |req| match req {
            TargetCollectionRequest::OpenTarget { query: _, target_handle, responder } => {
                let mut stream = target_handle.into_stream().unwrap();
                fuchsia_async::Task::local(async move {
                    while let Ok(Some(req)) = stream.try_next().await {
                        match req {
                            TargetHandleRequest::Reboot { state: _, responder } => {
                                assert!(!(cmd.bootloader && cmd.recovery));
                                responder.send(&mut Ok(())).unwrap();
                            }
                            r => panic!("unexpected request: {:?}", r),
                        }
                    }
                })
                .detach();
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
