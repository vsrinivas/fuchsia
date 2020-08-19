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
    let echo_text = cmd.text.unwrap_or("Ffx".to_string());
    match daemon_proxy.echo_string(&echo_text).await {
        Ok(r) => {
            writeln!(writer, "SUCCESS: received {:?}", r)?;
            Ok(())
        }
        Err(e) => panic!("ERROR: {:?}", e),
    }
}

///////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {
        super::*, anyhow::Context, fidl_fuchsia_developer_bridge::DaemonRequest, std::io::BufWriter,
    };

    fn setup_fake_daemon_server() -> bridge::DaemonProxy {
        setup_fake_daemon_proxy(|req| match req {
            DaemonRequest::EchoString { value, responder } => {
                responder
                    .send(value.as_ref())
                    .context("error sending response")
                    .expect("should send");
            }
            _ => assert!(false),
        })
    }

    async fn run_echo_test(cmd: EchoCommand) -> String {
        let mut output = String::new();
        let writer = unsafe { BufWriter::new(output.as_mut_vec()) };
        let proxy = setup_fake_daemon_server();
        let result = echo_impl(proxy, cmd, writer).await.unwrap();
        assert_eq!(result, ());
        output
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_echo_with_no_text() -> Result<()> {
        let output = run_echo_test(EchoCommand { text: None }).await;
        assert_eq!("SUCCESS: received \"Ffx\"\n".to_string(), output);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_echo_with_text() -> Result<()> {
        let output = run_echo_test(EchoCommand { text: Some("test".to_string()) }).await;
        assert_eq!("SUCCESS: received \"test\"\n".to_string(), output);
        Ok(())
    }
}
