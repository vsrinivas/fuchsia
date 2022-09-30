// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Result},
    ffx_core::ffx_plugin,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_kernel as fstats,
    fuchsia_zircon_status::Status,
};

#[ffx_plugin()]
pub async fn load(
    rcs_proxy: rc::RemoteControlProxy,
    cmd: ffx_cpu_load_args::CpuLoadCommand,
) -> Result<()> {
    let (stats_proxy, stats_server_end) = fidl::endpoints::create_proxy().unwrap();
    if let Err(i) = rcs_proxy.kernel_stats(stats_server_end).await? {
        bail!("Could not open fuchsia.kernel.Stats: {}", Status::from_raw(i));
    }

    load_impl(stats_proxy, cmd, &mut std::io::stdout()).await
}

pub async fn load_impl<W: std::io::Write>(
    stats_proxy: fstats::StatsProxy,
    cmd: ffx_cpu_load_args::CpuLoadCommand,
    writer: &mut W,
) -> Result<()> {
    if cmd.duration.is_zero() {
        bail!("Duration must be > 0");
    }

    let cpu_loads = stats_proxy.get_cpu_load(cmd.duration.as_nanos() as i64).await?;
    print_loads(cpu_loads, writer)?;

    Ok(())
}

/// Prints a vector of CPU load values in the following format:
///     CPU 0: 0.66%
///     CPU 1: 1.56%
///     CPU 2: 0.83%
///     CPU 3: 0.71%
///     Total: 3.76%
fn print_loads<W: std::io::Write>(cpu_load_pcts: Vec<f32>, writer: &mut W) -> Result<()> {
    for (i, load_pct) in cpu_load_pcts.iter().enumerate() {
        writeln!(writer, "CPU {}: {:.2}%", i, load_pct)?;
    }

    writeln!(writer, "Total: {:.2}%", cpu_load_pcts.iter().sum::<f32>())?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use {super::*, futures::channel::mpsc, std::time::Duration};

    /// Tests that invalid arguments are rejected.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_invalid_args() {
        let (proxy, _) = fidl::endpoints::create_proxy::<fstats::StatsMarker>().unwrap();
        assert!(load_impl(
            proxy,
            ffx_cpu_load_args::CpuLoadCommand { duration: Duration::from_secs(0) },
            &mut std::io::stdout()
        )
        .await
        .is_err());
    }

    /// Tests that the input parameter for duration is correctly converted between seconds and
    /// nanoseconds. The test uses a duration parameter of one second.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_cpu_load_duration() {
        let (duration_request_sender, mut duration_request_receiver) = mpsc::channel(1);

        let proxy = fidl::endpoints::spawn_stream_handler(move |req| {
            let mut duration_request_sender = duration_request_sender.clone();
            async move {
                match req {
                    fstats::StatsRequest::GetCpuLoad { duration, responder } => {
                        duration_request_sender.try_send(duration).unwrap();
                        let _ = responder.send(&vec![]); // returned values don't matter for this test
                    }
                    request => panic!("Unexpected request: {:?}", request),
                }
            }
        })
        .unwrap();

        let _ = load_impl(
            proxy,
            ffx_cpu_load_args::CpuLoadCommand { duration: Duration::from_secs(1) },
            &mut std::io::stdout(),
        )
        .await
        .unwrap();

        match duration_request_receiver.try_next() {
            Ok(Some(duration_request)) => {
                assert_eq!(duration_request as u128, Duration::from_secs(1).as_nanos())
            }
            e => panic!("Failed to get duration_request: {:?}", e),
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_cpu_load_output() {
        let proxy = fidl::endpoints::spawn_stream_handler(move |req| async move {
            let data = vec![0.66f32, 1.56, 0.83, 0.71];
            match req {
                fstats::StatsRequest::GetCpuLoad { responder, .. } => {
                    let _ = responder.send(&data.clone());
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        })
        .unwrap();

        let mut writer = Vec::new();
        let _ = load_impl(
            proxy,
            ffx_cpu_load_args::CpuLoadCommand { duration: Duration::from_secs(1) },
            &mut writer,
        )
        .await
        .unwrap();

        let output = String::from_utf8(writer).expect("valid utf8 output");
        assert_eq!(
            output,
            "\
CPU 0: 0.66%
CPU 1: 1.56%
CPU 2: 0.83%
CPU 3: 0.71%
Total: 3.76%
",
        );
    }
}
