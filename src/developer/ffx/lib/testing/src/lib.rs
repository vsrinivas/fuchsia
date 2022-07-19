// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow,
    fidl::endpoints::ProtocolMarker,
    fidl_fuchsia_developer_remotecontrol as fremotecontrol,
    fidl_fuchsia_test_manager as ftest_manager,
    ftest_manager::{RunBuilderMarker, RunBuilderProxy},
    selectors::{self, VerboseError},
    std::io::Write,
    std::path::PathBuf,
};

pub fn create_reporter<W: 'static + Write + Send + Sync>(
    filter_ansi: bool,
    dir: Option<PathBuf>,
    writer: W,
) -> Result<run_test_suite_lib::output::RunReporter, anyhow::Error> {
    let stdout_reporter = run_test_suite_lib::output::ShellReporter::new(writer);
    let dir_reporter =
        dir.map(run_test_suite_lib::output::DirectoryWithStdoutReporter::new).transpose()?;
    let reporter = match (dir_reporter, filter_ansi) {
        (Some(dir_reporter), false) => run_test_suite_lib::output::RunReporter::new(
            run_test_suite_lib::output::MultiplexedReporter::new(stdout_reporter, dir_reporter),
        ),
        (Some(dir_reporter), true) => run_test_suite_lib::output::RunReporter::new_ansi_filtered(
            run_test_suite_lib::output::MultiplexedReporter::new(stdout_reporter, dir_reporter),
        ),
        (None, false) => run_test_suite_lib::output::RunReporter::new(stdout_reporter),
        (None, true) => run_test_suite_lib::output::RunReporter::new_ansi_filtered(stdout_reporter),
    };
    Ok(reporter)
}

const RUN_BUILDER_SELECTOR: &str = "core/test_manager:expose:fuchsia.test.manager.RunBuilder";

pub struct RunBuilderConnector {
    remote_control: fremotecontrol::RemoteControlProxy,
}

impl RunBuilderConnector {
    pub async fn connect(&self) -> RunBuilderProxy {
        let (proxy, server_end) = fidl::endpoints::create_proxy::<RunBuilderMarker>()
            .expect(&format!("failed to create proxy to {}", RunBuilderMarker::DEBUG_NAME));
        self.remote_control
            .connect(
                selectors::parse_selector::<VerboseError>(RUN_BUILDER_SELECTOR)
                    .expect("cannot parse run builder selector"),
                server_end.into_channel(),
            )
            .await
            .expect("Failed to send connect request")
            .expect(&format!(
                "failed to connect to {} as {}",
                RunBuilderMarker::DEBUG_NAME,
                RUN_BUILDER_SELECTOR
            ));
        proxy
    }

    pub fn new(remote_control: fremotecontrol::RemoteControlProxy) -> Box<Self> {
        Box::new(Self { remote_control })
    }
}
