// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::fuzzer,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_fuzzer as fuzz, fidl_fuchsia_test_manager as test_manager,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon as zx,
    futures::{lock::Mutex, TryStreamExt},
    std::collections::HashMap,
};

const DEFAULT_TIMEOUT_IN_SECONDS: i64 = 15;

pub struct Manager {
    fuzzers: Mutex<HashMap<String, Option<fuzzer::Fuzzer>>>,
}

impl Manager {
    pub fn new() -> Self {
        Self { fuzzers: Mutex::new(HashMap::new()) }
    }

    pub async fn serve(&self, stream: fuzz::ManagerRequestStream) -> Result<(), fidl::Error> {
        stream
            .try_for_each(|request| async {
                let registry = self.registry();
                match request {
                    fuzz::ManagerRequest::Connect { fuzzer_url, controller, responder } => {
                        let mut response = self
                            .connect_fuzzer(&fuzzer_url, self.run_builder(), &registry, controller)
                            .await
                            .map_err(|e| e.into_raw());
                        responder.send(&mut response)
                    }
                    fuzz::ManagerRequest::Stop { fuzzer_url, responder } => {
                        let status = self.stop_fuzzer(&fuzzer_url, &registry).await;
                        responder.send(status.into_raw())
                    }
                    fuzz::ManagerRequest::Kill { fuzzer_url, responder } => {
                        let status = self.kill_fuzzer(&fuzzer_url, &registry).await;
                        responder.send(status.into_raw())
                    }
                }?;
                Ok(())
            })
            .await
    }

    // Helper functions to connect to other components. Methods that use these components take
    // explicit arguments that can be replaced when testing.
    fn run_builder(&self) -> test_manager::RunBuilderProxy {
        connect_to_protocol::<test_manager::RunBuilderMarker>()
            .expect("failed to connect to test_manager")
    }

    fn registry(&self) -> fuzz::RegistryProxy {
        connect_to_protocol::<fuzz::RegistryMarker>().expect("failed to connect to fuzz-registry")
    }

    // Installs the provided |controller| into the fuzzer at |fuzzer_url|, starting it first if
    // necessary. If the controller is not connected within 5 seconds, this future
    // completes with a status indicating it timed out.
    // See also |fuchsia.fuzzer.Manager.Connect|.
    async fn connect_fuzzer(
        &self,
        fuzzer_url: &str,
        run_builder: test_manager::RunBuilderProxy,
        registry: &fuzz::RegistryProxy,
        controller: ServerEnd<fuzz::ControllerMarker>,
    ) -> Result<(zx::Socket, zx::Socket, zx::Socket), zx::Status> {
        // Take the fuzzer out of the map. If we encounter an error, it will close the test on drop.
        // If the URL is present but the value is None, the fuzzer is starting and we should wait.
        let fuzzer = {
            let mut fuzzers = self.fuzzers.lock().await;
            fuzzers.entry(fuzzer_url.to_string()).or_insert(Some(fuzzer::Fuzzer::default())).take()
        };
        let mut fuzzer = fuzzer.ok_or(Err(zx::Status::SHOULD_WAIT))?;
        let result =
            self.start_fuzzer(&mut fuzzer, fuzzer_url, run_builder, registry, controller).await;
        {
            let mut fuzzers = self.fuzzers.lock().await;
            match result {
                Ok(_) => {
                    // Connected! Put the fuzzer back into the map.
                    fuzzers.insert(fuzzer_url.to_string(), Some(fuzzer));
                }
                Err(_) => {
                    // Error. Kill and remove fuzzer.
                    fuzzer.kill().await;
                    fuzzers.remove(&fuzzer_url.to_string());
                }
            }
        }
        result.map(|mut v| {
            let syslog = v.pop().unwrap();
            let stderr = v.pop().unwrap();
            let stdout = v.pop().unwrap();
            (stdout, stderr, syslog)
        })
    }

