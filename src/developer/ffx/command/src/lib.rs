// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use analytics::{get_notice, opt_out_for_this_invocation};
use anyhow::{Context, Result};
use errors::{ffx_error, ResultExt};
use ffx_metrics::{add_ffx_launch_and_timing_events, init_metrics_svc};
use fuchsia_async::TimeoutExt;
use itertools::Itertools;
use std::fs::File;
use std::io::Write;
use std::str::FromStr;
use std::time::{Duration, Instant};

mod ffx;
mod tools;

pub use ffx::*;
pub use tools::*;

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

/// If given an error result, prints a user-meaningful interpretation of it
/// to the given output handle
pub async fn report_user_error(err: &anyhow::Error) -> anyhow::Result<()> {
    // Report BUG errors as crash events
    if err.ffx_error().is_none() {
        // TODO(66918): make configurable, and evaluate chosen time value.
        if let Err(e) = analytics::add_crash_event(&format!("{}", err), None)
            .on_timeout(Duration::from_secs(2), || {
                tracing::error!("analytics timed out reporting crash event");
                Ok(())
            })
            .await
        {
            tracing::error!("analytics failed to submit crash event: {}", e);
        }
    }
    Ok(())
}

pub async fn run<T: ToolSuite>() -> Result<()> {
    let cmd = ffx::FfxCommandLine::from_env()?;
    let app = cmd.parse::<T>();

    let context = app.load_context()?;

    ffx_config::init(&context).await?;

    match context.env_file_path() {
        Ok(path) => path,
        Err(e) => {
            eprintln!("ffx could not determine the environment configuration path: {}", e);
            eprintln!("Ensure that $HOME is set, or pass the --env option to specify an environment configuration path");
            return Ok(());
        }
    };

    let tools = T::from_env(&app, &context)?;

    let args_ref = Vec::from_iter(app.subcommand.iter().map(|arg| &**arg));
    let tool = tools.from_args(&cmd, &args_ref);

    let log_to_stdio = tool.as_ref().map(|tool| tool.forces_stdout_log()).unwrap_or(false);
    ffx_config::logging::init(log_to_stdio || app.verbose, !log_to_stdio).await?;
    tracing::info!("starting command: {:?}", Vec::from_iter(cmd.all_iter()));

    // Since this is invoking the config, this must be run _after_ ffx_config::init.
    let log_level = app.log_level().await?;
    let _:  simplelog::LevelFilter = simplelog::LevelFilter::from_str(log_level.as_str()).with_context(||
        ffx_error!("'{}' is not a valid log level. Supported log levels are 'Off', 'Error', 'Warn', 'Info', 'Debug', and 'Trace'", log_level))?;

    let analytics_disabled = ffx_config::get("ffx.analytics.disabled").await.unwrap_or(false);
    let ffx_invoker = ffx_config::get("fuchsia.analytics.ffx_invoker").await.unwrap_or(None);

    init_metrics_svc(context.build_info(), ffx_invoker).await; // one time call to initialize app analytics
    if analytics_disabled {
        opt_out_for_this_invocation().await?
    }

    if let Some(note) = get_notice().await {
        eprintln!("{}", note);
    }

    let analytics_start = Instant::now();

    let command_start = Instant::now();

    let stamp = stamp_file(&app.stamp)?;
    let res = if let Some(tool) = tool {
        tool.run().await
    } else {
        Err(ffx_error!(
            "No configured tool suites were able to handle the command line: {}",
            cmd.all_iter().join(" ")
        )
        .into())
    };

    let command_done = Instant::now();
    tracing::info!("Command completed. Success: {}", res.is_ok());
    let command_duration = (command_done - command_start).as_secs_f32();
    let timing_in_millis = (command_done - command_start).as_millis().to_string();

    let analytics_task = fuchsia_async::Task::local(async move {
        let sanitized_args = cmd.redact_arg_values();
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
