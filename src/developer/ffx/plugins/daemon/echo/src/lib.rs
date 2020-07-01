// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    ffx_core::ffx_plugin,
    ffx_echo_args::EchoCommand,
    fidl_fuchsia_developer_bridge::DaemonProxy,
    std::io::{stdout, Write},
};

#[ffx_plugin()]
pub async fn echo(daemon_proxy: DaemonProxy, cmd: EchoCommand) -> Result<(), Error> {
    echo_impl(daemon_proxy, cmd, Box::new(stdout())).await
}

async fn echo_impl<W: Write>(
    daemon_proxy: DaemonProxy,
    cmd: EchoCommand,
    mut writer: W,
) -> Result<(), Error> {
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
        super::*,
        anyhow::Context,
        fidl_fuchsia_developer_bridge::{DaemonMarker, DaemonRequest},
        futures::TryStreamExt,
        std::io::BufWriter,
    };

    fn setup_fake_daemon_server() -> DaemonProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<DaemonMarker>().unwrap();

        fuchsia_async::spawn(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    DaemonRequest::EchoString { value, responder } => {
                        responder
                            .send(value.as_ref())
                            .context("error sending response")
                            .expect("should send");
                    }
                    _ => assert!(false),
                }
                // We should only get one request per stream. We want subsequent calls to fail
                // if more are made.
                break;
            }
        });

        proxy
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
    async fn test_echo_with_no_text() -> Result<(), Error> {
        let output = run_echo_test(EchoCommand { text: None }).await;
        assert_eq!("SUCCESS: received \"Ffx\"\n".to_string(), output);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_echo_with_text() -> Result<(), Error> {
        let output = run_echo_test(EchoCommand { text: Some("test".to_string()) }).await;
        assert_eq!("SUCCESS: received \"test\"\n".to_string(), output);
        Ok(())
    }
}
