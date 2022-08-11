// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::diagnostics::Forwarder,
    crate::fuzzer::Fuzzer,
    crate::writer::{OutputSink, Writer},
    anyhow::{bail, Context as _, Result},
    fidl::endpoints::{create_proxy, ProtocolMarker},
    fidl_fuchsia_fuzzer as fuzz, fuchsia_zircon_status as zx,
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

    /// Requests that the `fuzz-manager` connect to a fuzzer instance.
    ///
    /// This will create and connect a `fuchsia.fuzzer.Controller` to the fuzzer on the target
    /// device given by the `url`. Any artifacts produced by the fuzzer will be saved to the
    /// `artifact_dir`, and outputs such as logs can optionally be saved to the `output_dir`.
    ///
    /// Returns an object representing the connected fuzzer, or an error.
    pub async fn connect<P: AsRef<Path>, Q: AsRef<Path>>(
        &self,
        url: Url,
        artifact_dir: P,
        output_dir: Option<Q>,
    ) -> Result<Fuzzer<O>> {
        let (proxy, server_end) = create_proxy::<fuzz::ControllerMarker>()
            .context("failed to create fuchsia.fuzzer.Controller proxy")?;
        let result =
            self.proxy.connect(url.as_str(), server_end).await.context(fidl_name("Connect"))?;
        match result {
            Ok((stdout, stderr, syslog)) => {
                let forwarder =
                    Forwarder::try_new(stdout, stderr, syslog, output_dir, &self.writer)?;
                Ok(Fuzzer::new(proxy, url, artifact_dir, forwarder))
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
        super::Manager,
        crate::fuzzer::test_fixtures::{serve_controller, FakeFuzzer},
        crate::util::test_fixtures::{create_task, Test},
        crate::writer::test_fixtures::BufferSink,
        crate::writer::Writer,
        anyhow::{anyhow, Result},
        fidl::endpoints::{create_proxy, ServerEnd},
        fidl_fuchsia_fuzzer as fuzz, fuchsia_async as fasync, fuchsia_zircon_status as zx,
        futures::StreamExt,
        std::cell::RefCell,
        std::rc::Rc,
    };

    /// Set up the test fixtures for unit testing `Manager`.
    pub fn perform_test_setup(test: &Test) -> Result<(FakeManager, Manager<BufferSink>)> {
        let (proxy, server_end) = create_proxy::<fuzz::ManagerMarker>()?;
        let manager = Manager::new(proxy, test.writer());
        let fake = FakeManager::new(server_end, test);
        Ok((fake, manager))
    }

    /// Test fake used to serve the `fuchsia.fuzzer.Manager` protocol to tests.
    pub struct FakeManager {
        fuzzer: FakeFuzzer,
        url: Rc<RefCell<Option<String>>>,
        _task: fasync::Task<()>,
    }

    impl FakeManager {
        /// Creates a new `fuzz-manager` test fake.
        ///
        /// Serves `fuchsia.fuzzer.Manager` on the given `server_end` of a FIDL channel, and writes
        /// any output produced to the given `writer`.
        pub fn new(server_end: ServerEnd<fuzz::ManagerMarker>, test: &Test) -> Self {
            let fuzzer = FakeFuzzer::new();
            let url = Rc::new(RefCell::new(None));
            let writer = test.writer();
            let task = serve_manager(server_end, fuzzer.clone(), Rc::clone(&url), writer.clone());
            Self { fuzzer, url, _task: create_task(task, writer.clone()) }
        }

        /// Clones the fake fuzzer "connected" by the fake manager.
        pub fn clone_fuzzer(&self) -> FakeFuzzer {
            self.fuzzer.clone()
        }

        /// Returns the URL captured from serving `Manager` requests.
        pub fn take_url(&self) -> Option<String> {
            self.url.borrow_mut().take()
        }
    }

    async fn serve_manager(
        server_end: ServerEnd<fuzz::ManagerMarker>,
        fake: FakeFuzzer,
        url: Rc<RefCell<Option<String>>>,
        writer: Writer<BufferSink>,
    ) -> Result<()> {
        let mut stream = server_end.into_stream()?;
        let mut _task = None;
        loop {
            let fuzzer_url = match stream.next().await {
                Some(Ok(fuzz::ManagerRequest::Connect { fuzzer_url, controller, responder })) => {
                    let stream = controller.into_stream()?;
                    let (stdout, stderr, syslog) = fake.connect()?;
                    let task = serve_controller(stream, fake.clone());
                    _task = Some(create_task(task, writer.clone()));
                    let mut response = Ok((stdout, stderr, syslog));
                    responder.send(&mut response)?;
                    Ok(fuzzer_url)
                }
                Some(Ok(fuzz::ManagerRequest::Stop { fuzzer_url, responder })) => {
                    _task = None;
                    responder.send(zx::Status::OK.into_raw())?;
                    Ok(fuzzer_url)
                }
                Some(Err(e)) => Err(anyhow!(e)),
                None => Err(anyhow!("fuchsia.fuzzer.Manager client closed prematurely")),
            }?;
            let mut url_mut = url.borrow_mut();
            *url_mut = Some(fuzzer_url);
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::test_fixtures::perform_test_setup,
        crate::util::test_fixtures::{Test, TEST_URL},
        anyhow::Result,
        url::Url,
    };

    #[fuchsia::test]
    async fn test_connect() -> Result<()> {
        let test = Test::try_new()?;
        let artifact_dir = test.create_dir("artifacts")?;
        let (fake, manager) = perform_test_setup(&test)?;
        let url = Url::parse(TEST_URL)?;
        manager.connect(url.clone(), &artifact_dir, None::<&str>).await?;
        assert_eq!(fake.take_url(), Some(url.to_string()));
        Ok(())
    }

    #[fuchsia::test]
    async fn test_stop() -> Result<()> {
        let test = Test::try_new()?;
        let (fake, manager) = perform_test_setup(&test)?;
        let url = Url::parse(TEST_URL)?;
        manager.stop(&url).await?;
        assert_eq!(fake.take_url(), Some(url.to_string()));
        Ok(())
    }
}
