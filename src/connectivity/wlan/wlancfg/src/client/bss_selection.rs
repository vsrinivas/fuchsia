// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        client::types as client_types, config_management::network_config::PastConnectionList,
        util::pseudo_energy::*,
    },
    log::error,
};

// Number of previous RSSI measurements to exponentially weigh into average.
// TODO(fxbug.dev/84870): Tune smoothing factor.
pub const EWMA_SMOOTHING_FACTOR: usize = 10;

pub const RSSI_AND_VELOCITY_SCORE_WEIGHT: f32 = 0.6;
pub const SNR_SCORE_WEIGHT: f32 = 0.4;

// Threshold for BSS signal scores (bound from 0-100), under which a BSS's signal is considered
// suboptimal. Used to determine if roaming should be considered.
pub const SUBOPTIMAL_SIGNAL_THRESHOLD: u8 = 45;

/// Connection quality data related to signal
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct SignalData {
    pub ewma_rssi: EwmaPseudoDecibel,
    pub ewma_snr: EwmaPseudoDecibel,
    pub rssi_velocity: PseudoDecibel,
}

impl SignalData {
    pub fn new(
        initial_rssi: PseudoDecibel,
        initial_snr: PseudoDecibel,
        ewma_weight: usize,
    ) -> Self {
        Self {
            ewma_rssi: EwmaPseudoDecibel::new(ewma_weight, initial_rssi),
            ewma_snr: EwmaPseudoDecibel::new(ewma_weight, initial_snr),
            rssi_velocity: 0,
        }
    }
    pub fn update_with_new_measurement(&mut self, rssi: PseudoDecibel, snr: PseudoDecibel) {
        let prev_rssi = self.ewma_rssi.get();
        self.ewma_rssi.update_average(rssi);
        self.ewma_snr.update_average(snr);
        self.rssi_velocity =
            match calculate_pseudodecibel_velocity(vec![prev_rssi, self.ewma_rssi.get()]) {
                Ok(velocity) => velocity,
                Err(e) => {
                    error!("Failed to update SignalData velocity: {:?}", e);
                    self.rssi_velocity
                }
            };
    }
}

/// Aggregated information about the current BSS's connection quality, used for evaluation.
#[derive(Clone, Debug)]
pub struct BssQualityData {
    pub signal_data: SignalData,
    pub channel: client_types::WlanChan,
    // TX and RX rate, respectively.
    pub phy_rates: (u32, u32),
    // Connection data  of past successful connections to this BSS.
    pub past_connections_list: PastConnectionList,
}

