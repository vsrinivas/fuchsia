// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::fuzzer::Fuzzer,
    crate::writer::{OutputSink, Writer},
    anyhow::{anyhow, bail, Context as _, Result},
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
    pub async fn connect<P: AsRef<Path>>(&self, url: Url, output_dir: P) -> Result<Fuzzer<O>> {
        let (proxy, server_end) = create_proxy::<fuzz::ControllerMarker>()
            .context("failed to create fuchsia.fuzzer.Controller proxy")?;
        let result =
            self.proxy.connect(url.as_str(), server_end).await.context(fidl_name("Connect"))?;
        match result {
            Ok((stdout, stderr, syslog)) => {
                Fuzzer::try_new(url, proxy, stdout, stderr, syslog, output_dir, &self.writer)
            }
            Err(e) => bail!("failed to connect to fuzzer: {}", zx::Status::from_raw(e)),
        }
    }

    /// Requests that the `fuzz-manager` stop a running fuzzer instance.
    ///
    /// As a result of this call, the fuzzer component will cease an ongoing workflow and exit.
    pub async fn stop(&self, url: &Url) -> Result<()> {
        let raw = self.proxy.stop(url.as_str()).await.context(fidl_name("Stop"))?;
        match zx::Status::ok(raw) {
            Ok(_) => Ok(()),
            Err(e) => bail!("failed to stop fuzzer: {}", e),
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
        anyhow::{bail, Result},
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
            match request {
                Ok(fuzz::ManagerRequest::Connect { fuzzer_url, controller, responder }) => {
                    let stream = controller.into_stream()?;
                    fuzzer.set_url(fuzzer_url);
                    let (stdout, stderr, syslog) = fuzzer.connect()?;
                    let mut response = Ok((stdout, stderr, syslog));
                    responder.send(&mut response)?;
                    task =
                        Some(create_task(serve_controller(stream, fuzzer.clone()), writer.clone()));
                }
                Ok(fuzz::ManagerRequest::Stop { fuzzer_url, responder }) => {
                    fuzzer.set_url(fuzzer_url);
                    task = None;
                    responder.send(zx::Status::OK.into_raw())?;
                }
                Err(e) => bail!("error serving `Manager`: {:?}", e),
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
        manager.connect(url.clone(), test.root_dir()).await?;

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
