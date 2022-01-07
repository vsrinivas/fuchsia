// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::Result, async_trait::async_trait, fidl_fuchsia_developer_bridge as bridge,
    protocols::prelude::*,
};

#[ffx_protocol]
#[derive(Default)]
pub struct Echo;

#[async_trait(?Send)]
impl FidlProtocol for Echo {
    type Protocol = bridge::EchoMarker;
    type StreamHandler = FidlInstancedStreamHandler<Self>;

    async fn handle(&self, _cx: &Context, req: bridge::EchoRequest) -> Result<()> {
        match req {
            bridge::EchoRequest::EchoString { value, responder } => {
                responder.send(&mut String::from(value)).map_err(Into::into)
            }
        }
    }

    async fn start(&mut self, _cx: &Context) -> Result<()> {
        log::info!("started echo protocol");
        Ok(())
    }

    async fn stop(&mut self, _cx: &Context) -> Result<()> {
        log::info!("stopped echo protocol");
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
        let proxy = daemon.open_proxy::<bridge::EchoMarker>().await;
        let string = "check-it-out".to_owned();
        assert_eq!(string, proxy.echo_string(string.clone().as_ref()).await.unwrap());
    }
}
