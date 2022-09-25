// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::fuzzer::{Fuzzer, FuzzerState},
    anyhow::{Context as _, Error, Result},
    fidl::endpoints::{create_proxy, DiscoverableProtocolMarker, ServerEnd},
    fidl_fuchsia_fuzzer as fuzz, fidl_fuchsia_test_manager as test_manager, fuchsia_zircon as zx,
    futures::channel::mpsc,
    futures::StreamExt,
    fuzz::RegistryProxy,
    std::cell::RefCell,
    std::collections::HashMap,
    test_manager::{
        RunBuilderMarker, RunControllerMarker, RunControllerProxy, RunOptions,
        SuiteControllerMarker, SuiteControllerProxy,
    },
    tracing::warn,
    url::Url,
};

// If this much time elapses from a test suite's start without it connecting to the fuzz-registry,
// the test is assumed to not be a fuzz test.
pub const DEFAULT_TIMEOUT_IN_SECONDS: i64 = 15;

// Helper trait makes connecting to the test_manager configurable. In particular, unit tests provide
// an alternate implementation of this trait that connects to |test_support::FakeTestManager|.
pub trait FidlEndpoint<T: DiscoverableProtocolMarker> {
    fn create_proxy(&self) -> Result<T::Proxy, Error>;
}

pub struct Manager<T: FidlEndpoint<RunBuilderMarker>> {
    // Currently active fuzzers.
    fuzzers: RefCell<HashMap<Url, Fuzzer>>,

    // Connection the fuzz-registry.
    registry: RegistryProxy,

    // Produces connections to the test_manager.
    run_builder: T,
}

impl<T: FidlEndpoint<RunBuilderMarker>> Manager<T> {
    pub fn new(registry: RegistryProxy, run_builder: T) -> Self {
        Self { fuzzers: RefCell::new(HashMap::new()), registry, run_builder }
    }

    /// Serves requests from `receiver`.
    ///
    /// The fuzz-manager is resilient to error encountered when handling individual requests. Errors
    /// may be logged and/or returned to callers via FIDL responses, but the fuzz-manager itself
    /// will try to continue serving requests.
    pub async fn serve(
        &self,
        mut receiver: mpsc::UnboundedReceiver<fuzz::ManagerRequest>,
    ) -> Result<()> {
        while let Some(request) = receiver.next().await {
            match request {
                fuzz::ManagerRequest::Connect { fuzzer_url, controller, responder } => {
                    respond(self.connect(&fuzzer_url, controller).await, |r| responder.send(r))
                }
                fuzz::ManagerRequest::GetOutput { fuzzer_url, output, socket, responder } => {
                    respond(self.get_output(&fuzzer_url, output, socket).await, |r| {
                        responder.send(r)
                    })
                }
                fuzz::ManagerRequest::Stop { fuzzer_url, responder } => {
                    respond(self.stop(&fuzzer_url).await, |r| responder.send(r))
                }
            };
        }
        Ok(())
    }

    fn build(&self, url: &Url) -> Result<(RunControllerProxy, SuiteControllerProxy)> {
        let run_builder = self
            .run_builder
            .create_proxy()
            .context("failed to connect to fuchsia.test_manager.RunBuilder")?;
        let (run_proxy, run_controller) = create_proxy::<RunControllerMarker>()
            .context("failed to create fuchsia.test_manager.RunController")?;
        let (suite_proxy, suite_controller) = create_proxy::<SuiteControllerMarker>()
            .context("failed to create fuchsia.test_manager.SuiteController")?;
        let run_options =
            RunOptions { arguments: Some(vec![fuzz::FUZZ_MODE.to_string()]), ..RunOptions::EMPTY };
        run_builder
            .add_suite(url.as_str(), run_options, suite_controller)
            .map_err(Error::msg)
            .context("fuchsia.test_manager.RunBuilder/AddSuite")?;
        run_builder
            .build(run_controller)
            .map_err(Error::msg)
            .context("fuchsia.test_manager.RunBuilder/Build")?;
        Ok((run_proxy, suite_proxy))
    }

