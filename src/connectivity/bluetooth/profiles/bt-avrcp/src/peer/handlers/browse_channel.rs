// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    bt_avctp::AvctpCommandStream,
    fuchsia_syslog::{fx_log_info, fx_vlog},
    futures::{self, stream::StreamExt},
};

pub async fn handle_browse_channel_requests(mut stream: AvctpCommandStream) {
    while let Some(result) = stream.next().await {
        match result {
            Ok(command) => {
                fx_vlog!(tag: "avrcp", 2, "Received command over browse channel: {:?}.", command);
            }
            Err(e) => {
                fx_log_info!("Command stream returned error {:?}", e);
                break;
            }
        }
    }
}
