// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::mac::{Aid, MAX_AID};

const TIM_BITMAP_LEN: usize = (MAX_AID as usize + 1) / 8;

/// Represents a complete (that is, not partial) traffic indication bitmap. Consists of 2008 bits
/// (i.e. 251 bytes).
pub struct TrafficIndicationMap([u8; TIM_BITMAP_LEN]);

impl TrafficIndicationMap {
    pub fn new() -> Self {
        Self([0; TIM_BITMAP_LEN])
    }

    /// Sets a given AID to have buffered traffic (or not) in the traffic indication map.
    ///
    /// There is no special handling for AID 0 (i.e. for group traffic), we do not set it as the TIM
    /// header will indicate it, as per IEEE Std 802.11-2016, 9.4.2.6: The bit numbered 0 in the
    /// traffic indication virtual bitmap need not be included in the Partial Virtual Bitmap field
    /// even if that bit is set.
    pub fn set_traffic_buffered(&mut self, aid: Aid, buffered: bool) {
        let octet = aid as usize / 8;
        let bit_offset = aid as usize % 8;
        self.0[octet] = (self.0[octet] & !(1 << bit_offset)) | ((buffered as u8) << bit_offset)
    }

    /// IEEE Std 802.11-2016, 9.4.2.6: N1 is the largest even number such that bits numbered 1 to
    /// (N1 * 8) - 1 in the traffic indication virtual bitmap are all 0.
    fn n1(&self) -> usize {
        // TODO(fxbug.dev/40099): Consider using u64 and CTZ instead of checking all the bytes individually.
        for (i, b) in self.0.iter().enumerate() {
            if *b != 0 {
                // Round down to the nearest even number.
                return i & !0b1;
            }
        }

        // IEEE Std 802.11-2016, 9.4.2.6: In the event that all bits other than bit 0 in the traffic
        // indication virtual bitmap are 0, [...], the Bitmap Offset subfield is 0, [...].
        0
    }

    /// IEEE Std 802.11-2016, 9.4.2.6: N2 is the smallest number such that bits numbered
    /// (N2 + 1) * 8 to 2007 in the traffic indication virtual bitmap are all 0.
    fn n2(&self) -> usize {
        // TODO(fxbug.dev/40099): Consider using u64 and CLZ instead of checking all the bytes individually.
        for (i, b) in self.0.iter().enumerate().rev() {
            if *b != 0 {
                return i;
            }
        }

        0
    }

    /// Creates a view into TrafficIndicationMap suitable for use in a TIM IE.
    pub fn make_partial_virtual_bitmap(&self) -> (u8, &[u8]) {
        // Invariant: n1 <= n2. This is guaranteed as n1 scans from the front and n2 scans from the
        // back, so the range of n1..n2 is always non-decreasing.
        let n1 = self.n1();
        let n2 = self.n2();

        // IEEE Std 802.11-2016, 9.4.2.6: In the event that all bits other than bit 0 in the traffic
        // indication virtual bitmap are 0, the Partial Virtual Bitmap field is encoded as a single
        // octet equal to 0, [...].
        //
        // We always have at least one item in the bitmap here, even if there are no items in the
        // traffic indication bitmap: both n1 and n2 will be 0, which will take the first octet from
        // the bitmap (which will always be 0).
        ((n1 / 2) as u8, &self.0[n1..n2 + 1])
    }
}

