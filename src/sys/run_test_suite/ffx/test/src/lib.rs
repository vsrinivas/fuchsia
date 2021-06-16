// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod output_directory;

use {
    anyhow::{anyhow, format_err, Context, Result},
    async_trait::async_trait,
    errors::ffx_bail,
    ffx_core::ffx_plugin,
    ffx_test_args::{ListCommand, ResultCommand, RunCommand, TestCommand, TestSubcommand},
    fidl::endpoints::create_proxy,
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_developer_remotecontrol as fremotecontrol,
    fidl_fuchsia_test::CaseIteratorMarker,
    fidl_fuchsia_test_manager as ftest_manager,
    ftest_manager::{RunBuilderMarker, RunBuilderProxy},
    output_directory::DirectoryManager,
    run_test_suite_lib::diagnostics,
    std::fs::File,
    std::io::{stdout, Write},
    std::path::PathBuf,
};

const RUN_BUILDER_SELECTOR: &str = "core/test_manager:expose:fuchsia.test.manager.RunBuilder";

struct RunBuilderConnector {
    remote_control: fremotecontrol::RemoteControlProxy,
}

#[async_trait]
impl run_test_suite_lib::BuilderConnector for RunBuilderConnector {
    async fn connect(&self) -> RunBuilderProxy {
        let (proxy, server_end) = fidl::endpoints::create_proxy::<RunBuilderMarker>()
            .expect(&format!("failed to create proxy to {}", RunBuilderMarker::DEBUG_NAME));
        self.remote_control
            .connect(
                selectors::parse_selector(RUN_BUILDER_SELECTOR)
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
}

impl RunBuilderConnector {
    fn new(remote_control: fremotecontrol::RemoteControlProxy) -> Box<Self> {
        Box::new(Self { remote_control })
    }
}

#[ffx_plugin(
    "cmd-test.experimental",
    ftest_manager::HarnessProxy = "core/test_manager:expose:fuchsia.test.manager.Harness"
)]
pub async fn test(
    harness_proxy: ftest_manager::HarnessProxy,
    remote_control: fremotecontrol::RemoteControlProxy,
    cmd: TestCommand,
) -> Result<()> {
    let writer = Box::new(stdout());

    match cmd.subcommand {
        TestSubcommand::Run(run) => {
            run_test(harness_proxy, RunBuilderConnector::new(remote_control), run).await
        }
        TestSubcommand::List(list) => get_tests(harness_proxy, writer, list).await,
        TestSubcommand::Result(result) => result_command(result, writer).await,
    }
}

async fn get_directory_manager() -> Result<DirectoryManager> {
    let output_path_config: PathBuf = match ffx_config::get("test.output_path").await? {
        Some(output_path) => output_path,
        None => ffx_bail!(
            "Could not find the test output path configuration. Please run \
            `ffx config set test.output_path \"<PATH>\" to configure the location."
        ),
    };
    let save_count_config: usize = ffx_config::get("test.save_count").await?;
    DirectoryManager::new(output_path_config, save_count_config)
}

async fn run_test(
    harness_proxy: ftest_manager::HarnessProxy,
    builder_connector: Box<RunBuilderConnector>,
    cmd: RunCommand,
) -> Result<()> {
    let count = cmd.count.unwrap_or(1);
    let count = std::num::NonZeroU16::new(count)
        .ok_or_else(|| anyhow!("--count should be greater than zero."))?;

    let output_directory = match (cmd.disable_output_directory, cmd.output_directory) {
        (true, _) => None, // output to directory is disabled.
        (false, Some(directory)) => Some(directory.into()), // an override directory is specified.
        (false, None) => {
            // default to using a managed directory.
            let mut directory_manager = get_directory_manager().await?;
            Some(directory_manager.new_directory()?)
        }
    };

    match run_test_suite_lib::run_tests_and_get_outcome(
        run_test_suite_lib::TestParams {
            test_url: cmd.test_url,
            timeout: cmd.timeout.and_then(std::num::NonZeroU32::new),
            test_filter: cmd.test_filter,
            also_run_disabled_tests: cmd.run_disabled,
            parallel: cmd.parallel,
            test_args: vec![],
            harness: harness_proxy,
            builder_connector: builder_connector,
        },
        diagnostics::LogCollectionOptions {
            min_severity: cmd.min_severity_logs,
            max_severity: cmd.max_severity_logs,
        },
        count,
        cmd.filter_ansi,
        output_directory,
    )
    .await
    {
        run_test_suite_lib::Outcome::Passed => Ok(()),
        run_test_suite_lib::Outcome::Timedout => Err(anyhow!("Tests timed out")),
        run_test_suite_lib::Outcome::Failed
        | run_test_suite_lib::Outcome::Inconclusive
        | run_test_suite_lib::Outcome::Error => Err(anyhow!("There was an error running tests")),
    }
}

