// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    buffer_set::*, elementary_stream::*, input_packet_stream::*, output_validator::*, FatalError,
    Result,
};
use fidl_fuchsia_media::*;
use fidl_fuchsia_sysmem::BufferCollectionConstraints;
use fuchsia_stream_processors::*;
use std::{convert::TryFrom, rc::Rc};

pub type OrdinalSequence = <OrdinalPattern as IntoIterator>::IntoIter;

#[derive(Debug, Copy, Clone)]
pub struct StreamOptions {
    /// When true, the stream runner will queue format details for each stream. Otherwise it will
    /// inherit format details from the codec factory.
    pub queue_format_details: bool,
    pub release_input_buffers_at_end: bool,
    pub release_output_buffers_at_end: bool,
    pub input_buffer_collection_constraints: Option<BufferCollectionConstraints>,
    pub output_buffer_collection_constraints: Option<BufferCollectionConstraints>,
    pub stop_after_first_output: bool,
}

impl Default for StreamOptions {
    fn default() -> Self {
        Self {
            queue_format_details: true,
            release_input_buffers_at_end: false,
            release_output_buffers_at_end: false,
            input_buffer_collection_constraints: None,
            output_buffer_collection_constraints: None,
            stop_after_first_output: false,
        }
    }
}

pub struct Stream<'a> {
    pub format_details_version_ordinal: u64,
    pub stream_lifetime_ordinal: u64,
    pub input_buffer_ordinals: &'a mut OrdinalSequence,
    pub input_packet_stream:
        Option<InputPacketStream<Box<dyn Iterator<Item = ElementaryStreamChunk> + 'a>>>,
    pub output_buffer_ordinals: &'a mut OrdinalSequence,
    pub output_buffer_set: Option<BufferSet>,
    pub current_output_format: Option<Rc<ValidStreamOutputFormat>>,
    pub stream_processor: &'a mut StreamProcessorProxy,
    pub stream: &'a dyn ElementaryStream,
    pub options: StreamOptions,
    pub output: Vec<Output>,
}

pub enum StreamControlFlow {
    Continue,
    Stop,
}

impl<'a: 'b, 'b> Stream<'a> {
    pub async fn start(&'b mut self) -> Result<()> {
        if self.options.queue_format_details && self.input_packet_stream.is_some() {
            vlog!(2, "Sending input format details for follow-up stream.");
            self.stream_processor.queue_input_format_details(
                self.stream_lifetime_ordinal,
                self.stream.format_details(self.format_details_version_ordinal),
            )?;
        }

        self.send_available_input()?;

        Ok(())
    }

