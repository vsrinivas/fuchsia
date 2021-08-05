// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::format_err;
use derivative::Derivative;
use fidl_fuchsia_hardware_audio::*;
use fuchsia_async as fasync;
use futures::{Future, StreamExt};
use std::sync::{Arc, Mutex};
use vfs::{directory::entry::DirectoryEntry, mut_pseudo_directory, service};

use crate::driver::{ensure_dai_format_is_supported, ensure_pcm_format_is_supported};
use crate::DigitalAudioInterface;

/// The status of the current device.  Retrievable via `TestHandle::status`.
#[derive(Derivative, Clone, Debug)]
#[derivative(Default)]
pub enum TestStatus {
    #[derivative(Default)]
    Idle,
    Configured {
        dai_format: DaiFormat,
        pcm_format: PcmFormat,
    },
    Started {
        dai_format: DaiFormat,
        pcm_format: PcmFormat,
    },
}

impl TestStatus {
    fn start(&mut self) -> Result<(), ()> {
        if let Self::Configured { dai_format, pcm_format } = self {
            *self = Self::Started { dai_format: *dai_format, pcm_format: *pcm_format };
            Ok(())
        } else {
            Err(())
        }
    }

    fn stop(&mut self) {
        if let Self::Started { dai_format, pcm_format } = *self {
            *self = Self::Configured { dai_format, pcm_format };
        }
    }
}

#[derive(Clone)]
pub struct TestHandle(Arc<Mutex<TestStatus>>);

impl TestHandle {
    pub fn new() -> Self {
        Self(Arc::new(Mutex::new(TestStatus::default())))
    }

    pub fn status(&self) -> TestStatus {
        self.0.lock().unwrap().clone()
    }

    pub fn is_started(&self) -> bool {
        let lock = self.0.lock().unwrap();
        match *lock {
            TestStatus::Started { .. } => true,
            _ => false,
        }
    }

    fn set_configured(&self, dai_format: DaiFormat, pcm_format: PcmFormat) {
        let mut lock = self.0.lock().unwrap();
        *lock = TestStatus::Configured { dai_format, pcm_format };
    }

    fn start(&self) -> Result<(), ()> {
        self.0.lock().unwrap().start()
    }

    fn stop(&self) {
        self.0.lock().unwrap().stop()
    }
}

/// Logs and breaks out of the loop if the result is an Error.
macro_rules! log_error {
    ($result:expr, $tag:expr) => {
        if let Err(e) = $result {
            log::warn!("Error sending {}: {:?}", $tag, e);
            break;
        }
    };
}

async fn handle_ring_buffer(mut requests: RingBufferRequestStream, handle: TestHandle) {
    while let Some(req) = requests.next().await {
        if let Err(e) = req {
            log::warn!("Error processing RingBuffer request stream: {:?}", e);
            break;
        }
        match req.unwrap() {
            RingBufferRequest::Start { responder } => match handle.start() {
                Ok(()) => log_error!(responder.send(0), "ring buffer start response"),
                Err(()) => {
                    log::warn!("Started when we couldn't expect it, shutting down");
                }
            },
            RingBufferRequest::Stop { responder } => {
                handle.stop();
                log_error!(responder.send(), "ring buffer stop response");
            }
            x => unimplemented!("RingBuffer Request not implemented: {:?}", x),
        };
    }
}

async fn test_handle_dai_requests(
    dai_formats: DaiSupportedFormats,
    pcm_formats: SupportedFormats,
    as_input: bool,
    mut requests: DaiRequestStream,
    handle: TestHandle,
) {
    use std::slice::from_ref;
    let properties = DaiProperties {
        is_input: Some(as_input),
        manufacturer: Some("Fuchsia".to_string()),
        ..DaiProperties::EMPTY
    };

    let mut _rb_task = None;
    while let Some(req) = requests.next().await {
        if let Err(e) = req {
            log::warn!("Error processing DAI request stream: {:?}", e);
            break;
        }
        match req.unwrap() {
            DaiRequest::GetProperties { responder } => {
                log_error!(responder.send(properties.clone()), "properties response");
            }
            DaiRequest::GetDaiFormats { responder } => {
                log_error!(responder.send(&mut Ok(vec![dai_formats.clone()])), "formats response");
            }
            DaiRequest::GetRingBufferFormats { responder } => {
                log_error!(responder.send(&mut Ok(vec![pcm_formats.clone()])), "pcm response");
            }
            DaiRequest::CreateRingBuffer {
                dai_format, ring_buffer_format, ring_buffer, ..
            } => {
                let shutdown_bad_args =
                    |server: fidl::endpoints::ServerEnd<RingBufferMarker>, err: anyhow::Error| {
                        log::warn!("CreateRingBuffer: {:?}", err);
                        if let Err(e) =
                            server.close_with_epitaph(fuchsia_zircon::Status::INVALID_ARGS)
                        {
                            log::warn!("Couldn't send ring buffer epitaph: {:?}", e);
                        }
                    };
                if let Err(e) = ensure_dai_format_is_supported(from_ref(&dai_formats), &dai_format)
                {
                    shutdown_bad_args(ring_buffer, e);
                    continue;
                }
                let pcm_format = match ring_buffer_format.pcm_format {
                    Some(f) => f,
                    None => {
                        shutdown_bad_args(ring_buffer, format_err!("Only PCM format supported"));
                        continue;
                    }
                };
                if let Err(e) = ensure_pcm_format_is_supported(from_ref(&pcm_formats), &pcm_format)
                {
                    shutdown_bad_args(ring_buffer, e);
                    continue;
                }
                handle.set_configured(dai_format, pcm_format);
                let requests = ring_buffer.into_stream().expect("stream from server end");
                _rb_task = Some(fasync::Task::spawn(handle_ring_buffer(requests, handle.clone())));
            }
            x => unimplemented!("DAI request not implemented: {:?}", x),
        };
    }
}

