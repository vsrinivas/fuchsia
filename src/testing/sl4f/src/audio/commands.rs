// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::audio::types::AudioMethod;
use crate::server::Facade;
use anyhow::{Context, Error};
use async_trait::async_trait;
use base64;
use fidl_fuchsia_test_audio_recording::{AudioRecordingControlMarker, AudioRecordingControlProxy};
use fuchsia_component::client::connect_to_protocol;
use fuchsia_syslog::macros::fx_log_info;
use serde_json::{to_value, Value};
use std::convert::TryInto;

#[async_trait(?Send)]
impl Facade for AudioFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.parse()? {
            AudioMethod::PutInputAudio => self.put_input_audio(args).await,
            AudioMethod::StartInputInjection => self.start_input_injection(args).await,
            AudioMethod::StopInputInjection => self.stop_input_injection().await,
            AudioMethod::StartOutputSave => self.start_output_save().await,
            AudioMethod::StopOutputSave => self.stop_output_save().await,
            AudioMethod::GetOutputAudio => self.get_output_audio().await,
        }
    }
}

#[derive(Debug)]
pub struct AudioFacade {
    audio_proxy: AudioRecordingControlProxy,
}

impl AudioFacade {
    pub fn new() -> Result<AudioFacade, Error> {
        fx_log_info!("Launching audio_recording component");
        let audio_proxy = connect_to_protocol::<AudioRecordingControlMarker>()?;
        Ok(AudioFacade { audio_proxy })
    }

    pub async fn put_input_audio(&self, args: Value) -> Result<Value, Error> {
        let data = args.get("data").ok_or(format_err!("PutInputAudio failed, no data"))?;
        let data = data.as_str().ok_or(format_err!("PutInputAudio failed, data not string"))?;

        let wave_data_vec = base64::decode(data)?;
        let max_buffer_bytes = 8192;
        let wave_data_iter: Vec<&[u8]> = wave_data_vec.chunks(max_buffer_bytes).collect();

        let mut byte_cnt = 0;
        let sample_index = args["index"].as_u64().ok_or(format_err!("index not a number"))?;
        let sample_index = sample_index.try_into()?;

        let _ = self
            .audio_proxy
            .clear_input_audio(sample_index)
            .await
            .context("Error calling put_input_audio")?;

        for chunk in wave_data_iter {
            byte_cnt += self
                .audio_proxy
                .put_input_audio(sample_index, &chunk)
                .await
                .context("Error calling put_input_audio")?;
        }
        Ok(to_value(byte_cnt)?)
    }

    pub async fn start_input_injection(&self, args: Value) -> Result<Value, Error> {
        let sample_index = args["index"].as_u64().ok_or(format_err!("index not a number"))?;
        let sample_index = sample_index.try_into()?;
        let status = self
            .audio_proxy
            .start_input_injection(sample_index)
            .await
            .context("Error calling put_input_audio")?;
        match status {
            Ok(_) => return Ok(to_value(true)?),
            Err(_) => return Ok(to_value(false)?),
        }
    }

    pub async fn stop_input_injection(&self) -> Result<Value, Error> {
        let status = self
            .audio_proxy
            .stop_input_injection()
            .await
            .context("Error calling stop_input_injection")?;
        match status {
            Ok(_) => return Ok(to_value(true)?),
            Err(_) => return Ok(to_value(false)?),
        }
    }

    pub async fn start_output_save(&self) -> Result<Value, Error> {
        let status = self
            .audio_proxy
            .start_output_save()
            .await
            .context("Error calling start_output_save")?;
        match status {
            Ok(_) => return Ok(to_value(true)?),
            Err(_) => return Ok(to_value(false)?),
        }
    }

    pub async fn stop_output_save(&self) -> Result<Value, Error> {
        let status =
            self.audio_proxy.stop_output_save().await.context("Error calling stop_output_save")?;
        match status {
            Ok(_) => return Ok(to_value(true)?),
            Err(_) => return Ok(to_value(false)?),
        }
    }

    pub async fn get_output_audio(&self) -> Result<Value, Error> {
        let result =
            self.audio_proxy.get_output_audio().await.context("Error calling get_output_audio")?;
        let buffer_size = result.get_size()?;
        let mut buffer = vec![0; buffer_size.try_into().unwrap()];
        result.read(&mut buffer, 0)?;
        Ok(to_value(base64::encode(&buffer))?)
    }
}
