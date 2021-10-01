// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use fidl::prelude::*;
use fidl_fuchsia_hardware_audio::*;
use fidl_fuchsia_media as media;
use fuchsia_async as fasync;
use futures::{future::MaybeDone, StreamExt};
use log::info;
use std::sync::Arc;

use crate::driver::{ensure_dai_format_is_supported, ensure_pcm_format_is_supported};
use crate::DigitalAudioInterface;

pub struct DaiAudioDevice {
    /// Shared reference to the DigitalAudioInterface this device will run on.
    dai: Arc<DigitalAudioInterface>,
    /// True if the DAI is an input DAI (cached from `dai`)
    is_input: bool,
    /// The formats that this DAI will use for it's output and the PcmFormat that it
    /// requests/presents as an audio device.  None if not configured. If Some, the `dai` supports
    /// both options.
    configured_formats: Option<(DaiFormat, PcmFormat)>,
    dai_formats: Vec<DaiSupportedFormats>,
    pcm_formats: Vec<SupportedFormats>,
    config_stream_task: MaybeDone<fasync::Task<Result<(), Error>>>,
}

impl DaiAudioDevice {
    /// Builds an Audio device (input or output) from the DAI device.
    /// The format for the DAI device will be checked for compatibility with `dai_format` and that
    /// it can support an audio_core format of `audio_format`.
    pub async fn build(mut dai: DigitalAudioInterface) -> Result<Self, Error> {
        dai.connect()?;
        let props = dai.properties().await?;
        let dai_formats = dai.dai_formats().await?;
        let pcm_formats = dai.ring_buffer_formats().await?;
        Ok(Self {
            dai: Arc::new(dai),
            is_input: props.is_input.ok_or(format_err!("DAI did not provide required is_input"))?,
            configured_formats: None,
            dai_formats,
            pcm_formats,
            config_stream_task: MaybeDone::Gone,
        })
    }

    pub fn config(&mut self, dai_format: DaiFormat, pcm_format: PcmFormat) -> Result<(), Error> {
        ensure_dai_format_is_supported(&self.dai_formats[..], &dai_format)?;
        ensure_pcm_format_is_supported(&self.pcm_formats[..], &pcm_format)?;
        self.configured_formats = Some((dai_format, pcm_format));
        Ok(())
    }

    /// Register the audio device with audio enumerator `proxy`, connecting the DAI to the system,
    /// using `id`, `manufacturer` and `product` as identifiers.
    /// If the device is already registered, returns an error.  Otherwise it adds the device and
    /// starts responding appropriately to the Media system for the device.
    pub fn start(
        &mut self,
        proxy: media::AudioDeviceEnumeratorProxy,
        name: &str,
        id: [u8; 16],
        manufacturer: &str,
        product: &str,
    ) -> Result<(), Error> {
        if let MaybeDone::Future(_) = &self.config_stream_task {
            // Task is still running.
            return Err(format_err!("Attempted to start a DAI that is still running"));
        }
        let (dai_format, pcm_format) =
            self.configured_formats.clone().ok_or(format_err!("formats not configured"))?;
        let (client, request_stream) =
            fidl::endpoints::create_request_stream::<StreamConfigMarker>()?;
        self.config_stream_task =
            futures::future::maybe_done(fasync::Task::spawn(process_audio_requests(
                request_stream,
                self.dai.clone(),
                pcm_format,
                dai_format,
                id,
                manufacturer.to_string(),
                product.to_string(),
            )));
        Ok(proxy.add_device_by_channel(name, self.is_input, client)?)
    }

    /// Stop the device, removing it from the system. This method is idempotent, and the device
    /// can be started again with the same parameters.
    pub fn stop(&mut self) {
        self.config_stream_task = MaybeDone::Gone;
    }
}