pub fn is_traffic_buffered(offset: u8, bitmap: &[u8], aid: Aid) -> bool {
    // IEEE 802.11-2016 Std, 9.4.2.6: When dot11MultiBSSIDActivated is false [...] In this case, the
    // Bitmap Offset subfield value contains the number N1/2.
    let n1 = offset as usize * 2;
    let octet = aid as usize / 8;

    let carries_aid = n1 <= octet && octet < bitmap.len() + n1;
    carries_aid && bitmap[octet - n1] & (1 << (aid as usize % 8)) != 0
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn zero_offset() {
        let bitmap = &[0b0010010][..];

        assert!(!is_traffic_buffered(0, bitmap, 0));
        assert!(is_traffic_buffered(0, bitmap, 1));
        assert!(!is_traffic_buffered(0, bitmap, 2));
        assert!(!is_traffic_buffered(0, bitmap, 3));
        assert!(is_traffic_buffered(0, bitmap, 4));
        assert!(!is_traffic_buffered(0, bitmap, 5));
        assert!(!is_traffic_buffered(0, bitmap, 6));
        assert!(!is_traffic_buffered(0, bitmap, 7));
        assert!(!is_traffic_buffered(0, bitmap, 100));
    }

    #[test]
    fn with_offset() {
        let bitmap = &[0b0010010][..];

        // Offset of 1 means "skip 16 bits"
        assert!(!is_traffic_buffered(1, bitmap, 15));
        assert!(!is_traffic_buffered(1, bitmap, 16));
        assert!(is_traffic_buffered(1, bitmap, 17));
        assert!(!is_traffic_buffered(1, bitmap, 18));
        assert!(!is_traffic_buffered(1, bitmap, 19));
        assert!(is_traffic_buffered(1, bitmap, 20));
        assert!(!is_traffic_buffered(1, bitmap, 21));
        assert!(!is_traffic_buffered(1, bitmap, 22));
        assert!(!is_traffic_buffered(1, bitmap, 100));
    }

    #[test]
    fn traffic_indication_map_set_traffic_buffered() {
        let mut tim = TrafficIndicationMap::new();
        tim.set_traffic_buffered(12, true);
        let mut expected_bitmap = [0; TIM_BITMAP_LEN];
        expected_bitmap[1] = 0b00010000;
        assert_eq!(&tim.0[..], &expected_bitmap[..]);
        assert_eq!(tim.n1(), 0);
        assert_eq!(tim.n2(), 1);
    }

    #[test]
    fn traffic_indication_map_set_traffic_buffered_multiple_octets() {
        let mut tim = TrafficIndicationMap::new();
        tim.set_traffic_buffered(12, true);
        tim.set_traffic_buffered(35, true);
        let mut expected_bitmap = [0; TIM_BITMAP_LEN];
        expected_bitmap[1] = 0b00010000;
        expected_bitmap[4] = 0b00001000;
        assert_eq!(&tim.0[..], &expected_bitmap[..]);
        assert_eq!(tim.n1(), 0);
        assert_eq!(tim.n2(), 4);
    }

    #[test]
    fn from_partial_virtual_bitmap() {
        let mut tim = TrafficIndicationMap::new();
        tim.set_traffic_buffered(12, true);
        let (offset, bitmap) = tim.make_partial_virtual_bitmap();
        assert_eq!(offset, 0);
        assert_eq!(bitmap, &[0b00000000, 0b00010000]);
        assert!(is_traffic_buffered(offset, bitmap, 12));
    }

    #[test]
    fn from_partial_virtual_bitmap_bigger_offset() {
        let mut tim = TrafficIndicationMap::new();
        tim.set_traffic_buffered(49, true);
        let (offset, bitmap) = tim.make_partial_virtual_bitmap();
        assert_eq!(offset, 3);
        assert_eq!(bitmap, &[0b00000010]);
        assert!(is_traffic_buffered(offset, bitmap, 49));
    }

    #[test]
    fn from_partial_virtual_bitmap_multiple_octets() {
        let mut tim = TrafficIndicationMap::new();
        tim.set_traffic_buffered(12, true);
        tim.set_traffic_buffered(35, true);
        let (offset, bitmap) = tim.make_partial_virtual_bitmap();
        assert_eq!(offset, 0);
        assert_eq!(bitmap, &[0b00000000, 0b00010000, 0b00000000, 0b00000000, 0b00001000]);
        assert!(is_traffic_buffered(offset, bitmap, 12));
        assert!(is_traffic_buffered(offset, bitmap, 35));
    }

    #[test]
    fn from_partial_virtual_bitmap_no_traffic() {
        let tim = TrafficIndicationMap::new();
        let (offset, bitmap) = tim.make_partial_virtual_bitmap();
        assert_eq!(offset, 0);
        assert_eq!(bitmap, &[0b00000000]);
    }
}
