// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

use fidl_fuchsia_lowpan_spinel::DeviceEvent as SpinelDeviceEvent;
use fidl_fuchsia_lowpan_spinel::{DeviceCloseResult, DeviceOpenResult, DeviceProxyInterface};
use futures::channel::mpsc;
use futures::future::{ready, Ready};
use std::sync::Arc;
use std::sync::Mutex;

#[derive(Debug, Clone, Eq, PartialEq)]
pub enum DeviceRequest {
    Open,
    Close,
    GetMaxFrameSize,
    SendFrame(Vec<u8>),
    ReadyToReceiveFrames(u32),
}

pub fn new_mock_spinel_pair(
    max_frame_size: u32,
) -> (
    SpinelDeviceSink<MockDeviceProxy>,
    SpinelDeviceStream<MockDeviceProxy, mpsc::Receiver<Result<SpinelDeviceEvent, fidl::Error>>>,
    mpsc::Sender<Result<SpinelDeviceEvent, fidl::Error>>,
    mpsc::Receiver<DeviceRequest>,
) {
    let (device_event_sender, device_event_receiver) = mpsc::channel(10);
    let (device_proxy, device_request_receiver) = MockDeviceProxy::new(max_frame_size);
    let pair = new_spinel_device_pair(device_proxy, device_event_receiver);
    (pair.0, pair.1, device_event_sender, device_request_receiver)
}

#[derive(Clone)]
pub struct MockDeviceProxy {
    pub sender: Arc<Mutex<mpsc::Sender<DeviceRequest>>>,
    pub max_frame_size: u32,
}

impl MockDeviceProxy {
    pub fn new(max_frame_size: u32) -> (Self, mpsc::Receiver<DeviceRequest>) {
        let (sender, receiver) = mpsc::channel(10);
        (Self { sender: Arc::new(Mutex::new(sender)), max_frame_size }, receiver)
    }
}

impl DeviceProxyInterface for MockDeviceProxy {
    type OpenResponseFut = Ready<Result<DeviceOpenResult, fidl::Error>>;
    fn open(&self) -> Self::OpenResponseFut {
        let ret = self
            .sender
            .lock()
            .unwrap()
            .start_send(DeviceRequest::Open)
            .map_err(|_| fidl_fuchsia_lowpan_spinel::Error::IoError);
        ready(Ok(ret))
    }

    type CloseResponseFut = Ready<Result<DeviceCloseResult, fidl::Error>>;
    fn close(&self) -> Self::CloseResponseFut {
        let ret = self
            .sender
            .lock()
            .unwrap()
            .start_send(DeviceRequest::Close)
            .map_err(|_| fidl_fuchsia_lowpan_spinel::Error::IoError);
        ready(Ok(ret))
    }

    type GetMaxFrameSizeResponseFut = Ready<Result<u32, fidl::Error>>;
    fn get_max_frame_size(&self) -> Self::GetMaxFrameSizeResponseFut {
        let ret = self
            .sender
            .lock()
            .unwrap()
            .start_send(DeviceRequest::GetMaxFrameSize)
            .map_err(|_| fidl::Error::ClientChannelClosed {
                status: ZxStatus::PEER_CLOSED,
                protocol_name: "mock",
            })
            .map(|()| self.max_frame_size);

        ready(ret)
    }

    fn send_frame(&self, data: &[u8]) -> Result<(), fidl::Error> {
        self.sender.lock().unwrap().start_send(DeviceRequest::SendFrame(data.to_vec())).map_err(
            |_| fidl::Error::ClientChannelClosed {
                status: ZxStatus::PEER_CLOSED,
                protocol_name: "mock",
            },
        )
    }

    fn ready_to_receive_frames(&self, number_of_frames: u32) -> Result<(), fidl::Error> {
        self.sender
            .lock()
            .unwrap()
            .start_send(DeviceRequest::ReadyToReceiveFrames(number_of_frames))
            .map_err(|_| fidl::Error::ClientChannelClosed {
                status: ZxStatus::PEER_CLOSED,
                protocol_name: "mock",
            })
    }

    type OnReadyForSendFramesResponseFut = futures::future::Pending<Result<u32, fidl::Error>>;

    type OnReceiveFrameResponseFut = futures::future::Pending<Result<Vec<u8>, fidl::Error>>;

    type OnErrorResponseFut =
        futures::future::Pending<Result<(fidl_fuchsia_lowpan_spinel::Error, bool), fidl::Error>>;
}
