// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    buffer_set::*, elementary_stream::*, input_packet_stream::*, output_validator::*, stream::*,
    Result,
};
use fidl_fuchsia_media::*;
use futures::TryStreamExt;
use std::rc::Rc;

/// Runs elementary streams through a stream processor.
pub struct StreamRunner {
    input_buffer_ordinals: OrdinalSequence,
    output_buffer_ordinals: OrdinalSequence,
    stream_lifetime_ordinals: OrdinalSequence,
    format_details_ordinals: OrdinalSequence,
    output_buffer_set: Option<BufferSet>,
    input_buffer_set: Option<BufferSet>,
    stream_processor: StreamProcessorProxy,
}

impl StreamRunner {
    pub fn new(stream_processor: StreamProcessorProxy) -> Self {
        Self {
            input_buffer_ordinals: OrdinalPattern::Odd.into_iter(),
            output_buffer_ordinals: OrdinalPattern::Odd.into_iter(),
            stream_lifetime_ordinals: OrdinalPattern::Odd.into_iter(),
            format_details_ordinals: OrdinalPattern::All.into_iter(),
            input_buffer_set: None,
            output_buffer_set: None,
            stream_processor,
        }
    }

    pub async fn run_stream(
        &mut self,
        stream: Rc<dyn ElementaryStream>,
        options: StreamOptions,
    ) -> Result<Vec<Output>> {
        let format_details_version_ordinal = get_ordinal(&mut self.format_details_ordinals);
        let stream_lifetime_ordinal = get_ordinal(&mut self.stream_lifetime_ordinals);

        vlog!(
            2,
            "Starting a stream with lifetime ordinal {} and format details ordinal {}",
            stream_lifetime_ordinal,
            format_details_version_ordinal
        );

        let mut events = self.stream_processor.take_event_stream();

        let output = {
            let mut stream = Stream {
                format_details_version_ordinal,
                stream_lifetime_ordinal,
                input_buffer_ordinals: &mut self.input_buffer_ordinals,
                input_packet_stream: self.input_buffer_set.take().map(|buffer_set| {
                    InputPacketStream::new(buffer_set, stream.stream(), stream_lifetime_ordinal)
                }),
                output_buffer_ordinals: &mut self.output_buffer_ordinals,
                output_buffer_set: self.output_buffer_set.take(),
                current_output_format: None,
                stream_processor: &mut self.stream_processor,
                stream: stream.as_ref(),
                options,
                output: vec![],
            };

            await!(stream.start())?;

            let channel_closed = loop {
                let event = if let Some(event) = await!(events.try_next())? {
                    event
                } else {
                    break true;
                };

                let control_flow = await!(stream.handle_event(event))?;
                match control_flow {
                    StreamControlFlow::Continue => {}
                    StreamControlFlow::Stop => break false,
                };
            };

            let mut output = stream.output;
            if channel_closed {
                output.push(Output::CodecChannelClose);
            }

            self.input_buffer_set =
                stream.input_packet_stream.map(|stream| stream.take_buffer_set());
            self.output_buffer_set = stream.output_buffer_set;

            output
        };

        if options.release_input_buffers_at_end {
            self.input_buffer_set = None;
        }

        if options.release_output_buffers_at_end {
            self.output_buffer_set = None;
        }

        Ok(output)
    }
}
