// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_lowpan_spinel::{
        DeviceEvent, DeviceEventStream, DeviceProxy, Error as SpinelError,
    },
    fuchsia_async::futures::stream::TryStreamExt,
    fuchsia_syslog::macros::*,
};

pub const OT_RADIO_RESET_EVENT: &[u8; 3] = &[0x80, 0x06, 0x0];
pub const OT_RADIO_TX_ALLOWANCE_INIT_VAL: u32 = 4;
pub const OT_RADIO_TX_ALLOWANCE_LOW_THRESHOLD_VAL: u32 = 2;
pub const OT_RADIO_TX_ALLOWANCE_HIGH_THRESHOLD_VAL: u32 = 4;
pub const OT_RADIO_RX_ALLOWANCE_INIT_VAL: u32 = 4;
pub const OT_RADIO_RX_ALLOWANCE_INC_VAL: u32 = 2;
pub const OT_RADIO_GET_FRAME_AND_TX_ALLOWANCE: u32 = 2;
pub const OT_RADIO_GET_FRAME: u32 = 1;

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

pub fn validate_reset_frame(frame_slice: &[u8]) {
    assert_eq!(OT_RADIO_RESET_EVENT, &frame_slice[..3]);
    assert!((frame_slice[3] & 0xF0) == 0x70);
}

/// A LoWPAN Spinel Device Client implementation that uses for integration testing only.
pub struct LoWPANSpinelDeviceClientImpl {
    pub tx_allowance: u32,
    pub rx_allowance: u32,
    pub stream: DeviceEventStream,
    pub device_proxy: DeviceProxy,
}

impl<'a> LoWPANSpinelDeviceClientImpl {
    pub fn new(device_proxy_in: DeviceProxy) -> Self {
        let device_proxy_event_stream = device_proxy_in.take_event_stream();
        Self {
            tx_allowance: 0,
            rx_allowance: 0,
            stream: device_proxy_event_stream,
            device_proxy: device_proxy_in,
        }
    }

    pub async fn open_and_init_device(&mut self) {
        // Initialize ot-radio device.
        self.device_proxy.open().await.expect("sending FIDL req").expect("getting FIDL resp");

        // Get allowance for this component to send out frames
        self.tx_allowance =
            expect_on_ready_for_send_frame_event(&mut self.stream).await.expect("receiving frame");

        // Let spinel device know that this component is ready to receive frames
        self.device_proxy
            .ready_to_receive_frames(OT_RADIO_RX_ALLOWANCE_INIT_VAL)
            .expect("setting receive frame num");
        self.rx_allowance = OT_RADIO_RX_ALLOWANCE_INIT_VAL;

        // Receive and validate the reset event frame.
        let rx_frame = expect_on_receive_event(&mut self.stream).await.expect("receiving event");
        fx_log_info!("received frame {:?}", rx_frame);
        validate_reset_frame(&rx_frame);
        self.rx_allowance -= 1;
    }

    async fn expect_device_event_for_rx_frame(
        &mut self,
        num_of_event: u32,
    ) -> Result<Vec<u8>, anyhow::Error> {
        let mut res: Vec<u8> = vec![];
        for _ in 0..num_of_event {
            if let Some(event) =
                self.stream.try_next().await.context("error waiting for spinel events")?
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

    pub async fn receive_one_frame(&mut self) -> Result<Vec<u8>, anyhow::Error> {
        let ret_val: Vec<u8>;

        if self.rx_allowance == 0 {
            return Err(format_err!("no allowance for receiving frame"));
        }

        // If tx_allowance is in the lower threshold, the test component expects to receive an
        // OnReadyForSendFrame event and a frame. This prevents the component sending frames
        // too fast.
        if self.tx_allowance == OT_RADIO_TX_ALLOWANCE_LOW_THRESHOLD_VAL {
            ret_val =
                self.expect_device_event_for_rx_frame(OT_RADIO_GET_FRAME_AND_TX_ALLOWANCE).await?;
        } else {
            ret_val = self.expect_device_event_for_rx_frame(OT_RADIO_GET_FRAME).await?;
        }

        if !(self.tx_allowance > OT_RADIO_TX_ALLOWANCE_LOW_THRESHOLD_VAL
            && self.tx_allowance <= OT_RADIO_TX_ALLOWANCE_HIGH_THRESHOLD_VAL)
        {
            return Err(format_err!("invalid value for tx_allowance"));
        }

        // If rx_allowance is even, the component is ready to receive two more frames.
        if (self.rx_allowance & 1) == 0 {
            self.device_proxy.ready_to_receive_frames(OT_RADIO_RX_ALLOWANCE_INC_VAL)?;
            self.rx_allowance += OT_RADIO_RX_ALLOWANCE_INC_VAL;
        }

        return Ok(ret_val);
    }

    pub async fn send_one_frame(&mut self, frame: &[u8]) -> Result<(), anyhow::Error> {
        if self.tx_allowance == 0 {
            return Err(format_err!("no allowance for sending frame"));
        }

        self.device_proxy.send_frame(frame).context("sending frame")?;

        self.tx_allowance -= 1;

        return Ok(());
    }
}
