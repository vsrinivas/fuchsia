// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_bluetooth_hfp::{CallState, NetworkInformation};
use fuchsia_async as fasync;
use fuchsia_bluetooth::types::PeerId;
use fuchsia_inspect::{self as inspect, Property};
use fuchsia_inspect_contrib::nodes::{NodeExt, TimeProperty};
use fuchsia_inspect_derive::{AttachError, Inspect};
use lazy_static::lazy_static;
use std::collections::VecDeque;

use crate::features::{codecs_to_string, CodecId, HfFeatures};
use crate::peer::calls::number::Number;
use crate::peer::calls::types::Direction;
use crate::peer::service_level_connection::SlcState;

lazy_static! {
    static ref PEER_ID: inspect::StringReference<'static> = "peer_id".into();
}

#[derive(Default, Debug)]
pub struct HfpInspect {
    pub autoconnect: inspect::BoolProperty,
    // The inspect node for connected peers.
    pub peers: inspect::Node,
    inspect_node: inspect::Node,
}

impl Inspect for &mut HfpInspect {
    fn iattach(self, parent: &inspect::Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        self.inspect_node = parent.create_child(name.as_ref());
        self.autoconnect = self.inspect_node.create_bool("autoconnect", false);
        self.peers = self.inspect_node.create_child("peers");
        Ok(())
    }
}

impl HfpInspect {
    pub fn node(&self) -> &inspect::Node {
        &self.inspect_node
    }
}

#[derive(Default, Debug, Inspect)]
pub struct CallManagerInspect {
    manager_connection_id: inspect::UintProperty,
    connected: inspect::BoolProperty,
    inspect_node: inspect::Node,
}

impl CallManagerInspect {
    pub fn new_connection(&mut self, id: usize) {
        self.connected.set(true);
        self.manager_connection_id.set(id as u64);
    }

    pub fn set_disconnected(&mut self) {
        self.connected.set(false);
    }
}

#[derive(Default, Debug, Inspect)]
pub struct NetworkInformationInspect {
    service_available: inspect::BoolProperty,
    signal_strength: inspect::StringProperty,
    roaming: inspect::BoolProperty,
    inspect_node: inspect::Node,
}

impl NetworkInformationInspect {
    pub fn update(&mut self, info: &NetworkInformation) {
        self.service_available.set(info.service_available.unwrap_or(false));
        let signal = info.signal_strength.map_or("".to_string(), |s| format!("{:?}", s));
        self.signal_strength.set(&signal);
        self.roaming.set(info.roaming.unwrap_or(false));
    }
}

#[derive(Default, Debug)]
pub struct CallEntryInspect {
    number: inspect::StringProperty,
    is_incoming: inspect::BoolProperty,
    call_state: inspect::StringProperty,
    inspect_node: inspect::Node,
    //TODO(fxbug.dev/91250): Persist previous states, record their associated change times
}

impl Inspect for &mut CallEntryInspect {
    fn iattach(self, parent: &inspect::Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        self.inspect_node = parent.create_child(name.as_ref());
        self.number = self.inspect_node.create_string("number", "");
        self.is_incoming = self.inspect_node.create_bool("is_incoming", false);
        self.call_state = self.inspect_node.create_string("call_state", "");
        Ok(())
    }
}

impl CallEntryInspect {
    pub fn set_number(&mut self, number: Number) {
        self.number.set(String::from(number).as_str());
    }

    pub fn set_call_direction(&mut self, direction: Direction) {
        let is_incoming = match direction {
            Direction::MobileTerminated => true,
            Direction::MobileOriginated => false,
        };
        self.is_incoming.set(is_incoming);
    }

    pub fn set_call_state(&mut self, call_state: CallState) {
        self.call_state.set(format!("{:?}", call_state).as_str());
    }
}

#[derive(Debug)]
pub struct PeerTaskInspect {
    /// The Bluetooth identifier assigned to the peer.
    peer_id: PeerId,
    pub connected_peer_handler: inspect::BoolProperty,
    pub network: NetworkInformationInspect,
    hf_battery_level: Option<inspect::UintProperty>,
    inspect_node: inspect::Node,
}

