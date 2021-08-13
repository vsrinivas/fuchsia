// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    at_commands as at,
    at_commands::{DeserializeBytes, SerDe},
    core::{
        pin::Pin,
        task::{Context, Poll},
    },
    fuchsia_bluetooth::types::Channel,
    fuchsia_zircon as zx,
    futures::{
        channel::mpsc::{self, Receiver, Sender},
        stream::{FusedStream, Stream, StreamExt},
        AsyncWrite, AsyncWriteExt,
    },
    std::{collections::HashMap, collections::VecDeque, io::Cursor},
    tracing::{debug, info, warn},
};

use super::{
    indicators::{AgIndicators, AgIndicatorsReporting, HfIndicators},
    procedure::{Procedure, ProcedureError, ProcedureMarker, ProcedureRequest},
    slc_request::SlcRequest,
    update::AgUpdate,
};

use crate::features::{AgFeatures, CodecId, HfFeatures};

/// The maximum number of concurrent procedures currently supported by this SLC.
/// This value is chosen as a number significantly more than the total number of procedures
/// supported by this implementation.
const MAX_CONCURRENT_PROCEDURES: usize = 100;

/// An update that can be received from either the Audio Gateway (AG) or the Hands Free (HF).
#[derive(Debug, Clone)]
pub enum Command {
    /// A command received from the AG.
    Ag(AgUpdate),
    /// A command received from the HF.
    Hf(at::Command),
}

impl From<AgUpdate> for Command {
    fn from(src: AgUpdate) -> Self {
        Self::Ag(src)
    }
}

impl From<at::Command> for Command {
    fn from(src: at::Command) -> Self {
        Self::Hf(src)
    }
}

/// The state associated with this service level connection.
#[derive(Clone, Debug, Default)]
pub struct SlcState {
    /// Whether the channel has been initialized with the SLCI Procedure.
    pub initialized: bool,
    /// The features of the AG.
    pub ag_features: AgFeatures,
    /// The features of the HF.
    pub hf_features: HfFeatures,
    /// The codecs supported by the HF. If initialized, None if Codec Negotiation is not supported.
    pub hf_supported_codecs: Option<Vec<CodecId>>,
    /// The indicators supported by the HF and its current status.
    pub hf_indicators: HfIndicators,
    /// The current AG indicator events reporting state.
    pub ag_indicator_events_reporting: AgIndicatorsReporting,
    /// The current indicator status of the AG.
    pub ag_indicator_status: AgIndicators,
    /// The format used when representing the network operator name on the AG.
    pub ag_network_operator_name_format: Option<at::NetworkOperatorNameFormat>,
    /// Use AG Extended Error Codes.
    pub extended_errors: bool,
    /// Enable call waiting notifications.
    pub call_waiting_notifications: bool,
    /// Enable call line identification notifications during incoming calls.
    pub call_line_ident_notifications: bool,
}

impl SlcState {
    /// Returns true if both peers support the Codec Negotiation state.
    pub fn codec_negotiation(&self) -> bool {
        self.ag_features.contains(AgFeatures::CODEC_NEGOTIATION)
            && self.hf_features.contains(HfFeatures::CODEC_NEGOTIATION)
    }

    /// Returns true if both peers support Three-way calling.
    pub fn three_way_calling(&self) -> bool {
        self.hf_features.contains(HfFeatures::THREE_WAY_CALLING)
            && self.ag_features.contains(AgFeatures::THREE_WAY_CALLING)
    }

    /// Returns true if both peers support HF Indicators.
    pub fn hf_indicators(&self) -> bool {
        self.hf_features.contains(HfFeatures::HF_INDICATORS)
            && self.ag_features.contains(AgFeatures::HF_INDICATORS)
    }

    /// Returns the codecs that are known to be supported by the peer.
    /// May be inaccurate if initialization has not completed.
    pub fn codecs_supported(&self) -> Vec<CodecId> {
        // All HFs are required to support CVSD, even if they don't support Codec Negotiation.
        // See HFP v1.8, Sec 5.7 for more information.
        self.hf_supported_codecs.clone().unwrap_or(vec![CodecId::CVSD])
    }
}

/// Contains a snapshot of the SLC state that was initialized by the SLC Initialization procedure.
/// Only data that should be logged at the start of a connection is included.
struct SlcInitializationDebug<'a>(&'a SlcState);

use std::fmt;
impl<'a> fmt::Debug for SlcInitializationDebug<'a> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("SlcState")
            .field("hf_features", &self.0.hf_features)
            .field("hf_supported_codecs", &self.0.hf_supported_codecs)
            .field("hf_indicators", &self.0.hf_indicators)
            .field("ag_indicator_events_reporting", &self.0.ag_indicator_events_reporting)
            .finish_non_exhaustive()
    }
}

/// Provides an API for managing data packets to be sent over the provided `channel`.
///   - Provides a way to queue data to be sent.
///   - Provides a way to drain and asynchronously send the queued data to the remote.
///   - Provides a stream implementation to read bytes received from the remote and send
///     any queued bytes.
struct DataController<T: Stream + AsyncWrite> {
    /// The underlying channel representing the connection with the remote.
    channel: T,
    /// Bytes that are buffered to be sent to the remote.
    buffer: Vec<u8>,
    /// Cursor on the first buffer waiting indicating the next byte to be written.
    buffer_cursor: usize,
    /// Flag indicating whether the `channel` needs to be flushed or not.
    needs_flush: bool,
}

impl<T: Stream + AsyncWrite + Unpin> DataController<T> {
    fn new(channel: T) -> Self {
        Self { channel, buffer: Vec::new(), buffer_cursor: 0, needs_flush: false }
    }

    /// Adds the provided `bytes` to the send queue.
    fn queue_data(&mut self, mut bytes: Vec<u8>) {
        self.buffer.append(&mut bytes);
    }

    /// Write all queued data to the `channel` - returns Error if writing fails.
    async fn send_queued(&mut self) -> Result<(), zx::Status> {
        let bytes = std::mem::take(&mut self.buffer);
        let result = self.channel.write_all(&bytes).await;
        if let Ok(_) = result {
            debug!("Sent {:?}", String::from_utf8_lossy(&bytes));
        }
        self.buffer_cursor = 0;
        self.needs_flush = false;
        Ok(result?)
    }

