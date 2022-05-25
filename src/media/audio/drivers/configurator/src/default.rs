// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::configurator::Configurator,
    anyhow::{anyhow, Context, Error},
    async_trait::async_trait,
    fidl_fuchsia_hardware_audio::*,
};

/// This configurator uses the first element of the DAI formats reported by the codec and
/// configures one channel to be used per codec.
pub struct DefaultConfigurator {
    /// The last channel used
    last_channel_used: u8,
}

impl DefaultConfigurator {}

#[async_trait]
impl Configurator for DefaultConfigurator {
    fn new() -> Result<Self, Error> {
        Ok(Self { last_channel_used: 0 })
    }

    async fn process_new_codec(
        &mut self,
        mut device: crate::codec::CodecInterface,
    ) -> Result<(), Error> {
        let _ = device.connect().context("Couldn't connect to codec")?;
        let _props = device.get_info().await?;
        let _ = device.reset().await?;
        let default_gain = GainState {
            muted: Some(false),
            agc_enabled: Some(false),
            gain_db: Some(0.0f32),
            ..GainState::EMPTY
        };
        let _ = device.set_gain_state(default_gain).await?;
        let formats = device.get_dai_formats().await?.map_err(|e| anyhow!(e.to_string()))?;
        if formats.len() == 0
            || formats[0].number_of_channels.len() == 0
            || formats[0].sample_formats.len() == 0
            || formats[0].frame_formats.len() == 0
            || formats[0].frame_rates.len() == 0
            || formats[0].bits_per_slot.len() == 0
            || formats[0].bits_per_sample.len() == 0
        {
            return Err(anyhow!("Codec with bad format reported"));
        }
        // Use the last channel as long as it is not larger than the number of channels used.
        let channel = self.last_channel_used as u32 % formats[0].number_of_channels[0];
        let bitmask: u64 = 1 << channel;
        self.last_channel_used = self.last_channel_used + 1;
        let dai_format = fidl_fuchsia_hardware_audio::DaiFormat {
            number_of_channels: formats[0].number_of_channels[0],
            channels_to_use_bitmask: bitmask,
            sample_format: formats[0].sample_formats[0],
            frame_format: formats[0].frame_formats[0],
            frame_rate: formats[0].frame_rates[0],
            bits_per_slot: formats[0].bits_per_slot[0],
            bits_per_sample: formats[0].bits_per_sample[0],
        };
        let _codec_format_info = device.set_dai_format(dai_format).await;
        Ok(())
    }

    async fn process_new_dai(
        &mut self,
        mut _device: crate::dai::DaiInterface,
    ) -> Result<(), Error> {
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::discover::find_codecs, crate::testing::tests::get_dev_proxy,
        anyhow::Result,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_default_configurator_process_new_codec() -> Result<()> {
        let (_realm_instance, dev_proxy) = get_dev_proxy("class/codec").await?;
        let configurator = DefaultConfigurator::new()?;
        if let Err(e) = find_codecs(dev_proxy, 2, configurator).await {
            // One of the test drivers reports bad formats.
            assert_eq!(e.to_string(), "Codec processing error: Codec with bad format reported");
        }
        Ok(())
    }
}
