// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{diagnostics, events},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_test_manager as test_manager, fuchsia_async as fasync,
    fuchsia_async::TimeoutExt,
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon as zx,
    futures::channel::oneshot,
};

#[derive(Default)]
pub struct Fuzzer {
    run_controller: Option<test_manager::RunControllerProxy>,
    bridged: Vec<diagnostics::Bridge>,
    suite_event_handler: Option<fasync::Task<()>>,
}

impl Drop for Fuzzer {
    fn drop(&mut self) {
        if self.run_controller.is_some() {
            panic!("fuzzer dropped without being stopped or killed");
        }
    }
}

impl Fuzzer {
    /// Uses the run_builder to start the fuzzer if it is not currently running; otherwise does
    /// nothing. The URL is passed through test_manager and is opaque to this code.
    pub async fn start(
        &mut self,
        run_builder: test_manager::RunBuilderProxy,
        fuzzer_url: &str,
        deadline: zx::Time,
    ) -> Result<(), zx::Status> {
        // Return immediately if already started; e.g. upon reconnect.
        if self.run_controller.is_some() {
            return Ok(());
        }
        // Add the test suite.
        let (suite_proxy, suite_controller) = create_proxy::<test_manager::SuiteControllerMarker>()
            .expect("failed to create suite_controller");
        let options = test_manager::RunOptions {
            log_iterator: Some(test_manager::LogsIteratorOption::ArchiveIterator),
            ..test_manager::RunOptions::EMPTY
        };
        run_builder.add_suite(fuzzer_url, options, suite_controller).map_err(|e| {
            fx_log_err!("failed to add test suite: {}", e);
            zx::Status::INTERNAL
        })?;
        // Build and execute the run.
        let (run_proxy, run_controller) = create_proxy::<test_manager::RunControllerMarker>()
            .expect("failed to create run_controller");
        run_builder.build(run_controller).map_err(|e| {
            fx_log_err!("failed to build test: {}", e);
            zx::Status::INTERNAL
        })?;
        self.run_controller = Some(run_proxy);

        // Bridge the diagnostics.
        self.bridged.clear();

        let (stdout_sender, stdout_bridge) = diagnostics::Bridge::create_socket_forwarder();
        self.bridged.push(stdout_bridge);

        let (stderr_sender, stderr_bridge) = diagnostics::Bridge::create_socket_forwarder();
        self.bridged.push(stderr_bridge);

        let (syslog_sender, syslog_bridge) = diagnostics::Bridge::create_archive_forwarder();
        self.bridged.push(syslog_bridge);

        // Wait for the test suite's single case to start.
        let (sender, receiver) = oneshot::channel::<zx::Status>();
        self.suite_event_handler = Some(fasync::Task::spawn(async move {
            // Errors will be communicated via the oneshot channel.
            let mut handler =
                events::SuiteEventHandler::new(stdout_sender, stderr_sender, syslog_sender);
            let _ = handler.handle(suite_proxy, sender).await;
        }));
        let result = receiver
            .on_timeout(deadline, || Err(oneshot::Canceled))
            .await
            .unwrap_or(zx::Status::CANCELED);
        let result = zx::Status::ok(result.into_raw());
        if result.is_err() {
            self.kill().await;
        }
        result
    }

    /// Returns sockets connected to the fuzzer's stdout, stderr, and syslog.
    pub async fn get_artifacts(&self) -> Result<Vec<zx::Socket>, zx::Status> {
        if self.bridged.is_empty() {
            return Err(zx::Status::BAD_STATE);
        }
        let mut artifacts = Vec::new();
        for bridge in &self.bridged {
            let artifact = bridge.subscribe()?;
            artifacts.push(artifact);
        }
        Ok(artifacts)
    }

    /// Waits for the fuzzer to stop following a call to fuchsia.fuzzer.Registry.Disconnect.
    /// This method is idempotent; subsequent calls have no effect.
    pub async fn stop(&mut self) {
        // The registry should invoke fuchsia.fuzzer.ControllerProvider.Stop, which will cause the
        // fuzzer to (eventually) stop.
        self.join().await;
    }

    /// Asks the test_manager to immediately stop the fuzzer. This method is idempotent; subsequent
    /// calls have no effect.
    pub async fn kill(&mut self) {
        if let Some(run_controller) = self.run_controller.take() {
            match run_controller.kill() {
                Ok(_) | Err(fidl::Error::ClientChannelClosed { .. }) => {}
                Err(e) => fx_log_err!("failed to kill run_controller: {}", e),
            };
        }
        self.join().await;
    }

    async fn join(&mut self) {
        let suite_event_handler = self.suite_event_handler.take();
        if let Some(suite_event_handler) = suite_event_handler {
            suite_event_handler.await;
        }

        let run_controller = self.run_controller.take();
        if let Some(run_controller) = run_controller {
            loop {
                let events = match run_controller.get_events().await {
                    Err(fidl::Error::ClientChannelClosed { .. }) => Vec::new(),
                    Err(e) => {
                        fx_log_err!("{:?}", e);
                        Vec::new()
                    }
                    Ok(events) => events,
                };
                if events.is_empty() {
                    break;
                }
            }
        }
        while let Some(bridge) = self.bridged.pop() {
            bridge.join().await;
        }
    }
}