    /// Attempts to send any queued data to the `channel`.
    /// Returns Error if writing to or flushing the channel fails, OK otherwise.
    fn try_send_queued(&mut self, cx: &mut Context<'_>) -> Result<(), zx::Status> {
        while !self.buffer.is_empty() {
            let cursor = self.buffer_cursor;
            match Pin::new(&mut self.channel).poll_write(cx, &self.buffer[cursor..]) {
                Poll::Pending => {
                    // Unable to write, try again later.
                    return Ok(());
                }
                Poll::Ready(Err(e)) => {
                    warn!("Error writing bytes to channel: {:?}", e);
                    return Err(e.into());
                }
                Poll::Ready(Ok(written)) => {
                    self.buffer_cursor = cursor + written;

                    // If we've finished writing the entire buffer, we are ready to flush.
                    if self.buffer_cursor >= self.buffer.len() {
                        // Reset the pointer to the front of the buffer as we've finished writing.
                        self.buffer = Vec::new();
                        self.buffer_cursor = 0;
                        self.needs_flush = true;
                    }
                }
            }
        }

        // Attempt to flush
        if self.needs_flush {
            match Pin::new(&mut self.channel).poll_flush(cx) {
                Poll::Ready(Ok(())) => self.needs_flush = false,
                Poll::Ready(Err(e)) => return Err(e.into()),
                Poll::Pending => (),
            }
        }
        Ok(())
    }
}

impl<T> Stream for DataController<T>
where
    T: AsyncWrite + Stream<Item = Result<Vec<u8>, zx::Status>> + FusedStream + Unpin,
{
    type Item = T::Item;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        if self.is_terminated() {
            panic!("Cannot poll a terminated stream");
        }

        // Before reading on the channel, try to send any buffered messages.
        if let Err(e) = self.try_send_queued(cx) {
            return Poll::Ready(Some(Err(e)));
        }

        // Check for any data received from the peer.
        self.channel.poll_next_unpin(cx)
    }
}

impl<T> FusedStream for DataController<T>
where
    T: AsyncWrite + Stream<Item = Result<Vec<u8>, zx::Status>> + FusedStream + Unpin,
{
    fn is_terminated(&self) -> bool {
        self.channel.is_terminated()
    }
}

/// A connection between two peers that shares synchronized state and acts as the control plane for
/// HFP. See HFP v1.8, 4.2 for more information.
pub struct ServiceLevelConnection {
    /// The underlying RFCOMM connection connecting the peers.
    connection: Option<DataController<Channel>>,
    /// The current state associated with this connection.
    state: SlcState,
    /// The current active procedures serviced by this SLC.
    procedures: HashMap<ProcedureMarker, Box<dyn Procedure>>,
    /// Queued AG requests waiting for the SLC to be initialized.
    requests_pending_initialization: VecDeque<(ProcedureMarker, AgUpdate)>,
    /// The sender used to relay updates to the stream implementation.
    sender: Sender<SlcRequest>,
    /// The receiver polled by the stream implementation producing requests for more information
    /// from the HFP component.
    receiver: Receiver<SlcRequest>,
    /// The bytes that have been read from the RFCOMM connection to the remote peer but which
    /// have not yet been parsed into AT Commands.
    unparsed_bytes: DeserializeBytes,
    /// The SlcRequests that have not yet been processed.
    unprocessed_slc_requests: VecDeque<SlcRequest>,
}

impl ServiceLevelConnection {
    /// Create a new, unconnected `ServiceLevelConnection`.
    pub fn new() -> Self {
        let (sender, receiver) = mpsc::channel(MAX_CONCURRENT_PROCEDURES);
        Self {
            connection: None,
            state: SlcState::default(),
            procedures: HashMap::new(),
            requests_pending_initialization: VecDeque::new(),
            sender,
            receiver,
            unparsed_bytes: DeserializeBytes::new(),
            unprocessed_slc_requests: VecDeque::new(),
        }
    }

    /// Returns `true` if an active connection exists between the peers.
    pub fn connected(&self) -> bool {
        self.connection.as_ref().map(|ch| !ch.is_terminated()).unwrap_or(false)
    }

    /// Returns `true` if the channel has been initialized - namely the SLCI procedure has
    /// been completed for the connected channel.
    pub fn initialized(&self) -> bool {
        self.connected() && self.state.initialized
    }

    /// Returns `true` if the provided `procedure` is currently active.
    #[cfg(test)]
    fn is_active(&self, procedure: &ProcedureMarker) -> bool {
        self.procedures.contains_key(procedure)
    }

    /// Connect using the provided `channel`.
    pub fn connect(&mut self, channel: Channel) {
        // Reset the internal state before connecting the new `channel` to avoid processing
        // stale procedure requests.
        self.reset();
        self.connection = Some(DataController::new(channel));
        debug!("Initializing Service Level Connection");
    }

    /// Connects and initializes the provided `channel` with `state`.
    /// This method should be used in integration-style tests in order to bypass the
    /// back-and-forth needed to complete the SLC Initialization procedure.
    #[cfg(test)]
    pub fn initialize_at_state(&mut self, channel: Channel, state: SlcState) {
        self.connect(channel);
        self.state = state;
        self.set_initialized();
    }

    /// Sets the channel status to initialized.
    /// Note: This should only be called when the SLCI Procedure has successfully finished
    /// or in testing scenarios.
    fn set_initialized(&mut self) {
        info!("Service Level Connection initialized: {:?}", SlcInitializationDebug(&self.state));
        self.state.initialized = true;
    }

    pub fn network_operator_name_format(&self) -> &Option<at::NetworkOperatorNameFormat> {
        &self.state.ag_network_operator_name_format
    }

    /// Close the service level connection and reset the state.
    fn reset(&mut self) {
        *self = Self::new();
    }

