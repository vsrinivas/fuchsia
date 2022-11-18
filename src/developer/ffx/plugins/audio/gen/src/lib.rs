// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::ffx_bail,
    ffx_audio_gen_args::{
        AudioOutputFormat, GenCommand, PinkNoiseCommand, SampleType, SawtoothCommand, SineCommand,
        SquareCommand, SubCommand, TriangleCommand, WhiteNoiseCommand,
    },
    ffx_core::ffx_plugin,
    hound::{SampleFormat, WavSpec},
    rand::{rngs::ThreadRng, thread_rng, Rng},
    std::{f64::consts::PI, io, io::Write, time::Duration},
};

// Conversion constants for `SampleType::Uint8`.
// Note: Sample data in WAV file are stored as unsigned 8 bit values. However, the hound rust
// crate API accepts signed 8 bit Ints, and converts to unsigned 8 bit before writing.
const FLOAT_TO_INT8: i32 = -(i8::MIN as i32);

fn float_to_i8(value: f64) -> i8 {
    ((value * FLOAT_TO_INT8 as f64).round() as i16).clamp(i8::MIN as i16, i8::MAX as i16) as i8
}

// Conversion constants for `SampleType::Int16`.
const FLOAT_TO_INT16: i32 = -(i16::MIN as i32);

fn float_to_i16(value: f64) -> i16 {
    ((value * FLOAT_TO_INT16 as f64).round() as i32).clamp(i16::MIN as i32, i16::MAX as i32) as i16
}

// Conversion constants for `SampleType::Int32`.
const FLOAT_TO_INT32: i64 = -(i32::MIN as i64);

fn float_to_i32(value: f64) -> i32 {
    ((value * FLOAT_TO_INT32 as f64).round() as i64).clamp(i32::MIN as i64, i32::MAX as i64) as i32
}

// To produce pink noise, we first generate white noise then run it through a "pinking" filter to
// progressively attenuate high frequencies.
//
// This 4-stage feedforward/feedback filter attenuates by 1/f to convert white noise to pink.
const FEED_FWD: [f64; 4] = [0.049922035, -0.095993537, 0.050612699, -0.004408786];
const FEED_BACK: [f64; 4] = [1.0, -2.494956002, 2.017265875, -0.522189400];

type HistoryBuffer = [f64; 4];

// The above filtering produces a signal with an average min/max of approx [-0.20, +0.20], only
// very rarely exceeding [-0.24,0.24]. To normalize the pink-noise signal (making its loudness for
// a given amplitude closer to that of white noise), we boost our source white-noise signal by 4x.
const PINK_NOISE_SIGNAL_BOOST_FACTOR: f64 = 4.0;

struct GenericSignal {
    pub duration: Duration,
    pub frequency: Option<u64>,
    pub amplitude: Option<f64>,
    pub format: AudioOutputFormat,
    pub signal_type: SignalType,
    pub duty_cycle: Option<f64>,
}

impl GenericSignal {
    fn amplitude(&self) -> Result<f64> {
        match self.amplitude {
            Some(amplitude) => {
                if amplitude < 0.0 || amplitude > 1.0 {
                    ffx_bail!("Amplitude argument must be in range [0, 1.0]");
                }
                Ok(amplitude)
            }
            None => Ok(1.0),
        }
    }

    fn num_samples(&self) -> u32 {
        (self.format.sample_rate as f64
            * (self.duration.as_secs() as f64 + self.duration.subsec_nanos() as f64 * 1e-9))
            .round() as u32
    }

    fn spec(&self) -> WavSpec {
        WavSpec {
            channels: self.format.channels,
            sample_rate: self.format.sample_rate,
            bits_per_sample: match self.format.sample_type {
                SampleType::Uint8 => 8,
                SampleType::Int16 => 16,
                SampleType::Int32 => 32,
                SampleType::Float32 => 32,
            },
            sample_format: match self.format.sample_type {
                SampleType::Float32 => SampleFormat::Float,
                SampleType::Uint8 | SampleType::Int16 | SampleType::Int32 => SampleFormat::Int,
            },
        }
    }
}

