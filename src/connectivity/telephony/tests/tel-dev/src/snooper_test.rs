// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::component_test,
    anyhow::{format_err, Error},
    fidl_fuchsia_telephony_snoop::{Message as SnoopMessage, SnooperEvent, SnooperEventStream},
    fuchsia_zircon as zx,
    futures::{self, stream::TryStreamExt},
};

// TODO(jiamingw): let `fx format-code` skip the following part
pub const QMI_PATH: &str = "class/qmi-transport";
pub const QMI_IMEI_REQ: &[u8; 13] = &[1, 12, 0, 0, 2, 1, 0, 1, 0, 37, 0, 0, 0];
pub const QMI_IMEI_RESP: &[u8; 42] = &[
    1, 41, 0, 128, 2, 1, 2, 1, 0, 37, 0, 29, 0, 2, 4, 0, 0, 0, 0, 0, 16, 1, 0, 48, 17, 15, 0, 51,
    53, 57, 50, 54, 48, 48, 56, 48, 49, 54, 56, 51, 53, 49,
];
pub const QMI_PERIO_EVENT: &[u8; 12] = &[1, 11, 0, 128, 0, 0, 2, 0, 39, 0, 0, 0];
pub const SNOOPER_TEST_TIMEOUT: i64 = 45_000_000_000;
pub const SNOOPER_CONNECT_TIMEOUT: i64 = 100_000_000;

pub struct ValidateSnoopResultArgs<'a> {
    pub hardcoded: Option<Vec<u8>>,
    pub driver_channel: Option<&'a mut zx::Channel>,
    pub snoop_event_stream_vec: &'a mut Vec<SnooperEventStream>,
}

/// Read next message from snooper's `event_stream`
/// Caller should be responsible to set a limited wait time
pub async fn read_next_msg_from_snoop_stream(
    event_stream: &mut SnooperEventStream,
) -> Result<Vec<u8>, Error> {
    let res: Vec<u8>;
    if let Ok(Some(SnooperEvent::OnMessage { msg })) = event_stream.try_next().await {
        let qmi_message = match msg {
            SnoopMessage::QmiMessage(m) => m,
        };
        res = qmi_message.opaque_bytes.to_vec();
    } else {
        return Err(format_err!("read_next_msg_from_snoop_stream: unexpected msg"));
    }
    Ok(res)
}

/// Validate `driver_channel` and `snoop_event_stream_vec` to have idential "next" message
/// compared to `hardcoded`. `driver_channel` and `hardcoded` should have at least one
/// valid input. `snoop_event_stream_vec` should have at least one event stream.
pub async fn validate_snoop_result<'a>(args: ValidateSnoopResultArgs<'a>) -> Result<(), Error> {
    let mut src_msg_vec = Vec::<Vec<u8>>::new();
    // Collect source message from hardcoded value, if it is presented
    if let Some(hardcoded_vec) = args.hardcoded {
        src_msg_vec.push(hardcoded_vec);
    }
    // Collect source message from next message from driver channel, if it is present.
    if let Some(channel) = args.driver_channel {
        let mut qmi_msg_vec = component_test::read_next_msg_from_channel(channel)?;
        component_test::qmi_vec_resize(&mut qmi_msg_vec)?;
        src_msg_vec.push(qmi_msg_vec);
    }
    // Source msg need to be presented
    if src_msg_vec.is_empty() {
        return Err(format_err!("validate_snoop_result: no source data to compare"));
    }
    // Compare source msg if more than one source is presented
    if src_msg_vec.len() > 1 {
        let vec_0 = &src_msg_vec[0];
        let vec_1 = &src_msg_vec[1];
        if !component_test::is_equal_vec(&vec_0, &vec_1) {
            return Err(format_err!("hardcoded and driver channel msg not identical"));
        }
    }
    // Snoop message need to be presented
    if args.snoop_event_stream_vec.is_empty() {
        return Err(format_err!("validate_snoop_result: no snoop data to compare"));
    }
    // Compare snoop messages with source message
    for mut snoop_event_stream in args.snoop_event_stream_vec {
        let mut snoop_qmi_msg_vec =
            read_next_msg_from_snoop_stream(&mut snoop_event_stream).await?;
        component_test::qmi_vec_resize(&mut snoop_qmi_msg_vec)?;
        if !component_test::is_equal_vec(&snoop_qmi_msg_vec, &src_msg_vec[0]) {
            return Err(format_err!("snoop msg and source msg identical"));
        }
    }
    Ok(())
}
