// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        codec::CodecInterface,
        config::{Config, Device},
        configurator::Configurator,
        dai::DaiInterface,
        indexes::StreamConfigIndex,
        signal::SignalInterface,
    },
    anyhow::{anyhow, Context, Error},
    async_trait::async_trait,
    async_utils::hanging_get::client::HangingGetStream,
    fidl::prelude::*,
    fidl_fuchsia_hardware_audio::*,
    fidl_fuchsia_hardware_audio_signalprocessing::*,
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

    /// DAI Channel, for instance TDM slot.
    dai_channel: u8,

    /// Watch plug detect task.
    _plug_detect_task: fasync::Task<()>,
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

pub struct StreamConfigInner {
    /// Have we replied to the very first gain state watch.
    gain_state_first_watch_replied: bool,

    /// Hardwired and hence no plug detect support.
    hardwired: bool,

    /// Current plug state.
    plugged: bool,

    /// Last plugged time.
    plugged_time: i64,

    /// The plug state shas been updated since the last watch reply.
    plug_state_updated: bool,

    /// Responder for plug detects hanging get asynchronous replies.
    plug_state_responder: Option<StreamConfigWatchPlugStateResponder>,

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

impl Default for StreamConfigInner {
    fn default() -> StreamConfigInner {
        StreamConfigInner {
            gain_state_first_watch_replied: false,
            hardwired: true,
            // Must reply to the first Watch request, if there is no plug state update before the
            // first Watch, reply with plugged at time 0.
            plugged: true,
            plugged_time: 0,
            // Mark plug state as updated to always reply to first watch.
            // If plugged and plugged_time are not updated reply with default values.
            plug_state_updated: true,
            plug_state_responder: None,
            codec_needed: false,
            codec_states: vec![],
            dai_state: None,
            dai_format: None,
            ring_buffer_format: None,
            ring_buffer: None,
        }
    }
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
    async fn process_stream_requests(mut self) {
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

            StreamConfigRequest::CreateRingBuffer { format, ring_buffer, control_handle: _ } => {
                let mut inner = self.inner.lock().await;
                tracing::trace!(
                    "StreamConfig create ring buffer DAI format: {:?}  ring buffer format: {:?}",
                    inner.dai_format,
                    format
                );
                inner.ring_buffer_format = Some(format.clone());
                inner.ring_buffer = Some(ring_buffer);
                StreamConfig::try_to_create_ring_buffer(&mut inner).await?;
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
                let mut state = self.inner.lock().await;

                let time = state.plugged_time;
                let plug_state = PlugState {
                    plugged: Some(state.plugged),
                    plug_state_time: Some(time),
                    ..PlugState::EMPTY
                };
                tracing::trace!("StreamConfig watch plug state: {:?}", plug_state.plugged);
                if state.plug_state_updated {
                    state.plug_state_updated = false;
                    responder.send(plug_state)?;
                    return Ok(());
                } else if state.plug_state_responder.is_none() {
                    state.plug_state_responder = Some(responder);
                    return Ok(());
                } else {
                    tracing::warn!(
                        "Client watched plug state when another hanging get was pending"
                    );
                    // We drop responder which causes a shutdown (no call to drop_without_shutdown).
                }
            }

            StreamConfigRequest::SetGain { target_state: _, control_handle: _ } => {
                // We ignore this API since we report no gain change support.
                tracing::trace!("Set gain state");
            }
        }
        Ok(())
    }

