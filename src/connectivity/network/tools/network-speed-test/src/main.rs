// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod opts;

use {
    crate::opts::Opt,
    anyhow::{format_err, Context, Error},
    fidl_fuchsia_net_http::{self as http, LoaderProxy},
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon as zx,
    futures::io::{copy, AllowStdIo},
    serde::Serialize,
    std::process,
    structopt::StructOpt,
};

fn main() -> Result<(), Error> {
    let () = fuchsia_syslog::init().context("cannot init logger")?;

    let opt = Opt::from_args();
    log::info!("{:?}", opt);

    // create objects to hold test objects and results
    let mut test_results = TestResults::default();

    let mut test_pass = true;
    if let Err(e) = run_test(opt, &mut test_results) {
        test_pass = false;
        let mut message = Vec::new();
        for error in e.chain() {
            message.push(format!("Error: {}", error));
        }
        test_results.error_message = message.join("\n");
    }

    report_results(&mut test_results);

    if !test_pass {
        process::exit(1);
    }

    Ok(())
}

fn run_test(opt: Opt, test_results: &mut TestResults) -> Result<(), Error> {
    let mut exec = fasync::Executor::new().context("error creating event loop")?;

    let http_svc = connect_to_service::<http::LoaderMarker>()
        .context("Unable to connect to fuchsia.net.http.Loader")?;
    test_results.connect_to_http_service = true;

    let url_request = create_request(opt.target_url);
    let ind_download = exec
        .run_singlethreaded(fetch_and_discard_url(http_svc, url_request))
        .context("Failed to run fetch_and_discard_url")?;
    test_results.base_data_transfer = true;

    // TODO (NET-1665): aggregate info from individual results (when we do multiple requests)
    test_results.overall_avg_goodput_mbps = ind_download.goodput_mbps;
    test_results.total_bytes = ind_download.bytes;
    test_results.total_nanos = ind_download.nanos;

    Ok(())
}

// Object to hold overall test status
#[derive(Default, Serialize)]
struct TestResults {
    connect_to_http_service: bool,
    base_data_transfer: bool,
    overall_avg_goodput_mbps: f64,
    total_bytes: u64,
    total_nanos: u64,

    error_message: String,
}

// Object to hold results of a single download
#[derive(Default, Serialize)]
struct IndividualDownload {
    bytes: u64,
    nanos: u64,
    goodput_mbps: f64,
}

fn report_results(test_results: &TestResults) {
    println!("{}", serde_json::to_string_pretty(&test_results).unwrap());
}

fn create_request<T: Into<String>>(url_string: T) -> http::Request {
    http::Request {
        url: Some(url_string.into()),
        method: Some(String::from("GET")),
        headers: None,
        body: None,
        deadline: None,
    }
}

// TODO (NET-1663): move to helper method
// TODO (NET-1664): verify checksum on data received
async fn fetch_and_discard_url(
    loader_proxy: LoaderProxy,
    request: http::Request,
) -> Result<IndividualDownload, anyhow::Error> {
    let start_time = zx::Time::get(zx::ClockId::Monotonic);
    let response =
        loader_proxy.fetch(request).await.context("Error while calling Loader::Fetch")?;

    if let Some(e) = response.error {
        return Err(format_err!("LoaderProxy error - {:?}", e));
    }

    let socket = match response.body {
        Some(s) => fasync::Socket::from_socket(s).context("Error while wrapping body socket")?,
        _ => {
            return Err(format_err!(
                "failed to read UrlBody from the stream - error: {}",
                zx::Status::BAD_STATE
            ));
        }
    };

    // discard the bytes
    let mut stdio_sink = AllowStdIo::new(::std::io::sink());
    let bytes_received =
        copy(socket, &mut stdio_sink).await.context("Failed to read bytes from the socket")?;
    let stop_time = zx::Time::get(zx::ClockId::Monotonic);

    let time_nanos = (stop_time - start_time).into_nanos() as u64;
    let time_seconds = time_nanos as f64 * 1e-9;

    let bits_received = (bytes_received * 8) as f64;

    log::info!("Received {} bytes in {:.3} seconds", bytes_received, time_seconds);

    if bytes_received < 1 {
        return Err(format_err!(
            "Failed to download data from url! bytes_received = {}",
            bytes_received
        ));
    }

    let megabits_per_sec = bits_received * 1e-6 / time_seconds;

    let mut individual_download = IndividualDownload::default();
    individual_download.goodput_mbps = megabits_per_sec;
    individual_download.bytes = bytes_received;
    individual_download.nanos = time_nanos;

    Ok(individual_download)
}
