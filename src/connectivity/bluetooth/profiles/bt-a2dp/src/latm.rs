// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*!

Provides a parser and de-payloader for the Low-overhead Audio Transport Multiplex (LATM) format.
This format is defined in the MPEG4 specification Part 3 (ISO-14496-3). It is used to transport
various types of encoded audio including AAC. The top level syntax structure is an AudioMuxElement
which contains metadata about the encoded audio, followed by the payload data. While the format
supports multiple subframes of data with multiple programs/layers all muxed together, current observed
A2DP sources only use one.

Note: LATM can optionally include a sycnhronization layer, but that is not handled here as
it's not used by A2DP.

# Example

```rust

use crate::latm::AudioMuxElement;

let input : &[u8] = &[0; 100 /*some bytes*/];
let audio_mux_element = AudioMuxElement::try_from_bytes(input)?;

// extract first frame of payload if present
if let Some(payload) = audio_mux_element.get_payload(0) {
    //do something with payload
}


```

*/

use anyhow::{self, format_err};
use nom::{
    bits::{
        bits,
        complete::{tag, take},
    },
    combinator::cond,
    error::{make_error, ErrorKind},
    multi::count,
    IResult, Offset,
};

// Common type used by all bit parsing functions. First entry is slice pointed at current byte.
// Second entry is current offset into that byte.
type BitsCtx<'a> = (&'a [u8], usize);

/// LATM AudioMuxElement defined in ISO-14496-3 1.7.3.2.2
#[derive(Debug, Default, PartialEq)]
pub struct AudioMuxElement<'a> {
    stream_mux_config: StreamMuxConfig,
    payloads: Vec<Payload<'a>>,
}

/// Section 1.7.3.2.3. Also stores parsed PayloadLengthInfo information
#[derive(Debug, Default, PartialEq)]
struct StreamMuxConfig {
    audio_mux_version: usize,
    all_streams_same_time_framing: usize,
    num_subframes: usize,
    programs: Vec<Program>,
    crc_checksum: Option<usize>,
}

/// Program entry in StreamMuxConfig
#[derive(Debug, Default, PartialEq)]
struct Program {
    layers: Vec<Layer>,
}

/// Layer entry in StreamMuxConfig
#[derive(Debug, Default, PartialEq)]
struct Layer {
    audio_specific_config: AudioSpecificConfig,
    frame_length_type: usize,
    latm_buffer_fullness: Option<usize>,
    frame_length: Option<usize>,
    mux_slot_length_bytes: usize,
}

/// 1.6.2.1 Decoder specific info
#[derive(Debug, Default, PartialEq)]
struct AudioSpecificConfig {
    audio_object_type: usize,
    sampling_frequency_index: usize,
    sampling_frequency: Option<usize>,
    channel_configuration: usize,
    ga_specific_config: Option<GASpecificConfig>,

    // number of bits consumed while parsing
    bits_read: usize,
}

/// 4.4.1 Decoder configuration (GASpecificConfig)
#[derive(Debug, Default, PartialEq)]
struct GASpecificConfig {
    frame_length_flag: usize,
    depends_on_core_coder: usize,
    core_coder_delay: Option<usize>,
    extension_flag: usize,
    layer_nr: Option<usize>,
}

/// Payload entry in AudioMuxElement. If payload start is byte aligned, can just reference input
/// data slice, otherwise will copy bits out.
#[derive(Debug, PartialEq)]
enum Payload<'a> {
    ByteAligned(&'a [u8]),
    Copied(Vec<u8>),
}

