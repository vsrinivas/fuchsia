// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::ffx_bail,
    ffx_audio_gen_args::{GenCommand, SampleType, SineCommand, SubCommand},
    ffx_core::ffx_plugin,
};

#[ffx_plugin("audio")]
pub async fn gen_cmd(cmd: GenCommand) -> Result<()> {
    match cmd.subcommand {
        SubCommand::Sine(sine_cmd) => sine_impl(sine_cmd).await?,
    };

    Ok(())
}

async fn sine_impl(cmd: SineCommand) -> Result<()> {
    let amplitude = match cmd.amplitude {
        None => 1.0,
        Some(amplitude) => {
            if amplitude <= 0.0 || amplitude > 1.0 {
                ffx_bail!("Amplitude argument must be in range (0, 1.0]");
            }
            amplitude
        }
    };

    let _bits_per_sample = match cmd.format.sample_type {
        SampleType::Uint8 => 8,
        SampleType::Int16 => 16,
        SampleType::Int32 => 32,
        SampleType::Float32 => 32,
    };

    println!(
        "Generate sine wave with paramters: duration {}ms, frequency {}, amplitude {} ",
        cmd.duration.as_millis().to_string(),
        cmd.frequency.to_string(),
        amplitude.to_string()
    );

    let s = format!("{:?}", cmd.format.sample_type);
    println!(
        "Output file format: sample rate {}, sample type: {}, channels: {}.",
        cmd.format.sample_rate.to_string(),
        s,
        cmd.format.channels.to_string()
    );

    Ok(())
}
