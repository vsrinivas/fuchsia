// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{Context, Error},
    argh::FromArgs,
    fidl_fuchsia_hardware_power_statecontrol as reboot, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::{self as inspect, health::Reporter},
    futures::{StreamExt, TryStreamExt},
    sampler_config as config,
    tracing::{info, warn},
};

mod diagnostics;
mod executor;

/// The name of the subcommand and the logs-tag.
pub const PROGRAM_NAME: &str = "sampler";

/// args used to configure sampler.
#[derive(Debug, Default, FromArgs, PartialEq)]
#[argh(subcommand, name = "sampler")]
pub struct Args {
    /// required minimal sample rate.
    #[argh(option)]
    minimum_sample_rate_sec: i64,
}

pub async fn main(opt: Args) -> Result<(), Error> {
    // Serve inspect.
    let mut service_fs = ServiceFs::new();
    service_fs.take_and_serve_directory_handle()?;
    inspect_runtime::serve(inspect::component::inspector(), &mut service_fs)?;
    fasync::Task::spawn(async move {
        service_fs.collect::<()>().await;
    })
    .detach();

    // Starting service.
    inspect::component::health().set_starting_up();

    match config::SamplerConfig::from_directory(opt.minimum_sample_rate_sec, "/config/data/metrics")
    {
        Ok(sampler_config) => {
            // Create endpoint for the reboot watcher register.
            let (reboot_watcher_client, reboot_watcher_request_stream) =
                fidl::endpoints::create_request_stream::<reboot::RebootMethodsWatcherMarker>()?;

            {
                // Let the transient connection fall out of scope once we've passed the client
                // end to our callback server.
                let reboot_watcher_register =
                    connect_to_protocol::<reboot::RebootMethodsWatcherRegisterMarker>()
                        .context("Connect to Reboot watcher register")?;

                reboot_watcher_register
                    .register(reboot_watcher_client)
                    .context("Providing the reboot register with callback channel.")?;
            }

            let sampler_executor = executor::SamplerExecutor::new(sampler_config).await?;

            // Trigger the project samplers and returns a TaskCancellation struct used to trigger
            // reboot shutdown of sampler.
            let task_canceller = sampler_executor.execute();

            inspect::component::health().set_ok();
            reboot_watcher(reboot_watcher_request_stream, task_canceller).await;
            Ok(())
        }
        Err(e) => {
            warn!("Failed to parse sampler configurations from /config/data/metrics: {:?}", e);
            Ok(())
        }
    }
}

async fn reboot_watcher(
    mut stream: reboot::RebootMethodsWatcherRequestStream,
    task_canceller: executor::TaskCancellation,
) {
    if let Some(reboot::RebootMethodsWatcherRequest::OnReboot { reason: _, responder }) =
        stream.try_next().await.unwrap_or_else(|err| {
            // If the channel closed for some reason, we can just let Sampler keep running
            // until component manager kills it.
            warn!("Reboot callback channel closed: {:?}", err);
            None
        })
    {
        task_canceller.perform_reboot_cleanup().await;

        // acknowledge reboot notification to unblock before timeout.
        responder
            .send()
            .unwrap_or_else(|err| warn!("Acking the reboot register failed: {:?}", err));
    } else {
        // The reboot watcher channel somehow died. There's no reason to
        // clean ourselves up early, might as well just run until the component
        // manager tells us to stop.
        task_canceller.run_without_cancellation().await;
    }

    info!("Sampler has been halted due to reboot. Goodbye.");
}