// Helper function used in multiple syntax elements
fn parse_latm_get_value(input: BitsCtx<'_>) -> IResult<BitsCtx<'_>, usize> {
    let mut value = 0;
    let (input, bytes_for_value) = take(2usize)(input)?;

    let mut input = input;
    for _ in 0..=bytes_for_value {
        value <<= 8;
        let (input_inner, tmp): (_, usize) = take(8usize)(input)?;
        input = input_inner;
        value += tmp;
    }

    Ok((input, value))
}

impl Program {
    fn parse_layers(
        input: BitsCtx<'_>,
        num_layers: usize,
        program_index: usize,
        audio_mux_version: usize,
        all_streams_same_time_framing: usize,
    ) -> IResult<BitsCtx<'_>, Vec<Layer>> {
        let mut layers = Vec::new();
        let mut input = input;

        for i in 0..num_layers {
            let (input_inner, layer) = Layer::parse(
                input,
                i,
                program_index,
                audio_mux_version,
                all_streams_same_time_framing,
            )?;
            input = input_inner;
            layers.push(layer);
        }

        Ok((input, layers))
    }

    pub fn parse(
        input: BitsCtx<'_>,
        program_index: usize,
        audio_mux_version: usize,
        all_streams_same_time_framing: usize,
    ) -> IResult<BitsCtx<'_>, Program> {
        let (input, num_layer): (_, usize) = take(3usize)(input)?;
        let (input, layers) = Self::parse_layers(
            input,
            num_layer + 1,
            program_index,
            audio_mux_version,
            all_streams_same_time_framing,
        )?;
        Ok((input, Program { layers }))
    }
}

impl Layer {
    fn parse_layer_config(
        audio_mux_version: usize,
    ) -> impl Fn(BitsCtx<'_>) -> IResult<BitsCtx<'_>, AudioSpecificConfig> {
        move |input| match audio_mux_version {
            0 => AudioSpecificConfig::parse(input),
            _ => {
                let (input, asc_len) = parse_latm_get_value(input)?;
                let (input, audio_specific_config) = AudioSpecificConfig::parse(input)?;

                let (input, _): (_, usize) =
                    take(asc_len - audio_specific_config.bits_read)(input)?;
                Ok((input, audio_specific_config))
            }
        }
    }

    pub fn parse(
        input: BitsCtx<'_>,
        layer_index: usize,
        program_index: usize,
        audio_mux_version: usize,
        all_streams_same_time_framing: usize,
    ) -> IResult<BitsCtx<'_>, Layer> {
        let (input, use_same_config) =
            cond(layer_index > 0 && program_index > 0, take(1usize))(input)?;

        let use_same_config = use_same_config.unwrap_or(0);

        let (input, audio_specific_config) =
            cond(use_same_config == 0, Self::parse_layer_config(audio_mux_version))(input)?;

        if let None = audio_specific_config {
            return Err(nom::Err::Failure(make_error(input, ErrorKind::Eof)));
        }

        let audio_specific_config = audio_specific_config.unwrap();

        let (input, frame_length_type) = take(3usize)(input)?;

        if frame_length_type > 1 {
            //TODO(fxbug.dev/44552) CELP, HVCX, etc..
            return Err(nom::Err::Failure(make_error(input, ErrorKind::Eof)));
        }

        let (input, latm_buffer_fullness) = cond(frame_length_type == 0, take(8usize))(input)?;

        if all_streams_same_time_framing == 0 {
            //TODO(fxbug.dev/44552) core frame offset
            return Err(nom::Err::Failure(make_error(input, ErrorKind::Eof)));
        }

        let (input, frame_length) = cond(frame_length_type == 1, take(9usize))(input)?;

        let mux_slot_length_bytes = 0usize;

        Ok((
            input,
            Layer {
                audio_specific_config,
                frame_length_type,
                latm_buffer_fullness,
                frame_length,
                mux_slot_length_bytes,
            },
        ))
    }
}

impl AudioSpecificConfig {
    fn parse(input: BitsCtx<'_>) -> IResult<BitsCtx<'_>, Self> {
        let start_input = input;
        let (input, mut audio_object_type) = take(5usize)(input)?;
        let (input, audio_object_type_ext): (_, std::option::Option<usize>) =
            cond(audio_object_type == 32, take(6usize))(input)?;

        if let Some(audio_object_type_ext) = audio_object_type_ext {
            audio_object_type += audio_object_type_ext;
        }

        let (input, sampling_frequency_index) = take(4usize)(input)?;
        let (input, sampling_frequency) =
            cond(sampling_frequency_index == 0xf, take(24usize))(input)?;
        let (input, channel_configuration) = take(4usize)(input)?;

        let (input, ga_specific_config) = match audio_object_type {
            1 | 2 | 3 | 4 | 6 | 7 | 17 | 19 | 20 | 21 | 22 | 23 => {
                let (input, config) =
                    GASpecificConfig::parse(input, channel_configuration, audio_object_type)?;
                (input, Some(config))
            }
            _ => {
                //TODO(fxbug.dev/44552) other audio object types
                return Err(nom::Err::Failure(make_error(input, ErrorKind::Eof)));
            }
        };

        let bits_read = start_input.0.offset(input.0) * 8 - start_input.1 + input.1;

        Ok((
            input,
            Self {
                audio_object_type,
                sampling_frequency_index,
                sampling_frequency,
                channel_configuration,
                ga_specific_config,
                bits_read,
            },
        ))
    }
}

impl GASpecificConfig {
    fn parse(
        input: BitsCtx<'_>,
        channel_configuration: usize,
        audio_object_type: usize,
    ) -> IResult<BitsCtx<'_>, Self> {
        let (input, frame_length_flag) = take(1usize)(input)?;
        let (input, depends_on_core_coder) = take(1usize)(input)?;