impl Inspect for &mut PeerTaskInspect {
    fn iattach(self, parent: &inspect::Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        self.inspect_node = parent.create_child(name.as_ref());
        self.inspect_node.record_string(&*PEER_ID, self.peer_id.to_string());
        self.connected_peer_handler =
            self.inspect_node.create_bool("connected_peer_handler", false);
        self.network.iattach(&self.inspect_node, "network")?;
        Ok(())
    }
}

impl PeerTaskInspect {
    pub fn new(peer_id: PeerId) -> Self {
        Self {
            peer_id,
            connected_peer_handler: Default::default(),
            network: Default::default(),
            hf_battery_level: Default::default(),
            inspect_node: Default::default(),
        }
    }

    pub fn node(&self) -> &inspect::Node {
        &self.inspect_node
    }

    pub fn set_hf_battery_level(&mut self, batt: u8) {
        // Weak clone to avoid borrow issues when moving `self` into closure.
        let node = self.inspect_node.clone_weak();
        let batt_node =
            self.hf_battery_level.get_or_insert_with(|| node.create_uint("hf_battery_level", 0));
        batt_node.set(batt.into());
    }
}

/// The maximum number of recent procedures that will be stored in the inspect tree.
/// This is chosen as a reasonable window to provide information during debugging.
const MAX_RECENT_PROCEDURES: usize = 10;

#[derive(Default, Inspect)]
pub struct ServiceLevelConnectionInspect {
    /// When the SLC was first connected.
    #[inspect(skip)]
    connected_at: Option<TimeProperty>,
    /// When the SLC was initialized - e.g the initialization procedures complete.
    #[inspect(skip)]
    initialized_at: Option<TimeProperty>,
    hf_supported_codecs: inspect::StringProperty,
    #[inspect(skip)]
    selected_codec: Option<inspect::StringProperty>,
    handsfree_feature_support: HfFeaturesInspect,
    extended_errors: inspect::BoolProperty,
    call_waiting_notifications: inspect::BoolProperty,
    call_line_ident_notifications: inspect::BoolProperty,
    procedures: RecentProceduresInspect,
    inspect_node: inspect::Node,
}

impl ServiceLevelConnectionInspect {
    pub fn procedures_node(&self) -> &inspect::Node {
        &self.procedures.inspect_node
    }

    pub fn connected(&mut self, at: fasync::Time) {
        self.connected_at = Some(self.inspect_node.create_time_at("connected_at", at.into()));
    }

    pub fn initialized(&mut self, at: fasync::Time) {
        self.initialized_at = Some(self.inspect_node.create_time_at("initialized_at", at.into()));
    }

    pub fn update_slc_state(&mut self, state: &SlcState) {
        if let Some(codecs) = &state.hf_supported_codecs {
            self.hf_supported_codecs.set(&codecs_to_string(codecs));
        }

        self.set_selected_codec(&state.selected_codec);

        self.handsfree_feature_support.update(&state.hf_features);
        self.extended_errors.set(state.extended_errors);
        self.call_waiting_notifications.set(state.call_waiting_notifications);
        self.call_line_ident_notifications.set(state.call_line_ident_notifications);
    }

    pub fn set_selected_codec(&mut self, codec: &Option<CodecId>) {
        self.selected_codec =
            codec.map(|c| self.inspect_node.create_string("selected_codec", &c.to_string()));
    }

    /// Record the inspect data for a finished procedure.
    pub fn record_procedure(&mut self, node: inspect::Node) {
        if self.procedures.recent_procedures.len() >= MAX_RECENT_PROCEDURES {
            drop(self.procedures.recent_procedures.pop_front());
        }

        self.procedures.recent_procedures.push_back(node);
    }
}

#[derive(Inspect)]
struct RecentProceduresInspect {
    /// Internal bookkeeping to garbage collect recently finished procedures.
    #[inspect(skip)]
    recent_procedures: VecDeque<inspect::Node>,
    inspect_node: inspect::Node,
}

