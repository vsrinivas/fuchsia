// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Error, Result},
    errors::ffx_bail,
    fidl_fuchsia_audio_ffxdaemon::*,
    hound::{WavReader, WavSpec},
    std::{cmp, str::FromStr, time::Duration},
};

pub const DURATION_REGEX: &'static str = r"^(\d+)(h|m|s|ms)$";

/// Parses a Duration from string.
pub fn parse_duration(value: &str) -> Result<Duration, String> {
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

pub fn num_samples_in_stdin_chunk(spec: WavSpec) -> u32 {
    return MAX_AUDIO_BUFFER_BYTES / bytes_per_sample(spec);
}

pub fn bytes_per_sample(spec: WavSpec) -> u32 {
    match spec.bits_per_sample {
        1..=8 => 1,
        9..=16 => 2,
        17..=32 => 4,
        _ => panic!("Unsupported value for bits_per_sample: {}", spec.bits_per_sample),
    }
}

pub fn bytes_per_frame(spec: WavSpec) -> u32 {
    bytes_per_sample(spec) * spec.channels as u32
}

pub fn packets_per_second(spec: WavSpec) -> u32 {
    let vmo_size_bytes = spec.sample_rate * bytes_per_frame(spec);
    (vmo_size_bytes as f64 / MAX_AUDIO_BUFFER_BYTES as f64).ceil() as u32
}

pub fn packets_per_file<R>(reader: &WavReader<R>) -> u64
where
    R: std::io::Read,
{
    // TODO(camlloyd): For infinite files, we need to add a custom reader of the
    // WAVE format chunk since WavReader cannot be constructed from such a header.
    let num_samples_in_file = reader.len() as u64;
    let num_bytes_in_file = num_samples_in_file * bytes_per_sample(reader.spec()) as u64;
    let buffer_size_bytes =
        reader.spec().sample_rate as u64 * bytes_per_frame(reader.spec()) as u64;

    let bytes_per_packet = cmp::min(buffer_size_bytes / 2, MAX_AUDIO_BUFFER_BYTES as u64);

    (num_bytes_in_file as f64 / bytes_per_packet as f64).ceil() as u64
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