        let (input, core_coder_delay) = cond(depends_on_core_coder == 1, take(14usize))(input)?;
        let (input, extension_flag) = take(1usize)(input)?;

        if channel_configuration == 0 {
            return Err(nom::Err::Failure(make_error(input, ErrorKind::Eof)));
        }

        let (input, layer_nr) =
            cond(audio_object_type == 6 || audio_object_type == 20, take(3usize))(input)?;

        if extension_flag == 1 {
            //TODO(fxbug.dev/44552)
            return Err(nom::Err::Failure(make_error(input, ErrorKind::Eof)));
        }

        Ok((
            input,
            GASpecificConfig {
                frame_length_flag,
                depends_on_core_coder,
                core_coder_delay,
                extension_flag,
                layer_nr,
            },
        ))
    }
}

impl StreamMuxConfig {
    fn parse_programs(
        input: BitsCtx<'_>,
        count: usize,
        audio_mux_version: usize,
        all_streams_same_time_framing: usize,
    ) -> IResult<BitsCtx<'_>, Vec<Program>> {
        let mut programs = Vec::new();
        let mut input = input;

        for i in 0..count {
            let (input_inner, program) =
                Program::parse(input, i, audio_mux_version, all_streams_same_time_framing)?;
            input = input_inner;
            programs.push(program);
        }

        Ok((input, programs))
    }

    fn parse(input: BitsCtx<'_>) -> IResult<BitsCtx<'_>, StreamMuxConfig> {
        let (input, audio_mux_version) = take(1usize)(input)?;
        let (input, audio_mux_version_a) = cond(audio_mux_version != 0, take(1usize))(input)?;

        if let Some(1) = audio_mux_version_a {
            // not defined
            return Err(nom::Err::Failure(make_error(input, ErrorKind::Eof)));
        }

        let (input, _tara_buffer_fullness) =
            cond(audio_mux_version == 1, parse_latm_get_value)(input)?;
        let (input, all_streams_same_time_framing) = take(1usize)(input)?;
        let (input, num_subframes): (_, usize) = take(6usize)(input)?;
        let (input, num_program): (_, usize) = take(4usize)(input)?;

        let (input, programs) = Self::parse_programs(
            input,
            num_program + 1,
            audio_mux_version,
            all_streams_same_time_framing,
        )?;

        let (input, other_data_present): (_, u8) = take(1usize)(input)?;
        if other_data_present == 1 {
            //TODO(fxbug.dev/44552)
            return Err(nom::Err::Failure(make_error(input, ErrorKind::Eof)));
        }

        let (input, crc_check_present): (_, u8) = take(1usize)(input)?;
        let (input, crc_checksum) = cond(crc_check_present == 1, take(8usize))(input)?;

        Ok((
            input,
            StreamMuxConfig {
                audio_mux_version,
                all_streams_same_time_framing,
                num_subframes: num_subframes + 1,
                programs,
                crc_checksum,
            },
        ))
    }
}

impl<'a> Payload<'a> {
    // updates mux length stored in stream_mux_config
    fn parse_payload_length_info(
        input: BitsCtx<'a>,
        stream_mux_config: &mut StreamMuxConfig,
    ) -> IResult<BitsCtx<'a>, ()> {
        if stream_mux_config.all_streams_same_time_framing == 0 {
            //TODO(fxbug.dev/44552) different time bases
            return Err(nom::Err::Failure(make_error(input, ErrorKind::Eof)));
        }

        let mut input = input;

