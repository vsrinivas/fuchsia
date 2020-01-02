// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    fidl_fuchsia_telephony_snoop::SnooperMarker,
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog::{self as syslog, macros::*},
    qmi,
    tel_dev::{component_test::*, snooper_test::*},
};

const SNOOPER_CLIENT_NUM: i64 = 3;

// TODO (jiamingw): remove timeout within test

#[fuchsia_async::run_singlethreaded(test)]
async fn snooper_multi_clients() {
    syslog::init_with_tags(&["snooper-multi-clients"]).expect("Can't init logger");
    // Connect to driver and get a channel to talk to QMI driver directly.
    let qmi_device_file =
        get_fake_device_in_isolated_devmgr(QMI_PATH).await.expect("get fake device");
    let mut qmi_driver_channel =
        qmi::connect_transport_device(&qmi_device_file).await.expect("connecting to driver");
    // Connect to snooper and get the event stream.
    let mut snooper_proxies = Vec::new();
    for _ in 0..SNOOPER_CLIENT_NUM {
        let snooper_proxy = connect_to_service::<SnooperMarker>().expect("connecting to snooper");
        snooper_proxies.push(snooper_proxy);
    }
    // Wait for snooper to connect to device driver
    let mut device_num: u32;
    loop {
        device_num =
            snooper_proxies.get(0).unwrap().get_device_num().await.expect("query from snooper");
        if device_num != 0 {
            break;
        }
        wait_in_nanos(SNOOPER_CONNECT_TIMEOUT).await;
    }
    assert_eq!(device_num, 1); // Snooper should have 1 device connected.
                               // Create event stream vec for validating snooper output.
    let mut snoop_event_streams = Vec::new();
    for snooper_proxy in snooper_proxies {
        let snooper_event_stream = snooper_proxy.take_event_stream();
        snoop_event_streams.push(snooper_event_stream);
    }

    fx_log_info!("sending QMI request");
    qmi_driver_channel.write(QMI_IMEI_REQ, &mut Vec::new()).expect("sending QMI msg");
    validate_snoop_result(ValidateSnoopResultArgs {
        hardcoded: Some(QMI_IMEI_REQ.to_vec()),
        driver_channel: None,
        snoop_event_stream_vec: &mut snoop_event_streams,
    })
    .await
    .expect("validate req");
    validate_snoop_result(ValidateSnoopResultArgs {
        hardcoded: Some(QMI_IMEI_RESP.to_vec()),
        driver_channel: Some(&mut qmi_driver_channel),
        snoop_event_stream_vec: &mut snoop_event_streams,
    })
    .await
    .expect("validate resp 1");
    validate_snoop_result(ValidateSnoopResultArgs {
        hardcoded: Some(QMI_PERIO_EVENT.to_vec()),
        driver_channel: Some(&mut qmi_driver_channel),
        snoop_event_stream_vec: &mut snoop_event_streams,
    })
    .await
    .expect("validate resp 2");
    fx_log_info!("received and validated QMI request/responses");

    // Remove fake device and ensure the device is gone.
    unbind_fake_device(&qmi_device_file, ONE_SECOND_IN_NANOS).expect("removing fake device");
    validate_removal_of_fake_device(QMI_PATH, ONE_SECOND_IN_NANOS)
        .await
        .expect("validate removal of device");
}
