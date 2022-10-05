// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::format_err;
use fidl_fuchsia_bluetooth_bredr as bredr;
use fidl_fuchsia_hardware_audio::{self as audio, DaiFormat, PcmFormat};
use fidl_fuchsia_media as media;
use fuchsia_audio_dai::{self as dai, DaiAudioDevice, DigitalAudioInterface};
use fuchsia_bluetooth::types::{peer_audio_stream_id, PeerId, Uuid};
use thiserror::Error;
use tracing::{info, warn};

#[derive(Error, Debug)]
pub enum AudioError {
    #[error("Parameters aren't supported {:?}", .source)]
    UnsupportedParameters { source: anyhow::Error },
    #[error("Audio is already started")]
    AlreadyStarted,
    #[error("AudioCore Error: {:?}", .source)]
    AudioCore { source: anyhow::Error },
    #[error("Audio is not started")]
    NotStarted,
    #[error("Could not find suitable devices")]
    DiscoveryFailed,
}

impl AudioError {
    fn audio_core(e: anyhow::Error) -> Self {
        Self::AudioCore { source: e }
    }
}

pub trait AudioControl: Send {
    /// Start the audio, adding the audio device to the audio core and routing audio.
    fn start(
        &mut self,
        id: PeerId,
        params: bredr::ScoConnectionParameters,
    ) -> Result<(), AudioError>;

    /// Stop the audio, removing audio devices from the audio core.
    /// If the Audio is not started, this returns Err(AudioError::NotStarted).
    fn stop(&mut self) -> Result<(), AudioError>;
}

pub struct DaiAudioControl {
    output: DaiAudioDevice,
    input: DaiAudioDevice,
    audio_core: media::AudioDeviceEnumeratorProxy,
    started: bool,
}

impl DaiAudioControl {
    pub async fn discover() -> Result<Self, AudioError> {
        let devices = dai::find_devices().await.or(Err(AudioError::DiscoveryFailed))?;
        let proxy = fuchsia_component::client::connect_to_protocol::<
            media::AudioDeviceEnumeratorMarker,
        >()
        .or(Err(AudioError::AudioCore { source: format_err!("Failed to connect to protocol") }))?;
        Self::setup(devices, proxy).await
    }

    // Public for tests within the crate
    pub(crate) async fn setup(
        devices: Vec<DigitalAudioInterface>,
        proxy: media::AudioDeviceEnumeratorProxy,
    ) -> Result<Self, AudioError> {
        let mut input_dai = None;
        let mut output_dai = None;

        for mut device in devices {
            if device.connect().is_err() {
                continue;
            }
            let props = match device.properties().await {
                Err(e) => {
                    warn!("Couldn't find properties for device: {:?}", e);
                    continue;
                }
                Ok(props) => props,
            };
            let dai_input = props.is_input.ok_or(AudioError::DiscoveryFailed)?;
            if input_dai.is_none() && dai_input {
                let dai_device = DaiAudioDevice::build(device).await.or_else(|e| {
                    warn!("Couldn't build a dai audio device: {:?}", e);
                    Err(AudioError::DiscoveryFailed)
                })?;
                input_dai = Some(dai_device);
            } else if output_dai.is_none() && !dai_input {
                let dai_device = DaiAudioDevice::build(device).await.or_else(|e| {
                    warn!("Couldn't build a dai audio device: {:?}", e);
                    Err(AudioError::DiscoveryFailed)
                })?;
                output_dai = Some(dai_device);
            }

            if input_dai.is_some() && output_dai.is_some() {
                return Ok(Self::build(proxy, input_dai.unwrap(), output_dai.unwrap()));
            }
        }

        info!("Couldn't find the correct combination of DAI devices");
        Err(AudioError::DiscoveryFailed)
    }

    pub fn build(
        audio_core: media::AudioDeviceEnumeratorProxy,
        input: DaiAudioDevice,
        output: DaiAudioDevice,
    ) -> Self {
        Self { input, output, audio_core, started: false }
    }

    const HF_INPUT_UUID: Uuid = Uuid::new16(bredr::ServiceClassProfileIdentifier::Handsfree as u16);
    const HF_OUTPUT_UUID: Uuid =
        Uuid::new16(bredr::ServiceClassProfileIdentifier::HandsfreeAudioGateway as u16);

    fn start_device(
        &mut self,
        peer_id: &PeerId,
        input: bool,
        dai_format: DaiFormat,
        pcm_format: PcmFormat,
    ) -> Result<(), anyhow::Error> {
        let (uuid, dev) = if input {
            (Self::HF_INPUT_UUID, &mut self.input)
        } else {
            (Self::HF_OUTPUT_UUID, &mut self.output)
        };

        let dev_id = peer_audio_stream_id(*peer_id, uuid);
        dev.config(dai_format, pcm_format)?;
        dev.start(self.audio_core.clone(), "HFP Audio", dev_id, "Fuchsia", "Sapphire HFP Headset")
    }
}

