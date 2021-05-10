// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_echo_args::EchoCommand,
    fidl_fuchsia_developer_bridge as bridge,
    std::io::{stdout, Write},
};

#[ffx_plugin()]
pub async fn echo(daemon_proxy: bridge::DaemonProxy, cmd: EchoCommand) -> Result<()> {
    echo_impl(daemon_proxy, cmd, Box::new(stdout())).await
}

async fn echo_impl<W: Write>(
    daemon_proxy: bridge::DaemonProxy,
    cmd: EchoCommand,
    mut writer: W,
) -> Result<()> {
    // XXX(awdavies): This is generated in a macro.
    let (echo_proxy, server) = fidl::endpoints::create_endpoints::<bridge::EchoMarker>()?;
    let echo_proxy = echo_proxy.into_proxy()?;
    match daemon_proxy
        .connect_to_service(
            <bridge::EchoMarker as fidl::endpoints::DiscoverableService>::SERVICE_NAME,
            server.into_channel(),
        )
        .await?
    {
        Ok(_) => (),
        Err(e) => panic!("UNABLE TO CONNECT PROXY: {:#?}", e),
    };

    let echo_text = cmd.text.unwrap_or("Ffx".to_string());
    match echo_proxy.echo_string(&echo_text).await {
        Ok(r) => {
            writeln!(writer, "SUCCESS: received {:?}", r)?;
            Ok(())
        }
        Err(e) => panic!("ERROR: {:?}", e),
    }
}

//TODO(awdavies): Add tests back in when macros are implemented
