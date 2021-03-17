// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    at_commands as at,
    at_commands::SerDe,
    core::{
        pin::Pin,
        task::{Context, Poll},
    },
    fuchsia_bluetooth::types::Channel,
    futures::{
        ready,
        stream::{FusedStream, Stream, StreamExt},
        AsyncWriteExt,
    },
    log,
    std::collections::HashMap,
    std::io::Cursor,
};

use crate::{
    indicator_status::IndicatorStatus,
    procedure::{Procedure, ProcedureError, ProcedureMarker, ProcedureRequest},
    protocol::features::{AgFeatures, HfFeatures},
};

/// An update that can be received from either the Audio Gateway (AG) or the Hands Free (HF).
#[derive(Debug, Clone)]
pub enum Command {
    /// A command received from the AG.
    Ag(at::Response),
    /// A command received from the HF.
    Hf(at::Command),
}

impl From<at::Response> for Command {
    fn from(src: at::Response) -> Self {
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
    /// The codecs supported by the HF.
    pub hf_supported_codecs: Option<Vec<u32>>,
    /// The current indicator status of the AG.
    pub ag_indicator_status: IndicatorStatus,
    /// The format used when representing the network operator name on the AG.
    pub ag_network_operator_name_format: Option<at::NetworkOperatorNameFormat>,
    /// Use AG Extended Error Codes.
    pub extended_errors: bool,
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
}

/// A connection between two peers that shares synchronized state and acts as the control plane for
/// HFP. See HFP v1.8, 4.2 for more information.
pub struct ServiceLevelConnection {
    /// The underlying RFCOMM channel connecting the peers.
    channel: Option<Channel>,
    /// The current state associated with this connection.
    state: SlcState,
    /// The current active procedures serviced by this SLC.
    procedures: HashMap<ProcedureMarker, Box<dyn Procedure>>,
}

impl ServiceLevelConnection {
    /// Create a new, unconnected `ServiceLevelConnection`.
    pub fn new() -> Self {
        Self { channel: None, state: SlcState::default(), procedures: HashMap::new() }
    }

    /// Returns `true` if an active connection exists between the peers.
    pub fn connected(&self) -> bool {
        self.channel.as_ref().map(|ch| !ch.is_terminated()).unwrap_or(false)
    }

    /// Returns `true` if the channel has been initialized - namely the SLCI procedure has
    /// been completed for the connected channel.
    #[cfg(test)]
    fn initialized(&self) -> bool {
        self.connected() && self.state.initialized
    }

    /// Returns `true` if the provided `procedure` is currently active.
    #[cfg(test)]
    fn is_active(&self, procedure: &ProcedureMarker) -> bool {
        self.procedures.contains_key(procedure)
    }

    /// Connect using the provided `channel`.
    pub fn connect(&mut self, channel: Channel) {
        self.channel = Some(channel);
    }

    /// Sets the channel status to initialized.
    /// Note: This should only be called when the SLCI Procedure has successfully finished
    /// or in testing scenarios.
    fn set_initialized(&mut self) {
        self.state.initialized = true;
    }

    pub fn network_operator_name_format(&self) -> &Option<at::NetworkOperatorNameFormat> {
        &self.state.ag_network_operator_name_format
    }

    /// Close the service level connection and reset the state.
    fn reset(&mut self) {
        *self = Self::new();
    }

