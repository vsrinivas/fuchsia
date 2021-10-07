// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod output_directory;

use {
    anyhow::{anyhow, format_err, Context, Result},
    async_trait::async_trait,
    errors::ffx_bail,
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
    output_directory::{DirectoryError, DirectoryId, DirectoryManager},
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

#[ffx_plugin(ftest_manager::QueryProxy = "core/test_manager:expose:fuchsia.test.manager.Query")]
pub async fn test(
    query_proxy: ftest_manager::QueryProxy,
    remote_control: fremotecontrol::RemoteControlProxy,
    cmd: TestCommand,
) -> Result<()> {
    let writer = Box::new(stdout());

    match cmd.subcommand {
        TestSubcommand::Run(run) => run_test(RunBuilderConnector::new(remote_control), run).await,
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

async fn run_test(builder_connector: Box<RunBuilderConnector>, cmd: RunCommand) -> Result<()> {
    let count = cmd.count.unwrap_or(1);
    let count = std::num::NonZeroU16::new(count)
        .ok_or_else(|| anyhow!("--count should be greater than zero."))?;

    // Whether or not the experimental structured output is enabled.
    // When the experiment is disabled and the user attempts to use a strucutured output option,
    // we bail.
    let structured_output_experiment =
        match ffx_config::get("test.experimental_structured_output").await {
            Ok(true) => true,
            Ok(false) | Err(_) => false,
        };
    let output_directory =
        match (cmd.disable_output_directory, cmd.output_directory, structured_output_experiment) {
            (true, _, _) => None, // user explicitly disabled output.
            (false, Some(directory), true) => Some(directory.into()), // an override directory is specified.
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

    match run_test_suite_lib::run_tests_and_get_outcome(
        run_test_suite_lib::TestParams {
            test_url: cmd.test_url,
            timeout: cmd.timeout.and_then(std::num::NonZeroU32::new),
            test_filters: if cmd.test_filter.len() == 0 { None } else { Some(cmd.test_filter) },
            also_run_disabled_tests: cmd.run_disabled,
            parallel: cmd.parallel,
            test_args: cmd.test_args,
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
        run_test_suite_lib::Outcome::Timedout => ffx_bail!("Tests timed out."),
        run_test_suite_lib::Outcome::Failed => ffx_bail!("Tests failed."),
        run_test_suite_lib::Outcome::Inconclusive => ffx_bail!("Inconclusive test result."),
        run_test_suite_lib::Outcome::Error { internal } => match internal {
            // Using anyhow instead of ffx_bail here prints a message to file a bug.
            true => Err(anyhow!("There was an internal error running tests.")),
            false => ffx_bail!("There was an error running tests."),
        },
    }
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