impl From<SineCommand> for GenericSignal {
    fn from(cmd: SineCommand) -> Self {
        GenericSignal {
            duration: cmd.duration,
            frequency: Some(cmd.frequency),
            amplitude: cmd.amplitude,
            format: cmd.format,
            signal_type: SignalType::Sine,
            duty_cycle: None,
        }
    }
}
impl From<SquareCommand> for GenericSignal {
    fn from(cmd: SquareCommand) -> Self {
        GenericSignal {
            duration: cmd.duration,
            frequency: Some(cmd.frequency),
            amplitude: cmd.amplitude,
            format: cmd.format,
            signal_type: SignalType::Square,
            duty_cycle: Some(cmd.duty_cycle),
        }
    }
}

impl From<SawtoothCommand> for GenericSignal {
    fn from(cmd: SawtoothCommand) -> Self {
        GenericSignal {
            duration: cmd.duration,
            frequency: Some(cmd.frequency),
            amplitude: cmd.amplitude,
            format: cmd.format,
            signal_type: SignalType::Sawtooth,
            duty_cycle: None,
        }
    }
}

impl From<TriangleCommand> for GenericSignal {
    fn from(cmd: TriangleCommand) -> Self {
        GenericSignal {
            duration: cmd.duration,
            frequency: Some(cmd.frequency),
            amplitude: cmd.amplitude,
            format: cmd.format,
            signal_type: SignalType::Triangle,
            duty_cycle: None,
        }
    }
}

impl From<WhiteNoiseCommand> for GenericSignal {
    fn from(cmd: WhiteNoiseCommand) -> Self {
        GenericSignal {
            duration: cmd.duration,
            frequency: None,
            amplitude: cmd.amplitude,
            format: cmd.format,
            signal_type: SignalType::WhiteNoise,
            duty_cycle: None,
        }
    }
}

impl From<PinkNoiseCommand> for GenericSignal {
    fn from(cmd: PinkNoiseCommand) -> Self {
        GenericSignal {
            duration: cmd.duration,
            frequency: None,
            amplitude: cmd.amplitude,
            format: cmd.format,
            signal_type: SignalType::PinkNoise,
            duty_cycle: None,
        }
    }
}

#[derive(PartialEq)]
enum SignalType {
    Sine,
    Square,
    Sawtooth,
    Triangle,
    WhiteNoise,
    PinkNoise,
}

#[ffx_plugin("audio")]
pub async fn gen_cmd(cmd: GenCommand) -> Result<()> {
    match cmd.subcommand {
        SubCommand::Sine(cmd) => generate_signal(GenericSignal::from(cmd)).await?,
        SubCommand::Square(cmd) => generate_signal(GenericSignal::from(cmd)).await?,
        SubCommand::Sawtooth(cmd) => generate_signal(GenericSignal::from(cmd)).await?,
        SubCommand::Triangle(cmd) => generate_signal(GenericSignal::from(cmd)).await?,
        SubCommand::WhiteNoise(cmd) => generate_signal(GenericSignal::from(cmd)).await?,
        SubCommand::PinkNoise(cmd) => generate_signal(GenericSignal::from(cmd)).await?,
    }

    Ok(())
}

async fn generate_signal(cmd: GenericSignal) -> Result<()> {
    let mut cursor_writer = io::Cursor::new(Vec::<u8>::new());

    write_signal(cmd, &mut cursor_writer)?;

    let mut stdout = io::stdout().lock();
    stdout.write_all(&cursor_writer.into_inner())?;
    Ok(())
}

