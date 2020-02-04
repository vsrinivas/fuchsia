// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_lowpan_spinel::{DeviceEvent, DeviceEventStream, Error as SpinelError},
    fuchsia_async::futures::stream::TryStreamExt,
    fuchsia_syslog::macros::*,
};

pub async fn expect_on_receive_event(stream: &mut DeviceEventStream) -> Result<Vec<u8>, Error> {
    if let Some(event) = stream.try_next().await.context("error waiting for spinel events")? {
        match event {
            DeviceEvent::OnReceiveFrame { data } => {
                return Ok(data);
            }
            DeviceEvent::OnReadyForSendFrames { number_of_frames } => {
                return Err(format_err!(
                    "Expect OnReceive(), got OnReadyForSendFrame(): {:?}",
                    number_of_frames
                ));
            }
            DeviceEvent::OnError { error, .. } => {
                return Err(format_err!("Expect OnReceive(), got OnError(): {:?}", error));
            }
        }
    }
    return Err(format_err!("No event"));
}

pub async fn expect_on_ready_for_send_frame_event(
    stream: &mut DeviceEventStream,
) -> Result<u32, Error> {
    if let Some(event) = stream.try_next().await.context("error waiting for spinel events")? {
        match event {
            DeviceEvent::OnReceiveFrame { data } => {
                return Err(format_err!(
                    "Expect OnReadyForSendFrames(), got OnReceiveFrame(): {:?}",
                    data
                ));
            }
            DeviceEvent::OnReadyForSendFrames { number_of_frames } => {
                return Ok(number_of_frames);
            }
            DeviceEvent::OnError { error, .. } => {
                return Err(format_err!(
                    "Expect OnReadyForSendFrames(), got OnError(): {:?}",
                    error
                ));
            }
        }
    }
    return Err(format_err!("No event"));
}

pub async fn expect_on_error_event(
    stream: &mut DeviceEventStream,
    expected_err: SpinelError,
) -> Result<(), Error> {
    if let Some(event) = stream.try_next().await.context("error waiting for spinel events")? {
        match event {
            DeviceEvent::OnReceiveFrame { data } => {
                return Err(format_err!("Expect OnError(), got OnReceiveFrame(): {:?}", data));
            }
            DeviceEvent::OnReadyForSendFrames { number_of_frames } => {
                return Err(format_err!(
                    "Expect OnError(), got OnReadyForSendFrame(): {:?}",
                    number_of_frames
                ));
            }
            DeviceEvent::OnError { error, .. } => {
                if error != expected_err {
                    return Err(format_err!("Expect Err {:?}, got Err {:?}", error, expected_err));
                }
                return Ok(());
            }
        }
    }
    return Err(format_err!("No event"));
}

pub struct OtRadioDevice {
    pub tx_allowance: u32,
    pub rx_allowance: u32,
}

impl<'a> OtRadioDevice {
    pub async fn expect_one_rx_frame_event(
        &mut self,
        stream: &mut DeviceEventStream,
        num_of_event: u32,
    ) -> Result<Vec<u8>, anyhow::Error> {
        let mut res: Vec<u8> = vec![];
        fx_log_info!("waiting for {} frame", num_of_event);
        for _ in 0..num_of_event {
            if let Some(event) =
                stream.try_next().await.context("error waiting for spinel events")?
            {
                match event {
                    DeviceEvent::OnReceiveFrame { data } => {
                        res = data;
                        self.rx_allowance -= 1;
                    }
                    DeviceEvent::OnReadyForSendFrames { number_of_frames } => {
                        self.tx_allowance += number_of_frames;
                    }
                    DeviceEvent::OnError { error, .. } => {
                        return Err(format_err!("received error code {:?}", error));
                    }
                }
            }
        }
        return Ok(res);
    }
}
