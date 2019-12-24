// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Enum for supported audio commands.
pub enum AudioMethod {
    PutInputAudio,
    StartInputInjection,
    StopInputInjection,

    StartOutputSave,
    StopOutputSave,
    GetOutputAudio,
}

impl std::str::FromStr for AudioMethod {
    type Err = anyhow::Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "PutInputAudio" => Ok(AudioMethod::PutInputAudio),
            "StartInputInjection" => Ok(AudioMethod::StartInputInjection),
            "StopInputInjection" => Ok(AudioMethod::StopInputInjection),

            "StartOutputSave" => Ok(AudioMethod::StartOutputSave),
            "StopOutputSave" => Ok(AudioMethod::StopOutputSave),
            "GetOutputAudio" => Ok(AudioMethod::GetOutputAudio),
            _ => return Err(format_err!("invalid Audio Facade method: {}", method)),
        }
    }
}
