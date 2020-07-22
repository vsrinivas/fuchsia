// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fuchsia_examples_inspect::{FizzBuzzRequest, FizzBuzzRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::{self as inspect, component, HistogramProperty, NumericProperty},
    fuchsia_syslog::{self as syslog, macros::*},
    fuchsia_zircon::{self as zx},
    futures::{StreamExt, TryStreamExt},
    std::sync::Arc,
};

struct FizzBuzzServerMetrics {
    incoming_connection_count: inspect::UintProperty,
    closed_connection_count: inspect::UintProperty,
    request_count: inspect::UintProperty,
    request_time_histogram: inspect::UintExponentialHistogramProperty,
}

impl FizzBuzzServerMetrics {
    fn new() -> Self {
        let node = component::inspector().root().create_child("fizzbuzz_service");
        let metrics = Self {
            incoming_connection_count: node.create_uint("incoming_connection_count", 0),
            closed_connection_count: node.create_uint("closed_connection_count", 0),
            request_count: node.create_uint("request_count", 0),
            request_time_histogram: node.create_uint_exponential_histogram(
                "request_time_histogram_us",
                inspect::ExponentialHistogramParams {
                    floor: 1,
                    initial_step: 1,
                    step_multiplier: 2,
                    buckets: 16,
                },
            ),
        };
        component::inspector().root().record(node);
        metrics
    }
}

struct FizzBuzzServer {
    metrics: Arc<FizzBuzzServerMetrics>,
}

impl FizzBuzzServer {
    fn new(metrics: Arc<FizzBuzzServerMetrics>) -> Self {
        Self { metrics }
    }

    fn spawn(self, stream: FizzBuzzRequestStream) {
        fasync::Task::local(async move {
            self.metrics.incoming_connection_count.add(1);
            self.handle_request_stream(stream).await.unwrap_or_else(|e| {
                fx_log_err!("Error handling fizzbuzz request stream: {:?}", e);
            });
            self.metrics.closed_connection_count.add(1);
        })
        .detach();
    }

    async fn handle_request_stream(&self, mut stream: FizzBuzzRequestStream) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await.context("serve fizzbuzz")? {
            let FizzBuzzRequest::Execute { count, responder } = request;
            self.metrics.request_count.add(1);
            let start_time = zx::Time::get(zx::ClockId::Monotonic);
            responder.send(&fizzbuzz(count)).context("send execute response")?;
            let stop_time = zx::Time::get(zx::ClockId::Monotonic);
            let time_micros = (stop_time - start_time).into_micros() as u64;
            self.metrics.request_time_histogram.insert(time_micros);
        }
        Ok(())
    }
}

fn fizzbuzz(n: u32) -> String {
    (1..=n)
        .into_iter()
        .map(|i| match (i % 3, i % 5) {
            (0, 0) => "FizzBuzz".to_string(),
            (0, _) => "Fizz".to_string(),
            (_, 0) => "Buzz".to_string(),
            (_, _) => format!("{}", i).to_string(),
        })
        .collect::<Vec<_>>()
        .join(" ")
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["inspect_rust_codelab", "fizzbuzz"])?;
    let mut fs = ServiceFs::new();

    fx_log_info!("starting up...");

    let metrics = Arc::new(FizzBuzzServerMetrics::new());

    fs.dir("svc")
        .add_fidl_service(move |stream| FizzBuzzServer::new(metrics.clone()).spawn(stream));

    component::inspector().serve(&mut fs)?;

    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;
    Ok(())
}