fn write_signal(command: GenericSignal, cursor_writer: &mut io::Cursor<Vec<u8>>) -> Result<()> {
    let mut writer = hound::WavWriter::new(cursor_writer, command.spec()).unwrap();
    let frames_per_period =
        (command.spec().sample_rate as f64) / command.frequency.unwrap_or(1) as f64;
    let rads_per_frame = (2.0 * PI) / frames_per_period;

    let mut rng = thread_rng();
    let mut input_history: Vec<HistoryBuffer> =
        Vec::with_capacity(command.spec().channels as usize);
    let mut output_history: Vec<HistoryBuffer> =
        Vec::with_capacity(command.spec().channels as usize);

    if command.signal_type == SignalType::PinkNoise {
        // Skip the filter's initial transient response by pre-generating 1430 frames, the filter's T60
        // (-60 decay) interval, computed by "T60 = round(log(1000)/(1-max(abs(roots(FeedBack)))))"
        for _chan in 0..command.spec().channels {
            input_history.push([0.0; 4]);
            output_history.push([0.0; 4]);
        }

        for _i in 0..1430 {
            for channel in 0..command.spec().channels {
                let _ = next_pink_noise_sample(
                    channel as usize,
                    &mut input_history[..],
                    &mut output_history[..],
                    &mut rng,
                );
            }
        }
    }

    for frame in 0..command.num_samples() {
        for channel in 0..command.spec().channels {
            let value: f64 = match command.signal_type {
                SignalType::Sine => (rads_per_frame * frame as f64).sin(),
                SignalType::Square => {
                    if (frame as f64 % frames_per_period)
                        >= (frames_per_period * command.duty_cycle.unwrap_or(0.5))
                    {
                        -1.0
                    } else {
                        1.0
                    }
                }
                SignalType::Sawtooth => (((frame as f64 / frames_per_period) % 1.0) * 2.0) - 1.0,
                SignalType::Triangle => {
                    ((((frame as f64 / frames_per_period) % 1.0) - 0.5).abs() * 4.0) - 1.0
                }
                SignalType::WhiteNoise => rng.gen_range(0.0..=1.0) * 2.0 - 1.0,
                SignalType::PinkNoise => next_pink_noise_sample(
                    channel as usize,
                    &mut input_history[..],
                    &mut output_history[..],
                    &mut rng,
                ),
            };

            match command.spec().bits_per_sample {
                8 => {
                    let converted_sample = float_to_i8(value * command.amplitude().unwrap());
                    writer.write_sample(converted_sample).unwrap()
                }
                16 => {
                    let converted_sample: i16 = float_to_i16(value * command.amplitude().unwrap());
                    writer.write_sample(converted_sample).unwrap()
                }
                32 => match command.spec().sample_format {
                    hound::SampleFormat::Int => {
                        let converted_sample: i32 =
                            float_to_i32(value * command.amplitude().unwrap());

                        writer.write_sample(converted_sample).unwrap()
                    }
                    hound::SampleFormat::Float => {
                        writer.write_sample((value * command.amplitude().unwrap()) as f32).unwrap()
                    }
                },
                _ => {}
            }
        }
    }

    writer.finalize()?;
    Ok(())
}

fn next_pink_noise_sample(
    channel: usize,
    input_history: &mut [HistoryBuffer],
    output_history: &mut [HistoryBuffer],
    rng: &mut ThreadRng,
) -> f64 {
    // First, shift our previous inputs and outputs into the past, by one frame
    for i in (1..=3).rev() {
        output_history[channel][i] = output_history[channel][i - 1];
        input_history[channel][i] = input_history[channel][i - 1];
    }

    // Second, generate the initial white-noise input, boosting to normalize the result.
    input_history[channel][0] = rng.gen_range(0.0..=1.0) * 2.0 - 1.0;
    input_history[channel][0] *= PINK_NOISE_SIGNAL_BOOST_FACTOR;

    // Finally, apply the filter to {input + cached input/output values} to get the new output val.
    output_history[channel][0] = (input_history[channel][0] * FEED_FWD[0]
        + input_history[channel][1] * FEED_FWD[1]
        + input_history[channel][2] * FEED_FWD[2]
        + input_history[channel][3] * FEED_FWD[3])
        - (output_history[channel][1] * FEED_BACK[1]
            + output_history[channel][2] * FEED_BACK[2]
            + output_history[channel][3] * FEED_BACK[3]);

    output_history[channel][0]
}

////////////////////////////////////////////////////////////////////////////////
// tests
#[cfg(test)]
pub mod test {
    use super::*;
    use hound::WavReader;

    struct ExampleSignals {
        sine_u8_max_amp: GenericSignal,
        sine_u8_mid_amp: GenericSignal,
        sine_int16_min_amp: GenericSignal,
        sine_int16_max_amp: GenericSignal,
        sine_int32_max_amp: GenericSignal,
        square_int16_max_amp: GenericSignal,
        square_int32_max_amp: GenericSignal,
        square_int32_mid_amp: GenericSignal,
        square_float32_max_amp: GenericSignal,
        square_float32_mid_amp: GenericSignal,
    }