    /// Adds the sequence of AT `messages` to the queue of outgoing data packets, until there is
    /// a serialization error..
    /// Returns Error if serialization fails, OK otherwise.
    fn queue_messages_to_peer_until_error(
        &mut self,
        messages: &[at::Response],
    ) -> Result<(), at::SerializeError<at::Response>> {
        let mut bytes = Vec::new();
        at::Response::serialize(&mut bytes, messages)?;
        if let Some(connection) = &mut self.connection {
            connection.queue_data(bytes);
        }
        Ok(())
    }

    /// Adds the sequence of AT `messages` to the queue of outgoing data packets, logging if
    /// there is a serialization error.
    fn queue_messages_to_peer(&mut self, mut messages: Vec<at::Response>) {
        while !messages.is_empty() {
            match self.queue_messages_to_peer_until_error(&messages) {
                Ok(()) => break, // Successfully sent all messages.
                Err(err) => {
                    warn!("Unable to serialize AT response with {:?}, {:?}", err.cause, err.failed);
                    messages = err.remaining;
                }
            }
        }
    }

    /// Attempts to send any queued messages to the peer via the RFCOMM `connection`.
    /// Returns Error if there was an error sending the queued messages.
    async fn send_queued(&mut self) -> Result<(), zx::Status> {
        if let Some(connection) = &mut self.connection {
            connection.send_queued().await?;
        }
        Ok(())
    }

    /// Garbage collects the provided `procedure` and returns true if it has terminated.
    fn check_and_cleanup_procedure(&mut self, procedure: &ProcedureMarker) -> bool {
        let is_terminated = self.procedures.get(procedure).map_or(false, |p| p.is_terminated());
        if is_terminated {
            self.procedures.remove(procedure);

            // Special case of the SLCI Procedure - once this is complete, the channel is
            // considered initialized.
            if *procedure == ProcedureMarker::SlcInitialization {
                self.set_initialized();
            }
        }
        is_terminated
    }

    /// Handles an error received as a result of operating a procedure.
    fn procedure_error(&mut self, error: ProcedureError) {
        // TODO(fxbug.dev/73027): Right now, we only send an Error AT response to the peer.
        // Different error cases may warrant different SLC behavior. For example, errors
        // before the SLC is initialized may warrant complete channel shutdown. Fix this method
        // to make the error handling policy decisions.

        // Only log errors that are caused by the peer at debug level. Invalid messages received
        // from the peer is out of our control, so should not be logged at info level.
        // Debug logs more verbose error details.
        if error.caused_by_peer() {
            // This is most often not indicative of an error in the case of an `UnparsableHf` since
            // HFP peers frequently "test" for support of specific AT Commands by sending them.
            debug!("Sending error to peer in response to: {}", error);
        } else {
            info!("Sending error to peer in response to: {}", error.info());
        }
        self.queue_messages_to_peer(vec![at::Response::Error])
    }

    /// Handle a procedure update.
    ///
    /// Returns a potential request for more information if the update cannot be directly
    /// handled by the SLC.
    fn procedure_request(&mut self, request: ProcedureRequest) -> Option<SlcRequest> {
        match request {
            ProcedureRequest::None => {}
            ProcedureRequest::Error(e) => {
                self.procedure_error(e);
            }
            ProcedureRequest::SendMessages(messages) => {
                // Messages to be sent to the peer via the Service Level RFCOMM Connection.
                self.queue_messages_to_peer(messages)
            }
            ProcedureRequest::Request(req) => return Some(req),
        }
        None
    }

    /// Consume and handle a command received from the local device.
    /// This method:
    ///   1) Attempts to drive the procedure with the `command`. SLCI updates are handled immediately.
    ///      Any non-SLCI updates received before the SLC has been initialized will be queued and
    ///      processed FIFO after initialization.
    ///   2) Handles any subsequent request from (1) - sending any bytes to the peer
    ///      if needed or queueing up information requests to be consumed by the internal receiver.
    pub async fn receive_ag_request(&mut self, id: ProcedureMarker, command: AgUpdate) {
        // Non-SLCI requests received before initialization will be queued for later.
        // If there are still outstanding requests pending initialization, queue the request to
        // be processed after to maintain ordering of events.
        if id != ProcedureMarker::SlcInitialization {
            if !self.initialized() || !self.requests_pending_initialization.is_empty() {
                self.requests_pending_initialization.push_back((id, command));
                return;
            }
        }
        let request = self.handle_command(id, command.into());

        // If the command requires more information, relay the request to the stream implementation.
        // Otherwise, the command was handled and we should attempt to send any queued packets.
        let info_request = match self.procedure_request(request) {
            Some(r) => r,
            None => {
                // TODO(fxbug.dev/73027): Propagate this error to PeerTask to be handled.
                if let Err(e) = self.send_queued().await {
                    warn!("Error sending queued messages: {:?}", e);
                }
                return;
            }
        };
        if let Err(e) = self.sender.try_send(info_request) {
            warn!("Couldn't relay procedure info request to internal receiver: {:?}", e);
        }
    }

    /// Consume `bytes` from the peer (HF) and handle the command.
    ///
    /// Returns the an optional request for more information if the SLC requires input
    /// from the HFP component.
    fn receive_data(&mut self, bytes: &mut Vec<u8>) {
        let procedure_requests = self.receive_data_internal(bytes);
        let mut slc_requests = procedure_requests
            .into_iter()
            .map(|req| self.procedure_request(req))
            .flatten()
            .collect();
        self.unprocessed_slc_requests.append(&mut slc_requests)
    }

