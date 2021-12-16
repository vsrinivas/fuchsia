// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod output_directory;
mod suite_definition;

use {
    anyhow::{anyhow, format_err, Context, Result},
    errors::{ffx_bail, ffx_bail_with_code, ffx_error, ffx_error_with_code, FfxError},
    ffx_core::ffx_plugin,
    ffx_test_args::{
        DeleteResultCommand, ListCommand, ResultCommand, ResultSubCommand, RunCommand,
        ShowResultCommand, TestCommand, TestSubcommand,
    },
    fidl::endpoints::create_proxy,
    fidl::endpoints::ProtocolMarker,
    fidl_fuchsia_developer_remotecontrol as fremotecontrol,
    fidl_fuchsia_test_manager as ftest_manager,
    ftest_manager::{RunBuilderMarker, RunBuilderProxy},
    futures::FutureExt,
    lazy_static::lazy_static,
    output_directory::{DirectoryError, DirectoryId, DirectoryManager},
    selectors::{self, VerboseError},
    signal_hook::{
        consts::signal::{SIGINT, SIGTERM},
        iterator::Signals,
    },
    std::fs::File,
    std::io::{stdout, Write},
    std::path::PathBuf,
};

const RUN_BUILDER_SELECTOR: &str = "core/test_manager:expose:fuchsia.test.manager.RunBuilder";

lazy_static! {
    /// Error code returned if connecting to Test Manager fails.
    static ref SETUP_FAILED_CODE: i32 = -fidl::Status::UNAVAILABLE.into_raw();
    /// Error code returned if tests time out.
    static ref TIMED_OUT_CODE: i32 = -fidl::Status::TIMED_OUT.into_raw();
}

struct RunBuilderConnector {
    remote_control: fremotecontrol::RemoteControlProxy,
}

impl RunBuilderConnector {
    async fn connect(&self) -> RunBuilderProxy {
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

    fn new(remote_control: fremotecontrol::RemoteControlProxy) -> Box<Self> {
        Box::new(Self { remote_control })
    }
}

#[ffx_plugin(ftest_manager::QueryProxy = "core/test_manager:expose:fuchsia.test.manager.Query")]
pub async fn test(
    query_proxy_result: Result<ftest_manager::QueryProxy>,
    remote_control_result: Result<fremotecontrol::RemoteControlProxy>,
    cmd: TestCommand,
) -> Result<()> {
    let writer = Box::new(stdout());

    let query_proxy =
        query_proxy_result.map_err(|e| ffx_error_with_code!(*SETUP_FAILED_CODE, "{:?}", e))?;
    let remote_control =
        remote_control_result.map_err(|e| ffx_error_with_code!(*SETUP_FAILED_CODE, "{:?}", e))?;

    match cmd.subcommand {
        TestSubcommand::Run(run) => {
            run_test(RunBuilderConnector::new(remote_control), writer, run).await
        }
        TestSubcommand::List(list) => get_tests(query_proxy, writer, list).await,
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
    Ok(DirectoryManager::new(output_path_config, save_count_config)?)
}

async fn run_test<W: 'static + Write + Send + Sync>(
    builder_connector: Box<RunBuilderConnector>,
    writer: W,
    cmd: RunCommand,
) -> Result<()> {
    // Whether or not the experimental structured output is enabled.
    // When the experiment is disabled and the user attempts to use a strucutured output option,
    // we bail.
    let structured_output_experiment =
        match ffx_config::get("test.experimental_structured_output").await {
            Ok(true) => true,
            Ok(false) | Err(_) => false,
        };
    let output_directory =
        match (cmd.disable_output_directory, &cmd.output_directory, structured_output_experiment) {
            (true, _, _) => None, // user explicitly disabled output.
            (false, Some(directory), true) => Some(directory.clone().into()), // an override directory is specified.
            // user specified an override, but output is disabled by experiment flag.
            (false, Some(_), false) => {
                ffx_bail!(
                    "Structured output is experimental and is subject to breaking changes. \
                To enable structured output run \
                'ffx config set test.experimental_structured_output true'"
                )
            }
            // Default to a managed directory when structured output is enabled.
            (false, None, true) => {
                let mut directory_manager = get_directory_manager().await?;
                Some(directory_manager.new_directory()?)
            }
            // Default to nothing when structured output is disabled.
            (false, None, false) => None,
        };

    let min_log_severity = cmd.min_severity_logs;
    let filter_ansi = cmd.filter_ansi;

    let json_input_experiment = match ffx_config::get("test.experimental_json_input").await {
        Ok(true) => true,
        Ok(false) | Err(_) => false,
    };
    let run_params = run_test_suite_lib::RunParams {
        timeout_behavior: match cmd.continue_on_timeout {
            false => run_test_suite_lib::TimeoutBehavior::TerminateRemaining,
            true => run_test_suite_lib::TimeoutBehavior::Continue,
        },
        stop_after_failures: match cmd.stop_after_failures.map(std::num::NonZeroU16::new) {
            None => None,
            Some(None) => ffx_bail!("--stop-after-failures should be greater than zero."),
            Some(Some(stop_after)) => Some(stop_after),
        },
    };
    let test_definitions = test_params_from_args(cmd, std::io::stdin, json_input_experiment)?;

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
    let reporter = create_reporter(filter_ansi, output_directory, writer)?;
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
) -> Result<Vec<run_test_suite_lib::TestParams>, FfxError>
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
                suite_definition::test_params_from_reader(stdin_handle_fn())
                    .map_err(|e| ffx_error!("Failed to read test definitions: {:?}", e))
            } else {
                let file = std::fs::File::open(filename)
                    .map_err(|e| ffx_error!("Failed to open file {}: {:?}", filename, e))?;
                suite_definition::test_params_from_reader(file)
                    .map_err(|e| ffx_error!("Failed to read test definitions: {:?}", e))
            }
        }
        None => {
            let mut test_args_iter = cmd.test_args.iter();
            let (test_url, test_args) = match test_args_iter.next() {
                None => return Err(ffx_error!("No tests specified!")),
                Some(test_url) => {
                    (test_url.clone(), test_args_iter.map(String::clone).collect::<Vec<_>>())
                }
            };

            let count = cmd.count.unwrap_or(1);
            let count = std::num::NonZeroU16::new(count)
                .ok_or_else(|| ffx_error!("--count should be greater than zero."))?;
            Ok(vec![
                run_test_suite_lib::TestParams {
                    test_url,
                    timeout: cmd.timeout.and_then(std::num::NonZeroU32::new),
                    test_filters: if cmd.test_filter.len() == 0 {
                        None
                    } else {
                        Some(cmd.test_filter)
                    },
                    max_severity_logs: cmd.max_severity_logs,
                    also_run_disabled_tests: cmd.run_disabled,
                    parallel: cmd.parallel,
                    test_args,
                };
                count.get() as usize
            ])
        }
    }
}