    pub async fn send_message_to_peer(
        &mut self,
        message: at::Response,
    ) -> Result<(), at::SerializeError> {
        let mut bytes = Vec::new();
        message.serialize(&mut bytes)?;

        if let Some(ch) = &mut self.channel {
            log::info!("Sent {:?}", String::from_utf8_lossy(&bytes));
            ch.write_all(&bytes).await.unwrap();
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

    /// Consume and handle a command received from the local device.
    /// On success, returns the identifier and request from handling the `command`.
    // TODO(fxbug.dev/70591): Remove this allow once it is used by procedures initiated by the local device.
    #[allow(unused)]
    pub fn receive_ag_request(
        &mut self,
        command: at::Response,
    ) -> Result<(ProcedureMarker, ProcedureRequest), ProcedureError> {
        self.handle_command(command.into())
    }

    /// Consume bytes from the peer (HF), producing a parsed at::Command from the bytes and
    /// handling it.
    /// On success, returns the identifier and request from handling the `command`.
    pub fn receive_data(
        &mut self,
        bytes: &mut Vec<u8>,
    ) -> Result<(ProcedureMarker, ProcedureRequest), ProcedureError> {
        // Parse the byte buffer into a HF message.
        let parse_result = at::Command::deserialize(&mut Cursor::new(bytes));

        if let Err(err) = parse_result {
            log::warn!("Received unparseable AT command: {:?}", err);
            return Err(ProcedureError::UnparsableHf(err));
        }

        let command = parse_result.unwrap();
        log::info!("Received {:?}", command);
        self.handle_command(command.into())
    }

    /// Handles the provided `command`:
    ///   - Matches the command to a procedure or returns an Error if not supported.
    ///   - Progresses the matched procedure with the `command`.
    ///   - Garbage collects the procedure if completed.
    ///
    /// Returns the identifier of the procedure and a subsequent request (to be handled) or
    /// Error if any steps fail.
    fn handle_command(
        &mut self,
        command: Command,
    ) -> Result<(ProcedureMarker, ProcedureRequest), ProcedureError> {
        // Attempt to match the received message to a procedure.
        let procedure_id = self.match_command_to_procedure(&command)?;
        // Progress the procedure with the message.
        let request = match command {
            Command::Hf(cmd) => self.hf_message(procedure_id, cmd),
            Command::Ag(cmd) => self.ag_message(procedure_id, cmd),
        };

        // Potentially clean up the procedure if this was the last stage. Procedures that
        // have been cleaned up cannot require additional responses, as this would violate
        // the `Procedure::is_terminated()` guarantee.
        if self.check_and_cleanup_procedure(&procedure_id) && request.requires_response() {
            return Err(ProcedureError::UnexpectedRequest);
        }

        // There is special consideration for the SLC Initialization procedure:
        //   - Errors in this procedure are considered fatal. If we encounter an error in this
        //     procedure, we close the underlying RFCOMM channel and let the peer (HF) retry.
        // TODO(fxbug.dev/70591): We should determine the appropriate response to errors in other
        // procedures. It may make sense to shut down the entire SLC for all errors because the
        // service level connection is considered synchronized with the remote peer.
        if procedure_id == ProcedureMarker::SlcInitialization {
            // Errors in this procedure are considered fatal.
            if request.is_err() {
                log::warn!("Error in the SLC Initialization procedure. Closing channel");
                self.reset();
            }
        }

        Ok((procedure_id, request))
    }

    /// Matches the incoming message to a procedure. Returns the procedure identifier
    /// for the given `command` or an error if the command couldn't be matched.
    fn match_command_to_procedure(
        &self,
        command: &Command,
    ) -> Result<ProcedureMarker, ProcedureError> {
        // If we haven't initialized the SLC yet, the only valid procedure to match is
        // the SLCI Procedure.
        if !self.state.initialized {
            return Ok(ProcedureMarker::SlcInitialization);
        }

        // Otherwise, try to match it to a procedure - it must be a non SLCI command since
        // the channel has already been initialized.
        match ProcedureMarker::match_command(command) {
            Ok(ProcedureMarker::SlcInitialization) => {
                log::warn!(
                    "Received unexpected SLCI command after SLC initialization: {:?}",
                    command
                );
                Err(command.into())
            }
            res => res,
        }
    }

    /// Updates the the procedure specified by the `marker` with the received AG `message`.
    /// Initializes the procedure if it is not already in progress.
    /// Returns the request associated with the `message`.
    pub fn ag_message(
        &mut self,
        marker: ProcedureMarker,
        message: at::Response,
    ) -> ProcedureRequest {
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
}

impl Stream for ServiceLevelConnection {
    type Item = Result<(ProcedureMarker, ProcedureRequest), ProcedureError>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        if self.is_terminated() {
            panic!("Cannot poll a terminated stream");
        }
        if let Some(channel) = &mut self.channel {
            Poll::Ready(ready!(channel.poll_next_unpin(cx)).map(|item| {
                item.map_or_else(|e| Err(e.into()), |mut data| self.receive_data(&mut data))
            }))
        } else {
            Poll::Pending
        }
    }
}

impl FusedStream for ServiceLevelConnection {
    fn is_terminated(&self) -> bool {
        !self.connected()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            indicator_status::IndicatorStatus,
            protocol::features::{AgFeatures, HfFeatures},
        },
        fuchsia_async as fasync,
        fuchsia_bluetooth::types::Channel,
        futures::io::AsyncWriteExt,
        matches::assert_matches,
    };

    fn create_and_connect_slc() -> (ServiceLevelConnection, Channel) {
        let mut slc = ServiceLevelConnection::new();
        let (local, remote) = Channel::create();
        slc.connect(local);

        (slc, remote)
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

        let expected_marker = ProcedureMarker::SlcInitialization;

        let (actual_marker, actual_request) = match slc.next().await {
            Some(Ok((m, r))) => (m, r),
            x => panic!("Unexpected stream item: {:?}", x),
        };
        // The BRSF should start the SLCI procedure.
        assert_eq!(actual_marker, expected_marker);
        assert_matches!(actual_request, ProcedureRequest::GetAgFeatures { .. });
    }

    #[fasync::run_until_stalled(test)]
    async fn slc_stream_terminated() {
        let (mut slc, remote) = create_and_connect_slc();

        drop(remote);

        assert_matches!(slc.next().await, None);
        assert!(!slc.connected());
        assert!(slc.is_terminated());
    }

    #[fasync::run_until_stalled(test)]
    async fn unexpected_command_before_initialization_closes_channel() {
        let (mut slc, remote) = create_and_connect_slc();

        // Peer sends an unexpected AT command.
        let unexpected = format!("AT+CIND=?\r").into_bytes();
        let _ = remote.as_ref().write(&unexpected);

        {
            match slc.next().await {
                Some(Ok((_, ProcedureRequest::Error(_)))) => {}
                x => panic!("Expected Error Request but got: {:?}", x),
            }
        }

        // Channel should be disconnected now.
        assert!(!slc.connected());
    }

    async fn expect_outgoing_message_to_peer(slc: &mut ServiceLevelConnection) {
        match slc.next().await {
            Some(Ok((_, ProcedureRequest::SendMessages(_)))) => {}
            x => panic!("Expected SendMessage but got: {:?}", x),
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn completing_slc_init_procedure_initializes_channel() {
        let (mut slc, remote) = create_and_connect_slc();
        let slci_marker = ProcedureMarker::SlcInitialization;
        assert!(!slc.initialized());
        assert!(!slc.is_active(&slci_marker));

        // Peer sends us HF features.
        let features = HfFeatures::THREE_WAY_CALLING;
        let command1 = format!("AT+BRSF={}\r", features.bits()).into_bytes();
        let _ = remote.as_ref().write(&command1);
        let response_fn1 = {
            match slc.next().await {
                Some(Ok((_, ProcedureRequest::GetAgFeatures { response }))) => response,
                x => panic!("Expected GetAgFeatures but got: {:?}", x),
            }
        };
        // At this point, the SLC Initialization procedure should be in progress.
        assert!(slc.is_active(&slci_marker));

        // Simulate local response with AG Features - expect these to be sent to the peer.
        let features = AgFeatures::empty();
        let next_request = slc.ag_message(slci_marker, response_fn1(features));
        assert_matches!(next_request, ProcedureRequest::SendMessages(_));

        // Peer sends us an HF supported indicators request - expect an outgoing message
        // to the peer.
        let command2 = format!("AT+CIND=?\r").into_bytes();
        let _ = remote.as_ref().write(&command2);
        expect_outgoing_message_to_peer(&mut slc).await;
        // We then expect the HF to request the indicator status which will result
        // in the procedure asking the AG for the status.
        let command3 = format!("AT+CIND?\r").into_bytes();
        let _ = remote.as_ref().write(&command3);
        let response_fn2 = {
            match slc.next().await {
                Some(Ok((_, ProcedureRequest::GetAgIndicatorStatus { response }))) => response,
                x => panic!("Expected GetAgFeatures but got: {:?}", x),
            }
        };
        // Simulate local response with AG status - expect this to go to the peer.
        let status = IndicatorStatus::default();
        let next_request = slc.ag_message(slci_marker, response_fn2(status));
        assert_matches!(next_request, ProcedureRequest::SendMessages(_));

        // We then expect HF to request enabling Ind Status update in the AG - expect an outgoing
        // message to the peer.
        let command4 = format!("AT+CMER\r").into_bytes();
        let _ = remote.as_ref().write(&command4);
        expect_outgoing_message_to_peer(&mut slc).await;

        // The SLC should be considered initialized and the SLCI Procedure garbage collected.
        assert!(slc.initialized());
        assert!(!slc.is_active(&slci_marker));
    }

    #[test]
    fn slci_command_after_initialization_returns_error() {
        let _exec = fasync::Executor::new().unwrap();
        let (mut slc, _remote) = create_and_connect_slc();
        // Bypass the SLCI procedure by setting the channel to initialized.
        slc.set_initialized();

        // Receiving an AT command associated with the SLCI procedure thereafter should
        // be an error.
        let cmd1 = at::Command::Brsf { features: HfFeatures::empty().bits() as i64 };
        assert_matches!(
            slc.match_command_to_procedure(&cmd1.into()),
            Err(ProcedureError::UnexpectedHf(_))
        );
        let cmd2 = at::Command::CindTest {};
        assert_matches!(
            slc.match_command_to_procedure(&cmd2.into()),
            Err(ProcedureError::UnexpectedHf(_))
        );
    }

    // TODO(fxbug.dev/70591): Remove this test once locally-initiated procedures are implemented.
    #[fasync::run_singlethreaded(test)]
    async fn local_request_returns_error_because_not_implemented() {
        let (mut slc, _remote) = create_and_connect_slc();
        // Bypass the SLCI procedure by setting the channel to initialized.
        slc.set_initialized();

        let random_at = at::Response::Ok;
        let res = slc.receive_ag_request(random_at);
        assert_matches!(res, Err(ProcedureError::NotImplemented));
    }
}
