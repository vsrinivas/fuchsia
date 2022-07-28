// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::fuzzer::{ConnectResponse, Fuzzer, FuzzerState},
    anyhow::{Context as _, Error, Result},
    fidl::endpoints::{create_proxy, DiscoverableProtocolMarker, ServerEnd},
    fidl_fuchsia_fuzzer as fuzz, fidl_fuchsia_test_manager as test_manager, fuchsia_zircon as zx,
    futures::channel::mpsc,
    futures::StreamExt,
    fuzz::RegistryProxy,
    std::cell::RefCell,
    std::collections::HashMap,
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

pub struct Manager<T: FidlEndpoint<test_manager::RunBuilderMarker>> {
    // Currently active fuzzers.
    fuzzers: RefCell<HashMap<Url, Fuzzer>>,

    // Connection the fuzz-registry.
    registry: RegistryProxy,

    // Produces connections to the test_manager.
    run_builder: T,
}

impl<T: FidlEndpoint<test_manager::RunBuilderMarker>> Manager<T> {
    pub fn new(registry: RegistryProxy, run_builder: T) -> Self {
        Self { fuzzers: RefCell::new(HashMap::new()), registry, run_builder }
    }

    // Serves requests from |receiver|. Note that the `connect` and `stop` methods below return
    // results containg the FIDL responses. These responses may be "normal" errors, e.g.
    // `Ok(Err(INVALID_ARGS))`, in which case they will log a warning before returning. These errors
    // differ from those that indicate a failure on the part of the manager itself, e.g. `Err(...)`.
    pub async fn serve(
        &self,
        mut receiver: mpsc::UnboundedReceiver<fuzz::ManagerRequest>,
    ) -> Result<()> {
        while let Some(request) = receiver.next().await {
            let result = match request {
                fuzz::ManagerRequest::Connect { fuzzer_url, controller, responder } => {
                    let mut response =
                        self.connect(&fuzzer_url, controller).await.context("failed to connect")?;
                    responder.send(&mut response)
                }
                fuzz::ManagerRequest::Stop { fuzzer_url, responder } => {
                    let response = self.stop(&fuzzer_url).await.context("failed to stop")?;
                    responder.send(response)
                }
            };
            result.context("failed response")?;
        }
        Ok(())
    }

    // Requests that given |controller| be connected to the fuzzer given by |fuzzer_url|, starting
    // it if necessary. Returns a result containg the FIDL response.
    async fn connect(
        &self,
        fuzzer_url: &str,
        controller: ServerEnd<fuzz::ControllerMarker>,
    ) -> Result<ConnectResponse> {
        let mut fuzzers = self.fuzzers.borrow_mut();
        let url = match Url::parse(fuzzer_url) {
            Ok(url) => url,
            Err(e) => {
                warn!("failed to connect {}: failed to parse URL: {:?}", fuzzer_url, e);
                return Ok(Err(zx::Status::INVALID_ARGS.into_raw()));
            }
        };
        // Extract or create the fuzzer. If it is stopped or previously failed to start, try
        // starting it.
        let mut fuzzer = fuzzers.remove(&url).unwrap_or_default();
        match fuzzer.get_state() {
            FuzzerState::Stopped | FuzzerState::Failed(_) => {
                let run_builder = self
                    .run_builder
                    .create_proxy()
                    .context("failed to connect to fuchsia.test_manager.RunBuilder")?;
                let (run_proxy, run_controller) =
                    create_proxy::<test_manager::RunControllerMarker>()
                        .context("failed to create fuchsia.test_manager.RunController")?;
                let (suite_proxy, suite_controller) =
                    create_proxy::<test_manager::SuiteControllerMarker>()
                        .context("failed to create fuchsia.test_manager.SuiteController")?;
                run_builder.add_suite(
                    url.as_str(),
                    test_manager::RunOptions::EMPTY,
                    suite_controller,
                )?;
                run_builder.build(run_controller)?;
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
                return Ok(Err(status.into_raw()));
            }
            _ => unreachable!("invalid fuzzer state"),
        };
        // Now connect the controller via the registry.
        let timeout = zx::Duration::from_seconds(DEFAULT_TIMEOUT_IN_SECONDS).into_nanos();
        let status = self
            .registry
            .connect(url.as_str(), controller, timeout)
            .await
            .context("fuchsia.fuzzer.Registry/Connect")?;
        match zx::Status::from_raw(status) {
            zx::Status::OK => {}
            status => {
                warn!("failed to connect {}: fuzz-registry returned: {}", fuzzer_url, status);
                fuzzer.kill().await;
                return Ok(Err(status.into_raw()));
            }
        }
        let response = fuzzer.connect().await.context("failed to connect fuzzer")?;
        if response.is_ok() {
            fuzzers.insert(url, fuzzer);
        }
        Ok(response)
    }

