// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! tel-snoop-cli is used for snooping Qmi messages sent/received by transport driver

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_telephony_snoop::{
        Message as SnoopMessage, SnooperEvent, SnooperMarker, SnooperProxy,
    },
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog::{self as syslog, macros::*},
    futures::{self, stream::TryStreamExt},
    itertools::Itertools,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["tel-snoop-cli"]).expect("Can't init logger");
    let snooper: SnooperProxy = connect_to_service::<SnooperMarker>()?;
    fx_log_info!("connecting to tel snooper");
    let mut event_stream = snooper.take_event_stream();
    while let Ok(Some(SnooperEvent::OnMessage { msg })) = event_stream.try_next().await {
        let qmi_message = match msg {
            SnoopMessage::QmiMessage(m) => Some(m),
        };
        if qmi_message.is_none() {
            return Err(format_err!("tel-snoop-cli received message None"));
        }
        // is_none() is checked so unwrap() can be done here.
        let message = qmi_message.unwrap();
        print!(
            "Received msg direction: {:?}, timestamp: {}, msg: {}\n",
            message.direction,
            message.timestamp,
            message.opaque_bytes.iter().join(" ")
        );
    }
    fx_log_info!("tel-snoop-cli terminating");
    Ok(())
}