    // static DATA_START: usize = 44; // The header of a WAV (RIFF) file is 44 bytes.
    fn example_signals() -> ExampleSignals {
        ExampleSignals {
            sine_u8_max_amp: GenericSignal {
                duration: Duration::from_millis(10),
                frequency: Some(250),
                amplitude: Some(1.0),
                format: AudioOutputFormat {
                    sample_rate: 48000,
                    sample_type: SampleType::Uint8,
                    channels: 1,
                },
                signal_type: SignalType::Sine,
                duty_cycle: None,
            },
            sine_u8_mid_amp: GenericSignal {
                duration: Duration::from_millis(10),
                frequency: Some(250),
                amplitude: Some(0.5),
                format: AudioOutputFormat {
                    sample_rate: 48000,
                    sample_type: SampleType::Uint8,
                    channels: 1,
                },
                signal_type: SignalType::Sine,
                duty_cycle: None,
            },

            sine_int16_min_amp: GenericSignal {
                duration: Duration::from_millis(10),
                frequency: Some(0),
                amplitude: Some(0.0),
                format: AudioOutputFormat {
                    sample_rate: 48000,
                    sample_type: SampleType::Int16,
                    channels: 1,
                },
                signal_type: SignalType::Sine,
                duty_cycle: None,
            },
            sine_int16_max_amp: GenericSignal {
                duration: Duration::from_millis(100),
                frequency: Some(500),
                amplitude: Some(1.0),
                format: AudioOutputFormat {
                    sample_rate: 48000,
                    sample_type: SampleType::Int16,
                    channels: 1,
                },
                signal_type: SignalType::Sine,
                duty_cycle: None,
            },
            sine_int32_max_amp: GenericSignal {
                duration: Duration::from_millis(100),
                frequency: Some(10),
                amplitude: Some(1.0),
                format: AudioOutputFormat {
                    sample_rate: 48000,
                    sample_type: SampleType::Int32,
                    channels: 1,
                },
                signal_type: SignalType::Sine,
                duty_cycle: None,
            },
            square_int16_max_amp: GenericSignal {
                duration: Duration::from_millis(100),
                frequency: Some(250),
                amplitude: Some(1.0),
                format: AudioOutputFormat {
                    sample_rate: 48000,
                    sample_type: SampleType::Int16,
                    channels: 1,
                },
                signal_type: SignalType::Square,
                duty_cycle: None,
            },
            square_int32_max_amp: GenericSignal {
                duration: Duration::from_millis(100),
                frequency: Some(250),
                amplitude: Some(1.0),
                format: AudioOutputFormat {
                    sample_rate: 48000,
                    sample_type: SampleType::Int32,
                    channels: 1,
                },
                signal_type: SignalType::Square,
                duty_cycle: None,
            },
            square_int32_mid_amp: GenericSignal {
                duration: Duration::from_millis(100),
                frequency: Some(250),
                amplitude: Some(0.5),
                format: AudioOutputFormat {
                    sample_rate: 48000,
                    sample_type: SampleType::Int32,
                    channels: 2,
                },
                signal_type: SignalType::Square,
                duty_cycle: None,
            },
            square_float32_mid_amp: GenericSignal {
                duration: Duration::from_millis(100),
                frequency: Some(250),
                amplitude: Some(0.5),
                format: AudioOutputFormat {
                    sample_rate: 48000,
                    sample_type: SampleType::Float32,
                    channels: 1,
                },
                signal_type: SignalType::Square,
                duty_cycle: None,
            },
            square_float32_max_amp: GenericSignal {
                duration: Duration::from_millis(100),
                frequency: Some(250),
                amplitude: Some(1.0),
                format: AudioOutputFormat {
                    sample_rate: 48000,
                    sample_type: SampleType::Float32,
                    channels: 2,
                },
                signal_type: SignalType::Square,
                duty_cycle: None,
            },
        }
    }

    // num channels
    // length/duration)
    #[test]
    fn test_silence() {
        let mut cursor_writer = io::Cursor::new(Vec::<u8>::new());

        write_signal(example_signals().sine_int16_min_amp, &mut cursor_writer).unwrap();
        cursor_writer.set_position(0);
        let mut reader = WavReader::new(cursor_writer).unwrap();

        reader.samples::<i16>().for_each(|s| assert_eq!(0.0, s.unwrap() as f64));
    }

