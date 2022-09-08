// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use at::SerDe;
use at_commands as at;
use fuchsia_bluetooth::types::Channel;
use std::collections::hash_map::HashMap;
use tracing::warn;

use super::indicators::AgIndicators;
use super::procedure::{Procedure, ProcedureMarker};

use crate::config::HandsFreeFeatureSupport;
use crate::features::{AgFeatures, HfFeatures};

pub struct SlcState {
    /// Collection of active procedures.
    pub procedures: HashMap<ProcedureMarker, Box<dyn Procedure>>,
    /// Collection of shared features and indicators between two
    /// devices.
    pub shared_state: SharedState,
}

#[derive(Clone)]
pub struct SharedState {
    /// Featuers that the HF supports.
    pub hf_features: HfFeatures,
    /// Features that the AG supports.
    pub ag_features: AgFeatures,
    /// The current indicator status of the AG.
    pub ag_indicators: AgIndicators,
    /// Determines whether the SLCI procedure has completed and
    /// can proceed to do other procedures.
    pub initialized: bool,
    /// Determines whether the indicator status update function is enabled.
    pub indicators_update_enabled: bool,
}

// TODO(fxbug.dev/104703): More fields for SLCI
impl SlcState {
    pub fn new(config: HandsFreeFeatureSupport) -> Self {
        let features = SharedState::new(config);
        Self { procedures: HashMap::new(), shared_state: features }
    }

    /// Identifies procedure from the response and inserts the identifying procedure marker into map.
    pub fn match_to_procedure(
        &mut self,
        initialized: bool,
        response: &at::Response,
    ) -> Result<ProcedureMarker, Error> {
        let procedure_id =
            ProcedureMarker::identify_procedure_from_response(initialized, &response);
        match procedure_id {
            Ok(id) => {
                let _ = self.procedures.entry(id).or_insert(id.initialize());
                Ok(id)
            }
            Err(e) => {
                warn!("Could not match parsed value to specific procedure: {:?}", response);
                return Err(e);
            }
        }
    }
}

impl SharedState {
    pub fn new(config: HandsFreeFeatureSupport) -> Self {
        Self {
            hf_features: config.into(),
            ag_features: AgFeatures::default(),
            ag_indicators: AgIndicators::default(),
            initialized: false,
            indicators_update_enabled: true,
        }
    }

    pub fn supports_codec_negotiation(&self) -> bool {
        self.ag_features.contains(AgFeatures::CODEC_NEGOTIATION)
            && self.hf_features.contains(HfFeatures::CODEC_NEGOTIATION)
    }

    pub fn supports_three_way_calling(&self) -> bool {
        self.ag_features.contains(AgFeatures::THREE_WAY_CALLING)
            && self.hf_features.contains(HfFeatures::THREE_WAY_CALLING)
    }
}

/// Serializes the AT commands and sends them through the provided RFCOMM channel
pub fn write_commands_to_channel(channel: &mut Channel, commands: &mut [at::Command]) {
    if commands.len() > 0 {
        let mut bytes = Vec::new();
        let _ = at::Command::serialize(&mut bytes, commands);
        if let Err(e) = channel.as_ref().write(&bytes) {
            warn!("Could not fully write serialized commands to channel: {:?}", e);
        };
    }
}