impl AudioControl for DaiAudioControl {
    fn start(
        &mut self,
        id: PeerId,
        params: bredr::ScoConnectionParameters,
    ) -> Result<(), AudioError> {
        if self.started {
            return Err(AudioError::AlreadyStarted);
        }
        // I/O bandwidth is matched to frame rate but includes both source and sink, so the
        // audio frame rate is half that.
        let frame_rate = match params.io_bandwidth {
            Some(32000) => 16000,
            Some(16000) => 8000,
            _ => {
                return Err(AudioError::UnsupportedParameters {
                    source: format_err!("Unsupported frame_rate"),
                })
            }
        };
        let dai_format = DaiFormat {
            number_of_channels: 1,
            channels_to_use_bitmask: 0x1,
            sample_format: audio::DaiSampleFormat::PcmSigned,
            frame_format: audio::DaiFrameFormat::FrameFormatStandard(
                audio::DaiFrameFormatStandard::Tdm1,
            ),
            frame_rate,
            bits_per_slot: 16,
            bits_per_sample: 16,
        };
        let pcm_format = PcmFormat {
            number_of_channels: 1,
            sample_format: audio::SampleFormat::PcmSigned,
            bytes_per_sample: 2,
            valid_bits_per_sample: 16,
            frame_rate,
        };
        self.start_device(&id, true, dai_format.clone(), pcm_format.clone())
            .map_err(AudioError::audio_core)?;
        if let Err(e) = self.start_device(&id, false, dai_format, pcm_format) {
            // Stop the input device, so we have only two states: started and not started.
            self.input.stop();
            return Err(AudioError::audio_core(e));
        }
        self.started = true;
        Ok(())
    }

    fn stop(&mut self) -> Result<(), AudioError> {
        if !self.started {
            return Err(AudioError::NotStarted);
        }
        self.output.stop();
        self.input.stop();
        self.started = false;
        Ok(())
    }
}

#[derive(Default)]
pub struct TestAudioControl {
    started: bool,
}

impl AudioControl for TestAudioControl {
    fn start(
        &mut self,
        _id: PeerId,
        _params: bredr::ScoConnectionParameters,
    ) -> Result<(), AudioError> {
        if self.started {
            return Err(AudioError::AlreadyStarted);
        }
        self.started = true;
        Ok(())
    }

    fn stop(&mut self) -> Result<(), AudioError> {
        if !self.started {
            return Err(AudioError::NotStarted);
        }
        self.started = false;
        Ok(())
    }
}

#[cfg(test)]
mod test {
    use fidl::endpoints::Proxy;
    use fuchsia_async as fasync;
    use futures::{channel::mpsc, SinkExt, StreamExt};

    use crate::features::CodecId;
    use crate::sco_connector::parameter_sets_for_codec;

    use super::*;

    #[fuchsia::test]
    async fn fails_if_all_devices_not_found() {
        let (proxy, _requests) =
            fidl::endpoints::create_proxy_and_stream::<media::AudioDeviceEnumeratorMarker>()
                .expect("endpoints");

        let setup_result = DaiAudioControl::setup(vec![], proxy.clone()).await;
        assert!(matches!(setup_result, Err(AudioError::DiscoveryFailed)));

        let (input, _handle) = dai::test::test_digital_audio_interface(true);
        let setup_result = DaiAudioControl::setup(vec![input], proxy.clone()).await;
        assert!(matches!(setup_result, Err(AudioError::DiscoveryFailed)));

        let (output, _handle) = dai::test::test_digital_audio_interface(false);
        let setup_result = DaiAudioControl::setup(vec![output], proxy.clone()).await;
        assert!(matches!(setup_result, Err(AudioError::DiscoveryFailed)));

        let (input, _handle) = dai::test::test_digital_audio_interface(true);
        let (output, _handle) = dai::test::test_digital_audio_interface(false);
        let _audio = DaiAudioControl::setup(vec![input, output], proxy.clone()).await.unwrap();
    }

    struct TestAudioClient {
        _name: String,
        is_input: bool,
        client: audio::StreamConfigProxy,
    }

