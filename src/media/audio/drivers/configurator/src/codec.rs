// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use fidl::endpoints::Proxy;
use fidl_fuchsia_hardware_audio::*;
use fidl_fuchsia_io as fio;
use fuchsia_zircon as zx;
use futures::TryFutureExt;
use std::path::{Path, PathBuf};

pub struct CodecInterface {
    /// The proxy to the devfs "/dev".
    dev_proxy: fio::DirectoryProxy,
    /// The path under "/dev" used to connect to the device.
    path: PathBuf,
    /// The proxy to the device if connected.
    proxy: Option<fidl_fuchsia_hardware_audio::CodecProxy>,
}

impl CodecInterface {
    /// A new interface that will connect to the device at the `path` within the `dev_proxy`
    /// directory. The interface is unconnected when created.
    pub fn new(dev_proxy: fio::DirectoryProxy, path: &Path) -> Self {
        Self { dev_proxy: dev_proxy, path: path.to_path_buf(), proxy: None }
    }

    /// Get the codec proxy.
    fn get_proxy(&self) -> Result<&fidl_fuchsia_hardware_audio::CodecProxy, Error> {
        self.proxy.as_ref().ok_or(format_err!("Proxy not connected"))
    }

    /// Connect to the CodecInterface.
    pub fn connect(&mut self) -> Result<(), Error> {
        let path = self.path.to_str().ok_or(format_err!("invalid codec path"))?;
        let (codec_connect_proxy, codec_connect_server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_hardware_audio::CodecConnectorMarker>()?;
        fdio::service_connect_at(
            self.dev_proxy.as_channel().as_ref(),
            path,
            codec_connect_server.into_channel(),
        )?;

        let (ours, theirs) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_hardware_audio::CodecMarker>()?;
        codec_connect_proxy.connect(theirs)?;

        self.proxy = Some(ours);
        Ok(())
    }

    /// Get information from the codec.
    pub async fn get_info(&self) -> Result<fidl_fuchsia_hardware_audio::CodecInfo, Error> {
        self.get_proxy()?.clone().get_info().err_into().await
    }

    /// Reset codec.
    pub async fn reset(&self) -> Result<(), Error> {
        self.get_proxy()?.clone().reset().err_into().await
    }

    /// Set the gain state.
    pub async fn set_gain_state(&self, gain_state: GainState) -> Result<(), Error> {
        Ok(self.get_proxy()?.clone().set_gain_state(gain_state)?)
    }

    /// Get supported DAI formats.
    pub async fn get_dai_formats(
        &self,
    ) -> Result<fidl_fuchsia_hardware_audio::DaiGetDaiFormatsResult, Error> {
        self.get_proxy()?.clone().get_dai_formats().err_into().await
    }

    /// Set DAI format on the codec.
    pub async fn set_dai_format(
        &self,
        mut dai_format: fidl_fuchsia_hardware_audio::DaiFormat,
    ) -> Result<fidl_fuchsia_hardware_audio::CodecFormatInfo, Error> {
        self.get_proxy()?
            .clone()
            .set_dai_format(&mut dai_format)
            .await?
            .map_err(|e| zx::Status::from_raw(e).into())
    }
}

#[cfg(test)]
mod tests {
    use crate::configurator::Configurator;
    use crate::discover::find_codecs;
    use crate::testing::tests::get_dev_proxy;
    use anyhow::Result;
    use async_trait::async_trait;
    use tracing;

    pub struct TestConfigurator {}

    #[async_trait]
    impl Configurator for TestConfigurator {
        fn new() -> Self {
            Self {}
        }

        async fn process_new_codec(&mut self, mut device: crate::codec::CodecInterface) {
            if device.connect().is_err() {
                tracing::warn!("Couldn't connect to codec");
                return;
            }
            let info = device.get_info().await.unwrap();
            assert_eq!(info.unique_id, "123");
            assert_eq!(info.manufacturer, "456");
            assert_eq!(info.product_name, "789");

            let formats = device.get_dai_formats().await.unwrap().unwrap();
            // We have 2 test codecs, one with good behavior (formats listed) and one with bad
            // behavior (empty formats), we only set_dai_formats for the the one that reported
            // at least one format.
            if formats.len() == 0
                || formats[0].number_of_channels.len() == 0
                || formats[0].sample_formats.len() == 0
                || formats[0].frame_formats.len() == 0
                || formats[0].frame_rates.len() == 0
                || formats[0].bits_per_slot.len() == 0
                || formats[0].bits_per_sample.len() == 0
            {
                tracing::warn!("Codec with bad format reported");
                return;
            }

            // Good test codec checks.
            assert_eq!(formats[0].number_of_channels[0], 2);
            // Configure the first option for each field supported by the test codec.
            let format = fidl_fuchsia_hardware_audio::DaiFormat {
                number_of_channels: formats[0].number_of_channels[0],
                // Use all channels.
                channels_to_use_bitmask: (1 << formats[0].number_of_channels[0]) - 1,
                sample_format: formats[0].sample_formats[0],
                frame_format: formats[0].frame_formats[0],
                frame_rate: formats[0].frame_rates[0],
                bits_per_slot: formats[0].bits_per_slot[0],
                bits_per_sample: formats[0].bits_per_sample[0],
            };
            let _codec_format_info = device.set_dai_format(format).await;
        }

        async fn process_new_dai(&mut self, mut _device: crate::dai::DaiInterface) {}
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_codec_api() -> Result<()> {
        let (_realm_instance, dev_proxy) = get_dev_proxy("class/codec").await?;
        let configurator = TestConfigurator::new();
        find_codecs(dev_proxy, 2, configurator).await?;
        Ok(())
    }
}
