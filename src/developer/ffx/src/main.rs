// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use analytics::{add_crash_event, get_notice, opt_out_for_this_invocation};
use anyhow::{Context as _, Result};
use buildid;
use errors::{ffx_error, ResultExt as _};
use ffx_config::get_log_dirs;
use ffx_core::Injector;
use ffx_daemon_proxy::Injection;
use ffx_lib_args::{forces_stdout_log, from_env, is_schema, redact_arg_values, Ffx};
use ffx_metrics::{add_ffx_launch_and_timing_events, init_metrics_svc};
use fuchsia_async::TimeoutExt;
use std::fs::File;
use std::io::Write;
use std::str::FromStr;
use std::time::{Duration, Instant};

async fn report_log_hint(writer: &mut dyn std::io::Write) {
    let msg = if let Ok(log_dirs) = get_log_dirs().await {
        if log_dirs.len() == 1 {
            format!(
                "More information may be available in ffx host logs in directory:\n    {}",
                log_dirs[0]
            )
        } else {
            format!(
                "More information may be available in ffx host logs in directories:\n    {}",
                log_dirs.join("\n    ")
            )
        }
    } else {
        "More information may be available in ffx host logs, but ffx failed to retrieve configured log file locations".to_string()
    };
    if writeln!(writer, "{}", msg).is_err() {
        println!("{}", msg);
    }
}

fn stamp_file(stamp: &Option<String>) -> Result<Option<File>> {
    if let Some(stamp) = stamp {
        Ok(Some(File::create(stamp)?))
    } else {
        Ok(None)
    }
}

fn write_exit_code<T, W: Write>(res: &Result<T>, out: &mut W) -> Result<()> {
    write!(out, "{}\n", res.exit_code())?;
    Ok(())
}

async fn run() -> Result<()> {
    let app: Ffx = from_env();

    // todo(fxb/108692) we should get this in the environment context instead and leave the global
    // hoist() unset for ffx but I'm leaving the last couple uses of it in place for the sake of
    // avoiding complicated merge conflicts with isolation. Once we're ready for that, this should be
    // `let Hoist = hoist::Hoist::new()...`
    let hoist = hoist::init_hoist().context("initializing hoist")?;

    let context = app.load_context(&buildid::get_build_id()?)?;

    ffx_config::init(&context).await?;

    match context.env_file_path() {
        Ok(path) => path,
        Err(e) => {
            eprintln!("ffx could not determine the environment configuration path: {}", e);
            eprintln!("Ensure that $HOME is set, or pass the --env option to specify an environment configuration path");
            return Ok(());
        }
    };

    let log_to_stdio = forces_stdout_log(&app.subcommand);
    ffx_config::logging::init(log_to_stdio || app.verbose, !log_to_stdio).await?;

    tracing::info!("starting command: {:?}", std::env::args().collect::<Vec<String>>());

    // Since this is invoking the config, this must be run _after_ ffx_config::init.
    let log_level = app.log_level().await?;
    let _:  simplelog::LevelFilter = simplelog::LevelFilter::from_str(log_level.as_str()).with_context(||
        ffx_error!("'{}' is not a valid log level. Supported log levels are 'Off', 'Error', 'Warn', 'Info', 'Debug', and 'Trace'", log_level))?;

    let analytics_disabled = ffx_config::get("ffx.analytics.disabled").await.unwrap_or(false);
    let ffx_invoker = ffx_config::get("fuchsia.analytics.ffx_invoker").await.unwrap_or(None);
    let injection = Injection::new(hoist.clone(), app.machine, app.target().await?);
    init_metrics_svc(injection.build_info().await?, ffx_invoker).await; // one time call to initialize app analytics
    if analytics_disabled {
        opt_out_for_this_invocation().await?
    }

    if let Some(note) = get_notice().await {
        eprintln!("{}", note);
    }

    let analytics_start = Instant::now();

    let command_start = Instant::now();

    let stamp = stamp_file(&app.stamp)?;
    let res = if is_schema(&app.subcommand) {
        ffx_lib_suite::ffx_plugin_writer_all_output(0);
        Ok(())
    } else if app.machine.is_some() && !ffx_lib_suite::ffx_plugin_is_machine_supported(&app) {
        Err(anyhow::Error::new(ffx_error!("The machine flag is not supported for this subcommand")))
    } else {
        ffx_lib_suite::ffx_plugin_impl(&injection, app).await
    };

    let command_done = Instant::now();
    tracing::info!("Command completed. Success: {}", res.is_ok());
    let command_duration = (command_done - command_start).as_secs_f32();
    let timing_in_millis = (command_done - command_start).as_millis().to_string();

    let analytics_task = fuchsia_async::Task::local(async move {
        let sanitized_args = redact_arg_values();
        if let Err(e) =
            add_ffx_launch_and_timing_events(&context, sanitized_args, timing_in_millis).await
        {
            tracing::error!("metrics submission failed: {}", e);
        }
        Instant::now()
    });

    let analytics_done = analytics_task
        // TODO(66918): make configurable, and evaluate chosen time value.
        .on_timeout(Duration::from_secs(2), || {
            tracing::error!("metrics submission timed out");
            // Metrics timeouts should not impact user flows.
            Instant::now()
        })
        .await;

    tracing::info!(
        "Run finished. success: {}, command time: {}, analytics time: {}",
        res.is_ok(),
        &command_duration,
        (analytics_done - analytics_start).as_secs_f32()
    );

    // Write to our stamp file if it was requested
    if let Some(mut stamp) = stamp {
        write_exit_code(&res, &mut stamp)?;
        stamp.sync_all()?;
    }

    res
}

#[fuchsia_async::run_singlethreaded]
async fn main() {
    let result = run().await;
    let mut stderr = std::io::stderr();

    // unwrap because if stderr is not writable, the program should try to exit right away.
    errors::write_result(&result, &mut stderr).unwrap();
    // Report BUG errors as crash events
    if result.is_err() && result.ffx_error().is_none() {
        let err_msg = format!("{}", result.as_ref().unwrap_err());
        // TODO(66918): make configurable, and evaluate chosen time value.
        if let Err(e) = add_crash_event(&err_msg, None)
            .on_timeout(Duration::from_secs(2), || {
                tracing::error!("analytics timed out reporting crash event");
                Ok(())
            })
            .await
        {
            tracing::error!("analytics failed to submit crash event: {}", e);
        }
        report_log_hint(&mut stderr).await;
    }

    std::process::exit(result.exit_code());
}

#[cfg(test)]
mod test {
    use super::*;
    use std::io::BufWriter;
    use tempfile;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_stamp_file_creation() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("stamp").into_os_string().into_string().ok();
        let stamp = stamp_file(&path);

        assert!(stamp.unwrap().is_some());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_stamp_file_no_create() {
        let no_stamp = stamp_file(&None);
        assert!(no_stamp.unwrap().is_none());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_write_exit_code() {
        let mut out = BufWriter::new(Vec::new());
        write_exit_code(&Ok(0), &mut out).unwrap();
        assert_eq!(String::from_utf8(out.into_inner().unwrap()).unwrap(), "0\n");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_write_exit_code_on_failure() {
        let mut out = BufWriter::new(Vec::new());
        write_exit_code(&Result::<()>::Err(anyhow::anyhow!("fail")), &mut out).unwrap();
        assert_eq!(String::from_utf8(out.into_inner().unwrap()).unwrap(), "1\n")
    }
}