        for program in &mut stream_mux_config.programs {
            for layer in &mut program.layers {
                if layer.frame_length_type == 0 {
                    loop {
                        let (input_inner, tmp): (_, usize) = take(8usize)(input)?;
                        input = input_inner;
                        layer.mux_slot_length_bytes += tmp;
                        if tmp != 0xff {
                            break;
                        }
                    }
                } else {
                    //TODO(fxbug.dev/44552) mux_slot_length_coded
                    return Err(nom::Err::Failure(make_error(input, ErrorKind::Eof)));
                }
            }
        }

        Ok((input, ()))
    }

    fn parse_payloads(
        input: BitsCtx<'a>,
        stream_mux_config: &StreamMuxConfig,
    ) -> IResult<BitsCtx<'a>, Vec<Payload<'a>>> {
        let mut payloads = Vec::new();
        let mut input = input;

        if stream_mux_config.all_streams_same_time_framing == 0 {
            //TODO(fxbug.dev/44552) different time bases
            return Err(nom::Err::Failure(make_error(input, ErrorKind::Eof)));
        }

        for program in &stream_mux_config.programs {
            for layer in &program.layers {
                match layer.frame_length {
                    Some(frame_length) => {
                        if input.1 == 0 {
                            if input.0.len() < frame_length {
                                return Err(nom::Err::Failure(make_error(input, ErrorKind::Eof)));
                            }

                            payloads.push(Payload::ByteAligned(&input.0[..frame_length]));
                        } else {
                            let (input_inner, payload) = count(take(8usize), frame_length)(input)?;
                            input = input_inner;
                            payloads.push(Payload::Copied(payload));
                        }
                    }
                    None => {
                        match layer.frame_length_type {
                            0 => {
                                if input.1 == 0 {
                                    if input.0.len() < layer.mux_slot_length_bytes {
                                        return Err(nom::Err::Failure(make_error(
                                            input,
                                            ErrorKind::Eof,
                                        )));
                                    }

                                    payloads.push(Payload::ByteAligned(
                                        &input.0[0..layer.mux_slot_length_bytes],
                                    ));
                                } else {
                                    let (input_inner, payload) =
                                        count(take(8usize), layer.mux_slot_length_bytes)(input)?;
                                    input = input_inner;
                                    payloads.push(Payload::Copied(payload));
                                }
                            }
                            _ => {
                                //TODO(fxbug.dev/44552) other frame length types
                                return Err(nom::Err::Failure(make_error(input, ErrorKind::Eof)));
                            }
                        }
                    }
                }
            }
        }

        Ok((input, payloads))
    }

    fn parse(
        input: BitsCtx<'a>,
        stream_mux_config: &mut StreamMuxConfig,
    ) -> IResult<BitsCtx<'a>, Vec<Payload<'a>>> {
        let (input, _) = Self::parse_payload_length_info(input, stream_mux_config)?;
        let (input, payloads) = Self::parse_payloads(input, stream_mux_config)?;
        Ok((input, payloads))
    }
}

impl<'a> AudioMuxElement<'a> {
    fn parse_payloads(
        input: BitsCtx<'a>,
        stream_mux_config: &mut StreamMuxConfig,
    ) -> IResult<BitsCtx<'a>, Vec<Payload<'a>>> {
        let mut payloads = Vec::new();
        let mut input = input;

        for _ in 0..stream_mux_config.num_subframes {
            let (input_inner, mut subframe) = Payload::parse(input, stream_mux_config)?;
            input = input_inner;
            payloads.append(&mut subframe);
        }

        Ok((input, payloads))
    }

    fn parse_from_bits(input: BitsCtx<'a>) -> IResult<BitsCtx<'a>, AudioMuxElement<'a>> {
        // Note: assume muxConfigPresent is 1, as reccommended by RFC 3016

        let (input, use_same_stream_mux) = tag(0b0, 1usize)(input)?;
        if use_same_stream_mux == 0 {
            let (input, mut stream_mux_config) = StreamMuxConfig::parse(input)?;
            // audio_mux_version_a checked to be 0 above
            let (input, payloads) = Self::parse_payloads(input, &mut stream_mux_config)?;
            return Ok((input, AudioMuxElement { stream_mux_config, payloads }));
        } else {
            //TODO(fxbug.dev/44552) use prev stream_mux_config
            return Err(nom::Err::Failure(make_error(input, ErrorKind::Eof)));
        }
    }