    // Requests that the fuzzer given by |fuzzer_url| stop executing, and waits for it to finish.
    // Returns a result containg the FIDL response.
    async fn stop(&self, fuzzer_url: &str) -> Result<i32, Error> {
        let mut fuzzers = self.fuzzers.borrow_mut();
        let url = match Url::parse(fuzzer_url) {
            Ok(url) => url,
            Err(e) => {
                warn!("failed to stop {}: failed to parse URL: {:?}", fuzzer_url, e);
                return Ok(zx::Status::INVALID_ARGS.into_raw());
            }
        };
        let status = self
            .registry
            .disconnect(url.as_str())
            .await
            .context("fuchsia.fuzzer.Registry/Disconnect")?;
        match zx::Status::from_raw(status) {
            zx::Status::OK => {}
            status => {
                warn!("failed to stop {}: fuzz-registry returned: {}", fuzzer_url, status);
                fuzzers.remove(&url);
                return Ok(status.into_raw());
            }
        };
        if let Some(mut fuzzer) = fuzzers.remove(&url) {
            fuzzer.stop().await;
        }
        Ok(zx::Status::OK.into_raw())
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
            let result = fuzz_manager
                .connect(FOO_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            result.map_err(Error::msg).context("failed to connect to fuzzer")?;
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
            let result = fuzz_manager
                .connect(FOO_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            let (stdout, stderr, syslog) =
                result.map_err(Error::msg).context("failed to connect to fuzzer")?;
            test_realm_clone
                .borrow()
                .write_stdout(FOO_URL, "stdout")
                .await
                .context("failed to write 'stderr' to stderr")?;
            test_realm_clone
                .borrow()
                .write_stderr(FOO_URL, "stderr")
                .await
                .context("failed to write 'stderr' to stderr")?;
            test_realm_clone
                .borrow()
                .write_syslog(FOO_URL, "syslog")
                .await
                .context("failed to write 'syslog' to syslog")?;
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
            let result = fuzz_manager_foo
                .connect(FOO_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            let (_, stderr, _) =
                result.map_err(Error::msg).context("failed to connect to fuzzer")?;
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
            let result = fuzz_manager_bar
                .connect(BAR_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            let (_, stderr, _) =
                result.map_err(Error::msg).context("failed to connect to fuzzer")?;
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
        // join!(foo_fut(), serve_test_realm(test_realm));
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
            let result = fuzz_manager1
                .connect(FOO_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            result.map_err(Error::msg).context("failed to connect to fuzzer")?;
            assert!(!client1.is_closed());
            // Reconnecting should disconnect previous client.
            let (client2, server) = create_proxy::<fuzz::ControllerMarker>()
                .context("failed to create Controller endpoints")?;
            let result = fuzz_manager2
                .connect(FOO_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            result.map_err(Error::msg).context("failed to connect to fuzzer")?;
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
        test_realm.borrow_mut().registry_status = zx::Status::TIMED_OUT;
        let test_fut = || async move {
            let (_, server) = create_endpoints::<fuzz::ControllerMarker>()
                .context("failed to create Controller endpoints")?;
            let result = fuzz_manager
                .connect(FOO_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            assert_eq!(result, Err(zx::Status::TIMED_OUT.into_raw()));
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
            let result = fuzz_manager
                .connect(FOO_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            let (stdout, stderr, syslog) =
                result.map_err(Error::msg).context("failed to connect to fuzzer")?;
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
            let result = fuzz_manager
                .connect(FOO_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            assert_eq!(result, Err(zx::Status::NO_RESOURCES.into_raw()));
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
            let result = fuzz_manager
                .connect(FOO_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            assert_eq!(result, Err(zx::Status::NOT_FOUND.into_raw()));
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
            let result = fuzz_manager
                .connect(FOO_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            assert_eq!(result, Err(zx::Status::INVALID_ARGS.into_raw()));
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
            let result = fuzz_manager
                .connect(FOO_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            assert_eq!(result, Err(zx::Status::NOT_SUPPORTED.into_raw()));
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
            let result = fuzz_manager
                .connect(FOO_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            assert_eq!(result, Err(zx::Status::INTERNAL.into_raw()));
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
            let result = fuzz_manager
                .connect(FOO_URL, server)
                .await
                .context("fuchsia.fuzzer.Manager/Connect")?;
            assert_eq!(result, Err(zx::Status::INTERNAL.into_raw()));
            Ok::<(), Error>(())
        };
        let results = join!(test_fut(), serve_test_realm(test_realm));
        results.0.context("test failed")?;
        results.1.context("failed to serve test realm")?;
        Ok(())
    }
}