impl Default for RecentProceduresInspect {
    fn default() -> Self {
        Self {
            recent_procedures: VecDeque::with_capacity(MAX_RECENT_PROCEDURES),
            inspect_node: Default::default(),
        }
    }
}

#[derive(Default, Debug, Inspect)]
pub struct HfFeaturesInspect {
    echo_canceling_and_noise_reduction: inspect::BoolProperty,
    three_way_calling: inspect::BoolProperty,
    cli_presentation: inspect::BoolProperty,
    voice_recognition_activation: inspect::BoolProperty,
    remote_volume_control: inspect::BoolProperty,
    enhanced_call_status: inspect::BoolProperty,
    enhanced_call_control: inspect::BoolProperty,
    codec_negotiation: inspect::BoolProperty,
    handsfree_indicators: inspect::BoolProperty,
    esco_s4: inspect::BoolProperty,
    enhanced_voice_recognition: inspect::BoolProperty,
    enhanced_voice_recognition_with_text: inspect::BoolProperty,
    inspect_node: inspect::Node,
}

impl HfFeaturesInspect {
    fn update(&mut self, features: &HfFeatures) {
        self.echo_canceling_and_noise_reduction.set(features.contains(HfFeatures::NR_EC));
        self.three_way_calling.set(features.contains(HfFeatures::THREE_WAY_CALLING));
        self.cli_presentation.set(features.contains(HfFeatures::CLI_PRESENTATION));
        self.voice_recognition_activation.set(features.contains(HfFeatures::VR_ACTIVATION));
        self.remote_volume_control.set(features.contains(HfFeatures::REMOTE_VOLUME_CONTROL));
        self.enhanced_call_status.set(features.contains(HfFeatures::ENHANCED_CALL_STATUS));
        self.enhanced_call_control.set(features.contains(HfFeatures::ENHANCED_CALL_CONTROL));
        self.codec_negotiation.set(features.contains(HfFeatures::CODEC_NEGOTIATION));
        self.handsfree_indicators.set(features.contains(HfFeatures::HF_INDICATORS));
        self.esco_s4.set(features.contains(HfFeatures::ESCO_S4));
        self.enhanced_voice_recognition.set(features.contains(HfFeatures::EVR_STATUS));
        self.enhanced_voice_recognition_with_text.set(features.contains(HfFeatures::VR_TEXT));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl_fuchsia_bluetooth_hfp::SignalStrength;
    use fuchsia_inspect::{assert_data_tree, testing::AnyProperty};
    use fuchsia_inspect_derive::WithInspect;

    #[test]
    fn call_entry_inspect_tree() {
        let inspect = inspect::Inspector::new();
        let mut call_entry =
            CallEntryInspect::default().with_inspect(inspect.root(), "call").unwrap();
        call_entry.set_number(Number::from("1234567"));
        call_entry.set_call_direction(Direction::MobileTerminated);
        call_entry.set_call_state(CallState::IncomingRinging);
        assert_data_tree!(inspect, root: {
            call: {
                number: "\"1234567\"",
                is_incoming: true,
                call_state: "IncomingRinging",
            }
        });

        call_entry.set_call_state(CallState::Terminated);
        assert_data_tree!(inspect, root: {
            call: {
                number: "\"1234567\"",
                is_incoming: true,
                call_state: "Terminated",
            }
        });
    }

    #[test]
    fn peer_task_inspect_tree() {
        let inspect = inspect::Inspector::new();

        let id = PeerId(2);
        let mut peer_task = PeerTaskInspect::new(id).with_inspect(inspect.root(), "peer").unwrap();

        // Default inspect tree.
        assert_data_tree!(inspect, root: {
            peer: {
                peer_id: AnyProperty,
                connected_peer_handler: false,
                network: {
                    service_available: false,
                    signal_strength: "",
                    roaming: false,
                },
            }
        });

        let network = NetworkInformation {
            service_available: Some(true),
            signal_strength: Some(SignalStrength::Low),
            ..NetworkInformation::EMPTY
        };
        peer_task.connected_peer_handler.set(true);
        peer_task.network.update(&network);
        peer_task.set_hf_battery_level(10);
        assert_data_tree!(inspect, root: {
            peer: {
                peer_id: AnyProperty,
                connected_peer_handler: true,
                network: {
                    service_available: true,
                    signal_strength: "Low",
                    roaming: false,
                },
                hf_battery_level: 10u64,
            }
        });
    }

    #[test]
    fn service_level_connection_inspect_tree() {
        let exec = fasync::TestExecutor::new_with_fake_time().unwrap();
        exec.set_fake_time(fasync::Time::from_nanos(1230000));
        let inspect = inspect::Inspector::new();

        let mut slc =
            ServiceLevelConnectionInspect::default().with_inspect(inspect.root(), "slc").unwrap();
        // Default inspect tree.
        assert_data_tree!(inspect, root: {
            slc: {
                hf_supported_codecs: "",
                handsfree_feature_support: {
                    echo_canceling_and_noise_reduction: false,
                    three_way_calling: false,
                    cli_presentation: false,
                    voice_recognition_activation: false,
                    remote_volume_control: false,
                    enhanced_call_status: false,
                    enhanced_call_control: false,
                    codec_negotiation: false,
                    handsfree_indicators: false,
                    esco_s4 : false,
                    enhanced_voice_recognition: false,
                    enhanced_voice_recognition_with_text: false,
                },
                extended_errors: false,
                call_waiting_notifications: false,
                call_line_ident_notifications: false,
                procedures: {}
            }
        });

        let state = SlcState {
            initialized: true,
            hf_supported_codecs: Some(vec![CodecId::CVSD, CodecId::MSBC]),
            selected_codec: Some(CodecId::MSBC),
            extended_errors: true,
            ..SlcState::default()
        };
        slc.initialized(fasync::Time::now());
        slc.update_slc_state(&state);
        slc.connected(fasync::Time::now());
        assert_data_tree!(inspect, root: {
            slc: {
                connected_at: 1230000i64,
                initialized_at: 1230000i64,
                hf_supported_codecs: "CVSD, MSBC",
                selected_codec: "MSBC",
                handsfree_feature_support: {
                    echo_canceling_and_noise_reduction: false,
                    three_way_calling: false,
                    cli_presentation: false,
                    voice_recognition_activation: false,
                    remote_volume_control: false,
                    enhanced_call_status: false,
                    enhanced_call_control: false,
                    codec_negotiation: false,
                    handsfree_indicators: false,
                    esco_s4 : false,
                    enhanced_voice_recognition: false,
                    enhanced_voice_recognition_with_text: false,
                },
                extended_errors: true,
                call_waiting_notifications: false,
                call_line_ident_notifications: false,
                procedures: {}
            }
        });

        slc.set_selected_codec(&None);
        assert_data_tree!(inspect, root: {
            slc: {
                connected_at: 1230000i64,
                initialized_at: 1230000i64,
                hf_supported_codecs: "CVSD, MSBC",
                handsfree_feature_support: contains {},
                extended_errors: true,
                call_waiting_notifications: false,
                call_line_ident_notifications: false,
                procedures: {}
            }
        });

        {
            // An inspect node is created for a given procedure. When it finishes, it will be
            // recorded with the SLC Inspect data.
            let procedure_node = slc.procedures_node().create_child("procedure_0");
            procedure_node.record_string("name", "FooBar");
            slc.record_procedure(procedure_node);
        }
        assert_data_tree!(inspect, root: {
            slc: {
                connected_at: 1230000i64,
                initialized_at: 1230000i64,
                hf_supported_codecs: "CVSD, MSBC",
                handsfree_feature_support: contains {},
                extended_errors: true,
                call_waiting_notifications: false,
                call_line_ident_notifications: false,
                procedures: {
                    procedure_0: { "name": "FooBar" },
                },
            }
        });
    }
}