    async fn start_fuzzer(
        &self,
        fuzzer: &mut fuzzer::Fuzzer,
        fuzzer_url: &str,
        run_builder: test_manager::RunBuilderProxy,
        registry: &fuzz::RegistryProxy,
        controller: ServerEnd<fuzz::ControllerMarker>,
    ) -> Result<Vec<zx::Socket>, zx::Status> {
        let timeout = zx::Duration::from_seconds(DEFAULT_TIMEOUT_IN_SECONDS);
        let deadline = zx::Time::after(timeout);
        fuzzer.start(run_builder, fuzzer_url, deadline).await?;
        registry.connect(fuzzer_url, controller, timeout.into_nanos()).await.map_err(|e| {
            fx_log_err!("failed to get provider for {}: {}", fuzzer_url, e);
            zx::Status::INTERNAL
        })?;
        fuzzer.get_artifacts().await
    }

    async fn stop_fuzzer(&self, fuzzer_url: &str, registry: &fuzz::RegistryProxy) -> zx::Status {
        let fuzzer_result = self.remove_fuzzer(&fuzzer_url).await;
        let registry_result = disconnect_fuzzer(fuzzer_url, registry).await;
        match fuzzer_result {
            Ok(mut fuzzer) => {
                fuzzer.stop().await;
                zx::Status::from_result(registry_result)
            }
            Err(status) => status,
        }
    }

    async fn kill_fuzzer(&self, fuzzer_url: &str, registry: &fuzz::RegistryProxy) -> zx::Status {
        let fuzzer_result = match self.remove_fuzzer(&fuzzer_url).await {
            Ok(mut fuzzer) => {
                fuzzer.kill().await;
                Ok(())
            }
            Err(e) => Err(e),
        };
        let registry_result = disconnect_fuzzer(fuzzer_url, registry).await;
        match fuzzer_result {
            Ok(_) => zx::Status::from_result(registry_result),
            Err(status) => status,
        }
    }

    // Stops the fuzzer with the given |fuzzer_url|.
    // See also |fuchsia.fuzzer.Manager.Terminate|.
    async fn remove_fuzzer(&self, fuzzer_url: &str) -> Result<fuzzer::Fuzzer, zx::Status> {
        let removed = {
            let mut fuzzers = self.fuzzers.lock().await;
            fuzzers.remove(&fuzzer_url.to_string())
        };
        if removed.is_none() {
            return Err(zx::Status::NOT_FOUND);
        }
        let fuzzer = removed.unwrap();
        if fuzzer.is_none() {
            return Err(zx::Status::SHOULD_WAIT);
        }
        Ok(fuzzer.unwrap())
    }
}