    // Requests that given |controller| be connected to the fuzzer given by |fuzzer_url|, starting
    // it if necessary. Returns a result containg the FIDL response.
    async fn connect(
        &self,
        fuzzer_url: &str,
        controller: ServerEnd<fuzz::ControllerMarker>,
    ) -> Result<(), zx::Status> {
        let url = parse_url(fuzzer_url)?;
        // Extract or create the fuzzer. If it is stopped or previously failed to start, try
        // starting it.
        let mut fuzzer = self.take_fuzzer(&url).unwrap_or_default();
        match fuzzer.get_state() {
            FuzzerState::Stopped | FuzzerState::Failed(_) => {
                let (run_proxy, suite_proxy) = self.build(&url).map_err(warn_internal::<()>)?;
                fuzzer.start(run_proxy, suite_proxy).await;
            }
            _ => {}
        };
        // Check again.
        match fuzzer.get_state() {
            FuzzerState::Running => {}
            FuzzerState::Failed(status) => {
                warn!("failed to connect {}: {}", fuzzer_url, status);
                fuzzer.kill().await;
                return Err(status);
            }
            _ => unreachable!("invalid fuzzer state"),
        };
        // Now connect the controller via the registry.
        let timeout = zx::Duration::from_seconds(DEFAULT_TIMEOUT_IN_SECONDS).into_nanos();
        let raw = self
            .registry
            .connect(url.as_str(), controller, timeout)
            .await
            .context("fuchsia.fuzzer.Registry/Connect")
            .map_err(warn_internal::<i32>)?;
        match zx::Status::from_raw(raw) {
            zx::Status::OK => {}
            status => {
                warn!("failed to connect {}: fuzz-registry returned: {}", fuzzer_url, status);
                fuzzer.kill().await;
                return Err(status);
            }
        }
        self.put_fuzzer(&url, fuzzer)
    }

    // Installs the given socket to receive the given type of fuzzer output.
    async fn get_output(
        &self,
        fuzzer_url: &str,
        output: fuzz::TestOutput,
        socket: zx::Socket,
    ) -> Result<(), zx::Status> {
        let url = parse_url(fuzzer_url)?;
        let mut fuzzer = self.take_fuzzer(&url).ok_or_else(|| {
            warn!("failed to get output {}: fuzzer was not found", fuzzer_url);
            zx::Status::NOT_FOUND
        })?;
        match fuzzer.get_state() {
            FuzzerState::Running => {
                fuzzer.get_output(output, socket).await.map_err(warn_internal::<()>)?;
            }
            _ => {
                warn!("failed to get output {}: fuzzer is not running", fuzzer_url);
                return Err(zx::Status::BAD_STATE);
            }
        }
        self.put_fuzzer(&url, fuzzer)
    }

    // Requests that the fuzzer given by |fuzzer_url| stop executing, and waits for it to finish.
    // Returns a result containing the FIDL response.
    async fn stop(&self, fuzzer_url: &str) -> Result<(), zx::Status> {
        let url = parse_url(fuzzer_url)?;
        let fuzzer = self.take_fuzzer(&url);
        let status = self
            .registry
            .disconnect(url.as_str())
            .await
            .context("fuchsia.fuzzer.Registry/Disconnect")
            .map_err(warn_internal::<zx::Status>)?;
        match zx::Status::from_raw(status) {
            zx::Status::OK => {}
            status => {
                warn!("failed to stop {}: fuzz-registry returned: {}", fuzzer_url, status);
                return Err(status);
            }
        };
        if let Some(mut fuzzer) = fuzzer {
            fuzzer.stop().await;
        }
        Ok(())
    }

    fn take_fuzzer(&self, url: &Url) -> Option<Fuzzer> {
        let mut fuzzers = self.fuzzers.borrow_mut();
        fuzzers.remove(url)
    }

    fn put_fuzzer(&self, url: &Url, fuzzer: Fuzzer) -> Result<(), zx::Status> {
        let mut fuzzers = self.fuzzers.borrow_mut();
        fuzzers.insert(url.clone(), fuzzer);
        Ok(())
    }
}

fn parse_url(fuzzer_url: &str) -> Result<Url, zx::Status> {
    Url::parse(fuzzer_url).map_err(|e| {
        warn!("failed to parse URL: {:?}", e);
        zx::Status::INVALID_ARGS
    })
}

fn warn_internal<T>(e: Error) -> zx::Status {
    warn!("{:?}", e);
    zx::Status::INTERNAL
}

