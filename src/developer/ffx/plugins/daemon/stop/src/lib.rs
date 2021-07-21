// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result, ffx_core::ffx_plugin, ffx_daemon_stop_args::StopCommand,
    fidl_fuchsia_developer_bridge as bridge,
};

#[ffx_plugin()]
async fn stop(daemon_proxy: bridge::DaemonProxy, cmd: StopCommand) -> Result<()> {
    stop_impl(daemon_proxy, cmd, &mut std::io::stdout()).await
}

async fn stop_impl<W: std::io::Write>(
    daemon_proxy: bridge::DaemonProxy,
    _cmd: StopCommand,
    writer: &mut W,
) -> Result<()> {
    daemon_proxy.quit().await?;
    writeln!(writer, "Stopped daemon.")?;
    Ok(())
}

///////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {super::*, anyhow::Context, fidl_fuchsia_developer_bridge::DaemonRequest};

    fn setup_fake_daemon_server() -> bridge::DaemonProxy {
        setup_fake_daemon_proxy(|req| match req {
            DaemonRequest::Quit { responder } => {
                responder.send(true).context("error sending response").expect("should send");
            }
            _ => assert!(false),
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_stop_test() {
        let proxy = setup_fake_daemon_server();
        let mut writer = Vec::new();
        let result = stop_impl(proxy, StopCommand {}, &mut writer).await.unwrap();
        assert_eq!(result, ());
        let output = String::from_utf8(writer).unwrap();
        assert_eq!(output, "Stopped daemon.\n");
    }
}