/// Process the audio system requests for the digital audio interface, with the given `dai`
/// providing the audio ringbuffer.
async fn process_audio_requests(
    mut stream: StreamConfigRequestStream,
    dai: Arc<DigitalAudioInterface>,
    pcm_format: PcmFormat,
    dai_format: DaiFormat,
    unique_id: [u8; 16],
    manufacturer: String,
    product: String,
) -> Result<(), Error> {
    let dai_props = dai.properties().await?;
    let attributes = Some(vec![ChannelAttributes::EMPTY; pcm_format.number_of_channels as usize]);
    let channel_set = ChannelSet { attributes: attributes, ..ChannelSet::EMPTY };
    let supported_formats = PcmSupportedFormats {
        channel_sets: Some(vec![channel_set]),
        sample_formats: Some(vec![pcm_format.sample_format]),
        bytes_per_sample: Some(vec![pcm_format.bytes_per_sample]),
        valid_bits_per_sample: Some(vec![pcm_format.valid_bits_per_sample]),
        frame_rates: Some(vec![pcm_format.frame_rate]),
        ..PcmSupportedFormats::EMPTY
    };
    let mut gain_state_replied = false;
    let mut plug_state_replied = false;
    while let Some(request) = stream.next().await {
        if let Err(e) = request {
            info!(target: "dai_audio_device", "error from the request stream: {:?}", e);
            break;
        }
        match request.unwrap() {
            StreamConfigRequest::GetProperties { responder } => {
                let prop = StreamProperties {
                    unique_id: Some(unique_id.clone()),
                    is_input: dai_props.is_input,
                    can_mute: Some(false),
                    can_agc: Some(false),
                    min_gain_db: Some(0f32),
                    max_gain_db: Some(0f32),
                    gain_step_db: Some(0f32),
                    plug_detect_capabilities: Some(PlugDetectCapabilities::Hardwired),
                    manufacturer: Some(manufacturer.clone()),
                    product: Some(product.clone()),
                    clock_domain: Some(CLOCK_DOMAIN_MONOTONIC),
                    ..StreamProperties::EMPTY
                };

                responder.send(prop)?;
            }
            StreamConfigRequest::GetSupportedFormats { responder } => {
                let formats_vector = vec![SupportedFormats {
                    pcm_supported_formats: Some(supported_formats.clone()),
                    ..SupportedFormats::EMPTY
                }];
                responder.send(&mut formats_vector.into_iter())?;
            }
            StreamConfigRequest::CreateRingBuffer { format, ring_buffer, .. } => {
                dai.create_ring_buffer(dai_format, format, ring_buffer)?;
            }
            StreamConfigRequest::WatchGainState { responder } => {
                if gain_state_replied {
                    // We will never change gain state.
                    responder.drop_without_shutdown();
                    continue;
                }
                let gain_state = GainState {
                    muted: Some(false),
                    agc_enabled: Some(false),
                    gain_db: Some(0.0f32),
                    ..GainState::EMPTY
                };
                responder.send(gain_state)?;
                gain_state_replied = true;
            }
            StreamConfigRequest::SetGain { target_state, .. } => {
                // Setting gain is not supported.
                info!(target: "dai_audio_device", "Request to set gain ignored: {:?}", target_state);
            }
            StreamConfigRequest::WatchPlugState { responder } => {
                if plug_state_replied {
                    // We will never change plug state.
                    responder.drop_without_shutdown();
                    continue;
                }
                let time = fasync::Time::now();
                let plug_state = PlugState {
                    plugged: Some(true),
                    plug_state_time: Some(time.into_nanos() as i64),
                    ..PlugState::EMPTY
                };
                responder.send(plug_state)?;
                plug_state_replied = true;
            }
        }
    }
    info!(target: "dai_audio_device", "audio request stream processing ending");
    Ok(())
}

#[cfg(test)]
mod tests {
    use fidl_fuchsia_media::{AudioDeviceEnumeratorMarker, AudioDeviceEnumeratorRequest};
    use fixture::fixture;
    use fuchsia;
    use futures::{task::Context, Future};
    use std::pin::Pin;

    use super::*;
    use crate::test::test_digital_audio_interface;

    const SUPPORTED_DAI_FORMAT: DaiFormat = DaiFormat {
        number_of_channels: 1,
        channels_to_use_bitmask: 1,
        sample_format: DaiSampleFormat::PcmSigned,
        frame_format: DaiFrameFormat::FrameFormatStandard(DaiFrameFormatStandard::Tdm1),
        frame_rate: 16000,
        bits_per_slot: 16,
        bits_per_sample: 16,
    };

    const UNSUPPORTED_DAI_FORMAT: DaiFormat =
        DaiFormat { number_of_channels: 4, ..SUPPORTED_DAI_FORMAT };

    const SUPPORTED_PCM_FORMAT: PcmFormat = PcmFormat {
        number_of_channels: 1,
        channels_to_use_bitmask: 1,
        sample_format: SampleFormat::PcmSigned,
        bytes_per_sample: 2,
        valid_bits_per_sample: 16,
        frame_rate: 16000,
    };

    const UNSUPPORTED_PCM_FORMAT: PcmFormat =
        PcmFormat { number_of_channels: 4, ..SUPPORTED_PCM_FORMAT };

    async fn fix_audio_device<F, Fut>(_name: &str, test: F)
    where
        F: FnOnce(DaiAudioDevice) -> Fut,
        Fut: futures::Future<Output = ()>,
    {
        let (dai, _handle) = test_digital_audio_interface(true);
        let device = DaiAudioDevice::build(dai).await.expect("builds okay");
        test(device).await
    }

