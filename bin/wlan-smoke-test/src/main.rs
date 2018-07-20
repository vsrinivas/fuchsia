// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api, pin)]

// Explicitly added due to conflict using custom_attribute and async_await above.
#[macro_use]
extern crate serde_derive;

mod wlan_service_util;

use failure::{Error, ResultExt, err_msg};
use fidl_fuchsia_wlan_device_service as wlan_service;
use fidl_fuchsia_wlan_device_service::{DeviceServiceMarker, DeviceServiceProxy};
use fidl_fuchsia_wlan_sme as fidl_sme;
use fuchsia_app::client::connect_to_service;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use serde::{Serialize};
use serde::ser::{Serializer, SerializeMap, SerializeStruct};
use std::collections::HashMap;
use std::process;
use std::fmt;

type WlanService = DeviceServiceProxy;

fn main() -> Result<(), Error> {

    // create objects to hold test objects and results
    let mut test_results: TestResults = Default::default();

    let mut test_pass = true;
    if let Err(e) = run_test(&mut test_results) {
        test_pass = false;
        test_results.error_message = e.to_string();
    }

    report_results(&mut test_results);

    if !test_pass {
        process::exit(1);
    }

    Ok(())
}

fn run_test(test_results: &mut TestResults)
        -> Result<(), Error>
{
    let mut test_pass = true;
    let mut exec = fasync::Executor::new().context("error creating event loop")?;
    let wlan_svc = connect_to_service::<DeviceServiceMarker>()
            .context("Failed to connect to wlan_service")?;
    test_results.connect_to_wlan_service = true;

    let fut = async {
        let wlan_iface_ids = await!(wlan_service_util::get_iface_list(&wlan_svc))
                .context("wlan-smoke-test: failed to query wlanservice iface list")?;
        test_results.query_wlan_service_iface_list = true;

        if wlan_iface_ids.len() < 1 {
            return Err(err_msg("Did not find wlan interfaces"));
        };
        test_results.wlan_discovered = true;
        // note: interface discovery is marked false at the time of failure
        test_results.interface_status = true;

        for iface in wlan_iface_ids {
            let sme_proxy = await!(
                    wlan_service_util::get_iface_sme_proxy(&wlan_svc, iface))?;
            let status_response = match await!(sme_proxy.status()) {
                Ok(status) => status,
                Err(e) => {
                    test_results.interface_status = false;
                    continue;
                }
            };
            let iface_object = WlanIface::new(sme_proxy, status_response);

            // if any of the checks failed, throw an error to indicate a part of
            // the test failure
            if !(iface_object.connection_success &&
                 iface_object.dhcp_success &&
                 iface_object.data_transfer) {
                // note: failures are logged at the point of the failure,
                // simply checking here to return overall test status
                test_pass = false;
            }
            test_results.iface_objects.insert(iface, iface_object);
        }
        Ok(())
    };
    exec.run_singlethreaded(fut)?;

    if !test_pass {
        return Err(err_msg("Saw a failure on at least one interface"));
    }
    Ok(())
}

// Object to hold overall test status
#[derive(Default, Serialize)]
struct TestResults {
    connect_to_wlan_service: bool,
    query_wlan_service_iface_list: bool,
    wlan_discovered: bool,
    interface_status: bool,
    base_data_transfer: bool,

    #[serde(flatten)]
    iface_objects: HashMap<u16, WlanIface>,

    error_message: String
}

// Object to hold test specific status
#[derive(Serialize)]
struct WlanIface {
    #[serde(skip_serializing)]
    sme_proxy: fidl_sme::ClientSmeProxy,

    #[serde(skip_serializing)]
    initial_status: fidl_sme::ClientStatusResponse,

    connection_success: bool,

    dhcp_success: bool,

    data_transfer: bool
}

impl WlanIface {
    pub fn new(sme_proxy: fidl_sme::ClientSmeProxy,
               status: fidl_sme::ClientStatusResponse) -> WlanIface {
        WlanIface {
            sme_proxy: sme_proxy,
            initial_status: status,
            connection_success: false,
            dhcp_success: false,
            data_transfer: false
        }
    }
}

fn report_results(test_results: &TestResults) {
    println!("Test Results: {}", serde_json::to_string_pretty(&test_results).unwrap());
}
