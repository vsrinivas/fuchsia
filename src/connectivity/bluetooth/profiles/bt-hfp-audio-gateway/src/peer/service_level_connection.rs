// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
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
    std::collections::HashMap,
};

use crate::{
    at::{AtAgMessage, AtHfMessage, Parser},
    procedure::{
        nrec::NrecProcedure, slc_initialization::SlcInitProcedure, Procedure, ProcedureMarker,
        ProcedureRequest,
    },
};

/// A connection between two peers that shares synchronized state and acts as the control plane for
/// HFP. See HFP v1.8, 4.2 for more information.
pub struct ServiceLevelConnection {
    /// The underlying RFCOMM channel connecting the peers.
    channel: Option<Channel>,
    /// Whether the channel has been initialized with the SLCI Procedure.
    initialized: bool,
    /// An AT Command parser instance.
    parser: Parser,
    procedures: HashMap<ProcedureMarker, Box<dyn Procedure>>,
}

impl ServiceLevelConnection {
    /// Create a new, unconnected `ServiceLevelConnection`.
    pub fn new() -> Self {
        Self {
            initialized: false,
            channel: None,
            parser: Parser::default(),
            procedures: HashMap::new(),
        }
    }

    /// Returns `true` if an active connection exists between the peers.
    pub fn connected(&self) -> bool {
        self.channel.as_ref().map(|ch| !ch.is_terminated()).unwrap_or(false)
    }

    /// Connect using the provided `channel`.
    pub fn connect(&mut self, channel: Channel) {
        self.channel = Some(channel);
    }

    /// Close the service level connection and reset the state.
    fn reset(&mut self) {
        *self = Self::new();
    }

    /// Checks if the channel has been initialized, namely the SLCI Procedure is complete.
    fn check_initialized(&mut self) {
        if let Some(is_terminated) =
            self.procedures.get(&ProcedureMarker::SlcInitialization).map(|p| p.is_terminated())
        {
            self.initialized = is_terminated;
        }
    }

    pub async fn send_message_to_peer(&mut self, message: AtAgMessage) {
        let bytes = message.into_bytes();
        if let Some(ch) = &mut self.channel {
            log::info!("Sent {:?}", String::from_utf8_lossy(&bytes));
            ch.write_all(&bytes).await.unwrap();
        }
    }

    /// Consume bytes from the peer, producing a parsed AtHfMessage from the bytes and
    /// handling it.
    pub fn receive_data(&mut self, bytes: Vec<u8>) -> (ProcedureMarker, ProcedureRequest) {
        // Parse the byte buffer into a HF message.
        let command = self.parser.parse(&bytes);
        log::info!("Received {:?}", command);

        // Attempt to match the received message to a procedure.
        let procedure_id = self.match_command_to_procedure(&command);
        // Progress the procedure with the message.
        let request = self.hf_message(procedure_id, command);

        // There is special consideration for the SLC Initialization procedure.
        //   - Other procedures can only be started after this procedure has finished. Therefore,
        //     we check procedure termination before continuing.
        //   - Errors in this procedure are considered fatal. If we encounter an error in this
        //     procedure, we close the underlying RFCOMM channel and let the peer (HF) retry.
        if procedure_id == ProcedureMarker::SlcInitialization {
            // Check if it is finished.
            self.check_initialized();
            // Errors in this procedure are considered fatal.
            if request.is_err() {
                log::warn!("Error in the SLC Initialization procedure. Closing channel");
                self.reset();
            }
        }

        (procedure_id, request)
    }

    /// Matches the incoming HF message to a procedure. Returns the procedure identifier
    /// for the given `command`.
    // TODO(fxbug.dev/70591): This should be more sophisticated. For now since we only support the
    // SLC Init procedure, we match to that every time.
    pub fn match_command_to_procedure(&mut self, _command: &AtHfMessage) -> ProcedureMarker {
        // If we haven't initialized the SLC yet, the only valid procedure to match is
        // the SlcInitProcedure.
        if !self.initialized {
            // Potentially create a new SLCI.
            self.procedures
                .entry(ProcedureMarker::SlcInitialization)
                .or_insert(Box::new(SlcInitProcedure::new()));
            return ProcedureMarker::SlcInitialization;
        } else {
            if let AtHfMessage::Nrec(_) = _command {
                self.procedures
                    .entry(ProcedureMarker::Nrec)
                    .or_insert(Box::new(NrecProcedure::new()));
                return ProcedureMarker::Nrec;
            }
        }
        // TODO(fxbug.dev/70591): Try to match it to a different procedure.
        ProcedureMarker::Unknown
    }

    /// Updates the the procedure specified by the `marker` with the received AG `message`.
    /// Returns the request associated with the `message`.
    pub fn ag_message(
        &mut self,
        marker: ProcedureMarker,
        message: AtAgMessage,
    ) -> ProcedureRequest {
        match self.procedures.get_mut(&marker) {
            Some(p) => p.ag_update(message),
            None => {
                log::warn!("Procedure: {:?} doesn't exist", marker);
                ProcedureRequest::None
            }
        }
    }

    /// Updates the the procedure specified by the `marker` with the received HF `message`.
    /// Returns the request associated with the `message`.
    pub fn hf_message(
        &mut self,
        marker: ProcedureMarker,
        message: AtHfMessage,
    ) -> ProcedureRequest {
        match self.procedures.get_mut(&marker) {
            Some(p) => p.hf_update(message),
            None => {
                log::warn!("Procedure: {:?} doesn't exist", marker);
                ProcedureRequest::None
            }
        }
    }
}

impl Stream for ServiceLevelConnection {
    type Item = Result<(ProcedureMarker, ProcedureRequest), fuchsia_zircon::Status>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        if self.is_terminated() {
            panic!("Cannot poll a terminated stream");
        }
        if let Some(channel) = &mut self.channel {
            Poll::Ready(
                ready!(channel.poll_next_unpin(cx))
                    .map(|item| item.map(|data| self.receive_data(data))),
            )
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
        crate::protocol::features::{AgFeatures, HfFeatures},
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
    async fn scl_stream_produces_items() {
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
    async fn scl_stream_terminated() {
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
        let unexpected = format!("AT+CIND=\r").into_bytes();
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

    #[fasync::run_until_stalled(test)]
    async fn start_slc_init_procedure() {
        let (mut slc, remote) = create_and_connect_slc();

        // Peer sends us HF features.
        let features = HfFeatures::empty();
        let command = format!("AT+BRSF={}\r", features.bits()).into_bytes();
        let _ = remote.as_ref().write(&command);

        let response_fn = {
            match slc.next().await {
                Some(Ok((_, ProcedureRequest::GetAgFeatures { response }))) => response,
                x => panic!("Expected Request but got: {:?}", x),
            }
        };

        // Simulate local response with AG Features.
        let features = AgFeatures::empty();
        let next_request =
            slc.ag_message(ProcedureMarker::SlcInitialization, response_fn(features));
        assert_matches!(next_request, ProcedureRequest::SendMessage(_));
    }
}
