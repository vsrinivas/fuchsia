// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod opts;

use crate::opts::Opt;

use {
    anyhow::{bail, format_err, Context as _, Error},
    fidl_fuchsia_wlan_device_service::{DeviceServiceMarker, DeviceServiceProxy},
    fidl_fuchsia_wlan_sme as fidl_sme, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog::{self as syslog, fx_log_info},
    serde::Serialize,
    std::collections::HashMap,
    std::process,
    std::thread::sleep,
    std::time::{Duration, Instant},
    structopt::StructOpt,
};

#[allow(dead_code)]
type WlanService = DeviceServiceProxy;

fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["wlan-stress-test"]).expect("Should not fail");

    let opt = Opt::from_args();
    fx_log_info!("{:?}", opt);

    if opt.connect_test_enabled && opt.target_ssid.is_empty() {
        return Err(format_err!("A target_ssid must be provided for connect tests"));
    }
    if !opt.scan_test_enabled && !opt.connect_test_enabled && !opt.disconnect_test_enabled {
        return Err(format_err!("At least one api must be selected for testing"));
    }

    // create objects to hold test objects and results
    let mut test_results: TestResults = Default::default();
    test_results.num_repetitions = opt.repetitions;
    test_results.scan_test_enabled = opt.scan_test_enabled;
    test_results.connect_test_enabled = opt.connect_test_enabled;
    test_results.disconnect_test_enabled = opt.disconnect_test_enabled;

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

fn duration_to_ms(duration: Duration) -> u128 {
    (duration.as_secs() * 1000 + duration.subsec_millis() as u64) as u128
}

fn run_test(opt: Opt, test_results: &mut TestResults) -> Result<(), Error> {
    let mut scan_test_pass = true;
    let mut connect_test_pass = true;
    let mut disconnect_test_pass = true;
    let mut exec = fasync::Executor::new().context("Error creating event loop")?;
    let wlan_svc =
        connect_to_service::<DeviceServiceMarker>().context("Failed to connect to wlan_service")?;

    let fut = async {
        let wlan_iface_ids = wlan_service_util::get_iface_list(&wlan_svc)
            .await
            .context("Failed to query wlan_service iface list")?;

        if wlan_iface_ids.is_empty() {
            return Err(format_err!("Did not find wlan interfaces"));
        };

        for iface in wlan_iface_ids {
            let sme_proxy = wlan_service_util::client::get_sme_proxy(&wlan_svc, iface).await?;
            match sme_proxy.status().await {
                Ok(status) => status,
                Err(_) => continue,
            };
            let result = TestReport::new();
            let iface_object = WlanIface::new(sme_proxy, result);
            test_results.iface_objects.insert(iface, iface_object);
        }

        // now that we have interfaces...  let's try to use them!
        for (_iface_id, wlaniface) in test_results.iface_objects.iter_mut() {
            let mut total_scan_time_ms = 0;
            let mut total_connect_time_ms = 0;
            let mut total_disconnect_time_ms = 0;
            for i in 0..opt.repetitions {
                if opt.scan_test_enabled {
                    let start = Instant::now();
                    let result =
                        wlan_service_util::client::passive_scan(&wlaniface.sme_proxy).await;
                    match result {
                        Ok(_ess_info) => {
                            total_scan_time_ms +=
                                duration_to_ms(Instant::now().duration_since(start));
                            wlaniface.report.num_successful_scans += 1;
                            wlaniface.report.last_successful_scan_attempt = i + 1;
                        }
                        _ => {}
                    };
                }

                if opt.connect_test_enabled {
                    let start = Instant::now();
                    let result = wlan_service_util::client::connect(
                        &wlaniface.sme_proxy,
                        opt.target_ssid.as_bytes().to_vec(),
                        opt.target_pwd.as_bytes().to_vec(),
                    )
                    .await;
                    match result {
                        Ok(true) => {
                            total_connect_time_ms +=
                                duration_to_ms(Instant::now().duration_since(start));
                            wlaniface.report.num_successful_connects += 1;
                            wlaniface.report.last_successful_connection_attempt = i + 1;
                        }
                        _ => {}
                    };
                }

                if opt.disconnect_test_enabled {
                    let start = Instant::now();
                    let result = wlan_service_util::client::disconnect(&wlaniface.sme_proxy).await;
                    match result {
                        Ok(()) => {
                            total_disconnect_time_ms +=
                                duration_to_ms(Instant::now().duration_since(start));
                            wlaniface.report.num_successful_disconnects += 1;
                            wlaniface.report.last_successful_disconnect_attempt = i + 1;
                        }
                        _ => {}
                    };
                }
                sleep(Duration::from_millis(opt.wait_time_ms));
            }
            if wlaniface.report.num_successful_scans > 0 {
                wlaniface.report.average_scan_time_ms =
                    total_scan_time_ms / wlaniface.report.num_successful_scans;
            }
            if wlaniface.report.num_successful_connects > 0 {
                wlaniface.report.average_connect_time_ms =
                    total_connect_time_ms / wlaniface.report.num_successful_connects;
            }
            if wlaniface.report.num_successful_disconnects > 0 {
                wlaniface.report.average_disconnect_time_ms =
                    total_disconnect_time_ms / wlaniface.report.num_successful_disconnects;
            }
        }

        for (_iface_id, wlaniface) in test_results.iface_objects.iter_mut() {
            if opt.scan_test_enabled && (wlaniface.report.num_successful_scans < opt.repetitions) {
                scan_test_pass = false;
            }
            if opt.connect_test_enabled
                && (wlaniface.report.num_successful_connects < opt.repetitions)
            {
                connect_test_pass = false;
            }
            if opt.disconnect_test_enabled
                && (wlaniface.report.num_successful_disconnects < opt.repetitions)
            {
                disconnect_test_pass = false;
            }
        }

        Ok(())
    };
    exec.run_singlethreaded(fut)?;

    let mut error_string = String::from("Test Failed:");
    if opt.scan_test_enabled && !scan_test_pass {
        error_string.push_str(" Scan Failed");
    }
    if opt.connect_test_enabled && !connect_test_pass {
        error_string.push_str(" Connect Failed");
    }
    if opt.disconnect_test_enabled && !disconnect_test_pass {
        error_string.push_str(" Disconnect Failed");
    }
    if !(scan_test_pass && connect_test_pass && disconnect_test_pass) {
        bail!(error_string)
    }

    Ok(())
}

