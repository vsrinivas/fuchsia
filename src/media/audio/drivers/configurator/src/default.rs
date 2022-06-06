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
    futures::lock::Mutex,
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

impl DefaultConfigurator {}

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
        futures::lock::Mutex,
        std::sync::Arc,
    };

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
}
