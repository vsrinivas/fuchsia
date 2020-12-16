// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    async_std::future::timeout,
    chrono::{Local, Offset, TimeZone},
    ffx_core::{build_info, ffx_plugin},
    ffx_version_args::VersionCommand,
    fidl_fuchsia_developer_bridge::{self as bridge, VersionInfo},
    std::fmt::Display,
    std::io::Write,
    std::time::Duration,
};

const UNKNOWN_BUILD_HASH: &str = "(unknown)";
const DEFAULT_DAEMON_TIMEOUT_MS: u64 = 1500;

fn format_version_info<O: Offset + Display>(
    header: &str,
    info: &VersionInfo,
    verbose: bool,
    tz: &impl TimeZone<Offset = O>,
) -> String {
    let build_version = info.build_version.clone().unwrap_or("(unknown build version)".to_string());
    if !verbose {
        return build_version;
    }

    let hash = info.commit_hash.clone().unwrap_or(UNKNOWN_BUILD_HASH.to_string());
    let timestamp_str = match info.commit_timestamp {
        Some(t) => tz.timestamp(t as i64, 0).to_rfc2822(),
        None => String::from("(unknown commit time)"),
    };

    return format!(
        "\
{}:
  build-version: {}
  commit-hash: {}
  commit-time: {}",
        header, build_version, hash, timestamp_str
    );
}

#[ffx_plugin()]
pub async fn version(daemon_proxy: bridge::DaemonProxy, cmd: VersionCommand) -> Result<()> {
    version_cmd(&build_info(), cmd, daemon_proxy, std::io::stdout(), Local).await
}

pub async fn version_cmd<W: Write, O: Offset + Display>(
    version_info: &VersionInfo,
    cmd: VersionCommand,
    proxy: bridge::DaemonProxy,
    mut w: W,
    tz: impl TimeZone<Offset = O>,
) -> Result<()> {
    writeln!(w, "{}", format_version_info("ffx", version_info, cmd.verbose, &tz))?;

    if cmd.verbose {
        let daemon_version_info =
            match timeout(Duration::from_millis(DEFAULT_DAEMON_TIMEOUT_MS), proxy.get_version_info())
                .await
            {
                Ok(Ok(v)) => v,
                Err(_) => {
                    writeln!(w, "Timed out trying to get daemon version info")?;
                    return Ok(());
                }
                Ok(Err(e)) => {
                    writeln!(w, "Failed to get daemon version info:\n{}", e)?;
                    return Ok(());
                }
            };

        writeln!(w, "\n{}", format_version_info("daemon", &daemon_version_info, true, &tz))?;
    }
    Ok(())
}

#[cfg(test)]
mod test {
    use {
        super::*,
        chrono::Utc,
        fidl_fuchsia_developer_bridge::DaemonRequest,
        futures::TryStreamExt,
        futures::{
            channel::oneshot::{self, Receiver},
            future::Shared,
            FutureExt,
        },
        std::io::BufWriter,
    };

    const FAKE_DAEMON_HASH: &str = "fake daemon fake";
    const FAKE_FRONTEND_HASH: &str = "fake frontend fake";
    const FAKE_DAEMON_BUILD_VERSION: &str = "fake daemon build";
    const FAKE_FRONTEND_BUILD_VERSION: &str = "fake frontend build";
    const TIMESTAMP: u64 = 1604080617;
    const TIMESTAMP_STR: &str = "Fri, 30 Oct 2020 17:56:57 +0000";

    fn daemon_info() -> VersionInfo {
        VersionInfo {
            commit_hash: Some(FAKE_DAEMON_HASH.to_string()),
            commit_timestamp: Some(TIMESTAMP),
            build_version: Some(FAKE_DAEMON_BUILD_VERSION.to_string()),
            ..VersionInfo::EMPTY
        }
    }

    fn frontend_info() -> VersionInfo {
        VersionInfo {
            commit_hash: Some(FAKE_FRONTEND_HASH.to_string()),
            commit_timestamp: Some(TIMESTAMP),
            build_version: Some(FAKE_FRONTEND_BUILD_VERSION.to_string()),
            ..VersionInfo::EMPTY
        }
    }

