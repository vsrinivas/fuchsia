// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {failure::Error, fidl_fuchsia_input_report, fuchsia_syslog::fx_log_info, input};

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["touchable_session"]).expect("Failed to initialize logger.");

    let touch_device: fidl_fuchsia_input_report::InputDeviceProxy =
        input::get_touch_input_device().await?;
    fx_log_info!("got touch device");

    loop {
        let input_reports: Vec<fidl_fuchsia_input_report::InputReport> =
            touch_device.get_reports().await.unwrap();
        for input_report in input_reports {
            if let Some(ref touch_report) = input_report.touch {
                if let Some(ref contacts) = touch_report.contacts {
                    for contact_report in contacts {
                        if let Some(ref contact_id) = contact_report.contact_id {
                            fx_log_info!("contact id: {}", contact_id);
                        }
                    }
                }
            }
        }
    }
}
