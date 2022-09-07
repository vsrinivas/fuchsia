// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use at_commands as at;
use at_commands::{DeserializeBytes, SerDe};
use fidl_fuchsia_bluetooth_bredr as bredr;
use fuchsia_async as fasync;
use fuchsia_bluetooth::types::{Channel, PeerId};
use futures::StreamExt;
use parking_lot::Mutex;
use profile_client::ProfileEvent;
use std::io::Cursor;
use std::sync::Arc;
use tracing::{debug, trace, warn};

use self::procedure::ProcedureMarker;
use self::service_level_connection::{write_commands_to_channel, SlcState};

use crate::config::HandsFreeFeatureSupport;

pub mod indicators;
pub mod procedure;
pub mod service_level_connection;

/// Represents a Bluetooth peer that supports the AG role. Manages the Service Level Connection,
/// Audio Connection, and FIDL APIs
pub struct Peer {
    _id: PeerId,
    _config: HandsFreeFeatureSupport,
    _profile_svc: bredr::ProfileProxy,
    /// The processing task for data received from the remote peer over RFCOMM.
    /// This value is None if there is no RFCOMM channel present.
    /// If set, there is no guarantee that the RFCOMM channel is open.
    rfcomm_task: Option<fasync::Task<()>>,
    /// Tracks the current state associated with the service level connection
    /// to the given peer
    state: Arc<Mutex<SlcState>>,
}

impl Peer {
    pub fn new(
        id: PeerId,
        config: HandsFreeFeatureSupport,
        profile_svc: bredr::ProfileProxy,
    ) -> Self {
        Self {
            _id: id,
            _config: config,
            _profile_svc: profile_svc,
            rfcomm_task: None,
            state: Arc::new(Mutex::new(SlcState::new(config))),
        }
    }

    pub fn profile_event(&mut self, event: ProfileEvent) -> Result<(), Error> {
        match event {
            ProfileEvent::PeerConnected { id: _, protocol: _, channel } => {
                if self.rfcomm_task.take().is_some() {
                    debug!("Closing existing RFCOMM processing task.");
                }
                let processed_channel = process_function(channel, self.state.clone());
                let my_task = fasync::Task::spawn(async move {
                    if let Err(e) = processed_channel.await {
                        warn!("Error processing channel: {:?}", e);
                    }
                });

                self.rfcomm_task = Some(my_task);
            }
            ProfileEvent::SearchResult { id, protocol, attributes } => {
                trace!("Received search results for {}: {:?}, {:?}", id, protocol, attributes);
            }
        }
        Ok(())
    }
}

/// Processes and handles received AT responses from the remote peer through the RFCOMM channel
async fn process_function(mut channel: Channel, state: Arc<Mutex<SlcState>>) -> Result<(), Error> {
    // Scope is used so lock is not held beyond initial command setup.
    {
        let mut slc_state = state.lock();
        let init_proc_id = ProcedureMarker::SlcInitialization;
        let init_proc = ProcedureMarker::SlcInitialization.initialize();
        if slc_state.procedures.insert(init_proc_id, init_proc).is_some() {
            trace!("SLCI already active for peer, restarting SLCI");
        }
        write_commands_to_channel(
            &mut channel,
            &mut vec![at::Command::Brsf { features: slc_state.shared_state.hf_features.bits() }],
        );
    }

    while let Some(bytes_result) = channel.next().await {
        match bytes_result {
            Ok(bytes) => {
                let mut cursor = Cursor::new(&bytes);
                let empty_deserialized_bytes = DeserializeBytes::new();
                let parse_results =
                    at::Response::deserialize(&mut cursor, empty_deserialized_bytes);
                if let Some(error) = parse_results.error {
                    warn!("Could not deserialize correctly {:?}", error);
                }
                if parse_results.values.is_empty() {
                    continue;
                }
                handle_parsed_messages(state.clone(), &parse_results.values, &mut channel);
            }
            Err(e) => return Err(format_err!("Error in RFCOMM connection: {:?}", e)),
        }
    }
    Ok(())
}

