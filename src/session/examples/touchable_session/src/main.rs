// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    fidl_fuchsia_input_report,
    fuchsia_syslog::fx_log_info,
    futures::channel::mpsc::{self, Receiver, Sender},
    futures::StreamExt,
    input::{self},
};

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["touchable_session"]).expect("Failed to initialize logger.");

    // All InputReports will come in through this channel for now.
    let (sender, mut receiver): (
        Sender<fidl_fuchsia_input_report::InputReport>,
        Receiver<fidl_fuchsia_input_report::InputReport>,
    ) = mpsc::channel(1);

    let mouse: Result<input::MouseBinding, Error> =
        input::InputDeviceBinding::new(sender.clone()).await;
    if let Ok(_) = mouse {
        fx_log_info!("Got mouse device.");
    }

    let touch: Result<input::TouchBinding, Error> =
        input::InputDeviceBinding::new(sender.clone()).await;
    if let Ok(_) = touch {
        fx_log_info!("Got touch device.");
    }

    loop {
        if let Some(input_report) = receiver.next().await {
            if let Some(ref mouse_report) = input_report.mouse {
                if let Some(ref movement_x) = mouse_report.movement_x {
                    fx_log_info!("movement_x: {}", movement_x);
                }
            } else if let Some(ref touch_report) = input_report.touch {
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
