// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::fuzzer::Fuzzer,
    crate::writer::{OutputSink, Writer},
    anyhow::{anyhow, bail, Context as _, Error, Result},
    fidl::endpoints::{create_proxy, ProtocolMarker},
    fidl_fuchsia_developer_remotecontrol as rcs, fidl_fuchsia_fuzzer as fuzz,
    fuchsia_zircon_status as zx,
    selectors::{parse_selector, VerboseError},
    std::path::Path,
    url::Url,
};

/// Represents the FIDL connection from the `ffx fuzz` plugin to the `fuzz-manager` component on a
/// target device.
pub struct Manager<O: OutputSink> {
    proxy: fuzz::ManagerProxy,
    writer: Writer<O>,
}

impl<O: OutputSink> Manager<O> {
    /// Creates a new `Manager`.
    ///
    /// The created object maintains a FIDL `proxy` to the `fuzz-manager` component on a target
    /// device. Any output produced by this object will be written using the given `writer`.
    pub fn new(proxy: fuzz::ManagerProxy, writer: &Writer<O>) -> Self {
        Self { proxy, writer: writer.clone() }
    }

    /// Creates a new `Manager`.
    ///
    /// Uses `fuchsia.developer.remotecontrol` to create and connect the `fuchsia.fuzzer.Manager`
    /// proxy.
    pub async fn with_remote_control(
        rc: &rcs::RemoteControlProxy,
        writer: &Writer<O>,
    ) -> Result<Self> {
        let (proxy, server_end) = fidl::endpoints::create_proxy::<fuzz::ManagerMarker>()
            .context("failed to create proxy for fuchsia.fuzzer.Manager")?;
        let selector = format!("core/fuzz-manager:expose:{}", fuzz::ManagerMarker::DEBUG_NAME);
        let parsed =
            parse_selector::<VerboseError>(&selector).context("failed to parse selector")?;
        let result = rc
            .connect(parsed, server_end.into_channel())
            .await
            .context(format!("{}/Connect", rcs::RemoteControlMarker::DEBUG_NAME))?;
        result.map_err(|e| anyhow!("{:?}", e)).context("failed to connect to fuzz-manager")?;
        Ok(Manager::new(proxy, writer))
    }

    /// Requests that the `fuzz-manager` connect to a fuzzer instance.
    ///
    /// This will create and connect a `fuchsia.fuzzer.Controller` to the fuzzer on the target
    /// device given by the `url`. Any artifacts produced by the fuzzer will be saved to the
    /// `artifact_dir`, and outputs such as logs can optionally be saved to the `output_dir`.
    ///
    /// Returns an object representing the connected fuzzer, or an error.
    pub async fn connect<P: AsRef<Path>>(&self, url: &Url, output_dir: P) -> Result<Fuzzer<O>> {
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
        Fuzzer::try_new(&url, proxy, output_dir, &self.writer)
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
pub mod test_fixtures {
    use {
        crate::fuzzer::test_fixtures::{serve_controller, FakeFuzzer},
        crate::test_fixtures::create_task,
        crate::writer::test_fixtures::BufferSink,
        crate::writer::Writer,
        anyhow::{Context as _, Result},
        fidl::endpoints::ServerEnd,
        fidl_fuchsia_fuzzer as fuzz, fuchsia_zircon_status as zx,
        futures::StreamExt,
    };

    /// Serves `fuchsia.fuzzer.Manager` on the given `server_end` of a FIDL channel.
    pub async fn serve_manager(
        server_end: ServerEnd<fuzz::ManagerMarker>,
        fuzzer: FakeFuzzer,
        writer: Writer<BufferSink>,
    ) -> Result<()> {
        let mut stream = server_end.into_stream()?;
        let mut task = None;
        while let Some(request) = stream.next().await {
            let request = request.context("fuchsia.fuzzer.Manager")?;
            match request {
                fuzz::ManagerRequest::Connect { fuzzer_url, controller, responder } => {
                    let stream = controller.into_stream()?;
                    fuzzer.set_url(fuzzer_url);
                    responder.send(zx::Status::OK.into_raw())?;
                    task =
                        Some(create_task(serve_controller(stream, fuzzer.clone()), writer.clone()));
                }
                fuzz::ManagerRequest::GetOutput { fuzzer_url, output, socket, responder } => {
                    let running = fuzzer.url().unwrap_or(String::default());
                    if fuzzer_url == running {
                        responder.send(fuzzer.set_output(output, socket))?;
                    } else {
                        responder.send(zx::Status::NOT_FOUND.into_raw())?;
                    }
                }
                fuzz::ManagerRequest::Stop { fuzzer_url, responder } => {
                    let running = fuzzer.url().unwrap_or(String::default());
                    fuzzer.set_url(&fuzzer_url);
                    if fuzzer_url == running {
                        task = None;
                        responder.send(zx::Status::OK.into_raw())?;
                    } else {
                        responder.send(zx::Status::NOT_FOUND.into_raw())?;
                    }
                }
            };
        }
        if let Some(task) = task.take() {
            task.await;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::Manager,
        crate::test_fixtures::{Test, TEST_URL},
        anyhow::Result,
        url::Url,
    };

    #[fuchsia::test]
    async fn test_connect() -> Result<()> {
        let mut test = Test::try_new()?;
        let proxy = test.rcs()?;
        let manager = Manager::with_remote_control(&proxy, test.writer()).await?;

        let url = Url::parse(TEST_URL)?;
        manager.connect(&url, test.root_dir()).await?;

        let actual = test.fuzzer().url();
        let expected = Some(url.to_string());
        assert_eq!(actual, expected);
        Ok(())
    }

    #[fuchsia::test]
    async fn test_stop() -> Result<()> {
        let mut test = Test::try_new()?;
        let proxy = test.rcs()?;
        let manager = Manager::with_remote_control(&proxy, test.writer()).await?;

        let url = Url::parse(TEST_URL)?;
        manager.stop(&url).await?;

        let actual = test.fuzzer().url();
        let expected = Some(url.to_string());
        assert_eq!(actual, expected);
        Ok(())
    }
}
