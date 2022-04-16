// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    errors::FfxError,
    ffx_core::ffx_plugin,
    ffx_triage_args::TriageCommand,
    fidl_fuchsia_feedback::DataProviderProxy,
    std::{io::Write, path::PathBuf},
    tempfile::tempdir,
    triage::{analyze, ActionResultFormatter, ActionTagDirective},
    triage_app_lib::file_io::{config_from_files, diagnostics_from_directory},
};

mod config;
mod snapshot;
pub use snapshot::create_snapshot;

#[ffx_plugin("triage.enabled", DataProviderProxy = "core/appmgr:out:fuchsia.feedback.DataProvider")]
pub async fn triage(data_provider_proxy: DataProviderProxy, cmd: TriageCommand) -> Result<()> {
    triage_impl(data_provider_proxy, cmd, &mut std::io::stdout()).await.map_err(flatten_error)
}

async fn triage_impl<W: Write>(
    data_provider_proxy: DataProviderProxy,
    cmd: TriageCommand,
    writer: &mut W,
) -> Result<()> {
    let TriageCommand { config, data, tags, exclude_tags } = cmd;

    let config_files = config::get_or_default_config_files(config)?;

    let data_directory = match data {
        Some(d) => PathBuf::from(d),
        None => {
            let snapshot_tempdir =
                tempdir().context("Unable to create temporary snapshot directory.")?;
            let _ = snapshot::create_snapshot(data_provider_proxy, snapshot_tempdir.path(), writer)
                .await?;
            snapshot_tempdir.into_path()
        }
    };

    analyze_snapshot(config_files, data_directory, tags, exclude_tags, writer)
}

/// Analyze snapshot in the data_directory using config_files.
fn analyze_snapshot<W: Write>(
    config_files: Vec<PathBuf>,
    data_directory: PathBuf,
    tags: Vec<String>,
    exclude_tags: Vec<String>,
    writer: &mut W,
) -> Result<()> {
    let parse_result =
        config_from_files(&config_files, &ActionTagDirective::from_tags(tags, exclude_tags))
            .context("Unable to parse config files.")?;
    parse_result.validate().context("Unable to validate config files.")?;

    let diagnostic_data = diagnostics_from_directory(&data_directory)
        .context("Unable to process disagnostics data.")?;

    let action_results =
        analyze(&diagnostic_data, &parse_result).context("Unable to analyze data.")?;

    let results_formatter = ActionResultFormatter::new(&action_results);

    // TODO(fxbug.dev/95665): take format as cmdline argument.
    let output = results_formatter.to_text();

    writer.write_fmt(format_args!("{}\n", output)).context("Unable to write to destination.")?;
    Ok(())
}

/// Flattens the errors attached by context.
fn flatten_error(e: anyhow::Error) -> anyhow::Error {
    FfxError::Error(
        anyhow::anyhow!(
            "Triage encountered an error.{}",
            e.chain()
                .enumerate()
                .map(|(i, e)| format!("\n{:indent$}{}", "", e, indent = i))
                .collect::<Vec<String>>()
                .concat()
        ),
        -1,
    )
    .into()
}

#[cfg(test)]
mod tests {
    use super::*;
    use anyhow::Result;
    use fidl_fuchsia_feedback::{DataProviderProxy, DataProviderRequest, Snapshot};
    use lazy_static::lazy_static;
    use std::{collections::HashMap, fs, io::Write, path::Path};
    use tempfile::tempdir;

    macro_rules! test_file {
        (config $filename:expr) => {
            concat!("../../../../../../src/diagnostics/triage/test_data/config/", $filename)
        };
        (snapshot $filename:expr) => {
            concat!("../../../../../../src/diagnostics/triage/test_data/snapshot/", $filename)
        };
    }

    macro_rules! test_data {
        ($(configs: $($config: literal), + $(,)?)?
         $(snapshots: $($snapshot: literal), + $(,)?)?) => {
            {
                let mut m = HashMap::new();
                $($(m.insert($config, include_str!(test_file!(config $config)));)+)?
                $($(m.insert($snapshot, include_str!(test_file!(snapshot $snapshot)));)+)?
                m
            }
        }
    }

    lazy_static! {
        static ref TEST_DATA_MAP: HashMap<&'static str, &'static str> = test_data!{
            // Test config files
            configs:
                "annotation_tests.triage", "error_rate.triage", "log_tests.triage",
                "map_fold.triage","other.triage", "sample.triage",
                "sample_bundle.json", "sample_bundle_file_type_error.json",
                "sample_bundle_files_error.json", "sample_tags.triage",
            // Test snapshot files
            snapshots:
                "inspect.json", "annotations.json"
        };
    }

    fn setup_fake_data_provider_server() -> DataProviderProxy {
        setup_fake_data_provider_proxy(move |req| match req {
            DataProviderRequest::GetSnapshot { params, responder } => {
                let _channel = params.response_channel.unwrap();

                let snapshot = Snapshot { ..Snapshot::EMPTY };
                responder.send(snapshot).unwrap();
            }
            _ => unreachable!(),
        })
    }

    fn create_test_snapshot(snapshot_dir: &Path) {
        ["inspect.json", "annotations.json"].iter().for_each(|file_name| {
            let snapshot_file = snapshot_dir.join(file_name);
            let snapshot_data = TEST_DATA_MAP.get(file_name).expect("Unknown test snapshot data.");
            fs::write(&snapshot_file, snapshot_data).expect("Unable to write test snapshot data.");
        });
    }