    #[test]
    fn test_u8_sine_wave_max_amp() {
        let mut cursor_writer = io::Cursor::new(Vec::<u8>::new());
        let signal = example_signals().sine_u8_max_amp;
        let frames_per_period = signal.format.sample_rate as u64 / signal.frequency.unwrap();

        write_signal(signal, &mut cursor_writer).unwrap();

        cursor_writer.set_position(0);
        let mut reader = WavReader::new(cursor_writer).unwrap();

        reader.samples::<i8>().for_each(|s| {
            let r = s.unwrap();
            println!("{}", r);
        });

        reader.seek(0).unwrap();

        // Silence every half period, starting from beginning
        reader.samples::<i8>().step_by((frames_per_period / 2) as usize).for_each(|s| {
            let r = s.unwrap();
            assert_eq!(0, r);
        });

        reader.seek(0).unwrap();

        // Peaks
        reader
            .samples::<i8>()
            .skip((frames_per_period / 4) as usize)
            .step_by(frames_per_period as usize)
            .for_each(|s| {
                let r = s.unwrap();
                println!("{}", r);
                assert_eq!(i8::MAX, r);
            });

        reader.seek(0).unwrap();
        // Troughs
        reader
            .samples::<i8>()
            .skip((frames_per_period / 4 + frames_per_period / 2) as usize)
            .step_by(frames_per_period as usize)
            .for_each(|s| {
                let r = s.unwrap();
                println!("{}", r);
                assert_eq!(i8::MIN, r);
            });
    }

    #[test]
    fn test_u8_sine_wave_mid_amp() {
        let mut cursor_writer = io::Cursor::new(Vec::<u8>::new());
        let signal = example_signals().sine_u8_mid_amp;
        let frames_per_period = signal.format.sample_rate as u64 / signal.frequency.unwrap();

        write_signal(signal, &mut cursor_writer).unwrap();

        cursor_writer.set_position(0);
        let mut reader = WavReader::new(cursor_writer).unwrap();

        reader.samples::<i8>().for_each(|s| {
            let r = s.unwrap();
            println!("{}", r);
        });

        reader.seek(0).unwrap();

        // Silence every half period, starting from beginning
        reader.samples::<i8>().step_by((frames_per_period / 2) as usize).for_each(|s| {
            let r = s.unwrap();
            assert_eq!(0, r);
        });

        reader.seek(0).unwrap();

        // Peaks
        reader
            .samples::<i8>()
            .skip((frames_per_period / 4) as usize)
            .step_by(frames_per_period as usize)
            .for_each(|s| {
                let r = s.unwrap();
                // Round here since we round the sample value when converting from f64 to i8.
                assert_eq!((i8::MAX as f32 / 2.0).round() as i8, r);
            });

        reader.seek(0).unwrap();
        // Troughs
        reader
            .samples::<i8>()
            .skip((frames_per_period / 4 + frames_per_period / 2) as usize)
            .step_by(frames_per_period as usize)
            .for_each(|s| {
                let r = s.unwrap();
                println!("{}", r);
                assert_eq!(i8::MIN / 2, r);
            });
    }

    #[test]
    fn test_i16_square_wave() {
        let mut cursor_writer = io::Cursor::new(Vec::<u8>::new());

        write_signal(example_signals().square_int16_max_amp, &mut cursor_writer).unwrap();

        cursor_writer.set_position(0);
        let mut reader = WavReader::new(cursor_writer).unwrap();

        reader.samples::<i16>().for_each(|s| {
            let r = s.unwrap();
            println!("{}", r);
        });
        reader.seek(0).unwrap();
        let max_vals = reader
            .samples::<i16>()
            .filter(|s| {
                let r = s.as_ref().unwrap();
                *r == i16::MAX
            })
            .count();

        reader.seek(0).unwrap();
        let min_vals = reader
            .samples::<i16>()
            .filter(|s| {
                let r = s.as_ref().unwrap();
                *r == i16::MIN
            })
            .count();

        // Duty Cycle 50%
        assert_eq!(max_vals, min_vals);
    }

