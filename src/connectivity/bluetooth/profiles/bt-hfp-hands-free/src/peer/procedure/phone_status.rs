// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use at_commands as at;
use tracing::warn;

use super::{Procedure, ProcedureMarker};

use crate::peer::service_level_connection::SharedState;

/// This implementation supports the 7 indicators defined in HFP v1.8 Section 4.35.
/// The indices of these indicators are fixed.
const SERVICE_INDICATOR_INDEX: i64 = 1;
const CALL_INDICATOR_INDEX: i64 = 2;
const CALL_SETUP_INDICATOR_INDEX: i64 = 3;
const CALL_HELD_INDICATOR_INDEX: i64 = 4;
const SIGNAL_INDICATOR_INDEX: i64 = 5;
const ROAM_INDICATOR_INDEX: i64 = 6;
const BATT_CHG_INDICATOR_INDEX: i64 = 7;

pub struct PhoneStatusProcedure {
    // Whether the procedure has sent the phone status to the HF.
    terminated: bool,
}

impl PhoneStatusProcedure {
    pub fn new() -> Self {
        Self { terminated: false }
    }
}

impl Procedure for PhoneStatusProcedure {
    fn marker(&self) -> ProcedureMarker {
        ProcedureMarker::PhoneStatus
    }

    fn ag_update(
        &mut self,
        state: &mut SharedState,
        update: &Vec<at::Response>,
    ) -> Result<Vec<at::Command>, Error> {
        for respones in update {
            match respones {
                at::Response::Success(at::Success::Ciev { ind, value }) => {
                    if state.indicators_update_enabled {
                        match *ind {
                            SERVICE_INDICATOR_INDEX => {
                                state.ag_indicators.service.set_if_enabled(*value != 0);
                            }
                            CALL_INDICATOR_INDEX => {
                                state.ag_indicators.call.set_if_enabled(*value != 0);
                            }
                            CALL_SETUP_INDICATOR_INDEX => {
                                state.ag_indicators.callsetup.set_if_enabled(*value as u8);
                            }
                            CALL_HELD_INDICATOR_INDEX => {
                                state.ag_indicators.callheld.set_if_enabled(*value as u8);
                            }
                            SIGNAL_INDICATOR_INDEX => {
                                state.ag_indicators.signal.set_if_enabled(*value as u8);
                            }
                            ROAM_INDICATOR_INDEX => {
                                state.ag_indicators.roam.set_if_enabled(*value != 0);
                            }
                            BATT_CHG_INDICATOR_INDEX => {
                                state.ag_indicators.battchg.set_if_enabled(*value as u8);
                            }
                            _ => {
                                warn!("Indicator index {:?} is out of bounds from accepted indicators.", ind);
                            }
                        }
                    }
                }
                _ => {
                    return Err(format_err!(
                        "Received invalid response during a phone status update procedure: {:?}",
                        respones
                    ));
                }
            }
        }
        self.terminated = true;
        Ok(vec![])
    }

