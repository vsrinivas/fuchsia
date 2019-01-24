// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;

type MacAddr = [u8; 6];
const MAC_ADDR_LEN: usize = 6;

const SEQ_START_NUM: SequenceNum = 0;
type SnsEntryHash = u64;
type SequenceNum = u32;

/// IEEE Std 802.11-2016, 10.3.2.11.2, 10.3.2.11.3
/// A specific Sequence Number Space such as SNS1, SNS2, etc.
/// A STA owns multiple of such SNS maps, each of which holds
/// sequence numbers for its peers.
struct SnsMap {
    inner: HashMap<SnsEntryHash, SequenceNum>,
    modulo_divisor: SequenceNum,
}

impl SnsMap {
    pub fn new(modulo_divisor: SequenceNum) -> Self {
        Self {
            inner: HashMap::new(),
            modulo_divisor,
        }
    }

    pub fn next(&mut self, entry: &SnsEntryHash) -> SequenceNum {
        match self.inner.get_mut(entry) {
            None => {
                self.inner.insert(*entry, SEQ_START_NUM);
                SEQ_START_NUM
            }
            Some(seq) => {
                *seq = (*seq + 1) % self.modulo_divisor;
                *seq
            }
        }
    }
}

/// Manages all SNS for a STA.
pub struct SequenceManager {
    sns_map1024: SnsMap,
    sns_map4096: SnsMap,
}

impl SequenceManager {
    pub fn new() -> Self {
        Self {
            sns_map1024: SnsMap::new(1024),
            sns_map4096: SnsMap::new(4096),
        }
    }

    pub fn next_sns1(&mut self, sta_addr: &MacAddr) -> SequenceNum {
        self.sns_map4096.next(&hash_mac_addr(sta_addr))
    }

    pub fn next_sns2(&mut self, sta_addr: &MacAddr, tid: u16) -> SequenceNum {
        // IEEE Std 802.11-2016, 9.2.4.5.2
        // TID is 4 bit long.
        // Insert 0x10 to generate a unique hash.
        let hash = hash_mac_addr(sta_addr) + ((0x10 | tid as SnsEntryHash) << MAC_ADDR_LEN);
        self.sns_map4096.next(&hash)
    }

    // Sns3 optional

    pub fn next_sns4(&mut self, sta_addr: &MacAddr, aci: u8) -> SequenceNum {
        // IEEE Std 802.11-2016, 9.2.4.4.2
        // ACI subfield is 2 bit long.
        // Insert 0x20 to generate a unique hash.
        let hash = hash_mac_addr(sta_addr) + ((0x20 | aci as SnsEntryHash) << MAC_ADDR_LEN);
        self.sns_map1024.next(&hash)
    }

    pub fn next_sns5(&mut self) -> SequenceNum {
        // Arbitrary value by spec. Increment to assist debugging.
        const HASH: SnsEntryHash = 0x01 << (MAC_ADDR_LEN + 1);
        self.sns_map4096.next(&HASH)
    }
}

fn hash_mac_addr(mac_addr: &MacAddr) -> SnsEntryHash {
    return mac_addr.iter().fold(0, |acc, x| (acc << 8) | (*x as u64));
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sns1_next() {
        let mut seq_mgr = SequenceManager::new();

        for i in 0..4095 {
            let seq_num = seq_mgr.next_sns1(&[1; 6]);
            assert_eq!(i, seq_num);
        }

        let seq_num = seq_mgr.next_sns1(&[1; 6]);
        assert_eq!(4095, seq_num);

        let seq_num = seq_mgr.next_sns1(&[1; 6]);
        assert_eq!(0, seq_num);
    }

    #[test]
    fn sns1_next_multiple_peers() {
        const FIRST_PEER: [u8; 6] = [1; 6];
        const SECOND_PEER: [u8; 6] = [2; 6];
        let mut seq_mgr = SequenceManager::new();

        seq_mgr.next_sns1(&FIRST_PEER);
        seq_mgr.next_sns1(&FIRST_PEER);
        let seq_num = seq_mgr.next_sns1(&FIRST_PEER);
        assert_eq!(2, seq_num);

        seq_mgr.next_sns1(&SECOND_PEER);
        let seq_num = seq_mgr.next_sns1(&SECOND_PEER);
        assert_eq!(1, seq_num);

        let seq_num = seq_mgr.next_sns1(&FIRST_PEER);
        assert_eq!(3, seq_num);
    }

    #[test]
    fn sns2_next_multiple_tids() {
        let mut seq_mgr = SequenceManager::new();

        seq_mgr.next_sns2(&[1; 6], 0);
        seq_mgr.next_sns2(&[1; 6], 0);
        let seq_num = seq_mgr.next_sns2(&[1; 6], 0);
        assert_eq!(2, seq_num);

        seq_mgr.next_sns2(&[1; 6], 1);
        let seq_num = seq_mgr.next_sns2(&[1; 6], 1);
        assert_eq!(1, seq_num);

        let seq_num = seq_mgr.next_sns2(&[1; 6], 0);
        assert_eq!(3, seq_num);
    }

    #[test]
    fn sns4_next_multiple_acis() {
        let mut seq_mgr = SequenceManager::new();

        seq_mgr.next_sns4(&[1; 6], 0);
        seq_mgr.next_sns4(&[1; 6], 0);
        let seq_num = seq_mgr.next_sns4(&[1; 6], 0);
        assert_eq!(2, seq_num);

        seq_mgr.next_sns4(&[1; 6], 1);
        let seq_num = seq_mgr.next_sns4(&[1; 6], 1);
        assert_eq!(1, seq_num);

        let seq_num = seq_mgr.next_sns4(&[1; 6], 0);
        assert_eq!(3, seq_num);
    }

    #[test]
    fn mixed_sns_next() {
        let mut seq_mgr = SequenceManager::new();

        seq_mgr.next_sns1(&[1; 6]);
        seq_mgr.next_sns1(&[1; 6]);
        let seq_num = seq_mgr.next_sns1(&[1; 6]);
        assert_eq!(2, seq_num);

        seq_mgr.next_sns2(&[1; 6], 0);
        let seq_num = seq_mgr.next_sns2(&[1; 6], 0);
        assert_eq!(1, seq_num);

        let seq_num = seq_mgr.next_sns4(&[1; 6], 3);
        assert_eq!(0, seq_num);
    }
}
