// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use anyhow::Result;
use argh::FromArgs;
use async_trait::async_trait;
use ffx_writer::Writer;
use fho::{FfxMain, FfxTool};
use fidl_fuchsia_developer_ffx as ffx;

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "echo", description = "run echo test against the daemon")]
pub struct EchoCommand {
    #[argh(positional)]
    /// text string to echo back and forth
    pub text: Option<String>,
}

#[derive(FfxTool)]
pub struct EchoTool {
    #[command]
    cmd: EchoCommand,
    echo_proxy: fho::DaemonProtocol<ffx::EchoProxy>,
    writer: Writer,
}

#[async_trait(?Send)]
impl FfxMain for EchoTool {
    async fn main(&mut self) -> Result<()> {
        let text = self.cmd.text.take().unwrap_or("FFX".to_owned());
        let echo_out = self.echo_proxy.echo_string(text.as_str()).await?;
        self.writer.line(echo_out)?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures_lite::stream::StreamExt;

    fn setup_fake_echo_proxy() -> fho::DaemonProtocol<ffx::EchoProxy> {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<ffx::EchoMarker>().unwrap();
        fuchsia_async::Task::local(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    ffx::EchoRequest::EchoString { value, responder } => {
                        responder.send(value.as_ref()).unwrap();
                    }
                }
            }
        })
        .detach();
        fho::DaemonProtocol::new(proxy)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_regular_run() {
        const ECHO: &'static str = "foo";
        let cmd = EchoCommand { text: Some(ECHO.to_owned()) };
        let echo_proxy = setup_fake_echo_proxy();
        let mut tool = EchoTool { cmd, echo_proxy, writer: Writer::new_test(None) };
        tool.main().await.unwrap();
        assert_eq!(format!("{ECHO}\n"), tool.writer.test_output().unwrap());
    }
}