    /// Consume bytes from the peer (HF), producing a parsed at::Command from the bytes and
    /// handling it. Internal helper method for `Self::receive_data`.
    ///
    /// Returns the request from handling the command.
    fn receive_data_internal(&mut self, bytes: &[u8]) -> Vec<ProcedureRequest> {
        let mut procedure_requests = Vec::new();
        let mut cursor = Cursor::new(&bytes);

        // at::Command::deserialize will parse bytes until it runs out of parseable bytes or hits
        // an error. Then it will return all parsed values with an optional error.  If there is an
        // error, there may still be bytes to parse after the error is handled.  If there is no
        // error, there are no more parseable bytes.  This outer loop repeatedly calls deserialize
        // until there is no error.
        'read_all_bytes: loop {
            let parse_result =
                at::Command::deserialize(&mut cursor, std::mem::take(&mut self.unparsed_bytes));
            // Parse the byte buffer into a HF message.
            self.unparsed_bytes = parse_result.remaining_bytes;

            // This inner loop loops over parsed values and converts them to procedure requests.
            'process_commands: for command in parse_result.values {
                debug!("Received {:?}", command);
                // Attempt to match the received command to a procedure.
                let procedure_id = match self.match_command_to_procedure(&command) {
                    Ok(id) => id,
                    Err(e) => {
                        procedure_requests.push(e.into());
                        continue 'process_commands;
                    }
                };

                // Handle the received HF commend.
                let procedure_request = self.handle_command(procedure_id, command.into());
                procedure_requests.push(procedure_request);
            }

            if let Some(err) = parse_result.error {
                procedure_requests.push(ProcedureError::UnparsableHf(err).into());
            } else {
                // If there's no error and we're out of parsed values, we've done everything we can.
                break 'read_all_bytes;
            }
        }

        procedure_requests
    }

    /// Handles the provided `command`:
    ///   - Progresses the matched procedure with the `command`.
    ///   - Garbage collects the procedure if completed.
    ///
    /// Returns the request from progressing the procedure.
    fn handle_command(
        &mut self,
        procedure_id: ProcedureMarker,
        command: Command,
    ) -> ProcedureRequest {
        // Progress the procedure with the message.
        let request = match command {
            Command::Hf(cmd) => self.hf_message(procedure_id, cmd),
            Command::Ag(cmd) => self.ag_message(procedure_id, cmd),
        };

        // Potentially clean up the procedure if this was the last stage. Procedures that
        // have been cleaned up cannot require additional responses, as this would violate
        // the `Procedure::is_terminated()` guarantee.
        if self.check_and_cleanup_procedure(&procedure_id) && request.requires_response() {
            return ProcedureError::UnexpectedRequest.into();
        }
        request
    }

    /// Matches the incoming message to a procedure. Returns the procedure identifier
    /// for the given `command` or Error if the command couldn't be matched.
    fn match_command_to_procedure(
        &self,
        command: &at::Command,
    ) -> Result<ProcedureMarker, ProcedureError> {
        // If we haven't initialized the SLC yet, the only valid procedure to match is
        // the SLCI Procedure.
        if !self.state.initialized {
            return Ok(ProcedureMarker::SlcInitialization);
        }

        // Otherwise, try to match it to a procedure - it must be a non SLCI command since
        // the channel has already been initialized.
        match ProcedureMarker::match_command(command, self.initialized()) {
            Ok(ProcedureMarker::SlcInitialization) => {
                warn!("Received unexpected SLCI command after SLC initialization: {:?}", command);
                Err(command.into())
            }
            res => res,
        }
    }

    /// Updates the the procedure specified by the `marker` with the received AG `message`.
    /// Initializes the procedure if it is not already in progress.
    /// Returns the request associated with the `message`.
    pub fn ag_message(&mut self, marker: ProcedureMarker, message: AgUpdate) -> ProcedureRequest {
        self.procedures
            .entry(marker)
            .or_insert(marker.initialize())
            .ag_update(message, &mut self.state)
    }

    /// Updates the the procedure specified by the `marker` with the received HF `message`.
    /// Initializes the procedure if it is not already in progress.
    /// Returns the request associated with the `message`.
    pub fn hf_message(
        &mut self,
        marker: ProcedureMarker,
        message: at::Command,
    ) -> ProcedureRequest {
        self.procedures
            .entry(marker)
            .or_insert(marker.initialize())
            .hf_update(message, &mut self.state)
    }

    /// Helper function to process any requests that are pending SLC initialization.
    fn process_requests_pending_initialization(
        &mut self,
        cx: &mut Context<'_>,
    ) -> Option<Result<SlcRequest, ProcedureError>> {
        if self.initialized() {
            while let Some((marker, request)) = self.requests_pending_initialization.pop_front() {
                let request = self.handle_command(marker, request.into());
                let info_req = self.procedure_request(request);
                match info_req {
                    Some(info_req) => return Some(Ok(info_req)),
                    None => {
                        if let Some(conn) = &mut self.connection {
                            if let Err(e) = conn.try_send_queued(cx) {
                                return Some(Err(e.into()));
                            }
                        }
                        continue;
                    }
                }
            }
        }
        None
    }

    /// Helper function to poll the internal channel for information requests from any procedures.
    fn poll_next_procedure_update(
        &mut self,
        cx: &mut Context<'_>,
    ) -> Poll<Option<<Self as Stream>::Item>> {
        match self.receiver.poll_next_unpin(cx) {
            Poll::Ready(Some(request)) => Poll::Ready(Some(Ok(request))),
            Poll::Ready(None) => {
                info!("Internal procedure update channel closed unexpectedly");
                Poll::Ready(None)
            }
            Poll::Pending => Poll::Pending,
        }
    }

    /// Helper function to poll the RFCOMM channel for messages from the remote peer.
    fn poll_next_data_update(
        &mut self,
        cx: &mut Context<'_>,
    ) -> Poll<Option<<Self as Stream>::Item>> {
        if let Some(slc_req) = self.unprocessed_slc_requests.pop_front() {
            return Poll::Ready(Some(Ok(slc_req)));
        }
        // Otherwise loop until we have received all bytes and sent all responses.
        loop {
            if let Some(conn) = &mut self.connection {
                match conn.poll_next_unpin(cx) {
                    Poll::Ready(Some(Ok(mut data))) => {
                        self.receive_data(&mut data);
                        let request = self.unprocessed_slc_requests.pop_front();
                        // If the SLC requires more information, bubble it up.
                        // Otherwise, try to send any queued data as a result of self.receive_data()
                        // and continue the loop to register a waker for the next read.
                        match request {
                            Some(slc_req) => return Poll::Ready(Some(Ok(slc_req))),
                            None => {
                                if let Some(conn) = &mut self.connection {
                                    if let Err(e) = conn.try_send_queued(cx) {
                                        return Poll::Ready(Some(Err(e.into())));
                                    }
                                }
                                continue;
                            }
                        }
                    }
                    Poll::Ready(Some(Err(e))) => return Poll::Ready(Some(Err(e.into()))),
                    Poll::Ready(None) => {
                        self.reset();
                        return Poll::Ready(None);
                    }
                    Poll::Pending => break,
                }
            } else {
                break;
            }
        }
        Poll::Pending
    }
}

