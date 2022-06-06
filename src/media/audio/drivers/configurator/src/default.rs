// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(andresoportus): Remove this as usage of it is added.
#![allow(dead_code)]

use {
    crate::{
        config::{Config, Device},
        configurator::Configurator,
        indexes::StreamConfigIndex,
    },
    anyhow::{anyhow, Context, Error},
    async_trait::async_trait,
    fidl::prelude::*,
    fidl_fuchsia_hardware_audio::*,
    fidl_fuchsia_media::AudioDeviceEnumeratorMarker,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{lock::Mutex, select, StreamExt},
    std::collections::HashMap,
    std::sync::Arc,
};

pub struct CodecState {
    /// Codec manufacturer name.
    manufacturer: String,

    /// Codec product name.
    product: String,

    /// Formats supported by the Codec driver.
    supported_formats: Vec<DaiSupportedFormats>,

    /// Codec interface in use.
    proxy: CodecProxy,
}

pub struct DaiState {
    /// DAI manufacturer name.
    manufacturer: String,

    /// DAI product name.
    product: String,

    /// Formats supported by the DAI driver.
    supported_formats: Vec<DaiSupportedFormats>,

    /// DAI interface in use.
    interface: crate::dai::DaiInterface,
}

#[derive(Default)]
pub struct StreamConfigInner {
    /// Have we replied to the very first gain state watch.
    gain_state_first_watch_replied: bool,

    /// Have we replied to the very first plug state watch.
    plug_state_first_watch_replied: bool,

    /// Needs a codec to work.
    codec_needed: bool,

    /// Codec drivers states.
    codec_states: Vec<CodecState>,

    /// DAI driver state.
    dai_state: Option<DaiState>,

    /// DAI format used.
    dai_format: Option<DaiFormat>,

    /// Ring buffer format used.
    ring_buffer_format: Option<Format>,

    /// Ring buffer server end.
    ring_buffer: Option<fidl::endpoints::ServerEnd<RingBufferMarker>>,
}

pub struct StreamConfig {
    /// Handle to remove the audio device processing future when this is dropped.
    control_handle: StreamConfigControlHandle,

    /// StreamConfig properties.
    properties: StreamProperties,

    /// Stream to handle StreamConfig API protocol.
    stream: StreamConfigRequestStream,

    /// Client end.
    client: Option<fidl::endpoints::ClientEnd<StreamConfigMarker>>,

    /// Inner state.
    inner: Arc<Mutex<StreamConfigInner>>,
}

impl Drop for StreamConfig {
    fn drop(&mut self) {
        self.control_handle.shutdown();
    }
}

impl StreamConfig {
    async fn process_requests(mut self) {
        loop {
            select! {
                stream_config_request = self.stream.next() => {
                    match stream_config_request {
                        Some(Ok(request)) => {
                            if let Err(e) = self.handle_stream_request(request).await {
                                tracing::warn!("stream config request error: {:?}", e)
                            }
                        },
                        Some(Err(e)) => {
                            tracing::warn!("stream config error: {:?}, stopping", e);
                        },
                        None => {
                            tracing::warn!("no stream config error");
                        },
                    }
                }
                complete => break,
            }
        }
    }