/// Represents a mock DAI device that processes `Dai` requests from the provided `requests`
/// stream.
/// Returns a Future representing the processing task and a `TestHandle` which can be used
/// to validate behavior.
fn mock_dai_device(
    as_input: bool,
    requests: DaiRequestStream,
) -> (impl Future<Output = ()>, TestHandle) {
    let supported_dai_formats = DaiSupportedFormats {
        number_of_channels: vec![1],
        sample_formats: vec![DaiSampleFormat::PcmSigned],
        frame_formats: vec![DaiFrameFormat::FrameFormatStandard(DaiFrameFormatStandard::Tdm1)],
        frame_rates: vec![8000, 16000, 32000, 48000, 96000],
        bits_per_slot: vec![16],
        bits_per_sample: vec![16],
    };

    let number_of_channels = 1usize;
    let attributes = vec![ChannelAttributes::EMPTY; number_of_channels];
    let channel_set = ChannelSet { attributes: Some(attributes), ..ChannelSet::EMPTY };
    let supported_pcm_formats = SupportedFormats {
        pcm_supported_formats2: Some(PcmSupportedFormats2 {
            channel_sets: Some(vec![channel_set]),
            sample_formats: Some(vec![SampleFormat::PcmSigned]),
            bytes_per_sample: Some(vec![2]),
            valid_bits_per_sample: Some(vec![16]),
            frame_rates: Some(vec![8000, 16000, 32000, 48000, 96000]),
            ..PcmSupportedFormats2::EMPTY
        }),
        ..SupportedFormats::EMPTY
    };

    let handle = TestHandle::new();

    let handler_fut = test_handle_dai_requests(
        supported_dai_formats,
        supported_pcm_formats,
        as_input,
        requests,
        handle.clone(),
    );
    (handler_fut, handle)
}

/// Builds and returns a DigitalAudioInterface for testing scenarios. Returns the
/// `TestHandle` associated with this device which can be used to validate behavior.
pub fn test_digital_audio_interface(as_input: bool) -> (DigitalAudioInterface, TestHandle) {
    let (proxy, requests) =
        fidl::endpoints::create_proxy_and_stream::<DaiMarker>().expect("proxy creation");

    let (handler, handle) = mock_dai_device(as_input, requests);
    fasync::Task::spawn(handler).detach();

    (DigitalAudioInterface::from_proxy(proxy), handle)
}

async fn handle_dai_connect_requests(as_input: bool, mut stream: DaiConnectRequestStream) {
    while let Some(request) = stream.next().await {
        if let Ok(DaiConnectRequest::Connect { dai_protocol, .. }) = request {
            let (handler, _test_handle) =
                mock_dai_device(as_input, dai_protocol.into_stream().unwrap());
            fasync::Task::spawn(handler).detach();
        }
    }
}

/// Builds and returns a VFS with a mock input and output DAI device.
pub fn mock_dai_dev_with_io_devices(input: String, output: String) -> Arc<dyn DirectoryEntry> {
    mut_pseudo_directory! {
        "class" => mut_pseudo_directory! {
            "dai" => mut_pseudo_directory! {
                &input => service::host(
                    move |stream: DaiConnectRequestStream| handle_dai_connect_requests(true, stream)
                ),
                &output => service::host(
                    move |stream: DaiConnectRequestStream| handle_dai_connect_requests(false, stream)
                ),
            }
        }
    }
}
