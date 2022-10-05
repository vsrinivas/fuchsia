// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt::Debug;

use suite_definition::TestParamsOptions;

mod output_directory;
mod suite_definition;

use {
    anyhow::{anyhow, format_err, Context, Result},
    either::Either,
    errors::{ffx_bail, ffx_bail_with_code, ffx_error, ffx_error_with_code, FfxError},
    ffx_core::ffx_plugin,
    ffx_test_args::{
        DeleteResultCommand, ListCommand, ResultCommand, ResultSubCommand, RunCommand,
        ShowResultCommand, TestCommand, TestSubCommand,
    },
    fidl::endpoints::create_proxy,
    fidl_fuchsia_developer_remotecontrol as fremotecontrol,
    fidl_fuchsia_test_manager as ftest_manager,
    futures::FutureExt,
    lazy_static::lazy_static,
    output_directory::{DirectoryError, DirectoryId, DirectoryManager},
    signal_hook::{
        consts::signal::{SIGINT, SIGTERM},
        iterator::Signals,
    },
    std::io::{stdout, Write},
    std::path::PathBuf,
};

lazy_static! {
    /// Error code returned if connecting to Test Manager fails.
    static ref SETUP_FAILED_CODE: i32 = -fidl::Status::UNAVAILABLE.into_raw();
    /// Error code returned if tests time out.
    static ref TIMED_OUT_CODE: i32 = -fidl::Status::TIMED_OUT.into_raw();
}

#[ffx_plugin()]
pub async fn test(
    remote_control_result: Result<fremotecontrol::RemoteControlProxy>,
    cmd: TestCommand,
) -> Result<()> {
    let writer = Box::new(stdout());
    let remote_control =
        remote_control_result.map_err(|e| ffx_error_with_code!(*SETUP_FAILED_CODE, "{:?}", e))?;
    let (query_proxy, query_server_end) =
        fidl::endpoints::create_proxy::<ftest_manager::QueryMarker>()?;
    rcs::connect_with_timeout(
        std::time::Duration::from_secs(45),
        "core/test_manager:expose:fuchsia.test.manager.Query",
        &remote_control,
        query_server_end.into_channel(),
    )
    .await
    .map_err(|e| ffx_error_with_code!(*SETUP_FAILED_CODE, "{:?}", e))?;
    match cmd.subcommand {
        TestSubCommand::Run(run) => {
            run_test(testing_lib::RunBuilderConnector::new(remote_control), writer, run).await
        }
        TestSubCommand::List(list) => get_tests(query_proxy, writer, list).await,
        TestSubCommand::Result(result) => result_command(result, writer).await,
    }
}

async fn get_directory_manager(experiments: &Experiments) -> Result<DirectoryManager> {
    if !experiments.managed_structured_output.enabled {
        ffx_bail!(
            "Managed structured output is experimental and is subject to breaking changes. \
            To enable structured output run \
            'ffx config set {} true'",
            experiments.managed_structured_output.name
        )
    }
    let output_path_config: PathBuf = match ffx_config::get("test.output_path").await? {
        Some(output_path) => output_path,
        None => ffx_bail!(
            "Could not find the test output path configuration. Please run \
            `ffx config set test.output_path \"<PATH>\" to configure the location."
        ),
    };
    let save_count_config: usize = ffx_config::get("test.save_count").await?;
    Ok(DirectoryManager::new(output_path_config, save_count_config)?)
}

struct Experiment {
    name: &'static str,
    enabled: bool,
}

struct Experiments {
    managed_structured_output: Experiment,
    result_command: Experiment,
    json_input: Experiment,
    parallel_execution: Experiment,
}

impl Experiments {
    async fn get_experiment(experiment_name: &'static str) -> Experiment {
        Experiment {
            name: experiment_name,
            enabled: match ffx_config::get(experiment_name).await {
                Ok(enabled) => enabled,
                Err(_) => false,
            },
        }
    }

    async fn from_env() -> Self {
        Self {
            managed_structured_output: Self::get_experiment(
                "test.experimental_managed_structured_output",
            )
            .await,
            result_command: Self::get_experiment("test.experimental_result_command").await,
            json_input: Self::get_experiment("test.experimental_json_input").await,
            parallel_execution: Self::get_experiment("test.enable_experimental_parallel_execution")
                .await,
        }
    }
}