    async fn handle_stream_request(
        &mut self,
        request: StreamConfigRequest,
    ) -> std::result::Result<(), anyhow::Error> {
        match request {
            StreamConfigRequest::GetHealthState { responder } => {
                tracing::trace!("StreamConfig get health state");
                responder.send(HealthState::EMPTY)?;
            }

            StreamConfigRequest::SignalProcessingConnect { protocol: _, control_handle } => {
                tracing::trace!("StreamConfig signal processing connect");
                control_handle.shutdown_with_epitaph(zx::Status::NOT_SUPPORTED);
            }

            StreamConfigRequest::GetProperties { responder } => {
                tracing::trace!("StreamConfig properties: {:?}", self.properties);
                responder.send(self.properties.clone())?;
            }

            StreamConfigRequest::GetSupportedFormats { responder } => {
                let inner = self.inner.lock().await;
                let formats = match inner
                    .dai_state
                    .as_ref()
                    .ok_or(anyhow!("DAI state must be present before Stream Config"))?
                    .interface
                    .get_ring_buffer_formats()
                    .await
                {
                    Err(e) => {
                        tracing::warn!("Couldn't get DAI ring buffer formats for device: {:?}", e);
                        let supported_formats: Vec<SupportedFormats> = vec![];
                        responder.send(&mut supported_formats.into_iter())?;
                        return Ok(());
                    }
                    Ok(formats) => formats.map_err(|e| anyhow!(e.to_string()))?,
                };
                tracing::trace!("StreamConfig ring buffer formats: {:?}", formats);
                responder.send(&mut formats.into_iter())?;
            }

            StreamConfigRequest::CreateRingBuffer { format, ring_buffer: _, control_handle: _ } => {
                let mut inner = self.inner.lock().await;
                tracing::trace!(
                    "StreamConfig create ring buffer DAI format: {:?}  ring buffer format: {:?}",
                    inner.dai_format,
                    format
                );
                inner.ring_buffer_format = Some(format.clone());
            }

            StreamConfigRequest::WatchGainState { responder } => {
                tracing::trace!("StreamConfig watch gain state");
                let mut state = self.inner.lock().await;
                if state.gain_state_first_watch_replied == true {
                    // We will never change gain state.
                    responder.drop_without_shutdown();
                    return Ok(());
                }
                let gain_state = GainState {
                    muted: Some(false),
                    agc_enabled: Some(false),
                    gain_db: Some(0.0f32),
                    ..GainState::EMPTY
                };
                state.gain_state_first_watch_replied = true;
                responder.send(gain_state)?
            }

            StreamConfigRequest::WatchPlugState { responder } => {
                tracing::trace!("StreamConfig watch plug state");
                let mut state = self.inner.lock().await;
                if state.plug_state_first_watch_replied == true {
                    // We will never change plug state.
                    responder.drop_without_shutdown();
                    return Ok(());
                }
                let plug_state = PlugState {
                    plugged: Some(true),
                    plug_state_time: Some(0i64),
                    ..PlugState::EMPTY
                };
                state.plug_state_first_watch_replied = true;
                responder.send(plug_state)?
            }

            StreamConfigRequest::SetGain { target_state: _, control_handle: _ } => {
                // We ignore this API since we report no gain change support.
                tracing::trace!("Set gain state");
            }
        }
        Ok(())
    }
}

#[derive(Default)]
pub struct DefaultConfiguratorInner {
    /// The last channel used
    last_channel_used: u8,

    /// Indexes to the available StreamConfigs.
    stream_config_indexes: HashMap<Device, StreamConfigIndex>,

    /// States for each StreamConfigs.
    stream_config_states: HashMap<StreamConfigIndex, Arc<Mutex<StreamConfigInner>>>,
}

/// This configurator uses the first element of the DAI formats reported by the codec and
/// configures one channel to be used per codec.
//#[derive(Debug, Clone)]
pub struct DefaultConfigurator {
    /// Inner state.
    inner: Arc<Mutex<DefaultConfiguratorInner>>,

    /// Stream Configs to serve.
    stream_configs: Vec<StreamConfig>,
}

impl DefaultConfigurator {
    pub fn common_dai_format(
        formats1: &Vec<DaiSupportedFormats>,
        formats2: &Vec<DaiSupportedFormats>,
    ) -> Option<DaiFormat> {
        // TODO(95437): Add heuristics and configurability for the decision of what DAI formats
        // to use when there is more than one match here..
        for i in formats1 {
            for j in formats2 {
                // All these values must be set before we say we found the common dai_format.
                let mut dai_format = DaiFormat {
                    number_of_channels: 0,
                    channels_to_use_bitmask: 0,
                    sample_format: DaiSampleFormat::PcmSigned,
                    frame_format: DaiFrameFormat::FrameFormatStandard(DaiFrameFormatStandard::I2S),
                    frame_rate: 0,
                    bits_per_sample: 0,
                    bits_per_slot: 0,
                };
                let mut found = false;
                for k in &i.number_of_channels {
                    if j.number_of_channels.contains(k) {
                        dai_format.number_of_channels = *k;
                        found = true;
                    }
                }
                if !found {
                    continue;
                }
                // Default to use all channels.
                dai_format.channels_to_use_bitmask = (1 << dai_format.number_of_channels) - 1;
                found = false;
                for k in &i.sample_formats {
                    if j.sample_formats.contains(k) {
                        dai_format.sample_format = *k;
                        found = true;
                    }
                }
                if !found {
                    continue;
                }
                found = false;
                for k in &i.frame_formats {
                    if j.frame_formats.contains(k) {
                        dai_format.frame_format = *k;
                        found = true;
                    }
                }
                if !found {
                    continue;
                }
                found = false;
                for k in &i.frame_rates {
                    if j.frame_rates.contains(k) {
                        dai_format.frame_rate = *k;
                        found = true;
                    }
                }
                if !found {
                    continue;
                }
                found = false;
                for k in &i.bits_per_sample {
                    if j.bits_per_sample.contains(k) {
                        dai_format.bits_per_sample = *k;
                        found = true;
                    }
                }
                if !found {
                    continue;
                }
                found = false;
                for k in &i.bits_per_slot {
                    if j.bits_per_slot.contains(k) {
                        dai_format.bits_per_slot = *k;
                        found = true;
                    }
                }
                if !found {
                    continue;
                }
                tracing::trace!("Found common DAI format {:?}", dai_format);
                return Some(dai_format);
            }
        }
        None
    }
}

