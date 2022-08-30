// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Error, Result},
    argh::FromArgs,
    errors::ffx_bail,
    ffx_core::ffx_command,
    std::{str::FromStr, time::Duration},
};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "gen",
    description = "Generate an audio signal. Outputs a WAV file written to stdout.",
    example = "ffx audio gen sine --duration 5ms --frequency 440 --amplitude 0.5 --format 48000,int16,2ch"
)]
pub struct GenCommand {
    #[argh(subcommand)]
    pub subcommand: SubCommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum SubCommand {
    Sine(SineCommand),
    // TODO(fxbug.dev/106704) Add other signal types
    // Square,
    // Sawtooth,
    // Triangle,
    // PinkNoise,
    // WhiteNoise
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "sine", description = "Generate a sine wave signal.")]
pub struct SineCommand {
    #[argh(
        option,
        description = "duration of output signal. Examples: 5ms or 3s.",
        from_str_fn(parse_duration)
    )]
    pub duration: Duration,

    #[argh(option, description = "frequency of output wave in Hz.")]
    pub frequency: u64,

    #[argh(option, description = "signal amplitude in range (0, 1.0]. Default: 1.0.")]
    pub amplitude: Option<f32>,

    #[argh(
        option,
        description = "
            output format parameters: <SampleRate>,<SampleType>,<Channels>
            SampleRate: Integer 
            SampleType options: uint8, int16, int32, float32
            Channels: <uint>ch

            example: --format=48000,float32,2ch"
    )]
    pub format: AudioOutputFormat,
}

const DURATION_REGEX: &'static str = r"^(\d+)(h|m|s|ms)$";

/// Parses a Duration from string.
fn parse_duration(value: &str) -> Result<Duration, String> {
    let re = regex::Regex::new(DURATION_REGEX).unwrap();
    let captures = re
        .captures(&value)
        .ok_or(format!("Durations must be specified in the form {}.", DURATION_REGEX))?;
    let number: u64 = captures[1].parse().unwrap();
    let unit = &captures[2];

    match unit {
        "ms" => Ok(Duration::from_millis(number)),
        "s" => Ok(Duration::from_secs(number)),
        "m" => Ok(Duration::from_secs(number * 60)),
        "h" => Ok(Duration::from_secs(number * 3600)),
        _ => Err(format!(
            "Invalid duration string \"{}\"; must be of the form {}.",
            value, DURATION_REGEX
        )),
    }
}

#[derive(Debug, Eq, PartialEq)]
pub struct AudioOutputFormat {
    pub sample_rate: u32,
    pub sample_type: SampleType,
    pub channels: u16,
}

impl FromStr for AudioOutputFormat {
    type Err = Error;

    fn from_str(s: &str) -> Result<Self> {
        if s.len() == 0 {
            ffx_bail!("No format specified.")
        }

        let splits: Vec<&str> = s.split(",").collect();

        if splits.len() != 3 {
            ffx_bail!("Expected 3 comma-separated values: <SampleRate>,<SampleType>,<Channels> but have {}.", splits.len())
        }

        let sample_rate = match splits[0].parse::<u32>() {
            Ok(sample_rate) => sample_rate,
            Err(_) => ffx_bail!("First value (sample rate) should be an integer."),
        };

        let sample_type = match SampleType::from_str(splits[1]) {
            Ok(sample_type) => sample_type,
            Err(_) => ffx_bail!(
                "Second value (sample type) should be one of: uint8, int16, int32, float32."
            ),
        };

        let channels = match splits[2].strip_suffix("ch") {
            Some(channels) => match channels.parse::<u16>() {
                Ok(channels) => channels,
                Err(_) => ffx_bail!("Third value (channels) should have form \"<uint>ch\"."),
            },
            None => ffx_bail!("Channel argument should have form \"<uint>ch\"."),
        };

        Ok(Self { sample_rate: sample_rate, sample_type: sample_type, channels: channels })
    }
}

#[derive(Debug, Eq, PartialEq)]
pub enum SampleType {
    Uint8,
    Int16,
    Int32,
    Float32,
}

impl FromStr for SampleType {
    type Err = anyhow::Error;
    fn from_str(s: &str) -> Result<Self> {
        match s {
            "uint8" => Ok(SampleType::Uint8),
            "int16" => Ok(SampleType::Int16),
            "int32" => Ok(SampleType::Int32),
            "float32" => Ok(SampleType::Float32),
            _ => ffx_bail!("Invalid sampletype: {}.", s),
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests
#[cfg(test)]
pub mod test {
    use super::*;

    fn example_formats() -> Vec<AudioOutputFormat> {
        vec![
            AudioOutputFormat { sample_rate: 48000, sample_type: SampleType::Uint8, channels: 2 },
            AudioOutputFormat { sample_rate: 44100, sample_type: SampleType::Float32, channels: 1 },
        ]
    }

    #[test]
    fn test_format_parse() {
        let example_formats = example_formats();

        pretty_assertions::assert_eq!(
            example_formats[0],
            AudioOutputFormat::from_str("48000,uint8,2ch").unwrap()
        );

        pretty_assertions::assert_eq!(
            example_formats[1],
            AudioOutputFormat::from_str("44100,float32,1ch").unwrap()
        );

        // malformed inputs
        assert!(AudioOutputFormat::from_str("44100,float,1ch").is_err());

        assert!(AudioOutputFormat::from_str("44100").is_err());

        assert!(AudioOutputFormat::from_str("44100,float32,1").is_err());

        assert!(AudioOutputFormat::from_str("44100,float32").is_err());

        assert!(AudioOutputFormat::from_str(",,").is_err());
    }
}