fn create_reporter<W: 'static + Write + Send + Sync>(
    filter_ansi: bool,
    dir: Option<PathBuf>,
    writer: W,
) -> Result<run_test_suite_lib::output::RunReporter> {
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

async fn get_tests<W: Write>(
    query_proxy: ftest_manager::QueryProxy,
    mut write: W,
    cmd: ListCommand,
) -> Result<()> {
    let writer = &mut write;
    let (iterator_proxy, iterator) = create_proxy().unwrap();

    log::info!("launching test suite {}", cmd.test_url);

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
    match ffx_config::get("test.experimental_structured_output").await {
        Ok(true) => (),
        Ok(false) | Err(_) => ffx_bail!(
            "The result subcommand relies on structured output, which is experimental \
            and subject to breaking changes. \
            To enable structured output run 'ffx config set test.experimental_structured_output true'
        ")
    }

    match subcommand {
        ResultSubCommand::Show(ShowResultCommand { directory, index, name }) => {
            let directory_to_display = if let Some(directory_override) = directory {
                Some(directory_override.into())
            } else if let Some(specified_index) = index {
                get_directory_manager().await?.get_by_id(DirectoryId::Index(specified_index))?
            } else if let Some(specified_name) = name {
                get_directory_manager().await?.get_by_id(DirectoryId::Name(specified_name))?
            } else {
                get_directory_manager().await?.latest_directory()?
            };
            match directory_to_display {
                Some(dir) => display_output_directory(dir, writer),
                None => {
                    writeln!(writer, "Directory not found")?;
                    Ok(())
                }
            }
        }
        ResultSubCommand::List(_) => result_list_command(writer).await,
        ResultSubCommand::Delete(delete) => result_delete_command(delete, writer).await,
        ResultSubCommand::Save(save) => {
            get_directory_manager().await?.save_directory(save.index, save.name)?;
            Ok(())
        }
    }
}

async fn result_list_command<W: Write>(mut writer: W) -> Result<()> {
    let entries = get_directory_manager().await?.entries_ordered()?;
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
            run_artifacts.keys().map(|path| path.to_string_lossy()).collect::<Vec<_>>().join(", ")
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
                artifacts.keys().map(|path| path.to_string_lossy()).collect::<Vec<_>>().join(", ")
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
                        .keys()
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
    match get_directory_manager().await?.delete(id) {
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

    const VALID_INPUT_FILENAME: &str = "valid_defs.json";
    const INVALID_INPUT_FILENAME: &str = "invalid_defs.json";

    lazy_static! {
        static ref VALID_STDIN_INPUT: Vec<u8> = serde_json::to_vec(&serde_json::json!([
            {
                "test_url": "stdin-test-url-1",
            },
            {
                "test_url": "stdin-test-url-2",
                "timeout": 60,
            }
        ]))
        .expect("serialize json");
        static ref VALID_FILE_INPUT: Vec<u8> = serde_json::to_vec(&serde_json::json!([
            {
                "test_url": "file-test-url-1",
            },
            {
                "test_url": "file-test-url-2",
                "timeout": 60,
            }
        ]))
        .expect("serialize json");
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
                    max_severity_logs: None,
                    output_directory: None,
                    disable_output_directory: false,
                    continue_on_timeout: false,
                    stop_after_failures: None,
                },
                vec![run_test_suite_lib::TestParams {
                    test_url: "my-test-url".to_string(),
                    timeout: None,
                    test_filters: None,
                    also_run_disabled_tests: false,
                    parallel: None,
                    test_args: vec![],
                    max_severity_logs: None,
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
                    max_severity_logs: Some(diagnostics_data::Severity::Warn),
                    output_directory: None,
                    disable_output_directory: false,
                    continue_on_timeout: false,
                    stop_after_failures: None,
                },
                vec![
                    run_test_suite_lib::TestParams {
                        test_url: "my-test-url".to_string(),
                        timeout: None,
                        test_filters: None,
                        also_run_disabled_tests: false,
                        max_severity_logs: Some(diagnostics_data::Severity::Warn),
                        parallel: None,
                        test_args: vec![],
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
                    min_severity_logs: None,
                    max_severity_logs: None,
                    output_directory: None,
                    disable_output_directory: false,
                    continue_on_timeout: false,
                    stop_after_failures: None,
                },
                vec![run_test_suite_lib::TestParams {
                    test_url: "my-test-url".to_string(),
                    timeout: Some(NonZeroU32::new(10).unwrap()),
                    test_filters: Some(vec!["filter".to_string()]),
                    also_run_disabled_tests: true,
                    max_severity_logs: None,
                    parallel: Some(20),
                    test_args: vec!["--".to_string(), "arg".to_string()],
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
                    max_severity_logs: None,
                    output_directory: None,
                    disable_output_directory: false,
                    continue_on_timeout: false,
                    stop_after_failures: None,
                },
                vec![
                    run_test_suite_lib::TestParams {
                        test_url: "stdin-test-url-1".to_string(),
                        timeout: None,
                        test_filters: None,
                        also_run_disabled_tests: false,
                        max_severity_logs: None,
                        parallel: None,
                        test_args: vec![],
                    },
                    run_test_suite_lib::TestParams {
                        test_url: "stdin-test-url-2".to_string(),
                        timeout: Some(NonZeroU32::new(60).unwrap()),
                        test_filters: None,
                        also_run_disabled_tests: false,
                        max_severity_logs: None,
                        parallel: None,
                        test_args: vec![],
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
                    max_severity_logs: None,
                    output_directory: None,
                    disable_output_directory: false,
                    continue_on_timeout: false,
                    stop_after_failures: None,
                },
                vec![
                    run_test_suite_lib::TestParams {
                        test_url: "file-test-url-1".to_string(),
                        timeout: None,
                        test_filters: None,
                        also_run_disabled_tests: false,
                        max_severity_logs: None,
                        parallel: None,
                        test_args: vec![],
                    },
                    run_test_suite_lib::TestParams {
                        test_url: "file-test-url-2".to_string(),
                        timeout: Some(NonZeroU32::new(60).unwrap()),
                        test_filters: None,
                        also_run_disabled_tests: false,
                        max_severity_logs: None,
                        parallel: None,
                        test_args: vec![],
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
            assert_eq!(result.unwrap(), expected_test_params);
        }
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
                    max_severity_logs: None,
                    output_directory: None,
                    disable_output_directory: false,
                    continue_on_timeout: false,
                    stop_after_failures: None,
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
                    max_severity_logs: None,
                    output_directory: None,
                    disable_output_directory: false,
                    continue_on_timeout: false,
                    stop_after_failures: None,
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
                    max_severity_logs: None,
                    output_directory: None,
                    disable_output_directory: false,
                    continue_on_timeout: false,
                    stop_after_failures: None,
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