impl BssQualityData {
    pub fn new(
        signal_data: SignalData,
        channel: client_types::WlanChan,
        past_connections_list: PastConnectionList,
    ) -> Self {
        BssQualityData {
            signal_data: signal_data,
            channel: channel,
            phy_rates: (0, 0),
            past_connections_list: past_connections_list,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum RoamReason {
    SuboptimalSignal,
}

/// Returns scoring information for a particular BSS
pub fn evaluate_current_bss(bss: BssQualityData) -> (u8, Vec<RoamReason>) {
    let signal_score = score_signal_data(bss.signal_data);
    let mut roam_reasons: Vec<RoamReason> = vec![];

    // Add RoamReasons based on the raw signal score
    // TODO(haydennix): Add more RoamReasons for different score ranges
    match signal_score {
        u8::MIN..=SUBOPTIMAL_SIGNAL_THRESHOLD => {
            roam_reasons.push(RoamReason::SuboptimalSignal);
        }
        _ => {}
    }

    // TODO(haydennix): Adjust score based on system requirements,  user intent, etc.
    return (signal_score, roam_reasons);
}

/// Scoring table from go/tq-bss-eval-design.
fn score_signal_data(data: SignalData) -> u8 {
    let rssi_velocity_score = match data.ewma_rssi.get() {
        PseudoDecibel::MIN..=-81 => match data.rssi_velocity {
            PseudoDecibel::MIN..=-4 => 0,
            -3 => 0,
            -2 => 0,
            -1..=1 => 0,
            2 => 20,
            3 => 18,
            4..=PseudoDecibel::MAX => 10,
        },
        -80..=-76 => match data.rssi_velocity {
            PseudoDecibel::MIN..=-4 => 0,
            -3 => 0,
            -2 => 0,
            -1..=1 => 15,
            2 => 28,
            3 => 25,
            4..=PseudoDecibel::MAX => 15,
        },
        -75..=-71 => match data.rssi_velocity {
            PseudoDecibel::MIN..=-4 => 0,
            -3 => 5,
            -2 => 15,
            -1..=1 => 30,
            2 => 45,
            3 => 38,
            4..=PseudoDecibel::MAX => 25,
        },
        -70..=-66 => match data.rssi_velocity {
            PseudoDecibel::MIN..=-4 => 10,
            -3 => 18,
            -2 => 30,
            -1..=1 => 48,
            2 => 60,
            3 => 50,
            4..=PseudoDecibel::MAX => 38,
        },
        -65..=-61 => match data.rssi_velocity {
            PseudoDecibel::MIN..=-4 => 20,
            -3 => 30,
            -2 => 45,
            -1..=1 => 70,
            2 => 75,
            3 => 60,
            4..=PseudoDecibel::MAX => 55,
        },
        -60..=-56 => match data.rssi_velocity {
            PseudoDecibel::MIN..=-4 => 40,
            -3 => 50,
            -2 => 63,
            -1..=1 => 85,
            2 => 85,
            3 => 70,
            4..=PseudoDecibel::MAX => 65,
        },
        -55..=-51 => match data.rssi_velocity {
            PseudoDecibel::MIN..=-4 => 55,
            -3 => 65,
            -2 => 75,
            -1..=1 => 95,
            2 => 90,
            3 => 80,
            4..=PseudoDecibel::MAX => 75,
        },
        -50..=PseudoDecibel::MAX => match data.rssi_velocity {
            PseudoDecibel::MIN..=-4 => 60,
            -3 => 70,
            -2 => 80,
            -1..=1 => 100,
            2 => 95,
            3 => 90,
            4..=PseudoDecibel::MAX => 80,
        },
    };

    let snr_score = match data.ewma_snr.get() {
        PseudoDecibel::MIN..=10 => 0,
        11..=15 => 15,
        16..=20 => 37,
        21..=25 => 53,
        26..=30 => 68,
        31..=35 => 80,
        36..=40 => 95,
        41..=PseudoDecibel::MAX => 100,
    };

    return ((rssi_velocity_score as f32 * RSSI_AND_VELOCITY_SCORE_WEIGHT)
        + (snr_score as f32 * SNR_SCORE_WEIGHT)) as u8;
}

#[cfg(test)]
mod test {
    use {
        super::*,
        test_util::{assert_gt, assert_lt},
        wlan_common::channel,
    };

    #[fuchsia::test]
    fn test_update_with_new_measurements() {
        let mut signal_data = SignalData::new(-40, 30, EWMA_SMOOTHING_FACTOR);
        signal_data.update_with_new_measurement(-60, 15);
        assert_lt!(signal_data.ewma_rssi.get(), -40);
        assert_gt!(signal_data.ewma_rssi.get(), -60);
        assert_lt!(signal_data.ewma_snr.get(), 30);
        assert_gt!(signal_data.ewma_snr.get(), 15);
        assert_lt!(signal_data.rssi_velocity, 0);
    }

    #[fuchsia::test]
    fn test_weights_sum_to_one() {
        assert_eq!(RSSI_AND_VELOCITY_SCORE_WEIGHT + SNR_SCORE_WEIGHT, 1.0);
    }

    #[fuchsia::test]
    fn test_trivial_signal_data_scores() {
        let strong_clear_stable_signal = SignalData::new(-55, 35, 10);
        let weak_clear_stable_signal = SignalData::new(-80, 35, 10);

        let mut strong_clear_degrading_signal = SignalData::new(-55, 35, 10);
        strong_clear_degrading_signal.rssi_velocity = -3;
        let mut weak_clear_improving_signal = SignalData::new(-80, 35, 10);
        weak_clear_improving_signal.rssi_velocity = 2;

        let strong_noisy_stable_signal = SignalData::new(-55, 15, 10);

        assert_gt!(
            score_signal_data(strong_clear_stable_signal),
            score_signal_data(weak_clear_stable_signal)
        );

        assert_gt!(
            score_signal_data(strong_clear_stable_signal),
            score_signal_data(strong_clear_degrading_signal)
        );

        assert_gt!(
            score_signal_data(weak_clear_improving_signal),
            score_signal_data(weak_clear_stable_signal)
        );

        assert_gt!(
            score_signal_data(strong_clear_stable_signal),
            score_signal_data(strong_noisy_stable_signal)
        )
    }

    #[fuchsia::test]
    fn test_evaluate_trivial_roam_reasons() {
        // Low RSSI and SNR
        let weak_signal_bss = BssQualityData::new(
            SignalData::new(-90, 5, 10),
            channel::Channel::new(11, channel::Cbw::Cbw20),
            PastConnectionList::new(),
        );
        let (_, roam_reasons) = evaluate_current_bss(weak_signal_bss);
        assert!(roam_reasons.iter().any(|&r| r == RoamReason::SuboptimalSignal));

        // Moderate RSSI, low SNR
        let low_snr_bss = BssQualityData::new(
            SignalData::new(-65, 5, 10),
            channel::Channel::new(11, channel::Cbw::Cbw20),
            PastConnectionList::new(),
        );
        let (_, roam_reasons) = evaluate_current_bss(low_snr_bss);
        assert!(roam_reasons.iter().any(|&r| r == RoamReason::SuboptimalSignal));
    }
}