async fn disconnect_fuzzer(
    fuzzer_url: &str,
    registry: &fuzz::RegistryProxy,
) -> Result<(), zx::Status> {
    let status = match registry.disconnect(&fuzzer_url).await {
        Ok(raw) => zx::Status::from_raw(raw),
        Err(e) => {
            fx_log_err!("failed to disconnect {}: {}", fuzzer_url, e);
            zx::Status::INTERNAL
        }
    };
    zx::Status::ok(status.into_raw())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::manager::Manager as FuzzManager,
        crate::test_support as fake,
        anyhow::Error,
        fidl::endpoints::{create_endpoints, create_proxy, ClientEnd, Proxy},
        fidl_fuchsia_fuzzer as fuzz, fuchsia_async as fasync,
        futures::join,
    };

    // Test fixtures.

    #[derive(Debug, PartialEq)]
    enum TestEvent {
        Start,
        Reconnect,
        Stop,
        Exit,
    }

    #[derive(Debug)]
    struct FuzzerForTest {
        proxy: fuzz::ControllerProxy,
        out: fake::Diagnostics,
        err: fake::Diagnostics,
        log: fake::Diagnostics,
    }

    impl FuzzerForTest {
        fn new(
            client_end: ClientEnd<fuzz::ControllerMarker>,
            out: zx::Socket,
            err: zx::Socket,
            log: zx::Socket,
        ) -> Self {
            Self {
                proxy: client_end.into_proxy().expect("failed to create Controller proxy"),
                out: fake::Diagnostics::new(out),
                err: fake::Diagnostics::new(err),
                log: fake::Diagnostics::new(log),
            }
        }

        async fn out(&mut self) -> String {
            self.out.next().await.unwrap()
        }

        async fn err(&mut self) -> String {
            self.err.next().await.unwrap()
        }

        async fn log(&mut self) -> String {
            self.log.next().await.unwrap()
        }

        fn is_closed(&self) -> bool {
            self.proxy.as_channel().is_closed()
        }
    }

    struct FuzzManagerForTest {
        fuzz_manager: FuzzManager,
        test_manager: fake::TestManager,
        registry: Option<fuzz::RegistryProxy>,
    }

    impl FuzzManagerForTest {
        fn setup() -> Self {
            Self::setup_with_status(zx::Status::OK)
        }

        // Like |setup|, but the registry will return the given status on fuzzer connection.
        fn setup_with_status(status: zx::Status) -> Self {
            let mut test_manager = fake::TestManager::new();
            let registry = test_manager.serve_registry(status);
            Self {
                fuzz_manager: FuzzManager::new(),
                test_manager: test_manager,
                registry: Some(registry),
            }
        }

        async fn make_run_builder(&mut self, fuzzer_url: &str) -> test_manager::RunBuilderProxy {
            self.test_manager.make_run_builder(fuzzer_url, None).await
        }

        async fn default_connect(&mut self, fuzzer_url: &str) -> Result<FuzzerForTest, zx::Status> {
            let run_builder = self.test_manager.make_run_builder(fuzzer_url, None).await;
            self.connect(fuzzer_url, run_builder, TestEvent::Start).await
        }

        async fn connect(
            &self,
            fuzzer_url: &str,
            run_builder: test_manager::RunBuilderProxy,
            event: TestEvent,
        ) -> Result<FuzzerForTest, zx::Status> {
            let registry = self.registry.as_ref().unwrap();
            let (client_end, controller) = create_endpoints::<fuzz::ControllerMarker>().unwrap();
            let connect_fut =
                self.fuzz_manager.connect_fuzzer(fuzzer_url, run_builder, &registry, controller);
            let send_events = async {
                match event {
                    TestEvent::Start => {
                        self.test_manager.start_suite(fuzzer_url).await;
                        self.test_manager.start_case(fuzzer_url).await;
                    }
                    TestEvent::Reconnect => {}
                    TestEvent::Stop => {
                        self.test_manager.start_suite(fuzzer_url).await;
                        self.test_manager.stop(fuzzer_url).await;
                    }
                    TestEvent::Exit => {
                        self.test_manager.start_suite(fuzzer_url).await;
                        self.test_manager.kill(fuzzer_url).await;
                    }
                };
            };
            let result = join!(connect_fut, send_events).0;
            result.map(|x| FuzzerForTest::new(client_end, x.0, x.1, x.2))
        }

        async fn connect_with_error(
            &mut self,
            fuzzer_url: &str,
            err: test_manager::LaunchError,
        ) -> Result<FuzzerForTest, zx::Status> {
            let run_builder = self.test_manager.make_run_builder(fuzzer_url, Some(err)).await;
            let registry = self.registry.as_ref().unwrap();
            let (client_end, controller) = create_endpoints::<fuzz::ControllerMarker>().unwrap();
            let result = self
                .fuzz_manager
                .connect_fuzzer(fuzzer_url, run_builder, &registry, controller)
                .await;
            self.test_manager.reset(fuzzer_url).await;
            result.map(|x| FuzzerForTest::new(client_end, x.0, x.1, x.2))
        }

        async fn append(&self, fuzzer_url: &str, artifact: fake::Artifact) {
            self.test_manager.append(fuzzer_url, artifact).await;
        }

        async fn stop_fuzzer(&self, fuzzer_url: &str) -> zx::Status {
            let registry = self.registry.as_ref().unwrap();
            self.fuzz_manager.stop_fuzzer(fuzzer_url, &registry).await
        }

        async fn kill_fuzzer(&self, fuzzer_url: &str) -> zx::Status {
            let registry = self.registry.as_ref().unwrap();
            self.fuzz_manager.kill_fuzzer(fuzzer_url, &registry).await
        }

        async fn teardown(&mut self) -> anyhow::Result<()> {
            self.test_manager.stop_registry(self.registry.take()).await;
            Ok(())
        }
    }

    // Unit tests.

    #[fasync::run_singlethreaded(test)]
    async fn test_stop() -> Result<(), Error> {
        let mut fuzz_manager = FuzzManagerForTest::setup();
        let fuzzer = fuzz_manager.default_connect("foo").await?;
        assert_eq!(fuzz_manager.stop_fuzzer("foo").await, zx::Status::OK);
        assert!(fuzzer.is_closed());
        fuzz_manager.teardown().await?;
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_kill() -> Result<(), Error> {
        let mut fuzz_manager = FuzzManagerForTest::setup();
        let fuzzer = fuzz_manager.default_connect("foo").await?;
        assert_eq!(fuzz_manager.kill_fuzzer("foo").await, zx::Status::OK);
        assert!(fuzzer.is_closed());
        fuzz_manager.teardown().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_artifacts() -> Result<(), Error> {
        let mut fuzz_manager = FuzzManagerForTest::setup();
        let mut fuzzer = fuzz_manager.default_connect("foo").await?;
        // Verify all diagnostics are bridged.
        fuzz_manager.append("foo", fake::Artifact::Stdout("stdout".to_string())).await;
        fuzz_manager.append("foo", fake::Artifact::Stderr("stderr".to_string())).await;
        fuzz_manager.append("foo", fake::Artifact::SyslogInline("inline".to_string())).await;
        fuzz_manager.append("foo", fake::Artifact::SyslogSocket("socket".to_string())).await;
        assert_eq!(fuzzer.out().await, "stdout");
        assert_eq!(fuzzer.err().await, "stderr");
        assert_eq!(fuzzer.log().await, "inline");
        assert_eq!(fuzzer.log().await, "socket");
        assert_eq!(fuzz_manager.stop_fuzzer("foo").await, zx::Status::OK);
        fuzz_manager.teardown().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_concurrent() -> Result<(), Error> {
        let mut fuzz_manager = FuzzManagerForTest::setup();
        let mut foo = fuzz_manager.default_connect("foo").await?;
        let mut bar = fuzz_manager.default_connect("bar").await?;
        // Verify diagnostics are routed correctly.
        fuzz_manager.append("bar", fake::Artifact::Stdout("barbarbar".to_string())).await;
        fuzz_manager.append("foo", fake::Artifact::Stdout("foofoofoo".to_string())).await;
        assert_eq!(foo.out().await, "foofoofoo");
        assert_eq!(bar.out().await, "barbarbar");
        assert_eq!(fuzz_manager.stop_fuzzer("foo").await, zx::Status::OK);
        assert_eq!(fuzz_manager.stop_fuzzer("bar").await, zx::Status::OK);
        fuzz_manager.teardown().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_reconnect() -> Result<(), Error> {
        let mut fuzz_manager = FuzzManagerForTest::setup();
        {
            let _fuzzer = fuzz_manager.default_connect("foo").await?;
            fuzz_manager.append("foo", fake::Artifact::Stderr("msg1".to_string())).await;
        }
        // The fuzz-manager won't actually use the RunBuilderProxy, since the fuzzer is running.
        // Just pass an unconnected proxy.
        let (run_builder, _) = create_proxy::<test_manager::RunBuilderMarker>()
            .expect("failed to create RunBuilder proxy");
        let mut fuzzer = fuzz_manager.connect("foo", run_builder, TestEvent::Reconnect).await?;
        fuzz_manager.append("foo", fake::Artifact::Stderr("msg2".to_string())).await;
        assert_eq!(fuzzer.err().await, "msg1msg2");
        assert_eq!(fuzz_manager.stop_fuzzer("foo").await, zx::Status::OK);
        fuzz_manager.teardown().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_not_fuzzer() -> Result<(), Error> {
        // Simulate a non-fuzzer test by not registering a controller provider.
        let mut fuzz_manager = FuzzManagerForTest::setup_with_status(zx::Status::TIMED_OUT);
        let run_builder = fuzz_manager.make_run_builder("foo").await;
        let _fuzzer = fuzz_manager.connect("foo", run_builder, TestEvent::Start).await?;
        assert_eq!(fuzz_manager.stop_fuzzer("foo").await, zx::Status::OK);
        fuzz_manager.teardown().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_premature_stop() -> Result<(), Error> {
        let mut fuzz_manager = FuzzManagerForTest::setup();
        let run_builder = fuzz_manager.make_run_builder("foo").await;
        let result = fuzz_manager.connect("foo", run_builder, TestEvent::Stop).await;
        assert_eq!(result.err(), Some(zx::Status::BAD_STATE));
        fuzz_manager.teardown().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_premature_exit() -> Result<(), Error> {
        let mut fuzz_manager = FuzzManagerForTest::setup();
        let run_builder = fuzz_manager.make_run_builder("foo").await;
        let result = fuzz_manager.connect("foo", run_builder, TestEvent::Exit).await;
        assert_eq!(result.err(), Some(zx::Status::BAD_STATE));
        fuzz_manager.teardown().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_resource_unavailable() -> Result<(), Error> {
        let mut fuzz_manager = FuzzManagerForTest::setup();
        let result = fuzz_manager
            .connect_with_error("foo", test_manager::LaunchError::ResourceUnavailable)
            .await;
        assert_eq!(result.err(), Some(zx::Status::NO_RESOURCES));
        fuzz_manager.teardown().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_instance_cannot_resolve() -> Result<(), Error> {
        let mut fuzz_manager = FuzzManagerForTest::setup();
        let result = fuzz_manager
            .connect_with_error("foo", test_manager::LaunchError::InstanceCannotResolve)
            .await;
        assert_eq!(result.err(), Some(zx::Status::NOT_FOUND));
        // Try to stop a fuzzer that isn't running.
        let result = fuzz_manager.stop_fuzzer("foo").await;
        assert_eq!(result, zx::Status::NOT_FOUND);
        // Try to kill a fuzzer that isn't running.
        let result = fuzz_manager.kill_fuzzer("foo").await;
        assert_eq!(result, zx::Status::NOT_FOUND);
        fuzz_manager.teardown().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_invalid_args() -> Result<(), Error> {
        let mut fuzz_manager = FuzzManagerForTest::setup();
        let result =
            fuzz_manager.connect_with_error("foo", test_manager::LaunchError::InvalidArgs).await;
        assert_eq!(result.err(), Some(zx::Status::INVALID_ARGS));
        fuzz_manager.teardown().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_failed_to_connect_to_test_suite() -> Result<(), Error> {
        let mut fuzz_manager = FuzzManagerForTest::setup();
        let result = fuzz_manager
            .connect_with_error("foo", test_manager::LaunchError::FailedToConnectToTestSuite)
            .await;
        assert_eq!(result.err(), Some(zx::Status::NOT_SUPPORTED));
        fuzz_manager.teardown().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_case_enumeration() -> Result<(), Error> {
        let mut fuzz_manager = FuzzManagerForTest::setup();
        let result = fuzz_manager
            .connect_with_error("foo", test_manager::LaunchError::CaseEnumeration)
            .await;
        assert_eq!(result.err(), Some(zx::Status::INTERNAL));
        fuzz_manager.teardown().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_internal_error() -> Result<(), Error> {
        let mut fuzz_manager = FuzzManagerForTest::setup();
        let result =
            fuzz_manager.connect_with_error("foo", test_manager::LaunchError::InternalError).await;
        assert_eq!(result.err(), Some(zx::Status::INTERNAL));
        fuzz_manager.teardown().await
    }
}
