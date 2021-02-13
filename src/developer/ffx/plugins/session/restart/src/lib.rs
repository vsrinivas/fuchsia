// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Result},
    ffx_core::ffx_plugin,
    ffx_session_restart_args::SessionRestartCommand,
    fidl_fuchsia_session::RestarterProxy,
};

#[ffx_plugin(RestarterProxy = "core/session-manager:expose:fuchsia.session.Restarter")]
pub async fn restart(restarter_proxy: RestarterProxy, _cmd: SessionRestartCommand) -> Result<()> {
    println!("Restart the current session");
    restarter_proxy.restart().await?.map_err(|err| format_err!("{:?}", err))
}

#[cfg(test)]
mod test {
    use {super::*, fidl_fuchsia_session::RestarterRequest};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_restart_session() {
        let proxy = setup_fake_restarter_proxy(|req| match req {
            RestarterRequest::Restart { responder } => {
                let _ = responder.send(&mut Ok(()));
            }
        });

        let restart_cmd = SessionRestartCommand {};
        let result = restart(proxy, restart_cmd).await;
        assert!(result.is_ok());
    }
}