    // Attempt to parse AudioMuxElement out of input slice.
    pub fn try_from_bytes(input: &'a [u8]) -> Result<AudioMuxElement<'a>, anyhow::Error> {
        // type checker was unsure about error type when mapping, specify it manually.
        bits::<_, _, _, (&[u8], ErrorKind), _>(Self::parse_from_bits)(input)
            .map(|i| i.1)
            .map_err(|_| format_err!("Failed to parse AudioMuxElement"))
    }

    // Return payload frame at `index` if it exists
    pub fn get_payload(&self, index: usize) -> Option<&[u8]> {
        if index >= self.payloads.len() {
            return None;
        }

        match &self.payloads[index] {
            Payload::ByteAligned(p) => Some(&p),
            Payload::Copied(c) => Some(&c),
        }
    }
}

#[cfg(test)]
mod test {

    use super::*;
    use matches::assert_matches;

    #[test]
    fn test_audio_specific_config() {
        // aac-lc, 44100hz, stereo
        let input: BitsCtx<'_> = (&[0x12, 0x10], 0usize);

        assert_matches!(
            AudioSpecificConfig::parse(input),
            Ok((
                (&[], 0),
                AudioSpecificConfig {
                    audio_object_type: 2,
                    sampling_frequency_index: 4,
                    sampling_frequency: None,
                    channel_configuration: 2,
                    ga_specific_config: Some(GASpecificConfig {
                        frame_length_flag: 0,
                        depends_on_core_coder: 0,
                        core_coder_delay: None,
                        extension_flag: 0,
                        layer_nr: None,
                    }),
                    bits_read: 16,
                }
            ))
        );
    }

    #[test]
    fn test_latm_get_value() {
        // 2 bytes, 0x01 << 8 | 0x01
        let input: BitsCtx<'_> = (&[0x40, 0x40, 0x40], 0usize);
        assert_matches!(parse_latm_get_value(input), Ok(((&[0x40], 2), 0x101)));

        // 1 byte, 0xff
        let input: BitsCtx<'_> = (&[0x3f, 0xc0], 0usize);
        assert_matches!(parse_latm_get_value(input), Ok(((&[0xc0], 2), 0xff)));
    }

    #[test]
    fn test_stream_mux_config() {
        // version 0, 1 subframe/prog/layer, aac-lc stereo
        let input: BitsCtx<'_> = (&[32, 0, 19, 144, 18, 39], 1);

        let (leftovers, parsed) = StreamMuxConfig::parse(input).expect("parsed ok");
        assert_eq!(leftovers, (&[39][0..], 5));
        assert_eq!(
            parsed,
            StreamMuxConfig {
                audio_mux_version: 0,
                all_streams_same_time_framing: 1,
                num_subframes: 1,
                programs: vec![Program {
                    layers: vec![Layer {
                        audio_specific_config: AudioSpecificConfig {
                            audio_object_type: 2,
                            sampling_frequency_index: 7,
                            sampling_frequency: None,
                            channel_configuration: 2,
                            ga_specific_config: Some(GASpecificConfig {
                                frame_length_flag: 0,
                                depends_on_core_coder: 0,
                                core_coder_delay: None,
                                extension_flag: 0,
                                layer_nr: None,
                            }),
                            bits_read: 16,
                        },
                        frame_length_type: 0,
                        latm_buffer_fullness: Some(145),
                        frame_length: None,
                        mux_slot_length_bytes: 0,
                    }],
                }],
                crc_checksum: None,
            }
        );
    }

    #[test]
    fn test_payload_length_info() {
        // 363 mux_slot_length_bytes
        let input: BitsCtx<'_> = (&[39, 251, 97], 5);

        let mut stream_mux_config = StreamMuxConfig {
            all_streams_same_time_framing: 1,
            num_subframes: 1,
            programs: vec![Program {
                layers: vec![Layer {
                    audio_specific_config: AudioSpecificConfig { ..AudioSpecificConfig::default() },
                    ..Layer::default()
                }],
            }],
            ..StreamMuxConfig::default()
        };

        let (leftovers, _) =
            Payload::parse_payload_length_info(input, &mut stream_mux_config).expect("parsed ok");

        assert_eq!(stream_mux_config.programs[0].layers[0].mux_slot_length_bytes, 363);
        assert_eq!(leftovers, (&[97][0..], 5usize));
    }

