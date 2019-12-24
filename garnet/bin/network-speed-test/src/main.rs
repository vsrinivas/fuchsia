// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod opts;

use {
    crate::opts::Opt,
    anyhow::{format_err, Context, Error},
    fidl_fuchsia_net_oldhttp::{self as http, HttpServiceProxy},
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog::{self as syslog, fx_log_info},
    fuchsia_zircon as zx,
    futures::io::{copy, AllowStdIo},
    serde_derive::Serialize,
    std::process,
    structopt::StructOpt,
};

fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["network-speed-test"]).expect("should not fail");

    let opt = Opt::from_args();
    fx_log_info!("{:?}", opt);

    // create objects to hold test objects and results
    let mut test_results = TestResults::default();

    let mut test_pass = true;
    if let Err(e) = run_test(opt, &mut test_results) {
        test_pass = false;
        test_results.error_message = e.to_string();
    }

    report_results(&mut test_results);

    if !test_pass {
        process::exit(1);
    }

    Ok(())
}

fn run_test(opt: Opt, test_results: &mut TestResults) -> Result<(), Error> {
    let mut exec = fasync::Executor::new().context("error creating event loop")?;

    let http_svc = connect_to_service::<http::HttpServiceMarker>()?;
    test_results.connect_to_http_service = true;

    let url_request = create_url_request(opt.target_url);
    let ind_download = exec.run_singlethreaded(fetch_and_discard_url(http_svc, url_request))?;
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

fn create_url_request<T: Into<String>>(url_string: T) -> http::UrlRequest {
    http::UrlRequest {
        url: url_string.into(),
        method: String::from("GET"),
        headers: None,
        body: None,
        response_body_buffer_size: 0,
        auto_follow_redirects: true,
        cache_mode: http::CacheMode::BypassCache,
        response_body_mode: http::ResponseBodyMode::Stream,
    }
}

// TODO (NET-1663): move to helper method
// TODO (NET-1664): verify checksum on data received
async fn fetch_and_discard_url(
    http_service: HttpServiceProxy,
    mut url_request: http::UrlRequest,
) -> Result<IndividualDownload, anyhow::Error> {
    // Create a UrlLoader instance
    let (s, p) = zx::Channel::create().context("failed to create zx channel")?;
    let proxy = fasync::Channel::from_channel(p).context("failed to make async channel")?;

    let loader_server = fidl::endpoints::ServerEnd::<http::UrlLoaderMarker>::new(s);
    http_service.create_url_loader(loader_server)?;

    let loader_proxy = http::UrlLoaderProxy::new(proxy);
    let start_time = zx::Time::get(zx::ClockId::Monotonic);
    let response = loader_proxy.start(&mut url_request).await?;

    if let Some(e) = response.error {
        return Err(format_err!(
            "UrlLoaderProxy error - code:{} ({})",
            e.code,
            e.description.unwrap_or("".into())
        ));
    }

    let socket = match response.body.map(|x| *x) {
        Some(http::UrlBody::Stream(s)) => fasync::Socket::from_socket(s)?,
        _ => {
            return Err(format_err!(
                "failed to read UrlBody from the stream - error: {}",
                zx::Status::BAD_STATE
            ));
        }
    };

    // discard the bytes
    let mut stdio_sink = AllowStdIo::new(::std::io::sink());
    let bytes_received = copy(socket, &mut stdio_sink).await?;
    let stop_time = zx::Time::get(zx::ClockId::Monotonic);

    let time_nanos = (stop_time - start_time).into_nanos() as u64;
    let time_seconds = time_nanos as f64 * 1e-9;

    let bits_received = (bytes_received * 8) as f64;

    fx_log_info!("Received {} bytes in {:.3} seconds", bytes_received, time_seconds);

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
