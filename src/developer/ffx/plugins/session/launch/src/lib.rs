// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Result},
    ffx_core::ffx_plugin,
    ffx_session_launch_args::SessionLaunchCommand,
    fidl_fuchsia_session::{LaunchConfiguration, LauncherProxy},
};

#[ffx_plugin(LauncherProxy = "core/session-manager:expose:fuchsia.session.Launcher")]
pub async fn launch(launcher_proxy: LauncherProxy, cmd: SessionLaunchCommand) -> Result<()> {
    launch_impl(launcher_proxy, cmd, &mut std::io::stdout()).await
}

pub async fn launch_impl<W: std::io::Write>(
    launcher_proxy: LauncherProxy,
    cmd: SessionLaunchCommand,
    writer: &mut W,
) -> Result<()> {
    writeln!(writer, "Launching session: {}", cmd.url)?;
    let config = LaunchConfiguration { session_url: Some(cmd.url), ..LaunchConfiguration::EMPTY };
    launcher_proxy.launch(config).await?.map_err(|err| format_err!("{:?}", err))
}

#[cfg(test)]
mod test {
    use {super::*, fidl_fuchsia_session::LauncherRequest};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_launch_session() {
        const SESSION_URL: &str = "Session URL";

        let proxy = setup_fake_launcher_proxy(|req| match req {
            LauncherRequest::Launch { configuration, responder } => {
                assert!(configuration.session_url.is_some());
                let session_url = configuration.session_url.unwrap();
                assert!(session_url == SESSION_URL.to_string());
                let _ = responder.send(&mut Ok(()));
            }
        });

        let launch_cmd = SessionLaunchCommand { url: SESSION_URL.to_string() };
        let result = launch(proxy, launch_cmd).await;
        assert!(result.is_ok());
    }
}
