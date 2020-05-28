// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    ffx_core::ffx_plugin,
    ffx_powerctl_args::*,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_developer_remotecontrol::RemoteControlProxy,
    fidl_fuchsia_hardware_power_statecontrol::{
        AdminMarker, RebootReason, SuspendRequest, SystemPowerState,
    },
    selectors::parse_selector,
};

#[ffx_plugin()]
pub async fn powerctl_cmd(
    remote_proxy: RemoteControlProxy,
    cmd: PowerCtlCommand,
) -> Result<(), Error> {
    let state = match cmd.ctl_type {
        PowerCtlSubcommand::Reboot(_) => SystemPowerState::Reboot,
        PowerCtlSubcommand::Bootloader(_) => SystemPowerState::RebootBootloader,
        PowerCtlSubcommand::Recovery(_) => SystemPowerState::RebootRecovery,
        PowerCtlSubcommand::Poweroff(_) => SystemPowerState::Poweroff,
    };

    let (proxy, server_end) = create_proxy::<AdminMarker>()?;
    let selector =
        parse_selector("core/appmgr:out:fuchsia.hardware.power.statecontrol.Admin").unwrap();

    match remote_proxy
        .connect(selector, server_end.into_channel())
        .await
        .context("awaiting connect call")?
    {
        Ok(_) => {
            proxy
                .suspend2(SuspendRequest {
                    reason: Some(RebootReason::UserRequest),
                    state: Some(state),
                })
                .await?
                .unwrap();
            Ok(())
        }
        Err(e) => {
            eprintln!("Failed to connect to device: {:?}", e);
            Ok(())
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {
        super::*,
        fidl::endpoints::RequestStream,
        fidl::handle::AsyncChannel,
        fidl_fuchsia_developer_remotecontrol::{RemoteControlMarker, RemoteControlRequest},
        fidl_fuchsia_hardware_power_statecontrol::{AdminRequest, AdminRequestStream},
        futures::TryStreamExt,
        std::sync::{Arc, Mutex},
    };

    fn setup_fake_admin_service(
        mut stream: AdminRequestStream,
        state_ptr: Arc<Mutex<Option<SystemPowerState>>>,
    ) {
        hoist::spawn(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(AdminRequest::Suspend2 {
                        request:
                            SuspendRequest {
                                state: Some(state),
                                reason: Some(RebootReason::UserRequest),
                            },
                        responder,
                    }) => {
                        *(state_ptr.lock().unwrap()) = Some(state);
                        responder.send(&mut Ok(())).unwrap();
                    }
                    _ => assert!(false),
                }
                // We should only get one request per stream. We want subsequent calls to fail if more are
                // made.
                break;
            }
        });
    }

    fn setup_fake_remote_server() -> (RemoteControlProxy, Arc<Mutex<Option<SystemPowerState>>>) {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<RemoteControlMarker>().unwrap();
        let state_ptr = Arc::new(Mutex::new(None));
        let ptr_clone = state_ptr.clone();

        hoist::spawn(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(RemoteControlRequest::Connect {
                        selector: _,
                        service_chan,
                        responder,
                    }) => {
                        setup_fake_admin_service(
                            AdminRequestStream::from_channel(
                                AsyncChannel::from_channel(service_chan).unwrap(),
                            ),
                            state_ptr.clone(),
                        );
                        responder.send(&mut Ok(())).unwrap();
                    }
                    _ => assert!(false),
                }
            }
        });

        (proxy, ptr_clone)
    }

    async fn run_powerctl_test(state: PowerCtlSubcommand, expected_state: SystemPowerState) {
        let (remote_proxy, state_ptr) = setup_fake_remote_server();

        let result = powerctl_cmd(remote_proxy, PowerCtlCommand { ctl_type: state }).await.unwrap();
        assert_eq!(result, ());
        assert_eq!(*state_ptr.try_lock().unwrap(), Some(expected_state));
    }

    #[test]
    fn test_reboot() -> Result<(), Error> {
        hoist::run(async move {
            run_powerctl_test(
                PowerCtlSubcommand::Reboot(RebootCommand {}),
                SystemPowerState::Reboot,
            )
            .await;
        });
        Ok(())
    }

    #[test]
    fn test_bootloader() -> Result<(), Error> {
        hoist::run(async move {
            run_powerctl_test(
                PowerCtlSubcommand::Bootloader(BootloaderCommand {}),
                SystemPowerState::RebootBootloader,
            )
            .await;
        });
        Ok(())
    }

    #[test]
    fn test_recovery() -> Result<(), Error> {
        hoist::run(async move {
            run_powerctl_test(
                PowerCtlSubcommand::Recovery(RecoveryCommand {}),
                SystemPowerState::RebootRecovery,
            )
            .await;
        });
        Ok(())
    }

    #[test]
    fn test_poweroff() -> Result<(), Error> {
        hoist::run(async move {
            run_powerctl_test(
                PowerCtlSubcommand::Poweroff(PoweroffCommand {}),
                SystemPowerState::Poweroff,
            )
            .await;
        });
        Ok(())
    }
}