    // Not an error if we can't create the ring buffer because not all preconditions are met.
    async fn try_to_create_ring_buffer(
        stream_config_state: &mut StreamConfigInner,
    ) -> Result<(), Error> {
        // Create a ring buffer from the DAI if the ring buffer was already requested and we have
        // DAI state, codec state, a common DAI format and a ring buffer format.
        if let (Some(ring_buffer), Some(dai_state), Some(dai_format), Some(ring_buffer_format)) = (
            stream_config_state.ring_buffer.take(),
            stream_config_state.dai_state.as_ref(),
            stream_config_state.dai_format,
            stream_config_state.ring_buffer_format.as_ref(),
        ) {
            tracing::info!(
                "Creating ring buffer for DAI: {:?} {:?} formats: {:?} {:?}",
                dai_state.manufacturer,
                dai_state.product,
                dai_format,
                ring_buffer_format
            );
            let _ = dai_state
                .interface
                .create_ring_buffer(dai_format, ring_buffer_format.clone(), ring_buffer)
                .await?;
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
    async fn set_dai_format(
        proxy: &CodecProxy,
        mut codec_dai_format: DaiFormat,
        dai_channel: u8,
        manufacturer: &str,
        product: &str,
    ) -> Result<(), Error> {
        // Enable only the DAI channel as channel to use in the codec.
        // TODO(95437): Add flexibility instead of only allowing one DAI channel per codec.
        let bitmask: u64 = 1 << (dai_channel as u32 % codec_dai_format.number_of_channels);
        codec_dai_format.channels_to_use_bitmask = bitmask;
        tracing::info!(
            "Setting Codec {:?} {:?} to DAI format {:?}",
            manufacturer,
            product,
            codec_dai_format
        );
        if let Err(e) = proxy.set_dai_format(&mut codec_dai_format).await {
            return Err(anyhow!("Error when setting the DAI format: {:?}", e));
        }
        Ok(())
    }

    async fn watch_plug_detect(
        proxy: CodecProxy,
        stream_config_state: Arc<Mutex<StreamConfigInner>>,
    ) {
        let mut stream =
            HangingGetStream::new(proxy.clone(), CodecProxyInterface::watch_plug_state);
        loop {
            let plug_detect = stream.next().await;
            let mut stream_config_state = stream_config_state.lock().await;
            let plug_detect = match plug_detect {
                Some(v) => v,
                None => {
                    tracing::warn!("Watch stream got no plug state");
                    break;
                }
            };
            match plug_detect {
                Ok(v) => {
                    stream_config_state.plugged = match v.plugged {
                        Some(v) => v,
                        None => {
                            tracing::warn!("Plug state from codec with no plugged field");
                            break;
                        }
                    };
                    stream_config_state.plugged_time = if stream_config_state.hardwired {
                        0
                    } else {
                        match v.plug_state_time {
                            Some(v) => v,
                            None => {
                                tracing::warn!(
                                    "Plug state from codec with no plug_state_time field"
                                );
                                break;
                            }
                        }
                    };
                }
                Err(e) => {
                    tracing::warn!("Error getting plug state from codec: {:?}", e);
                    break;
                }
            }
            if let Some(responder) = stream_config_state.plug_state_responder.take() {
                let plug_state = PlugState {
                    plugged: Some(stream_config_state.plugged),
                    plug_state_time: Some(stream_config_state.plugged_time),
                    ..PlugState::EMPTY
                };
                match responder.send(plug_state) {
                    Ok(()) => continue,
                    Err(e) => {
                        tracing::warn!("Could not respond to plug state: {:?}", e);
                        continue;
                    }
                };
            } else {
                stream_config_state.plug_state_updated = true;
            }
        }
        tracing::warn!("Exiting watch plug detect");
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
            // If there is a device that is not hardwired then make the stream not hardwired.
            if !device.hardwired {
                if let Some(state) = states.get_mut(&index) {
                    state.try_lock().expect("Must exist").hardwired = false;
                }
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

    async fn process_new_codec(&mut self, mut interface: CodecInterface) -> Result<(), Error> {
        let _ = interface.connect().context("Couldn't connect to codec")?;

        // Get codec properties.
        let properties = interface.get_info().await?;

        // Reset the codec.
        let _ = interface.reset().await?;

        // Set the default gain state for the codec.
        let mut signal = SignalInterface::new();
        let proxy = interface.get_proxy()?;
        let _ = signal.connect_codec(proxy)?;
        match signal.get_elements().await {
            Ok(elements) => {
                for e in elements {
                    if let Some(id) = e.id {
                        if let Some(element_type) = e.type_ {
                            if element_type == ElementType::Gain {
                                let state = ElementState {
                                    type_specific: Some(TypeSpecificElementState::Gain(
                                        GainElementState {
                                            gain: Some(0.0f32),
                                            ..GainElementState::EMPTY
                                        },
                                    )),
                                    ..ElementState::EMPTY
                                };
                                signal.set_element_state(id, state).await?;
                            }
                        }
                    }
                }
            }
            Err(e) => {
                // We allow to continue if the Signal Processing API is not supported.
                tracing::warn!("Couldn't get elements from signal processing: {:?}", e)
            }
        }

        let plug_detect_capabilities = interface.get_plug_detect_capabilities().await?;

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
            hardwired: plug_detect_capabilities != PlugDetectCapabilities::CanAsyncNotify,
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

        // Use codec's hardwired state for the StreamConfig.
        stream_config_state2.hardwired = device.hardwired;

        // If the DAI already got its formats, lets check we have a match.
        if let Some(dai_state) = &stream_config_state2.dai_state {
            if let Some(common_format) =
                DefaultConfigurator::common_dai_format(&codec_formats, &dai_state.supported_formats)
            {
                stream_config_state2.dai_format = Some(common_format.clone());
                DefaultConfigurator::set_dai_format(
                    interface.get_proxy()?,
                    common_format,
                    dai_channel,
                    &device.manufacturer,
                    &device.product,
                )
                .await?;

                StreamConfig::try_to_create_ring_buffer(&mut stream_config_state2).await?;
            } else {
                tracing::warn!(
                    "Codec ({:?} {:?}) formats ({:?}) not found in DAI ({:?} {:?}) formats ({:?})",
                    device.manufacturer,
                    device.product,
                    codec_formats,
                    dai_state.manufacturer,
                    dai_state.product,
                    dai_state.supported_formats
                );
            }
        } else {
            tracing::info!("When codec was found, there was no format reported by the DAI yet");
        }

        let proxy = interface.get_proxy()?.clone();
        let stream_config_state_clone = stream_config_state.clone();
        // TODO(95437): Improve handing of misbehaving codecs for instance dropping the
        // corresponding StreamConfigs.
        let task = fasync::Task::spawn(async move {
            DefaultConfigurator::watch_plug_detect(proxy, stream_config_state_clone).await;
        });

        let proxy = interface.get_proxy()?.clone();
        let codec_state = CodecState {
            manufacturer: device.manufacturer.clone(),
            product: device.product.clone(),
            supported_formats: codec_formats.clone(),
            proxy: proxy,
            dai_channel: dai_channel,
            _plug_detect_task: task,
        };
        stream_config_state2.codec_states.push(codec_state);

        Ok(())
    }

    async fn process_new_dai(&mut self, mut interface: DaiInterface) -> Result<(), Error> {
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
            hardwired: true, // Set all DAIs to hardwired.
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
            supported_formats: dai_formats.clone(),
            interface: interface,
        };
        stream_config_state2.dai_state = Some(dai_state);

        // If a codec is not needed lets go ahead and create the ring buffer
        if !stream_config_state2.codec_needed {
            // Pick the first format.
            let dai_format = DaiFormat {
                number_of_channels: dai_formats[0].number_of_channels[0],
                channels_to_use_bitmask: 0,
                sample_format: dai_formats[0].sample_formats[0],
                frame_format: dai_formats[0].frame_formats[0],
                frame_rate: dai_formats[0].frame_rates[0],
                bits_per_sample: dai_formats[0].bits_per_sample[0],
                bits_per_slot: dai_formats[0].bits_per_slot[0],
            };
            stream_config_state2.dai_format = Some(dai_format);
            StreamConfig::try_to_create_ring_buffer(&mut stream_config_state2).await?;
        } else {
            // If the codecs already got state, lets check if we have a formats match.
            // We only request one ring buffer for all codecs in a stream config, but we
            // do configure all codecs via set_dai_format.
            for i in 0..stream_config_state2.codec_states.len() {
                let codec_state = &mut stream_config_state2.codec_states[i];
                if let Some(common_format) = DefaultConfigurator::common_dai_format(
                    &dai_formats,
                    &codec_state.supported_formats,
                ) {
                    DefaultConfigurator::set_dai_format(
                        &codec_state.proxy,
                        common_format.clone(),
                        codec_state.dai_channel,
                        &codec_state.manufacturer,
                        &codec_state.product,
                    )
                    .await?;

                    stream_config_state2.dai_format = Some(common_format.clone());

                    // Only one of these calls will actually create a ring buffer for a
                    // given Stream Config.
                    StreamConfig::try_to_create_ring_buffer(&mut stream_config_state2).await?;
                } else {
                    tracing::warn!(
                        "DAI ({:?} {:?}) formats ({:?}) not found in\
                             Codec ({:?} {:?}) formats ({:?})",
                        device.manufacturer,
                        device.product,
                        dai_formats,
                        codec_state.manufacturer,
                        codec_state.product,
                        codec_state.supported_formats
                    );
                }
            }
        }

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
            plug_detect_capabilities: Some(if stream_config_state2.hardwired {
                PlugDetectCapabilities::Hardwired
            } else {
                PlugDetectCapabilities::CanAsyncNotify
            }),
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
            tasks.push(fasync::Task::spawn(stream_config.process_stream_requests()));
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
                hardwired: true,
                is_codec: true,
                dai_channel: 0,
            },
            STREAM_CONFIG_INDEX_SPEAKERS,
        );
        // Codec 2.
        config.load_device(
            Device {
                manufacturer: "def".to_string(),
                product: "ghi".to_string(),
                hardwired: true,
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
                hardwired: true,
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
                hardwired: true,
                is_codec: true,
                dai_channel: 0,
            },
            STREAM_CONFIG_INDEX_MICS,
        );
        config.load_device(
            Device {
                manufacturer: "error".to_string(),
                product: "e".to_string(),
                hardwired: true,
                is_codec: true,
                dai_channel: 0,
            },
            STREAM_CONFIG_INDEX_HEADSET_OUT,
        );
        config.load_device(
            Device {
                manufacturer: "error".to_string(),
                product: "e".to_string(),
                hardwired: true,
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
                 is_codec: true, hardwired: true, dai_channel: 0 }) not in config"
            );
        }
        if let Err(e) = find_dais(dai_proxy, 1, configurator).await {
            // One of the test drivers reports bad formats.
            assert_eq!(
                e.to_string(),
                "DAI processing error: DAI (Device { manufacturer: \"test\", product: \"test\", is\
                 _codec: false, hardwired: true, dai_channel: 0 }) not in config"
            );
        }
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_default_configurator_create_ring_buffer() -> Result<()> {
        let (_realm_instance, dai_proxy) = get_dev_proxy("class/dai").await?;
        let mut config = Config::new()?;
        // One DAI only, no codecs needed.
        config.load_device(
            Device {
                manufacturer: "test".to_string(),
                product: "test".to_string(),
                is_codec: false,
                hardwired: true,
                dai_channel: 0,
            },
            STREAM_CONFIG_INDEX_SPEAKERS,
        );

        let configurator = Arc::new(Mutex::new(DefaultConfigurator::new(config)?));
        assert_matches!(find_dais(dai_proxy, 1, configurator.clone()).await, Ok(()));

        let configurator = configurator.clone();
        let configurator = configurator.lock();
        let stream_configs = &mut configurator.await.stream_configs;
        let mut stream_config = stream_configs.pop().expect("Must have a Stream Config");
        let pcm_format = PcmFormat {
            number_of_channels: 2,
            sample_format: SampleFormat::PcmSigned,
            frame_rate: 48_000,
            valid_bits_per_sample: 16,
            bytes_per_sample: 2,
        };
        let ring_buffer_format = Format { pcm_format: Some(pcm_format), ..Format::EMPTY };
        let (_client, server) = fidl::endpoints::create_endpoints::<RingBufferMarker>()
            .expect("Error creating ring buffer endpoint");
        let proxy = stream_config.client.take().expect("Must have a client").into_proxy()?;
        let _task = fasync::Task::spawn(stream_config.process_stream_requests());
        proxy.create_ring_buffer(ring_buffer_format, server)?;

        // To make sure we really complete running create ring buffer we call a 2-way method.
        let _props = proxy.get_properties().await?;

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_default_configurator_create_ring_buffer_with_codec() -> Result<()> {
        let (_realm_instance, dai_proxy) = get_dev_proxy("class/dai").await?;
        let (_realm_instance, codec_proxy) = get_dev_proxy("class/codec").await?;
        let mut config = Config::new()?;
        // Codec.
        config.load_device(
            Device {
                manufacturer: "456".to_string(),
                product: "789".to_string(),
                is_codec: true,
                hardwired: true,
                dai_channel: 0,
            },
            STREAM_CONFIG_INDEX_SPEAKERS,
        );
        // DAI.
        config.load_device(
            Device {
                manufacturer: "test".to_string(),
                product: "test".to_string(),
                is_codec: false,
                hardwired: true,
                dai_channel: 0,
            },
            STREAM_CONFIG_INDEX_SPEAKERS,
        );

        let configurator = Arc::new(Mutex::new(DefaultConfigurator::new(config)?));
        if let Err(e) = find_codecs(codec_proxy, 2, configurator.clone()).await {
            // One of the test drivers reports bad formats.
            assert_eq!(e.to_string(), "Codec processing error: Codec with bad format reported");
        }
        assert_matches!(find_dais(dai_proxy, 1, configurator.clone()).await, Ok(()));

        let configurator = configurator.clone();
        let configurator = configurator.lock();
        let stream_configs = &mut configurator.await.stream_configs;
        let mut stream_config = stream_configs.pop().expect("Must have a Stream Config");
        let pcm_format = PcmFormat {
            number_of_channels: 2,
            sample_format: SampleFormat::PcmSigned,
            frame_rate: 48_000,
            valid_bits_per_sample: 16,
            bytes_per_sample: 2,
        };
        let ring_buffer_format = Format { pcm_format: Some(pcm_format), ..Format::EMPTY };
        let (_client, server) = fidl::endpoints::create_endpoints::<RingBufferMarker>()
            .expect("Error creating ring buffer endpoint");
        let proxy = stream_config.client.take().expect("Must have a client").into_proxy()?;
        let _task = fasync::Task::spawn(stream_config.process_stream_requests());
        proxy.create_ring_buffer(ring_buffer_format, server)?;

        // To make sure we really complete running create ring buffer we call a 2-way method.
        let _props = proxy.get_properties().await?;

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
        let _task = fasync::Task::spawn(stream_config.process_stream_requests());
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
        let _task = fasync::Task::spawn(stream_config.process_stream_requests());
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
        let _task = fasync::Task::spawn(stream_config.process_stream_requests());
        let plug_state = match exec.run_until_stalled(&mut proxy.watch_plug_state()) {
            Poll::Ready(Ok(v)) => v,
            x => panic!("Expected Ready Ok from watch plug state, got: {:?}", x),
        };
        assert_eq!(plug_state.plugged, Some(true));
        assert_eq!(plug_state.plug_state_time, Some(0i64));
    }

    const TEST_CODEC_PLUGGED: bool = false;
    const TEST_CODEC_PLUG_STATE_TIME: i64 = 123i64;
    const TEST_CODEC_GAIN_PROCESSING_ELEMENT_ID: u64 = 1u64;
    const TEST_CODEC_GAIN: f32 = 0.0f32;

    pub struct TestCodec {
        /// Stream to handle Codec API protocol.
        codec_stream: CodecRequestStream,

        /// Stream to handle Signal Processing API protocol.
        signal_stream: Option<SignalProcessingRequestStream>,

        /// Last gain set
        gain: Option<f32>,
    }

    impl TestCodec {
        async fn process_codec_and_signal_requests(mut self) {
            loop {
                select! {
                    request = self.codec_stream.next() => {
                        match request {
                            Some(Ok(request)) => {
                                if let Err(e) = self.handle_codec_request(request, true).await {
                                    tracing::warn!("codec request error: {:?}", e)
                                }
                            },
                            Some(Err(e)) => {
                                tracing::warn!("codec error: {:?}, stopping", e);
                            },
                            None => {
                                tracing::warn!("no codec error");
                            },
                        }
                    },
                    request =
                        self.signal_stream.as_mut().expect("Must have signal stream").next() => {
                        match request {
                            Some(Ok(request)) => {
                                if let Err(e) = self.handle_signal_request(request).await {
                                    tracing::warn!("signal processing request error: {:?}", e)
                                }
                            },
                            Some(Err(e)) => {
                                tracing::warn!("signal processing error: {:?}, stopping", e);
                            },
                            None => {
                                tracing::warn!("no signal processing error");
                            },
                        }
                    },
                    complete => break,
                }
            }
        }

        async fn handle_codec_request(
            &mut self,
            request: CodecRequest,
            with_signal: bool,
        ) -> std::result::Result<(), anyhow::Error> {
            match request {
                CodecRequest::Reset { responder } => {
                    responder.send()?;
                }
                CodecRequest::GetInfo { responder } => {
                    let mut info = CodecInfo {
                        unique_id: "".to_string(),
                        manufacturer: "test".to_string(),
                        product_name: "testy".to_string(),
                    };
                    responder.send(&mut info)?;
                }
                CodecRequest::Stop { responder: _ } => {}
                CodecRequest::Start { responder: _ } => {}
                CodecRequest::IsBridgeable { responder: _ } => {}
                CodecRequest::SetBridgedMode { enable_bridged_mode: _, control_handle: _ } => {}
                CodecRequest::GetDaiFormats { responder } => {
                    let mut formats = Ok(vec![DaiSupportedFormats {
                        number_of_channels: vec![2],
                        sample_formats: vec![DaiSampleFormat::PcmSigned],
                        frame_formats: vec![DaiFrameFormat::FrameFormatStandard(
                            DaiFrameFormatStandard::I2S,
                        )],
                        frame_rates: vec![48000],
                        bits_per_sample: vec![24],
                        bits_per_slot: vec![32],
                    }]);
                    responder.send(&mut formats)?;
                }
                CodecRequest::SetDaiFormat { responder: _, format: _ } => {}
                CodecRequest::GetPlugDetectCapabilities { responder } => {
                    responder.send(PlugDetectCapabilities::Hardwired)?;
                }
                CodecRequest::WatchPlugState { responder } => {
                    responder.send(PlugState {
                        plugged: Some(TEST_CODEC_PLUGGED),
                        plug_state_time: Some(TEST_CODEC_PLUG_STATE_TIME),
                        ..PlugState::EMPTY
                    })?;
                }
                CodecRequest::SetGainState { target_state, control_handle: _ } => {
                    if !with_signal {
                        self.gain = target_state.gain_db;
                    }
                }
                CodecRequest::SignalProcessingConnect { protocol, control_handle: _ } => {
                    if with_signal {
                        self.signal_stream = Some(protocol.into_stream()?);
                    } else {
                        let _ = protocol.close_with_epitaph(zx::Status::NOT_SUPPORTED);
                    }
                }
                CodecRequest::WatchGainState { responder } => {
                    responder.send(GainState { gain_db: self.gain, ..GainState::EMPTY })?;
                }

                r => panic!("{:?} Not covered by test", r),
            }
            Ok(())
        }

        async fn handle_signal_request(
            &mut self,
            request: SignalProcessingRequest,
        ) -> std::result::Result<(), anyhow::Error> {
            match request {
                SignalProcessingRequest::GetElements { responder } => {
                    let pe = Element {
                        id: Some(TEST_CODEC_GAIN_PROCESSING_ELEMENT_ID),
                        type_: Some(ElementType::Gain),
                        type_specific: Some(TypeSpecificElement::Gain(Gain {
                            type_: Some(
                                fidl_fuchsia_hardware_audio_signalprocessing::GainType::Decibels,
                            ),
                            min_gain: Some(0.0f32),
                            max_gain: Some(0.0f32),
                            min_gain_step: Some(0.0f32),
                            ..Gain::EMPTY
                        })),
                        ..Element::EMPTY
                    };
                    let mut ret = Ok(vec![pe]);
                    responder.send(&mut ret)?;
                }
                SignalProcessingRequest::SetElementState {
                    processing_element_id,
                    state,
                    responder,
                } => {
                    if processing_element_id == TEST_CODEC_GAIN_PROCESSING_ELEMENT_ID {
                        match state.type_specific {
                            Some(type_specific) => match type_specific {
                                TypeSpecificElementState::Gain(gain) => {
                                    self.gain = gain.gain;
                                    let mut ret = Ok(());
                                    return Ok(responder.send(&mut ret)?);
                                }
                                _ => panic!("Must be of type gain"),
                            },
                            _ => panic!("Must have gain"),
                        }
                    }
                    panic!("Not covered by test");
                }
                SignalProcessingRequest::WatchElementState { processing_element_id, responder } => {
                    if processing_element_id == TEST_CODEC_GAIN_PROCESSING_ELEMENT_ID {
                        let state = ElementState {
                            type_specific: Some(TypeSpecificElementState::Gain(GainElementState {
                                gain: Some(TEST_CODEC_GAIN),
                                ..GainElementState::EMPTY
                            })),
                            ..ElementState::EMPTY
                        };
                        return Ok(responder.send(state)?);
                    }
                    panic!("Not covered by test");
                }
                r => panic!("{:?} Not covered by test", r),
            }
            Ok(())
        }
    }

    #[fixture(with_stream_config_stream)]
    #[fuchsia::test]
    fn test_stream_config_watch_plug_with_codec(
        mut exec: fasync::TestExecutor,
        mut stream_config: StreamConfig,
    ) {
        let (codec_client, codec_stream) = fidl::endpoints::create_request_stream::<CodecMarker>()
            .expect("Error creating endpoint");
        let (_signal_client, signal_stream) =
            fidl::endpoints::create_request_stream::<SignalProcessingMarker>()
                .expect("Error creating endpoint");
        let codec = TestCodec {
            codec_stream: codec_stream,
            signal_stream: Some(signal_stream),
            gain: None,
        };
        let _codec_task = fasync::Task::spawn(codec.process_codec_and_signal_requests());
        let stream_config_inner = stream_config.inner.clone();
        let _codec_plug_detect_task = fasync::Task::spawn(async move {
            DefaultConfigurator::watch_plug_detect(
                codec_client.into_proxy().expect("Must have proxy"),
                stream_config_inner,
            )
            .await;
        });

        {
            let stream_config_inner = stream_config.inner.clone();
            let mut inner = match exec.run_until_stalled(&mut stream_config_inner.lock()) {
                Poll::Ready(v) => v,
                Poll::Pending => panic!("Expected Ready Ok from stream config inne, got Pending"),
            };
            inner.hardwired = false;
        }

        let stream_config_client = stream_config.client.take().expect("Must have client");
        let proxy = stream_config_client.into_proxy().expect("Client should be available");
        let _stream_config_task = fasync::Task::spawn(stream_config.process_stream_requests());

        // First get plugged with time 0 since there is no plug state before this first watch.
        let plug_state = match exec.run_until_stalled(&mut proxy.watch_plug_state()) {
            Poll::Ready(Ok(v)) => v,
            x => panic!("Expected Ready Ok from watch plug state, got: {:?}", x),
        };
        assert_eq!(plug_state.plugged, Some(true));
        assert_eq!(plug_state.plug_state_time, Some(0i64));

        // Then get unplugged from the TestCodec.
        let plug_state = match exec.run_until_stalled(&mut proxy.watch_plug_state()) {
            Poll::Ready(Ok(v)) => v,
            x => panic!("Expected Ready Ok from watch plug state, got: {:?}", x),
        };
        assert_eq!(plug_state.plugged, Some(TEST_CODEC_PLUGGED));
        assert_eq!(plug_state.plug_state_time, Some(TEST_CODEC_PLUG_STATE_TIME));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_default_configurator_find_codec_with_signal_processing() -> Result<()> {
        let mut config = Config::new()?;
        config.load_device(
            Device {
                manufacturer: "test".to_string(),
                product: "testy".to_string(),
                hardwired: true,
                is_codec: true,
                dai_channel: 0,
            },
            STREAM_CONFIG_INDEX_SPEAKERS,
        );
        let mut configurator = DefaultConfigurator::new(config)?;
        let (codec_client, codec_stream) = fidl::endpoints::create_request_stream::<CodecMarker>()
            .expect("Error creating endpoint");
        let (_signal_client, signal_stream) =
            fidl::endpoints::create_request_stream::<SignalProcessingMarker>()
                .expect("Error creating endpoint");
        let codec = TestCodec {
            codec_stream: codec_stream,
            signal_stream: Some(signal_stream),
            gain: None,
        };
        let _codec_task = fasync::Task::spawn(codec.process_codec_and_signal_requests());
        let codec_proxy = codec_client.into_proxy().expect("Client should be available");
        let codec_interface = CodecInterface::new_with_proxy(codec_proxy);
        let proxy = codec_interface.get_proxy()?.clone();
        configurator.process_new_codec(codec_interface).await?;

        let mut signal = SignalInterface::new();
        let _ = signal.connect_codec(&proxy)?;
        let elements = signal.get_elements().await?;
        for e in elements {
            if let Some(id) = e.id {
                if let Some(element_type) = e.type_ {
                    if element_type == ElementType::Gain {
                        let state = signal.watch_element_state(id).await?;
                        match state.type_specific {
                            Some(type_specific) => match type_specific {
                                TypeSpecificElementState::Gain(gain) => {
                                    assert_eq!(gain.gain, Some(TEST_CODEC_GAIN));
                                    return Ok(());
                                }
                                _ => panic!("Must be of type gain"),
                            },
                            _ => panic!("Must have gain"),
                        }
                    }
                }
            }
        }
        panic!("Must have found gain");
    }

    pub struct TestCodecBad {
        /// Stream to handle Codec API protocol.
        codec_stream: CodecRequestStream,
    }

    impl TestCodecBad {
        async fn process_codec_requests(mut self) {
            loop {
                select! {
                    request = self.codec_stream.next() => {
                        match request {
                            Some(Ok(request)) => {
                                if let Err(e) = self.handle_codec_request(request).await {
                                    tracing::warn!("codec request error: {:?}", e)
                                }
                            },
                            Some(Err(e)) => {
                                tracing::warn!("codec error: {:?}, stopping", e);
                            },
                            None => {
                                tracing::warn!("no codec error");
                            },
                        }
                    }
                    complete => break,
                }
            }
        }

        async fn handle_codec_request(
            &mut self,
            request: CodecRequest,
        ) -> std::result::Result<(), anyhow::Error> {
            match request {
                CodecRequest::WatchPlugState { responder } => {
                    responder.send(PlugState {
                        // A plug state with missing plugged field is bad.
                        plug_state_time: Some(TEST_CODEC_PLUG_STATE_TIME),
                        ..PlugState::EMPTY
                    })?;
                }
                r => panic!("{:?} Not covered by test", r),
            }
            Ok(())
        }
    }

    #[fixture(with_stream_config_stream)]
    #[fuchsia::test]
    fn test_stream_config_watch_plug_with_bad_codec(
        mut exec: fasync::TestExecutor,
        mut stream_config: StreamConfig,
    ) {
        let (codec_client, codec_stream) = fidl::endpoints::create_request_stream::<CodecMarker>()
            .expect("Error creating endpoint");
        let codec = TestCodecBad { codec_stream: codec_stream };
        let _codec_task = fasync::Task::spawn(codec.process_codec_requests());
        let stream_config_inner = stream_config.inner.clone();
        let _codec_plug_detect_task = fasync::Task::spawn(async move {
            DefaultConfigurator::watch_plug_detect(
                codec_client.into_proxy().expect("Must have proxy"),
                stream_config_inner,
            )
            .await;
        });

        {
            let stream_config_inner = stream_config.inner.clone();
            let mut inner = match exec.run_until_stalled(&mut stream_config_inner.lock()) {
                Poll::Ready(v) => v,
                Poll::Pending => panic!("Expected Ready Ok from stream config inne, got Pending"),
            };
            inner.hardwired = false;
        }

        let stream_config_client = stream_config.client.take().expect("Must have client");
        let proxy = stream_config_client.into_proxy().expect("Client should be available");
        let _stream_config_task = fasync::Task::spawn(stream_config.process_stream_requests());

        let plug_state = match exec.run_until_stalled(&mut proxy.watch_plug_state()) {
            Poll::Ready(Ok(v)) => v,
            x => panic!("Expected Ready Ok from watch plug state, got: {:?}", x),
        };
        assert_eq!(plug_state.plugged, Some(true));
        assert_eq!(plug_state.plug_state_time, Some(0i64));

        // Then never respond because our test codec is bad.
        match exec.run_until_stalled(&mut proxy.watch_plug_state()) {
            Poll::Pending => {}
            x => panic!("Expected Pending from watch plug state, got: {:?}", x),
        }
    }
}
