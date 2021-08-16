// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    rand::seq::SliceRandom,
    wlan_common::tx_vector::{TxVecIdx, MAX_VALID_IDX, START_IDX},
};

const NUM_PROBE_SEQUENCE: usize = 8;
const SEQUENCE_LENGTH: usize = 1 + (MAX_VALID_IDX - START_IDX) as usize;

pub struct ProbeEntry {
    sequence_idx: usize,
    probe_idx: usize,
}

impl Default for ProbeEntry {
    fn default() -> Self {
        Self { sequence_idx: NUM_PROBE_SEQUENCE - 1, probe_idx: SEQUENCE_LENGTH - 1 }
    }
}

impl ProbeEntry {
    pub fn cycle_complete(&self) -> bool {
        self.probe_idx == SEQUENCE_LENGTH - 1
    }
}

pub type ProbeTable = [[TxVecIdx; SEQUENCE_LENGTH]; NUM_PROBE_SEQUENCE];

pub struct ProbeSequence {
    probe_table: ProbeTable,
}

impl ProbeSequence {
    pub fn sequential() -> Self {
        // This unwrap is safe, since START_IDX is const and always a valid TxVecIdx.
        let default_idx = TxVecIdx::new(START_IDX).unwrap();
        let mut probe_table = [[default_idx; SEQUENCE_LENGTH]; NUM_PROBE_SEQUENCE];
        for i in 0..NUM_PROBE_SEQUENCE {
            for j in START_IDX..=MAX_VALID_IDX {
                // The unwrap here is safe because the range is exactly the set of valid TxVecIdx.
                probe_table[i][(j - START_IDX) as usize] = TxVecIdx::new(j).unwrap();
            }
        }
        Self { probe_table }
    }

    pub fn random_new() -> Self {
        let mut rng = rand::thread_rng();
        // This unwrap is safe, since START_IDX is const and always a valid TxVecIdx.
        let default_idx = TxVecIdx::new(START_IDX).unwrap();
        let mut probe_table = [[default_idx; SEQUENCE_LENGTH]; NUM_PROBE_SEQUENCE];
        for i in 0..NUM_PROBE_SEQUENCE {
            for j in START_IDX..=MAX_VALID_IDX {
                // This unwrap is safe, since the range we're iterating over is exactly the set of
                // valid TxVecIdx values as defined by the START_IDX and MAX_VALID_IDX consts.
                probe_table[i][(j - START_IDX) as usize] = TxVecIdx::new(j).unwrap();
            }
            (&mut probe_table[i][..]).shuffle(&mut rng);
        }
        Self { probe_table }
    }

    pub fn next(&self, entry: &mut ProbeEntry) -> TxVecIdx {
        entry.probe_idx = (entry.probe_idx + 1) % SEQUENCE_LENGTH;
        if entry.probe_idx == 0 {
            entry.sequence_idx = (entry.sequence_idx + 1) % NUM_PROBE_SEQUENCE;
        }
        self.probe_table[entry.sequence_idx][entry.probe_idx]
    }
}

#[cfg(test)]
mod tests {
    use {super::*, std::collections::HashSet};

    #[test]
    fn random_new() {
        let probe_seq = ProbeSequence::random_new();
        // Verify that each probe sequence contains all possible TxVecIdx values.
        for i in 0..NUM_PROBE_SEQUENCE {
            let seq = &probe_seq.probe_table[i];
            let mut seen_tx_idx = HashSet::new();
            for tx_vector_idx in seq {
                seen_tx_idx.insert(*tx_vector_idx);
            }
            assert_eq!(seen_tx_idx.len(), (MAX_VALID_IDX - START_IDX + 1) as usize);
        }
    }

    #[test]
    fn probe_entries() {
        let probe_seq = ProbeSequence::random_new();
        let mut entry = ProbeEntry::default();
        let mut seen_tx_idx = HashSet::new();
        for _ in 0..SEQUENCE_LENGTH - 1 {
            seen_tx_idx.insert(probe_seq.next(&mut entry));
            assert!(!entry.cycle_complete());
        }
        // After the last sequence value, we should see the cycle is complete.
        seen_tx_idx.insert(probe_seq.next(&mut entry));
        assert!(entry.cycle_complete());
        assert_eq!(seen_tx_idx.len(), (MAX_VALID_IDX - START_IDX + 1) as usize);

        // Now we should start seeing duplicate values.
        assert!(seen_tx_idx.contains(&probe_seq.next(&mut entry)));
        assert!(!entry.cycle_complete());
    }
}