    #[test]
    fn test_i32_square_wave() {
        let mut cursor_writer = io::Cursor::new(Vec::<u8>::new());

        write_signal(example_signals().square_int32_max_amp, &mut cursor_writer).unwrap();

        cursor_writer.set_position(0);
        let mut reader = WavReader::new(cursor_writer).unwrap();

        reader.samples::<i32>().for_each(|s| {
            let r = s.unwrap();
            println!("{}", r);
        });
        reader.seek(0).unwrap();
        let max_vals = reader
            .samples::<i32>()
            .filter(|s| {
                let r = s.as_ref().unwrap();
                *r == i32::MAX
            })
            .count();

        reader.seek(0).unwrap();
        let min_vals = reader
            .samples::<i32>()
            .filter(|s| {
                let r = s.as_ref().unwrap();
                *r == i32::MIN
            })
            .count();

        // Duty Cycle 50%
        assert_eq!(max_vals, min_vals);
    }

    #[test]
    fn test_i32_square_wave_mid_amp() {
        let mut cursor_writer = io::Cursor::new(Vec::<u8>::new());

        write_signal(example_signals().square_int32_mid_amp, &mut cursor_writer).unwrap();

        cursor_writer.set_position(0);
        let mut reader = WavReader::new(cursor_writer).unwrap();

        reader.samples::<i32>().for_each(|s| {
            let r = s.unwrap();
            println!("{}", r);
        });
        reader.seek(0).unwrap();
        let max_vals = reader
            .samples::<i32>()
            .filter(|s| {
                let r = s.as_ref().unwrap();
                *r == (i32::MAX as f32 / 2.0).round() as i32
            })
            .count();

        reader.seek(0).unwrap();
        let min_vals = reader
            .samples::<i32>()
            .filter(|s| {
                let r = s.as_ref().unwrap();
                *r == i32::MIN / 2
            })
            .count();

        // Duty Cycle 50%
        println!("{}{}", max_vals, min_vals);
        assert_eq!(max_vals, min_vals);
    }

    #[test]
    fn test_sine_i16() {
        let mut cursor_writer = io::Cursor::new(Vec::<u8>::new());
        let signal = example_signals().sine_int16_max_amp;
        let frames_per_period = signal.format.sample_rate as u64 / signal.frequency.unwrap();

        write_signal(signal, &mut cursor_writer).unwrap();

        cursor_writer.set_position(0);
        let mut reader = WavReader::new(cursor_writer).unwrap();

        reader.seek(0).unwrap();
        // Silence every half period, starting from beginning
        reader.samples::<i16>().step_by((frames_per_period / 2) as usize).for_each(|s| {
            let r = s.unwrap();
            assert_eq!(0, r);
        });

        reader.seek(0).unwrap();
        reader.samples::<i16>().for_each(|s| {
            let r = s.unwrap();
            println!("{}", r);
        });
        reader.seek(0).unwrap();

        // Peaks
        reader
            .samples::<i16>()
            .skip((frames_per_period / 4) as usize)
            .step_by(frames_per_period as usize)
            .for_each(|s| {
                let r = s.unwrap();
                println!("{}", r);
                assert_eq!(i16::MAX, r);
            });

        reader.seek(0).unwrap();
        // Troughs
        reader
            .samples::<i16>()
            .skip((frames_per_period / 4 + frames_per_period / 2) as usize)
            .step_by(frames_per_period as usize)
            .for_each(|s| {
                let r = s.unwrap();
                println!("{}", r);
                assert_eq!(i16::MIN, r);
            });
    }

