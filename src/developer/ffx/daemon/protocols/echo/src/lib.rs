// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result, async_trait::async_trait, fidl_fuchsia_developer_ffx as ffx,
    protocols::prelude::*,
};

#[ffx_protocol]
#[derive(Default)]
pub struct Echo;

#[async_trait(?Send)]
impl FidlProtocol for Echo {
    type Protocol = ffx::EchoMarker;
    type StreamHandler = FidlInstancedStreamHandler<Self>;

    async fn handle(&self, _cx: &Context, req: ffx::EchoRequest) -> Result<()> {
        match req {
            ffx::EchoRequest::EchoString { value, responder } => {
                responder.send(&mut String::from(value)).map_err(Into::into)
            }
        }
    }

    async fn start(&mut self, _cx: &Context) -> Result<()> {
        tracing::info!("started echo protocol");
        Ok(())
    }

    async fn stop(&mut self, _cx: &Context) -> Result<()> {
        tracing::info!("stopped echo protocol");
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use protocols::testing::FakeDaemonBuilder;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_echo() {
        let daemon = FakeDaemonBuilder::new().register_fidl_protocol::<Echo>().build();
        let proxy = daemon.open_proxy::<ffx::EchoMarker>().await;
        let string = "check-it-out".to_owned();
        assert_eq!(string, proxy.echo_string(string.clone().as_ref()).await.unwrap());
    }
}