async fn get_tests<W: Write>(
    harness_proxy: ftest_manager::HarnessProxy,
    mut write: W,
    cmd: ListCommand,
) -> Result<()> {
    let writer = &mut write;
    let (suite_proxy, suite_server_end) = create_proxy().unwrap();
    let (_controller_proxy, controller_server_end) = create_proxy().unwrap();

    log::info!("launching test suite {}", cmd.test_url);

    let _result = harness_proxy
        .launch_suite(
            &cmd.test_url,
            ftest_manager::LaunchOptions::EMPTY,
            suite_server_end,
            controller_server_end,
        )
        .await
        .context("launch_suite call failed")?
        .map_err(|e| format_err!("error launching test: {:?}", e))?;

    let (case_iterator, test_server_end) = create_proxy::<CaseIteratorMarker>()?;
    suite_proxy
        .get_tests(test_server_end)
        .map_err(|e| format_err!("Error getting test steps: {}", e))?;

    loop {
        let cases = case_iterator.get_next().await?;
        if cases.is_empty() {
            return Ok(());
        }
        writeln!(writer, "Tests in suite {}:\n", cmd.test_url)?;
        for case in cases {
            match case.name {
                Some(n) => writeln!(writer, "{}", n)?,
                None => writeln!(writer, "<No name>")?,
            };
        }
    }
}

async fn result_command<W: Write>(
    ResultCommand { directory }: ResultCommand,
    mut writer: W,
) -> Result<()> {
    match directory {
        Some(specified_directory) => display_output_directory(specified_directory.into(), writer),
        None => {
            let directory_manager = get_directory_manager().await?;
            match directory_manager.latest_directory()? {
                Some(latest) => display_output_directory(latest, writer),
                None => writeln!(writer, "Found no test results to display").map_err(Into::into),
            }
        }
    }
}

fn display_output_directory<W: Write>(path: PathBuf, mut writer: W) -> Result<()> {
    let summary_path = path.join(test_output_directory::RUN_SUMMARY_NAME);
    let summary_file = File::open(summary_path)?;

    let test_output_directory::TestRunResult::V0 {
        artifacts: run_artifacts,
        outcome: run_outcome,
        suites,
        ..
    } = serde_json::from_reader(summary_file)?;

    writeln!(writer, "Run result: {:?}", run_outcome)?;
    if run_artifacts.len() > 0 {
        writeln!(
            writer,
            "Run artifacts: {}",
            run_artifacts.iter().map(|path| path.to_string_lossy()).collect::<Vec<_>>().join(", ")
        )?;
    }

    for suite in suites.iter() {
        let suite_summary_path = path.join(&suite.summary);
        let suite_summary_file = File::open(suite_summary_path)?;
        let test_output_directory::SuiteResult::V0 { artifacts, outcome, name, cases, .. } =
            serde_json::from_reader(suite_summary_file)?;
        writeln!(writer, "Suite {} result: {:?}", name, outcome)?;
        if artifacts.len() > 0 {
            writeln!(
                writer,
                "\tArtifacts: {}",
                artifacts.iter().map(|path| path.to_string_lossy()).collect::<Vec<_>>().join(", ")
            )?;
        }
        for case in cases.iter() {
            writeln!(writer, "\tCase '{}' result: {:?}", case.name, case.outcome)?;
            if case.artifacts.len() > 0 {
                writeln!(
                    writer,
                    "\tCase {} Artifacts: {}",
                    case.name,
                    case.artifacts
                        .iter()
                        .map(|path| path.to_string_lossy())
                        .collect::<Vec<_>>()
                        .join(", ")
                )?;
            }
        }
    }

    Ok(())
}