impl Stream for ServiceLevelConnection {
    type Item = Result<SlcRequest, ProcedureError>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        if self.is_terminated() {
            panic!("Cannot poll a terminated stream");
        }

        // Try to send any queued data in the RFCOMM channel.
        if let Some(conn) = &mut self.connection {
            if let Err(e) = conn.try_send_queued(cx) {
                return Poll::Ready(Some(Err(e.into())));
            }
        }

        // Try to process any local procedure requests pending channel initialization.
        match self.process_requests_pending_initialization(cx) {
            None => {}
            request => return Poll::Ready(request),
        }

        // Check for any local procedural updates.
        match self.poll_next_procedure_update(cx) {
            Poll::Pending => {}
            request => return request,
        }

        // Check for any data received from the connection.
        self.poll_next_data_update(cx)
    }
}

impl FusedStream for ServiceLevelConnection {
    fn is_terminated(&self) -> bool {
        !self.connected()
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use {
        super::*,
        crate::peer::{
            gain_control::Gain,
            indicators::{
                AgIndicator, BATT_CHG_INDICATOR_INDEX, CALL_HELD_INDICATOR_INDEX,
                CALL_INDICATOR_INDEX,
            },
            procedure::dtmf::DtmfCode,
        },
        fuchsia_async as fasync,
        fuchsia_bluetooth::types::Channel,
        futures::io::AsyncWriteExt,
        matches::assert_matches,
        std::{convert::TryFrom, mem::Discriminant},
    };

    /// Builds and returns a connected service level connection. Returns the SLC and
    /// the remote end of the channel.
    pub fn create_and_connect_slc() -> (ServiceLevelConnection, Channel) {
        let mut slc = ServiceLevelConnection::new();
        let (local, remote) = Channel::create();
        slc.connect(local);

        (slc, remote)
    }

    /// Builds and returns a service level connection that is connected and initialized with
    /// the provided `state`.
    /// Returns the SLC and the remote end of the channel.
    pub fn create_and_initialize_slc(state: SlcState) -> (ServiceLevelConnection, Channel) {
        let mut connection = ServiceLevelConnection::new();
        let (local, remote) = Channel::create();
        connection.initialize_at_state(local, state);
        (connection, remote)
    }

    /// Expects the provided `expected` AT data to be received by the `remote` channel.
    #[track_caller]
    pub async fn expect_data_received_by_peer(remote: &mut Channel, expected: Vec<at::Response>) {
        for expected_at in expected {
            let mut bytes = Vec::new();
            assert_matches!(remote.read_datagram(&mut bytes).await, Ok(_));
            let actual_result =
                at::Response::deserialize(&mut Cursor::new(bytes), DeserializeBytes::new());
            let actual = actual_result.values.get(0).expect("valid response");
            assert_eq!(actual, &expected_at);
        }
    }

    /// Expects a message to be received by the peer. If provided, validates the contents
    /// of the received message.
    #[track_caller]
    pub fn expect_peer_ready(
        exec: &mut fasync::TestExecutor,
        remote: &mut Channel,
        expected: Option<Vec<u8>>,
    ) {
        let mut vec = Vec::new();
        let actual_bytes = {
            let mut remote_fut = Box::pin(remote.read_datagram(&mut vec));
            match exec.run_until_stalled(&mut remote_fut) {
                Poll::Ready(Ok(bytes)) => bytes,
                x => panic!("Expected ready but got: {:?}", x),
            }
        };

        if let Some(expected) = expected {
            let expected_bytes = expected.len();
            assert_eq!(actual_bytes, expected_bytes);
            assert_eq!(vec, expected);
        }
    }

    /// Expects nothing to be received by the `remote` peer.
    #[track_caller]
    fn expect_peer_pending(exec: &mut fasync::TestExecutor, remote: &mut Channel) {
        let mut vec = Vec::new();
        let mut remote_fut = Box::pin(remote.read_datagram(&mut vec));
        assert_matches!(exec.run_until_stalled(&mut remote_fut), Poll::Pending);
    }

    /// Expects the service level connection to be pending, and polls to check that it is.
    #[track_caller]
    fn expect_slc_pending(exec: &mut fasync::TestExecutor, slc: &mut ServiceLevelConnection) {
        assert_matches!(exec.run_until_stalled(&mut slc.next()), Poll::Pending);
    }

    /// Expects the service level connection to be ready, and polls to check that it is.  Checks to make
    /// sure that the returned SlcRequest is the correct variant. SlcRequests contain closures so cannot
    /// be tested for equality directly.
    #[track_caller]
    fn expect_slc_ready(
        exec: &mut fasync::TestExecutor,
        slc: &mut ServiceLevelConnection,
        expected_request_discriminant: Discriminant<SlcRequest>,
    ) {
        assert_matches!(
            exec.run_until_stalled(&mut slc.next()),
            Poll::Ready(Some(Ok(actual_request))) if
                std::mem::discriminant(&actual_request) == expected_request_discriminant
        );
    }

    /// Serializes the AT Response into a byte buffer.
    #[track_caller]
    pub fn serialize_at_response(response: at::Response) -> Vec<u8> {
        let mut buf = Vec::new();
        at::Response::serialize(&mut buf, &vec![response]).expect("serialization is ok");
        buf
    }

    /// Simulates the HFP component responding to the `slc` with the provided `update`.
    #[track_caller]
    fn do_ag_update(
        exec: &mut fasync::TestExecutor,
        slc: &mut ServiceLevelConnection,
        marker: ProcedureMarker,
        update: AgUpdate,
    ) {
        let mut fut = Box::pin(slc.receive_ag_request(marker, update));
        assert_matches!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
    }

    #[fasync::run_until_stalled(test)]
    async fn connected_state_before_and_after_connect() {
        let mut slc = ServiceLevelConnection::new();
        assert!(!slc.connected());
        let (_left, right) = Channel::create();
        slc.connect(right);
        assert!(slc.connected());
    }

    #[fasync::run_until_stalled(test)]
    async fn slc_stream_produces_items() {
        let (mut slc, mut remote) = create_and_connect_slc();

        remote.write_all(b"AT+BRSF=0\r").await.unwrap();

        let actual_request = match slc.next().await {
            Some(Ok(r)) => r,
            x => panic!("Unexpected stream item: {:?}", x),
        };
        // The BRSF should start the SLCI procedure.
        assert_matches!(actual_request, SlcRequest::GetAgFeatures { .. });
    }

    #[fasync::run_until_stalled(test)]
    async fn slc_stream_terminated() {
        let (mut slc, remote) = create_and_connect_slc();

        drop(remote);

        assert_matches!(slc.next().await, None);
        assert!(!slc.connected());
        assert!(slc.is_terminated());
    }

    #[test]
    fn slc_handles_multipart_commands() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let (mut slc, remote) = create_and_connect_slc();
        // Bypass the SLCI procedure by setting the channel to initialized.
        slc.set_initialized();

        let set_speaker_gain_part_one = b"AT+VG";
        let set_speaker_gain_part_two = b"S=1\r";

        remote.as_ref().write(set_speaker_gain_part_one).expect("Sending part one.");
        expect_slc_pending(&mut exec, &mut slc);

        remote.as_ref().write(set_speaker_gain_part_two).expect("Sending part two.");
        let slc_volume_request = SlcRequest::SpeakerVolumeSynchronization {
            level: Gain::try_from(0 as u8).unwrap(),
            response: Box::new(|| AgUpdate::Ok),
        };
        expect_slc_ready(&mut exec, &mut slc, std::mem::discriminant(&slc_volume_request));
    }