    #[test]
    fn test_audio_mux_element_v1() {
        const PAYLOAD_LEN: usize = 915;
        const HEADER_LEN: usize = 13;
        let header: &[u8] = &[71, 252, 0, 0, 176, 144, 128, 3, 0, 255, 255, 255, 150];
        let input: &mut [u8] = &mut [0; PAYLOAD_LEN + HEADER_LEN];
        // version 1, 1 subframe/prog/layer, aac-lc stereo, 915 bytes of zero payload, byte aligned, variable
        // length frames. Based on headers captured from Android device source
        input[0..header.len()].copy_from_slice(header);

        let reference = AudioMuxElement {
            stream_mux_config: StreamMuxConfig {
                audio_mux_version: 1,
                all_streams_same_time_framing: 1,
                num_subframes: 1,
                programs: vec![Program {
                    layers: vec![Layer {
                        audio_specific_config: AudioSpecificConfig {
                            audio_object_type: 2,
                            sampling_frequency_index: 4,
                            sampling_frequency: None,
                            channel_configuration: 2,
                            ga_specific_config: Some(GASpecificConfig {
                                frame_length_flag: 0,
                                depends_on_core_coder: 0,
                                core_coder_delay: None,
                                extension_flag: 0,
                                layer_nr: None,
                            }),
                            bits_read: 16,
                        },
                        frame_length_type: 0,
                        latm_buffer_fullness: Some(192),
                        frame_length: None,
                        mux_slot_length_bytes: PAYLOAD_LEN,
                    }],
                }],
                crc_checksum: None,
            },
            payloads: vec![Payload::ByteAligned(&[0x0; PAYLOAD_LEN])],
        };

        let parsed = AudioMuxElement::try_from_bytes(input).expect("parsed ok");

        assert_eq!(reference, parsed);

        let ref_payload = &[0x0; PAYLOAD_LEN][0..];
        assert_eq!(reference.get_payload(0).expect("has payload"), ref_payload);

        //set audioMuxVersionA to 1 and verify we fail to parse
        input[0] = 103;
        AudioMuxElement::try_from_bytes(input).expect_err("Failure to parse");

        //set useSameStream to 1 and verify we fail to parse (StreamMuxConfig is required)
        input[0] = 199;
        AudioMuxElement::try_from_bytes(input).expect_err("Failure to parse");

        //set allStreamsSameTimeRemaining to 0 and verify we error, we don't support multiple time
        //bases
        input[0] = 71;
        input[1] = 248;
        AudioMuxElement::try_from_bytes(input).expect_err("Failure to parse");
    }

    #[test]
    fn test_audio_mux_element_v0() {
        const PAYLOAD_LEN: usize = 363;
        const HEADER_LEN: usize = 9;
        let input: &mut [u8] = &mut [0; PAYLOAD_LEN + HEADER_LEN];
        // version 0, 1 subframe/prog/layer, aac-lc stereo, 363 bytes of zero payload, non-byte aligned, variable
        // length frames
        let header: &[u8] = &[0x20, 0x00, 0x13, 0x90, 0x12, 0x27, 0xfb, 0x60, 0x00];
        input[0..header.len()].copy_from_slice(header);

        let parsed = AudioMuxElement {
            stream_mux_config: StreamMuxConfig {
                audio_mux_version: 0,
                all_streams_same_time_framing: 1,
                num_subframes: 1,
                programs: vec![Program {
                    layers: vec![Layer {
                        audio_specific_config: AudioSpecificConfig {
                            audio_object_type: 2,
                            sampling_frequency_index: 7,
                            sampling_frequency: None,
                            channel_configuration: 2,
                            ga_specific_config: Some(GASpecificConfig {
                                frame_length_flag: 0,
                                depends_on_core_coder: 0,
                                core_coder_delay: None,
                                extension_flag: 0,
                                layer_nr: None,
                            }),
                            bits_read: 16,
                        },
                        frame_length_type: 0,
                        latm_buffer_fullness: Some(145),
                        frame_length: None,
                        mux_slot_length_bytes: PAYLOAD_LEN,
                    }],
                }],
                crc_checksum: None,
            },
            payloads: vec![Payload::Copied(vec![0x0; PAYLOAD_LEN])],
        };

        assert_eq!(AudioMuxElement::try_from_bytes(input).expect("parsed ok"), parsed)
    }
}
