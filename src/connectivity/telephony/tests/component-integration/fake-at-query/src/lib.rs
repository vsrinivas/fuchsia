// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_telephony_ril::{RadioInterfaceLayerMarker, SetupMarker},
    fuchsia_component::{
        client::{launch, launcher},
        fuchsia_single_component_package_url,
    },
    fuchsia_syslog::{self as syslog, macros::*},
    futures::future::{join_all, Future},
    futures::lock::Mutex,
    qmi as tel_ctl, //Todo: rename qmi module
    std::pin::Pin,
    std::sync::Arc,
    tel_dev::component_test::*,
};

// Send this command to attempt to make a call on the test driver and fail.
const AT_CMD_REQ_ATD_STR: &str = "ATD\r";
const AT_CMD_RESP_NO_CARRIER_STR: &str = "NO CARRIER\r";

// Send this string to request the test driver sends back an invalid text string.
const AT_CMD_REQ_INVALID_UNICODE_STR: &str = "INVALID UNICODE";

const RIL_URL: &str = fuchsia_single_component_package_url!("ril-at");

// TODO(kehrt) Split this into multiple tests.  Unfortunately, test setup and teardown multiple
// times causes a variety of errors I don't understand yet.
#[fuchsia_async::run_singlethreaded(test)]
async fn at_one_query_test() -> Result<(), Error> {
    syslog::init_with_tags(&["at-query-test"]).expect("Can't init logger");

    //Setup
    const TEL_PATH: &str = "class/at-transport";
    let found_device =
        get_fake_device_in_isolated_devmgr(TEL_PATH).await.expect("getting fake device");
    let launcher = launcher().context("Failed to open launcher service")?;
    let chan = tel_ctl::connect_transport_device(&found_device).await?;
    let app =
        launch(&launcher, RIL_URL.to_string(), None).context("Failed to launch ril-at service")?;
    let ril_modem_setup = app.connect_to_service::<SetupMarker>()?;
    ril_modem_setup.connect_transport(chan).await?.expect("make sure telephony svc is running");
    let ril_modem = app.connect_to_service::<RadioInterfaceLayerMarker>()?;

    // Test sending and receiving one message.
    fx_log_err!("sending ATD");
    let resp = ril_modem.raw_command(&AT_CMD_REQ_ATD_STR).await?.expect("error sending get info");
    assert_eq!(resp, AT_CMD_RESP_NO_CARRIER_STR);
    fx_log_err!("received and verified responses");

    // Test sending a message that causes a parse error for the response.
    fx_log_err!("sending a request which expects an error response");
    let resp = ril_modem
        .raw_command(&AT_CMD_REQ_INVALID_UNICODE_STR)
        .await
        .expect("error sending get info");
    assert!(resp.is_err());
    fx_log_err!("received and verified responses");

    // Test interleaving a bunch of sends and receives.  let ril_modem_arc = Arc::new(Mutex::new(ril_modem));
    let ril_modem_arc = Arc::new(Mutex::new(ril_modem));
    let mut vec: Vec<Pin<Box<dyn Future<Output = (i32, String)>>>> = Vec::new();
    for x in 0..100 {
        let ril_modem_arc = ril_modem_arc.clone();
        vec.push(Box::pin(async move {
            let send_string = format!("RACE{:}", x);
            let ril_modem_arc = ril_modem_arc.lock().await;
            let recv_result = ril_modem_arc.raw_command(&send_string).await;
            (x, recv_result.unwrap().unwrap())
        }))
    }

    let joined = join_all(vec).await;

    for (x, recv_result) in joined.into_iter() {
        let recv_string = format!("ECAR{:}", x);
        assert!(recv_string == recv_result);
    }
    // Tear down
    unbind_fake_device(&found_device)?;
    fx_log_err!("unbinded device");
    validate_removal_of_fake_device(TEL_PATH).await.expect("validate removal of device");
    Ok(())
}
