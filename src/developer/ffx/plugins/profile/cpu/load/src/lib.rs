// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Result},
    ffx_core::ffx_plugin,
    fidl_fuchsia_kernel as fstats,
};

#[ffx_plugin(fstats::StatsProxy = "core/appmgr:out:fuchsia.kernel.Stats")]
pub async fn load(
    stats_proxy: fstats::StatsProxy,
    cmd: ffx_cpu_load_args::CpuLoadCommand,
) -> Result<()> {
    if cmd.duration.is_zero() {
        bail!("Duration must be > 0");
    }

    let cpu_loads = stats_proxy.get_cpu_load(cmd.duration.as_nanos() as i64).await?;
    print_loads(cpu_loads);

    Ok(())
}

/// Prints a vector of CPU load values in the following format:
///     CPU 0: 0.66%
///     CPU 1: 1.56%
///     CPU 2: 0.83%
///     CPU 3: 0.71%
///     Total: 3.76%
fn print_loads(cpu_load_pcts: Vec<f32>) {
    for (i, load_pct) in cpu_load_pcts.iter().enumerate() {
        println!("CPU {}: {:.2}%", i, load_pct);
    }

    println!("Total: {:.2}%", cpu_load_pcts.iter().sum::<f32>());
}

#[cfg(test)]
mod tests {
    use {super::*, futures::channel::mpsc, std::time::Duration};

    /// Tests that invalid arguments are rejected.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_invalid_args() {
        assert!(load(
            setup_fake_stats_proxy(|_| {}),
            ffx_cpu_load_args::CpuLoadCommand { duration: Duration::from_secs(0) }
        )
        .await
        .is_err());
    }

    /// Tests that the input parameter for duration is correctly converted between seconds and
    /// nanoseconds. The test uses a duration parameter of one second.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_cpu_load_duration() {
        let (mut duration_request_sender, mut duration_request_receiver) = mpsc::channel(1);

        // Gets a request from the stream and sends the duration parameter back via the mpsc channel
        let proxy = setup_fake_stats_proxy(move |req| {
            match req {
                fstats::StatsRequest::GetCpuLoad { duration, responder } => {
                    duration_request_sender.try_send(duration).unwrap();
                    let _ = responder.send(&vec![]); // returned values don't matter for this test
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        });

        let _ = load(proxy, ffx_cpu_load_args::CpuLoadCommand { duration: Duration::from_secs(1) })
            .await
            .unwrap();

        match duration_request_receiver.try_next() {
            Ok(Some(duration_request)) => {
                assert_eq!(duration_request as u128, Duration::from_secs(1).as_nanos())
            }
            e => panic!("Failed to get duration_request: {:?}", e),
        }
    }
}
