// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, ffx_core::ffx_plugin, ffx_daemon_stop_args::StopCommand,
    fidl_fuchsia_developer_bridge::DaemonProxy,
};

#[ffx_plugin()]
async fn stop(daemon_proxy: DaemonProxy, _cmd: StopCommand) -> Result<(), Error> {
    daemon_proxy.quit().await?;
    println!("Stopped daemon.");
    Ok(())
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
    };

    fn setup_fake_daemon_server() -> DaemonProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<DaemonMarker>().unwrap();

        fuchsia_async::Task::spawn(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    DaemonRequest::Quit { responder } => {
                        responder
                            .send(true)
                            .context("error sending response")
                            .expect("should send");
                    }
                    _ => assert!(false),
                }
                // We should only get one request per stream. We want subsequent calls to fail
                // if more are made.
                break;
            }
        })
        .detach();

        proxy
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_stop_test() {
        let proxy = setup_fake_daemon_server();
        let result = stop(proxy, StopCommand {}).await.unwrap();
        assert_eq!(result, ());
    }
}
