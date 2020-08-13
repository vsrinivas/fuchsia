// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Validations on timestamps of encoded audio packets.

use crate::pcm_audio::*;
use async_trait::async_trait;
use stream_processor_test::*;

#[derive(Debug, Clone, Copy)]
struct Timestamp {
    input_index: usize,
    timestamp: u64,
}

pub struct TimestampValidator {
    frame_length: usize,
    pcm_frame_size: usize,
    timestamp_generator: Option<TimestampGenerator>,
    timestamps: Vec<Timestamp>,
}

impl TimestampValidator {
    /// Given `frame_length`, the input size of encoded output packets in terms of PCM input frames,
    /// and the input stream, creates a validator that knows what timestamps to expect on each
    /// encoded output packet.
    pub fn new(
        frame_length: usize,
        pcm_frame_size: usize,
        timestamp_generator: Option<TimestampGenerator>,
        audio_stream: &impl ElementaryStream,
    ) -> Self {
        let mut input_index = 0;
        Self {
            frame_length,
            pcm_frame_size,
            timestamp_generator,
            timestamps: audio_stream
                .stream()
                .filter_map(move |chunk| {
                    chunk.timestamp.map(|timestamp| {
                        let ts = Timestamp { input_index, timestamp };
                        input_index += chunk.data.len();
                        ts
                    })
                })
                .collect(),
        }
    }

    pub fn expected_timestamp(&self, output_packet_index: usize) -> Option<u64> {
        let input_index = output_packet_index * self.frame_length * self.pcm_frame_size;
        match self.timestamps.binary_search_by_key(&input_index, |ts| ts.input_index) {
            Ok(i) => {
                // This is a carried timestamp.
                Some(self.timestamps[i].timestamp)
            }
            Err(i) => {
                // This is a potentially extrapolated timestamp; `i - 1` is the index of the input
                // timestamp that most closely precedes this output packet.
                let preceding = &self.timestamps[i.checked_sub(1)?];

                let delta = input_index - preceding.input_index;
                if delta >= self.frame_length * self.pcm_frame_size {
                    // This timestamp has already been extrapolated and should not be
                    // extrapolated again.
                    return None;
                }

                self.timestamp_generator
                    .as_ref()
                    .map(|timestamp_generator| timestamp_generator.timestamp_at(delta))
                    .map(move |time_delta| time_delta + preceding.timestamp)
            }
        }
    }
}

#[async_trait(?Send)]
impl OutputValidator for TimestampValidator {
    async fn validate(&self, output: &[Output]) -> Result<()> {
        for (i, packet) in output_packets(output).enumerate() {
            let expected = self.expected_timestamp(i);
            let actual = packet.packet.timestamp_ish;
            if actual != expected {
                Err(FatalError(format!(
                    "Expected {:?}; got {:?} in {:?}th packet {:#?}",
                    expected, actual, i, packet.packet
                )))?;
            }
        }

        Ok(())
    }
}