#[async_trait]
impl Configurator for DefaultConfigurator {
    fn new(config: Config) -> Result<Self, Error> {
        // Create a StreamConfigInner state for each index found in the configuration.
        let mut states = HashMap::new();
        for (device, index) in &config.stream_config_indexes {
            if !states.contains_key(index) {
                states.insert(index.clone(), Arc::new(Mutex::new(StreamConfigInner::default())));
            }
            // If there is at least one codec, we mark the StreamConfig as codec_needed.
            if device.is_codec {
                if let Some(state) = states.get_mut(&index) {
                    state.try_lock().expect("Must exist").codec_needed = true;
                }
            }
        }
        Ok(Self {
            inner: Arc::new(Mutex::new(DefaultConfiguratorInner {
                last_channel_used: 0,
                stream_config_states: states,
                stream_config_indexes: config.stream_config_indexes,
            })),
            stream_configs: vec![],
        })
    }

    async fn process_new_codec(
        &mut self,
        mut interface: crate::codec::CodecInterface,
    ) -> Result<(), Error> {
        let _ = interface.connect().context("Couldn't connect to codec")?;

        // Get codec properties.
        let properties = interface.get_info().await?;

        // Reset the codec.
        let _ = interface.reset().await?;

        // Set the default gain state for the codec.
        let default_gain = GainState {
            muted: Some(false),
            agc_enabled: Some(false),
            gain_db: Some(0.0f32),
            ..GainState::EMPTY
        };
        let _ = interface.set_gain_state(default_gain).await?;

        let inner = self.inner.clone();
        let mut inner = inner.lock().await;

        // Get the codec formats.
        let codec_formats =
            interface.get_dai_formats().await?.map_err(|e| anyhow!(e.to_string()))?;
        if codec_formats.len() == 0
            || codec_formats[0].number_of_channels.len() == 0
            || codec_formats[0].sample_formats.len() == 0
            || codec_formats[0].frame_formats.len() == 0
            || codec_formats[0].frame_rates.len() == 0
            || codec_formats[0].bits_per_sample.len() == 0
            || codec_formats[0].bits_per_slot.len() == 0
        {
            return Err(anyhow!("Codec with bad format reported"));
        }
        tracing::info!(
            "Codec {:?} {:?} formats {:?}",
            properties.manufacturer,
            properties.product_name,
            codec_formats
        );

        // TODO(95437): Add heuristics and configurability for the DAI channel to use instead of
        // simple increment.
        let dai_channel = inner.last_channel_used;
        inner.last_channel_used += 1;

        // Update the stream config state for this codec.
        let device = Device {
            manufacturer: properties.manufacturer,
            product: properties.product_name,
            is_codec: true,
            dai_channel: dai_channel,
        };
        let stream_config_index = inner
            .stream_config_indexes
            .get(&device)
            .cloned()
            .ok_or(anyhow!("Codec ({:?}) not in config", device))?;
        let stream_config_state = inner
            .stream_config_states
            .get_mut(&stream_config_index)
            .ok_or(anyhow!("Codec ({:?}) not in config", device))?;
        let mut stream_config_state2 = stream_config_state.lock().await;
        let codec_state = CodecState {
            manufacturer: device.manufacturer.clone(),
            product: device.product.clone(),
            supported_formats: codec_formats.clone(),
            proxy: interface.get_proxy()?.clone(),
        };
        stream_config_state2.codec_states.push(codec_state);

        Ok(())
    }