    #[fixture(fix_audio_device)]
    #[fuchsia::test]
    async fn configure_format_is_supported(mut dai: DaiAudioDevice) {
        let result = dai.config(UNSUPPORTED_DAI_FORMAT, SUPPORTED_PCM_FORMAT);
        assert!(result.is_err());

        let result = dai.config(SUPPORTED_DAI_FORMAT, UNSUPPORTED_PCM_FORMAT);
        assert!(result.is_err());

        let result = dai.config(SUPPORTED_DAI_FORMAT, SUPPORTED_PCM_FORMAT);
        assert!(result.is_ok());

        // Can reconfigure.
        let result = dai.config(SUPPORTED_DAI_FORMAT, SUPPORTED_PCM_FORMAT);
        assert!(result.is_ok());
    }

    const TEST_DEVICE_NAME: &'static str = &"Test device";
    const TEST_DEVICE_ID: &'static [u8; 16] =
        &[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15];
    const TEST_MANUFACTURER: &'static str = &"Spacely Sprockets";
    const TEST_PRODUCT: &'static str = &"💖 Test Fuchsia Device";

    async fn setup_audio_stream_proxy(dai: &mut DaiAudioDevice) -> StreamConfigProxy {
        let (proxy, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<AudioDeviceEnumeratorMarker>()
                .expect("proxy creation to work");

        let _ = dai.config(SUPPORTED_DAI_FORMAT, SUPPORTED_PCM_FORMAT).unwrap();

        let _ = dai
            .start(proxy, TEST_DEVICE_NAME, TEST_DEVICE_ID.clone(), TEST_MANUFACTURER, TEST_PRODUCT)
            .expect("Should start okay.");

        match requests.next().await {
            Some(Ok(AudioDeviceEnumeratorRequest::AddDeviceByChannel {
                device_name,
                is_input,
                channel,
                ..
            })) => {
                assert_eq!(&device_name, TEST_DEVICE_NAME, "name should match");
                assert_eq!(is_input, true, "should be an input device");
                channel.into_proxy().expect("make a proxy")
            }
            x => panic!("Expected AudioDevice to be added by channel, got {:?}", x),
        }
    }

    #[fixture(fix_audio_device)]
    #[fuchsia::test]
    async fn start_enumerator_registration(mut dai: DaiAudioDevice) {
        let (proxy, _requests) =
            fidl::endpoints::create_proxy_and_stream::<AudioDeviceEnumeratorMarker>()
                .expect("proxy creation to work");

        // start without config doesn't work
        let _ = dai
            .start(
                proxy.clone(),
                TEST_DEVICE_NAME,
                TEST_DEVICE_ID.clone(),
                TEST_MANUFACTURER,
                TEST_PRODUCT,
            )
            .expect_err("Should have been an Error setting up without configure");

        let stream_proxy = setup_audio_stream_proxy(&mut dai).await;

        let properties = stream_proxy.get_properties().await.expect("properties should return");
        assert_eq!(Some(true), properties.is_input);
        assert_eq!(TEST_DEVICE_ID, &properties.unique_id.expect("Unique ID should be set"));
        assert_eq!(
            TEST_MANUFACTURER,
            &properties.manufacturer.expect("manufacturer should be set")
        );
        assert_eq!(TEST_PRODUCT, &properties.product.expect("product should be set"));
    }

    #[fixture(fix_audio_device)]
    #[fuchsia::test]
    async fn stop_and_restart(mut dai: DaiAudioDevice) {
        let stream_proxy = setup_audio_stream_proxy(&mut dai).await;
        let _ = stream_proxy.get_properties().await.expect("properties should return");
        dai.stop();

        let e = stream_proxy.get_properties().await.expect_err("should have closed audio config");
        assert!(matches!(e, fidl::Error::ClientChannelClosed { .. }));

        let stream_proxy = setup_audio_stream_proxy(&mut dai).await;
        let _ = stream_proxy.get_properties().await.expect("properties should return");
    }

    #[fixture(fix_audio_device)]
    #[fuchsia::test]
    async fn reports_correct_supported_formats(mut dai: DaiAudioDevice) {
        let stream_proxy = setup_audio_stream_proxy(&mut dai).await;

        let mut supported_formats =
            stream_proxy.get_supported_formats().await.expect("Result from supported formats");

        assert_eq!(1, supported_formats.len());
        let pcm_formats = supported_formats
            .pop()
            .expect("should be a formats")
            .pcm_supported_formats
            .expect("pcm supported formats");
        assert_eq!(
            SUPPORTED_PCM_FORMAT.number_of_channels as usize,
            pcm_formats.channel_sets.unwrap()[0].attributes.as_ref().unwrap().len()
        );
        assert_eq!(&[SUPPORTED_PCM_FORMAT.sample_format], &pcm_formats.sample_formats.unwrap()[..]);
        assert_eq!(
            &[SUPPORTED_PCM_FORMAT.bytes_per_sample],
            &pcm_formats.bytes_per_sample.unwrap()[..]
        );
        assert_eq!(
            &[SUPPORTED_PCM_FORMAT.valid_bits_per_sample],
            &pcm_formats.valid_bits_per_sample.unwrap()[..]
        );
        assert_eq!(&[SUPPORTED_PCM_FORMAT.frame_rate], &pcm_formats.frame_rates.unwrap()[..]);
    }

    #[fixture(fix_audio_device)]
    #[fuchsia::test]
    async fn ringbuffer_starts_dai_without_error(mut dai: DaiAudioDevice) {
        let stream_proxy = setup_audio_stream_proxy(&mut dai).await;

        let (rb_proxy, server_end) =
            fidl::endpoints::create_proxy::<RingBufferMarker>().expect("proxy creation");

        let format = fidl_fuchsia_hardware_audio::Format {
            pcm_format: Some(SUPPORTED_PCM_FORMAT.clone()),
            ..fidl_fuchsia_hardware_audio::Format::EMPTY
        };
        stream_proxy.create_ring_buffer(format, server_end).expect("setup ring buffer okay");

        let start_time = rb_proxy.start().await;
        assert!(start_time.is_ok());
    }

    #[fixture(fix_audio_device)]
    #[fuchsia::test]
    async fn gain_state_hanging_get(mut dai: DaiAudioDevice) {
        let stream_proxy = setup_audio_stream_proxy(&mut dai).await;

        let gain_state = stream_proxy.watch_gain_state().await;
        let gain_state = gain_state.expect("should have received response from GainState");
        // We have no gain.
        assert_eq!(Some(0.0f32), gain_state.gain_db);

        // another gain state should be fine, but it will never reply.
        let mut gain_state_fut =
            stream_proxy.watch_gain_state().check().expect("message sends fine");

        // TODO(fxbug.dev/74427): FIDL client wakes up everything now. Once it's smarter at
        // waking up the right future, use a panic waker here.
        // We never expect to be woken, panic if we do.
        // let panic_waker = futures_test::task::panic_waker();

        assert!(Pin::new(&mut gain_state_fut)
            .poll(&mut Context::from_waker(futures::task::noop_waker_ref()))
            .is_pending());

        // Set gain state is ignored.  Shouldn't wake us up.
        let _ = stream_proxy
            .set_gain(GainState { muted: Some(true), ..GainState::EMPTY })
            .expect("set gain");
        assert!(Pin::new(&mut gain_state_fut)
            .poll(&mut Context::from_waker(futures::task::noop_waker_ref()))
            .is_pending());
        // Can do other things and they can succeed while this is in hanging state.
        let _ = stream_proxy.get_properties().await.expect("properties");
        assert!(Pin::new(&mut gain_state_fut)
            .poll(&mut Context::from_waker(futures::task::noop_waker_ref()))
            .is_pending());
    }

    #[fixture(fix_audio_device)]
    #[fuchsia::test]
    async fn plug_state_hanging_get(mut dai: DaiAudioDevice) {
        let stream_proxy = setup_audio_stream_proxy(&mut dai).await;

        let plug_state = stream_proxy.watch_plug_state().await;
        let plug_state = plug_state.expect("should have received response from PlugState");
        // We are permanently plugged in.
        assert_eq!(Some(true), plug_state.plugged);

        // another gain state should be fine, but it will never reply.
        let mut plug_state_fut =
            stream_proxy.watch_plug_state().check().expect("message sends fine");

        // TODO(fxbug.dev/74427): FIDL client wakes up everything now. Once it's smarter at
        // waking up the right future, use a panic waker here.
        // We never expect to be woken, panic if we do.
        // let panic_waker = futures_test::task::panic_waker();

        assert!(Pin::new(&mut plug_state_fut)
            .poll(&mut Context::from_waker(futures::task::noop_waker_ref()))
            .is_pending());

        // Can do other things and they can succeed while this is in hanging state.
        let _ = stream_proxy.get_properties().await.expect("properties");
        assert!(Pin::new(&mut plug_state_fut)
            .poll(&mut Context::from_waker(futures::task::noop_waker_ref()))
            .is_pending());
    }
}