#[derive(Serialize)]
struct TestReport {
    num_successful_scans: u128,
    last_successful_scan_attempt: u128,
    average_scan_time_ms: u128,

    num_successful_connects: u128,
    last_successful_connection_attempt: u128,
    average_connect_time_ms: u128,

    num_successful_disconnects: u128,
    last_successful_disconnect_attempt: u128,
    average_disconnect_time_ms: u128,
}

impl TestReport {
    pub fn new() -> TestReport {
        TestReport {
            num_successful_scans: 0,
            last_successful_scan_attempt: 0,
            average_scan_time_ms: 0,

            num_successful_connects: 0,
            last_successful_connection_attempt: 0,
            average_connect_time_ms: 0,

            num_successful_disconnects: 0,
            last_successful_disconnect_attempt: 0,
            average_disconnect_time_ms: 0,
        }
    }
}

#[derive(Serialize)]
struct WlanIface {
    #[serde(skip_serializing)]
    sme_proxy: fidl_sme::ClientSmeProxy,

    report: TestReport,
}

impl WlanIface {
    pub fn new(sme_proxy: fidl_sme::ClientSmeProxy, report: TestReport) -> WlanIface {
        WlanIface { sme_proxy: sme_proxy, report: report }
    }
}

// Object to hold overall test status
#[derive(Default, Serialize)]
struct TestResults {
    num_repetitions: u128,
    scan_test_enabled: bool,
    connect_test_enabled: bool,
    disconnect_test_enabled: bool,

    #[serde(flatten)]
    iface_objects: HashMap<u16, WlanIface>,

    error_message: String,
}

fn report_results(test_results: &TestResults) {
    println!("{}", serde_json::to_string_pretty(&test_results).unwrap());
}
