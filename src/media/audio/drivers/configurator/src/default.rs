// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::configurator::Configurator;
use async_trait::async_trait;

/// This configurator uses the first element of the DAI formats reported by the codec and
/// configures one channel to be used per codec.
pub struct DefaultConfigurator {
    /// The last channel used
    last_channel_used: u8,
}

impl DefaultConfigurator {}

#[async_trait]
impl Configurator for DefaultConfigurator {
    fn new() -> Self {
        Self { last_channel_used: 0 }
    }

    async fn process_new_codec(&mut self, mut device: crate::codec::CodecInterface) {
        if device.connect().is_err() {
            tracing::warn!("Couldn't connect to codec");
            return;
        }
        let _props = match device.get_info().await {
            Err(e) => {
                tracing::warn!("Couldn't get info for device: {:?}", e);
                return;
            }
            Ok(props) => props,
        };
        let formats = match device.get_dai_formats().await {
            Err(e) => {
                tracing::warn!("Couldn't get DAI formats for device: {:?}", e);
                return;
            }
            Ok(formats) => formats.unwrap(),
        };
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
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::discover::find_codecs;
    use crate::testing::tests::get_dev_proxy;
    use anyhow::Result;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_default_configurator_process_new_codec() -> Result<()> {
        let (_realm_instance, dev_proxy) = get_dev_proxy().await?;
        let configurator = DefaultConfigurator::new();
        find_codecs(dev_proxy, true, configurator).await?;
        Ok(())
    }
}