    impl TestAudioClient {
        async fn start_up(&self) -> Result<audio::RingBufferProxy, fidl::Error> {
            let _prop = self.client.get_properties().await?;
            let formats = self.client.get_supported_formats().await?;
            // Pick the first one, why not.
            let pcm_formats = formats.first().unwrap().pcm_supported_formats.as_ref().unwrap();
            let pcm_format = Some(PcmFormat {
                number_of_channels: pcm_formats.channel_sets.as_ref().unwrap()[0]
                    .attributes
                    .as_ref()
                    .unwrap()
                    .len() as u8,
                sample_format: pcm_formats.sample_formats.as_ref().unwrap()[0],
                bytes_per_sample: pcm_formats.bytes_per_sample.as_ref().unwrap()[0],
                valid_bits_per_sample: pcm_formats.valid_bits_per_sample.as_ref().unwrap()[0],
                frame_rate: pcm_formats.frame_rates.as_ref().unwrap()[0],
            });
            let (proxy, server_end) = fidl::endpoints::create_proxy()?;
            self.client.create_ring_buffer(
                audio::Format { pcm_format, ..audio::Format::EMPTY },
                server_end,
            )?;
            Ok(proxy)
        }
    }

    async fn handle_audio_requests(
        mut requests: media::AudioDeviceEnumeratorRequestStream,
        mut stream_proxies: mpsc::Sender<TestAudioClient>,
    ) {
        while let Some(req) = requests.next().await {
            match req.expect("AudioDeviceEnumerator stream error: {:?}") {
                media::AudioDeviceEnumeratorRequest::AddDeviceByChannel {
                    device_name,
                    is_input,
                    channel,
                    ..
                } => {
                    let dev = TestAudioClient {
                        _name: device_name.to_owned(),
                        is_input,
                        client: channel.into_proxy().unwrap(),
                    };
                    if let Err(e) = stream_proxies.feed(dev).await {
                        panic!("Couldn't send new device: {:?}", e);
                    }
                }
                x => unimplemented!("Got unimplemented AudioDeviceEnumerator: {:?}", x),
            }
        }
    }

    #[fuchsia::test]
    async fn starts_dai() {
        let (proxy, audio_requests) =
            fidl::endpoints::create_proxy_and_stream::<media::AudioDeviceEnumeratorMarker>()
                .expect("endpoints");

        let (send, mut new_client_recv) = mpsc::channel(1);
        let _audio_req_task = fasync::Task::spawn(handle_audio_requests(audio_requests, send));

        let (input, _input_handle) = dai::test::test_digital_audio_interface(true);
        let (output, _output_handle) = dai::test::test_digital_audio_interface(false);
        let mut audio = DaiAudioControl::setup(vec![input, output], proxy.clone()).await.unwrap();

        let sco_params = parameter_sets_for_codec(CodecId::CVSD).pop().unwrap();

        let result = audio.start(PeerId(0), sco_params.clone());
        result.expect("audio should start okay");

        // Expect new audio devices that are output and input.
        let audio_client_one = new_client_recv.next().await.expect("new audio device");
        let audio_client_two = new_client_recv.next().await.expect("new audio device");
        assert!(audio_client_one.is_input != audio_client_two.is_input, "input and output");

        let result = audio.start(PeerId(0), sco_params);
        let _ = result.expect_err("Starting an already started source is an error");
    }

    #[fuchsia::test]
    async fn stop_dai() {
        let (proxy, audio_requests) =
            fidl::endpoints::create_proxy_and_stream::<media::AudioDeviceEnumeratorMarker>()
                .expect("endpoints");

        let (send, mut new_client_recv) = mpsc::channel(1);
        let _audio_req_task = fasync::Task::spawn(handle_audio_requests(audio_requests, send));

        let (input, input_handle) = dai::test::test_digital_audio_interface(true);
        let (output, output_handle) = dai::test::test_digital_audio_interface(false);
        let mut audio = DaiAudioControl::setup(vec![input, output], proxy.clone()).await.unwrap();

        let _ = audio.stop().expect_err("stopping without starting is an error");

        let sco_params = parameter_sets_for_codec(CodecId::CVSD).pop().unwrap();
        let result = audio.start(PeerId(0), sco_params);
        result.expect("audio should start okay");

        // Expect a new audio devices that we can start.
        let audio_client_one = new_client_recv.next().await.expect("new audio device");
        let rb_one = audio_client_one.start_up().await.expect("should be able to start");
        let _start_time = rb_one.start().await.expect("DAI ringbuffer should start okay");
        let audio_client_two = new_client_recv.next().await.expect("new audio device");
        let rb_two = audio_client_two.start_up().await.expect("should be able to start");
        let _start_time = rb_two.start().await.expect("DAI ringbuffer should start okay");

        assert!(output_handle.is_started(), "Output DAI should be started");
        assert!(input_handle.is_started(), "Input DAI should be started");

        // Stopping should close the audio client.
        audio.stop().expect("audio should stop okay");
        let _audio_closed = audio_client_one.client.on_closed().await;
        let _audio_closed = audio_client_two.client.on_closed().await;
    }
}
