// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Context as _, Error, Result},
    fidl::endpoints::{create_proxy, ProtocolMarker},
    fidl_fuchsia_fuzzer as fuzz, fuchsia_zircon_status as zx,
    url::Url,
};

/// Represents the FIDL connection from the `ffx fuzz` plugin to the `fuzz-manager` component on a
/// target device.
pub struct Manager {
    proxy: fuzz::ManagerProxy,
}

impl Manager {
    /// Creates a new `Manager`.
    ///
    /// The created object maintains a FIDL `proxy` to the `fuzz-manager` component on a target
    /// device. Any output produced by this object will be written using the given `writer`.
    pub fn new(proxy: fuzz::ManagerProxy) -> Self {
        Self { proxy }
    }

    /// Requests that the `fuzz-manager` connect to a fuzzer instance.
    ///
    /// This will create and connect a `fuchsia.fuzzer.Controller` to the fuzzer on the target
    /// device given by the `url`. Any artifacts produced by the fuzzer will be saved to the
    /// `artifact_dir`, and outputs such as logs can optionally be saved to the `output_dir`.
    ///
    /// Returns an object representing the connected fuzzer, or an error.
    pub async fn connect(&self, url: &Url) -> Result<fuzz::ControllerProxy> {
        let (proxy, server_end) = create_proxy::<fuzz::ControllerMarker>()
            .context("failed to create fuchsia.fuzzer.Controller proxy")?;
        let raw = self
            .proxy
            .connect(url.as_str(), server_end)
            .await
            .context("fuchsia.fuzzer.Manager/Connect")?;
        if raw != zx::Status::OK.into_raw() {
            bail!("fuchsia.fuzzer.Manager/Connect returned ZX_ERR_{}", zx::Status::from_raw(raw));
        }
        Ok(proxy)
    }

    /// Returns a socket that provides the given type of fuzzer output.
    pub async fn get_output(&self, url: &Url, output: fuzz::TestOutput) -> Result<fidl::Socket> {
        let (rx, tx) = fidl::Socket::create(fidl::SocketOpts::STREAM)
            .map_err(Error::msg)
            .context("failed to create socket pair")?;
        self.proxy.get_output(url.as_str(), output, tx).await.context("failed to get output")?;
        Ok(rx)
    }

    /// Requests that the `fuzz-manager` stop a running fuzzer instance.
    ///
    /// As a result of this call, the fuzzer component will cease an ongoing workflow and exit.
    ///
    /// Returns whether a fuzzer was stopped.
    pub async fn stop(&self, url: &Url) -> Result<bool> {
        let raw = self.proxy.stop(url.as_str()).await.context(fidl_name("Stop"))?;
        match zx::Status::from_raw(raw) {
            zx::Status::OK => Ok(true),
            zx::Status::NOT_FOUND => Ok(false),
            status => bail!("failed to stop fuzzer: {}", status),
        }
    }
}

fn fidl_name(method: &str) -> String {
    format!("{}/{}", fuzz::ManagerMarker::DEBUG_NAME, method)
}

#[cfg(test)]
mod tests {
    use {
        super::Manager,
        anyhow::Result,
        fidl::endpoints::create_proxy,
        fidl_fuchsia_fuzzer as fuzz,
        fuchsia_fuzzctl_test::{create_task, serve_manager, Test, TEST_URL},
        url::Url,
    };

    #[fuchsia::test]
    async fn test_connect() -> Result<()> {
        let test = Test::try_new()?;
        let (proxy, server_end) = create_proxy::<fuzz::ManagerMarker>()?;
        let _task = create_task(serve_manager(server_end, test.clone()), test.writer());
        let manager = Manager::new(proxy);

        let url = Url::parse(TEST_URL)?;
        manager.connect(&url).await?;

        let actual = test.url().borrow().as_ref().map(|url| url.to_string());
        let expected = Some(url.to_string());
        assert_eq!(actual, expected);
        Ok(())
    }

    #[fuchsia::test]
    async fn test_stop() -> Result<()> {
        let test = Test::try_new()?;
        let (proxy, server_end) = create_proxy::<fuzz::ManagerMarker>()?;
        let _task = create_task(serve_manager(server_end, test.clone()), test.writer());
        let manager = Manager::new(proxy);

        let url = Url::parse(TEST_URL)?;
        manager.stop(&url).await?;

        let actual = test.url().borrow().as_ref().map(|url| url.to_string());
        let expected = Some(url.to_string());
        assert_eq!(actual, expected);
        Ok(())
    }
}