    fn is_terminated(&self) -> bool {
        self.terminated
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use assert_matches::assert_matches;

    use crate::config::HandsFreeFeatureSupport;

    #[fuchsia::test]
    fn update_with_invalid_response_returns_error() {
        let mut procedure = PhoneStatusProcedure::new();
        let config = HandsFreeFeatureSupport::default();
        let mut state = SharedState::new(config);
        let response = vec![at::Response::Ok];

        assert!(!procedure.is_terminated());

        assert_matches!(procedure.ag_update(&mut state, &response), Err(_));

        assert!(!procedure.is_terminated());
    }

    #[fuchsia::test]
    fn update_with_invalid_index_keeps_values() {
        let mut procedure = PhoneStatusProcedure::new();
        let config = HandsFreeFeatureSupport::default();
        let mut state = SharedState::new(config);
        let response = vec![at::Response::Success(at::Success::Ciev { ind: 0, value: 1 })];

        state.ag_indicators.set_default_values();

        assert!(!procedure.is_terminated());
        assert_eq!(state.ag_indicators.service.value.unwrap(), false);
        assert_eq!(state.ag_indicators.call.value.unwrap(), false);
        assert_eq!(state.ag_indicators.callsetup.value.unwrap(), 0);
        assert_eq!(state.ag_indicators.callheld.value.unwrap(), 0);
        assert_eq!(state.ag_indicators.signal.value.unwrap(), 0);
        assert_eq!(state.ag_indicators.roam.value.unwrap(), false);
        assert_eq!(state.ag_indicators.battchg.value.unwrap(), 0);

        let _ = procedure.ag_update(&mut state, &response);

        assert!(procedure.is_terminated());
        assert_eq!(state.ag_indicators.service.value.unwrap(), false);
        assert_eq!(state.ag_indicators.call.value.unwrap(), false);
        assert_eq!(state.ag_indicators.callsetup.value.unwrap(), 0);
        assert_eq!(state.ag_indicators.callheld.value.unwrap(), 0);
        assert_eq!(state.ag_indicators.signal.value.unwrap(), 0);
        assert_eq!(state.ag_indicators.roam.value.unwrap(), false);
        assert_eq!(state.ag_indicators.battchg.value.unwrap(), 0);
    }

    #[fuchsia::test]
    fn update_properly_changes_value() {
        let mut procedure = PhoneStatusProcedure::new();
        let config = HandsFreeFeatureSupport::default();
        let mut state = SharedState::new(config);

        state.ag_indicators.set_default_values();

        assert!(!procedure.is_terminated());

        assert_eq!(state.ag_indicators.service.value.unwrap(), false);
        assert_eq!(state.ag_indicators.call.value.unwrap(), false);
        assert_eq!(state.ag_indicators.callsetup.value.unwrap(), 0);
        assert_eq!(state.ag_indicators.callheld.value.unwrap(), 0);
        assert_eq!(state.ag_indicators.signal.value.unwrap(), 0);
        assert_eq!(state.ag_indicators.roam.value.unwrap(), false);
        assert_eq!(state.ag_indicators.battchg.value.unwrap(), 0);

        let response = vec![
            at::Response::Success(at::Success::Ciev { ind: SERVICE_INDICATOR_INDEX, value: 1 }),
            at::Response::Success(at::Success::Ciev { ind: CALL_INDICATOR_INDEX, value: 1 }),
            at::Response::Success(at::Success::Ciev { ind: CALL_SETUP_INDICATOR_INDEX, value: 1 }),
            at::Response::Success(at::Success::Ciev { ind: CALL_HELD_INDICATOR_INDEX, value: 1 }),
            at::Response::Success(at::Success::Ciev { ind: SIGNAL_INDICATOR_INDEX, value: 1 }),
            at::Response::Success(at::Success::Ciev { ind: ROAM_INDICATOR_INDEX, value: 1 }),
            at::Response::Success(at::Success::Ciev { ind: BATT_CHG_INDICATOR_INDEX, value: 1 }),
        ];

        assert_matches!(procedure.ag_update(&mut state, &response), Ok(_));

        assert_eq!(state.ag_indicators.service.value.unwrap(), true);
        assert_eq!(state.ag_indicators.call.value.unwrap(), true);
        assert_eq!(state.ag_indicators.callsetup.value.unwrap(), 1);
        assert_eq!(state.ag_indicators.callheld.value.unwrap(), 1);
        assert_eq!(state.ag_indicators.signal.value.unwrap(), 1);
        assert_eq!(state.ag_indicators.roam.value.unwrap(), true);
        assert_eq!(state.ag_indicators.battchg.value.unwrap(), 1);

        assert!(procedure.is_terminated());
    }

    #[fuchsia::test]
    fn update_maintains_value_when_updates_disabled() {
        let mut procedure = PhoneStatusProcedure::new();
        let config = HandsFreeFeatureSupport::default();
        let mut state = SharedState::new(config);
        state.indicators_update_enabled = false;

        state.ag_indicators.set_default_values();

        assert!(!procedure.is_terminated());

        assert_eq!(state.ag_indicators.service.value.unwrap(), false);
        assert_eq!(state.ag_indicators.call.value.unwrap(), false);
        assert_eq!(state.ag_indicators.callsetup.value.unwrap(), 0);
        assert_eq!(state.ag_indicators.callheld.value.unwrap(), 0);
        assert_eq!(state.ag_indicators.signal.value.unwrap(), 0);
        assert_eq!(state.ag_indicators.roam.value.unwrap(), false);
        assert_eq!(state.ag_indicators.battchg.value.unwrap(), 0);

        let response = vec![
            at::Response::Success(at::Success::Ciev { ind: SERVICE_INDICATOR_INDEX, value: 1 }),
            at::Response::Success(at::Success::Ciev { ind: CALL_INDICATOR_INDEX, value: 1 }),
            at::Response::Success(at::Success::Ciev { ind: CALL_SETUP_INDICATOR_INDEX, value: 1 }),
            at::Response::Success(at::Success::Ciev { ind: CALL_HELD_INDICATOR_INDEX, value: 1 }),
            at::Response::Success(at::Success::Ciev { ind: SIGNAL_INDICATOR_INDEX, value: 1 }),
            at::Response::Success(at::Success::Ciev { ind: ROAM_INDICATOR_INDEX, value: 1 }),
            at::Response::Success(at::Success::Ciev { ind: BATT_CHG_INDICATOR_INDEX, value: 1 }),
        ];

        assert_matches!(procedure.ag_update(&mut state, &response), Ok(_));

        assert_eq!(state.ag_indicators.service.value.unwrap(), false);
        assert_eq!(state.ag_indicators.call.value.unwrap(), false);
        assert_eq!(state.ag_indicators.callsetup.value.unwrap(), 0);
        assert_eq!(state.ag_indicators.callheld.value.unwrap(), 0);
        assert_eq!(state.ag_indicators.signal.value.unwrap(), 0);
        assert_eq!(state.ag_indicators.roam.value.unwrap(), false);
        assert_eq!(state.ag_indicators.battchg.value.unwrap(), 0);

        assert!(procedure.is_terminated());
    }
}