/// Matches the AT responses to a procedure and updates procedure
/// by writing to the RFCOMM channel if necessary.
pub fn handle_parsed_messages(
    state: Arc<Mutex<SlcState>>,
    parse_results: &Vec<at::Response>,
    channel: &mut Channel,
) {
    let mut slc_state = state.lock();
    let mut shared_state_clone = slc_state.shared_state.clone();
    // TODO (fxbug.dev/106180): Handle remaining bytes.
    trace!("Received: {:?}", parse_results);
    let specific_procedure =
        slc_state.match_to_procedure(shared_state_clone.initialized, &parse_results[0]);
    match specific_procedure {
        Ok(procedure_id) => {
            let procedure = slc_state
                .procedures
                .get_mut(&procedure_id)
                .expect("Initialized in match_to_procedure.");
            match procedure.ag_update(&mut shared_state_clone, &parse_results) {
                Ok(mut commands_to_send) => {
                    trace!("Writing to channel: {:?}", commands_to_send);
                    if procedure.is_terminated() {
                        trace!(
                            "Procedure {:?} has terminated, removing from active procedures.",
                            procedure_id
                        );
                        let _ = slc_state.procedures.remove(&procedure_id);
                    }
                    write_commands_to_channel(channel, &mut commands_to_send);
                    slc_state.shared_state = shared_state_clone;
                }
                Err(e) => {
                    warn!("Error updating procedure: {:?}", e);
                    let _ = slc_state.procedures.remove(&procedure_id);
                }
            }
        }
        Err(e) => {
            warn!("Cannot process AT Response: {:?}", e);
        }
    }
}

#[cfg(test)]
mod tests {

    use super::*;

    use assert_matches::assert_matches;
    use async_utils::PollExt;
    use fidl_fuchsia_bluetooth_bredr::ProfileMarker;
    use fuchsia_async as fasync;
    use fuchsia_bluetooth::types::Channel;
    use futures::pin_mut;

    use crate::config::HandsFreeFeatureSupport;
    use crate::features::HfFeatures;

    async fn receive_and_parse(mut channel: Channel) -> Result<Vec<at::Command>, Error> {
        if let Some(bytes_result) = channel.next().await {
            match bytes_result {
                Ok(bytes) => {
                    let mut cursor = Cursor::new(&bytes);
                    let empty_deserialized_bytes = DeserializeBytes::new();
                    let parse_results =
                        at::Command::deserialize(&mut cursor, empty_deserialized_bytes);
                    if let Some(error) = parse_results.error {
                        return Err(format_err!("Could not deserialize correctly {:?}", error));
                    }
                    return Ok(parse_results.values);
                }
                Err(e) => {
                    return Err(format_err!("Remote channel not longer connected: {:?}", e));
                }
            }
        } else {
            return Err(format_err!("No longer polling RFCOMM channel"));
        }
    }

    #[fuchsia::test]
    fn peer_channel_properly_extracted() {
        let _exec = fasync::TestExecutor::new().unwrap();
        let (channel_1, _channel_2) = Channel::create();
        let (profile_proxy, _profile_server) =
            fidl::endpoints::create_proxy_and_stream::<ProfileMarker>().unwrap();
        let event = ProfileEvent::PeerConnected {
            id: PeerId::random(),
            protocol: Vec::new(),
            channel: channel_1,
        };
        let mut peer =
            Peer::new(PeerId::random(), HandsFreeFeatureSupport::default(), profile_proxy.clone());
        peer.profile_event(event).expect("Profile event terminated incorrectly.");
        assert!(peer.rfcomm_task.is_some());
    }

    #[fuchsia::test]
    fn at_responses_are_accepted() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let (local, remote) = Channel::create();
        let config = HandsFreeFeatureSupport::default();
        let state = Arc::new(Mutex::new(SlcState::new(config)));
        let processed_channel = process_function(local, state);
        pin_mut!(processed_channel);
        let () = exec.run_until_stalled(&mut processed_channel).expect_pending("still active");