    #[test]
    fn test_sine_i32() {
        let mut cursor_writer = io::Cursor::new(Vec::<u8>::new());
        let signal = example_signals().sine_int32_max_amp;
        let frames_per_period = signal.format.sample_rate as u64 / signal.frequency.unwrap();

        write_signal(signal, &mut cursor_writer).unwrap();

        cursor_writer.set_position(0);
        let mut reader = WavReader::new(cursor_writer).unwrap();
        reader.seek(0).unwrap();

        reader.samples::<i32>().for_each(|s| {
            let r = s.unwrap();
            println!("{}", r);
        });

        reader.seek(0).unwrap();

        // Silence every half period, starting from beginning
        reader.samples::<i32>().step_by((frames_per_period / 2) as usize).for_each(|s| {
            let r = s.unwrap();
            assert_eq!(0, r);
        });

        reader.seek(0).unwrap();
        reader.samples::<i32>().for_each(|s| {
            let r = s.unwrap();
            println!("{}", r);
        });
        reader.seek(0).unwrap();

        // Peaks
        reader
            .samples::<i32>()
            .skip((frames_per_period / 4) as usize)
            .step_by(frames_per_period as usize)
            .for_each(|s| {
                let r = s.unwrap();
                println!("{}", r);
                assert_eq!(i32::MAX, r);
            });

        reader.seek(0).unwrap();
        // Troughs
        reader
            .samples::<i32>()
            .skip((frames_per_period / 4 + frames_per_period / 2) as usize)
            .step_by(frames_per_period as usize)
            .for_each(|s| {
                let r = s.unwrap();
                println!("{}", r);
                assert_eq!(i32::MIN, r);
            });
    }

    #[test]
    fn test_square_f32_mid_amp() {
        let mut cursor_writer = io::Cursor::new(Vec::<u8>::new());

        write_signal(example_signals().square_float32_mid_amp, &mut cursor_writer).unwrap();

        cursor_writer.set_position(0);
        let mut reader = WavReader::new(cursor_writer).unwrap();

        reader.samples::<f32>().for_each(|s| {
            let r = s.unwrap();
            println!("{}", r);
        });
        reader.seek(0).unwrap();
        let max_vals = reader
            .samples::<f32>()
            .filter(|s| {
                let r = s.as_ref().unwrap();
                *r == f32::MAX / 2.0
            })
            .count();

        reader.seek(0).unwrap();
        let min_vals = reader
            .samples::<f32>()
            .filter(|s| {
                let r = s.as_ref().unwrap();
                *r == f32::MIN / 2.0
            })
            .count();

        // Duty Cycle 50%
        assert_eq!(max_vals, min_vals);
    }

    #[test]
    fn test_square_f32_max_amp() {
        let mut cursor_writer = io::Cursor::new(Vec::<u8>::new());

        write_signal(example_signals().square_float32_max_amp, &mut cursor_writer).unwrap();

        cursor_writer.set_position(0);
        let mut reader = WavReader::new(cursor_writer).unwrap();

        reader.samples::<f32>().for_each(|s| {
            let r = s.unwrap();
            println!("{}", r);
        });
        reader.seek(0).unwrap();
        let max_vals = reader
            .samples::<f32>()
            .filter(|s| {
                let r = s.as_ref().unwrap();
                *r == f32::MAX
            })
            .count();

        reader.seek(0).unwrap();
        let min_vals = reader
            .samples::<f32>()
            .filter(|s| {
                let r = s.as_ref().unwrap();
                *r == f32::MIN
            })
            .count();

        // Duty Cycle 50%
        assert_eq!(max_vals, min_vals);
    }

    #[test]
    fn test_num_samples() {
        let signal = GenericSignal {
            duration: Duration::from_millis(10),
            frequency: Some(0),
            amplitude: Some(0.0),
            format: AudioOutputFormat {
                sample_rate: 48000,
                sample_type: SampleType::Int16,
                channels: 1,
            },
            signal_type: SignalType::Sine,
            duty_cycle: None,
        };
        assert_eq!(signal.num_samples(), 480);
    }
    #[test]
    fn test_sample_conversions() {
        // Float to i8's
        assert_eq!(float_to_i8(-1.0), -128);
        assert_eq!(float_to_i8(-0.0), 0x00);
        assert_eq!(float_to_i8(1.0), i8::MAX);
        assert_eq!(float_to_i8(0.75), 0x60);

        // Float to i16s
        assert_eq!(float_to_i16(-1.0), i16::MIN);
        assert_eq!(float_to_i16(-0.5), -0x4000);
        assert_eq!(float_to_i16(0.0), 0);
        assert_eq!(float_to_i16(0.75), 0x6000);

        // Float to i32s
        assert_eq!(float_to_i32(-1.0), i32::MIN);
        assert_eq!(float_to_i32(1.0), i32::MAX);
        assert_eq!(float_to_i32(-0.5), -0x40000000);
        assert_eq!(float_to_i32(0.0), 0);
        assert_eq!(float_to_i32(0.75), 0x60000000);
    }
}
