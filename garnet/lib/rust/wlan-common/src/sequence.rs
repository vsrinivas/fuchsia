// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;

type MacAddr = [u8; 6];

const SEQ_START_NUM: SequenceNum = 1;
pub type SequenceNum = u32;

/// IEEE Std 802.11-2016, 10.3.2.11.2, 10.3.2.11.3
/// A specific Sequence Number Space such as SNS1, SNS2, etc.
/// A STA owns multiple of such SNS maps, each of which holds
/// sequence numbers for its peers.
struct SnsMap<K> {
    inner: HashMap<K, SequenceNum>,
    modulo_divisor: SequenceNum,
}

impl<K: std::hash::Hash + Eq + Clone> SnsMap<K> {
    pub fn new(modulo_divisor: SequenceNum) -> Self {
        Self { inner: HashMap::new(), modulo_divisor }
    }

    pub fn next(&mut self, key: &K) -> SequenceNum {
        match self.inner.get_mut(key) {
            None => {
                self.inner.insert(key.clone(), SEQ_START_NUM);
                SEQ_START_NUM
            }
            Some(seq) => {
                *seq = (*seq + 1) % self.modulo_divisor;
                *seq
            }
        }
    }
}

#[derive(Hash, PartialEq, Eq, Clone)]
struct Sns2Key {
    sta_addr: MacAddr,
    tid: u16,
}

#[derive(Hash, PartialEq, Eq, Clone)]
struct Sns4Key {
    sta_addr: MacAddr,
    aci: u8,
}

/// Manages all SNS for a STA.
pub struct SequenceManager {
    sns1: SnsMap<MacAddr>,
    sns2: SnsMap<Sns2Key>,
    sns4: SnsMap<Sns4Key>,
    sns5: u32,
}

impl SequenceManager {
    pub fn new() -> Self {
        Self {
            sns1: SnsMap::new(4096),
            sns2: SnsMap::new(4096),
            sns4: SnsMap::new(1024),
            sns5: SEQ_START_NUM,
        }
    }

    pub fn next_sns1(&mut self, sta_addr: &MacAddr) -> SequenceNum {
        self.sns1.next(sta_addr)
    }

    pub fn next_sns2(&mut self, sta_addr: &MacAddr, tid: u16) -> SequenceNum {
        self.sns2.next(&Sns2Key { sta_addr: sta_addr.clone(), tid })
    }

    // Sns3 optional

    pub fn next_sns4(&mut self, sta_addr: &MacAddr, aci: u8) -> SequenceNum {
        self.sns4.next(&Sns4Key { sta_addr: sta_addr.clone(), aci })
    }

    pub fn next_sns5(&mut self) -> SequenceNum {
        // Arbitrary value by spec. Increment to assist debugging.
        self.sns5 = (self.sns5 + 1) % 4096;
        self.sns5
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sns1_next() {
        let mut seq_mgr = SequenceManager::new();

        for i in 0..4095 {
            let seq_num = seq_mgr.next_sns1(&[1; 6]);
            assert_eq!(i + 1, seq_num);
        }

        let seq_num = seq_mgr.next_sns1(&[1; 6]);
        assert_eq!(0, seq_num); // wrapped

        let seq_num = seq_mgr.next_sns1(&[1; 6]);
        assert_eq!(0 + 1, seq_num);
    }

    #[test]
    fn sns1_next_multiple_peers() {
        const FIRST_PEER: [u8; 6] = [1; 6];
        const SECOND_PEER: [u8; 6] = [2; 6];
        let mut seq_mgr = SequenceManager::new();

        seq_mgr.next_sns1(&FIRST_PEER);
        seq_mgr.next_sns1(&FIRST_PEER);
        let seq_num = seq_mgr.next_sns1(&FIRST_PEER);
        assert_eq!(3, seq_num);

        seq_mgr.next_sns1(&SECOND_PEER);
        let seq_num = seq_mgr.next_sns1(&SECOND_PEER);
        assert_eq!(2, seq_num);

        let seq_num = seq_mgr.next_sns1(&FIRST_PEER);
        assert_eq!(4, seq_num);
    }

    #[test]
    fn sns2_next_multiple_tids() {
        let mut seq_mgr = SequenceManager::new();

        seq_mgr.next_sns2(&[1; 6], 0);
        seq_mgr.next_sns2(&[1; 6], 0);
        let seq_num = seq_mgr.next_sns2(&[1; 6], 0);
        assert_eq!(3, seq_num);

        seq_mgr.next_sns2(&[1; 6], 1);
        let seq_num = seq_mgr.next_sns2(&[1; 6], 1);
        assert_eq!(2, seq_num);

        let seq_num = seq_mgr.next_sns2(&[1; 6], 0);
        assert_eq!(4, seq_num);
    }

    #[test]
    fn sns4_next_multiple_acis() {
        let mut seq_mgr = SequenceManager::new();

        seq_mgr.next_sns4(&[1; 6], 0);
        seq_mgr.next_sns4(&[1; 6], 0);
        let seq_num = seq_mgr.next_sns4(&[1; 6], 0);
        assert_eq!(3, seq_num);

        seq_mgr.next_sns4(&[1; 6], 1);
        let seq_num = seq_mgr.next_sns4(&[1; 6], 1);
        assert_eq!(2, seq_num);

        let seq_num = seq_mgr.next_sns4(&[1; 6], 0);
        assert_eq!(4, seq_num);
    }

    #[test]
    fn mixed_sns_next() {
        let mut seq_mgr = SequenceManager::new();

        seq_mgr.next_sns1(&[1; 6]);
        seq_mgr.next_sns1(&[1; 6]);
        let seq_num = seq_mgr.next_sns1(&[1; 6]);
        assert_eq!(3, seq_num);

        seq_mgr.next_sns2(&[1; 6], 0);
        let seq_num = seq_mgr.next_sns2(&[1; 6], 0);
        assert_eq!(2, seq_num);

        let seq_num = seq_mgr.next_sns4(&[1; 6], 3);
        assert_eq!(1, seq_num);
    }
}