async fn run_test<W: 'static + Write + Send + Sync>(
    builder_connector: Box<testing_lib::RunBuilderConnector>,
    writer: W,
    cmd: RunCommand,
) -> Result<()> {
    let experiments = Experiments::from_env().await;

    let min_log_severity = cmd.min_severity_logs;

    let output_directory = match (cmd.disable_output_directory, &cmd.output_directory) {
        (true, _) => None, // user explicitly disabled output.
        (false, Some(directory)) => Some(directory.clone().into()), // an override directory is specified.

        // Default to a managed directory if enabled.
        (false, None) if experiments.managed_structured_output.enabled => {
            let mut directory_manager = get_directory_manager(&experiments).await?;
            Some(directory_manager.new_directory()?)
        }
        (false, None) => None,
    };
    let output_directory_options = output_directory
        .map(|root_path| run_test_suite_lib::DirectoryReporterOptions { root_path });
    let reporter =
        run_test_suite_lib::create_reporter(cmd.filter_ansi, output_directory_options, writer)?;

    let run_params = run_test_suite_lib::RunParams {
        timeout_behavior: match cmd.continue_on_timeout {
            false => run_test_suite_lib::TimeoutBehavior::TerminateRemaining,
            true => run_test_suite_lib::TimeoutBehavior::Continue,
        },
        timeout_grace_seconds: ffx_config::get::<u64, _>("test.timeout_grace_seconds").await?
            as u32,
        stop_after_failures: match cmd.stop_after_failures.map(std::num::NonZeroU32::new) {
            None => None,
            Some(None) => ffx_bail!("--stop-after-failures should be greater than zero."),
            Some(Some(stop_after)) => Some(stop_after),
        },
        experimental_parallel_execution: match (
            cmd.experimental_parallel_execution,
            experiments.parallel_execution.enabled,
        ) {
            (None, _) => None,
            (Some(max_parallel_suites), true) => Some(max_parallel_suites),
            (_, false) => ffx_bail!(
              "Parallel test suite execution is experimental and is subject to breaking changes. \
              To enable parallel test suite execution, run: \n \
              'ffx config set {} true'",
              experiments.parallel_execution.name
            ),
        },
        accumulate_debug_data: false, // ffx never accumulates.
        log_protocol: None,
    };
    let test_definitions =
        test_params_from_args(cmd, std::io::stdin, experiments.json_input.enabled)?;

    let (cancel_sender, cancel_receiver) = futures::channel::oneshot::channel::<()>();
    let mut signals = Signals::new(&[SIGINT, SIGTERM]).unwrap();
    // signals.forever() is blocking, so we need to spawn a thread rather than use async.
    let _signal_handle_thread = std::thread::spawn(move || {
        for signal in signals.forever() {
            match signal {
                SIGINT | SIGTERM => {
                    let _ = cancel_sender.send(());
                    break;
                }
                _ => unreachable!(),
            }
        }
    });

    match run_test_suite_lib::run_tests_and_get_outcome(
        builder_connector.connect().await,
        test_definitions,
        run_params,
        min_log_severity,
        reporter,
        cancel_receiver.map(|_| ()),
    )
    .await
    {
        run_test_suite_lib::Outcome::Passed => Ok(()),
        run_test_suite_lib::Outcome::Timedout => {
            ffx_bail_with_code!(*TIMED_OUT_CODE, "Tests timed out.",)
        }
        run_test_suite_lib::Outcome::Failed | run_test_suite_lib::Outcome::DidNotFinish => {
            ffx_bail!("Tests failed.")
        }
        run_test_suite_lib::Outcome::Cancelled => ffx_bail!("Tests cancelled."),
        run_test_suite_lib::Outcome::Inconclusive => ffx_bail!("Inconclusive test result."),
        run_test_suite_lib::Outcome::Error { origin } => match origin.is_internal_error() {
            // Using anyhow instead of ffx_bail here prints a message to file a bug.
            true => Err(anyhow!("There was an internal error running tests: {:?}", origin)),
            false => ffx_bail!("There was an error running tests: {:?}", origin),
        },
    }
}

