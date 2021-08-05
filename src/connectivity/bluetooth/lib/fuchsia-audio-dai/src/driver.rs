// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use fidl::endpoints::{Proxy, ServerEnd};
use fidl_fuchsia_hardware_audio::*;
use fuchsia_zircon as zx;
use futures::{
    future::{self, Either},
    Future, FutureExt, TryFutureExt,
};
use std::path::{Path, PathBuf};

#[derive(Debug)]
pub struct DigitalAudioInterface {
    /// The path used to connect to the device.
    path: PathBuf,
    /// The proxy to the device, if connected.
    proxy: Option<DaiProxy>,
}

impl DigitalAudioInterface {
    /// A new interface that will connect to the device at `path`.
    /// The interface is unconnected when created.
    pub fn new(path: &Path) -> Self {
        Self { path: path.to_path_buf(), proxy: None }
    }

    /// Build a DAI from a proxy.  Path will be empty.
    pub(crate) fn from_proxy(proxy: DaiProxy) -> Self {
        Self { path: PathBuf::new(), proxy: Some(proxy) }
    }

    /// Connect to the DigitalAudioInterface.
    pub fn connect(&mut self) -> Result<(), Error> {
        if let Some(proxy) = &self.proxy {
            if !proxy.is_closed() {
                return Ok(());
            }
        }
        let (dai_connect_proxy, dai_connect_server) =
            fidl::endpoints::create_proxy::<DaiConnectMarker>()?;
        let path = self.path.to_str().ok_or(format_err!("invalid DAI path"))?;
        fdio::service_connect(path, dai_connect_server.into_channel())?;

        let (ours, theirs) = fidl::endpoints::create_proxy::<DaiMarker>()?;
        dai_connect_proxy.connect(theirs)?;

        self.proxy = Some(ours);
        Ok(())
    }

    fn get_proxy(&self) -> Result<&DaiProxy, Error> {
        self.proxy.as_ref().ok_or(format_err!("Proxy not connected"))
    }

    /// Get the properties of the DAI.
    /// Will attempt to connect to the DAI if not connected.
    pub fn properties(&self) -> impl Future<Output = Result<DaiProperties, Error>> {
        match self.get_proxy() {
            Err(e) => Either::Left(future::ready(Err(e))),
            Ok(proxy) => Either::Right(proxy.clone().get_properties().err_into()),
        }
    }

    pub fn dai_formats(&self) -> impl Future<Output = Result<Vec<DaiSupportedFormats>, Error>> {
        let proxy = match self.get_proxy() {
            Err(e) => return Either::Left(future::ready(Err(e))),
            Ok(proxy) => proxy,
        };
        Either::Right(proxy.clone().get_dai_formats().map(|o| match o {
            Err(e) => Err(e.into()),
            Ok(Err(e)) => Err(zx::Status::from_raw(e).into()),
            Ok(Ok(o)) => Ok(o),
        }))
    }

    pub fn ring_buffer_formats(
        &self,
    ) -> impl Future<Output = Result<Vec<SupportedFormats>, Error>> {
        let proxy = match self.get_proxy() {
            Err(e) => return Either::Left(future::ready(Err(e))),
            Ok(proxy) => proxy,
        };
        Either::Right(proxy.clone().get_ring_buffer_formats().map(|o| match o {
            Err(e) => Err(e.into()),
            Ok(Err(e)) => Err(zx::Status::from_raw(e).into()),
            Ok(Ok(o)) => Ok(o),
        }))
    }

    pub fn create_ring_buffer(
        &self,
        mut dai_format: DaiFormat,
        buffer_format: Format,
        ring_buffer_client: ServerEnd<RingBufferMarker>,
    ) -> Result<(), Error> {
        let proxy = self.get_proxy()?;
        proxy
            .create_ring_buffer(&mut dai_format, buffer_format, ring_buffer_client)
            .map_err(Into::into)
    }
}

pub(crate) fn ensure_pcm_format_is_supported(
    ring_buffer_formats: &[SupportedFormats],
    pcm_format: &PcmFormat,
) -> Result<(), Error> {
    for format in ring_buffer_formats {
        if let SupportedFormats {
            pcm_supported_formats:
                Some(PcmSupportedFormats {
                    channel_sets: Some(channel_sets),
                    sample_formats: Some(sample_formats),
                    bytes_per_sample: Some(bytes_per_sample),
                    valid_bits_per_sample: Some(valid_bits_per_sample),
                    frame_rates: Some(frame_rates),
                    ..
                }),
            ..
        } = format
        {
            if channel_sets[0].attributes.as_ref().unwrap().len()
                == pcm_format.number_of_channels as usize
                && sample_formats.contains(&pcm_format.sample_format)
                && bytes_per_sample.contains(&pcm_format.bytes_per_sample)
                && valid_bits_per_sample.contains(&pcm_format.valid_bits_per_sample)
                && frame_rates.contains(&pcm_format.frame_rate)
            {
                return Ok(());
            }
        }
    }
    Err(format_err!("DAI does not support PCM format: {:?}", pcm_format))
}

