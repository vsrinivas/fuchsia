// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, argh::FromArgs, ffx_core::ffx_command, fidl_fuchsia_media::AudioRenderUsage};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "play",
    description = "Reads a WAV file from stdin and writes the audio data to the target.",
    example = "ffx audio play renderer --usage MEDIA --buffer-size 10 --gain 1 --mute true --clock default"
)]
pub struct PlayCommand {
    #[argh(subcommand)]
    pub subcommand: SubCommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum SubCommand {
    Render(RenderCommand),
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "render", description = "Send audio data to AudioRenderer.")]
pub struct RenderCommand {
    #[argh(
        option,
        description = "purpose of the stream being used to render audio.",
        from_str_fn(str_to_usage)
    )]
    pub usage: AudioRenderUsage,

    #[argh(
        option,
        description = "buffer size (bytes) to allocate on device VMO. used to send audio data from ffx tool to AudioRenderer."
    )]
    pub buffer_size: u32,

    #[argh(option, description = "gain (in decibels) for the renderer.")]
    pub gain: f32,

    #[argh(option, description = "mute the renderer.")]
    pub mute: bool,
}

fn str_to_usage(src: &str) -> Result<AudioRenderUsage, String> {
    match src.to_uppercase().as_str() {
        "BACKGROUND" => Ok(AudioRenderUsage::Background),
        "MEDIA" => Ok(AudioRenderUsage::Media),
        "INTERRUPTION" => Ok(AudioRenderUsage::Interruption),
        "SYSTEM-AGENT" => Ok(AudioRenderUsage::SystemAgent),
        "COMMUNICATION" => Ok(AudioRenderUsage::Communication),
        _ => Err(String::from("Couldn't parse usage.")),
    }
}