fn respond<R>(result: Result<(), zx::Status>, responder: R)
where
    R: FnOnce(i32) -> Result<(), fidl::Error>,
{
    let response = match result {
        Ok(_) => zx::Status::OK.into_raw(),
        Err(status) => status.into_raw(),
    };
    if let Err(e) = responder(response) {
        warn!("failed to send FIDL response: {:?}", e);
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::test_support::{connect_to_manager, read_async, serve_test_realm, TestRealm},
        fidl::endpoints::{create_endpoints, create_proxy, Proxy},
        fidl_fuchsia_fuzzer as fuzz,
        futures::join,
        std::cell::RefCell,
        std::rc::Rc,
        test_manager::LaunchError,
        zx::AsHandleRef,
    };

    static FOO_URL: &str = "fuchsia-pkg://fuchsia.com/fuzz-manager-unittests#meta/foo.cm";
    static BAR_URL: &str = "fuchsia-pkg://fuchsia.com/fuzz-manager-unittests#meta/bar.cm";

    // Unit tests.

    #[fuchsia::test]
    async fn test_stop() -> Result<()> {
        let test_realm = Rc::new(RefCell::new(TestRealm::new()));
        let fuzz_manager =
            connect_to_manager(Rc::clone(&test_realm)).context("failed to connect to manager")?;
        let test_fut = || async move {
            let (client, server) = create_proxy::<fuzz::ControllerMarker>()
                .context("failed to create Controller endpoints")?;
            let status = fuzz_manager
                .connect(FOO_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            assert_eq!(status, zx::Status::OK.into_raw());

            fuzz_manager.stop(FOO_URL).await.context("failed to stop")?;
            assert!(client.is_closed());
            Ok::<(), Error>(())
        };
        let results = join!(test_fut(), serve_test_realm(test_realm));
        results.0.context("test failed")?;
        results.1.context("failed to serve test realm")?;
        Ok(())
    }

    #[fuchsia::test]
    async fn test_artifacts() -> Result<()> {
        let test_realm = Rc::new(RefCell::new(TestRealm::new()));
        let test_realm_clone = Rc::clone(&test_realm);
        let fuzz_manager =
            connect_to_manager(Rc::clone(&test_realm)).context("failed to connect to manager")?;
        let test_fut = || async move {
            let (_, server) = create_endpoints::<fuzz::ControllerMarker>()
                .context("failed to create Controller endpoints")?;
            let status = fuzz_manager
                .connect(FOO_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            assert_eq!(status, zx::Status::OK.into_raw());

            test_realm_clone
                .borrow()
                .write_stdout(FOO_URL, "stdout")
                .await
                .context("failed to write 'stderr' to stderr")?;
            let (stdout, socket) = zx::Socket::create(zx::SocketOpts::empty())?;
            let status = fuzz_manager.get_output(FOO_URL, fuzz::TestOutput::Stdout, socket).await?;
            assert_eq!(status, zx::Status::OK.into_raw());

            test_realm_clone
                .borrow()
                .write_stderr(FOO_URL, "stderr")
                .await
                .context("failed to write 'stderr' to stderr")?;
            let (stderr, socket) = zx::Socket::create(zx::SocketOpts::empty())?;
            let status = fuzz_manager.get_output(FOO_URL, fuzz::TestOutput::Stderr, socket).await?;
            assert_eq!(status, zx::Status::OK.into_raw());

            test_realm_clone
                .borrow()
                .write_syslog(FOO_URL, "syslog")
                .await
                .context("failed to write 'syslog' to syslog")?;
            let (syslog, socket) = zx::Socket::create(zx::SocketOpts::empty())?;
            let status = fuzz_manager.get_output(FOO_URL, fuzz::TestOutput::Syslog, socket).await?;
            assert_eq!(status, zx::Status::OK.into_raw());

            let msg = read_async(&stdout).await.context("failed to read stdout")?;
            assert_eq!(msg, "stdout");
            let msg = read_async(&stderr).await.context("failed to read stderr")?;
            assert_eq!(msg, "stderr");
            let msg = read_async(&syslog).await.context("failed to read syslog")?;
            assert_eq!(msg, "syslog");
            fuzz_manager.stop(FOO_URL).await.context("failed to stop")?;
            Ok::<(), Error>(())
        };
        let results = join!(test_fut(), serve_test_realm(test_realm));
        results.0.context("test failed")?;
        results.1.context("failed to serve test realm")?;
        Ok(())
    }

    #[fuchsia::test]
    async fn test_concurrent() -> Result<()> {
        let test_realm = Rc::new(RefCell::new(TestRealm::new()));
        let test_realm_foo = Rc::clone(&test_realm);
        let fuzz_manager_foo =
            connect_to_manager(Rc::clone(&test_realm)).context("failed to connect to manager")?;
        let foo_fut = || async move {
            let (_, server) = create_endpoints::<fuzz::ControllerMarker>()
                .context("failed to create Controller endpoints")?;
            let status = fuzz_manager_foo
                .connect(FOO_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            assert_eq!(status, zx::Status::OK.into_raw());

            let (stderr, socket) = zx::Socket::create(zx::SocketOpts::empty())?;
            let status =
                fuzz_manager_foo.get_output(FOO_URL, fuzz::TestOutput::Stderr, socket).await?;
            assert_eq!(status, zx::Status::OK.into_raw());
            test_realm_foo
                .borrow()
                .write_stderr(FOO_URL, "foofoofoo")
                .await
                .context("failed to write to stderr")?;

            let msg = read_async(&stderr).await.context("failed to read stderr")?;
            assert_eq!(msg, "foofoofoo");
            fuzz_manager_foo.stop(FOO_URL).await.context("failed to stop")?;
            Ok::<(), Error>(())
        };
        let test_realm_bar = Rc::clone(&test_realm);
        let fuzz_manager_bar =
            connect_to_manager(Rc::clone(&test_realm)).context("failed to connect to manager")?;
        let bar_fut = || async move {
            let (_, server) = create_endpoints::<fuzz::ControllerMarker>()
                .context("failed to create Controller endpoints")?;
            let status = fuzz_manager_bar
                .connect(BAR_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            assert_eq!(status, zx::Status::OK.into_raw());

            let (stderr, socket) = zx::Socket::create(zx::SocketOpts::empty())?;
            let status =
                fuzz_manager_bar.get_output(BAR_URL, fuzz::TestOutput::Stderr, socket).await?;
            assert_eq!(status, zx::Status::OK.into_raw());
            test_realm_bar
                .borrow()
                .write_stderr(BAR_URL, "barbarbar")
                .await
                .context("failed to write to stderr")?;

            let msg = read_async(&stderr).await.context("failed to read stderr")?;
            assert_eq!(msg, "barbarbar");
            fuzz_manager_bar.stop(BAR_URL).await.context("failed to stop")?;
            Ok::<(), Error>(())
        };
        let results = join!(foo_fut(), bar_fut(), serve_test_realm(test_realm));
        results.0.context("'foo' test failed")?;
        results.1.context("'bar' test failed")?;
        results.2.context("failed to serve test realm")?;
        Ok(())
    }

    #[fuchsia::test]
    async fn test_reconnect() -> Result<()> {
        let test_realm = Rc::new(RefCell::new(TestRealm::new()));
        let fuzz_manager1 =
            connect_to_manager(Rc::clone(&test_realm)).context("failed to connect to manager")?;
        let fuzz_manager2 =
            connect_to_manager(Rc::clone(&test_realm)).context("failed to connect to manager")?;
        let test_fut = || async move {
            let (client1, server) = create_proxy::<fuzz::ControllerMarker>()
                .context("failed to create Controller endpoints")?;
            let status = fuzz_manager1
                .connect(FOO_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            assert_eq!(status, zx::Status::OK.into_raw());
            assert!(!client1.is_closed());

            // Reconnecting should disconnect previous client.
            let (client2, server) = create_proxy::<fuzz::ControllerMarker>()
                .context("failed to create Controller endpoints")?;
            let status = fuzz_manager2
                .connect(FOO_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            assert_eq!(status, zx::Status::OK.into_raw());

            assert!(client1.is_closed());
            assert!(!client2.is_closed());
            fuzz_manager2.stop(FOO_URL).await.context("failed to stop")?;
            Ok::<(), Error>(())
        };
        let results = join!(test_fut(), serve_test_realm(test_realm));
        results.0.context("test failed")?;
        results.1.context("failed to serve test realm")?;
        Ok(())
    }

    #[fuchsia::test]
    async fn test_not_fuzzer() -> Result<()> {
        let test_realm = Rc::new(RefCell::new(TestRealm::new()));
        let fuzz_manager =
            connect_to_manager(Rc::clone(&test_realm)).context("failed to connect to manager")?;
        // Simulate a non-fuzzer test by not registering a controller provider.
        {
            let mut test_realm_mut = test_realm.borrow_mut();
            test_realm_mut.registry_status = zx::Status::TIMED_OUT;
            test_realm_mut.killable = true;
        }
        let test_fut = || async move {
            let (_, server) = create_endpoints::<fuzz::ControllerMarker>()
                .context("failed to create Controller endpoints")?;
            let status = fuzz_manager
                .connect(FOO_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            assert_eq!(status, zx::Status::TIMED_OUT.into_raw());
            Ok::<(), Error>(())
        };
        let results = join!(test_fut(), serve_test_realm(test_realm));
        results.0.context("test failed")?;
        results.1.context("failed to serve test realm")?;
        Ok(())
    }

    #[fuchsia::test]
    async fn test_premature_stop() -> Result<()> {
        let test_realm = Rc::new(RefCell::new(TestRealm::new()));
        let test_realm_clone = Rc::clone(&test_realm);
        let fuzz_manager =
            connect_to_manager(Rc::clone(&test_realm)).context("failed to connect to manager")?;
        let test_fut = || async move {
            let (_, server) = create_proxy::<fuzz::ControllerMarker>()
                .context("failed to create Controller endpoints")?;
            let status = fuzz_manager
                .connect(FOO_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            assert_eq!(status, zx::Status::OK.into_raw());

            let (stdout, socket) = zx::Socket::create(zx::SocketOpts::empty())?;
            let status = fuzz_manager.get_output(FOO_URL, fuzz::TestOutput::Stdout, socket).await?;
            assert_eq!(status, zx::Status::OK.into_raw());

            let (stderr, socket) = zx::Socket::create(zx::SocketOpts::empty())?;
            let status = fuzz_manager.get_output(FOO_URL, fuzz::TestOutput::Stderr, socket).await?;
            assert_eq!(status, zx::Status::OK.into_raw());

            let (syslog, socket) = zx::Socket::create(zx::SocketOpts::empty())?;
            let status = fuzz_manager.get_output(FOO_URL, fuzz::TestOutput::Syslog, socket).await?;
            assert_eq!(status, zx::Status::OK.into_raw());

            // Simulate an unexpected suite stop event.
            test_realm_clone
                .borrow()
                .send_suite_stopped(FOO_URL)
                .context("failed to stop suite")?;
            test_realm_clone.borrow().send_run_stopped(FOO_URL).context("failed to run suite")?;
            let msg = read_async(&stdout).await.context("failed to read stdout")?;
            assert_eq!(msg, "");
            assert!(stdout
                .wait_handle(zx::Signals::SOCKET_PEER_CLOSED, zx::Time::INFINITE_PAST)
                .is_ok());
            let msg = read_async(&stderr).await.context("failed to read stderr")?;
            assert_eq!(msg, "");
            assert!(stderr
                .wait_handle(zx::Signals::SOCKET_PEER_CLOSED, zx::Time::INFINITE_PAST)
                .is_ok());
            let msg = read_async(&syslog).await.context("failed to read syslog")?;
            assert_eq!(msg, "");
            assert!(syslog
                .wait_handle(zx::Signals::SOCKET_PEER_CLOSED, zx::Time::INFINITE_PAST)
                .is_ok());
            Ok::<(), Error>(())
        };
        let results = join!(test_fut(), serve_test_realm(test_realm));
        results.0.context("test failed")?;
        results.1.context("failed to serve test realm")?;
        Ok(())
    }

    #[fuchsia::test]
    async fn test_resource_unavailable() -> Result<()> {
        let test_realm = Rc::new(RefCell::new(TestRealm::new()));
        let fuzz_manager =
            connect_to_manager(Rc::clone(&test_realm)).context("failed to connect to manager")?;
        test_realm.borrow_mut().launch_error = Some(LaunchError::ResourceUnavailable);
        let test_fut = || async move {
            let (_, server) = create_endpoints::<fuzz::ControllerMarker>()
                .context("failed to create Controller endpoints")?;
            let status = fuzz_manager
                .connect(FOO_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            assert_eq!(status, zx::Status::NO_RESOURCES.into_raw());
            Ok::<(), Error>(())
        };
        let results = join!(test_fut(), serve_test_realm(test_realm));
        results.0.context("test failed")?;
        results.1.context("failed to serve test realm")?;
        Ok(())
    }

    #[fuchsia::test]
    async fn test_instance_cannot_resolve() -> Result<()> {
        let test_realm = Rc::new(RefCell::new(TestRealm::new()));
        let fuzz_manager =
            connect_to_manager(Rc::clone(&test_realm)).context("failed to connect to manager")?;
        test_realm.borrow_mut().launch_error = Some(LaunchError::InstanceCannotResolve);
        let test_fut = || async move {
            let (_, server) = create_endpoints::<fuzz::ControllerMarker>()
                .context("failed to create Controller endpoints")?;
            let status = fuzz_manager
                .connect(FOO_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            assert_eq!(status, zx::Status::NOT_FOUND.into_raw());
            assert_eq!(
                fuzz_manager.stop(FOO_URL).await.ok(),
                Some(zx::Status::NOT_FOUND.into_raw())
            );
            Ok::<(), Error>(())
        };
        let results = join!(test_fut(), serve_test_realm(test_realm));
        results.0.context("test failed")?;
        results.1.context("failed to serve test realm")?;
        Ok(())
    }

    #[fuchsia::test]
    async fn test_invalid_args() -> Result<()> {
        let test_realm = Rc::new(RefCell::new(TestRealm::new()));
        let fuzz_manager =
            connect_to_manager(Rc::clone(&test_realm)).context("failed to connect to manager")?;
        test_realm.borrow_mut().launch_error = Some(LaunchError::InvalidArgs);
        let test_fut = || async move {
            let (_, server) = create_endpoints::<fuzz::ControllerMarker>()
                .context("failed to create Controller endpoints")?;
            let status = fuzz_manager
                .connect(FOO_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            assert_eq!(status, zx::Status::INVALID_ARGS.into_raw());
            Ok::<(), Error>(())
        };
        let results = join!(test_fut(), serve_test_realm(test_realm));
        results.0.context("test failed")?;
        results.1.context("failed to serve test realm")?;
        Ok(())
    }

    #[fuchsia::test]
    async fn test_failed_to_connect_to_test_suite() -> Result<()> {
        let test_realm = Rc::new(RefCell::new(TestRealm::new()));
        let fuzz_manager =
            connect_to_manager(Rc::clone(&test_realm)).context("failed to connect to manager")?;
        test_realm.borrow_mut().launch_error = Some(LaunchError::FailedToConnectToTestSuite);
        let test_fut = || async move {
            let (_, server) = create_endpoints::<fuzz::ControllerMarker>()
                .context("failed to create Controller endpoints")?;
            let status = fuzz_manager
                .connect(FOO_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            assert_eq!(status, zx::Status::NOT_SUPPORTED.into_raw());
            Ok::<(), Error>(())
        };
        let results = join!(test_fut(), serve_test_realm(test_realm));
        results.0.context("test failed")?;
        results.1.context("failed to serve test realm")?;
        Ok(())
    }

    #[fuchsia::test]
    async fn test_case_enumeration() -> Result<()> {
        let test_realm = Rc::new(RefCell::new(TestRealm::new()));
        let fuzz_manager =
            connect_to_manager(Rc::clone(&test_realm)).context("failed to connect to manager")?;
        test_realm.borrow_mut().launch_error = Some(LaunchError::CaseEnumeration);
        let test_fut = || async move {
            let (_, server) = create_endpoints::<fuzz::ControllerMarker>()
                .context("failed to create Controller endpoints")?;
            let status = fuzz_manager
                .connect(FOO_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            assert_eq!(status, zx::Status::INTERNAL.into_raw());
            Ok::<(), Error>(())
        };
        let results = join!(test_fut(), serve_test_realm(test_realm));
        results.0.context("test failed")?;
        results.1.context("failed to serve test realm")?;
        Ok(())
    }

    #[fuchsia::test]
    async fn test_internal_error() -> Result<()> {
        let test_realm = Rc::new(RefCell::new(TestRealm::new()));
        let fuzz_manager =
            connect_to_manager(Rc::clone(&test_realm)).context("failed to connect to manager")?;
        test_realm.borrow_mut().launch_error = Some(LaunchError::InternalError);
        let test_fut = || async move {
            let (_, server) = create_endpoints::<fuzz::ControllerMarker>()
                .context("failed to create Controller endpoints")?;
            let status = fuzz_manager
                .connect(FOO_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            assert_eq!(status, zx::Status::INTERNAL.into_raw());
            Ok::<(), Error>(())
        };
        let results = join!(test_fut(), serve_test_realm(test_realm));
        results.0.context("test failed")?;
        results.1.context("failed to serve test realm")?;
        Ok(())
    }
}