pub(crate) fn ensure_dai_format_is_supported(
    supported_formats: &[DaiSupportedFormats],
    dai_format: &DaiFormat,
) -> Result<(), Error> {
    for dai_supported in supported_formats.iter() {
        if dai_supported.number_of_channels.contains(&dai_format.number_of_channels)
            && dai_supported.sample_formats.contains(&dai_format.sample_format)
            && dai_supported.frame_formats.contains(&dai_format.frame_format)
            && dai_supported.frame_rates.contains(&dai_format.frame_rate)
            && dai_supported.bits_per_slot.contains(&dai_format.bits_per_slot)
            && dai_supported.bits_per_sample.contains(&dai_format.bits_per_sample)
        {
            return Ok(());
        }
    }
    Err(format_err!("DAI does not support DAI format: {:?}", dai_format))
}

#[cfg(test)]
mod tests {
    use super::*;

    use async_utils::PollExt;
    use fuchsia;
    use fuchsia_async as fasync;
    use futures::{pin_mut, task::Poll, StreamExt};

    fn connected_dai() -> (DigitalAudioInterface, DaiRequestStream) {
        let (proxy, requests) =
            fidl::endpoints::create_proxy_and_stream::<DaiMarker>().expect("proxy");
        let dai = DigitalAudioInterface::from_proxy(proxy);
        (dai, requests)
    }

    #[fuchsia::test]
    fn get_properties() {
        let mut exec = fasync::TestExecutor::new().expect("executor");
        // Unconnected DAI
        let dai = DigitalAudioInterface { path: PathBuf::new(), proxy: None };

        let _ = exec
            .run_singlethreaded(&mut dai.properties())
            .expect_err("properties of an unconnected DAI should be error");

        let (dai, mut requests) = connected_dai();

        let properties_fut = dai.properties();
        pin_mut!(properties_fut);

        exec.run_until_stalled(&mut properties_fut).expect_pending("should be pending");

        match exec.run_until_stalled(&mut requests.next()) {
            Poll::Ready(Some(Ok(DaiRequest::GetProperties { responder }))) => responder
                .send(DaiProperties {
                    is_input: Some(true),
                    manufacturer: Some(String::from("Fuchsia")),
                    product_name: Some(String::from("Spinny Audio")),
                    ..DaiProperties::EMPTY
                })
                .expect("send response okay"),
            x => panic!("Expected a ready GetProperties request, got {:?}", x),
        };

        let result = exec.run_until_stalled(&mut properties_fut).expect("response from properties");
        let properties = result.expect("ok response");
        assert_eq!(Some(true), properties.is_input);
    }

    #[fuchsia::test]
    fn dai_formats() {
        let mut exec = fasync::TestExecutor::new().expect("executor");
        let (dai, mut requests) = connected_dai();

        let supported_formats_fut = dai.dai_formats();
        pin_mut!(supported_formats_fut);

        // Doesn't need to continue to be held to complete this
        drop(dai);

        exec.run_until_stalled(&mut supported_formats_fut).expect_pending("should be pending");

        match exec.run_until_stalled(&mut requests.next()) {
            Poll::Ready(Some(Ok(DaiRequest::GetDaiFormats { responder }))) => responder
                .send(&mut Ok(vec![
                    DaiSupportedFormats {
                        number_of_channels: vec![1],
                        sample_formats: vec![
                            DaiSampleFormat::PcmSigned,
                            DaiSampleFormat::PcmUnsigned,
                        ],
                        frame_formats: vec![DaiFrameFormat::FrameFormatStandard(
                            DaiFrameFormatStandard::Tdm1,
                        )],
                        frame_rates: vec![44100],
                        bits_per_slot: vec![16],
                        bits_per_sample: vec![16],
                    },
                    DaiSupportedFormats {
                        number_of_channels: vec![2],
                        sample_formats: vec![DaiSampleFormat::PcmSigned],
                        frame_formats: vec![DaiFrameFormat::FrameFormatStandard(
                            DaiFrameFormatStandard::I2S,
                        )],
                        frame_rates: vec![8000],
                        bits_per_slot: vec![32],
                        bits_per_sample: vec![32],
                    },
                ]))
                .expect("send response okay"),
            x => panic!("expected a ready GetDaiFormats, got {:?}", x),
        };

        let result = exec.run_until_stalled(&mut supported_formats_fut).expect("response");
        let formats = result.expect("ok response");
        assert_eq!(2, formats.len());
    }
}
