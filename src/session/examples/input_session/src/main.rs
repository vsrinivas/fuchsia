// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error, fidl_fuchsia_input_report::InputReport, fuchsia_async as fasync,
    fuchsia_syslog::fx_log_info, futures::channel::mpsc::Receiver, futures::StreamExt,
    input::mouse,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["input_session"]).expect("Failed to initialize logger.");

    let mut mouse_receiver: Receiver<InputReport> = mouse::all_mouse_reports().await?;
    while let Some(report) = mouse_receiver.next().await {
        if let Some(mouse_report) = report.mouse {
            fx_log_info!("movement_x: {}", mouse_report.movement_x.unwrap_or_default());
        }
    }
    Ok(())
}
