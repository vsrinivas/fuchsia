// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(andresoportus): Remove this as usage of it is added.
#![allow(dead_code)]

use {
    anyhow::{anyhow, format_err, Error},
    fidl::endpoints::Proxy,
    fidl_fuchsia_hardware_audio::*,
    fidl_fuchsia_io as fio,
    futures::TryFutureExt,
    std::path::{Path, PathBuf},
};

pub struct DaiInterface {
    /// The proxy to the devfs "/dev".
    dev_proxy: fio::DirectoryProxy,
    /// The path under "/dev" used to connect to the device.
    path: PathBuf,
    /// The proxy to the device if connected.
    proxy: Option<DaiProxy>,
}

impl DaiInterface {
    /// A new interface that will connect to the device at the `path` within the `dev_proxy`
    /// directory. The interface is unconnected when created.
    pub fn new(dev_proxy: fio::DirectoryProxy, path: &Path) -> Self {
        Self { dev_proxy: dev_proxy, path: path.to_path_buf(), proxy: None }
    }

    /// Get the DAI proxy.
    fn get_proxy(&self) -> Result<&DaiProxy, Error> {
        self.proxy.as_ref().ok_or(format_err!("Proxy not connected"))
    }

    /// Connect to the DaiInterface.
    pub fn connect(&mut self) -> Result<(), Error> {
        let path = self.path.to_str().ok_or(format_err!("invalid DAI path"))?;
        let (dai_connect_proxy, dai_connect_server) =
            fidl::endpoints::create_proxy::<DaiConnectorMarker>()?;
        fdio::service_connect_at(
            self.dev_proxy.as_channel().as_ref(),
            path,
            dai_connect_server.into_channel(),
        )?;

        let (ours, theirs) = fidl::endpoints::create_proxy::<DaiMarker>()?;
        dai_connect_proxy.connect(theirs)?;

        self.proxy = Some(ours);
        Ok(())
    }

    /// Get information from the DAI.
    pub async fn get_properties(&self) -> Result<DaiProperties, Error> {
        self.get_proxy()?.clone().get_properties().err_into().await
    }

    /// Reset DAI.
    pub async fn reset(&self) -> Result<(), Error> {
        self.get_proxy()?.clone().reset().err_into().await
    }

    /// Get supported DAI formats.
    pub async fn get_dai_formats(&self) -> Result<DaiGetDaiFormatsResult, Error> {
        self.get_proxy()?.clone().get_dai_formats().err_into().await
    }

    /// Get supported ring buffer formats.
    pub async fn get_ring_buffer_formats(&self) -> Result<DaiGetRingBufferFormatsResult, Error> {
        self.get_proxy()?.clone().get_ring_buffer_formats().err_into().await
    }

    /// Create ring buffer.
    pub async fn create_ring_buffer(
        &self,
        mut dai_format: DaiFormat,
        ring_buffer_format: Format,
        ring_buffer: fidl::endpoints::ServerEnd<RingBufferMarker>,
    ) -> Result<(), Error> {
        self.get_proxy()?
            .clone()
            .create_ring_buffer(&mut dai_format, ring_buffer_format, ring_buffer)
            .map_err(|e| anyhow!("FIDL error creating ring buffer {:?}", e))
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::configurator::Configurator, crate::discover::find_dais,
        crate::testing::tests::get_dev_proxy, anyhow::Context, anyhow::Result,
        async_trait::async_trait,
    };

    pub struct TestConfigurator {}

    #[async_trait]
    impl Configurator for TestConfigurator {
        fn new() -> Result<Self, Error> {
            Ok(Self {})
        }

        async fn process_new_codec(
            &mut self,
            mut _device: crate::codec::CodecInterface,
        ) -> Result<(), Error> {
            Ok(())
        }
        async fn process_new_dai(
            &mut self,
            mut device: crate::dai::DaiInterface,
        ) -> Result<(), Error> {
            let _ = device.connect().context("Couldn't connect to DAI")?;

            let info = device.get_properties().await?;
            assert_eq!(info.is_input.unwrap(), false);
            assert_eq!(info.manufacturer.unwrap(), "test");
            assert_eq!(info.product_name.unwrap(), "test");

            let _ = device.reset().await?;

            // We test that we can make calls to the DAI driver, no checks on formats.
            let _ring_buffer_formats = device.get_ring_buffer_formats().await.unwrap().unwrap();
            let _dai_formats = device.get_dai_formats().await.unwrap().unwrap();
            let (_ours, theirs) = fidl::endpoints::create_proxy::<RingBufferMarker>().unwrap();
            let ring_buffer_pcm_format = PcmFormat {
                number_of_channels: 2,
                sample_format: SampleFormat::PcmSigned,
                bytes_per_sample: 2,
                valid_bits_per_sample: 16,
                frame_rate: 48000,
            };
            let ring_buffer_format =
                Format { pcm_format: Some(ring_buffer_pcm_format), ..Format::EMPTY };
            let dai_format = DaiFormat {
                number_of_channels: 2,
                channels_to_use_bitmask: 3,
                sample_format: DaiSampleFormat::PcmSigned,
                frame_format: DaiFrameFormat::FrameFormatStandard(DaiFrameFormatStandard::I2S),
                frame_rate: 48000,
                bits_per_slot: 24,
                bits_per_sample: 32,
            };
            let _ = device.create_ring_buffer(dai_format, ring_buffer_format, theirs).await?;
            Ok(())
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dai_api() -> Result<(), Error> {
        let (_realm_instance, dev_proxy) = get_dev_proxy("class/dai").await?;
        let configurator = TestConfigurator::new()?;
        find_dais(dev_proxy, 1, configurator).await?;
        Ok(())
    }
}
