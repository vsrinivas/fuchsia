// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::audio::{facade::AudioFacade, types::AudioMethod};
use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::Value;

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