/// Generate TestParams from |cmd|.
/// |stdin_handle_fn| is a function that generates a handle to stdin and is a parameter to enable
/// testing.
fn test_params_from_args<F, R>(
    cmd: RunCommand,
    stdin_handle_fn: F,
    json_input_experiment_enabled: bool,
) -> Result<impl ExactSizeIterator<Item = run_test_suite_lib::TestParams> + Debug, FfxError>
where
    F: Fn() -> R,
    R: std::io::Read,
{
    match &cmd.test_file {
        Some(_) if !json_input_experiment_enabled => {
            return Err(ffx_error!(
                "The --test-file option is experimental, and the input format is \
                subject to breaking changes. To enable using --test-file, run \
                'ffx config set test.experimental_json_input true'"
            ))
        }
        Some(filename) => {
            if !cmd.test_args.is_empty() {
                return Err(ffx_error!("Tests may not be specified in both args and by file"));
            }
            if filename == "-" {
                suite_definition::test_params_from_reader(
                    stdin_handle_fn(),
                    TestParamsOptions { ignore_test_without_known_execution: false },
                )
                .map_err(|e| ffx_error!("Failed to read test definitions: {:?}", e))
            } else {
                let file = std::fs::File::open(filename)
                    .map_err(|e| ffx_error!("Failed to open file {}: {:?}", filename, e))?;
                suite_definition::test_params_from_reader(
                    file,
                    TestParamsOptions { ignore_test_without_known_execution: false },
                )
                .map_err(|e| ffx_error!("Failed to read test definitions: {:?}", e))
            }
        }
        .map(|file_params| Either::Left(file_params.into_iter())),
        None => {
            let mut test_args_iter = cmd.test_args.iter();
            let (test_url, test_args) = match test_args_iter.next() {
                None => return Err(ffx_error!("No tests specified!")),
                Some(test_url) => {
                    (test_url.clone(), test_args_iter.map(String::clone).collect::<Vec<_>>())
                }
            };

            let test_params = run_test_suite_lib::TestParams {
                test_url,
                timeout_seconds: cmd.timeout.and_then(std::num::NonZeroU32::new),
                test_filters: if cmd.test_filter.len() == 0 { None } else { Some(cmd.test_filter) },
                max_severity_logs: cmd.max_severity_logs,
                also_run_disabled_tests: cmd.run_disabled,
                parallel: cmd.parallel,
                test_args,
                show_full_moniker: cmd.show_full_moniker_in_logs,
                tags: vec![],
            };

            let count = cmd.count.unwrap_or(1);
            let count = std::num::NonZeroU32::new(count)
                .ok_or_else(|| ffx_error!("--count should be greater than zero."))?;
            let repeated = (0..count.get()).map(move |_: u32| test_params.clone());
            Ok(repeated)
        }
        .map(Either::Right),
    }
}

async fn get_tests<W: Write>(
    query_proxy: ftest_manager::QueryProxy,
    mut write: W,
    cmd: ListCommand,
) -> Result<()> {
    let writer = &mut write;
    let (iterator_proxy, iterator) = create_proxy().unwrap();

    tracing::info!("launching test suite {}", cmd.test_url);

    query_proxy
        .enumerate(&cmd.test_url, iterator)
        .await
        .context("enumeration failed")?
        .map_err(|e| format_err!("error launching test: {:?}", e))?;

    loop {
        let cases = iterator_proxy.get_next().await?;
        if cases.is_empty() {
            return Ok(());
        }
        writeln!(writer, "Tests in suite {}:\n", cmd.test_url)?;
        for case in cases {
            match case.name {
                Some(n) => writeln!(writer, "{}", n)?,
                None => writeln!(writer, "<No name>")?,
            }
        }
    }
}

async fn result_command<W: Write>(
    ResultCommand { subcommand }: ResultCommand,
    mut writer: W,
) -> Result<()> {
    let experiments = Experiments::from_env().await;
    if !experiments.result_command.enabled {
        ffx_bail!(
            "The result subcommand is experimental and subject to breaking changes. \
            To enable structured output run 'ffx config set {} true'
        ",
            experiments.result_command.name
        )
    }

    match subcommand {
        ResultSubCommand::Show(ShowResultCommand { directory, index, name }) => {
            let directory_to_display = if let Some(directory_override) = directory {
                Some(directory_override.into())
            } else if let Some(specified_index) = index {
                get_directory_manager(&experiments)
                    .await?
                    .get_by_id(DirectoryId::Index(specified_index))?
            } else if let Some(specified_name) = name {
                get_directory_manager(&experiments)
                    .await?
                    .get_by_id(DirectoryId::Name(specified_name))?
            } else {
                get_directory_manager(&experiments).await?.latest_directory()?
            };
            match directory_to_display {
                Some(dir) => display_output_directory(dir, writer),
                None => {
                    writeln!(writer, "Directory not found")?;
                    Ok(())
                }
            }
        }
        ResultSubCommand::List(_) => result_list_command(writer, &experiments).await,
        ResultSubCommand::Delete(delete) => {
            result_delete_command(delete, writer, &experiments).await
        }
        ResultSubCommand::Save(save) => {
            get_directory_manager(&experiments).await?.save_directory(save.index, save.name)?;
            Ok(())
        }
    }
}

