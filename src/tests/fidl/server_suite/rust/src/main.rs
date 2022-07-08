// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl::endpoints::{create_endpoints, ServerEnd},
    fidl_fidl_serversuite::{
        ReporterProxy, RunnerRequest, RunnerRequestStream, TargetMarker, TargetRequest, Test,
    },
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
};

async fn run_target_server(
    server_end: ServerEnd<TargetMarker>,
    reporter_proxy: &ReporterProxy,
) -> Result<(), Error> {
    server_end
        .into_stream()?
        .map(|result| result.context("failed request"))
        .try_for_each(|request| async move {
            match request {
                TargetRequest::OneWayNoPayload { control_handle: _ } => {
                    println!("OneWayNoPayload");
                    reporter_proxy
                        .received_one_way_no_payload()
                        .expect("calling received_one_way_no_payload failed");
                }
                TargetRequest::TwoWayNoPayload { responder } => {
                    println!("TwoWayNoPayload");
                    responder.send().expect("failed to send two way payload response");
                }
            }
            Ok(())
        })
        .await
}

async fn run_runner_server(stream: RunnerRequestStream) -> Result<(), Error> {
    stream
        .map(|result| result.context("failed request"))
        .try_for_each(|request| async move {
            match request {
                RunnerRequest::IsTestEnabled { test, responder } => {
                    let enabled = match test {
                        Test::OneWayWithNonZeroTxid | Test::TwoWayNoPayloadWithZeroTxid => false,
                        _ => true,
                    };
                    responder.send(enabled)?;
                }
                RunnerRequest::Start { reporter, responder } => {
                    println!("Runner.Start() called");
                    let reporter_proxy: &ReporterProxy = &reporter.into_proxy()?;
                    let (client_end, server_end) = create_endpoints::<TargetMarker>()?;
                    responder.send(client_end).expect("sending response failed");
                    run_target_server(server_end, reporter_proxy)
                        .await
                        .unwrap_or_else(|e| println!("target server failed {:?}", e));
                }
                RunnerRequest::CheckAlive { responder } => {
                    responder.send().expect("sending response failed");
                }
            }
            Ok(())
        })
        .await
}

enum IncomingService {
    Runner(RunnerRequestStream),
}

#[fuchsia::main]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(IncomingService::Runner);
    fs.take_and_serve_directory_handle().expect("serving directory failed");

    println!("Listening for incoming connections...");
    const MAX_CONCURRENT: usize = 10_000;
    fs.for_each_concurrent(MAX_CONCURRENT, |IncomingService::Runner(stream)| {
        run_runner_server(stream).unwrap_or_else(|e| panic!("runner server failed {:?}", e))
    })
    .await;

    Ok(())
}
