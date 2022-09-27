// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Result},
    ffx_core::ffx_plugin,
    ffx_off_args::OffCommand,
    fidl::Error as FidlError,
    fidl_fuchsia_hardware_power_statecontrol::AdminProxy,
};

#[ffx_plugin(
    AdminProxy = "bootstrap/power_manager:expose:fuchsia.hardware.power.statecontrol.Admin"
)]
pub async fn off(admin_proxy: AdminProxy, _cmd: OffCommand) -> Result<()> {
    let res = admin_proxy.poweroff().await;
    match res {
        Ok(Ok(_)) => Ok(()),
        Ok(Err(e)) => bail!(e),
        Err(e) => match e {
            FidlError::ClientChannelClosed { .. } => {
                tracing::info!(
                    "Off returned a client channel closed - assuming power down succeeded: {:?}",
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

    fn setup_fake_admin_server() -> AdminProxy {
        setup_fake_admin_proxy(|req| match req {
            AdminRequest::Poweroff { responder } => {
                responder.send(&mut Ok(())).unwrap();
            }
            _ => assert!(false),
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_off() -> Result<()> {
        let admin_proxy = setup_fake_admin_server();

        let result = off(admin_proxy, OffCommand {}).await;
        assert!(result.is_ok());
        Ok(())
    }
}