    async fn run_triage_test<W: Write>(cmd: TriageCommand, writer: &mut W) {
        let data_provider_proxy = setup_fake_data_provider_server();
        let result = triage_impl(data_provider_proxy, cmd, writer).await.map_err(flatten_error);

        if let Err(e) = result {
            writer.write_fmt(format_args!("{}", e)).expect("Failed to write to destination.")
        }
    }

    macro_rules! triage_integration_test {
        (@internal
         $name: ident,
         $(configs: $($config: literal), + $(,)?)?
         $(tags: $($tag: literal), + $(,)?)?
         $(exclude_tags: $($exclude_tag: literal), + $(,)?)?
         substring: $substring: literal,
         $should_contain: expr
         ) => {
            // TODO(fxbug.dev/77647): use fuchsia::test
            #[fuchsia_async::run_singlethreaded(test)]
            async fn $name() -> Result<()> {
                let config = vec![$($($config),+)?];
                let config_tempdir = tempdir()?;

                let config_files: Result<Vec<String>> = config
                    .iter()
                    .map(|config| {
                        let config_data =
                            TEST_DATA_MAP.get(config).expect("Unknown test config data.");
                        let config_file_path = config_tempdir.path().join(config);
                        fs::write(&config_file_path, config_data)?;
                        Ok(config_file_path.to_string_lossy().into())
                    })
                    .collect();
                let config_files = config_files?;

                let snapshot_tempdir = tempdir()?;
                create_test_snapshot(snapshot_tempdir.path());

                let mut writer = Vec::new();

                run_triage_test(TriageCommand {
                    config: config_files,
                    data: Some(snapshot_tempdir.path().to_string_lossy().into()),
                    tags: vec![$($($tag.into()),+)?],
                    exclude_tags: vec![$($($exclude_tag.into()),+)?],
                },&mut writer).await;

                let output = String::from_utf8(writer).unwrap();

                pretty_assertions::assert_eq!(
                    output.contains($substring),
                    $should_contain,
                    "{} does not contain: {}", output, $substring);

                Ok(())
            }
        };
        ($name: ident,
         $(configs: $($config: literal), + $(,)?)?
         $(tags: $($tag: literal), + $(,)?)?
         $(exclude_tags: $($exclude_tag: literal), + $(,)?)?
         substring: $substring: literal
         ) => {
            triage_integration_test!(
                @internal
                $name,
                $(configs: $($config),+)?
                $(tags: $($tag),+)?
                $(exclude_tags: $($exclude_tag),+)?
                substring: $substring,
                true);
        };

        ($name: ident,
         $(configs: $($config: literal), + $(,)?)?
         $(tags: $($tag: literal), + $(,)?)?
         $(exclude_tags: $($exclude_tag: literal), + $(,)?)?
         substring: not_contains $substring: literal
         ) => {
            triage_integration_test!(
                @internal
                $name,
                $(configs: $($config),+)?
                $(tags: $($tag),+)?
                $(exclude_tags: $($exclude_tag),+)?
                substring: $substring,
                false);
        };
    }

    triage_integration_test!(
        successfully_read_correct_files,
        configs: "other.triage", "sample.triage",
        substring: not_contains "Couldn't"
    );

    triage_integration_test!(
        use_namespace_in_actions,
        configs: "other.triage", "sample.triage",
        substring: "[WARNING] yes on A!"
    );

    triage_integration_test!(
        use_namespace_in_metrics,
        configs: "other.triage", "sample.triage",
        substring: "[WARNING] Used some of disk"
    );

    triage_integration_test!(
        fail_on_missing_namespace,
        configs: "sample.triage",
        substring: "Bad namespace"
    );

    triage_integration_test!(
        include_tagged_actions,
        configs: "sample_tags.triage",
        tags: "foo",
        substring: "[WARNING] trigger foo tag"
    );

    triage_integration_test!(
        only_runs_included_actions,
        configs: "sample_tags.triage",
        tags: "not_included",
        substring :""
    );

    triage_integration_test!(
        included_tags_override_excludes,
        configs: "sample_tags.triage",
        tags: "foo",
        exclude_tags: "foo",
        substring :"[WARNING] trigger foo tag"
    );

    triage_integration_test!(
        exclude_actions_with_excluded_tags,
        configs: "sample_tags.triage",
        exclude_tags: "foo",
        substring: ""
    );

    triage_integration_test!(
        error_rate_with_moniker_payload,
        configs: "error_rate.triage",
        substring: "[WARNING] Error rate for app.cmx is too high"
    );

    triage_integration_test!(
        annotation_test,
        configs: "annotation_tests.triage",
        substring: "[WARNING] Running on a chromebook"
    );

    triage_integration_test!(
        annotation_test2,
        configs: "annotation_tests.triage",
        substring: not_contains "[WARNING] Not using a chromebook"
    );

    triage_integration_test!(
        map_fold_test,
        configs: "map_fold.triage",
        substring: "Everything worked as expected"
    );

    triage_integration_test!(log_tests, configs: "log_tests.triage", substring: "");

    triage_integration_test!(bundle_test, configs: "sample_bundle.json",substring :"gauge: 120");

    triage_integration_test!(
        bundle_files_error_test,
        configs: "sample_bundle_files_error.json",
        substring: "looks like a bundle, but key 'files' is not an object"
    );

    triage_integration_test!(
        bundle_file_type_error_test,
        configs: "sample_bundle_file_type_error.json",
        substring :"looks like a bundle, but key file2 must contain a string"
    );
}