    #[test]
    fn slc_handles_multiple_commands() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let (mut slc, remote) = create_and_connect_slc();
        // Bypass the SLCI procedure by setting the channel to initialized.
        slc.set_initialized();

        let set_speaker_gain_send_dtmf = b"AT+VGS=1\rAT+VTS=#\r";

        remote.as_ref().write(set_speaker_gain_send_dtmf).expect("Sending.");

        let slc_volume_request = SlcRequest::SpeakerVolumeSynchronization {
            level: Gain::try_from(0 as u8).unwrap(),
            response: Box::new(|| AgUpdate::Ok),
        };
        expect_slc_ready(&mut exec, &mut slc, std::mem::discriminant(&slc_volume_request));

        let dtmf_request =
            SlcRequest::SendDtmf { code: DtmfCode::One, response: Box::new(Into::into) };
        expect_slc_ready(&mut exec, &mut slc, std::mem::discriminant(&dtmf_request));
    }

    // TODO(fxbug.dev/73027): Re-enable this test after error handling policies are implemented.
    #[test]
    #[ignore]
    fn unexpected_command_before_initialization_closes_channel() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let (mut slc, remote) = create_and_connect_slc();

        // Peer sends an unexpected AT command.
        let unexpected = format!("AT+CIND=?\r").into_bytes();
        let _ = remote.as_ref().write(&unexpected);

        // No requests should be received on the stream.
        expect_slc_pending(&mut exec, &mut slc);

        // Channel should be disconnected now.
        assert!(!slc.connected());
    }

    /// Tests that the SLC is resilient to a new connection being established while there
    /// is an existing one with outstanding procedures. The SLC should be completely reset
    /// and any outstanding procedures should be terminated.
    #[fasync::run_until_stalled(test)]
    async fn new_connection_when_outstanding_procedure_terminates_procedure() {
        let (mut slc, remote) = create_and_connect_slc();
        // Peer sends us HF features - we expect a request for the AG features on the
        // SLC stream.
        let slci_marker = ProcedureMarker::SlcInitialization;
        let features = HfFeatures::THREE_WAY_CALLING;
        let command = format!("AT+BRSF={}\r", features.bits()).into_bytes();
        let _ = remote.as_ref().write(&command);
        match slc.next().await {
            Some(Ok(SlcRequest::GetAgFeatures { .. })) => {}
            x => panic!("Expected a GetAgFeatures request but got: {:?}", x),
        }
        // At this point, the SLC Initialization procedure should be in progress.
        assert!(slc.is_active(&slci_marker));

        // A new connection comes through.
        let (local2, _remote2) = Channel::create();
        slc.connect(local2);

        // The old `remote` end should be closed, the SLCI procedure should no longer be in
        // progress.
        assert_matches!(remote.closed().await, Ok(()));
        assert!(!slc.is_active(&slci_marker));
    }

    #[test]
    fn completing_slc_init_procedure_initializes_channel() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let (mut slc, mut remote) = create_and_connect_slc();
        let slci_marker = ProcedureMarker::SlcInitialization;
        assert!(!slc.initialized());
        assert!(!slc.is_active(&slci_marker));

        // Peer sends us HF features - we expect a request for the AG features on the
        // SLC stream.
        let features = HfFeatures::THREE_WAY_CALLING;
        let command1 = format!("AT+BRSF={}\r", features.bits()).into_bytes();
        let _ = remote.as_ref().write(&command1);

        let response_fn1 = {
            match exec.run_until_stalled(&mut slc.next()) {
                Poll::Ready(Some(Ok(SlcRequest::GetAgFeatures { response }))) => response,
                x => panic!("Expected GetAgFeatures but got: {:?}", x),
            }
        };
        // At this point, the SLC Initialization procedure should be in progress.
        assert!(slc.is_active(&slci_marker));

        // Simulate local response with AG Features - expect these to be sent to the peer.
        let features = AgFeatures::empty();
        do_ag_update(&mut exec, &mut slc, slci_marker, response_fn1(features));
        expect_peer_ready(&mut exec, &mut remote, None);
        // No further requests - waiting on peer response.
        expect_slc_pending(&mut exec, &mut slc);

        // Peer sends us an HF supported indicators request - since the SLC can handle the request,
        // we expect no item in the SLC stream. The response should directly be sent to the peer.
        let command2 = format!("AT+CIND=?\r").into_bytes();
        let _ = remote.as_ref().write(&command2);
        expect_slc_pending(&mut exec, &mut slc);
        expect_peer_ready(&mut exec, &mut remote, None);

        // Peer requests the indicator status. Since this status is not managed by the SLC, we
        // expect a stream item to get the information.
        let command3 = format!("AT+CIND?\r").into_bytes();
        let _ = remote.as_ref().write(&command3);
        let response_fn2 = {
            match exec.run_until_stalled(&mut slc.next()) {
                Poll::Ready(Some(Ok(SlcRequest::GetAgIndicatorStatus { response }))) => response,
                x => panic!("Expected GetAgFeatures but got: {:?}", x),
            }
        };

        // Simulate local response with the AG indicators status - expect this to go to the peer.
        do_ag_update(&mut exec, &mut slc, slci_marker, response_fn2(AgIndicators::default()));
        expect_peer_ready(&mut exec, &mut remote, None);
        // No further requests - waiting on peer response.
        expect_slc_pending(&mut exec, &mut slc);

        // Peer requests to enable the Indicator Status update in the AG - since the SLC can
        // handle the request, we expect no item in the SLC stream, and the response should directly
        // be sent to the peer.
        let command4 = format!("AT+CMER=3,0,0,1\r").into_bytes();
        let _ = remote.as_ref().write(&command4);
        expect_slc_pending(&mut exec, &mut slc);
        expect_peer_ready(&mut exec, &mut remote, None);

        // The SLC should be considered initialized and the SLCI Procedure is done.
        assert!(slc.initialized());

        assert!(!slc.is_active(&slci_marker));
    }

    #[test]
    fn slc_state_codecs_supported() {
        let mut state = SlcState::default();
        // Without any codecs supported, only CVSD is returned.
        assert_eq!(vec![CodecId::CVSD], state.codecs_supported());

        // When codec negotiation is done, and we have a list, the list is returned.
        let codecs_raw: [u8; 4] = [0xf0, 0x9f, 0x92, 0x96];
        let codecs: Vec<CodecId> = codecs_raw.iter().cloned().map(Into::into).collect();
        state.hf_supported_codecs = Some(codecs.clone());
        assert_eq!(codecs, state.codecs_supported());
    }

    #[test]
    fn slci_command_after_initialization_returns_error() {
        let _exec = fasync::TestExecutor::new().unwrap();
        let (mut slc, _remote) = create_and_connect_slc();
        // Bypass the SLCI procedure by setting the channel to initialized.
        slc.set_initialized();

        // Receiving an AT command that can only be sent during the SLCI procedure thereafter should
        // be an error.
        let cmd1 = at::Command::Brsf { features: HfFeatures::empty().bits() as i64 };
        assert_matches!(
            slc.match_command_to_procedure(&cmd1.into()),
            Err(ProcedureError::UnexpectedHf(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn locally_initiated_phone_status_procedure_returns_message() {
        // Bypass the SLCI procedure by setting the channel to initialized and enable indicator
        // reporting.
        let state = SlcState {
            ag_indicator_events_reporting: AgIndicatorsReporting::new_enabled(),
            ..SlcState::default()
        };
        let (mut slc, mut remote) = create_and_initialize_slc(state);

        // Local device wants to initiate a phone status update.
        let expected_marker = ProcedureMarker::PhoneStatus;
        let status = AgIndicator::BatteryLevel(2);
        slc.receive_ag_request(expected_marker, status.into()).await;
        // We expect the PhoneStatus Procedure to be initiated and an outgoing message to the peer
        // with the status update.
        let expected_messages = vec![at::Response::Success(at::Success::Ciev {
            ind: BATT_CHG_INDICATOR_INDEX as i64,
            value: 2,
        })];
        expect_data_received_by_peer(&mut remote, expected_messages).await;

        // Since status updates require no response, the procedure should be terminated.
        assert!(!slc.is_active(&expected_marker));
    }

    #[test]
    fn ag_updates_are_queued_until_slc_initialization() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let (mut slc, mut remote) = create_and_connect_slc();
        assert!(!slc.initialized());

        // Receiving a Ag request to send the phone status update should result in no action
        // because the SLC is not initialized yet.
        let status1 = AgIndicator::Call(0);
        let expected1 = at::Response::Success(at::Success::Ciev {
            ind: CALL_INDICATOR_INDEX as i64,
            value: 0i64,
        });
        do_ag_update(&mut exec, &mut slc, ProcedureMarker::PhoneStatus, status1.into());
        expect_slc_pending(&mut exec, &mut slc);
        expect_peer_pending(&mut exec, &mut remote);

        // Peer sends us HF features - we expect a request for the AG features on the
        // SLC stream and the SLCI procedure should begin.
        let features = HfFeatures::THREE_WAY_CALLING;
        let command1 = format!("AT+BRSF={}\r", features.bits()).into_bytes();
        let _ = remote.as_ref().write(&command1);

        // Simulate local response with AG Features - expect these to be sent to the peer.
        let ag_features_update = {
            match exec.run_until_stalled(&mut slc.next()) {
                Poll::Ready(Some(Ok(SlcRequest::GetAgFeatures { response }))) => {
                    response(AgFeatures::empty())
                }
                x => panic!("Expected GetAgFeatures but got: {:?}", x),
            }
        };
        do_ag_update(&mut exec, &mut slc, ProcedureMarker::SlcInitialization, ag_features_update);
        expect_peer_ready(&mut exec, &mut remote, None);

        // Receiving another phone status amidst the SLCI procedure should be saved for later.
        let status2 = AgIndicator::CallHeld(1);
        let expected2 = at::Response::Success(at::Success::Ciev {
            ind: CALL_HELD_INDICATOR_INDEX as i64,
            value: 1i64,
        });
        do_ag_update(&mut exec, &mut slc, ProcedureMarker::PhoneStatus, status2.into());
        expect_slc_pending(&mut exec, &mut slc);
        expect_peer_pending(&mut exec, &mut remote);

        // Peer continues the SLCI procedure.
        let command2 = format!("AT+CIND=?\r").into_bytes();
        let _ = remote.as_ref().write(&command2);
        expect_slc_pending(&mut exec, &mut slc);
        expect_peer_ready(&mut exec, &mut remote, None);
        let command3 = format!("AT+CIND?\r").into_bytes();
        let _ = remote.as_ref().write(&command3);
        let ag_indicators = {
            match exec.run_until_stalled(&mut slc.next()) {
                Poll::Ready(Some(Ok(SlcRequest::GetAgIndicatorStatus { response }))) => {
                    response(AgIndicators::default())
                }
                x => panic!("Expected GetAgFeatures but got: {:?}", x),
            }
        };

        // Simulate local response with AG indicators status - expect this to go to the peer.
        do_ag_update(&mut exec, &mut slc, ProcedureMarker::SlcInitialization, ag_indicators);
        expect_peer_ready(&mut exec, &mut remote, None);
        expect_slc_pending(&mut exec, &mut slc);

        // Peer requests to enable the Indicator Status update in the AG.
        let command4 = format!("AT+CMER=3,0,0,1\r").into_bytes();
        let _ = remote.as_ref().write(&command4);
        expect_slc_pending(&mut exec, &mut slc);
        expect_peer_ready(&mut exec, &mut remote, None);

        // At this point, the mandatory portion of the SLCI procedure is complete. There are no optional
        // steps since we responded with an empty set of AgFeatures.
        assert!(slc.initialized());

        // A third request to send a PhoneStatus update _after_ SLCI completes is OK. This should
        // only be processed after any queued requests so that the peer gets the updates in order.
        let status3 = AgIndicator::CallHeld(0);
        let expected3 = at::Response::Success(at::Success::Ciev {
            ind: CALL_HELD_INDICATOR_INDEX as i64,
            value: 0i64,
        });
        do_ag_update(&mut exec, &mut slc, ProcedureMarker::PhoneStatus, status3.into());

        // The next time the SLC stream is polled, we expect the updates to be processed.
        // Since these are phone status updates, we expect the one-shot procedures to send data
        // to the peer and therefore no SLC stream items.
        expect_slc_pending(&mut exec, &mut slc);
        expect_peer_ready(&mut exec, &mut remote, Some(serialize_at_response(expected1)));
        expect_peer_ready(&mut exec, &mut remote, Some(serialize_at_response(expected2)));
        expect_peer_ready(&mut exec, &mut remote, Some(serialize_at_response(expected3)));
    }

    #[fasync::run_until_stalled(test)]
    async fn rfcomm_connection_stream_produces_items() {
        let (local, remote) = Channel::create();
        let mut connection = DataController::new(local);
        assert!(!connection.is_terminated());

        let data1 = vec![0x01, 0x02, 0x03, 0x04];
        let _ = remote.as_ref().write(&data1);
        assert_matches!(connection.next().await, Some(Ok(buf)) if buf == data1);

        let data2 = vec![0x01];
        let _ = remote.as_ref().write(&data2);
        assert_matches!(connection.next().await, Some(Ok(buf)) if buf == data2);

        drop(remote);
        assert_matches!(connection.next().await, None);
        assert!(connection.is_terminated());
    }

    #[test]
    fn queued_packets_get_sent_to_connection() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let (local, mut remote) = Channel::create();
        let mut connection = DataController::new(local);

        let mut data1 = vec![0x00, 0x02, 0x04, 0x06, 0x08];
        connection.queue_data(data1.clone());
        let mut data2 = vec![0x10, 0x11, 0x12];
        connection.queue_data(data2.clone());
        // Queueing the message shouldn't have any impact on it being sent to the peer.
        expect_peer_pending(&mut exec, &mut remote);

        // Polling the connection stream should result in messages sent to the peer. Since the
        // peer hasn't sent any data to us, we don't expect any stream items.
        assert_matches!(exec.run_until_stalled(&mut connection.next()), Poll::Pending);
        data1.append(&mut data2);
        expect_peer_ready(&mut exec, &mut remote, Some(data1));

        let data3 = vec![0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9];
        connection.queue_data(data3.clone());
        // Explicitly sending the queued data is OK.
        {
            let mut send_fut = Box::pin(connection.send_queued());
            assert_matches!(exec.run_until_stalled(&mut send_fut), Poll::Ready(Ok(())));
            expect_peer_ready(&mut exec, &mut remote, Some(data3));
        }
        // Polling the stream thereafter should have no effect. No duplicate messages.
        assert_matches!(exec.run_until_stalled(&mut connection.next()), Poll::Pending);
        expect_peer_pending(&mut exec, &mut remote);
    }

    #[fasync::run_until_stalled(test)]
    async fn read_error_result_is_propagated_to_stream() {
        // Close the local end of the channel so that local reads and remote writes
        // fail.
        let (local, remote) = Channel::create();
        assert!(remote.as_ref().half_close().is_ok());
        let mut connection = DataController::new(local);

        // Remote writing to us should fail.
        let bytes = vec![0x00, 0x03];
        assert_matches!(remote.as_ref().write(&bytes), Err(zx::Status::BAD_STATE));
        // A local read should also fail - the error should be propagated to the stream.
        assert_matches!(connection.next().await, Some(Err(zx::Status::BAD_STATE)));
    }

    #[fasync::run_until_stalled(test)]
    async fn write_error_result_is_propagated_to_stream() {
        // Close the remote end of the channel so that remote reads and local writes
        // fail.
        let (local, _remote) = Channel::create();
        assert!(local.as_ref().half_close().is_ok());
        let mut connection = DataController::new(local);

        // Queue some data to be sent to the remote.
        let bytes = vec![0x00, 0x03];
        connection.queue_data(bytes);

        // When the stream is polled, the attempted write of `bytes` should fail, and the
        // error should be propagated via the stream.
        assert_matches!(connection.next().await, Some(Err(zx::Status::IO)));

        // Trying to explicitly write the bytes should also fail.
        assert_matches!(connection.send_queued().await, Err(zx::Status::IO));
    }
}