    fn setup_fake_daemon_server(succeed: bool, info: VersionInfo) -> bridge::DaemonProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<bridge::DaemonMarker>().unwrap();
        fuchsia_async::Task::spawn(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    DaemonRequest::GetVersionInfo { responder } => {
                        if succeed {
                            responder.send(info).unwrap();
                        } else {
                            return;
                        }
                    }
                    _ => assert!(false),
                }
                // We should only get one request per stream. We want subsequent calls to fail if more are
                // made.
                break;
            }
        })
        .detach();

        proxy
    }

    fn setup_hanging_daemon_server(waiter: Shared<Receiver<()>>) -> bridge::DaemonProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<bridge::DaemonMarker>().unwrap();
        fuchsia_async::Task::spawn(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    DaemonRequest::GetVersionInfo { responder: _ } => {
                        waiter.await.unwrap();
                    }
                    _ => assert!(false),
                }
                // We should only get one request per stream. We want subsequent calls to fail if more are
                // made.
                break;
            }
        })
        .detach();

        proxy
    }

    async fn run_version_test(
        version_info: VersionInfo,
        daemon_proxy: bridge::DaemonProxy,
        cmd: VersionCommand,
    ) -> String {
        let mut output = String::new();
        let writer = unsafe { BufWriter::new(output.as_mut_vec()) };
        let result = version_cmd(&version_info, cmd, daemon_proxy, writer, Utc).await.unwrap();
        assert_eq!(result, ());
        output
    }

    fn assert_lines(output: String, expected_lines: Vec<String>) {
        let output_lines: Vec<&str> = output.lines().collect();

        if output_lines.len() != expected_lines.len() {
            println!("FULL OUTPUT: \n{}\n", output);
            assert!(
                false,
                format!("{} lines =/= {} lines", output_lines.len(), expected_lines.len())
            );
        }

        for (out_line, expected_line) in output_lines.iter().zip(expected_lines) {
            if !expected_line.is_empty() {
                if !out_line.contains(&expected_line) {
                    assert!(false, format!("'{}' does not contain '{}'", out_line, expected_line));
                }
            }
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_success() -> Result<()> {
        let proxy = setup_fake_daemon_server(false, VersionInfo::EMPTY);
        let output =
            run_version_test(frontend_info(), proxy, VersionCommand { verbose: false }).await;
        assert_eq!(output, format!("{}\n", FAKE_FRONTEND_BUILD_VERSION));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_empty_version_info_not_verbose() -> Result<()> {
        let proxy = setup_fake_daemon_server(false, VersionInfo::EMPTY);
        let output =
            run_version_test(VersionInfo::EMPTY, proxy, VersionCommand { verbose: false }).await;
        assert_eq!(output, "(unknown build version)\n");
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_success_verbose() -> Result<()> {
        let proxy = setup_fake_daemon_server(true, daemon_info());
        let output =
            run_version_test(frontend_info(), proxy, VersionCommand { verbose: true }).await;
        assert_lines(
            output,
            vec![
                "ffx:".to_string(),
                format!("  build-version: {}", FAKE_FRONTEND_BUILD_VERSION),
                format!("  commit-hash: {}", FAKE_FRONTEND_HASH),
                format!("  commit-time: {}", TIMESTAMP_STR),
                String::default(),
                "daemon:".to_string(),
                format!("  build-version: {}", FAKE_DAEMON_BUILD_VERSION),
                format!("  commit-hash: {}", FAKE_DAEMON_HASH),
                format!("  commit-time: {}", TIMESTAMP_STR),
            ],
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_daemon_fails() -> Result<()> {
        let proxy = setup_fake_daemon_server(false, daemon_info());
        let output =
            run_version_test(frontend_info(), proxy, VersionCommand { verbose: true }).await;

        assert_lines(
            output,
            vec![
                "ffx:".to_string(),
                format!("  build-version: {}", FAKE_FRONTEND_BUILD_VERSION),
                format!("  commit-hash: {}", FAKE_FRONTEND_HASH),
                format!("  commit-time: {}", TIMESTAMP_STR),
                "Failed to get daemon version info".to_string(),
                "PEER_CLOSED".to_string(),
            ],
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_daemon_hangs() -> Result<()> {
        let (tx, rx) = oneshot::channel::<()>();
        let proxy = setup_hanging_daemon_server(rx.shared());
        let output =
            run_version_test(frontend_info(), proxy, VersionCommand { verbose: true }).await;
        tx.send(()).unwrap();

        assert_lines(
            output,
            vec![
                "ffx:".to_string(),
                format!("  build-version: {}", FAKE_FRONTEND_BUILD_VERSION),
                format!("  commit-hash: {}", FAKE_FRONTEND_HASH),
                format!("  commit-time: {}", TIMESTAMP_STR),
                "Timed out".to_string(),
            ],
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_frontend_empty_and_daemon_returns_none() -> Result<()> {
        let proxy = setup_fake_daemon_server(true, VersionInfo::EMPTY);
        let output =
            run_version_test(VersionInfo::EMPTY, proxy, VersionCommand { verbose: true }).await;

        assert_lines(
            output,
            vec![
                "ffx:".to_string(),
                "  build-version: (unknown build version)".to_string(),
                "  commit-hash: (unknown)".to_string(),
                "  commit-time: (unknown commit time)".to_string(),
                String::default(),
                "daemon:".to_string(),
                "  build-version: (unknown build version)".to_string(),
                "  commit-hash: (unknown)".to_string(),
                "  commit-time: (unknown commit time)".to_string(),
            ],
        );
        Ok(())
    }
}