    pub async fn handle_event(
        &'b mut self,
        event: StreamProcessorEvent,
    ) -> Result<StreamControlFlow> {
        match event {
            StreamProcessorEvent::OnInputConstraints { input_constraints } => {
                vlog!(2, "Received input constraints.");
                vlog!(3, "Input constraints are: {:#?}", input_constraints);

                let buffer_set = BufferSetFactory::buffer_set(
                    get_ordinal(self.input_buffer_ordinals),
                    ValidStreamBufferConstraints::try_from(input_constraints)?,
                    self.stream_processor,
                    BufferSetType::Input,
                    self.options.input_buffer_collection_constraints,
                )
                .await?;

                vlog!(2, "Sending input format details in response to input constraints.");
                self.stream_processor.queue_input_format_details(
                    self.stream_lifetime_ordinal,
                    self.stream.format_details(self.format_details_version_ordinal),
                )?;

                let chunk_stream = self.stream.capped_chunks(buffer_set.buffer_size);
                self.input_packet_stream = Some(InputPacketStream::new(
                    buffer_set,
                    chunk_stream,
                    self.stream_lifetime_ordinal,
                ));
                self.send_available_input()?;
            }
            StreamProcessorEvent::OnOutputConstraints { output_config } => {
                vlog!(2, "Received output constraints.");
                vlog!(3, "Output constraints are: {:#?}", output_config);

                let constraints = ValidStreamOutputConstraints::try_from(output_config)?;
                if constraints.buffer_constraints_action_required {
                    self.output_buffer_set = Some(
                        BufferSetFactory::buffer_set(
                            get_ordinal(self.output_buffer_ordinals),
                            constraints.buffer_constraints,
                            self.stream_processor,
                            BufferSetType::Output,
                            self.options.output_buffer_collection_constraints,
                        )
                        .await?,
                    );
                }
            }
            StreamProcessorEvent::OnFreeInputPacket { free_input_packet } => {
                vlog!(2, "Received freed input packet.");
                vlog!(2, "Freed input packet is: {:#?}", free_input_packet);

                let free_input_packet = ValidPacketHeader::try_from(free_input_packet)?;
                let input_packet_stream = self.input_packet_stream.as_mut().expect(concat!(
                    "Unwrapping packet stream; ",
                    "it should be set before we ",
                    "get free input packets back."
                ));
                input_packet_stream.add_free_packet(free_input_packet)?;
                self.send_available_input()?;
            }
            StreamProcessorEvent::OnOutputFormat { output_format } => {
                vlog!(2, "Received output format.");
                vlog!(3, "Output format is: {:#?}", output_format);

                let output_format = ValidStreamOutputFormat::try_from(output_format)?;
                assert_eq!(output_format.stream_lifetime_ordinal, self.stream_lifetime_ordinal);
                self.current_output_format = Some(Rc::new(output_format));
            }
            StreamProcessorEvent::OnOutputPacket {
                output_packet,
                error_detected_before,
                error_detected_during,
            } => {
                assert!(!error_detected_before);
                assert!(!error_detected_during);
                vlog!(2, "Received output packet.");
                vlog!(3, "Output packet is: {:#?}", output_packet);

                let output_packet = ValidPacket::try_from(output_packet)?;
                self.output.push(Output::Packet(OutputPacket {
                    data: self
                        .output_buffer_set
                        .as_ref()
                        .ok_or(FatalError(String::from(concat!(
                            "There should be an output buffer set ",
                            "if we are receiving output packets"
                        ))))?
                        .read_packet(&output_packet)?,
                    format: self.current_output_format.clone().ok_or(FatalError(String::from(
                        concat!(
                            "There should be an output format set ",
                            "if we are receiving output packets"
                        ),
                    )))?,
                    packet: output_packet,
                }));

                self.stream_processor.recycle_output_packet(PacketHeader {
                    buffer_lifetime_ordinal: Some(output_packet.header.buffer_lifetime_ordinal),
                    packet_index: Some(output_packet.header.packet_index),
                })?;

                if self.options.stop_after_first_output {
                    return Ok(StreamControlFlow::Stop);
                }
            }
            StreamProcessorEvent::OnOutputEndOfStream {
                stream_lifetime_ordinal,
                error_detected_before,
            } => {
                assert!(!error_detected_before);
                vlog!(2, "Received output end of stream.");
                vlog!(3, "End of stream is for stream {}", stream_lifetime_ordinal);

                // TODO(turnage): Enable the flush method of ending stream in options.
                self.output.push(Output::Eos { stream_lifetime_ordinal });
                self.stream_processor.close_current_stream(
                    self.stream_lifetime_ordinal,
                    self.options.release_input_buffers_at_end,
                    self.options.release_output_buffers_at_end,
                )?;
                self.stream_processor.sync().await?;

                // TODO(turnage): Some codecs return all input packets explicitly, not
                //                implicitly. All codecs should return explicitly. For now
                //                we forgive it but soon we want to check that all input
                //                packets will come back.
                return Ok(StreamControlFlow::Stop);
            }
            e => {
                vlog!(2, "Got other event: {:#?}", e);
            }
        }

        Ok(StreamControlFlow::Continue)
    }

    fn send_available_input(&'b mut self) -> Result<()> {
        let input_packet_stream =
            if let Some(input_packet_stream) = self.input_packet_stream.as_mut() {
                input_packet_stream
            } else {
                return Ok(());
            };

        loop {
            match input_packet_stream.next_packet()? {
                PacketPoll::Ready(input_packet) => {
                    vlog!(2, "Sending input packet. {:?}", input_packet.valid_length_bytes);
                    self.stream_processor.queue_input_packet(input_packet)?;
                }
                PacketPoll::Eos => {
                    vlog!(2, "Sending end of stream.");
                    break Ok(self
                        .stream_processor
                        .queue_input_end_of_stream(self.stream_lifetime_ordinal)?);
                }
                PacketPoll::NotReady => break Ok(()),
            }
        }
    }
}
