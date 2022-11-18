// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_audio_sub_command::SubCommand, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "audio",
    description = "Interact with the audio subsystem.",
    example = "Generate a 5s long audio signal with 440Hz frequency:
        $ ffx audio gen sine --frequency=440 --duration=5s > /tmp/sine.wav",
    note = "Output format parameters: <SampleRate>,<SampleType>,<Channels>
        SampleRate: Integer 
        SampleType options: uint8, int16, int32, float32
        Channels: <uint>ch

        example: --format=48000,float32,2ch"
)]
pub struct AudioCommand {
    #[argh(subcommand)]
    pub subcommand: SubCommand,
}