        // Sending an at::Response is OK
        let _ = remote.as_ref().write(b"+BRSF:0\r").unwrap();
        // The data should be received by the process function and gracefully handled.
        let () = exec.run_until_stalled(&mut processed_channel).expect_pending("still active");

        // Remote end disconnects.
        drop(remote);
        let result = exec.run_until_stalled(&mut processed_channel).expect("terminated");
        assert_matches!(result, Ok(_));
    }

    #[fuchsia::test]
    fn at_commands_are_handled_gracefully() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let (local, remote) = Channel::create();
        let config = HandsFreeFeatureSupport::default();
        let state = Arc::new(Mutex::new(SlcState::new(config)));
        let processed_channel = process_function(local, state);
        pin_mut!(processed_channel);
        let () = exec.run_until_stalled(&mut processed_channel).expect_pending("still active");

        // While unexpected, sending an at::Command is OK
        let _ = remote.as_ref().write(b"AT+BRSF=0\r").unwrap();
        // The data should be received by the process function and gracefully handled.
        let () = exec.run_until_stalled(&mut processed_channel).expect_pending("still active");

        // Remote end disconnects.
        drop(remote);
        let result = exec.run_until_stalled(&mut processed_channel).expect("terminated");
        assert_matches!(result, Ok(_));
    }

    #[fuchsia::test]
    /// Checks that active procedure is added to map of active procedures and
    /// that the initial command is properly sent through the RFCOMM channel.
    fn peer_updates_map_of_active_procedures_and_sends_commands() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let (local, remote) = Channel::create();
        let config = HandsFreeFeatureSupport::default();
        let state = Arc::new(Mutex::new(SlcState::new(config)));
        let processed_channel = process_function(local, state.clone());
        pin_mut!(processed_channel);
        let () = exec.run_until_stalled(&mut processed_channel).expect_pending("still active");

        // Sending an at::Response is OK
        let _ = remote.as_ref().write(b"+BRSF:0\rOK\r").unwrap();
        let () = exec.run_until_stalled(&mut processed_channel).expect_pending("still active");

        let clone = state.clone();
        let state_size = clone.lock().procedures.len();
        let expected_message = vec![at::Command::Brsf {
            features: <HandsFreeFeatureSupport as Into<HfFeatures>>::into(
                HandsFreeFeatureSupport::default(),
            )
            .bits(),
        }];
        let expect_fut = receive_and_parse(remote);
        pin_mut!(expect_fut);
        let received_message = exec
            .run_until_stalled(&mut expect_fut)
            .expect("Message received.")
            .expect("Message is ok");

        assert_eq!(state_size, 1);
        assert_eq!(expected_message, received_message);
    }

    #[fuchsia::test]
    /// Checks that parsed results are correctly handled by matching parsed command to valid
    /// procedure, proper command is written based on procedure, and shared state is
    /// is properly updated.
    fn properly_matches_procedure_and_updates_state() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let (mut local, remote) = Channel::create();
        let config = HandsFreeFeatureSupport::default();
        let state = Arc::new(Mutex::new(SlcState::new(config)));
        let clone = state.clone();

        // Random non-zero feature flag set to see change in state.
        let parse_results =
            vec![at::Response::Success(at::Success::Brsf { features: 1 }), at::Response::Ok];
        handle_parsed_messages(state.clone(), &parse_results, &mut local);

        let expected_message = vec![at::Command::CindTest {}];
        let expect_fut = receive_and_parse(remote);
        pin_mut!(expect_fut);
        let received_message = exec
            .run_until_stalled(&mut expect_fut)
            .expect("Message received.")
            .expect("Message is ok");

        assert_eq!(expected_message, received_message);
        assert_eq!(clone.lock().shared_state.ag_features.bits(), 1);
    }
}