async fn result_list_command<W: Write>(mut writer: W, experiments: &Experiments) -> Result<()> {
    let entries = get_directory_manager(experiments).await?.entries_ordered()?;
    let unsaved_entries = entries
        .iter()
        .filter_map(|(id, entry)| match id {
            DirectoryId::Index(index) => Some((index, entry.timestamp)),
            DirectoryId::Name(_) => None,
        })
        .collect::<Vec<_>>();
    if unsaved_entries.len() > 0 {
        writeln!(writer, "Found run results:")?;
        for (id, maybe_timestamp) in unsaved_entries {
            match maybe_timestamp {
                Some(timestamp) => {
                    let local_time: chrono::DateTime<chrono::Local> = timestamp.into();
                    writeln!(writer, "{}: {}", id, local_time.to_rfc2822())?
                }
                None => writeln!(writer, "{}", id)?,
            }
        }
    }

    let saved_entries = entries
        .iter()
        .filter_map(|(id, entry)| match id {
            DirectoryId::Index(_) => None,
            DirectoryId::Name(name) => Some((name, entry.timestamp)),
        })
        .collect::<Vec<_>>();
    if saved_entries.len() > 0 {
        writeln!(writer, "Saved run results:")?;
        for (name, maybe_timestamp) in saved_entries {
            match maybe_timestamp {
                Some(timestamp) => {
                    let local_time: chrono::DateTime<chrono::Local> = timestamp.into();
                    writeln!(writer, "{}: {}", name, local_time.to_rfc2822())?
                }
                None => writeln!(writer, "{}", name)?,
            }
        }
    }
    Ok(())
}