    async fn process_new_dai(
        &mut self,
        mut interface: crate::dai::DaiInterface,
    ) -> Result<(), Error> {
        let _ = interface.connect().context("Couldn't connect to DAI")?;
        let (client, request_stream) =
            fidl::endpoints::create_request_stream::<StreamConfigMarker>()
                .expect("Error creating stream config endpoint");
        let dai_properties = interface.get_properties().await?;
        let mut inner = self.inner.lock().await;
        // Use an empty string if no manufacturer reported.
        let mut manufacturer = "".to_string();
        if let Some(value) = dai_properties.manufacturer {
            manufacturer = value;
        }
        // Use an empty string if no product reported.
        let mut product = "".to_string();
        if let Some(value) = dai_properties.product_name {
            product = value;
        }
        let device = Device {
            manufacturer: manufacturer,
            product: product,
            is_codec: false,
            dai_channel: 0,
        };
        let index = &inner
            .stream_config_indexes
            .get(&device)
            .cloned()
            .ok_or(anyhow!("DAI ({:?}) not in config", device))?;
        let stream_config_state = inner
            .stream_config_states
            .get_mut(&index)
            .ok_or(anyhow!("DAI ({:?}) not in config", device))?;

        let mut stream_config_state2 = stream_config_state.lock().await;

        let _ = interface.reset().await?;
        let dai_formats = interface.get_dai_formats().await?.map_err(|e| anyhow!(e.to_string()))?;
        if dai_formats.len() == 0
            || dai_formats[0].number_of_channels.len() == 0
            || dai_formats[0].sample_formats.len() == 0
            || dai_formats[0].frame_formats.len() == 0
            || dai_formats[0].frame_rates.len() == 0
            || dai_formats[0].bits_per_sample.len() == 0
            || dai_formats[0].bits_per_slot.len() == 0
        {
            return Err(anyhow!("DAI with bad format reported"));
        }
        tracing::info!(
            "DAI {:?} {:?} formats {:?}",
            device.manufacturer,
            device.product,
            dai_formats
        );

        let dai_state = DaiState {
            manufacturer: device.manufacturer.clone(),
            product: device.product.clone(),
            supported_formats: dai_formats,
            interface: interface,
        };
        stream_config_state2.dai_state = Some(dai_state);

        let configurator_product = "Driver Configurator for ".to_string() + &device.product;
        let stream_properties = StreamProperties {
            unique_id: Some(index.id),
            is_input: Some(
                dai_properties.is_input.ok_or(anyhow!("No is_input in DAI properties"))?,
            ),
            can_mute: Some(false),
            can_agc: Some(false),
            min_gain_db: Some(0f32),
            max_gain_db: Some(0f32),
            gain_step_db: Some(0f32),
            plug_detect_capabilities: Some(PlugDetectCapabilities::Hardwired),
            clock_domain: Some(0u32),
            manufacturer: Some("Google".to_string()),
            product: Some(configurator_product),
            ..StreamProperties::EMPTY
        };
        let control_handle = request_stream.control_handle().clone();
        let stream_config = StreamConfig {
            control_handle: control_handle,
            properties: stream_properties,
            stream: request_stream,
            inner: stream_config_state.clone(),
            client: Some(client),
        };
        self.stream_configs.push(stream_config);
        Ok(())
    }

