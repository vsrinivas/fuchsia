// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    buffer_set::*, elementary_stream::*, input_packet_stream::*, output_validator::*, stream::*,
    Result,
};
use fidl::endpoints::*;
use fidl_fuchsia_media::*;
use fidl_fuchsia_mediacodec::*;
use fuchsia_component::client;
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
    current_codec: Option<StreamProcessorProxy>,
}

impl StreamRunner {
    pub fn new() -> Self {
        Self {
            input_buffer_ordinals: OrdinalPattern::Odd.into_iter(),
            output_buffer_ordinals: OrdinalPattern::Odd.into_iter(),
            stream_lifetime_ordinals: OrdinalPattern::Odd.into_iter(),
            format_details_ordinals: OrdinalPattern::All.into_iter(),
            input_buffer_set: None,
            output_buffer_set: None,
            current_codec: None,
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

        let mut codec = if let Some(codec) = self.current_codec.take() {
            codec
        } else {
            // TODO(turnage): Accept parameters for using a decoder vs encoder,
            // and their parameters.
            await!(get_decoder(stream.as_ref(), format_details_version_ordinal))?
        };
        let mut events = codec.take_event_stream();

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
                codec: &mut codec,
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

        self.current_codec = Some(codec);

        if options.release_input_buffers_at_end {
            self.input_buffer_set = None;
        }

        if options.release_output_buffers_at_end {
            self.output_buffer_set = None;
        }

        Ok(output)
    }
}

async fn get_decoder(
    stream: &ElementaryStream,
    format_details_version_ordinal: u64,
) -> Result<StreamProcessorProxy> {
    let factory = client::connect_to_service::<CodecFactoryMarker>()?;
    let (decoder_client_end, decoder_request) = create_endpoints()?;
    let decoder = decoder_client_end.into_proxy()?;
    // TODO(turnage): Account for all error reporting methods in the runner options and output.
    factory.create_decoder(
        CreateDecoderParams {
            input_details: Some(stream.format_details(format_details_version_ordinal)),
            promise_separate_access_units_on_input: Some(stream.is_access_units()),
            require_can_stream_bytes_input: Some(false),
            require_can_find_start: Some(false),
            require_can_re_sync: Some(false),
            require_report_all_detected_errors: Some(false),
            require_hw: Some(false),
            permit_lack_of_split_header_handling: Some(true),
        },
        decoder_request,
    )?;
    Ok(decoder)
}