fn display_output_directory<W: Write>(path: PathBuf, mut writer: W) -> Result<()> {
    let test_output_directory::TestRunResult { common: run_common, suites, .. } =
        test_output_directory::TestRunResult::from_dir(&path)?;
    let run_common = run_common.into_owned();

    writeln!(writer, "Run result: {:?}", run_common.outcome)?;
    let run_artifacts = run_common.artifact_dir.contents();
    if run_artifacts.len() > 0 {
        writeln!(
            writer,
            "Run artifacts: {}",
            run_artifacts.iter().map(|path| path.to_string_lossy()).collect::<Vec<_>>().join(", ")
        )?;
    }

    for suite in suites {
        let test_output_directory::SuiteResult { common: suite_common, cases, .. } = suite;
        let suite_common = suite_common.into_owned();
        writeln!(writer, "Suite {} result: {:?}", suite_common.name, suite_common.outcome)?;
        let artifacts = suite_common.artifact_dir.contents();
        if artifacts.len() > 0 {
            writeln!(
                writer,
                "\tArtifacts: {}",
                artifacts.iter().map(|path| path.to_string_lossy()).collect::<Vec<_>>().join(", ")
            )?;
        }
        for case in cases {
            let case_common = case.common.into_owned();
            writeln!(writer, "\tCase '{}' result: {:?}", case_common.name, case_common.outcome)?;
            let artifacts = case_common.artifact_dir.contents();
            if artifacts.len() > 0 {
                writeln!(
                    writer,
                    "\tCase {} Artifacts: {}",
                    case_common.name,
                    artifacts
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

async fn result_delete_command<W: Write>(
    DeleteResultCommand { index, name }: DeleteResultCommand,
    mut writer: W,
    experiments: &Experiments,
) -> Result<()> {
    let id = match (index, name) {
        (Some(_), Some(_)) => {
            writeln!(writer, "Cannot specify both index and name.")?;
            return Ok(());
        }
        (Some(index), None) => DirectoryId::Index(index),
        (None, Some(name)) => DirectoryId::Name(name),
        (None, None) => {
            writeln!(writer, "No directory specified.")?;
            return Ok(());
        }
    };
    match get_directory_manager(experiments).await?.delete(id) {
        Ok(()) => {
            writeln!(writer, "Deleted a run result.")?;
            Ok(())
        }
        Err(DirectoryError::IdNotFound(_)) => {
            writeln!(writer, "Directory not found")?;
            Ok(())
        }
        Err(e) => Err(e.into()),
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use lazy_static::lazy_static;
    use std::num::NonZeroU32;
    use test_list::TestTag;

    const VALID_INPUT_FILENAME: &str = "valid_defs.json";
    const INVALID_INPUT_FILENAME: &str = "invalid_defs.json";

    lazy_static! {
        static ref VALID_INPUT_FORMAT: String = serde_json::to_string(&serde_json::json!({
          "schema_id": "experimental",
          "data": [
            {
                "name": "{}-test-1",
                "labels": ["{}-label"],
                "execution": {
                    "type": "fuchsia_component",
                    "component_url": "{}-test-url-1",
                },
                "tags": [],
            },
            {
                "name": "{}-test-2",
                "labels": ["{}-label"],
                "execution": {
                    "type": "fuchsia_component",
                    "component_url": "{}-test-url-2",
                    "timeout_seconds": 60,
                },
                "tags": [],
            },
            {
                "name": "{}-test-3",
                "labels": ["{}-label"],
                "execution": {
                    "type": "fuchsia_component",
                    "component_url": "{}-test-url-3",
                    "test_args": ["--flag"],
                    "test_filters": ["Unit"],
                    "also_run_disabled_tests": true,
                    "parallel": 4,
                    "max_severity_logs": "INFO",
                },
                "tags": [{
                    "key": "hermetic",
                    "value": "true",
                }],
            }
        ]}))
        .expect("serialize json");
        static ref VALID_STDIN_INPUT: Vec<u8> =
            VALID_INPUT_FORMAT.replace("{}", "stdin").into_bytes();
        static ref VALID_FILE_INPUT: Vec<u8> =
            VALID_INPUT_FORMAT.replace("{}", "file").into_bytes();
        static ref INVALID_INPUT: Vec<u8> = vec![1u8; 64];
    }

    #[test]
    fn test_get_test_params() {
        let dir = tempfile::tempdir().expect("Create temp dir");
        std::fs::write(dir.path().join("test_defs.json"), &*VALID_FILE_INPUT).expect("write file");

        let cases = vec![
            (
                RunCommand {
                    timeout: None,
                    test_args: vec!["my-test-url".to_string()],
                    test_file: None,
                    test_filter: vec![],
                    run_disabled: false,
                    filter_ansi: false,
                    parallel: None,
                    count: None,
                    min_severity_logs: None,
                    show_full_moniker_in_logs: false,
                    max_severity_logs: None,
                    output_directory: None,
                    disable_output_directory: false,
                    continue_on_timeout: false,
                    stop_after_failures: None,
                    experimental_parallel_execution: None,
                },
                vec![run_test_suite_lib::TestParams {
                    test_url: "my-test-url".to_string(),
                    timeout_seconds: None,
                    test_filters: None,
                    also_run_disabled_tests: false,
                    show_full_moniker: false,
                    parallel: None,
                    test_args: vec![],
                    max_severity_logs: None,
                    tags: vec![],
                }],
            ),
            (
                RunCommand {
                    timeout: None,
                    test_args: vec!["my-test-url".to_string()],
                    test_file: None,
                    test_filter: vec![],
                    run_disabled: false,
                    filter_ansi: false,
                    parallel: None,
                    count: Some(10),
                    min_severity_logs: None,
                    show_full_moniker_in_logs: false,
                    max_severity_logs: Some(diagnostics_data::Severity::Warn),
                    output_directory: None,
                    disable_output_directory: false,
                    continue_on_timeout: false,
                    stop_after_failures: None,
                    experimental_parallel_execution: None,
                },
                vec![
                    run_test_suite_lib::TestParams {
                        test_url: "my-test-url".to_string(),
                        timeout_seconds: None,
                        test_filters: None,
                        show_full_moniker: false,
                        also_run_disabled_tests: false,
                        max_severity_logs: Some(diagnostics_data::Severity::Warn),
                        parallel: None,
                        test_args: vec![],
                        tags: vec![],
                    };
                    10
                ],
            ),
            (
                RunCommand {
                    timeout: Some(10),
                    test_args: vec!["my-test-url".to_string(), "--".to_string(), "arg".to_string()],
                    test_file: None,
                    test_filter: vec!["filter".to_string()],
                    run_disabled: true,
                    filter_ansi: false,
                    parallel: Some(20),
                    count: None,
                    show_full_moniker_in_logs: false,
                    min_severity_logs: None,
                    max_severity_logs: None,
                    output_directory: None,
                    disable_output_directory: false,
                    continue_on_timeout: false,
                    stop_after_failures: None,
                    experimental_parallel_execution: None,
                },
                vec![run_test_suite_lib::TestParams {
                    test_url: "my-test-url".to_string(),
                    timeout_seconds: Some(NonZeroU32::new(10).unwrap()),
                    test_filters: Some(vec!["filter".to_string()]),
                    also_run_disabled_tests: true,
                    show_full_moniker: false,
                    max_severity_logs: None,
                    parallel: Some(20),
                    test_args: vec!["--".to_string(), "arg".to_string()],
                    tags: vec![],
                }],
            ),
            (
                RunCommand {
                    timeout: None,
                    test_args: vec![],
                    test_file: Some("-".to_string()),
                    test_filter: vec![],
                    run_disabled: false,
                    filter_ansi: false,
                    parallel: None,
                    count: None,
                    min_severity_logs: None,
                    show_full_moniker_in_logs: false,
                    max_severity_logs: None,
                    output_directory: None,
                    disable_output_directory: false,
                    continue_on_timeout: false,
                    stop_after_failures: None,
                    experimental_parallel_execution: None,
                },
                vec![
                    run_test_suite_lib::TestParams {
                        test_url: "stdin-test-url-1".to_string(),
                        timeout_seconds: None,
                        test_filters: None,
                        also_run_disabled_tests: false,
                        show_full_moniker: false,
                        max_severity_logs: None,
                        parallel: None,
                        test_args: vec![],
                        tags: vec![],
                    },
                    run_test_suite_lib::TestParams {
                        test_url: "stdin-test-url-2".to_string(),
                        timeout_seconds: Some(NonZeroU32::new(60).unwrap()),
                        test_filters: None,
                        show_full_moniker: false,
                        also_run_disabled_tests: false,
                        max_severity_logs: None,
                        parallel: None,
                        test_args: vec![],
                        tags: vec![],
                    },
                    run_test_suite_lib::TestParams {
                        test_url: "stdin-test-url-3".to_string(),
                        timeout_seconds: None,
                        test_filters: Some(vec!["Unit".to_string()]),
                        also_run_disabled_tests: true,
                        max_severity_logs: Some(diagnostics_data::Severity::Info),
                        show_full_moniker: false,
                        parallel: Some(4),
                        test_args: vec!["--flag".to_string()],
                        tags: vec![TestTag {
                            key: "hermetic".to_string(),
                            value: "true".to_string(),
                        }],
                    },
                ],
            ),
            (
                RunCommand {
                    timeout: None,
                    test_args: vec![],
                    test_file: Some(
                        dir.path().join("test_defs.json").to_str().unwrap().to_string(),
                    ),
                    test_filter: vec![],
                    run_disabled: false,
                    filter_ansi: false,
                    parallel: None,
                    count: None,
                    min_severity_logs: None,
                    show_full_moniker_in_logs: false,
                    max_severity_logs: None,
                    output_directory: None,
                    disable_output_directory: false,
                    continue_on_timeout: false,
                    stop_after_failures: None,
                    experimental_parallel_execution: None,
                },
                vec![
                    run_test_suite_lib::TestParams {
                        test_url: "file-test-url-1".to_string(),
                        timeout_seconds: None,
                        test_filters: None,
                        also_run_disabled_tests: false,
                        show_full_moniker: false,
                        max_severity_logs: None,
                        parallel: None,
                        test_args: vec![],
                        tags: vec![],
                    },
                    run_test_suite_lib::TestParams {
                        test_url: "file-test-url-2".to_string(),
                        timeout_seconds: Some(NonZeroU32::new(60).unwrap()),
                        test_filters: None,
                        also_run_disabled_tests: false,
                        show_full_moniker: false,
                        max_severity_logs: None,
                        parallel: None,
                        test_args: vec![],
                        tags: vec![],
                    },
                    run_test_suite_lib::TestParams {
                        test_url: "file-test-url-3".to_string(),
                        timeout_seconds: None,
                        test_filters: Some(vec!["Unit".to_string()]),
                        also_run_disabled_tests: true,
                        show_full_moniker: false,
                        max_severity_logs: Some(diagnostics_data::Severity::Info),
                        parallel: Some(4),
                        test_args: vec!["--flag".to_string()],
                        tags: vec![TestTag {
                            key: "hermetic".to_string(),
                            value: "true".to_string(),
                        }],
                    },
                ],
            ),
        ];

        for (run_command, expected_test_params) in cases.into_iter() {
            let result = test_params_from_args(
                run_command.clone(),
                || std::io::Cursor::new(&*VALID_STDIN_INPUT),
                true,
            );
            assert!(
                result.is_ok(),
                "Error getting test params from {:?}: {:?}",
                run_command,
                result.unwrap_err()
            );
            assert_eq!(result.unwrap().into_iter().collect::<Vec<_>>(), expected_test_params);
        }
    }

    #[test]
    fn test_get_test_params_count() {
        // Regression test for https://fxbug.dev/111145: using an extremely
        // large test count should result in a modest memory allocation. If
        // that wasn't the case, this test would fail.
        const COUNT: u32 = u32::MAX;
        let params = test_params_from_args(
            RunCommand {
                test_args: vec!["my-test-url".to_string()],
                count: Some(COUNT),
                timeout: None,
                test_file: None,
                test_filter: vec![],
                run_disabled: false,
                filter_ansi: false,
                parallel: None,
                min_severity_logs: None,
                show_full_moniker_in_logs: false,
                max_severity_logs: Some(diagnostics_data::Severity::Warn),
                output_directory: None,
                disable_output_directory: false,
                continue_on_timeout: false,
                stop_after_failures: None,
                experimental_parallel_execution: None,
            },
            || std::io::Cursor::new(&*VALID_STDIN_INPUT),
            true,
        )
        .expect("should succeed");
        assert_eq!(params.len(), usize::try_from(COUNT).unwrap());
    }

    #[test]
    fn test_get_test_params_invalid_args() {
        let dir = tempfile::tempdir().expect("Create temp dir");
        std::fs::write(dir.path().join(VALID_INPUT_FILENAME), &*VALID_FILE_INPUT)
            .expect("write file");
        std::fs::write(dir.path().join(INVALID_INPUT_FILENAME), &*INVALID_INPUT)
            .expect("write file");
        let cases = vec![
            (
                "no tests specified",
                RunCommand {
                    timeout: None,
                    test_args: vec![],
                    test_file: None,
                    test_filter: vec![],
                    run_disabled: false,
                    filter_ansi: false,
                    parallel: None,
                    count: None,
                    min_severity_logs: None,
                    show_full_moniker_in_logs: false,
                    max_severity_logs: None,
                    output_directory: None,
                    disable_output_directory: false,
                    continue_on_timeout: false,
                    stop_after_failures: None,
                    experimental_parallel_execution: None,
                },
            ),
            (
                "tests specified in both args and file",
                RunCommand {
                    timeout: None,
                    test_args: vec!["my-test".to_string()],
                    test_file: Some(
                        dir.path().join(VALID_INPUT_FILENAME).to_str().unwrap().to_string(),
                    ),
                    test_filter: vec![],
                    run_disabled: false,
                    filter_ansi: false,
                    parallel: None,
                    count: None,
                    min_severity_logs: None,
                    show_full_moniker_in_logs: false,
                    max_severity_logs: None,
                    output_directory: None,
                    disable_output_directory: false,
                    continue_on_timeout: false,
                    stop_after_failures: None,
                    experimental_parallel_execution: None,
                },
            ),
            (
                "read invalid input from file",
                RunCommand {
                    timeout: None,
                    test_args: vec![],
                    test_file: Some(
                        dir.path().join(INVALID_INPUT_FILENAME).to_str().unwrap().to_string(),
                    ),
                    test_filter: vec![],
                    run_disabled: false,
                    filter_ansi: false,
                    parallel: None,
                    count: None,
                    min_severity_logs: None,
                    show_full_moniker_in_logs: false,
                    max_severity_logs: None,
                    output_directory: None,
                    disable_output_directory: false,
                    continue_on_timeout: false,
                    stop_after_failures: None,
                    experimental_parallel_execution: None,
                },
            ),
        ];

        for (case_name, invalid_run_command) in cases.into_iter() {
            let result = test_params_from_args(
                invalid_run_command,
                || std::io::Cursor::new(&*VALID_STDIN_INPUT),
                true,
            );
            assert!(
                result.is_err(),
                "Getting test params for case '{}' unexpectedly succeeded",
                case_name
            );
        }
    }
}
