// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, ffx_core::ffx_plugin, ffx_off_args::OffCommand,
    fidl_fuchsia_hardware_power_statecontrol as fpower,
};

#[ffx_plugin(fpower::AdminProxy = "core/appmgr:out:fuchsia.hardware.power.statecontrol.Admin")]
pub async fn off(admin_proxy: fpower::AdminProxy, _cmd: OffCommand) -> Result<(), Error> {
    admin_proxy.poweroff().await?.unwrap();
    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {super::*, fidl_fuchsia_hardware_power_statecontrol::AdminRequest, futures::TryStreamExt};

    fn setup_fake_admin_server() -> fpower::AdminProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<fpower::AdminMarker>().unwrap();

        fuchsia_async::spawn(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    AdminRequest::Poweroff { responder } => {
                        responder.send(&mut Ok(())).unwrap();
                    }
                    _ => assert!(false),
                }
                // We should only get one request per stream. We want subsequent calls to fail if more are
                // made.
                break;
            }
        });

        proxy
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_off() -> Result<(), Error> {
        let admin_proxy = setup_fake_admin_server();

        let result = off(admin_proxy, OffCommand {}).await.unwrap();
        assert_eq!(result, ());
        Ok(())
    }
}
