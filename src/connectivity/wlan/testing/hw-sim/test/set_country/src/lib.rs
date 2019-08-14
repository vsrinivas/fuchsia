// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use {
    fidl_fuchsia_wlan_device_service::{
        DeviceServiceMarker, DeviceServiceProxy, SetCountryRequest,
    },
    fidl_fuchsia_wlan_tap::WlantapPhyEvent,
    fuchsia_async::{DurationExt, Executor, TimeoutExt},
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon::DurationNum,
    fuchsia_zircon_sys::ZX_OK,
    futures::channel::oneshot,
    pin_utils::pin_mut,
    wlan_hw_sim::*,
};

async fn set_country_helper<'a>(
    receiver: oneshot::Receiver<()>,
    svc: &'a DeviceServiceProxy,
    req: &'a mut SetCountryRequest,
) {
    let status = svc.set_country(req).await.expect("calling set_country");
    assert_eq!(status, ZX_OK);
    receiver.await.expect("error receiving set_country_helper mpsc message");
}

/// Issue service.fidl:SetCountry() protocol to Wlanstack's service with a test country code.
/// Test two things:
///  - If wlantap PHY device received the specified test country code
///  - If the SetCountry() returned successfully (ZX_OK).
#[test]
fn set_country() {
    const ALPHA2: &[u8; 2] = b"RS";

    let mut exec = Executor::new().expect("Failed to create an executor");
    let mut helper =
        test_utils::TestHelper::begin_test(&mut exec, create_wlantap_config_client(HW_MAC_ADDR));
    let svc = connect_to_service::<DeviceServiceMarker>()
        .expect("Failed to connect to wlanstack_dev_svc");

    let resp = exec
        .run_singlethreaded(
            svc.list_phys().on_timeout(1.seconds().after_now(), || {
                panic!("list_phys did not complete in time")
            }),
        )
        .unwrap();

    assert!(resp.phys.len() > 0, "WLAN PHY device is created but ListPhys returned empty.");
    let phy_id = resp.phys[0].phy_id;
    let mut req = SetCountryRequest { phy_id, alpha2: *ALPHA2 };

    let (sender, receiver) = oneshot::channel();
    // Employ a future to make sure this test does not end before WlantanPhyEvent is captured.
    let set_country_fut = set_country_helper(receiver, &svc, &mut req);
    pin_mut!(set_country_fut);

    let mut sender = Some(sender);
    helper.run_until_complete_or_timeout(
        &mut exec,
        1.seconds(),
        "wlanstack_dev_svc set_country",
        |event| {
            if let WlantapPhyEvent::SetCountry { args } = event {
                assert_eq!(args.alpha2, *ALPHA2);
                sender.take().map(|s| s.send(()));
            }
        },
        set_country_fut,
    );
}
