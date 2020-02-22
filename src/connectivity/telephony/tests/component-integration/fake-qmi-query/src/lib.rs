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
    qmi,
    tel_dev::component_test::*,
};

const RIL_URL: &str = fuchsia_single_component_package_url!("ril-qmi");

// Tests that creating and destroying a fake QMI device binds and unbinds the qmi-host driver.
#[fuchsia_async::run_singlethreaded(test)]
async fn qmi_query_test() -> Result<(), Error> {
    syslog::init_with_tags(&["qmi-query-test"]).expect("Can't init logger");
    const TEL_PATH: &str = "class/qmi-transport";
    let found_device =
        get_fake_device_in_isolated_devmgr(TEL_PATH).await.expect("getting fake device");
    let launcher = launcher().context("Failed to open launcher service")?;
    let chan = qmi::connect_transport_device(&found_device).await?;
    let app =
        launch(&launcher, RIL_URL.to_string(), None).context("Failed to launch ril-qmi service")?;
    let ril_modem_setup = app.connect_to_service::<SetupMarker>()?;
    ril_modem_setup.connect_transport(chan).await?.expect("make sure telephony svc is running");
    let ril_modem = app.connect_to_service::<RadioInterfaceLayerMarker>()?;

    fx_log_info!("sending a IMEI request");
    let imei = ril_modem.get_device_identity().await?.expect("error sending IMEI request");
    assert_eq!(imei, "359260080168351");
    let imei = ril_modem.get_device_identity().await?.expect("error sending IMEI request");
    assert_eq!(imei, "359260080168351");
    fx_log_info!("received and verified responses");
    unbind_fake_device(&found_device)?;
    fx_log_info!("unbinded device");
    validate_removal_of_fake_device(TEL_PATH).await.expect("validate removal of device");
    Ok(())
}