    fn serve_interface(&mut self) -> Result<Vec<fasync::Task<()>>, Error> {
        let mut tasks = Vec::new();
        while let Some(mut stream_config) = self.stream_configs.pop() {
            let client = stream_config.client.take().ok_or(anyhow!("Must have client"))?;
            let svc =
                fuchsia_component::client::connect_to_protocol::<AudioDeviceEnumeratorMarker>()
                    .context("Failed to connect to AudioDeviceEnumerator")?;
            svc.add_device_by_channel(
                &stream_config.properties.product.as_ref().ok_or(anyhow!("Must have product"))?,
                stream_config.properties.is_input.ok_or(anyhow!("Must have is_input"))?,
                client,
            )?;
            tasks.push(fasync::Task::spawn(stream_config.process_requests()));
        }
        if tasks.len() == 0 {
            return Err(anyhow!("No Stream Configs to serve"));
        }
        return Ok(tasks);
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            discover::{find_codecs, find_dais},
            indexes::{
                STREAM_CONFIG_INDEX_HEADSET_OUT, STREAM_CONFIG_INDEX_MICS,
                STREAM_CONFIG_INDEX_SPEAKERS,
            },
            testing::tests::get_dev_proxy,
        },
        anyhow::Result,
        assert_matches::assert_matches,
        fixture::fixture,
        futures::{future, lock::Mutex, task::Poll},
        std::sync::Arc,
    };

    // Integration tests using //src/media/audio/drivers/tests/realm devices.

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_default_configurator_devices_found() -> Result<()> {
        let (_realm_instance, codec_proxy) = get_dev_proxy("class/codec").await?;
        let (_realm_instance, dai_proxy) = get_dev_proxy("class/dai").await?;
        let mut config = Config::new()?;
        // Codec 1.
        config.load_device(
            Device {
                manufacturer: "456".to_string(),
                product: "789".to_string(),
                is_codec: true,
                dai_channel: 0,
            },
            STREAM_CONFIG_INDEX_SPEAKERS,
        );
        // Codec 2.
        config.load_device(
            Device {
                manufacturer: "456".to_string(),
                product: "789".to_string(),
                is_codec: true,
                dai_channel: 1,
            },
            STREAM_CONFIG_INDEX_SPEAKERS,
        );
        // DAI.
        config.load_device(
            Device {
                manufacturer: "test".to_string(),
                product: "test".to_string(),
                is_codec: false,
                dai_channel: 0,
            },
            STREAM_CONFIG_INDEX_SPEAKERS,
        );

        let configurator = Arc::new(Mutex::new(DefaultConfigurator::new(config)?));
        if let Err(e) = find_codecs(codec_proxy, 2, configurator.clone()).await {
            // One of the test drivers reports bad formats.
            assert_eq!(e.to_string(), "Codec processing error: Codec with bad format reported");
        }
        assert_matches!(find_dais(dai_proxy, 1, configurator).await, Ok(()));

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_default_configurator_devices_not_found() -> Result<()> {
        let (_realm_instance, codec_proxy) = get_dev_proxy("class/codec").await?;
        let (_realm_instance, dai_proxy) = get_dev_proxy("class/dai").await?;
        let mut config = Config::new()?;
        config.load_device(
            Device {
                manufacturer: "error".to_string(),
                product: "e".to_string(),
                is_codec: true,
                dai_channel: 0,
            },
            STREAM_CONFIG_INDEX_MICS,
        );
        config.load_device(
            Device {
                manufacturer: "error".to_string(),
                product: "e".to_string(),
                is_codec: true,
                dai_channel: 0,
            },
            STREAM_CONFIG_INDEX_HEADSET_OUT,
        );
        config.load_device(
            Device {
                manufacturer: "error".to_string(),
                product: "e".to_string(),
                is_codec: true,
                dai_channel: 0,
            },
            STREAM_CONFIG_INDEX_SPEAKERS,
        );
        let configurator = Arc::new(Mutex::new(DefaultConfigurator::new(config)?));
        if let Err(e) = find_codecs(codec_proxy, 2, configurator.clone()).await {
            assert_eq!(
                e.to_string(),
                "Codec processing error: Codec (Device { manufacturer: \"456\", product: \"789\", \
                 is_codec: true, dai_channel: 0 }) not in config"
            );
        }
        if let Err(e) = find_dais(dai_proxy, 1, configurator).await {
            // One of the test drivers reports bad formats.
            assert_eq!(
                e.to_string(),
                "DAI processing error: DAI (Device { manufacturer: \"test\", product: \"test\", is\
                 _codec: false, dai_channel: 0 }) not in config"
            );
        }
        Ok(())
    }

    // Unit tests.

    #[fuchsia::test]
    fn test_common_dai_format() -> Result<()> {
        // Empty vectors return none.
        let mut all_formats_left: Vec<DaiSupportedFormats> = vec![];
        let mut all_formats_right: Vec<DaiSupportedFormats> = vec![];
        let common = DefaultConfigurator::common_dai_format(&all_formats_left, &all_formats_right);
        assert!(common.is_none());

        // One format in both side returns the format.
        let formats_left = DaiSupportedFormats {
            number_of_channels: vec![2],
            sample_formats: vec![DaiSampleFormat::PcmSigned],
            frame_formats: vec![DaiFrameFormat::FrameFormatStandard(DaiFrameFormatStandard::I2S)],
            frame_rates: vec![48000],
            bits_per_sample: vec![24],
            bits_per_slot: vec![32],
        };
        all_formats_left.push(formats_left.clone());
        all_formats_right.push(formats_left.clone());
        let common = DefaultConfigurator::common_dai_format(&all_formats_left, &all_formats_right);
        assert!(common.is_some());
        assert!(common.unwrap().number_of_channels == formats_left.number_of_channels[0]);
        assert!(common.unwrap().sample_format == formats_left.sample_formats[0]);
        assert!(common.unwrap().frame_format == formats_left.frame_formats[0]);
        assert!(common.unwrap().frame_rate == formats_left.frame_rates[0]);
        assert!(common.unwrap().bits_per_sample == formats_left.bits_per_sample[0]);
        assert!(common.unwrap().bits_per_slot == formats_left.bits_per_slot[0]);

        // A format non-matching returns none.
        let formats_right = DaiSupportedFormats {
            number_of_channels: vec![8],
            sample_formats: vec![DaiSampleFormat::PcmSigned],
            frame_formats: vec![DaiFrameFormat::FrameFormatStandard(DaiFrameFormatStandard::I2S)],
            frame_rates: vec![48000],
            bits_per_sample: vec![24],
            bits_per_slot: vec![32],
        };
        all_formats_left = vec![formats_left.clone()];
        all_formats_right = vec![formats_right];
        let common = DefaultConfigurator::common_dai_format(&all_formats_left, &all_formats_right);
        assert!(common.is_none());

        // Matching format found. Left only advertises one format.
        let formats_right = DaiSupportedFormats {
            number_of_channels: vec![2, 4, 6, 8],
            sample_formats: vec![DaiSampleFormat::PcmUnsigned, DaiSampleFormat::PcmSigned],
            frame_formats: vec![
                DaiFrameFormat::FrameFormatStandard(DaiFrameFormatStandard::I2S),
                DaiFrameFormat::FrameFormatStandard(DaiFrameFormatStandard::StereoLeft),
                DaiFrameFormat::FrameFormatStandard(DaiFrameFormatStandard::StereoRight),
            ],
            frame_rates: vec![16000, 24000, 48000, 96000],
            bits_per_sample: vec![16, 24, 32],
            bits_per_slot: vec![16, 32],
        };
        all_formats_left = vec![formats_left.clone()];
        all_formats_right = vec![formats_right];
        let common = DefaultConfigurator::common_dai_format(&all_formats_left, &all_formats_right);
        assert!(common.is_some());
        assert!(common.unwrap().number_of_channels == formats_left.number_of_channels[0]);
        assert!(common.unwrap().sample_format == formats_left.sample_formats[0]);
        assert!(common.unwrap().frame_format == formats_left.frame_formats[0]);
        assert!(common.unwrap().frame_rate == formats_left.frame_rates[0]);
        assert!(common.unwrap().bits_per_sample == formats_left.bits_per_sample[0]);
        assert!(common.unwrap().bits_per_slot == formats_left.bits_per_slot[0]);

        // Matching format found. Right only advertises one format.
        let formats_right = formats_left;
        let formats_left = DaiSupportedFormats {
            number_of_channels: vec![2, 4, 6, 8],
            sample_formats: vec![DaiSampleFormat::PcmUnsigned, DaiSampleFormat::PcmSigned],
            frame_formats: vec![
                DaiFrameFormat::FrameFormatStandard(DaiFrameFormatStandard::I2S),
                DaiFrameFormat::FrameFormatStandard(DaiFrameFormatStandard::StereoLeft),
                DaiFrameFormat::FrameFormatStandard(DaiFrameFormatStandard::StereoRight),
            ],
            frame_rates: vec![16000, 24000, 48000, 96000],
            bits_per_sample: vec![16, 24, 32],
            bits_per_slot: vec![16, 32],
        };
        all_formats_left = vec![formats_left];
        all_formats_right = vec![formats_right.clone()];
        let common = DefaultConfigurator::common_dai_format(&all_formats_left, &all_formats_right);
        assert!(common.is_some());
        assert!(common.unwrap().number_of_channels == formats_right.number_of_channels[0]);
        assert!(common.unwrap().sample_format == formats_right.sample_formats[0]);
        assert!(common.unwrap().frame_format == formats_right.frame_formats[0]);
        assert!(common.unwrap().frame_rate == formats_right.frame_rates[0]);
        assert!(common.unwrap().bits_per_sample == formats_right.bits_per_sample[0]);
        assert!(common.unwrap().bits_per_slot == formats_right.bits_per_slot[0]);

        // Matching format found. Both sides have multiple formats.
        let formats_right = DaiSupportedFormats {
            number_of_channels: vec![2, 4, 8],
            sample_formats: vec![DaiSampleFormat::PcmUnsigned, DaiSampleFormat::PcmSigned],
            frame_formats: vec![
                DaiFrameFormat::FrameFormatStandard(DaiFrameFormatStandard::I2S),
                DaiFrameFormat::FrameFormatStandard(DaiFrameFormatStandard::StereoRight),
            ],
            frame_rates: vec![16000, 24000, 48000],
            bits_per_sample: vec![16, 24, 32],
            bits_per_slot: vec![16, 32],
        };
        let formats_left = DaiSupportedFormats {
            number_of_channels: vec![1, 2, 3],
            sample_formats: vec![DaiSampleFormat::PcmSigned],
            frame_formats: vec![
                DaiFrameFormat::FrameFormatStandard(DaiFrameFormatStandard::I2S),
                DaiFrameFormat::FrameFormatStandard(DaiFrameFormatStandard::StereoLeft),
            ],
            frame_rates: vec![48000, 96000],
            bits_per_sample: vec![20, 24],
            bits_per_slot: vec![32],
        };
        all_formats_left = vec![formats_left];
        all_formats_right = vec![formats_right];
        let common = DefaultConfigurator::common_dai_format(&all_formats_left, &all_formats_right);
        assert!(common.is_some());
        assert!(common.unwrap().number_of_channels == 2);
        assert!(common.unwrap().sample_format == DaiSampleFormat::PcmSigned);
        assert!(
            common.unwrap().frame_format
                == DaiFrameFormat::FrameFormatStandard(DaiFrameFormatStandard::I2S)
        );
        assert!(common.unwrap().frame_rate == 48000);
        assert!(common.unwrap().bits_per_sample == 24);
        assert!(common.unwrap().bits_per_slot == 32);

        // Matching format found.
        // Both sides have vectors with multiple entries and each entry has multiple values formats.
        let formats_right1 = DaiSupportedFormats {
            number_of_channels: vec![8],
            sample_formats: vec![DaiSampleFormat::PcmSigned],
            frame_formats: vec![DaiFrameFormat::FrameFormatStandard(DaiFrameFormatStandard::I2S)],
            frame_rates: vec![48000],
            bits_per_sample: vec![32],
            bits_per_slot: vec![16, 32],
        };
        let formats_right2 = DaiSupportedFormats {
            number_of_channels: vec![2, 4, 8],
            sample_formats: vec![DaiSampleFormat::PcmUnsigned, DaiSampleFormat::PcmUnsigned],
            frame_formats: vec![
                DaiFrameFormat::FrameFormatStandard(DaiFrameFormatStandard::I2S),
                DaiFrameFormat::FrameFormatStandard(DaiFrameFormatStandard::StereoRight),
            ],
            frame_rates: vec![16000, 24000, 48000],
            bits_per_sample: vec![16, 24, 32],
            bits_per_slot: vec![16, 32],
        };
        let formats_left1 = DaiSupportedFormats {
            number_of_channels: vec![1],
            sample_formats: vec![DaiSampleFormat::PcmSigned],
            frame_formats: vec![
                DaiFrameFormat::FrameFormatStandard(DaiFrameFormatStandard::I2S),
                DaiFrameFormat::FrameFormatStandard(DaiFrameFormatStandard::StereoLeft),
            ],
            frame_rates: vec![48000, 96000],
            bits_per_sample: vec![20, 24],
            bits_per_slot: vec![32],
        };
        let formats_left2 = DaiSupportedFormats {
            number_of_channels: vec![1, 2, 3],
            sample_formats: vec![DaiSampleFormat::PcmSigned],
            frame_formats: vec![
                DaiFrameFormat::FrameFormatStandard(DaiFrameFormatStandard::I2S),
                DaiFrameFormat::FrameFormatStandard(DaiFrameFormatStandard::StereoLeft),
            ],
            frame_rates: vec![48000],
            bits_per_sample: vec![16, 20, 24],
            bits_per_slot: vec![16, 32],
        };
        let formats_left3 = DaiSupportedFormats {
            number_of_channels: vec![1, 2, 3],
            sample_formats: vec![DaiSampleFormat::PcmUnsigned],
            frame_formats: vec![DaiFrameFormat::FrameFormatStandard(
                DaiFrameFormatStandard::StereoRight,
            )],
            frame_rates: vec![48000],
            bits_per_sample: vec![16],
            bits_per_slot: vec![16],
        };
        all_formats_left = vec![formats_left1, formats_left2, formats_left3];
        all_formats_right = vec![formats_right1, formats_right2];
        let common = DefaultConfigurator::common_dai_format(&all_formats_left, &all_formats_right);
        assert!(common.is_some());
        assert!(common.unwrap().number_of_channels == 2);
        assert!(common.unwrap().sample_format == DaiSampleFormat::PcmUnsigned);
        assert!(
            common.unwrap().frame_format
                == DaiFrameFormat::FrameFormatStandard(DaiFrameFormatStandard::StereoRight)
        );
        assert!(common.unwrap().frame_rate == 48000);
        assert!(common.unwrap().bits_per_sample == 16);
        assert!(common.unwrap().bits_per_slot == 16);

        Ok(())
    }

    fn with_stream_config_stream<F>(_name: &str, test: F)
    where
        F: FnOnce(fasync::TestExecutor, StreamConfig) -> (),
    {
        let exec = fasync::TestExecutor::new_with_fake_time().expect("Executor should build");
        let (client, request_stream) =
            fidl::endpoints::create_request_stream::<StreamConfigMarker>()
                .expect("Error creating stream config endpoint");
        let control_handle = request_stream.control_handle().clone();
        let mut stream_config_state: StreamConfigInner = Default::default();
        stream_config_state.dai_state = None;
        let dai_format = DaiFormat {
            number_of_channels: 2,
            channels_to_use_bitmask: 3,
            sample_format: DaiSampleFormat::PcmSigned,
            frame_format: DaiFrameFormat::FrameFormatStandard(DaiFrameFormatStandard::I2S),
            frame_rate: 48_000,
            bits_per_sample: 24,
            bits_per_slot: 32,
        };
        stream_config_state.dai_format = Some(dai_format);
        let stream_properties = StreamProperties {
            unique_id: Some(STREAM_CONFIG_INDEX_MICS.id),
            is_input: Some(false),
            can_mute: Some(false),
            can_agc: Some(false),
            min_gain_db: Some(0f32),
            max_gain_db: Some(0f32),
            gain_step_db: Some(0f32),
            plug_detect_capabilities: Some(PlugDetectCapabilities::Hardwired),
            clock_domain: Some(0u32),
            manufacturer: Some("Test Manufacturer".to_string()),
            product: Some("Test Product".to_string()),
            ..StreamProperties::EMPTY
        };
        let stream_config = StreamConfig {
            control_handle: control_handle,
            properties: stream_properties,
            stream: request_stream,
            client: Some(client),
            inner: Arc::new(Mutex::new(stream_config_state)),
        };
        test(exec, stream_config)
    }

    #[fixture(with_stream_config_stream)]
    #[fuchsia::test]
    fn test_stream_config_drop(mut exec: fasync::TestExecutor, mut stream_config: StreamConfig) {
        let client = stream_config.client.take().expect("Must have client");
        drop(stream_config);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut future::pending::<()>()));
        // The audio client should be dropped.
        assert_eq!(Err(zx::Status::PEER_CLOSED), client.channel().write(&[0], &mut Vec::new()));
    }

    #[fixture(with_stream_config_stream)]
    #[fuchsia::test]
    fn test_stream_config_get_properties(
        mut exec: fasync::TestExecutor,
        mut stream_config: StreamConfig,
    ) {
        let client = stream_config.client.take().expect("Must have client");
        let proxy = client.into_proxy().expect("Client should be available");
        let _task = fasync::Task::spawn(stream_config.process_requests());
        let properties = match exec.run_until_stalled(&mut proxy.get_properties()) {
            Poll::Ready(Ok(v)) => v,
            x => panic!("Expected Ready Ok from get_properties, got: {:?}", x),
        };
        assert_eq!(properties.unique_id, Some(STREAM_CONFIG_INDEX_MICS.id));
        assert_eq!(properties.is_input, Some(false));
        assert_eq!(properties.can_mute, Some(false));
        assert_eq!(properties.can_agc, Some(false));
        assert_eq!(properties.min_gain_db, Some(0f32));
        assert_eq!(properties.max_gain_db, Some(0f32));
        assert_eq!(properties.gain_step_db, Some(0f32));
        assert_eq!(properties.plug_detect_capabilities, Some(PlugDetectCapabilities::Hardwired));
        assert_eq!(properties.clock_domain, Some(0u32));
        assert_eq!(properties.manufacturer, Some("Test Manufacturer".to_string()));
        assert_eq!(properties.product, Some("Test Product".to_string()));
    }

    #[fixture(with_stream_config_stream)]
    #[fuchsia::test]
    fn test_stream_config_watch_gain(
        mut exec: fasync::TestExecutor,
        mut stream_config: StreamConfig,
    ) {
        let client = stream_config.client.take().expect("Must have client");
        let proxy = client.into_proxy().expect("Client should be available");
        let _task = fasync::Task::spawn(stream_config.process_requests());
        let gain_state = match exec.run_until_stalled(&mut proxy.watch_gain_state()) {
            Poll::Ready(Ok(v)) => v,
            x => panic!("Expected Ready Ok from watch gain state, got: {:?}", x),
        };
        assert_eq!(gain_state.muted, Some(false));
        assert_eq!(gain_state.agc_enabled, Some(false));
        assert_eq!(gain_state.gain_db, Some(0.0f32));
    }

    #[fixture(with_stream_config_stream)]
    #[fuchsia::test]
    fn test_stream_config_watch_plug(
        mut exec: fasync::TestExecutor,
        mut stream_config: StreamConfig,
    ) {
        let client = stream_config.client.take().expect("Must have client");
        let proxy = client.into_proxy().expect("Client should be available");
        let _task = fasync::Task::spawn(stream_config.process_requests());
        let plug_state = match exec.run_until_stalled(&mut proxy.watch_plug_state()) {
            Poll::Ready(Ok(v)) => v,
            x => panic!("Expected Ready Ok from watch plug state, got: {:?}", x),
        };
        assert_eq!(plug_state.plugged, Some(true));
        assert_eq!(plug_state.plug_state_time, Some(0i64));
    }
}
